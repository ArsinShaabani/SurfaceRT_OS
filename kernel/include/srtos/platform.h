/* SurfaceRT_OS - board / platform glue */
#ifndef SRTOS_PLATFORM_H
#define SRTOS_PLATFORM_H

#include <srtos/types.h>
#include <srtos/bootinfo.h>

void platform_init(void);
void platform_mount_root(void);
int platform_has_display(void);
void platform_dump(void);

/* Driver entry points used by platform_init() (implemented per driver). */
void uart_init_early(const struct srt_bootinfo *bi);
void uart_console_init(void);
void fb_console_init(const struct srt_framebuffer *fb);

/* Thread entry points (services). */
void shell_thread_main(void *arg);
void disp_thread_main(void *arg);
void net_thread_main(void *arg);

#endif
