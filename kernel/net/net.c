/*
 * SurfaceRT_OS - minimal but real network stack
 *
 * Ethernet -> ARP -> IPv4 -> UDP.  Deliberately not a TCP stack: the wireless
 * display protocol runs on UDP with its own framing, acknowledgements and
 * keyframe recovery, which is the right trade-off for latency.
 *
 * Everything is zero-copy on receive (the driver hands us its buffer) and a
 * single reusable 1500 byte buffer on transmit.
 */
#include <srtos/net.h>
#include <srtos/kernel.h>
#include <srtos/sched.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/arch.h>

#define ARP_CACHE_SIZE 8
#define ARP_TIMEOUT_MS 60000
#define UDP_MAX_SOCKETS 8
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17
#define ARP_REQUEST 1
#define ARP_REPLY 2

struct __packed eth_hdr {
    u8 dst[6];
    u8 src[6];
    u16 type;
};

struct __packed arp_pkt {
    u16 htype;
    u16 ptype;
    u8 hlen;
    u8 plen;
    u16 oper;
    u8 sha[6];
    u32 spa;
    u8 tha[6];
    u32 tpa;
};

struct __packed ip_hdr {
    u8 version_ihl;
    u8 tos;
    u16 total_len;
    u16 id;
    u16 frag;
    u8 ttl;
    u8 proto;
    u16 checksum;
    u32 src;
    u32 dst;
};

struct __packed udp_hdr {
    u16 src_port;
    u16 dst_port;
    u16 len;
    u16 checksum;
};

struct __packed icmp_hdr {
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 seq;
};

struct arp_entry {
    u32 ip;
    u8 mac[6];
    u64 stamp_ms;
    int valid;
};

struct udp_socket {
    u16 port;
    udp_rx_fn fn;
    void *arg;
    int used;
};

static struct net_device *devices[NET_MAX_DEVS];
static int nr_devices;
static struct arp_entry arp_cache[ARP_CACHE_SIZE];
static struct udp_socket udp_sockets[UDP_MAX_SOCKETS];
static u8 tx_buffer[1600] __aligned(8);
static spinlock_t net_lock = SPINLOCK_INIT;
static u32 g_icmp_id;

static inline u16 bswap16(u16 v)
{
    return (u16)((v >> 8) | (v << 8));
}

static const u8 broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void net_init(void)
{
    memset(devices, 0, sizeof(devices));
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(udp_sockets, 0, sizeof(udp_sockets));
    nr_devices = 0;
    g_icmp_id = 0x5352;
    pr_info("net: stack ready (ARP/IPv4/ICMP/UDP)\n");
}

int net_register_device(struct net_device *nd)
{
    if (!nd || nr_devices >= NET_MAX_DEVS) {
        return -1;
    }
    if (!nd->mtu) {
        nd->mtu = ETH_MTU;
    }
    nd->up = 1;
    devices[nr_devices++] = nd;
    pr_info("net: %s up, mac %02x:%02x:%02x:%02x:%02x:%02x\n", nd->name,
           nd->mac[0], nd->mac[1], nd->mac[2], nd->mac[3], nd->mac[4], nd->mac[5]);
    return 0;
}

struct net_device *net_get(const char *name)
{
    int i;
    for (i = 0; i < nr_devices; i++) {
        if (!strcmp(devices[i]->name, name)) {
            return devices[i];
        }
    }
    return 0;
}

struct net_device *net_get_index(int idx)
{
    return (idx >= 0 && idx < nr_devices) ? devices[idx] : 0;
}

int net_if_count(void)
{
    return nr_devices;
}

