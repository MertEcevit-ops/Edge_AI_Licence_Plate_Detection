/**
  ******************************************************************************
  * @file    bsp_uart.h
  * @brief   UART driver wrapper used by X-CUBE-AI validation console.
  ******************************************************************************
  */

#ifndef BSP_UART_H
#define BSP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  BSP_UART_OK    = 0,
  BSP_UART_ERROR = 1,
} BSP_UART_StatusTypeDef;

BSP_UART_StatusTypeDef BSP_UART_Open(void);
UART_HandleTypeDef *BSP_UART_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
