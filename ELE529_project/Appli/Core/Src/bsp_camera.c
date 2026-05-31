/**
  ******************************************************************************
  * @file    bsp_camera.c
  * @brief   BSP Camera (CSI/DCMIPP) driver implementation for STM32N6570-DK
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bsp_camera.h"
#include <string.h>

/* Private variables ---------------------------------------------------------*/

/* Camera context */
BSP_Camera_Ctx_t CameraCtx = {0};

static DCMIPP_HandleTypeDef hdcmipp;
static uint8_t camera_opened = 0U;

/* Frame buffers — placed in noncacheable region for DMA coherency */
static uint8_t __attribute__((section("noncacheable_buffer"), aligned(32)))
    camera_preview_fb[CAMERA_PREVIEW_FB_SIZE];

static uint8_t __attribute__((section("noncacheable_buffer"), aligned(32)))
    camera_ai_fb[CAMERA_AI_FB_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void Camera_GPIO_Init(void);
static DCMIPP_PipeConfTypeDef Camera_GetPipeConfig(BSP_Camera_PipeType_t pipe);

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Initialize the camera subsystem.
  */
BSP_Camera_StatusTypeDef BSP_Camera_Init(void)
{
  if (BSP_Camera_Open() != BSP_CAM_OK)
  {
    return BSP_CAM_ERROR;
  }

  /* Initialize context */
  CameraCtx.State           = CAMERA_STATE_RESET;
  CameraCtx.PreviewWidth    = CAMERA_PREVIEW_WIDTH;
  CameraCtx.PreviewHeight   = CAMERA_PREVIEW_HEIGHT;
  CameraCtx.AIWidth         = CAMERA_AI_WIDTH;
  CameraCtx.AIHeight        = CAMERA_AI_HEIGHT;
  CameraCtx.PreviewFBAddr   = (uint32_t)camera_preview_fb;
  CameraCtx.AIFBAddr        = (uint32_t)camera_ai_fb;
  CameraCtx.FrameReadyCb    = NULL;

  /* Initialize camera reset GPIO */
  Camera_GPIO_Init();

  /* Hardware reset the camera sensor */
  BSP_Camera_HW_Reset();

  /* Clear framebuffers */
  memset(camera_preview_fb, 0, sizeof(camera_preview_fb));
  memset(camera_ai_fb, 0, sizeof(camera_ai_fb));

  CameraCtx.State = CAMERA_STATE_INIT;

  return BSP_CAM_OK;
}

BSP_Camera_StatusTypeDef BSP_Camera_Open(void)
{
  DCMIPP_CSI_PIPE_ConfTypeDef pCSI_PipeConfig = {0};
  DCMIPP_CSI_ConfTypeDef pCSI_Config = {0};
  DCMIPP_PipeConfTypeDef pPipeConfig = {0};

  if (camera_opened != 0U)
  {
    return BSP_CAM_OK;
  }

  hdcmipp.Instance = DCMIPP;
  if (HAL_DCMIPP_Init(&hdcmipp) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pCSI_PipeConfig.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  pCSI_PipeConfig.DataTypeIDA = DCMIPP_DT_YUV420_8;
  pCSI_PipeConfig.DataTypeIDB = DCMIPP_DT_YUV420_8;
  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE0,
                                    &pCSI_PipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pCSI_Config.PHYBitrate = DCMIPP_CSI_PHY_BT_80;
  pCSI_Config.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  pCSI_Config.NumberOfLanes = DCMIPP_CSI_ONE_DATA_LANE;
  if (HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &pCSI_Config) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pPipeConfig.FrameRate = DCMIPP_FRAME_RATE_ALL;
  pPipeConfig.PixelPipePitch = CAMERA_PREVIEW_WIDTH * 2U;
  pPipeConfig.PixelPackerFormat = CAMERA_PF_RGB565;
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE0,
                                &pPipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, 0U, DCMIPP_CSI_DT_BPP6);

  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1,
                                    &pCSI_PipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pPipeConfig.PixelPipePitch = CAMERA_AI_WIDTH * 3U;
  pPipeConfig.PixelPackerFormat = CAMERA_PF_RGB888;
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1,
                                &pPipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pCSI_PipeConfig.DataTypeIDB = DCMIPP_DT_RGB565;
  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE2,
                                    &pCSI_PipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  pPipeConfig.FrameRate = DCMIPP_FRAME_RATE_1_OVER_4;
  pPipeConfig.PixelPipePitch = 10U;
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE2,
                                &pPipeConfig) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  camera_opened = 1U;
  return BSP_CAM_OK;
}

