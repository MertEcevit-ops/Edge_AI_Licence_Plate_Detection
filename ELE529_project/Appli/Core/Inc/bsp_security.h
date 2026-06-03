#ifndef BSP_SECURITY_H
#define BSP_SECURITY_H

#include "stm32n6xx_hal.h"
#include <stddef.h>

#define BSP_SECURITY_PHY_ADDRESS    0x00U
#define SECURITY_AES256_KEY_SIZE    32U
#define SECURITY_AES_BLOCK_SIZE     16U

typedef enum
{
  BSP_SECURITY_OK    = 0x00U,
  BSP_SECURITY_ERROR = 0x01U
} BSP_Security_StatusTypeDef;

/* Donanım Sürücü Yönetimi */
BSP_Security_StatusTypeDef BSP_Security_Open(void);
BSP_Security_StatusTypeDef BSP_Security_Close(void);

/* Donanımsal RNG (Rastgele Sayı Üretici) */
BSP_Security_StatusTypeDef BSP_Security_Random(uint8_t *buffer, size_t buffer_len);

/* Donanımsal Düz HASH (SHA-256) */
BSP_Security_StatusTypeDef BSP_Security_SHA256(const uint8_t *data, size_t data_len, uint8_t digest[32]);

/* [YENİ] Donanımsal SAES (Secure AES-256-CBC Şifreleme) */
BSP_Security_StatusTypeDef BSP_Security_AES256_CBC_Encrypt(const uint8_t *key, const uint8_t *iv,
                                                           const uint8_t *plaintext, uint32_t length,
                                                           uint8_t *ciphertext);

/* [YENİ] Donanımsal HASH (HMAC-SHA256 Veri Doğrulama) */
BSP_Security_StatusTypeDef BSP_Security_HMAC_SHA256(const uint8_t *key, uint16_t key_len,
                                                    const uint8_t *data, size_t data_len,
                                                    uint8_t digest[32]);

/* Handle Erişim Fonksiyonları */
HASH_HandleTypeDef *BSP_Security_GetHashHandle(void);
RNG_HandleTypeDef  *BSP_Security_GetRngHandle(void);
PKA_HandleTypeDef  *BSP_Security_GetPkaHandle(void);

#endif /* BSP_SECURITY_H */
