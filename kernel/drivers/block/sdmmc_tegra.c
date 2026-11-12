/*
 * SurfaceRT_OS - Tegra 3 SDHCI host controller driver
 *
 * Drives the internal eMMC (SDMMC4), the microSD slot (SDMMC1) and the SDIO
 * Wi-Fi module (SDMMC2) through one SDHCI-compatible core.  Transfers use PIO
 * (the SDHCI buffer port): slower than ADMA, but it works without any DMA
 * descriptor plumbing and is plenty for a 40 MB/s eMMC boot partition.
 *
 * Card identification implements the SD (ACMD41) and MMC (CMD1) paths plus
 * high capacity (CSD v2) block addressing.
 */
#include <srtos/types.h>
#include <srtos/kernel.h>
#include <srtos/block.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include <srtos/gpio.h>
#include <srtos/clock.h>
#include "tegra30.h"

#define SDMMC_TIMEOUT_MS 500
#define MAX_BLOCKS_PER_XFER 64

struct sdmmc_host {
    const char *name;
    int index;          /* SDMMC controller number 1..4 */
    u32 base;
    u32 ocr;
    u32 rca;
    u32 capacity_blocks;
    int high_capacity;
    int present;
    int is_sdio;
    struct block_device bd;
};

static struct sdmmc_host hosts[4];

static inline u32 mmc_rd(struct sdmmc_host *h, u32 off)
{
    return readl(h->base + off);
}

static inline void mmc_wr(struct sdmmc_host *h, u32 off, u32 v)
{
    writel(v, h->base + off);
}

static inline u32 mmc_state(struct sdmmc_host *h)
{
    return mmc_rd(h, SDHCI_PRESENT_STATE);
}

static int mmc_wait_state(struct sdmmc_host *h, u32 mask, u32 value)
{
    u32 start = (u32)clock_uptime_ms();
    while ((mmc_state(h) & mask) != value) {
        if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
            return -1;
        }
    }
    return 0;
}

static int mmc_reset(struct sdmmc_host *h)
{
    mmc_wr(h, SDHCI_SOFTWARE_RESET, 1);
    if (mmc_wait_state(h, 1u << 24, 0) != 0) {
        /* the reset bit lives in the clock control register's high byte on
         * some SDHCI revisions; poll the register directly */
    }
    {
        u32 start = (u32)clock_uptime_ms();
        while (mmc_rd(h, SDHCI_SOFTWARE_RESET) & 1) {
            if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                return -1;
            }
        }
    }
    mmc_wr(h, SDHCI_TIMEOUT_CONTROL, 0x0E);
    mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
    mmc_wr(h, SDHCI_ERROR_INT_STATUS, 0xFFFF);
    mmc_wr(h, SDHCI_NORMAL_INT_ENABLE, 0x0003);
    mmc_wr(h, SDHCI_ERROR_INT_ENABLE, 0x0000);
    return 0;
}

static void mmc_set_clock(struct sdmmc_host *h, u32 hz)
{
    u32 caps = mmc_rd(h, SDHCI_CAPABILITIES);
    u32 base_hz = (caps >> 8) & 0xFF;
    u32 div;

    base_hz = base_hz ? base_hz * 1000000u : 50000000u;
    if (!hz) {
        mmc_wr(h, SDHCI_CLOCK_CONTROL, 0);
        return;
    }
    div = base_hz / (2u * hz);
    if (div > 0x3FF) {
        div = 0x3FF;
    }
    mmc_wr(h, SDHCI_CLOCK_CONTROL, 0);
    mmc_wr(h, SDHCI_CLOCK_CONTROL, (div & 0xFF) | ((div >> 8) & 0x3) << 8);
    mmc_wr(h, SDHCI_CLOCK_CONTROL, (div & 0xFF) | ((div >> 8) & 0x3) << 8 | BIT(0));
    mdelay(1);
    mmc_wr(h, SDHCI_CLOCK_CONTROL,
           (div & 0xFF) | (((div >> 8) & 0x3) << 8) | BIT(0) | BIT(2));
    mdelay(1);
}

static void mmc_set_bus_width(struct sdmmc_host *h, int width)
{
    u32 ctrl = mmc_rd(h, SDHCI_HOST_CONTROL) & ~0x22u;
    if (width == 4) {
        ctrl |= 0x02;
    } else if (width == 8) {
        ctrl |= 0x20;
    }
    mmc_wr(h, SDHCI_HOST_CONTROL, ctrl);
}

static int mmc_send_cmd(struct sdmmc_host *h, u32 idx, u32 arg, u32 flags,
                        u32 *resp)
{
    u32 cmd = (idx << 8) | (flags & 0x3F);
    u32 start;
    int i;

    if (mmc_wait_state(h, BIT(0) | BIT(1), 0) != 0) { /* CMD/DAT inhibit */
        return -1;
    }
    mmc_wr(h, SDHCI_ARGUMENT, arg);
    mmc_wr(h, SDHCI_COMMAND, cmd);

    start = (u32)clock_uptime_ms();
    for (;;) {
        u32 st = mmc_rd(h, SDHCI_NORMAL_INT_STATUS);
        if (st & BIT(0)) { /* command complete */
            mmc_wr(h, SDHCI_NORMAL_INT_STATUS, BIT(0));
            break;
        }
        if (st & BIT(15)) { /* error interrupt */
            u32 err = mmc_rd(h, SDHCI_ERROR_INT_STATUS);
            mmc_wr(h, SDHCI_ERROR_INT_STATUS, err);
            mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
            pr_debug("sdmmc%d: cmd%u error 0x%04x\n", h->index, idx, err);
            return -1;
        }
        if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
            return -1;
        }
    }
    if (resp) {
        for (i = 0; i < 4; i++) {
            resp[i] = mmc_rd(h, SDHCI_RESPONSE + i * 4);
        }
    }
    return 0;
}

static int mmc_pio_transfer(struct sdmmc_host *h, u64 lba, u32 blocks, void *buf,
                            int write)
{
    u32 *p = (u32 *)buf;
    u32 words, i;
    u32 mask = write ? BIT(5) : BIT(5);

    (void)mask;
    mmc_wr(h, SDHCI_BLOCK_SIZE, (1u << 16) | 512u);
    mmc_wr(h, SDHCI_BLOCK_COUNT, blocks ? blocks : 1);
    mmc_wr(h, SDHCI_ARGUMENT, (u32)lba);
    mmc_wr(h, SDHCI_TRANSFER_MODE,
           (blocks > 1 ? BIT(5) | BIT(1) : 0) | (write ? 0 : BIT(4)) |
               (write ? 0 : 0));
    mmc_wr(h, SDHCI_COMMAND,
           ((blocks > 1 ? 25u : 24u) << 8) | BIT(4) | BIT(3) | BIT(5) |
               (write ? 0 : BIT(4)));

    words = (blocks ? blocks : 1) * 128;
    for (i = 0; i < words; i++) {
        u32 start = (u32)clock_uptime_ms();
        if (write) {
            while (mmc_state(h) & BIT(1)) {
                if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                    return -1;
                }
            }
            mmc_wr(h, SDHCI_BUFFER, p[i]);
        } else {
            while (mmc_state(h) & BIT(11)) {
                if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                    return -1;
                }
            }
            p[i] = mmc_rd(h, SDHCI_BUFFER);
        }
    }
    {
        u32 start = (u32)clock_uptime_ms();
        while (!(mmc_rd(h, SDHCI_NORMAL_INT_STATUS) & BIT(1))) {
            if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                return -1;
            }
            if (mmc_rd(h, SDHCI_NORMAL_INT_STATUS) & BIT(15)) {
                mmc_wr(h, SDHCI_ERROR_INT_STATUS, 0xFFFF);
                mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
                return -1;
            }
        }
    }
    mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
    return 0;
}

#define CMD_RESP_NONE 0x00
#define CMD_RESP_136 0x01
#define CMD_RESP_48 0x02
#define CMD_RESP_48_BUSY 0x03
#define CMD_DATA_PRESENT 0x20
#define CMD_CRC_CHECK 0x08
#define CMD_IDX_CHECK 0x10

static u32 extract_bits(const u32 *resp, int start, int end)
{
    u64 v = ((u64)resp[3] << 96) | ((u64)resp[2] << 64) | ((u64)resp[1] << 32) |
            resp[0];
    int nbits = end - start + 1;
    return (u32)((v >> (128 - 1 - end)) & ((1ull << nbits) - 1ull));
}

static int sd_send_acmd(struct sdmmc_host *h, u32 acmd, u32 arg, u32 *resp)
{
    u32 rca = h->rca << 16;
    if (mmc_send_cmd(h, 55, rca, CMD_RESP_48 | CMD_CRC_CHECK | CMD_IDX_CHECK,
                     0) != 0) {
        return -1;
    }
    return mmc_send_cmd(h, acmd, arg,
                        CMD_RESP_48 | CMD_CRC_CHECK | CMD_IDX_CHECK, resp);
}

