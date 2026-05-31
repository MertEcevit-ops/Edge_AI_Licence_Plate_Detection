/**
  ******************************************************************************
  * @file    security_layer.h
  * @brief   AES-256-CBC + SHA-256 payload sealing for ALPR transport.
  ******************************************************************************
  */

#ifndef SECURITY_LAYER_H
#define SECURITY_LAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define SECURITY_AES256_KEY_SIZE       32U
#define SECURITY_AES_BLOCK_SIZE        16U
#define SECURITY_SHA256_SIZE           32U
#define SECURITY_MAX_PLAINTEXT_SIZE    160U
#define SECURITY_MAX_CIPHERTEXT_SIZE   (SECURITY_MAX_PLAINTEXT_SIZE + SECURITY_AES_BLOCK_SIZE)
#define SECURITY_PACKET_MAGIC          0x52504C41UL
#define SECURITY_PACKET_VERSION        1U

#if defined(__GNUC__)
#define SECURITY_PACKED __attribute__((packed))
#else
#define SECURITY_PACKED
#endif

typedef enum
{
  SECURITY_OK             = 0,
  SECURITY_ERROR          = 1,
  SECURITY_INVALID_PARAM  = 2,
  SECURITY_BUFFER_TOO_BIG = 3,
} Security_StatusTypeDef;

typedef struct SECURITY_PACKED
{
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint32_t frame_id;
  uint32_t timestamp_ms;
  uint32_t plaintext_len;
  uint16_t ciphertext_len;
  uint16_t reserved;
  uint8_t  iv[SECURITY_AES_BLOCK_SIZE];
  uint8_t  sha256[SECURITY_SHA256_SIZE];
  uint8_t  ciphertext[SECURITY_MAX_CIPHERTEXT_SIZE];
} SecurityPacket_t;

#define SECURITY_PACKET_HEADER_SIZE ((uint16_t)offsetof(SecurityPacket_t, ciphertext))

Security_StatusTypeDef SecurityLayer_Init(void);
Security_StatusTypeDef SecurityLayer_Seal(const uint8_t *plaintext,
                                           size_t plaintext_len,
                                           uint32_t frame_id,
                                           uint32_t timestamp_ms,
                                           SecurityPacket_t *packet);
size_t SecurityLayer_GetPacketWireSize(const SecurityPacket_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* SECURITY_LAYER_H */
