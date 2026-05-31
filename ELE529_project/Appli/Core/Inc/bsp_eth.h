/**
  ******************************************************************************
  * @file    bsp_eth.h
  * @brief   BSP Ethernet PHY (LAN8742) driver for STM32N6570-DK
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef BSP_ETH_H
#define BSP_ETH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>

/* Exported defines ----------------------------------------------------------*/

/** @defgroup BSP_ETH_PHY_Defines LAN8742 PHY Register Map
  * @{
  */

/* PHY address on MDIO bus (depends on board strap pins) */
#define BSP_ETH_PHY_ADDRESS         0U

/* LAN8742 PHY Register addresses */
#define PHY_BCR                     0x00U  /* Basic Control Register          */
#define PHY_BSR                     0x01U  /* Basic Status Register           */
#define PHY_ID1                     0x02U  /* PHY Identifier 1                */
#define PHY_ID2                     0x03U  /* PHY Identifier 2                */
#define PHY_AUTONEG_ADV             0x04U  /* Auto-Negotiation Advertisement  */
#define PHY_AUTONEG_LPAR            0x05U  /* Auto-Neg Link Partner Ability   */
#define PHY_SPECIAL_CTRL_STATUS     0x1FU  /* Special Control/Status Register */

/* PHY BCR bit definitions */
#define PHY_BCR_RESET               ((uint16_t)0x8000U)
#define PHY_BCR_LOOPBACK            ((uint16_t)0x4000U)
#define PHY_BCR_SPEED_100           ((uint16_t)0x2000U)
#define PHY_BCR_AUTONEG_EN          ((uint16_t)0x1000U)
#define PHY_BCR_POWER_DOWN          ((uint16_t)0x0800U)
#define PHY_BCR_ISOLATE             ((uint16_t)0x0400U)
#define PHY_BCR_RESTART_AUTONEG     ((uint16_t)0x0200U)
#define PHY_BCR_FULLDUPLEX          ((uint16_t)0x0100U)

/* PHY BSR bit definitions */
#define PHY_BSR_100BASETX_FD        ((uint16_t)0x4000U)
#define PHY_BSR_100BASETX_HD        ((uint16_t)0x2000U)
#define PHY_BSR_10BASET_FD          ((uint16_t)0x1000U)
#define PHY_BSR_10BASET_HD          ((uint16_t)0x0800U)
#define PHY_BSR_AUTONEG_COMPLETE    ((uint16_t)0x0020U)
#define PHY_BSR_REMOTE_FAULT        ((uint16_t)0x0010U)
#define PHY_BSR_AUTONEG_ABILITY     ((uint16_t)0x0008U)
#define PHY_BSR_LINK_STATUS         ((uint16_t)0x0004U)
#define PHY_BSR_JABBER_DETECT       ((uint16_t)0x0002U)
#define PHY_BSR_EXTENDED_CAP        ((uint16_t)0x0001U)

/* PHY Special Control/Status Register (0x1F) bit definitions */
#define PHY_SCSR_SPEED_MASK         ((uint16_t)0x001CU)
#define PHY_SCSR_10BASET_HD         ((uint16_t)0x0004U)
#define PHY_SCSR_10BASET_FD         ((uint16_t)0x0014U)
#define PHY_SCSR_100BASETX_HD       ((uint16_t)0x0008U)
#define PHY_SCSR_100BASETX_FD       ((uint16_t)0x0018U)

/* PHY LAN8742 expected ID */
#define PHY_LAN8742_ID              0x0007C130U  /* ID1[15:0] << 16 | ID2[15:0] */

/* Timeouts */
#define PHY_RESET_TIMEOUT_MS        500U
#define PHY_AUTONEG_TIMEOUT_MS      5000U

/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/

/** @defgroup BSP_ETH_Types ETH Types
  * @{
  */

typedef enum
{
  BSP_ETH_OK       = 0,
  BSP_ETH_ERROR    = 1,
  BSP_ETH_TIMEOUT  = 2,
  BSP_ETH_BUSY     = 3,
} BSP_ETH_StatusTypeDef;

typedef enum
{
  BSP_ETH_LINK_DOWN = 0,
  BSP_ETH_LINK_UP   = 1,
} BSP_ETH_LinkStateTypeDef;

typedef struct
{
  uint32_t Speed;      /* ETH_SPEED_10M or ETH_SPEED_100M */
  uint32_t DuplexMode; /* ETH_FULLDUPLEX_MODE or ETH_HALFDUPLEX_MODE */
} BSP_ETH_LinkInfo_t;

/**
  * @}
  */

/* Exported functions --------------------------------------------------------*/

/** @defgroup BSP_ETH_Functions ETH Functions
  * @{
  */

/**
  * @brief  Initialize the Ethernet PHY (LAN8742).
  *         Performs soft reset + auto-negotiation.
  * @retval BSP_ETH_OK on success
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_Init(void);
BSP_ETH_StatusTypeDef BSP_ETH_Open(void);

/**
  * @brief  Get current link state.
  * @param  pLinkInfo  If link is up, filled with speed/duplex info
  * @retval BSP_ETH_LINK_UP or BSP_ETH_LINK_DOWN
  */
BSP_ETH_LinkStateTypeDef BSP_ETH_GetLinkState(BSP_ETH_LinkInfo_t *pLinkInfo);

/**
  * @brief  Read a PHY register via MDIO.
  * @param  RegAddr  Register address (0-31)
  * @param  pRegVal  Output: register value
  * @retval BSP_ETH_OK on success
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_ReadReg(uint32_t RegAddr, uint32_t *pRegVal);

/**
  * @brief  Write a PHY register via MDIO.
  * @param  RegAddr  Register address (0-31)
  * @param  RegVal   Value to write
  * @retval BSP_ETH_OK on success
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_WriteReg(uint32_t RegAddr, uint32_t RegVal);

/**
  * @brief  Software reset the PHY.
  * @retval BSP_ETH_OK on success
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_Reset(void);

/**
  * @brief  Start auto-negotiation and wait for completion.
  * @retval BSP_ETH_OK on success, BSP_ETH_TIMEOUT if auto-neg didn't complete
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_AutoNegotiate(void);

/**
  * @brief  Enable/disable PHY loopback mode (for testing).
  * @param  enable  1 = enable, 0 = disable
  * @retval BSP_ETH_OK on success
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_SetLoopback(uint8_t enable);
ETH_HandleTypeDef *BSP_ETH_GetHandle(void);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* BSP_ETH_H */
