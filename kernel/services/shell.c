/*
 * SurfaceRT_OS - serial shell
 *
 * A small, extensible command interpreter.  Adding a command means adding one
 * entry to the table below - nothing else in the system has to change.
 */
#include <srtos/kernel.h>
#include <srtos/console.h>
#include <srtos/sched.h>
#include <srtos/mm.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/vfs.h>
#include <srtos/fs.h>
#include <srtos/block.h>
#include <srtos/net.h>
#include <srtos/fb.h>
#include <srtos/svc.h>
#include <srtos/gpio.h>
#include <srtos/i2c.h>
#include <srtos/pmic.h>
#include <srtos/arch.h>
#include <srtos/platform.h>
#include <srtos/driver.h>
#include <srtos/srdp.h>
#include <srtos/soc.h>

#define LINE_MAX 128
#define ARGS_MAX 8

typedef int (*cmd_fn)(int argc, char **argv);

struct command {
    const char *name;
    const char *help;
    cmd_fn fn;
};

int installer_run(const char *source, int partition);
void shell_feed_char(char c);
int shell_feed_read(char *c);

static int cmd_help(int argc, char **argv)
{
    const struct command *cmds = 0;
    extern const struct command __shell_commands_start[];
    (void)argc;
    (void)argv;
    cmds = __shell_commands_start;
    printk("commands:\n");
    for (; cmds->name; cmds++) {
        printk("  %-10s %s\n", cmds->name, cmds->help);
    }
    return 0;
}

static int cmd_uname(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printk("%s %s (%s)\n", SRTOS_NAME, SRTOS_VERSION, SRTOS_CODENAME);
    printk("board: %s, %s, %d x %s (1 core online)\n", SRTOS_BOARD_NAME,
           SRTOS_SOC_NAME, PLATFORM_CPU_CORES, PLATFORM_CPU_NAME);
    printk("build: %s %s\n", __DATE__, __TIME__);
    return 0;
}

static int cmd_mem(int argc, char **argv)
{
    size_t used, peak, total;
    (void)argc;
    (void)argv;
    pmm_dump_stats();
    heap_stats(&used, &peak, &total);
    printk("heap: %u used, %u peak, %u total bytes\n", (u32)used, (u32)peak,
           (u32)total);
    return 0;
}

static int cmd_ps(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sched_dump();
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    vfs_list(argc > 1 ? argv[1] : "/");
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    char buf[256];
    u32 len = 0;
    int r;
    if (argc < 2) {
        printk("usage: cat <file>\n");
        return -1;
    }
    r = vfs_read_file(argv[1], buf, sizeof(buf) - 1, &len);
    if (r != 0) {
        printk("cat: %s: error %d\n", argv[1], r);
        return -1;
    }
    buf[len] = 0;
    printk("%s", buf);
    return 0;
}

static int cmd_net(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "ip")) {
        struct net_device *nd = net_get_index(0);
        if (!nd || argc < 3) {
            printk("usage: net ip <a.b.c.d> [mask] [gw]\n");
            return -1;
        }
        net_set_ip(nd, ip_from_str(argv[2]),
                   argc > 3 ? ip_from_str(argv[3]) : NET_IPV4_ADDR(255, 255, 255, 0),
                   argc > 4 ? ip_from_str(argv[4]) : 0);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "dhcp")) {
        dhcp_start(net_get_index(0));
        return 0;
    }
    net_dump();
    return 0;
}

static int cmd_ping(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printk("ping: icmp echo is handled by the stack; use the host to test\n");
    return 0;
}

static int cmd_disp(int argc, char **argv)
{
    char buf[256];
    if (argc >= 2 && !strcmp(argv[1], "start")) {
        return disp_service_start(argc > 2 ? (u16)strtoul(argv[2], 0, 10)
                                          : SRDP_CTRL_PORT);
    }
    if (argc >= 2 && !strcmp(argv[1], "stop")) {
        return disp_service_stop();
    }
    if (argc >= 2 && !strcmp(argv[1], "test")) {
        /* paint a test pattern so the panel can be verified without a host */
        fb_clear(0x00203040);
        fb_draw_text(40, 40, "SRTOS display test", 0x00FFFFFF, 0, 3);
        for (int i = 0; i < 16; i++) {
            fb_fill_rect(40 + i * 40, 100, 36, 60, FB_RGB(i * 16, 255 - i * 16, 128));
        }
        return 0;
    }
    disp_service_status(buf, sizeof(buf));
    printk("disp: %s\n", buf);
    printk("usage: disp [start|stop|status|test] [port]\n");
    return 0;
}

static int cmd_svc(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "load-all")) {
        int n = svc_load_all();
        printk("svc: %d module(s) loaded\n", n);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "load")) {
        return svc_load_module(argv[2]);
    }
    if (argc >= 3 && !strcmp(argv[1], "unload")) {
        return svc_unload_module(argv[2]);
    }
    if (argc >= 2 && !strcmp(argv[1], "start")) {
        return svc_start(argv[2]);
    }
    svc_list();
    return 0;
}

static int cmd_blk(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    block_dump();
    part_dump();
    return 0;
}

static int cmd_gpio(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "get")) {
        printk("gpio %d = %d\n", atoi(argv[2]), gpio_get_value(atoi(argv[2])));
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "set")) {
        gpio_direction_output(atoi(argv[2]), atoi(argv[3]));
        return 0;
    }
    gpio_dump();
    return 0;
}

static int cmd_i2c(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "read")) {
        u8 v = 0;
        int r = i2c_read_reg(atoi(argv[2]), (u8)strtoul(argv[3], 0, 0),
                             (u8)strtoul(argv[4], 0, 0), &v);
        printk("i2c: %s value 0x%02x\n", r == 0 ? "ok" : "error", v);
        return 0;
    }
    i2c_dump();
    return 0;
}

