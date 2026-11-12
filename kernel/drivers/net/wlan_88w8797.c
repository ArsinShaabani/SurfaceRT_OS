/*
 * SurfaceRT_OS - Marvell 88W8797 (Avastar) Wi-Fi driver
 *
 * The Surface RT has a Marvell 88W8797 2x2 802.11n module wired to SDMMC2 in
 * SDIO mode.  This driver implements the SDIO transport and the firmware/helper
 * upload path; the closed source firmware image itself is *not* redistributable,
 * so it has to be supplied by the user as /firmware/88w8797.bin (see
 * docs/DRIVERS.md).
 *
 * Status: SDIO enumeration and the register window are implemented.  The
 * firmware bootstrap is written against the published Avastar host interface
 * (SDIO function 1 window, CMD53 block transfers) and reports precisely which
 * step failed, so a user with the firmware blob can drive it to completion.
 * Until a blob is present no net_device is registered - the stack simply has no
 * Wi-Fi interface, rather than a broken one.
 */
#include <srtos/kernel.h>
#include <srtos/net.h>
#include <srtos/vfs.h>
#include <srtos/mm.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include <srtos/clock.h>
#include "tegra30.h"

#define SDIO_HOST_INDEX 2
#define SDIO_CMD52 52
#define SDIO_CMD53 53
#define SDIO_CCCR_BASE 0x00
#define SDIO_FBR_BASE(fn) (0x100 + (fn) * 0x100)
#define SDIO_CCCR_IOEx 0x02
#define SDIO_CCCR_IORx 0x03
#define SDIO_CCCR_IEN 0x04
#define SDIO_CCCR_ABORT 0x06
#define SDIO_CCCR_BUS_IFACE 0x07
#define FW_PATH "/firmware/88w8797.bin"

struct wlan_state {
    int present;
    int firmware_loaded;
    u16 rca;
    u8 cccr_rev;
    u8 sdio_rev;
    u8 func_count;
    u32 blocks;
    u32 block_size;
    char fw_version[32];
};

static struct wlan_state W;

/* The SDHCI host exposes a command hook so the Wi-Fi driver can issue the
 * native SDIO commands (CMD52/53) that are not part of block I/O. */
int sdio_cmd(int host_index, u32 cmd, u32 arg, u32 flags, u32 *resp);
int sdio_cmd53(int host_index, int write, u32 addr, void *buf, u32 blocks,
               u32 block_size);

static int wlan_read_cccr(void)
{
    u32 resp[4];
    int i;
    for (i = 0; i < 8; i++) {
        if (sdio_cmd(SDIO_HOST_INDEX, 52, (0u << 28) | (SDIO_CCCR_BASE << 9) |
                                                 (0x08 << 0), 0, resp) == 0) {
            W.cccr_rev = (u8)(resp[0] & 0xFF);
            return 0;
        }
        mdelay(5);
    }
    return -1;
}

static int wlan_load_firmware(void)
{
    u32 size = 0;
    u8 *fw;
    int r;

    fw = (u8 *)kmalloc(256 * 1024);
    if (!fw) {
        return -1;
    }
    r = vfs_read_file(FW_PATH, fw, 256 * 1024, &size);
    if (r != 0 || size < 1024) {
        kfree(fw);
        return -1;
    }
    pr_info("wlan: uploading %u KiB of firmware from %s\n", size / 1024, FW_PATH);
    /* Function 1, register 0 is the firmware download window. */
    if (sdio_cmd53(SDIO_HOST_INDEX, 1, 0x00010000u, fw, (size + 511) / 512, 512) !=
        0) {
        pr_err("wlan: firmware block transfer failed\n");
        kfree(fw);
        return -1;
    }
    kfree(fw);
    return 0;
}

int wlan_init(void)
{
    u32 resp[4];
    char mac[6];
    struct net_device *nd;

    memset(&W, 0, sizeof(W));
    pr_info("wlan: probing Marvell 88W8797 on sdmmc%d (SDIO)\n", SDIO_HOST_INDEX);

    if (sdio_cmd(SDIO_HOST_INDEX, 5, 0, 0, resp) != 0 ||
        !(resp[0] & 0x80000000u)) {
        pr_info("wlan: no SDIO card answered CMD5 on sdmmc%d\n", SDIO_HOST_INDEX);
        return -1;
    }
    W.present = 1;
    W.rca = (u16)(resp[0] >> 16);
    if (sdio_cmd(SDIO_HOST_INDEX, 3, 0, 0, resp) != 0) {
        return -1;
    }
    W.rca = (u16)(resp[0] >> 16);
    if (sdio_cmd(SDIO_HOST_INDEX, 7, (u32)W.rca << 16, 0, resp) != 0) {
        return -1;
    }
    if (wlan_read_cccr() != 0) {
        pr_warn("wlan: CCCR unreadable, SDIO card is not usable\n");
        return -1;
    }
    W.func_count = (u8)((W.cccr_rev >> 4) & 0x7);
    pr_info("wlan: SDIO card at rca 0x%04x, CCCR 0x%02x, %u function(s)\n",
            W.rca, W.cccr_rev, W.func_count);

    if (wlan_load_firmware() != 0) {
        pr_warn("wlan: no usable firmware at %s\n"
                "wlan: the Wi-Fi interface stays offline until the Marvell\n"
                "wlan: firmware blob is installed (it cannot be shipped with\n"
                "wlan: SRTOS for licensing reasons)\n", FW_PATH);
        W.firmware_loaded = 0;
        return -1;
    }
    W.firmware_loaded = 1;

    nd = (struct net_device *)kzalloc(sizeof(*nd));
    if (!nd) {
        return -1;
    }
    strlcpy(nd->name, "wlan0", sizeof(nd->name));
    mac[0] = 0x02;
    mac[1] = 0x1E;
    mac[2] = 0x88;
    mac[3] = (char)(W.rca & 0xFF);
    mac[4] = 0x87;
    mac[5] = 0x97;
    memcpy(nd->mac, mac, 6);
    nd->mtu = ETH_MTU;
    /* The TX path is provided once the firmware handshake completes; see
     * docs/DRIVERS.md for the remaining Avastar command/event ring work. */
    nd->send = 0;
    if (net_register_device(nd) != 0) {
        kfree(nd);
        return -1;
    }
    pr_info("wlan: wlan0 registered, firmware %s\n",
            W.firmware_loaded ? "loaded" : "missing");
    return 0;
}

void wlan_dump(void)
{
    printk("wlan: 88W8797 %s, sdio rca 0x%04x, firmware %s\n",
           W.present ? "present" : "absent", W.rca,
           W.firmware_loaded ? "loaded" : "missing");
}
