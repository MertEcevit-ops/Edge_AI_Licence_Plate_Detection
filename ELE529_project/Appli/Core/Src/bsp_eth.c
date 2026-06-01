/**
  ******************************************************************************
  * @file    bsp_eth.c
  * @brief   BSP Ethernet PHY (LAN8742) driver for STM32N6570-DK
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bsp_eth.h"
#include <string.h>

static ETH_HandleTypeDef heth1;
static uint8_t eth_opened = 0U;

#if defined ( __ICCARM__ )
#pragma location=0x341F8000
static ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT];
#pragma location=0x341F80C0
static ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT];

#elif defined ( __CC_ARM )

static __attribute__((at(0x341F8000))) ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT];
static __attribute__((at(0x341F80C0))) ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT];

#elif defined ( __GNUC__ )

static ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT]
    __attribute__((section(".RxDecripSection")));
static ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT]
    __attribute__((section(".TxDecripSection")));
#endif

/* Exported functions --------------------------------------------------------*/

BSP_ETH_StatusTypeDef BSP_ETH_Open(void)
{
  static uint8_t MACAddr[6];

  if (eth_opened != 0U)
  {
    return BSP_ETH_OK;
  }

  heth1.Instance = ETH1;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth1.Init.MACAddr = &MACAddr[0];
  heth1.Init.MediaInterface = HAL_ETH_RMII_MODE;

  for (uint32_t ch = 0; ch < ETH_DMA_CH_CNT; ch++)
  {
    heth1.Init.TxDesc[ch] = DMATxDscrTab[ch];
    heth1.Init.RxDesc[ch] = DMARxDscrTab[ch];
  }

  heth1.Init.RxBuffLen = 1524;

  if (HAL_ETH_Init(&heth1) != HAL_OK)
  {
    return BSP_ETH_ERROR;
  }

  eth_opened = 1U;
  return BSP_ETH_OK;
}

BSP_ETH_StatusTypeDef BSP_ETH_Close(void)
{
  if (eth_opened == 0U)
  {
    return BSP_ETH_OK;
  }

  if (HAL_ETH_DeInit(&heth1) != HAL_OK)
  {
    return BSP_ETH_ERROR;
  }

  eth_opened = 0U;
  return BSP_ETH_OK;
}

/**
  * @brief  Initialize the Ethernet PHY (soft reset + auto-negotiation).
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_Init(void)
{
  BSP_ETH_StatusTypeDef status;

  status = BSP_ETH_Open();
  if (status != BSP_ETH_OK)
  {
    return status;
  }

  /* Soft reset the PHY */
  status = BSP_ETH_PHY_Reset();
  if (status != BSP_ETH_OK)
  {
    return status;
  }

  /* Start auto-negotiation */
  status = BSP_ETH_PHY_AutoNegotiate();
  return status;
}

/**
  * @brief  Get current link state and negotiated parameters.
  */
BSP_ETH_LinkStateTypeDef BSP_ETH_GetLinkState(BSP_ETH_LinkInfo_t *pLinkInfo)
{
  uint32_t bsr_val  = 0;
  uint32_t scsr_val = 0;

  /* Read Basic Status Register */
  if (BSP_ETH_PHY_ReadReg(PHY_BSR, &bsr_val) != BSP_ETH_OK)
  {
    return BSP_ETH_LINK_DOWN;
  }

  /* Check link status bit */
  if ((bsr_val & PHY_BSR_LINK_STATUS) == 0U)
  {
    return BSP_ETH_LINK_DOWN;
  }

  /* Link is up — read speed/duplex from Special Control/Status Register */
  if (pLinkInfo != NULL)
  {
    if (BSP_ETH_PHY_ReadReg(PHY_SPECIAL_CTRL_STATUS, &scsr_val) != BSP_ETH_OK)
    {
      pLinkInfo->Speed      = ETH_SPEED_100M;
      pLinkInfo->DuplexMode = ETH_FULLDUPLEX_MODE;
      return BSP_ETH_LINK_UP;
    }

    switch (scsr_val & PHY_SCSR_SPEED_MASK)
    {
      case PHY_SCSR_100BASETX_FD:
        pLinkInfo->Speed      = ETH_SPEED_100M;
        pLinkInfo->DuplexMode = ETH_FULLDUPLEX_MODE;
        break;

      case PHY_SCSR_100BASETX_HD:
        pLinkInfo->Speed      = ETH_SPEED_100M;
        pLinkInfo->DuplexMode = ETH_HALFDUPLEX_MODE;
        break;

      case PHY_SCSR_10BASET_FD:
        pLinkInfo->Speed      = ETH_SPEED_10M;
        pLinkInfo->DuplexMode = ETH_FULLDUPLEX_MODE;
        break;

      case PHY_SCSR_10BASET_HD:
      default:
        pLinkInfo->Speed      = ETH_SPEED_10M;
        pLinkInfo->DuplexMode = ETH_HALFDUPLEX_MODE;
        break;
    }
  }

  return BSP_ETH_LINK_UP;
}

