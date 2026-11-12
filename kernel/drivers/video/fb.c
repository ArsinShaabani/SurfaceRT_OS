/*
 * SurfaceRT_OS - framebuffer core, 5x7 text console and blitter
 *
 * The framebuffer handed over by the firmware (or programmed by the DC driver)
 * is memory mapped 1:1, so all drawing is a plain memory write.  Everything is
 * drawn in ARGB and converted to the panel's native order in fb_pack().
 */
#include <srtos/fb.h>
#include <srtos/kernel.h>
#include <srtos/console.h>
#include <srtos/string.h>
#include <srtos/arch.h>

#define FONT_W 5
#define FONT_H 7

struct fb_surface g_fb;
static int g_ready;

/* ---------------------------------------------------------------- 5x7 font --- */
/* One byte per column, bit 0 = top row.  Lower case maps to upper case. */
static const u8 font5x7[][FONT_W] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* ! */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* " */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* # */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* $ */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* % */
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, /* & */
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, /* ' */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* ( */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* ) */
    { 0x08, 0x2A, 0x1C, 0x2A, 0x08 }, /* * */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* + */
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, /* , */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* . */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* / */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* 2 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* 3 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* 6 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* 9 */
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* : */
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* ; */
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, /* < */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* = */
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, /* > */
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, /* ? */
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, /* @ */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, /* A */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* B */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* C */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, /* D */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* E */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, /* F */
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, /* G */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* H */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* I */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* J */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* K */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* L */
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, /* M */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* N */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* O */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* P */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* Q */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* R */
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, /* S */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* T */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* U */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* V */
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, /* W */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* X */
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, /* Y */
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, /* Z */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, /* [ */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* backslash */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, /* ] */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* ^ */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* _ */
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, /* ` */
    { 0x20, 0x54, 0x54, 0x54, 0x78 }, /* a */
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, /* b */
    { 0x38, 0x44, 0x44, 0x44, 0x20 }, /* c */
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, /* d */
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, /* e */
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, /* f */
    { 0x0C, 0x52, 0x52, 0x52, 0x3E }, /* g */
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, /* h */
    { 0x00, 0x44, 0x7D, 0x40, 0x00 }, /* i */
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, /* j */
    { 0x7F, 0x10, 0x28, 0x44, 0x00 }, /* k */
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, /* l */
    { 0x7C, 0x04, 0x18, 0x04, 0x78 }, /* m */
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, /* n */
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, /* o */
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, /* p */
    { 0x08, 0x14, 0x14, 0x18, 0x7C }, /* q */
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, /* r */
    { 0x48, 0x54, 0x54, 0x54, 0x20 }, /* s */
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, /* t */
    { 0x3C, 0x40, 0x40, 0x20, 0x7C }, /* u */
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, /* v */
    { 0x3C, 0x40, 0x30, 0x40, 0x3C }, /* w */
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, /* x */
    { 0x0C, 0x50, 0x50, 0x50, 0x3C }, /* y */
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, /* z */
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, /* { */
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, /* | */
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, /* } */
    { 0x08, 0x04, 0x08, 0x10, 0x08 }, /* ~ */
};

static inline u32 fb_pack(u32 argb)
{
    if (g_fb.format == SRT_FB_BGRX) {
        return (argb & 0xFF00FF00u) | ((argb & 0x00FF0000u) >> 16) |
               ((argb & 0x000000FFu) << 16);
    }
    return argb;
}

void fb_init(const struct srt_framebuffer *info)
{
    if (!info || !info->phys || !info->width || !info->height) {
        g_ready = 0;
        return;
    }
    memset(&g_fb, 0, sizeof(g_fb));
    g_fb.phys = (u32)info->phys;
    g_fb.pixels = (u32 *)(uptr)info->phys;
    g_fb.width = info->width;
    g_fb.height = info->height;
    g_fb.stride = info->stride ? info->stride : info->width;
    g_fb.bpp = info->bpp ? info->bpp : 32;
    g_fb.format = info->format;
    g_fb.bytes_per_pixel = g_fb.bpp / 8;
    g_ready = 1;
    pr_info("fb: %ux%u %u bpp, stride %u, %s order\n", g_fb.width, g_fb.height,
            g_fb.bpp, g_fb.stride,
            g_fb.format == SRT_FB_BGRX ? "BGRX" : "RGBX");
}

int fb_available(void) { return g_ready; }
struct fb_surface *fb_get(void) { return &g_fb; }
u32 fb_width(void) { return g_fb.width; }
u32 fb_height(void) { return g_fb.height; }
u32 fb_stride(void) { return g_fb.stride; }

void fb_clear(u32 argb)
{
    fb_fill_rect(0, 0, g_fb.width, g_fb.height, argb);
}

void fb_putpixel(u32 x, u32 y, u32 argb)
{
    if (!g_ready || x >= g_fb.width || y >= g_fb.height) {
        return;
    }
    g_fb.pixels[(uptr)y * g_fb.stride + x] = fb_pack(argb);
}

