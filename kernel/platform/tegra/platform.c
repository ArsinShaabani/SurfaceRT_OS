/*
 * SurfaceRT_OS - Tegra family platform bring-up
 *
 * Shared by every Tegra board we support: Surface RT (Tegra 3 / T30) and
 * Surface 2, a.k.a. "Surface RT 2" (Tegra 4 / T114).  Everything that differs
 * between them - CPU core, timer block, L2, board wiring, IRQ numbers - lives
 * in kernel/platform/<soc>/<soc>.h and is reached through <srtos/soc.h>.
 *
 * Drivers are wired through the generic driver model so that `drivers` in the
 * shell shows what actually probed (and what did not, which is the first thing
 * you want to know on a new board).
 */
#include <srtos/kernel.h>
#include <srtos/platform.h>
#include <srtos/driver.h>
#include <srtos/mm.h>
#include <srtos/vfs.h>
#include <srtos/block.h>
#include <srtos/net.h>
#include <srtos/gpio.h>
#include <srtos/i2c.h>
#include <srtos/pmic.h>
#include <srtos/fb.h>
#include <srtos/string.h>
#include <srtos/soc.h>

/* implemented by the individual drivers */
void fb_init(const struct srt_framebuffer *fb);
void fb_register_console(void);
int dc_init(void);
int sdmmc_init(void);
int usb_init(void);
int input_init(void);
int wlan_init(void);
int thermal_init(void);
int fuse_init(void);
int fat32_mount_root(void);

/* A machine readable board id.  scripts/mkimage.py greps the kernel image for
 * it, so an ESP can never be built with a kernel for a different tablet. */
const char srtos_platform_id[] = "SRTOS-PLATFORM:" SRTOS_PLATFORM_TOKEN;

static const struct srt_bootinfo *g_bi;
static int g_have_display;
static int g_have_block;

int platform_has_display(void)
{
    return g_have_display;
}

/* --------------------------------------------------------- driver probes --- */
static int probe_gpio(struct driver *d) { (void)d; return gpio_init(); }
static int probe_i2c(struct driver *d) { (void)d; return i2c_init(); }
static int probe_pmic(struct driver *d) { (void)d; return pmic_init(); }
static int probe_fuse(struct driver *d) { (void)d; return fuse_init(); }
static int probe_thermal(struct driver *d) { (void)d; return thermal_init(); }
static int probe_sdmmc(struct driver *d)
{
    (void)d;
    if (sdmmc_init() != 0) {
        return -1;
    }
    block_init();
    g_have_block = 1;
    return 0;
}

static int probe_display(struct driver *d)
{
    (void)d;
    g_bi = bootinfo();
    if (!g_bi || !(g_bi->loader_flags & SRT_BOOT_FLAG_FB_VALID)) {
        return -1;
    }
    fb_init(&g_bi->fb);
    g_have_display = 1;
    /* Take over the display controller when possible; if that fails we keep
     * the firmware framebuffer, which is enough for every service. */
    if (dc_init() != 0) {
        pr_info("display: using the firmware framebuffer only\n");
    }
    fb_register_console();
    return 0;
}

static int probe_usb(struct driver *d) { (void)d; return usb_init(); }
static int probe_input(struct driver *d) { (void)d; return input_init(); }
static int probe_net(struct driver *d) { (void)d; net_init(); return 0; }
static int probe_wlan(struct driver *d) { (void)d; return wlan_init(); }

static struct driver board_drivers[] = {
    { .name = "tegra-gpio", .cls = DRV_GPIO, .probe = probe_gpio },
    { .name = "tegra-i2c", .cls = DRV_I2C, .probe = probe_i2c },
    { .name = "max77663", .cls = DRV_PMIC, .probe = probe_pmic },
    { .name = "tegra-fuse", .cls = DRV_THERMAL, .probe = probe_fuse },
    { .name = "tegra-tsensor", .cls = DRV_THERMAL, .probe = probe_thermal },
    { .name = "sdhci", .cls = DRV_BLOCK, .probe = probe_sdmmc },
    { .name = "tegra-dc", .cls = DRV_DISPLAY, .probe = probe_display },
    { .name = "ehci", .cls = DRV_USB, .probe = probe_usb },
    { .name = "input-core", .cls = DRV_INPUT, .probe = probe_input },
    { .name = "net-core", .cls = DRV_NET, .probe = probe_net },
    { .name = "avastar-88w8797", .cls = DRV_WLAN, .probe = probe_wlan },
};

void platform_init(void)
{
    unsigned i;
    int err;

    driver_core_init();
    uart_console_init();

    for (i = 0; i < ARRAY_SIZE(board_drivers); i++) {
        driver_register(&board_drivers[i]);
    }
    err = driver_init_all();
    if (!err) {
        pr_warn("platform: no driver probed successfully\n");
    }
    driver_dump();
}

void platform_mount_root(void)
{
    if (!g_have_block) {
        pr_warn("platform: no block device, cannot mount a root file system\n");
        return;
    }
    if (fat32_mount_root() == 0) {
        pr_info("platform: root filesystem mounted\n");
    } else {
        pr_warn("platform: no FAT32 partition found (is the ESP intact?)\n");
    }
}

void platform_dump(void)
{
    printk("platform: %s, %s [%s]\n", SRTOS_BOARD_NAME, SRTOS_SOC_NAME,
           srtos_platform_id);
    printk("  cpu      : %d x %s\n", PLATFORM_CPU_CORES, PLATFORM_CPU_NAME);
    printk("  ram      : 0x%08x (%u MiB)\n", g_bi ? (u32)g_bi->ram_base : 0,
           g_bi ? (u32)(g_bi->ram_size >> 20) : 0);
    printk("  display  : %s (%s panel)\n", g_have_display ? "yes" : "no",
           PLATFORM_PANEL_HINT);
    printk("  blocks   : %d\n", block_count());
    printk("  netifs   : %d\n", net_if_count());
}
