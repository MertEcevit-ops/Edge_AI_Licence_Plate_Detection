/**
  ******************************************************************************
  * @file    bsp_lcd.h
  * @brief   BSP LCD (LTDC) driver for STM32N6570-DK (MB1860 panel)
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

#ifndef BSP_LCD_H
#define BSP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>

/* Exported defines ----------------------------------------------------------*/

/** @defgroup BSP_LCD_Defines LCD Defines
  * @{
  */

/* LCD Resolution - STM32N6570-DK MB1860 panel (HVGA) */
#define LCD_WIDTH                  640U
#define LCD_HEIGHT                 480U

/* Pixel format */
#define LCD_PIXEL_FORMAT_RGB565    LTDC_PIXEL_FORMAT_RGB565
#define LCD_PIXEL_FORMAT_ARGB8888  LTDC_PIXEL_FORMAT_ARGB8888

/* Default pixel format */
#define LCD_DEFAULT_PIXEL_FORMAT   LCD_PIXEL_FORMAT_RGB565
#define LCD_BYTES_PER_PIXEL        2U  /* RGB565 = 2 bytes */

/* Framebuffer size */
#define LCD_FB_SIZE                (LCD_WIDTH * LCD_HEIGHT * LCD_BYTES_PER_PIXEL)

/* Layer indices */
#define LCD_LAYER_0                0U
#define LCD_LAYER_1                1U

/* RGB565 colors */
#define RGB565_BLACK               0x0000U
#define RGB565_WHITE               0xFFFFU
#define RGB565_RED                 0xF800U
#define RGB565_GREEN               0x07E0U
#define RGB565_BLUE                0x001FU
#define RGB565_YELLOW              0xFFE0U
#define RGB565_CYAN                0x07FFU
#define RGB565_MAGENTA             0xF81FU
#define RGB565_GRAY                0x8410U

/* LCD control GPIOs */
#define LCD_BL_CTRL_PIN            GPIO_PIN_6   /* PQ6 */
#define LCD_BL_CTRL_PORT           GPIOQ
#define LCD_NRST_PIN               GPIO_PIN_1   /* PE1 */
#define LCD_NRST_PORT              GPIOE

/* LTDC timing parameters for MB1860 panel */
#define LCD_HSYNC                  8U
#define LCD_HBP                    7U
#define LCD_HFP                    6U
#define LCD_VSYNC                  4U
#define LCD_VBP                    2U
#define LCD_VFP                    2U

/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/

/** @defgroup BSP_LCD_Types LCD Types
  * @{
  */

typedef enum
{
  BSP_LCD_OK       = 0,
  BSP_LCD_ERROR    = 1,
  BSP_LCD_TIMEOUT  = 2,
} BSP_LCD_StatusTypeDef;

typedef struct
{
  uint32_t Width;
  uint32_t Height;
  uint32_t PixelFormat;
  uint32_t BytesPerPixel;
  uint32_t ActiveLayer;
  uint32_t *FrameBufferAddr[2];  /* Double-buffer support */
  uint8_t  BackBufferIdx;        /* Index of back buffer (0 or 1) */
} BSP_LCD_Ctx_t;

/**
  * @}
  */

/* Exported variables --------------------------------------------------------*/
extern BSP_LCD_Ctx_t LcdCtx;

/* Exported functions --------------------------------------------------------*/

/** @defgroup BSP_LCD_Functions LCD Functions
  * @{
  */

/**
  * @brief  Initialize the LCD peripheral (LTDC + GPIO + backlight).
  *         Opens LTDC through the LCD BSP driver.
  * @retval BSP_LCD_OK on success, BSP_LCD_ERROR otherwise
  */
BSP_LCD_StatusTypeDef BSP_LCD_Init(void);
BSP_LCD_StatusTypeDef BSP_LCD_Open(void);

/**
  * @brief  De-initialize the LCD peripheral.
  * @retval BSP_LCD_OK on success
  */
BSP_LCD_StatusTypeDef BSP_LCD_DeInit(void);

/**
  * @brief  Set the framebuffer address for a given layer.
  * @param  LayerIdx  Layer index (LCD_LAYER_0 or LCD_LAYER_1)
  * @param  Address   Framebuffer start address
  * @retval BSP_LCD_OK on success
  */
BSP_LCD_StatusTypeDef BSP_LCD_SetFramebuffer(uint32_t LayerIdx, uint32_t Address);

/**
  * @brief  Configure a layer with given parameters.
  * @param  LayerIdx     Layer index
  * @param  X0, Y0       Window start position
  * @param  Width,Height Window size
  * @param  PixelFormat  Pixel format (RGB565 or ARGB8888)
  * @param  FBAddr       Framebuffer address
  * @retval BSP_LCD_OK on success
  */
BSP_LCD_StatusTypeDef BSP_LCD_ConfigLayer(uint32_t LayerIdx,
                                          uint32_t X0, uint32_t Y0,
                                          uint32_t Width, uint32_t Height,
                                          uint32_t PixelFormat,
                                          uint32_t FBAddr);

/**
  * @brief  Clear the framebuffer of a given layer with a specified color.
  * @param  LayerIdx Layer index
  * @param  Color    16-bit RGB565 color value
  */
void BSP_LCD_Clear(uint32_t LayerIdx, uint16_t Color);

/**
  * @brief  Draw a single pixel.
  * @param  LayerIdx Layer index
  * @param  X, Y     Pixel position
  * @param  Color    16-bit RGB565 color value
  */
void BSP_LCD_DrawPixel(uint32_t LayerIdx, uint32_t X, uint32_t Y, uint16_t Color);

/**
  * @brief  Fill a rectangle with a solid color.
  * @param  LayerIdx      Layer index
  * @param  X, Y          Start position
  * @param  Width, Height Rectangle size
  * @param  Color         16-bit RGB565 color value
  */
void BSP_LCD_FillRect(uint32_t LayerIdx,
                      uint32_t X, uint32_t Y,
                      uint32_t Width, uint32_t Height,
                      uint16_t Color);

/**
  * @brief  Turn on the LCD display (enable backlight + LTDC output).
  */
void BSP_LCD_DisplayOn(void);

/**
  * @brief  Turn off the LCD display (disable backlight).
  */
void BSP_LCD_DisplayOff(void);

/**
  * @brief  Swap front and back framebuffers (double-buffering).
  *         Updates LTDC Layer0 address on next VSYNC.
  * @param  LayerIdx Layer index
  */
void BSP_LCD_SwapBuffers(uint32_t LayerIdx);

/**
  * @brief  Get pointer to the current back-buffer (for drawing).
  * @param  LayerIdx Layer index
  * @retval Pointer to framebuffer
  */
uint16_t *BSP_LCD_GetBackBuffer(uint32_t LayerIdx);

/**
  * @brief  LTDC line event callback (called at VSYNC).
  *         Weak function — can be overridden in user code.
  */
void BSP_LCD_LineEventCallback(void);
LTDC_HandleTypeDef *BSP_LCD_GetHandle(void);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* BSP_LCD_H */
