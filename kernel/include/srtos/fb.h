/* SurfaceRT_OS - framebuffer and compositor */
#ifndef SRTOS_FB_H
#define SRTOS_FB_H

#include <srtos/types.h>
#include <srtos/bootinfo.h>

/* Pixel format helpers: everything internal is 0xAARRGGBB */
#define FB_RGB(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

struct fb_surface {
    u32 *pixels;      /* virtual (identity mapped) address */
    u32 phys;
    u32 width;
    u32 height;
    u32 stride;       /* pixels per line */
    u32 bpp;
    u32 format;
    u32 bytes_per_pixel;
};

void fb_init(const struct srt_framebuffer *info);
int fb_available(void);
struct fb_surface *fb_get(void);
u32 fb_width(void);
u32 fb_height(void);
u32 fb_stride(void);

void fb_clear(u32 argb);
void fb_putpixel(u32 x, u32 y, u32 argb);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb);
void fb_hline(u32 x, u32 y, u32 w, u32 argb);
int fb_blit(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride);
int fb_blit_raw(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride,
                u32 format);
void fb_flush(void);
void fb_draw_text(u32 x, u32 y, const char *text, u32 fg, u32 bg, u32 scale);

/* Console on the framebuffer */
void fb_register_console(void);
void fb_console_clear(void);
void fb_console_scroll(void);

#endif
