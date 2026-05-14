/**
  ******************************************************************************
  * @file    lwipopts.h
  * @brief   LwIP stack configuration for STM32N6 + FreeRTOS
  ******************************************************************************
  */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * Platform / OS
 *---------------------------------------------------------------------------*/
#define NO_SYS                          0    /* FreeRTOS is present */
#define SYS_LIGHTWEIGHT_PROT            1    /* Thread-safe */
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     1

/*-----------------------------------------------------------------------------
 * Memory options
 *---------------------------------------------------------------------------*/
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16 * 1024)

#define MEMP_NUM_PBUF                   30
#define MEMP_NUM_UDP_PCB                6
#define MEMP_NUM_TCP_PCB                10
#define MEMP_NUM_TCP_PCB_LISTEN         6
#define MEMP_NUM_TCP_SEG                12
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                10
#define MEMP_NUM_SYS_TIMEOUT            10

/*-----------------------------------------------------------------------------
 * Pbuf options
 *---------------------------------------------------------------------------*/
#define PBUF_POOL_SIZE                  16
#define PBUF_POOL_BUFSIZE               1536

/*-----------------------------------------------------------------------------
 * TCP options
 *---------------------------------------------------------------------------*/
#define LWIP_TCP                        1
#define TCP_TTL                         255
#define TCP_QUEUE_OOSEQ                 0
#define TCP_MSS                         (1500 - 40)  /* TCP_MSS = MTU - IP/TCP headers */
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                         (4 * TCP_MSS)

/*-----------------------------------------------------------------------------
 * UDP options
 *---------------------------------------------------------------------------*/
#define LWIP_UDP                        1
#define UDP_TTL                         255

/*-----------------------------------------------------------------------------
 * ICMP options
 *---------------------------------------------------------------------------*/
#define LWIP_ICMP                       1

/*-----------------------------------------------------------------------------
 * DHCP options
 *---------------------------------------------------------------------------*/
#define LWIP_DHCP                       1

/*-----------------------------------------------------------------------------
 * ARP options
 *---------------------------------------------------------------------------*/
#define LWIP_ARP                        1
#define ARP_TABLE_SIZE                  10
#define ARP_QUEUEING                    1

/*-----------------------------------------------------------------------------
 * IP options
 *---------------------------------------------------------------------------*/
#define IP_FORWARD                      0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

/*-----------------------------------------------------------------------------
 * Network interface options
 *---------------------------------------------------------------------------*/
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1

/*-----------------------------------------------------------------------------
 * Statistics
 *---------------------------------------------------------------------------*/
#define LWIP_STATS                      0

/*-----------------------------------------------------------------------------
 * Checksum options — hardware checksum offload for STM32 ETH
 *---------------------------------------------------------------------------*/
#define CHECKSUM_BY_HARDWARE            1

#if CHECKSUM_BY_HARDWARE
  #define CHECKSUM_GEN_IP               0
  #define CHECKSUM_GEN_UDP              0
  #define CHECKSUM_GEN_TCP              0
  #define CHECKSUM_GEN_ICMP             0
  #define CHECKSUM_CHECK_IP             0
  #define CHECKSUM_CHECK_UDP            0
  #define CHECKSUM_CHECK_TCP            0
  #define CHECKSUM_CHECK_ICMP           0
#endif

/*-----------------------------------------------------------------------------
 * OS / threading options (FreeRTOS)
 *---------------------------------------------------------------------------*/
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          1024
#define TCPIP_THREAD_PRIO               24       /* osPriorityAboveNormal */
#define TCPIP_MBOX_SIZE                 8

#define DEFAULT_THREAD_STACKSIZE        512
#define DEFAULT_ACCEPTMBOX_SIZE         4
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       8

/*-----------------------------------------------------------------------------
 * Debug options (disabled by default; enable selectively for debugging)
 *---------------------------------------------------------------------------*/
#define LWIP_DEBUG                      0

#ifdef __cplusplus
}
#endif

#endif /* LWIPOPTS_H */