/**
  * @brief  De-initialize the camera subsystem.
  */
BSP_Camera_StatusTypeDef BSP_Camera_DeInit(void)
{
  /* Stop all pipes */
  BSP_Camera_Stop(CAMERA_PIPE_TYPE_PREVIEW);
  BSP_Camera_Stop(CAMERA_PIPE_TYPE_AI);

  HAL_DCMIPP_DeInit(&hdcmipp);
  CameraCtx.State = CAMERA_STATE_RESET;
  camera_opened = 0U;

  return BSP_CAM_OK;
}

/**
  * @brief  Start continuous capture on a specific pipe.
  */
BSP_Camera_StatusTypeDef BSP_Camera_Start(BSP_Camera_PipeType_t pipe, uint32_t fb_addr)
{
  uint32_t dcmipp_pipe;
  HAL_StatusTypeDef hal_status;

  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW:
      dcmipp_pipe = CAMERA_PIPE_PREVIEW;
      if (fb_addr == 0) fb_addr = CameraCtx.PreviewFBAddr;
      break;

    case CAMERA_PIPE_TYPE_AI:
      dcmipp_pipe = CAMERA_PIPE_AI;
      if (fb_addr == 0) fb_addr = CameraCtx.AIFBAddr;
      break;

    case CAMERA_PIPE_TYPE_AUX:
      dcmipp_pipe = CAMERA_PIPE_AUX;
      break;

    default:
      return BSP_CAM_ERROR;
  }

  /* The STM32N6570-DK camera is wired through CSI, so use the CSI pipe API. */
  hal_status = HAL_DCMIPP_CSI_PIPE_Start(&hdcmipp, dcmipp_pipe, 0U,
                                          fb_addr, DCMIPP_MODE_CONTINUOUS);
  if (hal_status != HAL_OK)
  {
    CameraCtx.State = CAMERA_STATE_ERROR;
    return BSP_CAM_ERROR;
  }

  CameraCtx.State = CAMERA_STATE_RUNNING;
  return BSP_CAM_OK;
}

/**
  * @brief  Stop capture on a specific pipe.
  */
BSP_Camera_StatusTypeDef BSP_Camera_Stop(BSP_Camera_PipeType_t pipe)
{
  uint32_t dcmipp_pipe;

  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW: dcmipp_pipe = CAMERA_PIPE_PREVIEW; break;
    case CAMERA_PIPE_TYPE_AI:     dcmipp_pipe = CAMERA_PIPE_AI;      break;
    case CAMERA_PIPE_TYPE_AUX:    dcmipp_pipe = CAMERA_PIPE_AUX;     break;
    default: return BSP_CAM_ERROR;
  }

  if (HAL_DCMIPP_PIPE_Stop(&hdcmipp, dcmipp_pipe) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  return BSP_CAM_OK;
}

/**
  * @brief  Suspend capture (pause without de-init).
  */
BSP_Camera_StatusTypeDef BSP_Camera_Suspend(BSP_Camera_PipeType_t pipe)
{
  uint32_t dcmipp_pipe;

  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW: dcmipp_pipe = CAMERA_PIPE_PREVIEW; break;
    case CAMERA_PIPE_TYPE_AI:     dcmipp_pipe = CAMERA_PIPE_AI;      break;
    case CAMERA_PIPE_TYPE_AUX:    dcmipp_pipe = CAMERA_PIPE_AUX;     break;
    default: return BSP_CAM_ERROR;
  }

  if (HAL_DCMIPP_PIPE_Suspend(&hdcmipp, dcmipp_pipe) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  CameraCtx.State = CAMERA_STATE_SUSPENDED;
  return BSP_CAM_OK;
}

/**
  * @brief  Resume previously suspended capture.
  */
BSP_Camera_StatusTypeDef BSP_Camera_Resume(BSP_Camera_PipeType_t pipe)
{
  uint32_t dcmipp_pipe;

  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW: dcmipp_pipe = CAMERA_PIPE_PREVIEW; break;
    case CAMERA_PIPE_TYPE_AI:     dcmipp_pipe = CAMERA_PIPE_AI;      break;
    case CAMERA_PIPE_TYPE_AUX:    dcmipp_pipe = CAMERA_PIPE_AUX;     break;
    default: return BSP_CAM_ERROR;
  }

  if (HAL_DCMIPP_PIPE_Resume(&hdcmipp, dcmipp_pipe) != HAL_OK)
  {
    return BSP_CAM_ERROR;
  }

  CameraCtx.State = CAMERA_STATE_RUNNING;
  return BSP_CAM_OK;
}

