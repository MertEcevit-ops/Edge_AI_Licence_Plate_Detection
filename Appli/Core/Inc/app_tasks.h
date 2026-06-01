/**
  ******************************************************************************
  * @file    app_tasks.h
  * @brief   FreeRTOS application task & pipeline definitions for Edge AI ALPR
  *
  *  ALPR Pipeline Architecture:
  *  ===========================
  *
  *  ┌──────────┐   MsgQ    ┌───────────┐   MsgQ    ┌──────────┐
  *  │ Camera   │ ────────> │ Detection │ ────────> │ OCR      │
  *  │ Capture  │  (frame)  │ (YOLO/NPU)│  (ROI)   │ handoff  │
  *  └──────────┘           └───────────┘           └──────────┘
  *       │                      │                       │
  *       │ (preview fb)         │ (bbox overlay)        │ MsgQ
  *       ▼                      ▼                       ▼
  *  ┌──────────┐           ┌───────────┐          ┌──────────────┐
  *  │  LTDC    │           │  Display  │          │   Crypto +   │
  *  │ (HW)    │           │  Task     │          │   Transmit   │
  *  └──────────┘           └───────────┘          └──────────────┘
  *                                                      │
  *                                                      ▼
  *                                                 ┌──────────┐
  *                                                 │  LwIP /  │
  *                                                 │ Ethernet │
  *                                                 └──────────┘
  *
  ******************************************************************************
  */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "cmsis_os2.h"
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  PIPELINE DATA STRUCTURES                                                  */
/* ═══════════════════════════════════════════════════════════════════════════ */

/** Maximum number of detections per frame (YOLO output) */
#define MAX_DETECTIONS_PER_FRAME    8

/** Maximum license plate string length (e.g. "34 ABC 123") */
#define MAX_PLATE_STRING_LEN        16

/** Maximum ROIs forwarded to OCR per frame */
#define MAX_OCR_ROIS_PER_FRAME      4

/**
  * @brief  Bounding box structure for detected license plates.
  */
typedef struct
{
  uint16_t x;           /**< Top-left X coordinate (in AI frame coords) */
  uint16_t y;           /**< Top-left Y coordinate */
  uint16_t w;           /**< Width */
  uint16_t h;           /**< Height */
  float    confidence;  /**< Detection confidence [0.0 - 1.0] */
} BBox_t;

/**
  * @brief  Message passed from CameraCaptureTask → DetectionTask.
  *         Contains the AI frame buffer pointer and frame metadata.
  */
typedef struct
{
  uint32_t fb_addr;        /**< Framebuffer address (AI pipe, RGB888) */
  uint32_t frame_id;       /**< Monotonically increasing frame counter */
  uint32_t timestamp_ms;   /**< HAL_GetTick() at capture time */
} FrameMsg_t;

/**
  * @brief  Message passed from DetectionTask → OCRTask.
  *         Contains cropped plate ROI info + reference to source frame.
  */
typedef struct
{
  uint32_t frame_id;       /**< Source frame ID for traceability */
  uint32_t timestamp_ms;   /**< Original capture timestamp */
  uint32_t fb_addr;        /**< Source framebuffer address */
  BBox_t   bbox;           /**< Bounding box of the detected plate */
  uint8_t  roi_index;      /**< ROI index within this frame (0..N) */
} DetectionMsg_t;

/**
  * @brief  Message passed from OCRTask → CryptoTransmitTask.
  *         Contains the recognized plate string ready for encryption.
  */
typedef struct
{
  uint32_t frame_id;                         /**< Source frame ID */
  uint32_t timestamp_ms;                     /**< Original capture timestamp */
  uint32_t fb_addr;                          /**< Source AI framebuffer address */
  char     plate_text[MAX_PLATE_STRING_LEN]; /**< Null-terminated plate string */
  float    ocr_confidence;                   /**< OCR recognition confidence */
  BBox_t   bbox;                             /**< Plate location (for logging) */
  uint8_t  roi_index;                        /**< ROI index within source frame */
} PlateResultMsg_t;

/**
  * @brief  Display overlay data shared between Detection and Display tasks.
  */
