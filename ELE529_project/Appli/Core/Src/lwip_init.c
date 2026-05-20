/**
  ******************************************************************************
  * @file    lwip_init.c
  * @brief   LwIP TCP/IP stack initialization for STM32N6 + FreeRTOS
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "ethernetif.h"
#include "bsp_eth.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"

#include <stdio.h>

/* Private defines -----------------------------------------------------------*/

/* Set to 1 to use DHCP, 0 for static IP */
#ifndef USE_DHCP
#define USE_DHCP                1
#endif

/* Static IP configuration (used when USE_DHCP == 0) */
#define STATIC_IP_ADDR0         192
#define STATIC_IP_ADDR1         168
#define STATIC_IP_ADDR2         1
#define STATIC_IP_ADDR3         100

#define STATIC_NETMASK0         255
#define STATIC_NETMASK1         255
#define STATIC_NETMASK2         255
#define STATIC_NETMASK3         0

#define STATIC_GW_ADDR0         192
#define STATIC_GW_ADDR1         168
#define STATIC_GW_ADDR2         1
#define STATIC_GW_ADDR3         1

/* Private variables ---------------------------------------------------------*/

/* LwIP network interface */
struct netif gnetif;

/* DHCP state tracking */
static volatile uint8_t dhcp_started = 0;

/* Private function prototypes -----------------------------------------------*/
static void tcpip_init_done_cb(void *arg);
static void netif_status_cb(struct netif *netif);
static void netif_link_cb(struct netif *netif);

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Initialize the LwIP TCP/IP stack.
  *         Must be called from a FreeRTOS task context (after scheduler start).
  */
void LwIP_Init(void)
{
  ip4_addr_t ipaddr, netmask, gateway;

  /* Initialize the LwIP stack with RTOS */
  tcpip_init(tcpip_init_done_cb, NULL);

#if USE_DHCP
  ip4_addr_set_zero(&ipaddr);
  ip4_addr_set_zero(&netmask);
  ip4_addr_set_zero(&gateway);
#else
  IP4_ADDR(&ipaddr,  STATIC_IP_ADDR0, STATIC_IP_ADDR1,
                     STATIC_IP_ADDR2, STATIC_IP_ADDR3);
  IP4_ADDR(&netmask, STATIC_NETMASK0, STATIC_NETMASK1,
                     STATIC_NETMASK2, STATIC_NETMASK3);
  IP4_ADDR(&gateway, STATIC_GW_ADDR0, STATIC_GW_ADDR1,
                     STATIC_GW_ADDR2, STATIC_GW_ADDR3);
#endif

  /* Add the network interface */
  netif_add(&gnetif, &ipaddr, &netmask, &gateway, NULL,
            &ethernetif_init, &tcpip_input);

  /* Set as default interface */
  netif_set_default(&gnetif);

  /* Register callbacks */
  netif_set_status_callback(&gnetif, netif_status_cb);
  netif_set_link_callback(&gnetif, netif_link_cb);

  /* Initialize PHY and check initial link state */
  BSP_ETH_PHY_Init();

  /* Start with link check — will bring interface up if cable is connected */
  ethernetif_check_link(&gnetif);
}

/**
  * @brief  LwIP periodic handler — call from the Ethernet link task.
  *         Checks link state and manages DHCP.
  */
void LwIP_Process(void)
{
  /* Check link status */
  ethernetif_check_link(&gnetif);

#if USE_DHCP
  /* Start DHCP if link is up and DHCP hasn't been started */
  if (netif_is_link_up(&gnetif) && !dhcp_started)
  {
    dhcp_start(&gnetif);
    dhcp_started = 1;
  }

  /* If link went down, stop DHCP */
  if (!netif_is_link_up(&gnetif) && dhcp_started)
  {
    dhcp_stop(&gnetif);
    dhcp_started = 0;
  }
#endif
}

/**
  * @brief  Get the current IP address string (for debug output).
  * @param  buf   Output buffer (at least 16 bytes)
  * @param  size  Buffer size
  * @retval Pointer to buf
  */
char *LwIP_GetIPAddress(char *buf, uint32_t size)
{
  ip4_addr_t addr = *netif_ip4_addr(&gnetif);

  if (addr.addr != 0)
  {
    uint8_t *ip = (uint8_t *)&addr.addr;
    snprintf(buf, size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }
  else
  {
    snprintf(buf, size, "0.0.0.0");
  }

  return buf;
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  tcpip_init completion callback.
  */
static void tcpip_init_done_cb(void *arg)
{
  (void)arg;
  /* TCP/IP thread is running */
}

/**
  * @brief  Network interface status change callback.
  */
static void netif_status_cb(struct netif *netif)
{
  if (netif_is_up(netif))
  {
    /* Interface is up — IP address may be assigned */
    /* You can add USART debug printf here */
  }
  else
  {
    /* Interface is down */
  }
}

/**
  * @brief  Network interface link change callback.
  */
static void netif_link_cb(struct netif *netif)
{
  if (netif_is_link_up(netif))
  {
    /* Ethernet cable connected */
#if USE_DHCP
    if (!dhcp_started)
    {
      dhcp_start(netif);
      dhcp_started = 1;
    }
#else
    netif_set_up(netif);
#endif
  }
  else
  {
    /* Ethernet cable disconnected */
#if USE_DHCP
    dhcp_stop(netif);
    dhcp_started = 0;
#endif
    netif_set_down(netif);
  }
}
