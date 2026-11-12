/*
 * SurfaceRT_OS - DHCP client (RFC 2131 subset)
 *
 * DISCOVER -> OFFER -> REQUEST -> ACK with retransmission.  Runs as a small
 * state machine driven by the net thread, so no timers or threads of its own
 * are needed.
 */
#include <srtos/net.h>
#include <srtos/kernel.h>
#include <srtos/clock.h>
#include <srtos/string.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC 0x63825363u
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6
#define RETRY_MS 4000

struct __packed dhcp_msg {
    u8 op, htype, hlen, hops;
    u32 xid;
    u16 secs, flags;
    u32 ciaddr, yiaddr, siaddr, giaddr;
    u8 chaddr[16];
    u8 sname[64];
    u8 file[128];
    u32 magic;
    u8 options[312];
};

enum dhcp_state {
    DHCP_IDLE = 0,
    DHCP_SELECTING,
    DHCP_REQUESTING,
    DHCP_BOUND,
    DHCP_FAILED,
};

static struct {
    enum dhcp_state state;
    struct net_device *nd;
    u32 xid;
    u32 offered_ip;
    u32 server_ip;
    u32 mask;
    u32 router;
    u32 dns;
    u32 lease;
    u64 sent_ms;
    u32 attempts;
} D;

int dhcp_is_bound(void)
{
    return D.state == DHCP_BOUND;
}

static void dhcp_send(u8 type)
{
    struct dhcp_msg *m;
    u8 buf[sizeof(struct dhcp_msg)];
    u32 len;

    memset(buf, 0, sizeof(buf));
    m = (struct dhcp_msg *)buf;
    m->op = 1;
    m->htype = 1;
    m->hlen = 6;
    m->xid = D.xid;
    m->flags = 0x0080; /* broadcast reply */
    memcpy(m->chaddr, D.nd->mac, 6);
    m->magic = DHCP_MAGIC;

    len = sizeof(struct dhcp_msg) - sizeof(m->options);
    m->options[0] = 53;
    m->options[1] = 1;
    m->options[2] = type;
    len += 3;
    m->options[3] = 55;
    m->options[4] = 4;
    m->options[5] = 1;   /* subnet mask */
    m->options[6] = 3;   /* router */
    m->options[7] = 6;   /* dns */
    m->options[8] = 15;  /* domain */
    len += 6;
    m->options[9] = 12;
    m->options[10] = 4;
    m->options[11] = 's';
    m->options[12] = 'r';
    m->options[13] = 't';
    m->options[14] = 'o';
    len += 6;
    if (type == DHCP_REQUEST && D.offered_ip) {
        m->options[15] = 50;
        m->options[16] = 4;
        memcpy(&m->options[17], &D.offered_ip, 4);
        m->options[21] = 54;
        m->options[22] = 4;
        memcpy(&m->options[23], &D.server_ip, 4);
        len += 12;
        m->options[len++] = 255;
    } else {
        m->options[15] = 255;
        len += 1;
    }
    udp_send(0xFFFFFFFFu, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buf, len);
    D.sent_ms = clock_uptime_ms();
}

static u32 opt_u32(const u8 *opt, u32 len)
{
    u32 v = 0;
    if (len >= 4) {
        memcpy(&v, opt, 4);
    }
    return v;
}

static void dhcp_rx(u32 src_ip, u16 src_port, const u8 *data, u32 len, void *arg)
{
    const struct dhcp_msg *m = (const struct dhcp_msg *)data;
    const u8 *opt;
    u32 pos, opt_len;
    u8 msg_type = 0;

    (void)src_port;
    (void)arg;
    if (len < 240 || m->op != 2 || m->xid != D.xid || m->magic != DHCP_MAGIC) {
        return;
    }
    opt = m->options;
    opt_len = len - (u32)((const u8 *)m->options - data);
    for (pos = 0; pos < opt_len;) {
        u8 code = opt[pos];
        u8 olen;
        if (code == 0) {
            pos++;
            continue;
        }
        if (code == 255) {
            break;
        }
        if (pos + 1 >= opt_len) {
            break;
        }
        olen = opt[pos + 1];
        if (pos + 2 + olen > opt_len) {
            break;
        }
        switch (code) {
        case 53:
            msg_type = opt[pos + 2];
            break;
        case 1:
            D.mask = opt_u32(opt + pos + 2, olen);
            break;
        case 3:
            D.router = opt_u32(opt + pos + 2, olen);
            break;
        case 6:
            D.dns = opt_u32(opt + pos + 2, olen);
            break;
        case 51:
            D.lease = opt_u32(opt + pos + 2, olen);
            break;
        case 54:
            D.server_ip = opt_u32(opt + pos + 2, olen);
            break;
        default:
            break;
        }
        pos += 2 + olen;
    }

    if (msg_type == DHCP_OFFER && D.state == DHCP_SELECTING) {
        D.offered_ip = m->yiaddr;
        D.state = DHCP_REQUESTING;
        dhcp_send(DHCP_REQUEST);
    } else if (msg_type == DHCP_ACK && D.state == DHCP_REQUESTING) {
        char buf[16];
        if (!D.mask) {
            D.mask = NET_IPV4_ADDR(255, 255, 255, 0);
        }
        net_set_ip(D.nd, m->yiaddr, D.mask, D.router);
        D.lease = D.lease;
        D.state = DHCP_BOUND;
        pr_info("dhcp: bound to %s (server %s, lease %u s)\n",
                ip_to_str(m->yiaddr, buf), ip_to_str(D.server_ip, buf + 0), D.lease);
        net_arp_gratuitous(D.nd);
    } else if (msg_type == DHCP_NAK) {
        pr_warn("dhcp: request rejected, restarting\n");
        D.state = DHCP_SELECTING;
        dhcp_send(DHCP_DISCOVER);
    }
}

void dhcp_start(struct net_device *nd)
{
    if (!nd) {
        return;
    }
    memset(&D, 0, sizeof(D));
    D.nd = nd;
    D.xid = (u32)((clock_uptime_ms() * 2654435761u) ^ 0x5A5A1234u);
    D.state = DHCP_SELECTING;
    if (udp_socket_open(DHCP_CLIENT_PORT, dhcp_rx, 0) != 0) {
        pr_err("dhcp: cannot bind port %u\n", DHCP_CLIENT_PORT);
        return;
    }
    pr_info("dhcp: discovering on %s (xid 0x%08x)\n", nd->name, D.xid);
    dhcp_send(DHCP_DISCOVER);
}

/* Called once a second from the net thread. */
void dhcp_tick(void)
{
    if (D.state == DHCP_IDLE || D.state == DHCP_BOUND || !D.nd) {
        return;
    }
    if (clock_uptime_ms() - D.sent_ms < RETRY_MS) {
        return;
    }
    if (++D.attempts > 6) {
        pr_warn("dhcp: no server, falling back to a link local address\n");
        net_set_ip(D.nd, NET_IPV4_ADDR(169, 254, 1, 10),
                   NET_IPV4_ADDR(255, 255, 0, 0), 0);
        D.state = DHCP_FAILED;
        return;
    }
    if (D.state == DHCP_REQUESTING) {
        dhcp_send(DHCP_REQUEST);
    } else {
        dhcp_send(DHCP_DISCOVER);
    }
}
