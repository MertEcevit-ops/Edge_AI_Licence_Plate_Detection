/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   FreeRTOS application tasks for Edge AI ALPR pipeline.
  *
  *  Implements the full ALPR processing pipeline:
  *
  *  1. CameraCaptureTask  — Manages DCMIPP DMA, provides frames to pipeline
  *  2. DetectionTask      — Runs Tiny-YOLO on NPU for license plate detection
  *  3. OCRTask            — Prepares cropped plate handoff for host CNN OCR
  *  4. CryptoTransmitTask — Encrypts ROI payload + sends via LwIP
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

#include "ai_wrapper_ATON.h"
#include "ll_aton_NN_interface.h"
#include "cmsis_os2.h"
#include "lwip/api.h"
#include "lwip/ip_addr.h"
#include <string.h>
#include <stdio.h>

/* Local bring-up switches ---------------------------------------------------*/
#define APP_TEST_GENERATE_FRAME_ON_CAMERA_TIMEOUT   0U
#define APP_TEST_OVERLAY_ENABLED                    0U
#define APP_DISPLAY_TEST_PERIOD_MS                  100U
#define APP_DETECTION_CONFIDENCE_THRESHOLD          0.45f
#define APP_DETECTION_NMS_IOU_THRESHOLD             0.45f
#define APP_MAX_RAW_DETECTIONS                      24U
#define APP_DETECTION_OUTPUT_CANDIDATES             2100U
#define APP_DETECTION_OUTPUT_FIELDS                 5U
#define APP_PLATE_IMAGE_WIDTH                       96U
#define APP_PLATE_IMAGE_HEIGHT                      32U
#define APP_PLATE_PGM_MAX_HEADER                    24U
#define APP_PLATE_PGM_MAX_SIZE \
  (APP_PLATE_PGM_MAX_HEADER + (APP_PLATE_IMAGE_WIDTH * APP_PLATE_IMAGE_HEIGHT))
#define APP_PLATE_IMAGE_B64_SIZE \
  ((((APP_PLATE_PGM_MAX_SIZE + 2U) / 3U) * 4U) + 1U)

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
static volatile uint32_t detector_inference_count = 0;
static volatile uint32_t detector_error_count = 0;
static volatile uint32_t host_ocr_payload_count = 0;
static volatile uint32_t host_ocr_payload_drop_count = 0;

static struct npu_instance detector_instance;
static uint8_t detector_ready = 0U;
static osMutexId_t aiInferenceMutex = NULL;

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
static uint8_t ALPR_Detector_Init(void);
static uint8_t ALPR_RunDetection(const FrameMsg_t *frame_msg,
                                 BBox_t *detections,
                                 uint8_t max_detections);
static void ALPR_PreprocessRgb888ToFloat(const uint8_t *src_rgb888,
                                         float *dst_float);
static uint8_t ALPR_PostProcessDetections(const float *output,
                                          size_t output_count,
                                          BBox_t *detections,
                                          uint8_t max_detections);
static uint8_t ALPR_BuildBox(float cx, float cy, float w, float h,
                             float confidence, BBox_t *box);
static void ALPR_InsertRawDetection(BBox_t *raw_detections,
                                    uint8_t *raw_count,
                                    const BBox_t *candidate);
static float ALPR_BoxIoU(const BBox_t *a, const BBox_t *b);
static size_t Crypto_BuildPlaintext(const PlateResultMsg_t *result,
                                    uint8_t *buffer, size_t buffer_size);
static size_t Crypto_EncodePlateImageB64(const PlateResultMsg_t *result,
                                         char *output,
                                         size_t output_size);
static void Crypto_SendSecurePacket(const SecurityPacket_t *packet);
static size_t App_Base64Encode(const uint8_t *input, size_t input_len,
                               char *output, size_t output_size);

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
  aiInferenceMutex = osMutexNew(NULL);
  memset(&SharedOverlay, 0, sizeof(SharedOverlay));

  /* ── 3. Initialize Network Stack ──────────────────────────────────────── */

  LwIP_Init();

  if (SecurityLayer_Init() != SECURITY_OK)
  {
    Error_Handler();
  }

  (void)ALPR_Detector_Init();

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
      num_detections = ALPR_RunDetection(&frame_msg, detections,
                                         MAX_DETECTIONS_PER_FRAME);

