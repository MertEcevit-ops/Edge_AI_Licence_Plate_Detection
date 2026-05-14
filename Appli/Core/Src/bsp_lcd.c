/**
  ******************************************************************************
  * @file    bsp_lcd.c
  * @brief   BSP LCD (LTDC) driver implementation for STM32N6570-DK
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bsp_lcd.h"
#include <string.h>

/* Private variables ---------------------------------------------------------*/

/* Framebuffers — placed in noncacheable region for DMA coherency */
static uint16_t __attribute__((section("noncacheable_buffer"), aligned(32)))
    lcd_fb0[LCD_WIDTH * LCD_HEIGHT];

static uint16_t __attribute__((section("noncacheable_buffer"), aligned(32)))
    lcd_fb1[LCD_WIDTH * LCD_HEIGHT];

/* LCD context */
BSP_LCD_Ctx_t LcdCtx = {0};

/* External LTDC handle from CubeMX-generated main.c */
extern LTDC_HandleTypeDef hltdc;

/* Private function prototypes -----------------------------------------------*/
static void LCD_GPIO_BacklightInit(void);
static void LCD_HW_Reset(void);

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Initialize the LCD peripheral.
  */
BSP_LCD_StatusTypeDef BSP_LCD_Init(void)
{
  LTDC_LayerCfgTypeDef layerCfg = {0};

  /* Initialize LCD context */
  LcdCtx.Width          = LCD_WIDTH;
  LcdCtx.Height         = LCD_HEIGHT;
  LcdCtx.PixelFormat    = LCD_DEFAULT_PIXEL_FORMAT;
  LcdCtx.BytesPerPixel  = LCD_BYTES_PER_PIXEL;
  LcdCtx.ActiveLayer    = LCD_LAYER_0;
  LcdCtx.FrameBufferAddr[0] = (uint32_t *)lcd_fb0;
  LcdCtx.FrameBufferAddr[1] = (uint32_t *)lcd_fb1;
  LcdCtx.BackBufferIdx  = 1;

  /* Initialize backlight GPIO */
  LCD_GPIO_BacklightInit();

  /* Hardware reset the LCD panel */
  LCD_HW_Reset();

  /* Configure Layer 0 for full-screen RGB565 */
  layerCfg.WindowX0       = 0;
  layerCfg.WindowX1       = LCD_WIDTH;
  layerCfg.WindowY0       = 0;
  layerCfg.WindowY1       = LCD_HEIGHT;
  layerCfg.PixelFormat    = LTDC_PIXEL_FORMAT_RGB565;
  layerCfg.Alpha          = 255;
  layerCfg.Alpha0         = 0;
  layerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layerCfg.FBStartAdress  = (uint32_t)lcd_fb0;
  layerCfg.ImageWidth     = LCD_WIDTH;
  layerCfg.ImageHeight    = LCD_HEIGHT;
  layerCfg.Backcolor.Blue  = 0;
  layerCfg.Backcolor.Green = 0;
  layerCfg.Backcolor.Red   = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &layerCfg, LCD_LAYER_0) != HAL_OK)
  {
    return BSP_LCD_ERROR;
  }

  /* Clear both framebuffers to black */
  memset(lcd_fb0, 0, sizeof(lcd_fb0));
  memset(lcd_fb1, 0, sizeof(lcd_fb1));

  /* Enable LTDC line interrupt for VSYNC-based buffer swap */
  HAL_LTDC_ProgramLineEvent(&hltdc, 0);

  /* Turn on the display */
  BSP_LCD_DisplayOn();

  return BSP_LCD_OK;
}

/**
  * @brief  De-initialize the LCD peripheral.
  */
BSP_LCD_StatusTypeDef BSP_LCD_DeInit(void)
{
  BSP_LCD_DisplayOff();
  HAL_LTDC_DeInit(&hltdc);
  return BSP_LCD_OK;
}

/**
  * @brief  Set framebuffer address for a layer.
  */
BSP_LCD_StatusTypeDef BSP_LCD_SetFramebuffer(uint32_t LayerIdx, uint32_t Address)
{
  if (HAL_LTDC_SetAddress(&hltdc, Address, LayerIdx) != HAL_OK)
  {
    return BSP_LCD_ERROR;
  }
  return BSP_LCD_OK;
}

/**
  * @brief  Configure a layer with given parameters.
  */
BSP_LCD_StatusTypeDef BSP_LCD_ConfigLayer(uint32_t LayerIdx,
                                          uint32_t X0, uint32_t Y0,
                                          uint32_t Width, uint32_t Height,
                                          uint32_t PixelFormat,
                                          uint32_t FBAddr)
{
  LTDC_LayerCfgTypeDef layerCfg = {0};

  layerCfg.WindowX0       = X0;
  layerCfg.WindowX1       = X0 + Width;
  layerCfg.WindowY0       = Y0;
  layerCfg.WindowY1       = Y0 + Height;
  layerCfg.PixelFormat    = PixelFormat;
  layerCfg.Alpha          = 255;
  layerCfg.Alpha0         = 0;
  layerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layerCfg.FBStartAdress  = FBAddr;
  layerCfg.ImageWidth     = Width;
  layerCfg.ImageHeight    = Height;
  layerCfg.Backcolor.Blue  = 0;
  layerCfg.Backcolor.Green = 0;
  layerCfg.Backcolor.Red   = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &layerCfg, LayerIdx) != HAL_OK)
  {
    return BSP_LCD_ERROR;
  }

  return BSP_LCD_OK;
}

