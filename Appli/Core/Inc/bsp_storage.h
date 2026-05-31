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

typedef enum
{
  BSP_STORAGE_OK    = 0,
  BSP_STORAGE_ERROR = 1,
} BSP_Storage_StatusTypeDef;

BSP_Storage_StatusTypeDef BSP_Storage_Open(void);

XSPI_HandleTypeDef *BSP_Storage_GetXSPIHandle(void);
SD_HandleTypeDef *BSP_Storage_GetSDHandle(void);
CACHEAXI_HandleTypeDef *BSP_Storage_GetCacheAXIHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_STORAGE_H */