#if APP_TEST_OVERLAY_ENABLED
      if (num_detections == 0U)
      {
        detections[0].x = (uint16_t)((frame_msg.frame_id * 7U) %
                                     (CAMERA_AI_WIDTH - 96U));
        detections[0].y = (uint16_t)(96U + ((frame_msg.frame_id * 3U) % 64U));
        detections[0].w = 96U;
        detections[0].h = 32U;
        detections[0].confidence = camera_capture_started ? 0.90f : 0.25f;
        num_detections = 1U;
      }
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
        det_msg.timestamp_ms = frame_msg.timestamp_ms;
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
/*  TASK 3: OCR HANDOFF                                                       */
/*  Priority: Normal                                                          */
/*  Role: Receives plate ROIs from DetectionTask and forwards ROI metadata    */
/*        to CryptoTransmitTask, where a compact encrypted PGM crop is built  */
/*        for the host CNN OCR receiver.                                      */
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
      memset(&result_msg, 0, sizeof(result_msg));
      result_msg.frame_id       = det_msg.frame_id;
      result_msg.timestamp_ms   = det_msg.timestamp_ms;
      result_msg.fb_addr        = det_msg.fb_addr;
      result_msg.bbox           = det_msg.bbox;
      result_msg.roi_index      = det_msg.roi_index;
      result_msg.ocr_confidence = 0.0f;
      result_msg.plate_text[0]  = '\0';

      /* ── Update display overlay with recognized text ───────────── */
      if (osMutexAcquire(OverlayMutex, 10) == osOK)
      {
        if (det_msg.roi_index < MAX_DETECTIONS_PER_FRAME)
        {
          strncpy(SharedOverlay.plate_texts[det_msg.roi_index],
                  "HOST OCR", MAX_PLATE_STRING_LEN - 1);
          SharedOverlay.plate_texts[det_msg.roi_index]
                                   [MAX_PLATE_STRING_LEN - 1] = '\0';
        }
        osMutexRelease(OverlayMutex);
      }

      /* ── Forward ROI metadata to encryption + host OCR handoff ─── */
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
  static uint8_t plaintext[SECURITY_MAX_PLAINTEXT_SIZE];
  static SecurityPacket_t secure_packet;

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

static uint8_t ALPR_Detector_Init(void)
{
  const LL_Buffer_InfoTypeDef *input_info;
  const LL_Buffer_InfoTypeDef *output_info;

  if (detector_ready != 0U)
  {
    return 1U;
  }

  memset(&detector_instance, 0, sizeof(detector_instance));
  if (npu_get_instance_by_index(0, &detector_instance) != 0)
  {
    detector_error_count++;
    return 0U;
  }

  input_info = npu_get_input_buffers_info(&detector_instance, 0);
  output_info = npu_get_output_buffers_info(&detector_instance, 0);
  if ((input_info == NULL) || (output_info == NULL))
  {
    detector_error_count++;
    return 0U;
  }

  if (get_ll_buffer_size(input_info) <
      (CAMERA_AI_WIDTH * CAMERA_AI_HEIGHT * 3U * sizeof(float)))
  {
    detector_error_count++;
    return 0U;
  }

  if (get_ll_buffer_size(output_info) <
      (APP_DETECTION_OUTPUT_CANDIDATES *
       APP_DETECTION_OUTPUT_FIELDS * sizeof(float)))
  {
    detector_error_count++;
    return 0U;
  }

  if (npu_init(&detector_instance, 1U) != 0)
  {
    detector_error_count++;
    return 0U;
  }

  detector_ready = 1U;
  return 1U;
}

static uint8_t ALPR_RunDetection(const FrameMsg_t *frame_msg,
                                 BBox_t *detections,
                                 uint8_t max_detections)
{
  const LL_Buffer_InfoTypeDef *input_info;
  const LL_Buffer_InfoTypeDef *output_info;
  float *input;
  float *output;
  uint8_t num_detections = 0U;
  int inference_ms;

  if ((frame_msg == NULL) || (detections == NULL) ||
      (max_detections == 0U) || (frame_msg->fb_addr == 0U))
  {
    return 0U;
  }

  if (ALPR_Detector_Init() == 0U)
  {
    return 0U;
  }

  if ((aiInferenceMutex != NULL) &&
      (osMutexAcquire(aiInferenceMutex, 250U) != osOK))
  {
    detector_error_count++;
    return 0U;
  }

  input_info = npu_get_input_buffers_info(&detector_instance, 0);
  output_info = npu_get_output_buffers_info(&detector_instance, 0);
  if ((input_info == NULL) || (output_info == NULL))
  {
    detector_error_count++;
    if (aiInferenceMutex != NULL)
    {
      osMutexRelease(aiInferenceMutex);
    }
    return 0U;
  }

  input = (float *)LL_Buffer_addr_start(input_info);
  output = (float *)LL_Buffer_addr_start(output_info);

  if ((input != NULL) && (output != NULL))
  {
    ALPR_PreprocessRgb888ToFloat((const uint8_t *)frame_msg->fb_addr, input);
    inference_ms = npu_run(&detector_instance, NULL);
    if (inference_ms >= 0)
    {
      detector_inference_count++;
      num_detections = ALPR_PostProcessDetections(
          output,
          get_ll_buffer_size(output_info) / sizeof(float),
          detections,
          max_detections);
    }
    else
    {
      detector_error_count++;
    }
  }
  else
  {
    detector_error_count++;
  }

  if (aiInferenceMutex != NULL)
  {
    osMutexRelease(aiInferenceMutex);
  }

  return num_detections;
}

static void ALPR_PreprocessRgb888ToFloat(const uint8_t *src_rgb888,
                                         float *dst_float)
{
  const float inv_255 = 1.0f / 255.0f;
  const uint32_t pixel_count = CAMERA_AI_WIDTH * CAMERA_AI_HEIGHT;

  for (uint32_t i = 0U; i < pixel_count; i++)
  {
    uint32_t src_index = i * 3U;
    dst_float[src_index + 0U] = (float)src_rgb888[src_index + 0U] * inv_255;
    dst_float[src_index + 1U] = (float)src_rgb888[src_index + 1U] * inv_255;
    dst_float[src_index + 2U] = (float)src_rgb888[src_index + 2U] * inv_255;
  }
}

static uint8_t ALPR_PostProcessDetections(const float *output,
                                          size_t output_count,
                                          BBox_t *detections,
                                          uint8_t max_detections)
{
  BBox_t raw_detections[APP_MAX_RAW_DETECTIONS];
  uint8_t raw_count = 0U;
  uint8_t selected_count = 0U;
  const size_t expected_count = APP_DETECTION_OUTPUT_CANDIDATES *
                                APP_DETECTION_OUTPUT_FIELDS;

  if ((output == NULL) || (detections == NULL) ||
      (output_count < expected_count))
  {
    return 0U;
  }

  for (uint32_t i = 0U; i < APP_DETECTION_OUTPUT_CANDIDATES; i++)
  {
    float confidence = output[(4U * APP_DETECTION_OUTPUT_CANDIDATES) + i];

    if (confidence >= APP_DETECTION_CONFIDENCE_THRESHOLD)
    {
      BBox_t candidate;
      float cx = output[i];
      float cy = output[APP_DETECTION_OUTPUT_CANDIDATES + i];
      float w = output[(2U * APP_DETECTION_OUTPUT_CANDIDATES) + i];
      float h = output[(3U * APP_DETECTION_OUTPUT_CANDIDATES) + i];

      if (ALPR_BuildBox(cx, cy, w, h, confidence, &candidate) != 0U)
      {
        ALPR_InsertRawDetection(raw_detections, &raw_count, &candidate);
      }
    }
  }

  for (uint8_t i = 0U; i < raw_count; i++)
  {
    uint8_t keep = 1U;

    for (uint8_t j = 0U; j < selected_count; j++)
    {
      if (ALPR_BoxIoU(&raw_detections[i], &detections[j]) >
          APP_DETECTION_NMS_IOU_THRESHOLD)
      {
        keep = 0U;
        break;
      }
    }

    if (keep != 0U)
    {
      detections[selected_count++] = raw_detections[i];
      if (selected_count >= max_detections)
      {
        break;
      }
    }
  }

  return selected_count;
}

