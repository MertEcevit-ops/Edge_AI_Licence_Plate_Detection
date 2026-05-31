/**
  ******************************************************************************
  * @file    bsp_camera.h
  * @brief   BSP Camera (CSI/DCMIPP) driver for STM32N6570-DK
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>

/* Exported defines ----------------------------------------------------------*/

/** @defgroup BSP_Camera_Defines Camera Defines
  * @{
  */

/* Camera sensor default resolution */
#define CAMERA_PREVIEW_WIDTH        640U
#define CAMERA_PREVIEW_HEIGHT       480U

/* AI inference input resolution (typically square, downscaled) */
#define CAMERA_AI_WIDTH             320U
#define CAMERA_AI_HEIGHT            320U

/* Pixel formats */
#define CAMERA_PF_RGB565            DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1
#define CAMERA_PF_RGB888            DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1

/* Camera control GPIOs */
#define CAMERA_NRST_PIN             GPIO_PIN_8   /* PC8 — NRST_CAM */
#define CAMERA_NRST_PORT            GPIOC

/* DCMIPP Pipe assignments */
#define CAMERA_PIPE_PREVIEW         DCMIPP_PIPE0   /* LCD preview (RGB565)  */
#define CAMERA_PIPE_AI              DCMIPP_PIPE1   /* AI inference (RGB888) */
#define CAMERA_PIPE_AUX             DCMIPP_PIPE2   /* Auxiliary / metadata  */

/* Preview framebuffer size (RGB565) */
#define CAMERA_PREVIEW_FB_SIZE      (CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2U)

/* AI framebuffer size (RGB888) */
#define CAMERA_AI_FB_SIZE           (CAMERA_AI_WIDTH * CAMERA_AI_HEIGHT * 3U)

/* Camera sensor I2C address (7-bit) — update for your sensor */
#define CAMERA_I2C_ADDRESS          0x1AU  /* Typical for IMX335; OV5640 = 0x3C */

/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/

/** @defgroup BSP_Camera_Types Camera Types
  * @{
  */

typedef enum
{
  BSP_CAM_OK       = 0,
  BSP_CAM_ERROR    = 1,
  BSP_CAM_TIMEOUT  = 2,
  BSP_CAM_BUSY     = 3,
} BSP_Camera_StatusTypeDef;

typedef enum
{
  CAMERA_STATE_RESET = 0,
  CAMERA_STATE_INIT,
  CAMERA_STATE_RUNNING,
  CAMERA_STATE_SUSPENDED,
  CAMERA_STATE_ERROR,
} BSP_Camera_StateTypeDef;

typedef enum
{
  CAMERA_PIPE_TYPE_PREVIEW = 0,
  CAMERA_PIPE_TYPE_AI,
  CAMERA_PIPE_TYPE_AUX,
} BSP_Camera_PipeType_t;

/**
  * @brief  Frame ready callback function pointer type.
  * @param  pipe    Which pipe generated the frame
  * @param  fb_addr Framebuffer address containing the captured frame
  */
typedef void (*BSP_Camera_FrameReadyCb_t)(BSP_Camera_PipeType_t pipe, uint32_t fb_addr);

typedef struct
{
  BSP_Camera_StateTypeDef   State;
  uint32_t                  PreviewWidth;
  uint32_t                  PreviewHeight;
  uint32_t                  AIWidth;
  uint32_t                  AIHeight;
  uint32_t                  PreviewFBAddr;
  uint32_t                  AIFBAddr;
  BSP_Camera_FrameReadyCb_t FrameReadyCb;
} BSP_Camera_Ctx_t;

/**
  * @}
  */

/* Exported variables --------------------------------------------------------*/
extern BSP_Camera_Ctx_t CameraCtx;

/* Exported functions --------------------------------------------------------*/

/** @defgroup BSP_Camera_Functions Camera Functions
  * @{
  */

/**
  * @brief  Initialize the camera subsystem (CSI PHY, DCMIPP pipes, sensor reset).
  * @note   This function configures DCMIPP pipes but does NOT start capture.
  *         Call BSP_Camera_Start() to begin capturing frames.
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_Init(void);
BSP_Camera_StatusTypeDef BSP_Camera_Open(void);

/**
  * @brief  De-initialize the camera subsystem.
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_DeInit(void);

/**
  * @brief  Start continuous frame capture on specified pipe.
  * @param  pipe    Pipe type (PREVIEW, AI, or AUX)
  * @param  fb_addr Destination framebuffer address
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_Start(BSP_Camera_PipeType_t pipe, uint32_t fb_addr);

/**
  * @brief  Stop frame capture on specified pipe.
  * @param  pipe    Pipe type
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_Stop(BSP_Camera_PipeType_t pipe);

/**
  * @brief  Suspend frame capture (pause without full de-init).
  * @param  pipe    Pipe type
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_Suspend(BSP_Camera_PipeType_t pipe);

/**
  * @brief  Resume previously suspended capture.
  * @param  pipe    Pipe type
  * @retval BSP_CAM_OK on success
  */
BSP_Camera_StatusTypeDef BSP_Camera_Resume(BSP_Camera_PipeType_t pipe);

/**
  * @brief  Get the last captured frame buffer address.
  * @param  pipe    Pipe type
  * @retval Framebuffer address, or 0 if no frame available
  */
uint32_t BSP_Camera_GetFrameBuffer(BSP_Camera_PipeType_t pipe);

/**
  * @brief  Register a frame-ready callback.
  * @param  cb  Callback function pointer
  */
void BSP_Camera_RegisterCallback(BSP_Camera_FrameReadyCb_t cb);

/**
  * @brief  Hardware reset the camera sensor via NRST_CAM pin.
  */
void BSP_Camera_HW_Reset(void);
DCMIPP_HandleTypeDef *BSP_Camera_GetHandle(void);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAMERA_H */
