/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   FreeRTOS application tasks for Edge AI ALPR pipeline.
  *
  *  Implements the full ALPR processing pipeline:
  *
  *  1. CameraCaptureTask  — Manages DCMIPP DMA, provides frames to pipeline
  *  2. DetectionTask      — Runs Tiny-YOLO on NPU for license plate detection
  *  3. OCRTask            — Runs OCR model on NPU for character recognition
  *  4. CryptoTransmitTask — Encrypts plate text (mbedTLS) + sends via LwIP
  *  5. EthInputTask       — Handles incoming Ethernet frames (LwIP RX path)
  *  6. EthLinkTask        — Polls PHY link state, manages netif up/down
  *  7. DisplayTask        — LCD overlay with detection boxes + plate text
  *  8. WatchdogTask       — Feeds IWDG, monitors task health
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_tasks.h"
#include "bsp_lcd.h"
#include "bsp_camera.h"
#include "bsp_eth.h"
#include "ethernetif.h"
#include "main.h"

#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>

/* Local bring-up switches ---------------------------------------------------*/
#define APP_TEST_GENERATE_FRAME_ON_CAMERA_TIMEOUT   1U
#define APP_TEST_OVERLAY_ENABLED                    1U
#define APP_DISPLAY_TEST_PERIOD_MS                  100U

#define RGB565_BLACK                                0x0000U
#define RGB565_WHITE                                0xFFFFU
#define RGB565_RED                                  0xF800U
#define RGB565_GREEN                                0x07E0U
#define RGB565_BLUE                                 0x001FU
#define RGB565_YELLOW                               0xFFE0U
#define RGB565_CYAN                                 0x07FFU
#define RGB565_MAGENTA                              0xF81FU
#define RGB565_GRAY                                 0x8410U

/* External declarations -----------------------------------------------------*/
extern struct netif gnetif;
extern void LwIP_Init(void);
extern void LwIP_Process(void);
extern osStatus_t ethernetif_wait_rx(uint32_t timeout_ms);
extern IWDG_HandleTypeDef hiwdg;

/* External peripheral handles from main.c */
/* NOTE: Uncomment these when HASH/PKA modules are enabled in stm32n6xx_hal_conf.h */
/* extern HASH_HandleTypeDef hhash; */   /* Hardware SHA-256 (CubeMX) */
/* extern PKA_HandleTypeDef  hpka; */    /* Public Key Accelerator    */

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GLOBAL SYNC OBJECTS & SHARED DATA                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Task handles */
osThreadId_t CameraCaptureTaskHandle  = NULL;
osThreadId_t DetectionTaskHandle      = NULL;
osThreadId_t OCRTaskHandle            = NULL;
osThreadId_t CryptoTransmitTaskHandle = NULL;
osThreadId_t EthInputTaskHandle       = NULL;
osThreadId_t EthLinkTaskHandle        = NULL;
osThreadId_t DisplayTaskHandle        = NULL;
osThreadId_t WatchdogTaskHandle       = NULL;

/* Message queues — pipeline interconnects */
osMessageQueueId_t FrameQueue       = NULL;  /* Camera → Detection   */
osMessageQueueId_t DetectionQueue   = NULL;  /* Detection → OCR      */
osMessageQueueId_t PlateResultQueue = NULL;  /* OCR → CryptoTransmit */

/* Shared display overlay data */
DisplayOverlay_t SharedOverlay;
osMutexId_t      OverlayMutex = NULL;

/* Camera frame-ready semaphore (signalled from DCMIPP ISR) */
static osSemaphoreId_t camFrameSemaphore = NULL;

/* Frame counter */
static volatile uint32_t frame_counter = 0;
static volatile uint32_t camera_last_frame_tick = 0;
static volatile uint32_t camera_timeout_count = 0;
static volatile uint8_t camera_capture_started = 0;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  PRIVATE FUNCTION PROTOTYPES                                               */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void CameraCaptureTask(void *argument);
static void DetectionTask(void *argument);
static void OCRTask(void *argument);
static void CryptoTransmitTask(void *argument);
static void EthInputTask(void *argument);
static void EthLinkTask(void *argument);
static void DisplayTask(void *argument);
static void WatchdogTask(void *argument);

