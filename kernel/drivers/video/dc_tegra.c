/*
 * SurfaceRT_OS - Tegra 3 display controller (DC) driver
 *
 * Takes over the display from the firmware so the kernel can program the
 * panel timing, the window base address and the colour depth itself.  The
 * framebuffer address comes from the loader, so switching to our own window
 * setup keeps the same visible content (no flicker on hand-off).
 *
 * Tegra 3 details:
 *   DC0 (display A)  0x54200000  - internal eDP/DSI panel on the Surface RT
 *   DC1 (display B)  0x54240000  - HDMI (Surface RT has a micro HDMI port)
 *   DSI              0x54300000
 *
 * The sequence below follows the Tegra 2/3 display programming guide: stop the
 * DC, set up the window, program the timing/panel, then start with a frame
 * end interrupt so the base address can be flipped during vblank.
 */
#include <srtos/kernel.h>
#include <srtos/fb.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include <srtos/irq.h>
#include <srtos/clock.h>
#include "tegra30.h"

#define DC0 T30_DC_BASE(0)
#define DC_WIN_A 0
#define DC_WINBUF_START_ADDR 0x800
#define DC_WINBUF_ADDR_H_OFFSET 0x806
#define DC_WINBUF_ADDR_V_OFFSET 0x807
#define DC_WINBUF_END_ADDR 0x802
#define DC_WINBUF_STRIDE 0x808

static int g_dc_ready;

static inline void dc_wr(u32 off, u32 v)
{
    writel(v, DC0 + off);
}

static inline u32 dc_rd(u32 off)
{
    return readl(DC0 + off);
}

/* Host1x sync point wait: the DC raises frame-end syncpoints 26/27. */
#define HOST1X_BASE 0x50000000u
#define HOST1X_SYNC_REG(n) (HOST1X_BASE + 0x3000 + ((n) * 0x40))

static int dc_wait_vblank(u32 timeout_ms)
{
    u32 start = (u32)clock_uptime_ms();
    u32 before = readl(HOST1X_SYNC_REG(26));
    while (readl(HOST1X_SYNC_REG(26)) == before) {
        if ((u32)clock_uptime_ms() - start > timeout_ms) {
            return -1;
        }
    }
    return 0;
}

static int dc_program_window(struct fb_surface *fb)
{
    u32 fmt;

    /* stop the display while we reprogram it */
    dc_wr(DC_CMD_DISPLAY_COMMAND, 0);
    dc_wr(DC_CMD_STATE_ACCESS, 0);
    (void)dc_wait_vblank(50);

    switch (fb->format) {
    case SRT_FB_BGRX:
        fmt = 0x0D; /* T_A8R8G8B8 on Tegra 3 */
        break;
    case SRT_FB_RGBX:
    default:
        fmt = 0x0D;
        break;
    }

    dc_wr(DC_CMD_DISPLAY_WINDOW_HEADER, BIT(DC_WIN_A));
    dc_wr(DC_WIN_WIN_OPTIONS, 0);
    dc_wr(DC_WIN_COLOR_DEPTH, fmt);
    dc_wr(DC_WIN_POSITION, 0);
    dc_wr(DC_WIN_SIZE, ((fb->height - 1) << 16) | (fb->width - 1));
    dc_wr(DC_WIN_STRIDE, fb->stride * 4);
    dc_wr(DC_WIN_BUF_ADDR, fb->phys);
    dc_wr(DC_WINBUF_START_ADDR, fb->phys);
    dc_wr(DC_WINBUF_END_ADDR, fb->phys + fb->stride * fb->height * 4 - 1);
    dc_wr(DC_WINBUF_ADDR_H_OFFSET, 0);
    dc_wr(DC_WINBUF_ADDR_V_OFFSET, 0);
    dc_wr(DC_WINBUF_STRIDE, fb->stride * 4);
    dc_wr(DC_WIN_WIN_OPTIONS, DC_WIN_WIN_OPTIONS_WIN_ENABLE);

    /* colour keying off, no blending */
    dc_wr(DC_DISP_BLEND_BACKGROUND_COLOR, 0);
    dc_wr(DC_DISP_COLOR_KEY0_LOW, 0);
    dc_wr(DC_DISP_COLOR_KEY0_HIGH, 0);
    return 0;
}

int dc_init(void)
{
    struct fb_surface *fb = fb_get();

    if (!fb || !fb->phys) {
        return -1;
    }
    if (dc_rd(DC_CMD_DISPLAY_COMMAND) == 0xFFFFFFFFu) {
        pr_warn("dc: no response from DC0 at 0x%08x\n", DC0);
        return -1;
    }

    /* Program the window and re-enable the display.  The panel timing itself
     * was set up by the firmware; keeping it means the hand-off is seamless and
     * we do not have to re-run the panel's power-on sequence. */
    dc_program_window(fb);
    dc_wr(DC_CMD_STATE_CONTROL, 0x01);
    (void)dc_wait_vblank(50);
    dc_wr(DC_CMD_DISPLAY_COMMAND,
          0x02 | DC_CMD_DISPLAY_COMMAND_OPTION0); /* continuous mode */

    g_dc_ready = 1;
    pr_info("dc: window A %ux%u, base 0x%08x, stride %u\n", fb->width,
            fb->height, fb->phys, fb->stride * 4);
    return 0;
}

/* Flip the window base address during vblank (used by double buffered modes). */
int dc_set_framebuffer(u32 phys)
{
    if (!g_dc_ready) {
        return -1;
    }
    if (dc_wait_vblank(30) != 0) {
        return -1;
    }
    dc_wr(DC_CMD_STATE_ACCESS, 0x02); /* interlace/immediate window access */
    dc_wr(DC_WINBUF_START_ADDR, phys);
    dc_wr(DC_WIN_BUF_ADDR, phys);
    dc_wr(DC_CMD_STATE_CONTROL, 0x01);
    return 0;
}

void dc_dump(void)
{
    printk("dc: display command 0x%08x, window options 0x%08x\n",
           dc_rd(DC_CMD_DISPLAY_COMMAND), dc_rd(DC_WIN_WIN_OPTIONS));
}
