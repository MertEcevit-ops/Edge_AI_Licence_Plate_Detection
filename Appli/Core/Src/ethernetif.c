/**
  ******************************************************************************
  * @file    ethernetif.c
  * @brief   LwIP network interface (ethernetif) for STM32N6 + FreeRTOS
  *          Bridges HAL_ETH driver to the LwIP TCP/IP stack.
  *
  *  Adapted for the STM32N6xx HAL ETH driver which uses:
  *    - RxDescList[channel] (array, not single struct)
  *    - ETH_TxPacketConfigTypeDef (not ETH_TxPacketConfig)
  *    - HAL_ETH_ReadData() for RX
  *    - HAL_ETH_Transmit_IT() for TX
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "ethernetif.h"
#include "bsp_eth.h"
#include "main.h"

#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/timeouts.h"
#include "lwip/ethip6.h"
#include "netif/etharp.h"
#include "lwip/tcpip.h"

#include "cmsis_os2.h"

#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define IFNAME0                 's'
#define IFNAME1                 't'
#define ETH_RX_BUFFER_SIZE      1536U
#define ETH_RX_BUFFER_CNT       12U
#define ETH_DMA_CHANNEL         0U   /* DMA channel index for RX */

/* Private variables ---------------------------------------------------------*/

/* External ETH handle from CubeMX main.c */
extern ETH_HandleTypeDef heth1;

/* Semaphore to signal RX task from ISR */
static osSemaphoreId_t ethRxSemaphore = NULL;