typedef struct
{
  uint32_t frame_id;
  uint8_t  num_detections;
  BBox_t   detections[MAX_DETECTIONS_PER_FRAME];
  char     plate_texts[MAX_DETECTIONS_PER_FRAME][MAX_PLATE_STRING_LEN];
} DisplayOverlay_t;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK CONFIGURATION                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Stack sizes (in 32-bit words).
 *
 * Rationale:
 * - CameraCapture:   Light — just manages DMA/DCMIPP triggers
 * - Detection:       Large — YOLO pre/post-processing on CPU, NPU invocation
 * - OCR:             Medium — ROI handoff to secure host OCR
 * - CryptoTransmit:  Medium — SHA-256 / AES-256 context, crop encode, LwIP send
 * - EthInput:        Medium — pbuf processing
 * - EthLink:         Small — PHY register polling
 * - Display:         Medium — framebuffer drawing operations
 * - Watchdog:        Minimal — just feeds IWDG
 */
#define CAMERA_TASK_STACK          256
#define DETECTION_TASK_STACK       1024
#define OCR_TASK_STACK             1024
#define CRYPTO_TX_TASK_STACK       768
#define ETH_INPUT_TASK_STACK       512
#define ETH_LINK_TASK_STACK        256
#define DISPLAY_TASK_STACK         512
#define WATCHDOG_TASK_STACK        128

/*
 * Task priorities (CMSIS-RTOS2).
 *
 * Priority assignment rationale:
 *   Highest   → Watchdog:     Must never starve — system health
 *   High      → EthInput:     Network frames must be processed promptly
 *   High      → Camera:       Frame capture timing is critical (DMA sync)
 *   AbvNormal → Detection:    Feed the pipeline continuously
 *   Normal    → OCR:          Process plate ROIs as they arrive
 *   Normal    → CryptoTx:     Encrypt + send results
 *   BelowNorm → EthLink:      PHY polling is infrequent
 *   BelowNorm → Display:      Cosmetic — lowest impact if delayed
 */
#define CAMERA_TASK_PRIO           osPriorityHigh
#define DETECTION_TASK_PRIO        osPriorityAboveNormal
#define OCR_TASK_PRIO              osPriorityNormal
#define CRYPTO_TX_TASK_PRIO        osPriorityNormal
#define ETH_INPUT_TASK_PRIO        osPriorityHigh
#define ETH_LINK_TASK_PRIO         osPriorityBelowNormal
#define DISPLAY_TASK_PRIO          osPriorityBelowNormal
#define WATCHDOG_TASK_PRIO         osPriorityRealtime

/* Message queue depths */
#define FRAME_QUEUE_DEPTH          2   /* Camera → Detection (double-buffer)  */
#define DETECTION_QUEUE_DEPTH      4   /* Detection → OCR (multiple ROIs)     */
#define PLATE_QUEUE_DEPTH          4   /* OCR → CryptoTransmit                */

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  TASK HANDLES & SYNC OBJECTS (extern)                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

/* Task handles */
extern osThreadId_t CameraCaptureTaskHandle;
extern osThreadId_t DetectionTaskHandle;
extern osThreadId_t OCRTaskHandle;
extern osThreadId_t CryptoTransmitTaskHandle;
extern osThreadId_t EthInputTaskHandle;
extern osThreadId_t EthLinkTaskHandle;
extern osThreadId_t DisplayTaskHandle;
extern osThreadId_t WatchdogTaskHandle;

/* Message queues */
extern osMessageQueueId_t FrameQueue;         /* FrameMsg_t         */
extern osMessageQueueId_t DetectionQueue;     /* DetectionMsg_t     */
extern osMessageQueueId_t PlateResultQueue;   /* PlateResultMsg_t   */

/* Display overlay — protected by mutex */
extern DisplayOverlay_t   SharedOverlay;
extern osMutexId_t        OverlayMutex;

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  PUBLIC API                                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  Initialize all BSP peripherals, create sync objects, and spawn
  *         all application FreeRTOS tasks for the ALPR pipeline.
  * @note   Must be called from a FreeRTOS task context (after osKernelStart).
  */
void AppTasks_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */
