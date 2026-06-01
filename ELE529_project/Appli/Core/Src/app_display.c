/**
  ******************************************************************************
  * @file    app_display.c
  * @brief   Application-level LCD rendering for the ALPR overlay.
  ******************************************************************************
  */

#include "app_display.h"
#include "bsp_camera.h"
#include "bsp_lcd.h"
#include "main.h"

#define APP_DISPLAY_TEST_PERIOD_MS  100U

void AppDisplay_DrawBringupFrame(const DisplayOverlay_t *overlay,
                                 uint32_t camera_last_tick,
                                 uint8_t camera_started,
                                 uint32_t timeout_count)
{
  const uint16_t bars[] = {
    RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_YELLOW,
    RGB565_CYAN, RGB565_MAGENTA, RGB565_WHITE, RGB565_GRAY
  };
  uint32_t now = HAL_GetTick();
  uint8_t camera_live = ((camera_last_tick != 0U) &&
                         ((now - camera_last_tick) < 1000U)) ? 1U : 0U;

  if (overlay == NULL)
  {
    return;
  }

  AppDisplay_ClearBackBuffer(RGB565_BLACK);

  for (uint32_t i = 0; i < (sizeof(bars) / sizeof(bars[0])); i++)
  {
    BSP_LCD_FillRect(LCD_LAYER_0, i * 80U, 0U, 80U, 64U, bars[i]);
  }

  BSP_LCD_FillRect(LCD_LAYER_0, 0U, 70U, LCD_WIDTH, 4U,
                   camera_live ? RGB565_GREEN : RGB565_RED);
  BSP_LCD_FillRect(LCD_LAYER_0, 0U, 78U, camera_started ? 160U : 80U,
                   12U, camera_started ? RGB565_GREEN : RGB565_YELLOW);

  AppDisplay_DrawFrameCounter(overlay->frame_id, timeout_count);

  for (uint8_t i = 0; i < overlay->num_detections; i++)
  {
    const BBox_t *bb = &overlay->detections[i];
    uint32_t sx = ((uint32_t)bb->x * LCD_WIDTH) / CAMERA_AI_WIDTH;
    uint32_t sy = ((uint32_t)bb->y * LCD_HEIGHT) / CAMERA_AI_HEIGHT;
    uint32_t sw = ((uint32_t)bb->w * LCD_WIDTH) / CAMERA_AI_WIDTH;
    uint32_t sh = ((uint32_t)bb->h * LCD_HEIGHT) / CAMERA_AI_HEIGHT;
    uint16_t color = camera_live ? RGB565_GREEN : RGB565_YELLOW;

    AppDisplay_DrawRect(sx, sy, sw, sh, color);
  }

  /* Moving pulse proves DisplayTask is alive even before camera/link tests. */
  uint32_t pulse_x = (HAL_GetTick() / APP_DISPLAY_TEST_PERIOD_MS) %
                     (LCD_WIDTH - 48U);
  BSP_LCD_FillRect(LCD_LAYER_0, pulse_x, LCD_HEIGHT - 36U, 48U, 20U,
                   RGB565_CYAN);
}

void AppDisplay_DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
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

void AppDisplay_DrawFrameCounter(uint32_t frame_id, uint32_t timeout_count)
{
  for (uint32_t bit = 0; bit < 16U; bit++)
  {
    uint16_t color = ((frame_id >> bit) & 1U) ? RGB565_GREEN : RGB565_GRAY;
    BSP_LCD_FillRect(LCD_LAYER_0, 16U + (bit * 18U), 96U, 14U, 28U, color);
  }

  for (uint32_t bit = 0; bit < 8U; bit++)
  {
    uint16_t color = ((timeout_count >> bit) & 1U) ?
                     RGB565_RED : RGB565_GRAY;
    BSP_LCD_FillRect(LCD_LAYER_0, 16U + (bit * 18U), 132U, 14U, 14U, color);
  }
}

void AppDisplay_ClearBackBuffer(uint16_t color)
{
  uint16_t *fb = BSP_LCD_GetBackBuffer(LCD_LAYER_0);

  for (uint32_t i = 0; i < (LCD_WIDTH * LCD_HEIGHT); i++)
  {
    fb[i] = color;
  }
}
