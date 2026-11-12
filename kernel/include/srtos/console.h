/* SurfaceRT_OS - console multiplexer */
#ifndef SRTOS_CONSOLE_H
#define SRTOS_CONSOLE_H

#include <srtos/types.h>

struct console {
    const char *name;
    void (*write)(const char *s, u32 len);
    int (*read)(char *buf, u32 len); /* optional, for the shell */
    int used;
};

void console_register(struct console *c);
void console_unregister(struct console *c);
void console_write(const char *s, u32 len);
int console_read(char *buf, u32 len);
int console_count(void);
struct console *console_at(int idx);

#endif
