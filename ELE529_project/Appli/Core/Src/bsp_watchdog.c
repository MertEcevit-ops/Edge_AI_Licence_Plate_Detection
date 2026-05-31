/**
  ******************************************************************************
  * @file    bsp_watchdog.c
  * @brief   Independent watchdog driver wrapper.
  ******************************************************************************
  */

#include "bsp_watchdog.h"

static IWDG_HandleTypeDef hiwdg;
static uint8_t watchdog_opened = 0U;

BSP_Watchdog_StatusTypeDef BSP_Watchdog_Open(void)
{
  if (watchdog_opened != 0U)
  {
    return BSP_WATCHDOG_OK;
  }

  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Window = 4095;
  hiwdg.Init.Reload = 2500;
  hiwdg.Init.EWI = 0;

  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    return BSP_WATCHDOG_ERROR;
  }

  watchdog_opened = 1U;
  return BSP_WATCHDOG_OK;
}

BSP_Watchdog_StatusTypeDef BSP_Watchdog_Refresh(void)
{
  if (BSP_Watchdog_Open() != BSP_WATCHDOG_OK)
  {
    return BSP_WATCHDOG_ERROR;
  }

  if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
  {
    return BSP_WATCHDOG_ERROR;
  }

  return BSP_WATCHDOG_OK;
}

IWDG_HandleTypeDef *BSP_Watchdog_GetHandle(void)
{
  return &hiwdg;
}
