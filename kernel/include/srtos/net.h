/* SurfaceRT_OS - minimal network stack */
#ifndef SRTOS_NET_H
#define SRTOS_NET_H

#include <srtos/types.h>
#include <srtos/list.h>
#include <srtos/irq.h>

#define ETH_ALEN 6
#define ETH_HDR_LEN 14
#define ETH_MTU 1500

#define NET_MAX_DEVS 4
/* IPv4 addresses are kept in *wire order* in memory (byte 0 = first octet),
 * which is what the packet header wants; IP_A()..IP_D() extract the octets. */
#define NET_IPV4_ADDR(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define IP_A(ip) ((u32)(ip) & 0xFFu)
#define IP_B(ip) (((u32)(ip) >> 8) & 0xFFu)
#define IP_C(ip) (((u32)(ip) >> 16) & 0xFFu)
#define IP_D(ip) (((u32)(ip) >> 24) & 0xFFu)

struct net_device {
    char name[12];
    u8 mac[ETH_ALEN];
    int mtu;
    int up;
    u32 ip;
    u32 netmask;
    u32 gateway;
    u32 dns;
    void *priv;
    int (*send)(struct net_device *nd, const void *data, u32 len);
    /* statistics */
    u32 rx_packets, tx_packets, rx_bytes, tx_bytes, rx_dropped, tx_errors;
    struct list_head node;
};

/* A received frame (zero copy within the driver's buffer). */
struct net_frame {
    const u8 *data;
    u32 len;
};

void net_init(void);
int net_register_device(struct net_device *nd);
struct net_device *net_get(const char *name);
struct net_device *net_get_index(int idx);
int net_if_count(void);
void net_rx(struct net_device *nd, const u8 *frame, u32 len);
int net_send(struct net_device *nd, const void *data, u32 len);
void net_set_ip(struct net_device *nd, u32 ip, u32 mask, u32 gw);
void net_dump(void);

/* UDP sockets */
typedef void (*udp_rx_fn)(u32 src_ip, u16 src_port, const u8 *data, u32 len,
                          void *arg);
int udp_socket_open(u16 port, udp_rx_fn fn, void *arg);
void udp_socket_close(u16 port);
int udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, u32 len);

u32 ip_checksum(const void *data, u32 len);
const char *ip_to_str(u32 ip, char *buf);
u32 ip_from_str(const char *s);

void net_arp_gratuitous(struct net_device *nd);
void dhcp_start(struct net_device *nd);
int dhcp_is_bound(void);
void dhcp_tick(void);

#endif