static uint8_t ALPR_BuildBox(float cx, float cy, float w, float h,
                             float confidence, BBox_t *box)
{
  float x_min;
  float y_min;
  float x_max;
  float y_max;
  uint32_t x0;
  uint32_t y0;
  uint32_t x1;
  uint32_t y1;

  if ((box == NULL) || (cx != cx) || (cy != cy) || (w != w) || (h != h) ||
      (w <= 0.0f) || (h <= 0.0f))
  {
    return 0U;
  }

  if ((cx <= 2.0f) && (cy <= 2.0f) && (w <= 2.0f) && (h <= 2.0f))
  {
    cx *= (float)CAMERA_AI_WIDTH;
    w  *= (float)CAMERA_AI_WIDTH;
    cy *= (float)CAMERA_AI_HEIGHT;
    h  *= (float)CAMERA_AI_HEIGHT;
  }

  if ((w < 4.0f) || (h < 4.0f))
  {
    return 0U;
  }

  x_min = cx - (w * 0.5f);
  y_min = cy - (h * 0.5f);
  x_max = cx + (w * 0.5f);
  y_max = cy + (h * 0.5f);

  if ((x_max <= 0.0f) || (y_max <= 0.0f) ||
      (x_min >= (float)CAMERA_AI_WIDTH) ||
      (y_min >= (float)CAMERA_AI_HEIGHT))
  {
    return 0U;
  }

  if (x_min < 0.0f) x_min = 0.0f;
  if (y_min < 0.0f) y_min = 0.0f;
  if (x_max > (float)CAMERA_AI_WIDTH) x_max = (float)CAMERA_AI_WIDTH;
  if (y_max > (float)CAMERA_AI_HEIGHT) y_max = (float)CAMERA_AI_HEIGHT;

  x0 = (uint32_t)x_min;
  y0 = (uint32_t)y_min;
  x1 = (uint32_t)(x_max + 0.5f);
  y1 = (uint32_t)(y_max + 0.5f);

  if (x1 > CAMERA_AI_WIDTH) x1 = CAMERA_AI_WIDTH;
  if (y1 > CAMERA_AI_HEIGHT) y1 = CAMERA_AI_HEIGHT;

  if ((x1 <= x0) || (y1 <= y0))
  {
    return 0U;
  }

  box->x = (uint16_t)x0;
  box->y = (uint16_t)y0;
  box->w = (uint16_t)(x1 - x0);
  box->h = (uint16_t)(y1 - y0);
  box->confidence = confidence;

  return 1U;
}

static void ALPR_InsertRawDetection(BBox_t *raw_detections,
                                    uint8_t *raw_count,
                                    const BBox_t *candidate)
{
  uint8_t pos;

  if ((raw_detections == NULL) || (raw_count == NULL) ||
      (candidate == NULL))
  {
    return;
  }

  if (*raw_count < APP_MAX_RAW_DETECTIONS)
  {
    pos = *raw_count;
    (*raw_count)++;
  }
  else
  {
    pos = APP_MAX_RAW_DETECTIONS - 1U;
    if (candidate->confidence <= raw_detections[pos].confidence)
    {
      return;
    }
  }

  while ((pos > 0U) &&
         (candidate->confidence > raw_detections[pos - 1U].confidence))
  {
    raw_detections[pos] = raw_detections[pos - 1U];
    pos--;
  }

  raw_detections[pos] = *candidate;
}

