#include "security_layer.h"
#include "bsp_security.h"
#include <string.h>

/* Donanıma beslenecek statik 256-bit anahtarımız */
static const uint8_t alpr_aes256_key[SECURITY_AES256_KEY_SIZE] = {
  0x41U, 0x4CU, 0x50U, 0x52U, 0x2DU, 0x45U, 0x4CU, 0x45U,
  0x35U, 0x32U, 0x39U, 0x2DU, 0x53U, 0x45U, 0x43U, 0x55U,
  0x52U, 0x45U, 0x2DU, 0x41U, 0x45U, 0x53U, 0x32U, 0x35U,
  0x36U, 0x2DU, 0x4BU, 0x45U, 0x59U, 0x21U, 0x30U, 0x31U
};

Security_StatusTypeDef SecurityLayer_Init(void)
{
  return (BSP_Security_Open() == BSP_SECURITY_OK) ? SECURITY_OK : SECURITY_ERROR;
}

Security_StatusTypeDef SecurityLayer_Seal(const uint8_t *plaintext,
                                           size_t plaintext_len,
                                           uint32_t frame_id,
                                           uint32_t timestamp_ms,
                                           SecurityPacket_t *packet)
{
  if ((plaintext == NULL) || (packet == NULL)) return SECURITY_INVALID_PARAM;
  if (plaintext_len > SECURITY_MAX_PLAINTEXT_SIZE) return SECURITY_BUFFER_TOO_BIG;
  if (SecurityLayer_Init() != SECURITY_OK) return SECURITY_ERROR;

  /* Paketi sıfırla ve üst bilgileri yaz */
  memset(packet, 0, sizeof(*packet));
  packet->magic = SECURITY_PACKET_MAGIC;
  packet->version = SECURITY_PACKET_VERSION;
  packet->header_size = SECURITY_PACKET_HEADER_SIZE;
  packet->frame_id = frame_id;
  packet->timestamp_ms = timestamp_ms;
  packet->plaintext_len = (uint32_t)plaintext_len;

  /* 1. PKCS7 Padding Hizalaması (Blok şifreleme için veriyi 16'nın katı yapmak) */
  uint8_t padded_buffer[SECURITY_MAX_CIPHERTEXT_SIZE] = {0};
  size_t padded_len = ((plaintext_len / SECURITY_AES_BLOCK_SIZE) + 1U) * SECURITY_AES_BLOCK_SIZE;
  uint8_t pad_val = (uint8_t)(padded_len - plaintext_len);

  memcpy(padded_buffer, plaintext, plaintext_len);
  memset(padded_buffer + plaintext_len, pad_val, pad_val);

  /* 2. Donanımsal RNG'den güvenli 16-byte IV üret */
  if (BSP_Security_Random(packet->iv, sizeof(packet->iv)) != BSP_SECURITY_OK)
  {
    return SECURITY_ERROR;
  }

  /* 3. Donanımsal SAES Motorunu tetikle */
  if (BSP_Security_AES256_CBC_Encrypt(alpr_aes256_key, packet->iv, padded_buffer, (uint32_t)padded_len, packet->ciphertext) != BSP_SECURITY_OK)
  {
    return SECURITY_ERROR;
  }
  packet->ciphertext_len = (uint16_t)padded_len;

  /* 4. Endüstri Standardı: Encrypt-then-MAC */
  /* Hacker'ların paketi manipüle etmesini engellemek için şifreli verinin donanımsal HMAC-SHA256'sını alıyoruz */
  if (BSP_Security_HMAC_SHA256(alpr_aes256_key, sizeof(alpr_aes256_key), packet->ciphertext, packet->ciphertext_len, packet->sha256) != BSP_SECURITY_OK)
  {
    return SECURITY_ERROR;
  }

  return SECURITY_OK;
}

size_t SecurityLayer_GetPacketWireSize(const SecurityPacket_t *packet)
{
  if ((packet == NULL) || (packet->magic != SECURITY_PACKET_MAGIC)) return 0U;
  return (size_t)packet->header_size + packet->ciphertext_len;
}