static int sdmmc_identify(struct sdmmc_host *h, int is_sd)
{
    u32 resp[4];
    u32 ocr = 0;
    int i, tries = 100;

    mmc_set_clock(h, 400000);
    mdelay(2);
    mmc_send_cmd(h, 0, 0, CMD_RESP_NONE, 0);
    mdelay(2);

    if (is_sd) {
        mmc_send_cmd(h, 8, 0x1AA, CMD_RESP_48 | CMD_CRC_CHECK | CMD_IDX_CHECK,
                     resp);
    }

    for (i = 0; i < tries; i++) {
        if (is_sd) {
            if (sd_send_acmd(h, 41, 0x40FF8000u, &ocr) != 0) {
                mdelay(10);
                continue;
            }
        } else {
            u32 arg = 0x40FF8000u;
            if (h->capacity_blocks > 0xFFFFFFFFull / 512u) {
                arg |= BIT(30);
            }
            if (mmc_send_cmd(h, 1, arg, CMD_RESP_48, &ocr) != 0) {
                mdelay(10);
                continue;
            }
        }
        if (ocr & BIT(31)) {
            break;
        }
        mdelay(10);
    }
    if (!(ocr & BIT(31))) {
        return -1;
    }
    h->ocr = ocr;
    h->high_capacity = (ocr & BIT(30)) ? 1 : 0;

    if (mmc_send_cmd(h, 2, 0, CMD_RESP_136 | CMD_CRC_CHECK, resp) != 0) {
        return -1;
    }
    if (mmc_send_cmd(h, 3, 1u << 16, CMD_RESP_48 | CMD_CRC_CHECK | CMD_IDX_CHECK,
                     resp) != 0) {
        return -1;
    }
    h->rca = (resp[0] >> 16) & 0xFFFF;

    if (mmc_send_cmd(h, 9, h->rca << 16, CMD_RESP_136 | CMD_CRC_CHECK, resp) != 0) {
        return -1;
    }
    {
        u32 c_size = extract_bits(resp, 48, 69);
        u32 c_size_mult = extract_bits(resp, 39, 41);
        u32 read_bl_len = extract_bits(resp, 80, 83);
        if (c_size >= 0x0FFF) {
            h->capacity_blocks = (c_size + 1) * 1024;
        } else {
            u64 blocks = (u64)(c_size + 1) << (c_size_mult + 2);
            u64 bytes = blocks << read_bl_len;
            h->capacity_blocks = (u32)(bytes / 512u);
        }
    }

    if (mmc_send_cmd(h, 7, h->rca << 16, CMD_RESP_48_BUSY | CMD_CRC_CHECK, 0) != 0) {
        return -1;
    }
    if (is_sd) {
        sd_send_acmd(h, 6, 2, 0); /* 4 bit bus */
        mmc_set_bus_width(h, 4);
    } else {
        mmc_set_bus_width(h, 8);
    }
    mmc_send_cmd(h, 16, 512, CMD_RESP_48, 0);
    mmc_set_clock(h, 25000000);
    return 0;
}

static int sdmmc_bd_read(struct block_device *bd, u64 lba, u32 count, void *buf)
{
    struct sdmmc_host *h = (struct sdmmc_host *)bd->priv;
    u8 *p = (u8 *)buf;
    while (count) {
        u32 n = count > MAX_BLOCKS_PER_XFER ? MAX_BLOCKS_PER_XFER : count;
        u32 addr = h->high_capacity ? (u32)lba : (u32)(lba * 512u);
        if (mmc_pio_transfer(h, addr, n, p, 0) != 0) {
            return -1;
        }
        lba += n;
        count -= n;
        p += n * 512u;
    }
    return 0;
}

static int sdmmc_bd_write(struct block_device *bd, u64 lba, u32 count,
                          const void *buf)
{
    struct sdmmc_host *h = (struct sdmmc_host *)bd->priv;
    const u8 *p = (const u8 *)buf;
    while (count) {
        u32 n = count > MAX_BLOCKS_PER_XFER ? MAX_BLOCKS_PER_XFER : count;
        u32 addr = h->high_capacity ? (u32)lba : (u32)(lba * 512u);
        if (mmc_pio_transfer(h, addr, n, (void *)p, 1) != 0) {
            return -1;
        }
        lba += n;
        count -= n;
        p += n * 512u;
    }
    return 0;
}

static int sdmmc_bd_flush(struct block_device *bd)
{
    struct sdmmc_host *h = (struct sdmmc_host *)bd->priv;
    return mmc_send_cmd(h, 13, h->rca ? (h->rca << 16) : 0, CMD_RESP_48, 0);
}

struct sdmmc_host *sdmmc_host_get(int index)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (hosts[i].index == index) {
            return &hosts[i];
        }
    }
    return 0;
}

static const struct {
    int index;
    const char *name;
    const char *what;
    int is_sd;
} board_slots[] = {
    { 4, "mmcblk0", "eMMC", 0 },     /* Surface RT: 32/64 GB internal eMMC */
    { 1, "mmcblk1", "microSD", 1 },  /* microSDXC slot on SDMMC1          */
};