static float ALPR_BoxIoU(const BBox_t *a, const BBox_t *b)
{
  uint32_t ax1 = a->x;
  uint32_t ay1 = a->y;
  uint32_t ax2 = (uint32_t)a->x + a->w;
  uint32_t ay2 = (uint32_t)a->y + a->h;
  uint32_t bx1 = b->x;
  uint32_t by1 = b->y;
  uint32_t bx2 = (uint32_t)b->x + b->w;
  uint32_t by2 = (uint32_t)b->y + b->h;
  uint32_t ix1 = (ax1 > bx1) ? ax1 : bx1;
  uint32_t iy1 = (ay1 > by1) ? ay1 : by1;
  uint32_t ix2 = (ax2 < bx2) ? ax2 : bx2;
  uint32_t iy2 = (ay2 < by2) ? ay2 : by2;
  uint32_t inter_area;
  uint32_t union_area;

  if ((ix2 <= ix1) || (iy2 <= iy1))
  {
    return 0.0f;
  }

  inter_area = (ix2 - ix1) * (iy2 - iy1);
  union_area = ((uint32_t)a->w * a->h) + ((uint32_t)b->w * b->h) -
               inter_area;

  if (union_area == 0U)
  {
    return 0.0f;
  }

  return (float)inter_area / (float)union_area;
}

static size_t Crypto_BuildPlaintext(const PlateResultMsg_t *result,
                                    uint8_t *buffer, size_t buffer_size)
{
  static char plate_image_b64[APP_PLATE_IMAGE_B64_SIZE];
  uint32_t confidence_milli;
  size_t image_b64_len;
  int written;

  if ((result == NULL) || (buffer == NULL) || (buffer_size == 0U))
  {
    return 0U;
  }

  confidence_milli = (result->ocr_confidence <= 0.0f) ? 0U :
      (uint32_t)(result->ocr_confidence * 1000.0f);

  image_b64_len = Crypto_EncodePlateImageB64(result, plate_image_b64,
                                             sizeof(plate_image_b64));

  if (image_b64_len > 0U)
  {
    written = snprintf((char *)buffer, buffer_size,
                       "{\"frame_id\":%lu,\"timestamp_ms\":%lu,"
                       "\"plate\":null,"
                       "\"edge_ocr_status\":\"host_ocr_image_ready\","
                       "\"ocr_confidence_milli\":%lu,"
                       "\"bbox\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u},"
                       "\"plate_image_format\":\"pgm\","
                       "\"plate_image_width\":%u,"
                       "\"plate_image_height\":%u,"
                       "\"plate_image_b64\":\"%s\"}",
                       (unsigned long)result->frame_id,
                       (unsigned long)result->timestamp_ms,
                       (unsigned long)confidence_milli,
                       result->bbox.x, result->bbox.y,
                       result->bbox.w, result->bbox.h,
                       APP_PLATE_IMAGE_WIDTH,
                       APP_PLATE_IMAGE_HEIGHT,
                       plate_image_b64);
  }
  else
  {
    written = snprintf((char *)buffer, buffer_size,
                       "{\"frame_id\":%lu,\"timestamp_ms\":%lu,"
                       "\"plate\":null,"
                       "\"edge_ocr_status\":\"host_ocr_image_unavailable\","
                       "\"ocr_confidence_milli\":%lu,"
                       "\"bbox\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u},"
                       "\"plate_image_b64\":null}",
                       (unsigned long)result->frame_id,
                       (unsigned long)result->timestamp_ms,
                       (unsigned long)confidence_milli,
                       result->bbox.x, result->bbox.y,
                       result->bbox.w, result->bbox.h);
  }

  if ((written <= 0) || ((size_t)written >= buffer_size))
  {
    host_ocr_payload_drop_count++;
    return 0U;
  }

  if (image_b64_len > 0U)
  {
    host_ocr_payload_count++;
  }
  else
  {
    host_ocr_payload_drop_count++;
  }

  return (size_t)written;
}

