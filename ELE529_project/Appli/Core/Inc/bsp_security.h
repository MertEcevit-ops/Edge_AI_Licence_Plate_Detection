/**
  ******************************************************************************
  * @file    bsp_security.h
  * @brief   Security peripheral driver wrapper: HASH, RNG and PKA.
  ******************************************************************************
  */

#ifndef BSP_SECURITY_H
#define BSP_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stddef.h>

typedef enum
{
  BSP_SECURITY_OK    = 0,
  BSP_SECURITY_ERROR = 1,
} BSP_Security_StatusTypeDef;

BSP_Security_StatusTypeDef BSP_Security_Open(void);
BSP_Security_StatusTypeDef BSP_Security_SHA256(const uint8_t *data,
                                                size_t data_len,
                                                uint8_t digest[32]);
BSP_Security_StatusTypeDef BSP_Security_Random(uint8_t *buffer,
                                                size_t buffer_len);

HASH_HandleTypeDef *BSP_Security_GetHashHandle(void);
RNG_HandleTypeDef *BSP_Security_GetRngHandle(void);
PKA_HandleTypeDef *BSP_Security_GetPkaHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SECURITY_H */
