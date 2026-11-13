/*
 * SurfaceRT_OS - Tegra 3 EHCI host controller + USB HID boot keyboard
 *
 * Bring-up: clock/power the UTMI pads, reset the controller, build the
 * asynchronous queue head list, reset the root port, enumerate the device with
 * control transfers and poll the HID interrupt endpoint from a kernel thread.
 *
 * The transfer engine uses one qTD chain per transfer with the "interrupt on
 * complete" bit set and polls the status register; that is slower than
 * interrupts but cannot wedge the kernel on a misbehaving device.
 */
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/sched.h>
#include <srtos/input.h>
#include <srtos/clock.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include <srtos/irq.h>
#include <srtos/soc.h>

#define USB1_BASE TEGRA_USB_BASE(0) /* xHCI-less EHCI instance on T30     */
#define USB_TIMEOUT_MS 200

struct __packed qtd {
    u32 next;
    u32 alt_next;
    u32 token;
    u32 buf[5];
};

struct __packed qh {
    u32 horiz_link;
    u32 ep_char;
    u32 ep_cap;
    u32 cur_qtd;
    u32 next_qtd;
    u32 alt_qtd;
    u32 token;
    u32 buf[5];
};

static struct qh *g_async_head;
static struct qtd *g_qtd_pool;
static int g_usb_ready;
static int g_kbd_addr = -1;
static int g_kbd_ep, g_kbd_mps, g_kbd_iface;

static inline u32 usb_rd(u32 off)
{
    return readl(USB1_BASE + off);
}

static inline void usb_wr(u32 off, u32 v)
{
    writel(v, USB1_BASE + off);
}

static void usb_delay_ms(u32 ms)
{
    mdelay(ms);
}

static int usb_wait(u32 mask, u32 value, u32 timeout_ms)
{
    u32 start = (u32)clock_uptime_ms();
    while ((usb_rd(EHCI_USBSTS) & mask) != value) {
        if ((u32)clock_uptime_ms() - start > timeout_ms) {
            return -1;
        }
    }
    return 0;
}

static int usb_controller_reset(void)
{
    usize cap = usb_rd(EHCI_CAPLENGTH) & 0xFF;
    (void)cap;

    usb_wr(EHCI_USBCMD, 0);
    usb_delay_ms(5);
    usb_wr(EHCI_USBCMD, EHCI_USBCMD_RST);
    usb_delay_ms(10);
    if (usb_wait(EHCI_USBCMD_RST, 0, 250) != 0) {
        return -1;
    }
    usb_wr(EHCI_USBINTR, 0);
    usb_wr(EHCI_CTRLDSSEGMENT, 0);
    usb_wr(EHCI_USBCMD, EHCI_USBCMD_RUN | EHCI_USBCMD_PSE | EHCI_USBCMD_ASE);
    if (usb_wait(EHCI_USBSTS_HALTED, 0, 100) != 0) {
        return -1;
    }
    return 0;
}

static int usb_build_lists(void)
{
    if (!g_async_head) {
        g_async_head = (struct qh *)pmm_alloc_zeroed(1);
        g_qtd_pool = (struct qtd *)pmm_alloc_zeroed(1);
    }
    if (!g_async_head || !g_qtd_pool) {
        return -1;
    }
    memset(g_async_head, 0, 64);
    g_async_head->horiz_link = (u32)(uptr)g_async_head | BIT(1); /* QH, self */
    g_async_head->ep_char = (64u << 16) | (2u << 12) | 0x40u;    /* 64B, high */
    g_async_head->ep_cap = (1u << 30) | (0xFFu << 16) | (1u << 15) | 0x40u;
    g_async_head->next_qtd = 1; /* terminate */
    usb_wr(EHCI_ASYNCLISTADDR, (u32)(uptr)g_async_head);
    return 0;
}

static void usb_port_power(int port)
{
    u32 v = usb_rd(EHCI_PORTSC(port));
    usb_wr(EHCI_PORTSC(port), (v & ~EHCI_PORTSC_OWNER) | EHCI_PORTSC_POWER);
    usb_delay_ms(20);
}

static int usb_port_reset(int port)
{
    u32 v = usb_rd(EHCI_PORTSC(port));
    usb_wr(EHCI_PORTSC(port), (v & ~EHCI_PORTSC_OWNER) | EHCI_PORTSC_RESET);
    usb_delay_ms(50);
    v = usb_rd(EHCI_PORTSC(port));
    usb_wr(EHCI_PORTSC(port), v & ~EHCI_PORTSC_RESET);
    usb_delay_ms(20);
    v = usb_rd(EHCI_PORTSC(port));
    if (!(v & EHCI_PORTSC_ENABLE)) {
        pr_warn("usb: port %d did not enable (portsc=0x%08x)\n", port, v);
        return -1;
    }
    return 0;
}

#define QTD_PID_OUT 0xE1u
#define QTD_PID_IN 0x69u
#define QTD_PID_SETUP 0x2Du
#define QTD_ACTIVE 0x80u