static size_t Crypto_EncodePlateImageB64(const PlateResultMsg_t *result,
                                         char *output,
                                         size_t output_size)
{
  static uint8_t plate_pgm[APP_PLATE_PGM_MAX_SIZE];
  const uint8_t *src;
  uint8_t *pgm_pixels;
  int header_len;
  size_t pgm_len;
  uint32_t max_src_x;
  uint32_t max_src_y;

  if ((result == NULL) || (output == NULL) || (output_size == 0U) ||
      (result->fb_addr == 0U) || (result->bbox.w == 0U) ||
      (result->bbox.h == 0U) || (result->bbox.x >= CAMERA_AI_WIDTH) ||
      (result->bbox.y >= CAMERA_AI_HEIGHT))
  {
    return 0U;
  }

  header_len = snprintf((char *)plate_pgm, APP_PLATE_PGM_MAX_HEADER,
                        "P5\n%u %u\n255\n",
                        APP_PLATE_IMAGE_WIDTH,
                        APP_PLATE_IMAGE_HEIGHT);
  if ((header_len <= 0) ||
      ((size_t)header_len >= APP_PLATE_PGM_MAX_HEADER))
  {
    return 0U;
  }

  src = (const uint8_t *)result->fb_addr;
  pgm_pixels = &plate_pgm[(size_t)header_len];
  max_src_x = CAMERA_AI_WIDTH - 1U;
  max_src_y = CAMERA_AI_HEIGHT - 1U;

  for (uint32_t y = 0U; y < APP_PLATE_IMAGE_HEIGHT; y++)
  {
    uint32_t src_y = result->bbox.y +
        (((uint32_t)result->bbox.h * y) / APP_PLATE_IMAGE_HEIGHT);
    if (src_y > max_src_y)
    {
      src_y = max_src_y;
    }

    for (uint32_t x = 0U; x < APP_PLATE_IMAGE_WIDTH; x++)
    {
      uint32_t src_x = result->bbox.x +
          (((uint32_t)result->bbox.w * x) / APP_PLATE_IMAGE_WIDTH);
      uint32_t src_index;
      uint8_t r;
      uint8_t g;
      uint8_t b;

      if (src_x > max_src_x)
      {
        src_x = max_src_x;
      }

      src_index = ((src_y * CAMERA_AI_WIDTH) + src_x) * 3U;
      r = src[src_index + 0U];
      g = src[src_index + 1U];
      b = src[src_index + 2U];

      pgm_pixels[(y * APP_PLATE_IMAGE_WIDTH) + x] =
          (uint8_t)(((77U * r) + (150U * g) + (29U * b)) >> 8U);
    }
  }

  pgm_len = (size_t)header_len +
            (APP_PLATE_IMAGE_WIDTH * APP_PLATE_IMAGE_HEIGHT);
  return App_Base64Encode(plate_pgm, pgm_len, output, output_size);
}

static size_t App_Base64Encode(const uint8_t *input, size_t input_len,
                               char *output, size_t output_size)
{
  static const char b64_table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t encoded_len;
  size_t in_index = 0U;
  size_t out_index = 0U;

  if ((input == NULL) || (output == NULL))
  {
    return 0U;
  }

  encoded_len = ((input_len + 2U) / 3U) * 4U;
  if (output_size <= encoded_len)
  {
    return 0U;
  }

  while ((in_index + 3U) <= input_len)
  {
    uint32_t triple = ((uint32_t)input[in_index] << 16U) |
                      ((uint32_t)input[in_index + 1U] << 8U) |
                      input[in_index + 2U];
    output[out_index++] = b64_table[(triple >> 18U) & 0x3FU];
    output[out_index++] = b64_table[(triple >> 12U) & 0x3FU];
    output[out_index++] = b64_table[(triple >> 6U) & 0x3FU];
    output[out_index++] = b64_table[triple & 0x3FU];
    in_index += 3U;
  }

  if (in_index < input_len)
  {
    uint32_t triple = (uint32_t)input[in_index] << 16U;
    output[out_index++] = b64_table[(triple >> 18U) & 0x3FU];

    if ((in_index + 1U) < input_len)
    {
      triple |= (uint32_t)input[in_index + 1U] << 8U;
      output[out_index++] = b64_table[(triple >> 12U) & 0x3FU];
      output[out_index++] = b64_table[(triple >> 6U) & 0x3FU];
      output[out_index++] = '=';
    }
    else
    {
      output[out_index++] = b64_table[(triple >> 12U) & 0x3FU];
      output[out_index++] = '=';
      output[out_index++] = '=';
    }
  }

  output[out_index] = '\0';
  return out_index;
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
