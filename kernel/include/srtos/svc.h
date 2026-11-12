/*
 * SurfaceRT_OS - service manager and loadable service modules
 *
 * Services can be
 *   * built in  (registered with svc_register() and started at boot), or
 *   * loaded from /SERVICES/<NAME>.SVC at run time.
 *
 * A loadable service is a position independent ELF32/ARM shared object that
 * exports `srt_module_init`.  It never links against the kernel directly;
 * instead it receives a table of host function pointers (struct srt_host_api),
 * which makes the ABI explicit, versioned and safe to extend.
 */
#ifndef SRTOS_SVC_H
#define SRTOS_SVC_H

#include <stdarg.h>
#include <srtos/types.h>

#define SRT_MODULE_ABI 1
#define SRT_MODULE_MAX 8
#define SRT_MODULE_NAME_MAX 24

struct srt_frame_info {
    u32 width;
    u32 height;
    u32 stride;
    u32 bpp;
};

struct srt_host_api {
    u32 abi_version;
    u32 size;

    int (*printk)(const char *fmt, ...);
    void *(*kmalloc)(size_t size);
    void *(*kzalloc)(size_t size);
    void (*kfree)(void *ptr);
    void (*msleep)(u32 ms);
    u64 (*uptime_ms)(void);
    u32 (*random)(void);

    int (*fb_info)(struct srt_frame_info *info);
    int (*fb_blit)(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride);
    void (*fb_fill)(u32 x, u32 y, u32 w, u32 h, u32 argb);

    int (*udp_open)(u16 port, void *cb, void *arg);
    void (*udp_close)(u16 port);
    int (*udp_send)(u32 ip, u16 port, u16 src_port, const void *data, u32 len);
    u32 (*net_ip)(const char *ifname);

    int (*vfs_read)(const char *path, void *buf, u32 max, u32 *out_len);
    int (*vfs_write)(const char *path, const void *buf, u32 len);

    int (*thread_create)(const char *name, void (*fn)(void *), void *arg,
                         u32 stack);
};

struct srt_module {
    char name[SRT_MODULE_NAME_MAX];
    char path[64];
    u32 abi;
    void *image;
    u32 image_size;
    void *entry;
    int running;
    u32 version;
};

struct srt_service {
    const char *name;
    const char *version;
    int (*init)(void);
    int (*deinit)(void);
    int running;
};

void svc_manager_init(void);
void disp_sink_register(void);
int svc_register(struct srt_service *svc);
int svc_start_all(void);
int svc_start(const char *name);
void svc_list(void);
struct srt_host_api *svc_host_api(void);

int svc_load_module(const char *path);
int svc_load_all(void);
int svc_unload_module(const char *name);
int svc_module_count(void);
struct srt_module *svc_module_at(int idx);
void svc_dump(void);

/* Implemented by the display service, called by the shell. */
int disp_service_status(char *buf, u32 len);
int disp_service_start(u16 port);
int disp_service_stop(void);

#endif /* SRTOS_SVC_H */
