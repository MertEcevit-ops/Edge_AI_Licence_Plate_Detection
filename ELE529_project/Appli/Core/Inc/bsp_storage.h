/**
  ******************************************************************************
  * @file    bsp_storage.h
  * @brief   Storage and memory peripheral driver wrapper.
  ******************************************************************************
  */

#ifndef BSP_STORAGE_H
#define BSP_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifndef HEXASPI_IO_7_Pin
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
#define HEXASPI_DQS1_Pin GPIO_PIN_3
#define HEXASPI_DQS1_GPIO_Port GPIOO
#define HEXASPI_NCS_Pin GPIO_PIN_0
#define HEXASPI_NCS_GPIO_Port GPIOO
#define HEXASPI_IO_9_Pin GPIO_PIN_9
#define HEXASPI_IO_9_GPIO_Port GPIOP
#define HEXASPI_IO_10_Pin GPIO_PIN_10
#define HEXASPI_IO_10_GPIO_Port GPIOP
#define HEXASPI_CLK_Pin GPIO_PIN_4
#define HEXASPI_CLK_GPIO_Port GPIOO
#endif

typedef enum
{
  BSP_STORAGE_OK    = 0,
  BSP_STORAGE_ERROR = 1,
} BSP_Storage_StatusTypeDef;

BSP_Storage_StatusTypeDef BSP_Storage_Open(void);
BSP_Storage_StatusTypeDef BSP_Storage_Close(void);

XSPI_HandleTypeDef *BSP_Storage_GetXSPIHandle(void);
SD_HandleTypeDef *BSP_Storage_GetSDHandle(void);
CACHEAXI_HandleTypeDef *BSP_Storage_GetCacheAXIHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_STORAGE_H */