/**
  * @brief  Clear framebuffer with a color.
  */
void BSP_LCD_Clear(uint32_t LayerIdx, uint16_t Color)
{
  uint16_t *fb = (uint16_t *)LcdCtx.FrameBufferAddr[LayerIdx];
  uint32_t pixels = LCD_WIDTH * LCD_HEIGHT;

  for (uint32_t i = 0; i < pixels; i++)
  {
    fb[i] = Color;
  }
}

/**
  * @brief  Draw a single pixel.
  */
void BSP_LCD_DrawPixel(uint32_t LayerIdx, uint32_t X, uint32_t Y, uint16_t Color)
{
  if ((X < LCD_WIDTH) && (Y < LCD_HEIGHT))
  {
    uint16_t *fb = (uint16_t *)LcdCtx.FrameBufferAddr[LcdCtx.BackBufferIdx];
    fb[Y * LCD_WIDTH + X] = Color;
  }
}

/**
  * @brief  Fill a rectangle with a solid color.
  */
void BSP_LCD_FillRect(uint32_t LayerIdx,
                      uint32_t X, uint32_t Y,
                      uint32_t Width, uint32_t Height,
                      uint16_t Color)
{
  uint16_t *fb = (uint16_t *)LcdCtx.FrameBufferAddr[LcdCtx.BackBufferIdx];

  /* Clamp to framebuffer boundaries */
  if (X + Width > LCD_WIDTH) Width = LCD_WIDTH - X;
  if (Y + Height > LCD_HEIGHT) Height = LCD_HEIGHT - Y;

  for (uint32_t row = Y; row < Y + Height; row++)
  {
    for (uint32_t col = X; col < X + Width; col++)
    {
      fb[row * LCD_WIDTH + col] = Color;
    }
  }
}

/**
  * @brief  Turn on the LCD display (backlight + LTDC).
  */
void BSP_LCD_DisplayOn(void)
{
  /* Enable LTDC */
  __HAL_LTDC_ENABLE(&hltdc);

  /* Turn on backlight (PQ6 high) */
  HAL_GPIO_WritePin(LCD_BL_CTRL_PORT, LCD_BL_CTRL_PIN, GPIO_PIN_SET);
}

/**
  * @brief  Turn off the LCD display.
  */
void BSP_LCD_DisplayOff(void)
{
  /* Turn off backlight */
  HAL_GPIO_WritePin(LCD_BL_CTRL_PORT, LCD_BL_CTRL_PIN, GPIO_PIN_RESET);

  /* Disable LTDC */
  __HAL_LTDC_DISABLE(&hltdc);
}

/**
  * @brief  Swap front and back buffers (double-buffering).
  */
void BSP_LCD_SwapBuffers(uint32_t LayerIdx)
{
  /* Set the LTDC layer address to the current back-buffer */
  HAL_LTDC_SetAddress_NoReload(&hltdc,
                                (uint32_t)LcdCtx.FrameBufferAddr[LcdCtx.BackBufferIdx],
                                LayerIdx);

  /* Reload on next VSYNC */
  HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);

  /* Toggle back-buffer index */
  LcdCtx.BackBufferIdx = (LcdCtx.BackBufferIdx == 0) ? 1 : 0;
}

/**
  * @brief  Get pointer to the current back-buffer.
  */
uint16_t *BSP_LCD_GetBackBuffer(uint32_t LayerIdx)
{
  (void)LayerIdx;
  return (uint16_t *)LcdCtx.FrameBufferAddr[LcdCtx.BackBufferIdx];
}

/**
  * @brief  LTDC Line Event callback — called at VSYNC.
  */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc_ptr)
{
  /* Re-program the line event for the next frame */
  HAL_LTDC_ProgramLineEvent(hltdc_ptr, 0);

  /* Call user callback */
  BSP_LCD_LineEventCallback();
}

/**
  * @brief  Weak user callback for VSYNC events.
  */
__weak void BSP_LCD_LineEventCallback(void)
{
  /* Override this in application code if needed */
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initialize the LCD backlight GPIO (PQ6).
  */
static void LCD_GPIO_BacklightInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOQ_CLK_ENABLE();

  gpio.Pin   = LCD_BL_CTRL_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_BL_CTRL_PORT, &gpio);

  /* Start with backlight off */
  HAL_GPIO_WritePin(LCD_BL_CTRL_PORT, LCD_BL_CTRL_PIN, GPIO_PIN_RESET);
}

/**
  * @brief  Hardware reset the LCD panel via PE1 (LCD_NRST).
  */
static void LCD_HW_Reset(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();

  gpio.Pin   = LCD_NRST_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_NRST_PORT, &gpio);

  /* Reset sequence: LOW -> wait -> HIGH -> wait */
  HAL_GPIO_WritePin(LCD_NRST_PORT, LCD_NRST_PIN, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_NRST_PORT, LCD_NRST_PIN, GPIO_PIN_SET);
  HAL_Delay(10);
}