static void Camera_FrameReadyCb(BSP_Camera_PipeType_t pipe, uint32_t fb_addr);
static void Display_DrawBringupFrame(const DisplayOverlay_t *overlay);
static void Display_DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint16_t color);
static void Display_DrawFrameCounter(uint32_t frame_id);
static void Display_ClearBackBuffer(uint16_t color);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK ATTRIBUTES                                                           */
/* ═══════════════════════════════════════════════════════════════════════════ */

static const osThreadAttr_t cameraCaptureTask_attr = {
  .name = "CamCapture", .priority = (osPriority_t)CAMERA_TASK_PRIO,
  .stack_size = CAMERA_TASK_STACK * 4,
};
static const osThreadAttr_t detectionTask_attr = {
  .name = "Detection", .priority = (osPriority_t)DETECTION_TASK_PRIO,
  .stack_size = DETECTION_TASK_STACK * 4,
};
static const osThreadAttr_t ocrTask_attr = {
  .name = "OCR", .priority = (osPriority_t)OCR_TASK_PRIO,
  .stack_size = OCR_TASK_STACK * 4,
};
static const osThreadAttr_t cryptoTxTask_attr = {
  .name = "CryptoTx", .priority = (osPriority_t)CRYPTO_TX_TASK_PRIO,
  .stack_size = CRYPTO_TX_TASK_STACK * 4,
};
static const osThreadAttr_t ethInputTask_attr = {
  .name = "EthInput", .priority = (osPriority_t)ETH_INPUT_TASK_PRIO,
  .stack_size = ETH_INPUT_TASK_STACK * 4,
};
static const osThreadAttr_t ethLinkTask_attr = {
  .name = "EthLink", .priority = (osPriority_t)ETH_LINK_TASK_PRIO,
  .stack_size = ETH_LINK_TASK_STACK * 4,
};
static const osThreadAttr_t displayTask_attr = {
  .name = "Display", .priority = (osPriority_t)DISPLAY_TASK_PRIO,
  .stack_size = DISPLAY_TASK_STACK * 4,
};
static const osThreadAttr_t watchdogTask_attr = {
  .name = "Watchdog", .priority = (osPriority_t)WATCHDOG_TASK_PRIO,
  .stack_size = WATCHDOG_TASK_STACK * 4,
};

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  PUBLIC API                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