void net_set_ip(struct net_device *nd, u32 ip, u32 mask, u32 gw)
{
    if (!nd) {
        return;
    }
    nd->ip = ip;
    nd->netmask = mask;
    nd->gateway = gw;
    pr_info("net: %s ip %u.%u.%u.%u/%u.%u.%u.%u\n", nd->name, ip >> 24,
           (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, mask >> 24,
           (mask >> 16) & 0xFF, (mask >> 8) & 0xFF, mask & 0xFF);
}

u32 ip_checksum(const void *data, u32 len)
{
    const u8 *p = (const u8 *)data;
    u32 sum = 0;
    while (len > 1) {
        sum += ((u32)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (u32)p[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum & 0xFFFF;
}

const char *ip_to_str(u32 ip, char *buf)
{
    snprintf(buf, 16, "%u.%u.%u.%u", IP_A(ip), IP_B(ip), IP_C(ip), IP_D(ip));
    return buf;
}

u32 ip_from_str(const char *s)
{
    u32 parts[4] = { 0, 0, 0, 0 };
    int i;
    for (i = 0; i < 4 && *s; i++) {
        parts[i] = strtoul(s, (char **)&s, 10);
        if (*s == '.') {
            s++;
        }
    }
    return NET_IPV4_ADDR(parts[0] & 0xFF, parts[1] & 0xFF, parts[2] & 0xFF,
                        parts[3] & 0xFF);
}

static void arp_cache_store(u32 ip, const u8 *mac)
{
    int i, slot = 0;
    u64 oldest = ~0ull;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            slot = i;
            goto store;
        }
        if (!arp_cache[i].valid) {
            slot = i;
            goto store;
        }
        if (arp_cache[i].stamp_ms < oldest) {
            oldest = arp_cache[i].stamp_ms;
            slot = i;
        }
    }
store:
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].stamp_ms = clock_uptime_ms();
    arp_cache[slot].valid = 1;
}

static int arp_cache_lookup(u32 ip, u8 *mac)
{
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            if (clock_uptime_ms() - arp_cache[i].stamp_ms > ARP_TIMEOUT_MS) {
                arp_cache[i].valid = 0;
                return -1;
            }
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static void eth_send(struct net_device *nd, const u8 *dst, u16 type,
                     const u8 *payload, u32 len)
{
    struct eth_hdr *eh;
    u32 flags = spin_lock_irqsave(&net_lock);

    if (len + ETH_HDR_LEN > sizeof(tx_buffer)) {
        spin_unlock_irqrestore(&net_lock, flags);
        return;
    }
    eh = (struct eth_hdr *)tx_buffer;
    memcpy(eh->dst, dst, 6);
    memcpy(eh->src, nd->mac, 6);
    eh->type = (u16)((type >> 8) | (type << 8));
    memcpy(tx_buffer + ETH_HDR_LEN, payload, len);
    nd->send(nd, tx_buffer, len + ETH_HDR_LEN);
    nd->tx_packets++;
    nd->tx_bytes += len + ETH_HDR_LEN;
    spin_unlock_irqrestore(&net_lock, flags);
}

static void arp_send(struct net_device *nd, u16 oper, u32 target_ip,
                     const u8 *target_mac)
{
    struct arp_pkt arp;
    u8 dst[6];

    memset(&arp, 0, sizeof(arp));
    arp.htype = 0x0100; /* big endian 1 */
    arp.ptype = 0x0008; /* big endian 0x0800 */
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = (u16)((oper >> 8) | (oper << 8));
    memcpy(arp.sha, nd->mac, 6);
    arp.spa = nd->ip; /* little endian on the wire for ARM: see note below */
    if (target_mac) {
        memcpy(arp.tha, target_mac, 6);
        memcpy(dst, target_mac, 6);
    } else {
        memcpy(dst, broadcast_mac, 6);
    }
    arp.tpa = target_ip;
    eth_send(nd, dst, 0x0806, (const u8 *)&arp, sizeof(arp));
}

void net_arp_gratuitous(struct net_device *nd)
{
    arp_send(nd, ARP_REQUEST, nd->ip, 0);
}

int net_send(struct net_device *nd, const void *data, u32 len)
{
    if (!nd || !nd->send) {
        return -1;
    }
    return nd->send(nd, data, len);
}

static int resolve_mac(struct net_device *nd, u32 dst_ip, u8 *mac)
{
    if (dst_ip == 0xFFFFFFFFu || dst_ip == 0) {
        memcpy(mac, broadcast_mac, 6);
        return 0;
    }
    if ((dst_ip & nd->netmask) != (nd->ip & nd->netmask)) {
        if (!nd->gateway) {
            return -1;
        }
        if (arp_cache_lookup(nd->gateway, mac) == 0) {
            return 0;
        }
        arp_send(nd, ARP_REQUEST, nd->gateway, 0);
        return -1;
    }
    if (arp_cache_lookup(dst_ip, mac) == 0) {
        return 0;
    }
    arp_send(nd, ARP_REQUEST, dst_ip, 0);
    return -1;
}

static void ip_send(struct net_device *nd, u32 dst_ip, u8 proto,
                    const u8 *payload, u32 len)
{
    u8 packet[ETH_MTU];
    struct ip_hdr *ip = (struct ip_hdr *)packet;
    u8 mac[6];
    u32 total = sizeof(*ip) + len;

    if (total > sizeof(packet)) {
        return;
    }
    if (resolve_mac(nd, dst_ip, mac) != 0) {
        nd->tx_errors++;
        return;
    }

    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = bswap16((u16)total);
    ip->id = (u16)((u32)clock_uptime_ms() & 0xFFFF);
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->src = nd->ip;
    ip->dst = dst_ip;
    ip->checksum = 0;
    ip->checksum = bswap16((u16)ip_checksum(ip, sizeof(*ip)));
    memcpy(packet + sizeof(*ip), payload, len);
    eth_send(nd, mac, 0x0800, packet, total);
}

int udp_socket_open(u16 port, udp_rx_fn fn, void *arg)
{
    int i;
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == port) {
            udp_sockets[i].fn = fn;
            udp_sockets[i].arg = arg;
            return 0;
        }
    }
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].fn = fn;
            udp_sockets[i].arg = arg;
            pr_debug("net: udp port %u bound\n", port);
            return 0;
        }
    }
    return -1;
}

