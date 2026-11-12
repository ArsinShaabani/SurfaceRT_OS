/*
 * SurfaceRT_OS - wireless display sink (SRDP/1)
 *
 * Glue between the UDP stack, the portable SRDP receive engine (srdp_core.c)
 * and the framebuffer.  Everything that parses untrusted input lives in the
 * core, which is also built into the host simulator and the unit tests - so the
 * code that runs on the tablet is the code that was tested.
 */
#include <srtos/svc.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/net.h>
#include <srtos/fb.h>
#include <srtos/sched.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/platform.h>
#include <srtos/srdp.h>
#include <srtos/srdp_core.h>

#define STATUS_INTERVAL_MS 1000

static struct srdp_sink g_sink;
static u32 *g_back;
static int g_running;
static u16 g_ctrl_port, g_video_port;
static u32 g_host_ip;
static u16 g_host_port;
static u32 g_fps_x100, g_frames_in_window;
static u64 g_window_start;
static u32 g_keyframes;
static u32 g_last_rx_ms;
static char g_host[20] = "-";

static void *sink_alloc(u32 size)
{
    return kmalloc(size);
}

static void sink_free(void *p)
{
    kfree(p);
}

static void status_line(const char *fmt, ...)
{
    char buf[180];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fb_fill_rect(0, 0, fb_width(), 18, 0x00101820);
    fb_draw_text(4, 5, buf, 0x0060E0FF, 0, 1);
}

static void idle_screen(void)
{
    char ipbuf[16];
    struct net_device *nd = net_get_index(0);
    u32 accent = 0x000F2A45;

    fb_clear(0x000C1018);
    fb_fill_rect(0, 0, fb_width(), 4, 0x003070C0);
    fb_draw_text(40, 50, "SurfaceRT_OS", 0x0060E0FF, 0, 4);
    fb_draw_text(40, 100, "wireless display sink  -  SRDP/1", 0x00C8D8E8, 0, 2);
    fb_fill_rect(40, 130, fb_width() - 80, 2, accent);

    {
        char line[96];
        snprintf(line, sizeof(line), "panel %ux%u   ports %u control / %u video",
                 fb_width(), fb_height(), g_ctrl_port, g_video_port);
        fb_draw_text(40, 150, line, 0x00A0B8C8, 0, 2);
    }
    if (nd) {
        snprintf(ipbuf, sizeof(ipbuf), "%s", ip_to_str(nd->ip, ipbuf));
        fb_draw_text(40, 185, "ip address", 0x00A0B8C8, 0, 2);
        fb_draw_text(200, 185, ipbuf, 0x00FFFFFF, 0, 2);
        fb_draw_text(40, 215, "interface ", 0x00A0B8C8, 0, 2);
        fb_draw_text(200, 215, nd->name, 0x00FFFFFF, 0, 2);
    } else {
        fb_draw_text(40, 185, "no network interface yet", 0x00FFC040, 0, 2);
    }
    fb_draw_text(40, 270, "host:  python3 host/srdisplay/sender.py --host "
                          "<tablet-ip>", 0x0080A0B0, 0, 1);
    fb_draw_text(40, 288, "shell: disp status | disp test | disp stop",
                 0x0080A0B0, 0, 1);
}

static void blit_dirty(void)
{
    u32 i;
    struct fb_surface *fb = fb_get();

    for (i = 0; i < g_sink.dirty_count; i++) {
        struct srdp_dirty *d = &g_sink.dirty[i];
        fb_blit_raw(d->x, d->y, d->w, d->h,
                    g_back + (uptr)d->y * g_sink.width + d->x, g_sink.width,
                    fb->format == SRT_FB_RGBX ? SRT_FB_RGBX : SRT_FB_BGRX);
    }
    g_sink.dirty_count = 0;
}

static void send_packet(u16 port, u8 type, const u8 *data, u32 len)
{
    if (g_host_ip) {
        udp_send(g_host_ip, port ? port : SRDP_CTRL_PORT, g_video_port, data, len);
    }
}

static void handle_event(struct srdp_event *ev, u32 src_ip, u16 src_port)
{
    u8 out[128];
    int n;

    switch (ev->kind) {
    case SRDP_EV_HELLO:
        g_host_ip = src_ip;
        g_host_port = src_port;
        ip_to_str(src_ip, g_host);
        n = srdp_build_hello_ack(&g_sink, out, sizeof(out),
                                 fb_get()->format == SRT_FB_RGBX
                                     ? SRDP_FMT_RGBA : SRDP_FMT_BGRA,
                                 SRDP_MAX_DATAGRAM);
        if (n > 0) {
            send_packet(src_port, SRDP_T_HELLO_ACK, out, (u32)n);
        }
        pr_info("disp: session %d accepted from %s:%u\n", g_sink.session, g_host,
                src_port);
        status_line("streaming from %s", g_host);
        break;
    case SRDP_EV_DIRTY:
        blit_dirty();
        break;
    case SRDP_EV_FRAME: {
        u64 now = clock_uptime_ms();
        g_frames_in_window++;
        g_last_rx_ms = (u32)now;
        if (now - g_window_start >= STATUS_INTERVAL_MS) {
            g_fps_x100 = (u32)((g_frames_in_window * 100000ull) /
                               (now - g_window_start));
            g_frames_in_window = 0;
            g_window_start = now;
            status_line("live  %s  %u.%02u fps  drop %u", g_host,
                        g_fps_x100 / 100, g_fps_x100 % 100,
                        g_sink.dropped_rects);
        }
        break;
    }
    case SRDP_EV_PING:
        n = srdp_build_pong(&g_sink, ev->cookie, out, sizeof(out));
        if (n > 0) {
            send_packet(src_port, SRDP_T_PONG, out, (u32)n);
        }
        break;
    case SRDP_EV_KEYFRAME_REQ:
        n = srdp_build_keyframe_req((u32)g_sink.session, ev->reason, out,
                                    sizeof(out));
        if (n > 0) {
            g_keyframes++;
            send_packet(g_host_port ? g_host_port : SRDP_CTRL_PORT,
                        SRDP_T_KEYFRAME_REQ, out, (u32)n);
        }
        break;
    case SRDP_EV_BYE:
        pr_info("disp: host closed the session\n");
        g_host_ip = 0;
        idle_screen();
        break;
    default:
        break;
    }
}

