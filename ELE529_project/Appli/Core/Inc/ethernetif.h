/**
  ******************************************************************************
  * @file    ethernetif.h
  * @brief   LwIP network interface (ethernetif) header for STM32N6
  ******************************************************************************
  */

#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/err.h"
#include "lwip/netif.h"
#include "cmsis_os2.h"

err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
void ethernetif_check_link(struct netif *netif);
void ethernetif_rx_notify(void);
osStatus_t ethernetif_wait_rx(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ETHERNETIF_H */