/**
  * @brief  Get the framebuffer address for a pipe.
  */
uint32_t BSP_Camera_GetFrameBuffer(BSP_Camera_PipeType_t pipe)
{
  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW: return CameraCtx.PreviewFBAddr;
    case CAMERA_PIPE_TYPE_AI:     return CameraCtx.AIFBAddr;
    default: return 0;
  }
}

/**
  * @brief  Register a frame-ready callback.
  */
void BSP_Camera_RegisterCallback(BSP_Camera_FrameReadyCb_t cb)
{
  CameraCtx.FrameReadyCb = cb;
}

/**
  * @brief  Hardware reset the camera sensor.
  */
void BSP_Camera_HW_Reset(void)
{
  /* Assert reset (active low) */
  HAL_GPIO_WritePin(CAMERA_NRST_PORT, CAMERA_NRST_PIN, GPIO_PIN_RESET);
  HAL_Delay(10);

  /* Release reset */
  HAL_GPIO_WritePin(CAMERA_NRST_PORT, CAMERA_NRST_PIN, GPIO_PIN_SET);
  HAL_Delay(50);  /* Wait for sensor PLL lock */
}

DCMIPP_HandleTypeDef *BSP_Camera_GetHandle(void)
{
  return &hdcmipp;
}

/* HAL Callbacks -------------------------------------------------------------*/

/**
  * @brief  DCMIPP Frame Event callback — called when a complete frame is captured.
  */
void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp_ptr,
                                         uint32_t Pipe)
{
  BSP_Camera_PipeType_t pipe_type;
  uint32_t fb_addr = 0;

  if (Pipe == CAMERA_PIPE_PREVIEW)
  {
    pipe_type = CAMERA_PIPE_TYPE_PREVIEW;
    fb_addr   = CameraCtx.PreviewFBAddr;
  }
  else if (Pipe == CAMERA_PIPE_AI)
  {
    pipe_type = CAMERA_PIPE_TYPE_AI;
    fb_addr   = CameraCtx.AIFBAddr;
  }
  else
  {
    pipe_type = CAMERA_PIPE_TYPE_AUX;
  }

  /* Notify application via registered callback */
  if (CameraCtx.FrameReadyCb != NULL)
  {
    CameraCtx.FrameReadyCb(pipe_type, fb_addr);
  }
}

/**
  * @brief  DCMIPP VSYNC Event callback.
  */
void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp_ptr,
                                          uint32_t Pipe)
{
  /* Can be used for frame synchronization if needed */
  (void)hdcmipp_ptr;
  (void)Pipe;
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initialize camera reset GPIO (PC8).
  */
static void Camera_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();

  gpio.Pin   = CAMERA_NRST_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CAMERA_NRST_PORT, &gpio);

  /* Start with reset de-asserted */
  HAL_GPIO_WritePin(CAMERA_NRST_PORT, CAMERA_NRST_PIN, GPIO_PIN_SET);
}

/**
  * @brief  Get DCMIPP pipe configuration for a given pipe type.
  *         (Helper for dynamic reconfiguration if needed)
  */
static DCMIPP_PipeConfTypeDef Camera_GetPipeConfig(BSP_Camera_PipeType_t pipe)
{
  DCMIPP_PipeConfTypeDef cfg = {0};

  switch (pipe)
  {
    case CAMERA_PIPE_TYPE_PREVIEW:
      cfg.FrameRate         = DCMIPP_FRAME_RATE_ALL;
      cfg.PixelPipePitch    = CAMERA_PREVIEW_WIDTH * 2U;  /* RGB565 */
      cfg.PixelPackerFormat = CAMERA_PF_RGB565;
      break;

    case CAMERA_PIPE_TYPE_AI:
      cfg.FrameRate         = DCMIPP_FRAME_RATE_ALL;
      cfg.PixelPipePitch    = CAMERA_AI_WIDTH * 3U;  /* RGB888 */
      cfg.PixelPackerFormat = CAMERA_PF_RGB888;
      break;

    case CAMERA_PIPE_TYPE_AUX:
      cfg.FrameRate         = DCMIPP_FRAME_RATE_1_OVER_4;
      cfg.PixelPipePitch    = 10;
      cfg.PixelPackerFormat = CAMERA_PF_RGB888;
      break;
  }

  return cfg;
}