void AppTasks_Init(void)
{
  /* ── 1. Initialize BSP Peripherals ────────────────────────────────────── */

  /* LCD — display startup screen */
  BSP_LCD_Init();
  BSP_LCD_Clear(LCD_LAYER_0, 0x001F);  /* Dark blue = "system booting" */

  /* Camera — configure DCMIPP pipes */
  BSP_Camera_Init();
  BSP_Camera_RegisterCallback(Camera_FrameReadyCb);

  /* ── 2. Create Synchronization Objects ────────────────────────────────── */

  /* Camera ISR → CameraCaptureTask */
  camFrameSemaphore = osSemaphoreNew(1, 0, NULL);

  /* Pipeline message queues */
  FrameQueue       = osMessageQueueNew(FRAME_QUEUE_DEPTH,
                                        sizeof(FrameMsg_t), NULL);
  DetectionQueue   = osMessageQueueNew(DETECTION_QUEUE_DEPTH,
                                        sizeof(DetectionMsg_t), NULL);
  PlateResultQueue = osMessageQueueNew(PLATE_QUEUE_DEPTH,
                                        sizeof(PlateResultMsg_t), NULL);

  /* Display overlay mutex */
  OverlayMutex = osMutexNew(NULL);
  memset(&SharedOverlay, 0, sizeof(SharedOverlay));

  /* ── 3. Initialize Network Stack ──────────────────────────────────────── */

  LwIP_Init();

  /* ── 4. Create Application Tasks ──────────────────────────────────────── */

  /* Real-time pipeline tasks */
  CameraCaptureTaskHandle  = osThreadNew(CameraCaptureTask,  NULL,
                                          &cameraCaptureTask_attr);
  DetectionTaskHandle      = osThreadNew(DetectionTask,      NULL,
                                          &detectionTask_attr);
  OCRTaskHandle            = osThreadNew(OCRTask,            NULL,
                                          &ocrTask_attr);
  CryptoTransmitTaskHandle = osThreadNew(CryptoTransmitTask, NULL,
                                          &cryptoTxTask_attr);

  /* Network tasks */
  EthInputTaskHandle = osThreadNew(EthInputTask, NULL, &ethInputTask_attr);
  EthLinkTaskHandle  = osThreadNew(EthLinkTask,  NULL, &ethLinkTask_attr);

  /* Support tasks */
  DisplayTaskHandle  = osThreadNew(DisplayTask,  NULL, &displayTask_attr);
  WatchdogTaskHandle = osThreadNew(WatchdogTask, NULL, &watchdogTask_attr);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 1: CAMERA CAPTURE                                                    */
/*  Priority: High                                                            */
/*  Role: Manages DCMIPP DMA capture. When a new AI frame is ready            */
/*        (signalled via ISR semaphore), packages a FrameMsg_t and puts       */
/*        it into the FrameQueue for the DetectionTask.                       */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void CameraCaptureTask(void *argument)
{
  (void)argument;
  FrameMsg_t frame_msg;

  /* Wait for sensor to stabilize */
  osDelay(100);

  /* Start DCMIPP capture on both pipes */
  BSP_Camera_StatusTypeDef preview_status;
  BSP_Camera_StatusTypeDef ai_status;

  preview_status = BSP_Camera_Start(CAMERA_PIPE_TYPE_PREVIEW, 0);
  ai_status = BSP_Camera_Start(CAMERA_PIPE_TYPE_AI, 0);
  camera_capture_started = ((preview_status == BSP_CAM_OK) &&
                            (ai_status == BSP_CAM_OK)) ? 1U : 0U;

  for (;;)
  {
    /* Wait for DCMIPP frame-complete interrupt (AI pipe) */
    if (osSemaphoreAcquire(camFrameSemaphore, 500) == osOK)
    {
      /* Build frame message */
      frame_msg.fb_addr      = BSP_Camera_GetFrameBuffer(CAMERA_PIPE_TYPE_AI);
      frame_msg.frame_id     = ++frame_counter;
      frame_msg.timestamp_ms = HAL_GetTick();
      camera_last_frame_tick = frame_msg.timestamp_ms;

      /* Enqueue to DetectionTask — non-blocking (drop frame if queue full) */
      osMessageQueuePut(FrameQueue, &frame_msg, 0, 0);
    }
    else
    {
      camera_timeout_count++;

#if APP_TEST_GENERATE_FRAME_ON_CAMERA_TIMEOUT
      frame_msg.fb_addr      = BSP_Camera_GetFrameBuffer(CAMERA_PIPE_TYPE_AI);
      frame_msg.frame_id     = ++frame_counter;
      frame_msg.timestamp_ms = HAL_GetTick();

      /* Synthetic heartbeat keeps Detection/Display testable without camera IRQs. */
      osMessageQueuePut(FrameQueue, &frame_msg, 0, 0);
#endif
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 2: DETECTION (Tiny-YOLO on NPU)                                      */
/*  Priority: AboveNormal                                                     */
/*  Role: Receives AI frames from CameraCaptureTask, runs Tiny-YOLO           */
/*        inference on the NPU, extracts bounding boxes, and forwards         */
/*        each detected plate ROI to the OCRTask.                             */
/*        Also updates the shared display overlay with detection results.     */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void DetectionTask(void *argument)
{
  (void)argument;
  FrameMsg_t     frame_msg;
  DetectionMsg_t det_msg;

  /* Detection results (local working buffer) */
  BBox_t   detections[MAX_DETECTIONS_PER_FRAME];
  uint8_t  num_detections;

  for (;;)
  {
    /* Block until a frame is available from CameraCaptureTask */
    if (osMessageQueueGet(FrameQueue, &frame_msg, NULL, osWaitForever) == osOK)
    {
      /*
       * ──────────────────────────────────────────────────────────────
       *  NPU INFERENCE — Tiny-YOLO License Plate Detection
       * ──────────────────────────────────────────────────────────────
       *
       * TODO: Integrate STM32Cube.AI generated code here.
       *
       * Steps:
       * 1. Prepare input tensor from frame_msg.fb_addr
       *    - The AI pipe outputs 320×320 RGB888
       *    - Normalize pixel values to [0,1] or [-1,1] per model spec
       *    - Input buffer: ai_input[0].data = (ai_float *)normalized_buf;
       *
       * 2. Run inference:
       *    ai_run(network_handle, ai_input, ai_output);
       *
       * 3. Post-process YOLO output:
       *    - Parse output tensor (anchors, grid cells)
       *    - Apply confidence threshold (e.g., > 0.5)
       *    - Non-Maximum Suppression (NMS) to remove duplicates
       *    - Fill detections[] array with resulting bounding boxes
       *
       * Example placeholder:
       */
      num_detections = 0;  /* Replace with actual YOLO post-processing */

#if APP_TEST_OVERLAY_ENABLED
      detections[0].x = (uint16_t)((frame_msg.frame_id * 7U) %
                                   (CAMERA_AI_WIDTH - 96U));
      detections[0].y = (uint16_t)(96U + ((frame_msg.frame_id * 3U) % 64U));
      detections[0].w = 96U;
      detections[0].h = 32U;
      detections[0].confidence = camera_capture_started ? 0.90f : 0.25f;
      num_detections = 1;
#endif

      /* ── Update shared display overlay ─────────────────────────── */
      if (osMutexAcquire(OverlayMutex, 10) == osOK)
      {
        SharedOverlay.frame_id       = frame_msg.frame_id;
        SharedOverlay.num_detections = num_detections;
        for (uint8_t i = 0; i < num_detections && i < MAX_DETECTIONS_PER_FRAME; i++)
        {
          SharedOverlay.detections[i] = detections[i];
          SharedOverlay.plate_texts[i][0] = '\0';  /* OCR fills this later */
        }
        osMutexRelease(OverlayMutex);
      }

      /* ── Forward each detected ROI to OCR task ─────────────────── */
      for (uint8_t i = 0; i < num_detections; i++)
      {
        det_msg.frame_id  = frame_msg.frame_id;
        det_msg.fb_addr   = frame_msg.fb_addr;
        det_msg.bbox      = detections[i];
        det_msg.roi_index = i;

        /* Non-blocking put — drop if OCR is backed up */
        osMessageQueuePut(DetectionQueue, &det_msg, 0, 0);
      }
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 3: OCR (Optical Character Recognition on NPU)                        */
/*  Priority: Normal                                                          */
/*  Role: Receives plate ROIs from DetectionTask, crops the region,           */
/*        runs OCR inference on the NPU, and forwards the recognized          */
/*        plate text to CryptoTransmitTask.                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void OCRTask(void *argument)
{
  (void)argument;
  DetectionMsg_t   det_msg;
  PlateResultMsg_t result_msg;

  for (;;)
  {
    /* Block until a detected plate ROI arrives */
    if (osMessageQueueGet(DetectionQueue, &det_msg, NULL, osWaitForever) == osOK)
    {
      /*
       * ──────────────────────────────────────────────────────────────
       *  PLATE CROP + OCR INFERENCE
       * ──────────────────────────────────────────────────────────────
       *
       * TODO: Integrate STM32Cube.AI OCR model here.
       *
       * Steps:
       * 1. Crop the plate region from det_msg.fb_addr using det_msg.bbox
       *    - Source: 320×320 RGB888 frame
       *    - Crop: bbox.x, bbox.y, bbox.w, bbox.h
       *    - Resize/pad to OCR model input size (e.g., 128×32 or 200×64)
       *
       * 2. Run OCR inference:
       *    ai_run(ocr_network_handle, ocr_input, ocr_output);
       *
       * 3. Decode OCR output:
       *    - CTC/attention decoder
       *    - Map output indices to character set
       *    - Build plate string (e.g., "34 ABC 123")
       *
       * Example placeholder:
       */
      memset(&result_msg, 0, sizeof(result_msg));
      result_msg.frame_id       = det_msg.frame_id;
      result_msg.timestamp_ms   = HAL_GetTick();
      result_msg.bbox           = det_msg.bbox;
      result_msg.ocr_confidence = 0.0f;  /* Replace with actual confidence */
      strncpy(result_msg.plate_text, "XX-000-XX",
              MAX_PLATE_STRING_LEN - 1);  /* Replace with OCR output */

      /* ── Update display overlay with recognized text ───────────── */
      if (osMutexAcquire(OverlayMutex, 10) == osOK)
      {
        if (det_msg.roi_index < MAX_DETECTIONS_PER_FRAME)
        {
          strncpy(SharedOverlay.plate_texts[det_msg.roi_index],
                  result_msg.plate_text, MAX_PLATE_STRING_LEN - 1);
        }
        osMutexRelease(OverlayMutex);
      }

      /* ── Forward plate text to encryption + transmission ───────── */
      osMessageQueuePut(PlateResultQueue, &result_msg, 0, 50);
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 4: CRYPTO + TRANSMIT                                                 */
/*  Priority: Normal                                                          */
/*  Role: Receives recognized plate text from OCRTask, encrypts it using      */
/*        mbedTLS (SHA-256 hash + AES-256 encryption), then transmits         */
/*        the encrypted payload to the backend server via LwIP TCP/UDP.       */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void CryptoTransmitTask(void *argument)
{
  (void)argument;
  PlateResultMsg_t result_msg;

  /* Encryption working buffers */
  uint8_t hash_digest[32];    /* SHA-256 output (32 bytes) */
  uint8_t encrypted_buf[128]; /* AES-256 encrypted payload */

  for (;;)
  {
    /* Block until a plate result is available */
    if (osMessageQueueGet(PlateResultQueue, &result_msg, NULL,
                          osWaitForever) == osOK)
    {
      /*
       * ──────────────────────────────────────────────────────────────
       *  STEP 1: HASH — Compute SHA-256 of plate data for integrity
       * ──────────────────────────────────────────────────────────────
       *
       * TODO: Use hardware HASH peripheral or mbedTLS:
       *
       *   // Hardware HASH (STM32N6 has SHA-256 accelerator):
       *   HAL_HASH_Start(&hhash,
       *                  (uint8_t *)result_msg.plate_text,
       *                  strlen(result_msg.plate_text),
       *                  hash_digest,
       *                  HAL_MAX_DELAY);
       *
       *   // Or mbedTLS software:
       *   mbedtls_sha256(result_msg.plate_text,
       *                  strlen(result_msg.plate_text),
       *                  hash_digest, 0);
       */
      (void)hash_digest;

      /*
       * ──────────────────────────────────────────────────────────────
       *  STEP 2: ENCRYPT — AES-256-CBC/GCM encryption
       * ──────────────────────────────────────────────────────────────
       *
       * TODO: Use mbedTLS AES:
       *
       *   mbedtls_aes_context aes_ctx;
       *   mbedtls_aes_init(&aes_ctx);
       *   mbedtls_aes_setkey_enc(&aes_ctx, aes_key, 256);
       *   mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_ENCRYPT,
       *                         payload_len, iv, plaintext, encrypted_buf);
       *   mbedtls_aes_free(&aes_ctx);
       *
       * Or use PKA for asymmetric operations if needed.
       */
      (void)encrypted_buf;

      /*
       * ──────────────────────────────────────────────────────────────
       *  STEP 3: TRANSMIT — Send encrypted payload via LwIP
       * ──────────────────────────────────────────────────────────────
       *
       * TODO: Implement TCP client or UDP sender:
       *
       *   // TCP example:
       *   struct netconn *conn = netconn_new(NETCONN_TCP);
       *   ip4_addr_t server_ip;
       *   IP4_ADDR(&server_ip, 192, 168, 1, 10);
       *   netconn_connect(conn, &server_ip, 8080);
       *
       *   struct netbuf *buf = netbuf_new();
       *   void *data = netbuf_alloc(buf, encrypted_len);
       *   memcpy(data, encrypted_buf, encrypted_len);
       *   netconn_send(conn, buf);
       *   netbuf_delete(buf);
       *   netconn_close(conn);
       *   netconn_delete(conn);
       *
       *   // UDP example:
       *   struct netconn *conn = netconn_new(NETCONN_UDP);
       *   netconn_connect(conn, &server_ip, 9000);
       *   netconn_send(conn, buf);
       */
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 5: ETHERNET RX INPUT                                                 */
/*  Priority: High                                                            */
/*  Role: Waits for ETH RX interrupt semaphore, then reads received           */
/*        frames and passes them to the LwIP TCP/IP stack.                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void EthInputTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    if (ethernetif_wait_rx(100) == osOK)
    {
      ethernetif_input(&gnetif);
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 6: ETHERNET LINK MONITOR                                             */
/*  Priority: BelowNormal                                                     */
/*  Role: Periodically polls the PHY link state and manages                   */
/*        DHCP / netif up/down transitions.                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void EthLinkTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    LwIP_Process();
    osDelay(200);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 7: DISPLAY                                                           */
/*  Priority: BelowNormal                                                     */
/*  Role: Reads the shared display overlay data and draws bounding boxes      */
/*        and recognized plate text on the LCD preview framebuffer.           */
/*        Runs at ~30 FPS. Non-critical — can be preempted by pipeline.      */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void DisplayTask(void *argument)
{
  (void)argument;
  DisplayOverlay_t local_overlay;
  memset(&local_overlay, 0, sizeof(local_overlay));

  for (;;)
  {
    /* ── Grab a snapshot of the overlay data ─────────────────────── */
    if (osMutexAcquire(OverlayMutex, 10) == osOK)
    {
      memcpy(&local_overlay, &SharedOverlay, sizeof(DisplayOverlay_t));
      osMutexRelease(OverlayMutex);
    }

    Display_DrawBringupFrame(&local_overlay);
    BSP_LCD_SwapBuffers(LCD_LAYER_0);

    osDelay(APP_DISPLAY_TEST_PERIOD_MS);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK 8: WATCHDOG                                                          */
/*  Priority: Realtime (highest)                                              */
/*  Role: Feeds the Independent Watchdog (IWDG) periodically.                 */
/*        If any task hangs the CPU, the watchdog will reset the system.      */
/*        Can be extended with per-task health monitoring.                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void WatchdogTask(void *argument)
{
  (void)argument;

  /*
   * IWDG is configured in main.c (MX_IWDG_Init):
   *   Prescaler = 64, Reload = 2500
   *   Timeout ≈ (64 * 2500) / 32000 Hz = 5 seconds
   *
   * We feed it every 1 second — providing a comfortable margin.
   */

  for (;;)
  {
    HAL_IWDG_Refresh(&hiwdg);

    /*
     * Optional: Monitor task health by checking task notification flags
     * from each pipeline task. If a task hasn't reported in N seconds,
     * don't feed the watchdog → system reset.
     */

    osDelay(1000);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  ISR CALLBACK                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  Camera frame ready callback (called from DCMIPP ISR context).
  *         Signals the CameraCaptureTask that a new AI frame is available.
  */
static void Camera_FrameReadyCb(BSP_Camera_PipeType_t pipe, uint32_t fb_addr)
{
  (void)fb_addr;

  if (pipe == CAMERA_PIPE_TYPE_AI)
  {
    if (camFrameSemaphore != NULL)
    {
      osSemaphoreRelease(camFrameSemaphore);
    }
  }
}

static void Display_DrawBringupFrame(const DisplayOverlay_t *overlay)
{
  const uint16_t bars[] = {
    RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_YELLOW,
    RGB565_CYAN, RGB565_MAGENTA, RGB565_WHITE, RGB565_GRAY
  };
  uint32_t now = HAL_GetTick();
  uint8_t camera_live = ((camera_last_frame_tick != 0U) &&
                         ((now - camera_last_frame_tick) < 1000U)) ? 1U : 0U;

  Display_ClearBackBuffer(RGB565_BLACK);

  for (uint32_t i = 0; i < (sizeof(bars) / sizeof(bars[0])); i++)
  {
    BSP_LCD_FillRect(LCD_LAYER_0, i * 80U, 0U, 80U, 64U, bars[i]);
  }

  BSP_LCD_FillRect(LCD_LAYER_0, 0U, 70U, LCD_WIDTH, 4U,
                   camera_live ? RGB565_GREEN : RGB565_RED);
  BSP_LCD_FillRect(LCD_LAYER_0, 0U, 78U, camera_capture_started ? 160U : 80U,
                   12U, camera_capture_started ? RGB565_GREEN : RGB565_YELLOW);

  Display_DrawFrameCounter(overlay->frame_id);

  for (uint8_t i = 0; i < overlay->num_detections; i++)
  {
    const BBox_t *bb = &overlay->detections[i];
    uint32_t sx = ((uint32_t)bb->x * LCD_WIDTH) / CAMERA_AI_WIDTH;
    uint32_t sy = ((uint32_t)bb->y * LCD_HEIGHT) / CAMERA_AI_HEIGHT;
    uint32_t sw = ((uint32_t)bb->w * LCD_WIDTH) / CAMERA_AI_WIDTH;
    uint32_t sh = ((uint32_t)bb->h * LCD_HEIGHT) / CAMERA_AI_HEIGHT;
    uint16_t color = camera_live ? RGB565_GREEN : RGB565_YELLOW;

    Display_DrawRect(sx, sy, sw, sh, color);
  }

  /* Moving pulse proves DisplayTask is alive even before camera/link tests. */
  uint32_t pulse_x = (HAL_GetTick() / APP_DISPLAY_TEST_PERIOD_MS) %
                     (LCD_WIDTH - 48U);
  BSP_LCD_FillRect(LCD_LAYER_0, pulse_x, LCD_HEIGHT - 36U, 48U, 20U,
                   RGB565_CYAN);
}

static void Display_DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint16_t color)
{
  if ((x >= LCD_WIDTH) || (y >= LCD_HEIGHT) || (w == 0U) || (h == 0U))
  {
    return;
  }

  if ((x + w) > LCD_WIDTH)
  {
    w = LCD_WIDTH - x;
  }

  if ((y + h) > LCD_HEIGHT)
  {
    h = LCD_HEIGHT - y;
  }

  BSP_LCD_FillRect(LCD_LAYER_0, x, y, w, 3U, color);
  BSP_LCD_FillRect(LCD_LAYER_0, x, y + h - 1U, w, 3U, color);
  BSP_LCD_FillRect(LCD_LAYER_0, x, y, 3U, h, color);
  BSP_LCD_FillRect(LCD_LAYER_0, x + w - 1U, y, 3U, h, color);
}

static void Display_DrawFrameCounter(uint32_t frame_id)
{
  for (uint32_t bit = 0; bit < 16U; bit++)
  {
    uint16_t color = ((frame_id >> bit) & 1U) ? RGB565_GREEN : RGB565_GRAY;
    BSP_LCD_FillRect(LCD_LAYER_0, 16U + (bit * 18U), 96U, 14U, 28U, color);
  }

  for (uint32_t bit = 0; bit < 8U; bit++)
  {
    uint16_t color = ((camera_timeout_count >> bit) & 1U) ?
                     RGB565_RED : RGB565_GRAY;
    BSP_LCD_FillRect(LCD_LAYER_0, 16U + (bit * 18U), 132U, 14U, 14U, color);
  }
}

static void Display_ClearBackBuffer(uint16_t color)
{
  uint16_t *fb = BSP_LCD_GetBackBuffer(LCD_LAYER_0);

  for (uint32_t i = 0; i < (LCD_WIDTH * LCD_HEIGHT); i++)
  {
    fb[i] = color;
  }
}