static u8 *g_dma;          /* 4 KiB scratch: setup packet + data stage */
static struct qtd *g_qtd;  /* three qTDs, used as a chain */
static struct qh *g_ep0_qh;

static void qtd_init(struct qtd *q, u8 pid, u32 bytes, void *buf, int ioc,
                     int toggle)
{
    memset(q, 0, sizeof(*q));
    q->next = 1; /* terminate */
    q->token = pid | (3u << 3) | ((u32)(ioc ? 1 : 0) << 6) |
               ((bytes & 0x7FFF) << 16) | ((u32)(toggle ? 1 : 0) << 31);
    q->buf[0] = (u32)(uptr)buf;
}

static int qh_wait_idle(u32 timeout_ms)
{
    u32 start = (u32)clock_uptime_ms();
    while (g_ep0_qh->token & QTD_ACTIVE) {
        if ((u32)clock_uptime_ms() - start > timeout_ms) {
            return -1;
        }
    }
    return 0;
}

static int usb_control(int addr, int mps, u8 request_type, u8 request,
                       u16 value, u16 index, void *data, u16 len)
{
    u8 *setup = g_dma;
    u8 *stage = g_dma + 64;
    struct qtd *q_setup = &g_qtd[0];
    struct qtd *q_data = &g_qtd[1];
    struct qtd *q_status = &g_qtd[2];
    int in = (request_type & 0x80) ? 1 : 0;

    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (u8)(value & 0xFF);
    setup[3] = (u8)(value >> 8);
    setup[4] = (u8)(index & 0xFF);
    setup[5] = (u8)(index >> 8);
    setup[6] = (u8)(len & 0xFF);
    setup[7] = (u8)(len >> 8);

    if (len && !in && data) {
        memcpy(stage, data, len);
    }
    if (qh_wait_idle(USB_TIMEOUT_MS) != 0) {
        return -1;
    }

    qtd_init(q_setup, QTD_PID_SETUP, 8, setup, 0, 0);
    if (len) {
        qtd_init(q_data, in ? QTD_PID_IN : QTD_PID_OUT, len, stage, 0, 1);
        q_setup->next = (u32)(uptr)q_data;
        qtd_init(q_status, in ? QTD_PID_OUT : QTD_PID_IN, 0, 0, 1, 1);
        q_data->next = (u32)(uptr)q_status;
    } else {
        qtd_init(q_status, in ? QTD_PID_OUT : QTD_PID_IN, 0, 0, 1, 1);
        q_setup->next = (u32)(uptr)q_status;
    }

    g_ep0_qh->ep_char = ((u32)mps << 16) | ((u32)addr << 8) | 0x40u;
    g_ep0_qh->ep_cap = ((u32)mps << 16) | (1u << 14) | 0x40u;
    g_ep0_qh->next_qtd = 1;
    g_ep0_qh->alt_qtd = 1;
    g_ep0_qh->token = 0;
    g_ep0_qh->horiz_link = (u32)(uptr)g_ep0_qh | BIT(1);

    /* hand the chain to the controller */
    g_ep0_qh->next_qtd = (u32)(uptr)q_setup;
    __asm__ __volatile__("dsb" ::: "memory");
    g_ep0_qh->token = QTD_ACTIVE << 24; /* status byte in the overlay */
    __asm__ __volatile__("dsb" ::: "memory");

    if (qh_wait_idle(USB_TIMEOUT_MS) != 0) {
        pr_debug("usb: control transfer timed out (req %u)\n", request);
        return -1;
    }
    if ((g_ep0_qh->token >> 24) & 0x40) { /* halted */
        return -1;
    }
    if (len && in && data) {
        memcpy(data, stage, len);
    }
    return 0;
}

static int usb_get_descriptor(int addr, int mps, u8 type, u8 index, void *buf,
                              u16 len)
{
    return usb_control(addr, mps, 0x80, 6, (u16)((type << 8) | index), 0, buf, len);
}

struct __packed usb_device_desc {
    u8 len, type;
    u16 bcd_usb;
    u8 cls, sub, proto, max_packet;
    u16 vendor, product, bcd_device;
    u8 imanufacturer, iproduct, iserial;
    u8 configurations;
};

struct __packed usb_config_desc {
    u8 len, type;
    u16 total_len;
    u8 interfaces, config_value, iconfig, attributes, max_power;
};

struct __packed usb_iface_desc {
    u8 len, type, number, alt, endpoints, cls, sub, proto, iface_string;
};

struct __packed usb_ep_desc {
    u8 len, type, address, attributes;
    u16 max_packet;
    u8 interval;
};

static int usb_set_address(int addr, int mps)
{
    return usb_control(0, mps, 0x00, 5, (u16)addr, 0, 0, 0);
}

static int usb_set_config(int addr, int mps, u8 config)
{
    return usb_control(addr, mps, 0x00, 9, config, 0, 0, 0);
}