void fb_hline(u32 x, u32 y, u32 w, u32 argb)
{
    u32 px, *row;
    if (!g_ready || y >= g_fb.height) {
        return;
    }
    if (x >= g_fb.width) {
        return;
    }
    if (x + w > g_fb.width) {
        w = g_fb.width - x;
    }
    px = fb_pack(argb);
    row = g_fb.pixels + (uptr)y * g_fb.stride + x;
    while (w--) {
        *row++ = px;
    }
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb)
{
    u32 row;
    for (row = 0; row < h && y + row < g_fb.height; row++) {
        fb_hline(x, y + row, w, argb);
    }
}

int fb_blit(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride)
{
    return fb_blit_raw(x, y, w, h, pixels, stride, SRT_FB_BGRX);
}

int fb_blit_raw(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride,
                u32 format)
{
    const u32 *src = (const u32 *)pixels;
    u32 row, col;

    if (!g_ready || !pixels) {
        return -1;
    }
    if (x >= g_fb.width || y >= g_fb.height) {
        return 0;
    }
    if (x + w > g_fb.width) {
        w = g_fb.width - x;
    }
    if (y + h > g_fb.height) {
        h = g_fb.height - y;
    }
    for (row = 0; row < h; row++) {
        u32 *dst = g_fb.pixels + (uptr)(y + row) * g_fb.stride + x;
        const u32 *s = src + (uptr)row * stride;
        if (format == g_fb.format) {
            memcpy(dst, s, w * 4);
        } else {
            for (col = 0; col < w; col++) {
                dst[col] = fb_pack(s[col]);
            }
        }
    }
    return 0;
}

void fb_flush(void)
{
    /* The Surface RT panel is scanned out of normal memory: a DSB is all the
     * synchronisation a non-cached write needs. */
    __asm__ __volatile__("dsb" ::: "memory");
}

static int glyph_index(int c)
{
    if (c < 32 || c > 126) {
        c = '?';
    }
    return c - 32;
}

void fb_draw_text(u32 x, u32 y, const char *text, u32 fg, u32 bg, u32 scale)
{
    u32 cx = x, cy = y, col, line;
    if (!scale) {
        scale = 1;
    }
    while (*text) {
        int c = (unsigned char)*text++;
        const u8 *g;
        if (c == '\n') {
            cx = x;
            cy += (FONT_H + 2) * scale;
            continue;
        }
        g = font5x7[glyph_index(c)];
        for (col = 0; col < FONT_W; col++) {
            u8 bits = g[col];
            for (line = 0; line < FONT_H; line++) {
                u32 px = cx + col * scale;
                u32 py = cy + line * scale;
                if (bg != 0) {
                    fb_fill_rect(px, py, scale, scale, bg);
                }
                if (bits & BIT(line)) {
                    fb_fill_rect(px, py, scale, scale, fg);
                }
            }
        }
        cx += (FONT_W + 1) * scale;
    }
}

/* ---------------------------------------------------------------- console --- */
#define CON_SCALE 2
#define CHAR_W ((FONT_W + 1) * CON_SCALE)
#define CHAR_H ((FONT_H + 2) * CON_SCALE)

static u32 g_col, g_row, g_cols, g_rows;
static u32 g_fg = 0x00E8F0FFu, g_bg = 0x00101820u;
static struct console g_fb_console;

static void fb_console_putc(char c)
{
    if (!g_ready) {
        return;
    }
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\t') {
        g_col = (g_col + 4) & ~3u;
    } else {
        char s[2];
        s[0] = c;
        s[1] = 0;
        fb_draw_text(g_col * CHAR_W, g_row * CHAR_H, s, g_fg, g_bg, CON_SCALE);
        g_col++;
    }
    if (g_col >= g_cols) {
        g_col = 0;
        g_row++;
    }
    if (g_row >= g_rows) {
        fb_console_scroll();
        g_row = g_rows - 1;
    }
}

void fb_console_scroll(void)
{
    u32 y;
    if (!g_ready) {
        return;
    }
    /* move everything up by one text row (memmove within the framebuffer) */
    for (y = 0; y + CHAR_H < g_rows * CHAR_H; y++) {
        memcpy(g_fb.pixels + (uptr)y * g_fb.stride,
               g_fb.pixels + (uptr)(y + CHAR_H) * g_fb.stride,
               g_fb.width * 4);
    }
    fb_fill_rect(0, (g_rows - 1) * CHAR_H, g_fb.width, CHAR_H, g_bg);
}

void fb_console_clear(void)
{
    if (!g_ready) {
        return;
    }
    fb_clear(g_bg);
    g_col = g_row = 0;
}

static void fb_console_write(const char *s, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        fb_console_putc(s[i]);
    }
}

void fb_register_console(void)
{
    if (!g_ready) {
        return;
    }
    g_cols = g_fb.width / CHAR_W;
    g_rows = g_fb.height / CHAR_H;
    g_col = g_row = 0;
    fb_console_clear();
    g_fb_console.name = "fb0";
    g_fb_console.write = fb_console_write;
    g_fb_console.read = 0;
    console_register(&g_fb_console);
    pr_info("fb0: text console %ux%u characters (%ux%u px cells)\n", g_cols,
            g_rows, CHAR_W, CHAR_H);
}
