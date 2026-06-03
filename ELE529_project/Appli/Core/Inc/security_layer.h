#ifndef SECURITY_LAYER_H
#define SECURITY_LAYER_H

#include <stdint.h>
#include <stddef.h>

#define SECURITY_PACKET_MAGIC         0x414C5052U // 'ALPR'
#define SECURITY_PACKET_VERSION       0x01U
#define SECURITY_PACKET_HEADER_SIZE   24U

#define SECURITY_MAX_PLAINTEXT_SIZE   2048U
#define SECURITY_MAX_CIPHERTEXT_SIZE  2064U // Plaintext + padding payı

typedef enum
{
  SECURITY_OK           = 0x00U,
  SECURITY_ERROR        = 0x01U,
  SECURITY_INVALID_PARAM= 0x02U,
  SECURITY_BUFFER_TOO_BIG=0x03U
} Security_StatusTypeDef;

/* Ağ üzerinden Host PC'ye basılacak binary paket formatı */
typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint8_t  version;
  uint8_t  header_size;
  uint16_t ciphertext_len;
  uint32_t frame_id;
  uint32_t timestamp_ms;
  uint32_t plaintext_len;
  uint8_t  iv[16];
  uint8_t  ciphertext[SECURITY_MAX_CIPHERTEXT_SIZE];
  uint8_t  sha256[32]; /* Burası artık HMAC-SHA256 etiketi taşıyacak */
} SecurityPacket_t;

Security_StatusTypeDef SecurityLayer_Init(void);
Security_StatusTypeDef SecurityLayer_Seal(const uint8_t *plaintext, size_t plaintext_len,
                                           uint32_t frame_id, uint32_t timestamp_ms,
                                           SecurityPacket_t *packet);
size_t SecurityLayer_GetPacketWireSize(const SecurityPacket_t *packet);

#endif /* SECURITY_LAYER_H */
