/**
  ******************************************************************************
  * @file    app_display.h
  * @brief   Application-level LCD rendering for the ALPR overlay.
  ******************************************************************************
  */

#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_tasks.h"
#include <stdint.h>

void AppDisplay_DrawBringupFrame(const DisplayOverlay_t *overlay,
                                 uint32_t camera_last_tick,
                                 uint8_t camera_started,
                                 uint32_t timeout_count);
void AppDisplay_DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint16_t color);
void AppDisplay_DrawFrameCounter(uint32_t frame_id, uint32_t timeout_count);
void AppDisplay_ClearBackBuffer(uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
