/*
 * SurfaceRT_OS example service module
 *
 * Demonstrates the whole extension model: a position independent ARM binary
 * that receives a versioned host API table, can print, draw on the framebuffer
 * and start its own kernel thread - without ever linking against the kernel.
 *
 * Build with `make services`, install as /SERVICES/HELLO.SVC on the ESP and load
 * at run time with:  svc load /services/hello.svc
 */
#include <srtos/svc.h>

static const struct srt_host_api *g_api;
static u32 g_ticks;

static void hello_thread(void *arg)
{
    (void)arg;
    for (;;) {
        struct srt_frame_info info;
        g_ticks++;
        if (g_api->fb_info(&info) == 0) {
            char line[80];
            /* the module has no libc: format by hand */
            unsigned n = 0;
            const char *prefix = "hello.svc alive, tick ";
            while (*prefix && n < sizeof(line) - 12) {
                line[n++] = *prefix++;
            }
            {
                char num[12];
                int k = 0;
                u32 v = g_ticks;
                if (!v) {
                    num[k++] = '0';
                }
                while (v) {
                    num[k++] = (char)('0' + (v % 10));
                    v /= 10;
                }
                while (k) {
                    line[n++] = num[--k];
                }
            }
            line[n] = 0;
            g_api->fb_fill(8, info.height - 24, 260, 16, 0x00101820);
            g_api->printk("hello: %s (screen %ux%u)\n", line, info.width,
                          info.height);
        }
        g_api->msleep(10000);
    }
}

int srt_module_init(const struct srt_host_api *api, u32 abi)
{
    if (!api || abi != SRT_MODULE_ABI || api->size < sizeof(*api)) {
        return -1;
    }
    g_api = api;
    api->printk("hello: loaded, host API %u bytes, abi %u\n", api->size, abi);
    if (api->thread_create("hello", hello_thread, 0, 4096) != 0) {
        api->printk("hello: could not start a thread\n");
        return -1;
    }
    return 0;
}
