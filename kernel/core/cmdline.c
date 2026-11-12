/* SurfaceRT_OS - kernel command line parsing */
#include <srtos/kernel.h>
#include <srtos/string.h>

static const struct srt_bootinfo *g_bi;

void cmdline_set_bootinfo(const struct srt_bootinfo *bi)
{
    g_bi = bi;
}

const struct srt_bootinfo *bootinfo(void)
{
    return g_bi;
}

const char *cmdline_get(void)
{
    return g_bi ? g_bi->cmdline : "";
}

/* Returns 1 if `key` appears as a bare word on the command line. */
int cmdline_has(const char *key)
{
    const char *p = cmdline_get();
    size_t klen = strlen(key);

    while (*p) {
        const char *e = p;
        while (*e && *e != ' ') {
            e++;
        }
        if ((size_t)(e - p) == klen && !strncmp(p, key, klen)) {
            return 1;
        }
        p = *e ? e + 1 : e;
    }
    return 0;
}

/* Copies the value of key=value into buf.  Returns buf or NULL. */
const char *cmdline_value(const char *key, char *buf, size_t len)
{
    const char *p = cmdline_get();
    size_t klen = strlen(key);

    while (*p) {
        const char *e = p;
        while (*e && *e != ' ') {
            e++;
        }
        if ((size_t)(e - p) > klen && !strncmp(p, key, klen) && p[klen] == '=') {
            size_t vlen = (size_t)(e - p) - klen - 1;
            if (vlen >= len) {
                vlen = len - 1;
            }
            memcpy(buf, p + klen + 1, vlen);
            buf[vlen] = 0;
            return buf;
        }
        p = *e ? e + 1 : e;
    }
    return 0;
}
