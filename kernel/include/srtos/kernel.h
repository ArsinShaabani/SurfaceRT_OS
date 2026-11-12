/* SurfaceRT_OS - kernel core API */
#ifndef SRTOS_KERNEL_H
#define SRTOS_KERNEL_H

#include <stdarg.h>
#include <srtos/types.h>
#include <srtos/bootinfo.h>

#define SRTOS_NAME "SurfaceRT_OS"
#define SRTOS_VERSION "1.0.0"
#define SRTOS_CODENAME "Tegra Sink"

enum log_level {
    LOG_EMERG = 0,
    LOG_ALERT,
    LOG_CRIT,
    LOG_ERR,
    LOG_WARN,
    LOG_NOTICE,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE,
};

int printk(const char *fmt, ...) __printf(1, 2);
void vprintk(const char *fmt, va_list ap);
void log_set_level(int level);
int log_get_level(void);
#define pr_err(fmt, ...) printk("<3>" fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk("<4>" fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) printk("<6>" fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) printk("<7>" fmt, ##__VA_ARGS__)

void panic(const char *fmt, ...) __printf(1, 2) __noreturn;
void arm_exception_panic(void) __noreturn;

const struct srt_bootinfo *bootinfo(void);
const char *cmdline_get(void);
int cmdline_has(const char *key);
const char *cmdline_value(const char *key, char *buf, size_t len);

void reboot_now(void) __noreturn;
void poweroff_now(void) __noreturn;

/* Driver init ordering (see .init_array support in core/driver.c). */
#define core_init(fn) \
    static void (*__init_##fn)(void) __section(".init_array") __unused = (fn)

#endif
