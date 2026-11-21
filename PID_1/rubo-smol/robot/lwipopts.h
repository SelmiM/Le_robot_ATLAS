#pragma once

// lwIP settings for this robot, based on the Pico SDK example. Disable unused
// features to limit RAM use. Most work should not require changing this file.

// No operating system: lwIP runs from our main loop, not its own thread.
// This is what pairs with pico_cyw43_arch_lwip_poll in CMakeLists.txt.
#define NO_SYS                      1

// The BSD-socket and netconn APIs both need threads, so they are out. We use
// lwIP's "raw" callback API instead -- see WebRadio.h.
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
// MEM_SIZE is where everything we send waits until it is acknowledged. The
// phone page is several kilobytes, and a browser opens a few connections at
// once, so 4000 bytes was too tight. MEMP_NUM_TCP_PCB is how many connections
// can exist at once: WebRadio.h keeps up to 8, plus a few closing down.
#define MEM_SIZE                    16000
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_DHCP                   1     // ask the network for an address
#define LWIP_DNS                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN_FULLDUPLEX     0
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// Let the hardware work out checksums where it can.
#define LWIP_CHKSUM_ALGORITHM       3

// Set any of these to 1, plus LWIP_DEBUG, when the network misbehaves.
#define LWIP_DEBUG                  0
#define TCP_DEBUG                   LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF
