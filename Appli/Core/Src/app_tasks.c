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
#include "app_display.h"
#include "bsp_lcd.h"
#include "bsp_camera.h"
#include "bsp_eth.h"
#include "bsp_watchdog.h"
#include "ethernetif.h"
#include "security_layer.h"
#include "main.h"

#include "cmsis_os2.h"
#include "lwip/api.h"
#include "lwip/ip_addr.h"
#include <string.h>
#include <stdio.h>

/* Local bring-up switches ---------------------------------------------------*/
#define APP_TEST_GENERATE_FRAME_ON_CAMERA_TIMEOUT   1U
#define APP_TEST_OVERLAY_ENABLED                    1U
#define APP_DISPLAY_TEST_PERIOD_MS                  100U

#ifndef ALPR_HOST_IP_ADDR0
#define ALPR_HOST_IP_ADDR0                          192U
#define ALPR_HOST_IP_ADDR1                          168U
#define ALPR_HOST_IP_ADDR2                          1U
#define ALPR_HOST_IP_ADDR3                          10U
#endif

#ifndef ALPR_HOST_TCP_PORT
#define ALPR_HOST_TCP_PORT                          9000U
#endif

/* External declarations -----------------------------------------------------*/
extern struct netif gnetif;
extern void LwIP_Init(void);
extern void LwIP_Process(void);
extern osStatus_t ethernetif_wait_rx(uint32_t timeout_ms);

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
static size_t Crypto_BuildPlaintext(const PlateResultMsg_t *result,
                                    uint8_t *buffer, size_t buffer_size);
static void Crypto_SendSecurePacket(const SecurityPacket_t *packet);

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
  /* ── 1. BSP peripherals are opened by BSP_Board_Open() before FreeRTOS. ─ */

  /* Display startup screen */
  BSP_LCD_Clear(LCD_LAYER_0, RGB565_BLUE);

  /* Camera callback registration needs RTOS objects created in this context. */
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

  if (SecurityLayer_Init() != SECURITY_OK)
  {
    Error_Handler();
  }

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
/*        SHA-256 hash + AES-256 encryption, then transmits                   */
/*        the encrypted payload to the backend server via LwIP TCP/UDP.       */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void CryptoTransmitTask(void *argument)
{
  (void)argument;
  PlateResultMsg_t result_msg;
  uint8_t plaintext[SECURITY_MAX_PLAINTEXT_SIZE];
  SecurityPacket_t secure_packet;

  for (;;)
  {
    /* Block until a plate result is available */
    if (osMessageQueueGet(PlateResultQueue, &result_msg, NULL,
                          osWaitForever) == osOK)
    {
      size_t plaintext_len = Crypto_BuildPlaintext(&result_msg, plaintext,
                                                   sizeof(plaintext));

      if (plaintext_len == 0U)
      {
        continue;
      }

      if (SecurityLayer_Seal(plaintext, plaintext_len, result_msg.frame_id,
                             result_msg.timestamp_ms,
                             &secure_packet) != SECURITY_OK)
      {
        continue;
      }

      Crypto_SendSecurePacket(&secure_packet);
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

    AppDisplay_DrawBringupFrame(&local_overlay,
                                camera_last_frame_tick,
                                camera_capture_started,
                                camera_timeout_count);
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

  for (;;)
  {
    (void)BSP_Watchdog_Refresh();
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

static size_t Crypto_BuildPlaintext(const PlateResultMsg_t *result,
                                    uint8_t *buffer, size_t buffer_size)
{
  uint32_t confidence_milli;
  int written;

  if ((result == NULL) || (buffer == NULL) || (buffer_size == 0U))
  {
    return 0U;
  }

  confidence_milli = (result->ocr_confidence <= 0.0f) ? 0U :
      (uint32_t)(result->ocr_confidence * 1000.0f);

  written = snprintf((char *)buffer, buffer_size,
                     "{\"frame_id\":%lu,\"timestamp_ms\":%lu,"
                     "\"plate\":\"%s\",\"ocr_confidence_milli\":%lu,"
                     "\"bbox\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u}}",
                     (unsigned long)result->frame_id,
                     (unsigned long)result->timestamp_ms,
                     result->plate_text,
                     (unsigned long)confidence_milli,
                     result->bbox.x, result->bbox.y,
                     result->bbox.w, result->bbox.h);

  if ((written <= 0) || ((size_t)written >= buffer_size))
  {
    return 0U;
  }

  return (size_t)written;
}

static void Crypto_SendSecurePacket(const SecurityPacket_t *packet)
{
  struct netconn *conn;
  ip_addr_t server_ip;
  size_t wire_size;

  wire_size = SecurityLayer_GetPacketWireSize(packet);
  if (wire_size == 0U)
  {
    return;
  }

  conn = netconn_new(NETCONN_TCP);
  if (conn == NULL)
  {
    return;
  }

  IP_ADDR4(&server_ip, ALPR_HOST_IP_ADDR0, ALPR_HOST_IP_ADDR1,
           ALPR_HOST_IP_ADDR2, ALPR_HOST_IP_ADDR3);

  if (netconn_connect(conn, &server_ip,
                      (uint16_t)ALPR_HOST_TCP_PORT) == ERR_OK)
  {
    (void)netconn_write(conn, packet, wire_size, NETCONN_COPY);
    (void)netconn_close(conn);
  }

  netconn_delete(conn);
}
