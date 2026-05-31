/**
  ******************************************************************************
  * @file    bsp_uart.c
  * @brief   UART driver wrapper used by X-CUBE-AI validation console.
  ******************************************************************************
  */

#include "bsp_uart.h"

UART_HandleTypeDef huart1;

static uint8_t uart_opened = 0U;

BSP_UART_StatusTypeDef BSP_UART_Open(void)
{
  if (uart_opened != 0U)
  {
    return BSP_UART_OK;
  }

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    return BSP_UART_ERROR;
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart1,
                                    UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    return BSP_UART_ERROR;
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart1,
                                    UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    return BSP_UART_ERROR;
  }

  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    return BSP_UART_ERROR;
  }

  uart_opened = 1U;
  return BSP_UART_OK;
}

UART_HandleTypeDef *BSP_UART_GetHandle(void)
{
  return &huart1;
}

void MX_USART1_UART_Init(void)
{
  if (BSP_UART_Open() != BSP_UART_OK)
  {
    Error_Handler();
  }
}
