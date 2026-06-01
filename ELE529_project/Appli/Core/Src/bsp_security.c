/**
  ******************************************************************************
  * @file    bsp_security.c
  * @brief   Security peripheral driver wrapper: HASH, RNG and PKA.
  ******************************************************************************
  */

#include "bsp_security.h"

static HASH_HandleTypeDef hhash;
static RNG_HandleTypeDef  hrng;
static PKA_HandleTypeDef  hpka;
static uint8_t security_opened = 0U;

static BSP_Security_StatusTypeDef BSP_Security_HASH_Open(void);
static BSP_Security_StatusTypeDef BSP_Security_RNG_Open(void);
static BSP_Security_StatusTypeDef BSP_Security_PKA_Open(void);

BSP_Security_StatusTypeDef BSP_Security_Open(void)
{
  if (security_opened != 0U)
  {
    return BSP_SECURITY_OK;
  }

  if (BSP_Security_RNG_Open() != BSP_SECURITY_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  if (BSP_Security_PKA_Open() != BSP_SECURITY_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  if (BSP_Security_HASH_Open() != BSP_SECURITY_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  security_opened = 1U;
  return BSP_SECURITY_OK;
}

BSP_Security_StatusTypeDef BSP_Security_Close(void)
{
  BSP_Security_StatusTypeDef status = BSP_SECURITY_OK;

  if (security_opened == 0U)
  {
    return BSP_SECURITY_OK;
  }

  if (HAL_HASH_DeInit(&hhash) != HAL_OK)
  {
    status = BSP_SECURITY_ERROR;
  }

  if (HAL_PKA_DeInit(&hpka) != HAL_OK)
  {
    status = BSP_SECURITY_ERROR;
  }

  if (HAL_RNG_DeInit(&hrng) != HAL_OK)
  {
    status = BSP_SECURITY_ERROR;
  }

  security_opened = 0U;
  return status;
}

BSP_Security_StatusTypeDef BSP_Security_SHA256(const uint8_t *data,
                                                size_t data_len,
                                                uint8_t digest[32])
{
  if ((data == NULL) || (digest == NULL))
  {
    return BSP_SECURITY_ERROR;
  }

  if (BSP_Security_Open() != BSP_SECURITY_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  if (HAL_HASH_Start(&hhash, data, (uint32_t)data_len, digest,
                     HAL_MAX_DELAY) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  return BSP_SECURITY_OK;
}

BSP_Security_StatusTypeDef BSP_Security_Random(uint8_t *buffer,
                                                size_t buffer_len)
{
  size_t offset = 0U;

  if ((buffer == NULL) && (buffer_len != 0U))
  {
    return BSP_SECURITY_ERROR;
  }

  if (BSP_Security_Open() != BSP_SECURITY_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  while (offset < buffer_len)
  {
    uint32_t random_word = 0U;
    size_t copy_len = buffer_len - offset;

    if (HAL_RNG_GenerateRandomNumber(&hrng, &random_word) != HAL_OK)
    {
      return BSP_SECURITY_ERROR;
    }

    if (copy_len > sizeof(random_word))
    {
      copy_len = sizeof(random_word);
    }

    for (size_t i = 0U; i < copy_len; i++)
    {
      buffer[offset + i] = (uint8_t)(random_word >> (8U * i));
    }

    offset += copy_len;
  }

  return BSP_SECURITY_OK;
}

HASH_HandleTypeDef *BSP_Security_GetHashHandle(void)
{
  return &hhash;
}

RNG_HandleTypeDef *BSP_Security_GetRngHandle(void)
{
  return &hrng;
}

PKA_HandleTypeDef *BSP_Security_GetPkaHandle(void)
{
  return &hpka;
}

static BSP_Security_StatusTypeDef BSP_Security_HASH_Open(void)
{
  hhash.Instance = HASH;
  hhash.Init.DataType = HASH_NO_SWAP;
  hhash.Init.Algorithm = HASH_ALGOSELECTION_SHA256;

  if (HAL_HASH_Init(&hhash) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  return BSP_SECURITY_OK;
}

static BSP_Security_StatusTypeDef BSP_Security_RNG_Open(void)
{
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;

  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  return BSP_SECURITY_OK;
}

static BSP_Security_StatusTypeDef BSP_Security_PKA_Open(void)
{
  hpka.Instance = PKA;

  if (HAL_PKA_Init(&hpka) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  return BSP_SECURITY_OK;
}
