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

#ifndef VCP_TX_Pin
#define VCP_TX_Pin GPIO_PIN_5
#define VCP_TX_GPIO_Port GPIOE
#endif

#ifndef VCP_RX_Pin
#define VCP_RX_Pin GPIO_PIN_6
#define VCP_RX_GPIO_Port GPIOE
#endif

typedef enum
{
  BSP_UART_OK    = 0,
  BSP_UART_ERROR = 1,
} BSP_UART_StatusTypeDef;

BSP_UART_StatusTypeDef BSP_UART_Open(void);
BSP_UART_StatusTypeDef BSP_UART_Close(void);
UART_HandleTypeDef *BSP_UART_GetHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