int sdmmc_init(void)
{
    unsigned i;
    int found = 0;

    memset(hosts, 0, sizeof(hosts));
    for (i = 0; i < ARRAY_SIZE(board_slots); i++) {
        struct sdmmc_host *h = &hosts[i];
        u32 state;

        h->index = board_slots[i].index;
        h->name = board_slots[i].name;
        h->base = T30_SDMMC_BASE(h->index - 1);

        mmc_wr(h, SDHCI_POWER_CONTROL, 0);
        if (mmc_reset(h) != 0) {
            pr_warn("sdmmc%d: controller reset failed\n", h->index);
            continue;
        }
        mmc_wr(h, SDHCI_POWER_CONTROL, 0x0E | BIT(0)); /* 3.3V, bus power */
        mdelay(2);
        mmc_set_clock(h, 400000);

        state = mmc_state(h);
        if (!(state & BIT(16))) { /* card inserted / CMD line high */
            pr_info("sdmmc%d: no card on %s\n", h->index, board_slots[i].what);
            continue;
        }
        if (sdmmc_identify(h, board_slots[i].is_sd) != 0) {
            pr_warn("sdmmc%d: %s identification failed\n", h->index,
                    board_slots[i].what);
            continue;
        }
        h->present = 1;
        strlcpy(h->bd.name, h->name, sizeof(h->bd.name));
        h->bd.block_size = 512;
        h->bd.block_count = h->capacity_blocks;
        h->bd.priv = h;
        h->bd.read = sdmmc_bd_read;
        h->bd.write = sdmmc_bd_write;
        h->bd.flush = sdmmc_bd_flush;
        block_register(&h->bd);
        pr_info("sdmmc%d: %s ready, %u MiB, %s capacity, RCA 0x%04x\n", h->index,
                board_slots[i].what,
                (u32)((h->capacity_blocks * 512ull) >> 20),
                h->high_capacity ? "high" : "standard", h->rca);
        found++;
    }
    return found ? 0 : -1;
}

/* ------------------------------------------------------------ SDIO access --- */
/*
 * Raw SDIO commands for transport drivers (the Wi-Fi module on SDMMC2 needs
 * CMD52/CMD53 which are not part of the block protocol).
 */
int sdio_cmd(int host_index, u32 cmd, u32 arg, u32 flags, u32 *resp)
{
    struct sdmmc_host *h = sdmmc_host_get(host_index);
    if (!h) {
        return -1;
    }
    (void)flags;
    return mmc_send_cmd(h, cmd, arg, CMD_RESP_48 | CMD_CRC_CHECK | CMD_IDX_CHECK,
                        resp);
}

int sdio_cmd53(int host_index, int write, u32 addr, void *buf, u32 blocks,
               u32 block_size)
{
    struct sdmmc_host *h = sdmmc_host_get(host_index);
    u32 arg, words, i;
    u32 *p = (u32 *)buf;

    if (!h || !blocks) {
        return -1;
    }
    mmc_set_clock(h, 25000000);
    mmc_wr(h, SDHCI_BLOCK_SIZE, ((block_size & 0xFFF) << 16) | 512u);
    mmc_wr(h, SDHCI_BLOCK_COUNT, blocks);

    arg = ((write ? 1u : 0u) << 31) | (1u << 28) | (1u << 27) |
          (((addr >> 9) & 0x1FFFFu) << 9) | (blocks & 0x1FFu);
    mmc_wr(h, SDHCI_ARGUMENT, arg);
    mmc_wr(h, SDHCI_TRANSFER_MODE,
           BIT(5) | BIT(1) | (write ? 0u : BIT(4)));
    mmc_wr(h, SDHCI_COMMAND, (53u << 8) | BIT(5) | BIT(4) | BIT(3) |
                                 (write ? 0u : BIT(4)));

    words = blocks * (block_size / 4u);
    for (i = 0; i < words; i++) {
        u32 start = (u32)clock_uptime_ms();
        if (write) {
            while (mmc_state(h) & BIT(1)) {
                if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                    return -1;
                }
            }
            mmc_wr(h, SDHCI_BUFFER, p[i]);
        } else {
            while (mmc_state(h) & BIT(11)) {
                if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                    return -1;
                }
            }
            p[i] = mmc_rd(h, SDHCI_BUFFER);
        }
    }
    {
        u32 start = (u32)clock_uptime_ms();
        while (!(mmc_rd(h, SDHCI_NORMAL_INT_STATUS) & BIT(1))) {
            if ((u32)clock_uptime_ms() - start > SDMMC_TIMEOUT_MS) {
                return -1;
            }
            if (mmc_rd(h, SDHCI_NORMAL_INT_STATUS) & BIT(15)) {
                mmc_wr(h, SDHCI_ERROR_INT_STATUS, 0xFFFF);
                mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
                return -1;
            }
        }
    }
    mmc_wr(h, SDHCI_NORMAL_INT_STATUS, 0xFFFF);
    return 0;
}
