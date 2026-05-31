/**
  ******************************************************************************
  * @file    security_layer.c
  * @brief   AES-256-CBC + SHA-256 payload sealing for ALPR transport.
  ******************************************************************************
  */

#include "security_layer.h"
#include "bsp_security.h"
#include <string.h>

#define AES_NB 4U
#define AES_NK 8U
#define AES_NR 14U
#define AES_ROUND_KEY_SIZE 240U

static const uint8_t alpr_aes256_key[SECURITY_AES256_KEY_SIZE] = {
  0x41U, 0x4CU, 0x50U, 0x52U, 0x2DU, 0x45U, 0x4CU, 0x45U,
  0x35U, 0x32U, 0x39U, 0x2DU, 0x53U, 0x45U, 0x43U, 0x55U,
  0x52U, 0x45U, 0x2DU, 0x41U, 0x45U, 0x53U, 0x32U, 0x35U,
  0x36U, 0x2DU, 0x4BU, 0x45U, 0x59U, 0x21U, 0x30U, 0x31U
};

static const uint8_t sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[15] = {
  0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36,0x6c,0xd8,0xab,0x4d
};

static void AES256_KeyExpansion(const uint8_t *key, uint8_t *round_key);
static void AES256_EncryptBlock(const uint8_t *input, uint8_t *output,
                                const uint8_t *round_key);
static void AddRoundKey(uint8_t *state, const uint8_t *round_key,
                        uint8_t round);
static void SubBytes(uint8_t *state);
static void ShiftRows(uint8_t *state);
static void MixColumns(uint8_t *state);
static uint8_t xtime(uint8_t value);
static Security_StatusTypeDef AES256_CBC_Encrypt(const uint8_t *plaintext,
                                                  size_t plaintext_len,
                                                  const uint8_t iv[16],
                                                  uint8_t *ciphertext,
                                                  uint16_t *ciphertext_len);

Security_StatusTypeDef SecurityLayer_Init(void)
{
  return (BSP_Security_Open() == BSP_SECURITY_OK) ? SECURITY_OK :
                                                    SECURITY_ERROR;
}

Security_StatusTypeDef SecurityLayer_Seal(const uint8_t *plaintext,
                                           size_t plaintext_len,
                                           uint32_t frame_id,
                                           uint32_t timestamp_ms,
                                           SecurityPacket_t *packet)
{
  if ((plaintext == NULL) || (packet == NULL))
  {
    return SECURITY_INVALID_PARAM;
  }

  if (plaintext_len > SECURITY_MAX_PLAINTEXT_SIZE)
  {
    return SECURITY_BUFFER_TOO_BIG;
  }

  if (SecurityLayer_Init() != SECURITY_OK)
  {
    return SECURITY_ERROR;
  }

  memset(packet, 0, sizeof(*packet));
  packet->magic = SECURITY_PACKET_MAGIC;
  packet->version = SECURITY_PACKET_VERSION;
  packet->header_size = SECURITY_PACKET_HEADER_SIZE;
  packet->frame_id = frame_id;
  packet->timestamp_ms = timestamp_ms;
  packet->plaintext_len = (uint32_t)plaintext_len;

  if (BSP_Security_SHA256(plaintext, plaintext_len,
                          packet->sha256) != BSP_SECURITY_OK)
  {
    return SECURITY_ERROR;
  }

  if (BSP_Security_Random(packet->iv,
                          sizeof(packet->iv)) != BSP_SECURITY_OK)
  {
    return SECURITY_ERROR;
  }

  return AES256_CBC_Encrypt(plaintext, plaintext_len, packet->iv,
                            packet->ciphertext, &packet->ciphertext_len);
}

size_t SecurityLayer_GetPacketWireSize(const SecurityPacket_t *packet)
{
  if ((packet == NULL) || (packet->magic != SECURITY_PACKET_MAGIC))
  {
    return 0U;
  }

  return (size_t)packet->header_size + packet->ciphertext_len;
}

static Security_StatusTypeDef AES256_CBC_Encrypt(const uint8_t *plaintext,
                                                  size_t plaintext_len,
                                                  const uint8_t iv[16],
                                                  uint8_t *ciphertext,
                                                  uint16_t *ciphertext_len)
{
  uint8_t round_key[AES_ROUND_KEY_SIZE];
  uint8_t previous[SECURITY_AES_BLOCK_SIZE];
  uint8_t block[SECURITY_AES_BLOCK_SIZE];
  size_t padded_len;
  size_t offset;
  uint8_t pad_len;

  padded_len = ((plaintext_len / SECURITY_AES_BLOCK_SIZE) + 1U) *
               SECURITY_AES_BLOCK_SIZE;
  if (padded_len > SECURITY_MAX_CIPHERTEXT_SIZE)
  {
    return SECURITY_BUFFER_TOO_BIG;
  }

  pad_len = (uint8_t)(padded_len - plaintext_len);
  AES256_KeyExpansion(alpr_aes256_key, round_key);
  memcpy(previous, iv, sizeof(previous));

  for (offset = 0U; offset < padded_len; offset += SECURITY_AES_BLOCK_SIZE)
  {
    for (uint32_t i = 0U; i < SECURITY_AES_BLOCK_SIZE; i++)
    {
      size_t input_index = offset + i;
      block[i] = (input_index < plaintext_len) ? plaintext[input_index] :
                                            pad_len;
      block[i] ^= previous[i];
    }

    AES256_EncryptBlock(block, &ciphertext[offset], round_key);
    memcpy(previous, &ciphertext[offset], SECURITY_AES_BLOCK_SIZE);
  }

  *ciphertext_len = (uint16_t)padded_len;
  return SECURITY_OK;
}