void udp_socket_close(u16 port)
{
    int i;
    for (i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == port) {
            udp_sockets[i].used = 0;
        }
    }
}

int udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, u32 len)
{
    u8 packet[ETH_MTU];
    struct udp_hdr *udp = (struct udp_hdr *)packet;
    struct net_device *nd = 0;
    u32 pseudo_sum = 0, i;
    u32 total = sizeof(*udp) + len;
    int d;

    if (!len || total > sizeof(packet)) {
        return -1;
    }
    for (d = 0; d < nr_devices; d++) {
        if (devices[d]->up && (devices[d]->ip & devices[d]->netmask) ==
                                  (dst_ip & devices[d]->netmask)) {
            nd = devices[d];
            break;
        }
    }
    if (!nd) {
        for (d = 0; d < nr_devices; d++) {
            if (devices[d]->up && devices[d]->ip) {
                nd = devices[d];
                break;
            }
        }
    }
    if (!nd) {
        return -1;
    }
    memset(udp, 0, sizeof(*udp));
    udp->src_port = bswap16(src_port);
    udp->dst_port = bswap16(dst_port);
    udp->len = bswap16((u16)total);
    memcpy(packet + sizeof(*udp), data, len);

    /* pseudo header checksum */
    {
        const u8 *src = (const u8 *)&nd->ip;
        const u8 *dst = (const u8 *)&dst_ip;
        for (i = 0; i < 4; i++) {
            pseudo_sum += (u32)src[i] << 8;
            pseudo_sum += dst[i] << 8;
        }
        pseudo_sum += 17;
        pseudo_sum += total;
    }
    udp->checksum = 0;
    {
        u32 sum = ip_checksum(packet, total);
        sum += pseudo_sum;
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        udp->checksum = bswap16((u16)(~sum));
    }
    ip_send(nd, dst_ip, IP_PROTO_UDP, packet, total);
    return 0;
}

static void icmp_send_echo_reply(struct net_device *nd, const u8 *payload, u32 len)
{
    u8 packet[ETH_MTU];
    if (len > sizeof(packet)) {
        return;
    }
    memcpy(packet, payload, len);
    ((struct icmp_hdr *)packet)->type = 0; /* echo reply */
    ((struct icmp_hdr *)packet)->checksum = 0;
    ((struct icmp_hdr *)packet)->checksum =
        bswap16((u16)ip_checksum(packet, len));
    ip_send(nd, 0, IP_PROTO_ICMP, packet, len);
}