/* RX buffer pool — noncacheable for DMA */
static uint8_t __attribute__((section("noncacheable_buffer"), aligned(32)))
    eth_rx_buffer[ETH_RX_BUFFER_CNT][ETH_RX_BUFFER_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void low_level_init(struct netif *netif);
static err_t low_level_output(struct netif *netif, struct pbuf *p);
static struct pbuf *low_level_input(struct netif *netif);

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  LwIP netif init callback.
  */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  netif->hostname = "stm32n6";
#endif

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;

  netif->output     = etharp_output;
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif
  netif->linkoutput  = low_level_output;

  /* Set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* Set MTU */
  netif->mtu = 1500;

  /* Accept broadcast, ARP, and link traffic */
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

  /* Create the RX semaphore */
  ethRxSemaphore = osSemaphoreNew(1, 0, NULL);

  /* Low-level initialization */
  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Read received frames and pass them to LwIP.
  */
void ethernetif_input(struct netif *netif)
{
  struct pbuf *p;

  do
  {
    p = low_level_input(netif);
    if (p != NULL)
    {
      if (netif->input(p, netif) != ERR_OK)
      {
        pbuf_free(p);
      }
    }
  } while (p != NULL);
}

/**
  * @brief  Check link status and update netif.
  */
void ethernetif_check_link(struct netif *netif)
{
  BSP_ETH_LinkInfo_t link_info;
  BSP_ETH_LinkStateTypeDef link_state;
  ETH_MACConfigTypeDef mac_config;

  link_state = BSP_ETH_GetLinkState(&link_info);

  if (link_state == BSP_ETH_LINK_UP)
  {
    if (!netif_is_link_up(netif))
    {
      /* Link just came up — update MAC config with negotiated speed/duplex */
      HAL_ETH_GetMACConfig(&heth1, &mac_config);
      mac_config.Speed      = link_info.Speed;
      mac_config.DuplexMode = link_info.DuplexMode;
      HAL_ETH_SetMACConfig(&heth1, &mac_config);

      /* Start ETH */
      HAL_ETH_Start_IT(&heth1);

      netif_set_link_up(netif);
      netif_set_up(netif);
    }
  }
  else
  {
    if (netif_is_link_up(netif))
    {
      /* Link went down */
      HAL_ETH_Stop_IT(&heth1);
      netif_set_link_down(netif);
      netif_set_down(netif);
    }
  }
}

/**
  * @brief  Notify RX task that a frame is available (called from ISR).
  */
void ethernetif_rx_notify(void)
{
  if (ethRxSemaphore != NULL)
  {
    osSemaphoreRelease(ethRxSemaphore);
  }
}

/**
  * @brief  Wait for a frame to be received (blocking).
  *         Used by the Ethernet RX task.
  * @param  timeout_ms  Maximum wait time in milliseconds
  * @retval osOK if frame received, osErrorTimeout on timeout
  */
osStatus_t ethernetif_wait_rx(uint32_t timeout_ms)
{
  if (ethRxSemaphore != NULL)
  {
    return osSemaphoreAcquire(ethRxSemaphore, timeout_ms);
  }
  return osError;
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Low-level ETH initialization.
  */
static void low_level_init(struct netif *netif)
{
  uint32_t idx;

  /* Copy MAC address from ETH handle */
  for (idx = 0; idx < ETH_HWADDR_LEN; idx++)
  {
    netif->hwaddr[idx] = heth1.Init.MACAddr[idx];
  }

  /*
   * STM32N6 ETH HAL does not have HAL_ETH_DescAssignMemory().
   * RX buffers are managed via the RxAllocateCallback mechanism
   * or by pre-filling the RX DMA descriptors in the ETH init.
   *
   * The CubeMX-generated MX_ETH1_Init() already sets up DMA
   * descriptors at the addresses specified in main.c
   * (DMARxDscrTab / DMATxDscrTab).
   *
   * For custom buffer allocation, register callbacks:
   *   HAL_ETH_RegisterRxAllocateCallback(&heth1, ...);
   *   HAL_ETH_RegisterRxLinkCallback(&heth1, ...);
   */
  (void)eth_rx_buffer;  /* Reserved for future use */
  (void)idx;
}

/**
  * @brief  Low-level TX: send an Ethernet frame via HAL_ETH.
  */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  uint32_t i = 0;
  struct pbuf *q;
  err_t errval = ERR_OK;
  ETH_BufferTypeDef tx_buffer[ETH_TX_DESC_CNT];
  ETH_TxPacketConfigTypeDef tx_config = {0};

  (void)netif;

  memset(tx_buffer, 0, sizeof(tx_buffer));

  /* Iterate through pbuf chain, building scatter-gather list */
  for (q = p; q != NULL; q = q->next)
  {
    if (i >= ETH_TX_DESC_CNT)
    {
      return ERR_BUF;
    }

    tx_buffer[i].buffer = q->payload;
    tx_buffer[i].len    = q->len;

    if (i > 0)
    {
      tx_buffer[i - 1].next = &tx_buffer[i];
    }

    if (q->next == NULL)
    {
      tx_buffer[i].next = NULL;
    }

    i++;
  }

  /* Configure TX packet */
  tx_config.Length    = p->tot_len;
  tx_config.TxBuffer = tx_buffer;
  tx_config.pData    = p;

  /* Transmit */
  pbuf_ref(p);  /* Keep reference until TX complete */

  if (HAL_ETH_Transmit_IT(&heth1, &tx_config) != HAL_OK)
  {
    pbuf_free(p);
    errval = ERR_IF;
  }

  return errval;
}

/**
  * @brief  Low-level RX: read one received Ethernet frame into a pbuf.
  *         Uses HAL_ETH_ReadData() which is the STM32N6-specific RX API.
  */
static struct pbuf *low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;
  void *app_buff = NULL;
  uint32_t frame_length = 0;

  (void)netif;

  /* Check if a frame was received */
  if (HAL_ETH_ReadData(&heth1, &app_buff) != HAL_OK)
  {
    return NULL;
  }

  /* Get frame length from RX descriptor list (channel 0) */
  frame_length = heth1.RxDescList[ETH_DMA_CHANNEL].RxDataLength;

  if ((frame_length > 0) && (app_buff != NULL))
  {
    /* Allocate a pbuf chain */
    p = pbuf_alloc(PBUF_RAW, frame_length, PBUF_POOL);

    if (p != NULL)
    {
      /* Copy received data into pbuf */
      uint32_t offset = 0;
      struct pbuf *q;

      for (q = p; q != NULL; q = q->next)
      {
        memcpy(q->payload, (uint8_t *)app_buff + offset, q->len);
        offset += q->len;
      }
    }
  }

  return p;
}

/* HAL ETH Callbacks ---------------------------------------------------------*/

/**
  * @brief  ETH RX complete callback (called from ISR context).
  */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth)
{
  (void)heth;
  ethernetif_rx_notify();
}

/**
  * @brief  ETH TX complete callback.
  */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth)
{
  (void)heth;
  /* TX buffer can be freed here if needed */
}

/**
  * @brief  ETH error callback.
  */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *heth)
{
  /* Handle ETH DMA errors — restart if necessary */
  if ((heth->ErrorCode & HAL_ETH_ERROR_DMA) != 0U)
  {
    heth->ErrorCode = HAL_ETH_ERROR_NONE;
  }
}
