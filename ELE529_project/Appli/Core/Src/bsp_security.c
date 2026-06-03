#include "bsp_security.h"

static HASH_HandleTypeDef hhash;
static RNG_HandleTypeDef  hrng;
static PKA_HandleTypeDef  hpka;
static CRYP_HandleTypeDef hcryp; /* Donanımsal Kripto (AES/SAES) Yapısı */
static uint8_t security_opened = 0U;

static BSP_Security_StatusTypeDef BSP_Security_HASH_Open(void);
static BSP_Security_StatusTypeDef BSP_Security_RNG_Open(void);
static BSP_Security_StatusTypeDef BSP_Security_PKA_Open(void);

BSP_Security_StatusTypeDef BSP_Security_Open(void)
{
  if (security_opened != 0U) return BSP_SECURITY_OK;

  if (BSP_Security_RNG_Open() != BSP_SECURITY_OK)  return BSP_SECURITY_ERROR;
  if (BSP_Security_PKA_Open() != BSP_SECURITY_OK)  return BSP_SECURITY_ERROR;
  if (BSP_Security_HASH_Open() != BSP_SECURITY_OK) return BSP_SECURITY_ERROR;

  security_opened = 1U;
  return BSP_SECURITY_OK;
}

BSP_Security_StatusTypeDef BSP_Security_Close(void)
{
  BSP_Security_StatusTypeDef status = BSP_SECURITY_OK;
  if (security_opened == 0U) return BSP_SECURITY_OK;

  if (HAL_HASH_DeInit(&hhash) != HAL_OK) status = BSP_SECURITY_ERROR;
  if (HAL_PKA_DeInit(&hpka)   != HAL_OK) status = BSP_SECURITY_ERROR;
  if (HAL_RNG_DeInit(&hrng)   != HAL_OK) status = BSP_SECURITY_ERROR;

  security_opened = 0U;
  return status;
}

BSP_Security_StatusTypeDef BSP_Security_Random(uint8_t *buffer, size_t buffer_len)
{
  size_t offset = 0U;
  if ((buffer == NULL) && (buffer_len != 0U)) return BSP_SECURITY_ERROR;
  if (BSP_Security_Open() != BSP_SECURITY_OK) return BSP_SECURITY_ERROR;

  while (offset < buffer_len)
  {
    uint32_t random_word = 0U;
    size_t copy_len = buffer_len - offset;

    if (HAL_RNG_GenerateRandomNumber(&hrng, &random_word) != HAL_OK) return BSP_SECURITY_ERROR;

    if (copy_len > sizeof(random_word)) copy_len = sizeof(random_word);

    for (size_t i = 0U; i < copy_len; i++)
    {
      buffer[offset + i] = (uint8_t)(random_word >> (8U * i));
    }
    offset += copy_len;
  }
  return BSP_SECURITY_OK;
}

BSP_Security_StatusTypeDef BSP_Security_SHA256(const uint8_t *data, size_t data_len, uint8_t digest[32])
{
  if ((data == NULL) || (digest == NULL)) return BSP_SECURITY_ERROR;
  if (BSP_Security_Open() != BSP_SECURITY_OK) return BSP_SECURITY_ERROR;

  if (HAL_HASH_Start(&hhash, data, (uint32_t)data_len, digest, HAL_MAX_DELAY) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }
  return BSP_SECURITY_OK;
}

/* Donanımsal Secure AES Şifreleme Motoru */
BSP_Security_StatusTypeDef BSP_Security_AES256_CBC_Encrypt(const uint8_t *key, const uint8_t *iv,
                                                           const uint8_t *plaintext, uint32_t length,
                                                           uint8_t *ciphertext)
{
  hcryp.Instance = SAES; /* STM32N6 Secure AES Donanımı */
  hcryp.Init.DataType      = CRYP_DATATYPE_8B;
  hcryp.Init.KeySize       = CRYP_KEYSIZE_256B;
  hcryp.Init.pKey          = (uint32_t *)key;
  hcryp.Init.pInitVect     = (uint32_t *)iv;
  hcryp.Init.Algorithm     = CRYP_AES_CBC;
  hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;

  if (HAL_CRYP_Init(&hcryp) != HAL_OK) return BSP_SECURITY_ERROR;

  if (HAL_CRYP_Encrypt(&hcryp, (uint32_t *)plaintext, length, (uint32_t *)ciphertext, HAL_MAX_DELAY) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  HAL_CRYP_DeInit(&hcryp);
  return BSP_SECURITY_OK;
}

/* Donanımsal HMAC-SHA256 Doğrulama Motoru */
BSP_Security_StatusTypeDef BSP_Security_HMAC_SHA256(const uint8_t *key, uint16_t key_len,
                                                    const uint8_t *data, size_t data_len,
                                                    uint8_t digest[32])
{
  HASH_HandleTypeDef hhash_hmac = {0};

  hhash_hmac.Instance = HASH;
  hhash_hmac.Init.DataType  = HASH_DATATYPE_8B;
  hhash_hmac.Init.Algorithm = HASH_ALGOSELECTION_SHA256;
  hhash_hmac.Init.Mode      = HASH_ALGOMODE_HMAC;
  hhash_hmac.Init.pKey      = (uint8_t *)key;
  hhash_hmac.Init.KeySize   = key_len;

  if (HAL_HASH_Init(&hhash_hmac) != HAL_OK) return BSP_SECURITY_ERROR;

  if (HAL_HASH_Start(&hhash_hmac, (uint8_t *)data, data_len, digest, HAL_MAX_DELAY) != HAL_OK)
  {
    return BSP_SECURITY_ERROR;
  }

  HAL_HASH_DeInit(&hhash_hmac);
  return BSP_SECURITY_OK;
}

HASH_HandleTypeDef *BSP_Security_GetHashHandle(void) { return &hhash; }
RNG_HandleTypeDef  *BSP_Security_GetRngHandle(void)  { return &hrng; }
PKA_HandleTypeDef  *BSP_Security_GetPkaHandle(void)  { return &hpka; }

static BSP_Security_StatusTypeDef BSP_Security_HASH_Open(void)
{
  hhash.Instance = HASH;
  hhash.Init.DataType = HASH_NO_SWAP;
  hhash.Init.Algorithm = HASH_ALGOSELECTION_SHA256;
  return (HAL_HASH_Init(&hhash) == HAL_OK) ? BSP_SECURITY_OK : BSP_SECURITY_ERROR;
}

static BSP_Security_StatusTypeDef BSP_Security_RNG_Open(void)
{
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  return (HAL_RNG_Init(&hrng) == HAL_OK) ? BSP_SECURITY_OK : BSP_SECURITY_ERROR;
}

static BSP_Security_StatusTypeDef BSP_Security_PKA_Open(void)
{
  hpka.Instance = PKA;
  return (HAL_PKA_Init(&hpka) == HAL_OK) ? BSP_SECURITY_OK : BSP_SECURITY_ERROR;
}