static void usb_scan_configuration(int addr, int mps, const u8 *cfg, u32 len)
{
    u32 pos = 0;
    int iface = -1;

    while (pos + 2 <= len) {
        u8 dlen = cfg[pos];
        u8 dtype = cfg[pos + 1];
        if (dlen < 2) {
            break;
        }
        if (dtype == 4 && pos + 9 <= len) { /* interface */
            const struct usb_iface_desc *ifd =
                (const struct usb_iface_desc *)(cfg + pos);
            iface = ifd->number;
            if (ifd->cls == 3) { /* HID */
                pr_info("usb: HID interface %d, protocol %d\n", ifd->number,
                        ifd->proto);
            }
        } else if (dtype == 5 && pos + 7 <= len) { /* endpoint */
            const struct usb_ep_desc *ep = (const struct usb_ep_desc *)(cfg + pos);
            if ((ep->attributes & 0x03) == 3) { /* interrupt */
                if (g_kbd_addr < 0) {
                    g_kbd_addr = addr;
                    g_kbd_ep = ep->address;
                    g_kbd_mps = ep->max_packet;
                    g_kbd_iface = iface;
                    pr_info("usb: keyboard endpoint 0x%02x, %u bytes, %u ms\n",
                            ep->address, ep->max_packet, ep->interval);
                }
            }
        }
        pos += dlen;
    }
}

static int usb_enumerate_port(int port)
{
    struct usb_device_desc dev;
    u8 cfg_buf[256];
    int mps = 8;

    usb_port_power(port);
    if (!(usb_rd(EHCI_PORTSC(port)) & EHCI_PORTSC_CONN)) {
        return -1;
    }
    if (usb_port_reset(port) != 0) {
        return -1;
    }
    if (usb_get_descriptor(0, mps, 1, 0, &dev, 8) != 0) {
        pr_warn("usb: no device descriptor on port %d\n", port);
        return -1;
    }
    mps = dev.max_packet ? dev.max_packet : 8;
    if (usb_get_descriptor(0, mps, 1, 0, &dev, sizeof(dev)) != 0) {
        return -1;
    }
    pr_info("usb: device %04x:%04x class %u on port %d (mps %d)\n",
            dev.vendor, dev.product, dev.cls, port, mps);
    if (usb_set_address(1, mps) != 0) {
        return -1;
    }
    mdelay(5);
    if (usb_get_descriptor(1, mps, 2, 0, cfg_buf, 9) != 0) {
        return -1;
    }
    {
        const struct usb_config_desc *cd = (const struct usb_config_desc *)cfg_buf;
        u32 total = cd->total_len > sizeof(cfg_buf) ? sizeof(cfg_buf) : cd->total_len;
        if (usb_get_descriptor(1, mps, 2, 0, cfg_buf, (u16)total) != 0) {
            return -1;
        }
        usb_scan_configuration(1, mps, cfg_buf, total);
        if (usb_set_config(1, mps, cd->config_value) != 0) {
            return -1;
        }
    }
    if (g_kbd_addr == 1) {
        /* boot protocol: byte 0 = modifiers, byte 2..7 = key codes */
        usb_control(1, mps, 0x21, 11, 0, g_kbd_iface, 0, 0);
    }
    return 0;
}

static void usb_kbd_thread(void *arg)
{
    u8 last[8];
    int i;
    (void)arg;

    memset(last, 0, sizeof(last));
    pr_info("usb: HID keyboard poller running\n");
    for (;;) {
        u8 report[8];
        memset(report, 0, sizeof(report));
        if (usb_control(g_kbd_addr, 8, 0xA1, 1, 0, g_kbd_iface, report,
                        sizeof(report)) == 0) {
            for (i = 2; i < 8; i++) {
                if (report[i] && report[i] != last[i]) {
                    input_report_key(report[i], 1);
                } else if (!report[i] && last[i]) {
                    input_report_key(last[i], 0);
                }
            }
            memcpy(last, report, sizeof(last));
        }
        msleep(20);
    }
}

int usb_init(void)
{
    int port;

    if (!g_dma) {
        g_dma = (u8 *)pmm_alloc_zeroed(1);
        g_qtd = (struct qtd *)pmm_alloc_zeroed(1);
        g_ep0_qh = (struct qh *)pmm_alloc_zeroed(1);
    }
    if (!g_dma || !g_qtd || !g_ep0_qh) {
        return -1;
    }
    if (usb_controller_reset() != 0) {
        pr_warn("usb: EHCI controller did not come out of reset\n");
        return -1;
    }
    if (usb_build_lists() != 0) {
        return -1;
    }
    g_usb_ready = 1;

    for (port = 0; port < 1; port++) {
        if (usb_enumerate_port(port) == 0) {
            break;
        }
    }
    if (g_kbd_addr > 0) {
        thread_create("usbkbd", usb_kbd_thread, 0, 4096);
    }
    pr_info("usb: EHCI host ready\n");
    return 0;
}
