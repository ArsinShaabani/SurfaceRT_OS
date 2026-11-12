/*
 * SurfaceRT_OS - Microsoft Surface RT (Tegra 3 T30) platform bring-up
 *
 * This is the only file that knows about *this* board.  The drivers are wired
 * through the generic driver model so that `drivers` in the shell shows what
 * actually probed, and so that porting to another Tegra 3 device is a sibling
 * file with the same entry points (selected with PLATFORM= in the Makefile).
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
#include "tegra30.h"

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
    printk("platform: Surface RT / Tegra 3 T30\n");
    printk("  ram      : 0x%08x (%u MiB)\n", g_bi ? (u32)g_bi->ram_base : 0,
           g_bi ? (u32)(g_bi->ram_size >> 20) : 0);
    printk("  display  : %s\n", g_have_display ? "yes" : "no");
    printk("  blocks   : %d\n", block_count());
    printk("  netifs   : %d\n", net_if_count());
}
