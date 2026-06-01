/**
  ******************************************************************************
  * @file    bsp_watchdog.h
  * @brief   Independent watchdog driver wrapper.
  ******************************************************************************
  */

#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  BSP_WATCHDOG_OK    = 0,
  BSP_WATCHDOG_ERROR = 1,
} BSP_Watchdog_StatusTypeDef;

BSP_Watchdog_StatusTypeDef BSP_Watchdog_Open(void);
BSP_Watchdog_StatusTypeDef BSP_Watchdog_Close(void);
BSP_Watchdog_StatusTypeDef BSP_Watchdog_Refresh(void);
IWDG_HandleTypeDef *BSP_Watchdog_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_WATCHDOG_H */
