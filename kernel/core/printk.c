/* SurfaceRT_OS - printk and console multiplexing */
#include <srtos/kernel.h>
#include <srtos/console.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/irq.h>

#define MAX_CONSOLES 8

static struct console *consoles[MAX_CONSOLES];
static int nr_consoles;
static int log_level = LOG_INFO;
static spinlock_t log_lock = SPINLOCK_INIT;

void console_register(struct console *c)
{
    int i;
    u32 flags;
    if (!c || nr_consoles >= MAX_CONSOLES) {
        return;
    }
    flags = spin_lock_irqsave(&log_lock);
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (consoles[i] == c) {
            spin_unlock_irqrestore(&log_lock, flags);
            return;
        }
    }
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (!consoles[i]) {
            consoles[i] = c;
            nr_consoles++;
            break;
        }
    }
    spin_unlock_irqrestore(&log_lock, flags);
}

void console_unregister(struct console *c)
{
    int i;
    u32 flags = spin_lock_irqsave(&log_lock);
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (consoles[i] == c) {
            consoles[i] = 0;
            nr_consoles--;
        }
    }
    spin_unlock_irqrestore(&log_lock, flags);
}

void console_write(const char *s, u32 len)
{
    int i;
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (consoles[i] && consoles[i]->write) {
            consoles[i]->write(s, len);
        }
    }
}

int console_read(char *buf, u32 len)
{
    int i, n;
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (consoles[i] && consoles[i]->read) {
            n = consoles[i]->read(buf, len);
            if (n > 0) {
                return n;
            }
        }
    }
    return 0;
}

int console_count(void)
{
    return nr_consoles;
}

struct console *console_at(int idx)
{
    int i, n = 0;
    for (i = 0; i < MAX_CONSOLES; i++) {
        if (consoles[i]) {
            if (n++ == idx) {
                return consoles[i];
            }
        }
    }
    return 0;
}

void log_set_level(int level)
{
    log_level = CLAMP(level, LOG_EMERG, LOG_TRACE);
}

int log_get_level(void)
{
    return log_level;
}

static void emit(const char *s, u32 len)
{
    if (!len) {
        return;
    }
    console_write(s, len);
}

void vprintk(const char *fmt, va_list ap)
{
    char buf[512];
    char out[600];
    int level = LOG_INFO;
    int len;
    u64 us;
    u32 flags;

    if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '9' && fmt[2] == '>') {
        level = fmt[1] - '0';
        fmt += 3;
    }
    if (level > log_level) {
        return;
    }
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len < 0) {
        return;
    }
    us = clock_uptime_us();
    len = snprintf(out, sizeof(out), "[%5llu.%06llu] %s", us / 1000000ull,
                   us % 1000000ull, buf);

    flags = spin_lock_irqsave(&log_lock);
    emit(out, (u32)len);
    spin_unlock_irqrestore(&log_lock, flags);
}

int printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt, ap);
    va_end(ap);
    return 0;
}