static void AES256_KeyExpansion(const uint8_t *key, uint8_t *round_key)
{
  uint8_t temp[4];
  uint32_t i;

  memcpy(round_key, key, SECURITY_AES256_KEY_SIZE);

  for (i = AES_NK; i < AES_NB * (AES_NR + 1U); i++)
  {
    temp[0] = round_key[(i - 1U) * 4U + 0U];
    temp[1] = round_key[(i - 1U) * 4U + 1U];
    temp[2] = round_key[(i - 1U) * 4U + 2U];
    temp[3] = round_key[(i - 1U) * 4U + 3U];

    if ((i % AES_NK) == 0U)
    {
      uint8_t t = temp[0];
      temp[0] = sbox[temp[1]] ^ rcon[i / AES_NK];
      temp[1] = sbox[temp[2]];
      temp[2] = sbox[temp[3]];
      temp[3] = sbox[t];
    }
    else if ((i % AES_NK) == 4U)
    {
      temp[0] = sbox[temp[0]];
      temp[1] = sbox[temp[1]];
      temp[2] = sbox[temp[2]];
      temp[3] = sbox[temp[3]];
    }

    round_key[i * 4U + 0U] = round_key[(i - AES_NK) * 4U + 0U] ^ temp[0];
    round_key[i * 4U + 1U] = round_key[(i - AES_NK) * 4U + 1U] ^ temp[1];
    round_key[i * 4U + 2U] = round_key[(i - AES_NK) * 4U + 2U] ^ temp[2];
    round_key[i * 4U + 3U] = round_key[(i - AES_NK) * 4U + 3U] ^ temp[3];
  }
}

static void AES256_EncryptBlock(const uint8_t *input, uint8_t *output,
                                const uint8_t *round_key)
{
  uint8_t state[SECURITY_AES_BLOCK_SIZE];

  memcpy(state, input, SECURITY_AES_BLOCK_SIZE);
  AddRoundKey(state, round_key, 0U);

  for (uint8_t round = 1U; round < AES_NR; round++)
  {
    SubBytes(state);
    ShiftRows(state);
    MixColumns(state);
    AddRoundKey(state, round_key, round);
  }

  SubBytes(state);
  ShiftRows(state);
  AddRoundKey(state, round_key, AES_NR);
  memcpy(output, state, SECURITY_AES_BLOCK_SIZE);
}

static void AddRoundKey(uint8_t *state, const uint8_t *round_key,
                        uint8_t round)
{
  const uint8_t *key = &round_key[(uint32_t)round * SECURITY_AES_BLOCK_SIZE];

  for (uint32_t i = 0U; i < SECURITY_AES_BLOCK_SIZE; i++)
  {
    state[i] ^= key[i];
  }
}

static void SubBytes(uint8_t *state)
{
  for (uint32_t i = 0U; i < SECURITY_AES_BLOCK_SIZE; i++)
  {
    state[i] = sbox[state[i]];
  }
}

static void ShiftRows(uint8_t *state)
{
  uint8_t tmp;

  tmp = state[1];
  state[1] = state[5];
  state[5] = state[9];
  state[9] = state[13];
  state[13] = tmp;

  tmp = state[2];
  state[2] = state[10];
  state[10] = tmp;
  tmp = state[6];
  state[6] = state[14];
  state[14] = tmp;

  tmp = state[15];
  state[15] = state[11];
  state[11] = state[7];
  state[7] = state[3];
  state[3] = tmp;
}

static void MixColumns(uint8_t *state)
{
  for (uint32_t col = 0U; col < 4U; col++)
  {
    uint8_t *c = &state[col * 4U];
    uint8_t a0 = c[0];
    uint8_t a1 = c[1];
    uint8_t a2 = c[2];
    uint8_t a3 = c[3];
    uint8_t t = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
    uint8_t u = a0;

    c[0] ^= t ^ xtime((uint8_t)(a0 ^ a1));
    c[1] ^= t ^ xtime((uint8_t)(a1 ^ a2));
    c[2] ^= t ^ xtime((uint8_t)(a2 ^ a3));
    c[3] ^= t ^ xtime((uint8_t)(a3 ^ u));
  }
}

static uint8_t xtime(uint8_t value)
{
  return (uint8_t)((value << 1U) ^ (((value >> 7U) & 1U) * 0x1bU));
}