static int cmd_fb(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printk("fb: %ux%u %s, %u bpp, stride %u\n", fb_width(), fb_height(),
           fb_get()->format == SRT_FB_BGRX ? "BGRX" : "RGBX", fb_get()->bpp,
           fb_stride());
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    if (argc >= 2) {
        log_set_level(atoi(argv[1]));
    }
    printk("loglevel: %d (0=emerg .. 8=trace)\n", log_get_level());
    return 0;
}

static int cmd_drivers(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    driver_dump();
    mmu_dump();
    return 0;
}

static int cmd_boot(int argc, char **argv)
{
    char buf[16];
    u32 len = 0;

    if (argc < 2) {
        char current[16] = "srtos";
        if (vfs_read_file("/srtos/default", current, sizeof(current) - 1,
                          &len) == 0) {
            current[len] = 0;
        }
        printk("next boot: %s   (usage: boot srtos|windows)\n", current);
        return 0;
    }
    memset(buf, 0, sizeof(buf));
    if (!strcasecmp(argv[1], "windows")) {
        strlcpy(buf, "windows", sizeof(buf));
    } else if (!strcasecmp(argv[1], "srtos")) {
        strlcpy(buf, "srtos", sizeof(buf));
    } else {
        printk("boot: unknown target %s (srtos|windows)\n", argv[1]);
        return -1;
    }
    if (vfs_write_file("/srtos/default", buf, sizeof(buf)) != 0) {
        printk("boot: cannot write /srtos/default - is the ESP mounted?\n");
        return -1;
    }
    printk("boot: next boot -> %s (reboot to apply)\n", buf);
    return 0;
}

static int cmd_install(int argc, char **argv)
{
    int part = 0;
    const char *src = "\\SRTOS\\INSTALL";
    if (argc >= 2) {
        src = argv[1];
    }
    if (argc >= 3) {
        part = atoi(argv[2]);
    }
    printk("install: writing the SRTOS image to the internal eMMC...\n");
    return installer_run(src, part);
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    reboot_now();
}

static int cmd_poweroff(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    poweroff_now();
}

static int cmd_clock(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    clock_dump();
    return 0;
}

static int cmd_platform(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    platform_dump();
    return 0;
}

#define CMD(name, help) { #name, help, cmd_##name }

const struct command __shell_commands_start[] __section(".shell_cmds") = {
    CMD(help, "list commands"),
    CMD(uname, "kernel version and build"),
    CMD(mem, "physical and heap memory"),
    CMD(ps, "list kernel threads"),
    CMD(ls, "list a directory"),
    CMD(cat, "print a file"),
    CMD(net, "network status / net ip / net dhcp"),
    CMD(ping, "icmp echo info"),
    CMD(disp, "wireless display sink control"),
    CMD(svc, "services and loadable modules"),
    CMD(blk, "block devices and partitions"),
    CMD(gpio, "gpio status"),
    CMD(i2c, "i2c status"),
    CMD(fb, "framebuffer info"),
    CMD(log, "get/set the log level"),
    CMD(drivers, "driver and mmu summary"),
    CMD(install, "install SRTOS to the internal eMMC"),
    CMD(boot, "choose the next boot target (srtos|windows)"),
    CMD(clock, "timers and uptime"),
    CMD(platform, "platform summary"),
    CMD(reboot, "reboot the tablet"),
    CMD(poweroff, "power off the tablet"),
};


static void prompt(void)
{
    printk("srtos> ");
}

static int run_line(char *line)
{
    char *argv[ARGS_MAX];
    int argc = 0;
    char *p = line;
    const struct command *c;
    extern const struct command __shell_commands_start[];

    while (*p && argc < ARGS_MAX) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p) {
            *p++ = 0;
        }
    }
    if (!argc) {
        return 0;
    }
    for (c = __shell_commands_start; c->name; c++) {
        if (!strcmp(c->name, argv[0])) {
            int r;
            printk("\n");
            r = c->fn(argc, argv);
            if (r != 0) {
                printk("[%s returned %d]\n", c->name, r);
            }
            return 0;
        }
    }
    printk("srtos: %s: command not found (try 'help')\n", argv[0]);
    return -1;
}

void shell_thread_main(void *arg)
{
    char line[LINE_MAX];
    u32 len = 0;

    (void)arg;
    printk("\nSurfaceRT_OS shell ready - type 'help' for the command list.\n");
    prompt();

    for (;;) {
        char ch;
        if (!shell_feed_read(&ch) && console_read(&ch, 1) != 1) {
            msleep(20);
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            printk("\n");
            if (len) {
                run_line(line);
            }
            len = 0;
            prompt();
        } else if (ch == 8 || ch == 127) {
            if (len) {
                len--;
                printk("\b \b");
            }
        } else if (ch >= 32 && ch < 127 && len + 1 < sizeof(line)) {
            line[len++] = ch;
            printk("%c", ch);
        }
    }
}

/* ------------------------------------------------- keyboard injection ------ */
#define FEED_SIZE 64

static volatile u32 feed_head, feed_tail;
static char feed_buf[FEED_SIZE];

void shell_feed_char(char c)
{
    u32 next = (feed_head + 1) % FEED_SIZE;
    if (next == feed_tail) {
        return;
    }
    feed_buf[feed_head] = c;
    feed_head = next;
}

int shell_feed_read(char *c)
{
    if (feed_tail == feed_head) {
        return 0;
    }
    *c = feed_buf[feed_tail];
    feed_tail = (feed_tail + 1) % FEED_SIZE;
    return 1;
}