static void video_rx(u32 src_ip, u16 src_port, const u8 *data, u32 len, void *arg)
{
    struct srdp_event ev;
    (void)arg;
    if (srdp_sink_feed(&g_sink, data, len, &ev) == 0 && ev.kind != SRDP_EV_NONE) {
        handle_event(&ev, src_ip, src_port);
    }
}

static void ctrl_rx(u32 src_ip, u16 src_port, const u8 *data, u32 len, void *arg)
{
    struct srdp_event ev;
    (void)arg;
    if (srdp_sink_feed(&g_sink, data, len, &ev) == 0 && ev.kind != SRDP_EV_NONE) {
        handle_event(&ev, src_ip, src_port);
    } else if (len >= sizeof(struct srdp_hdr)) {
        /* CONFIG and other control-only messages are accepted silently */
        const struct srdp_hdr *h = (const struct srdp_hdr *)data;
        if (h->type == SRDP_T_CONFIG) {
            pr_debug("disp: configuration updated by the host\n");
        }
    }
}

/* ------------------------------------------------------------- service API --- */
int disp_service_start(u16 port)
{
    struct fb_surface *fb = fb_get();

    if (!fb || !fb->width) {
        return -1;
    }
    if (g_running) {
        return 0;
    }
    if (!g_back) {
        u32 pixels = fb->stride * fb->height;
        u32 pages = (pixels * 4u + PAGE_SIZE - 1) / PAGE_SIZE;
        g_back = (u32 *)pmm_alloc_pages(pages);
        if (!g_back) {
            pr_err("disp: cannot allocate a %u KiB back buffer\n",
                   (pixels * 4u) / 1024u);
            return -1;
        }
        memset(g_back, 0, pages * PAGE_SIZE);
    }
    g_ctrl_port = port ? port : SRDP_CTRL_PORT;
    g_video_port = (u16)(g_ctrl_port + 1);

    if (srdp_sink_init(&g_sink, fb->stride, fb->height, sink_alloc,
                       sink_free) != 0) {
        return -1;
    }
    g_sink.back = g_back;
    g_sink.width = fb->stride;
    g_sink.height = fb->height;

    if (udp_socket_open(g_ctrl_port, ctrl_rx, 0) != 0 ||
        udp_socket_open(g_video_port, video_rx, 0) != 0) {
        pr_err("disp: cannot bind udp %u/%u\n", g_ctrl_port, g_video_port);
        return -1;
    }
    g_running = 1;
    g_window_start = clock_uptime_ms();
    idle_screen();
    pr_info("disp: sink listening on udp %u (control) and %u (video)\n",
            g_ctrl_port, g_video_port);
    return 0;
}

int disp_service_stop(void)
{
    if (!g_running) {
        return 0;
    }
    udp_socket_close(g_ctrl_port);
    udp_socket_close(g_video_port);
    srdp_sink_reset(&g_sink);
    g_running = 0;
    g_host_ip = 0;
    return 0;
}

int disp_service_status(char *buf, u32 len)
{
    return snprintf(buf, len,
                    "%s, session %d, host %s, %u frames, %u.%02u fps, "
                    "%u rects dropped, %u keyframes",
                    g_running ? "running" : "stopped", g_sink.session, g_host,
                    g_sink.frames, g_fps_x100 / 100, g_fps_x100 % 100,
                    g_sink.dropped_rects, g_keyframes);
}

static int disp_service_init(void)
{
    return disp_service_start(g_ctrl_port ? g_ctrl_port : SRDP_CTRL_PORT);
}

void disp_thread_main(void *arg)
{
    (void)arg;
    if (disp_service_init() != 0) {
        pr_err("disp: service failed to start\n");
        thread_exit();
    }
    for (;;) {
        msleep(1000);
        if (g_running && g_host_ip &&
            (u32)clock_uptime_ms() - g_last_rx_ms > 5000) {
            struct srdp_event ev;
            pr_warn("disp: no video for 5 s, asking for a refresh\n");
            ev.kind = SRDP_EV_KEYFRAME_REQ;
            ev.reason = 1;
            handle_event(&ev, g_host_ip, g_host_port);
            g_last_rx_ms = (u32)clock_uptime_ms();
        }
    }
}

static int disp_service_deinit(void)
{
    return disp_service_stop();
}

static struct srt_service disp_sink_service = {
    .name = "dispsink",
    .version = "1.0",
    .init = disp_service_init,
    .deinit = disp_service_deinit,
};

void disp_sink_register(void)
{
    svc_register(&disp_sink_service);
}