static void ip_input(struct net_device *nd, const u8 *data, u32 len)
{
    const struct ip_hdr *ip = (const struct ip_hdr *)data;
    u32 ihl = (ip->version_ihl & 0x0F) * 4u;
    u32 total = bswap16(ip->total_len);
    u8 proto = ip->proto;

    if ((ip->version_ihl >> 4) != 4 || ihl < 20 || total > len) {
        nd->rx_dropped++;
        return;
    }
    if (ip_checksum(data, ihl) != 0) {
        nd->rx_dropped++;
        return;
    }
    if (ip->dst != nd->ip && ip->dst != 0xFFFFFFFFu &&
        !((ip->dst & nd->netmask) == (nd->ip & nd->netmask))) {
        /* not for us (no forwarding) */
        nd->rx_dropped++;
        return;
    }

    if (proto == IP_PROTO_UDP) {
        const struct udp_hdr *udp = (const struct udp_hdr *)(data + ihl);
        u32 ulen = bswap16(udp->len);
        u16 dport = bswap16(udp->dst_port);
        u16 sport = bswap16(udp->src_port);
        int i;
        if (ulen < sizeof(*udp) || ihl + ulen > len) {
            nd->rx_dropped++;
            return;
        }
        for (i = 0; i < UDP_MAX_SOCKETS; i++) {
            if (udp_sockets[i].used && udp_sockets[i].port == dport) {
                if (udp_sockets[i].fn) {
                    udp_sockets[i].fn(ip->src, sport, data + ihl + sizeof(*udp),
                                      ulen - sizeof(*udp), udp_sockets[i].arg);
                }
                return;
            }
        }
        nd->rx_dropped++;
    } else if (proto == IP_PROTO_ICMP) {
        const struct icmp_hdr *icmp = (const struct icmp_hdr *)(data + ihl);
        if (total >= ihl + 8 && icmp->type == 8) {
            icmp_send_echo_reply(nd, data + ihl, total - ihl);
        }
    }
}

static void arp_input(struct net_device *nd, const u8 *data, u32 len)
{
    const struct arp_pkt *arp = (const struct arp_pkt *)data;
    if (len < sizeof(*arp) || bswap16(arp->oper) != ARP_REPLY) {
        return;
    }
    arp_cache_store(arp->spa, arp->sha);
}

void net_rx(struct net_device *nd, const u8 *frame, u32 len)
{
    const struct eth_hdr *eh = (const struct eth_hdr *)frame;
    u16 type;

    if (!nd || len < ETH_HDR_LEN) {
        return;
    }
    nd->rx_packets++;
    nd->rx_bytes += len;
    if (!memcmp(eh->src, nd->mac, 6)) {
        return; /* our own frame echoed back */
    }
    if (memcmp(eh->dst, nd->mac, 6) && memcmp(eh->dst, broadcast_mac, 6)) {
        nd->rx_dropped++;
        return;
    }
    type = bswap16(eh->type);
    if (type == 0x0800) {
        ip_input(nd, frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
    } else if (type == 0x0806) {
        arp_input(nd, frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
    }
}

void net_dump(void)
{
    int i;
    char buf[16];
    printk("  IF       MAC                IP              RX/TX\n");
    for (i = 0; i < nr_devices; i++) {
        struct net_device *nd = devices[i];
        u32 rx = nd->rx_packets, tx = nd->tx_packets;
        printk("  %-8s %02x:%02x:%02x:%02x:%02x:%02x  %-15s %u/%u\n", nd->name,
               nd->mac[0], nd->mac[1], nd->mac[2], nd->mac[3], nd->mac[4],
               nd->mac[5], ip_to_str(nd->ip, buf), rx, tx);
    }
}

void net_thread_main(void *arg)
{
    struct net_device *first = net_get_index(0);
    int dhcp_kicked = 0;

    (void)arg;
    for (;;) {
        msleep(1000);
        if (!dhcp_kicked && first) {
            dhcp_start(first);
            dhcp_kicked = 1;
        }
        dhcp_tick();
    }
}
