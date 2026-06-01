/**
  ******************************************************************************
  * @file    bsp_board.c
  * @brief   Board-level abstraction for STM32N6570-DK platform bring-up.
  ******************************************************************************
  */

#include "bsp_board.h"
#include "bsp_camera.h"
#include "bsp_eth.h"
#include "bsp_lcd.h"
#include "bsp_security.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "bsp_watchdog.h"

BSP_Board_StatusTypeDef BSP_Board_Open(void)
{
  BSP_Board_GPIO_Open();

  if (BSP_Storage_Open() != BSP_STORAGE_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_Security_Open() != BSP_SECURITY_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_UART_Open() != BSP_UART_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_Watchdog_Open() != BSP_WATCHDOG_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_LCD_Init() != BSP_LCD_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_Camera_Init() != BSP_CAM_OK)
  {
    return BSP_BOARD_ERROR;
  }

  if (BSP_ETH_Open() != BSP_ETH_OK)
  {
    return BSP_BOARD_ERROR;
  }

  return BSP_BOARD_OK;
}

BSP_Board_StatusTypeDef BSP_Board_Close(void)
{
  BSP_Board_StatusTypeDef status = BSP_BOARD_OK;

  if (BSP_ETH_Close() != BSP_ETH_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_Camera_DeInit() != BSP_CAM_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_LCD_DeInit() != BSP_LCD_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_Watchdog_Close() != BSP_WATCHDOG_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_UART_Close() != BSP_UART_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_Security_Close() != BSP_SECURITY_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  if (BSP_Storage_Close() != BSP_STORAGE_OK)
  {
    status = BSP_BOARD_ERROR;
  }

  return status;
}

void BSP_Board_GPIO_Open(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOP_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOQ_CLK_ENABLE();
}
