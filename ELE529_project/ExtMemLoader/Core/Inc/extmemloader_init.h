/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    extmemloader_init.h
  * @author  MCD Application Team
  * @brief   Header file of Loader_Src.c
  *
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
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef EXTMEMLOADER_INIT_H
#define EXTMEMLOADER_INIT_H

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"

/* Exported types ------------------------------------------------------------*/

/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/

/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/

/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

uint32_t extmemloader_Init(void);
void Error_Handler(void);

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#define OSC_IN_Pin GPIO_PIN_0
#define OSC_IN_GPIO_Port GPIOH
#define OSC_OUT_Pin GPIO_PIN_1
#define OSC_OUT_GPIO_Port GPIOH
#define ETH_MDINT_Pin GPIO_PIN_3
#define ETH_MDINT_GPIO_Port GPIOD
#define HEXASPI_IO_7_Pin GPIO_PIN_7
#define HEXASPI_IO_7_GPIO_Port GPIOP
#define HEXASPI_IO_6_Pin GPIO_PIN_6
#define HEXASPI_IO_6_GPIO_Port GPIOP
#define HEXASPI_IO_0_Pin GPIO_PIN_0
#define HEXASPI_IO_0_GPIO_Port GPIOP
#define HEXASPI_IO_4_Pin GPIO_PIN_4
#define HEXASPI_IO_4_GPIO_Port GPIOP
#define HEXASPI_IO_1_Pin GPIO_PIN_1
#define HEXASPI_IO_1_GPIO_Port GPIOP
#define HEXASPI_IO_15_Pin GPIO_PIN_15
#define HEXASPI_IO_15_GPIO_Port GPIOP
#define HEXASPI_IO_5_Pin GPIO_PIN_5
#define HEXASPI_IO_5_GPIO_Port GPIOP
#define HEXASPI_IO_12_Pin GPIO_PIN_12
#define HEXASPI_IO_12_GPIO_Port GPIOP
#define HEXASPI_IO_3_Pin GPIO_PIN_3
#define HEXASPI_IO_3_GPIO_Port GPIOP
#define HEXASPI_IO_2_Pin GPIO_PIN_2
#define HEXASPI_IO_2_GPIO_Port GPIOP
#define HEXASPI_IO_13_Pin GPIO_PIN_13
#define HEXASPI_IO_13_GPIO_Port GPIOP
#define HEXASPI_DQS0_Pin GPIO_PIN_2
#define HEXASPI_DQS0_GPIO_Port GPIOO
#define HEXASPI_IO_11_Pin GPIO_PIN_11
#define HEXASPI_IO_11_GPIO_Port GPIOP
#define HEXASPI_IO_8_Pin GPIO_PIN_8
#define HEXASPI_IO_8_GPIO_Port GPIOP
#define HEXASPI_IO_14_Pin GPIO_PIN_14
#define HEXASPI_IO_14_GPIO_Port GPIOP
#define ETH_CLK_Pin GPIO_PIN_5
#define ETH_CLK_GPIO_Port GPIOF
#define HEXASPI_DQS1_Pin GPIO_PIN_3
#define HEXASPI_DQS1_GPIO_Port GPIOO
#define HEXASPI_NCS_Pin GPIO_PIN_0
#define HEXASPI_NCS_GPIO_Port GPIOO
#define ETH_RXD2_Pin GPIO_PIN_8
#define ETH_RXD2_GPIO_Port GPIOF
#define ETH_CLK125_Pin GPIO_PIN_2
#define ETH_CLK125_GPIO_Port GPIOF
#define HEXASPI_IO_9_Pin GPIO_PIN_9
#define HEXASPI_IO_9_GPIO_Port GPIOP
#define HEXASPI_IO_10_Pin GPIO_PIN_10
#define HEXASPI_IO_10_GPIO_Port GPIOP
#define HEXASPI_CLK_Pin GPIO_PIN_4
#define HEXASPI_CLK_GPIO_Port GPIOO
#define ETH_RXD3_Pin GPIO_PIN_9
#define ETH_RXD3_GPIO_Port GPIOF
#define ETH_TXD3_Pin GPIO_PIN_4
#define ETH_TXD3_GPIO_Port GPIOG
#define ETH_TX2_Pin GPIO_PIN_3
#define ETH_TX2_GPIO_Port GPIOG
#define ETH_GTX_CLK_Pin GPIO_PIN_0
#define ETH_GTX_CLK_GPIO_Port GPIOF
#endif /* EXTMEMLOADER_INIT_H */