/**
  * @brief  Read a PHY register via MDIO.
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_ReadReg(uint32_t RegAddr, uint32_t *pRegVal)
{
  if (HAL_ETH_ReadPHYRegister(&heth1, BSP_ETH_PHY_ADDRESS,
                               RegAddr, pRegVal) != HAL_OK)
  {
    return BSP_ETH_ERROR;
  }
  return BSP_ETH_OK;
}

/**
  * @brief  Write a PHY register via MDIO.
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_WriteReg(uint32_t RegAddr, uint32_t RegVal)
{
  if (HAL_ETH_WritePHYRegister(&heth1, BSP_ETH_PHY_ADDRESS,
                                RegAddr, RegVal) != HAL_OK)
  {
    return BSP_ETH_ERROR;
  }
  return BSP_ETH_OK;
}

/**
  * @brief  Software reset the PHY.
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_Reset(void)
{
  uint32_t reg_val;
  uint32_t tick_start;

  /* Set the soft reset bit */
  if (BSP_ETH_PHY_WriteReg(PHY_BCR, PHY_BCR_RESET) != BSP_ETH_OK)
  {
    return BSP_ETH_ERROR;
  }

  /* Wait for the reset bit to auto-clear */
  tick_start = HAL_GetTick();

  do
  {
    if (BSP_ETH_PHY_ReadReg(PHY_BCR, &reg_val) != BSP_ETH_OK)
    {
      return BSP_ETH_ERROR;
    }

    if ((HAL_GetTick() - tick_start) > PHY_RESET_TIMEOUT_MS)
    {
      return BSP_ETH_TIMEOUT;
    }

  } while ((reg_val & PHY_BCR_RESET) != 0U);

  /* Post-reset delay */
  HAL_Delay(10);

  return BSP_ETH_OK;
}

/**
  * @brief  Start auto-negotiation and wait for completion.
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_AutoNegotiate(void)
{
  uint32_t reg_val;
  uint32_t tick_start;

  /* Enable auto-negotiation and restart */
  if (BSP_ETH_PHY_WriteReg(PHY_BCR,
                             PHY_BCR_AUTONEG_EN | PHY_BCR_RESTART_AUTONEG) != BSP_ETH_OK)
  {
    return BSP_ETH_ERROR;
  }

  /* Wait for auto-negotiation complete */
  tick_start = HAL_GetTick();

  do
  {
    if (BSP_ETH_PHY_ReadReg(PHY_BSR, &reg_val) != BSP_ETH_OK)
    {
      return BSP_ETH_ERROR;
    }

    if ((HAL_GetTick() - tick_start) > PHY_AUTONEG_TIMEOUT_MS)
    {
      return BSP_ETH_TIMEOUT;
    }

  } while ((reg_val & PHY_BSR_AUTONEG_COMPLETE) == 0U);

  return BSP_ETH_OK;
}

/**
  * @brief  Enable/disable PHY loopback mode.
  */
BSP_ETH_StatusTypeDef BSP_ETH_PHY_SetLoopback(uint8_t enable)
{
  uint32_t reg_val;

  if (BSP_ETH_PHY_ReadReg(PHY_BCR, &reg_val) != BSP_ETH_OK)
  {
    return BSP_ETH_ERROR;
  }

  if (enable)
  {
    reg_val |= PHY_BCR_LOOPBACK;
  }
  else
  {
    reg_val &= ~PHY_BCR_LOOPBACK;
  }

  return BSP_ETH_PHY_WriteReg(PHY_BCR, reg_val);
}

ETH_HandleTypeDef *BSP_ETH_GetHandle(void)
{
  return &heth1;
}
