/*
 * SurfaceRT_OS - MBR/GPT partition parsing
 *
 * Used to find the SRTOS partition on the eMMC, the microSD card or a USB
 * stick.  Both the protective MBR from Windows and a plain MBR are understood.
 */
#include <srtos/types.h>
#include <srtos/kernel.h>
#include <srtos/block.h>
#include <srtos/mm.h>
#include <srtos/string.h>
#include <srtos/fs.h>

#define MAX_PARTITIONS 8

static struct srt_partition partitions[MAX_PARTITIONS];
static int nr_partitions;

static const u8 gpt_signature[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
static const u8 esp_type[16] = { 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                                 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b };
static const u8 srtos_type[16] = { 0x53, 0x52, 0x54, 0x4f, 0x53, 0x44, 0x41, 0x54,
                                   0x41, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

int part_scan(struct block_device *bd)
{
    u8 *buf;
    int i, found = 0;

    nr_partitions = 0;
    if (!bd) {
        return -1;
    }
    buf = (u8 *)kmalloc(4096);
    if (!buf) {
        return -1;
    }
    if (bd->read(bd, 0, 1, buf) != 0) {
        kfree(buf);
        return -1;
    }
    if (!memcmp(buf + 510, "\x55\xaa", 2)) {
        /* MBR: four primary entries at offset 446 */
        for (i = 0; i < 4; i++) {
            const u8 *e = buf + 446 + i * 16;
            u8 type = e[4];
            u32 start = (u32)e[8] | ((u32)e[9] << 8) | ((u32)e[10] << 16) |
                        ((u32)e[11] << 24);
            u32 count = (u32)e[12] | ((u32)e[13] << 8) | ((u32)e[14] << 16) |
                        ((u32)e[15] << 24);
            if (!type || !count) {
                continue;
            }
            partitions[nr_partitions].index = i + 1;
            partitions[nr_partitions].start_lba = start;
            partitions[nr_partitions].block_count = count;
            partitions[nr_partitions].bootable = e[0] == 0x80;
            partitions[nr_partitions].esp = (type == 0xEF || type == 0x0C ||
                                             type == 0x0B);
            partitions[nr_partitions].srtos = 0;
            nr_partitions++;
        }
        found = nr_partitions;
    }

    /* GPT header at LBA 1 */
    if (bd->read(bd, 1, 1, buf) == 0 && !memcmp(buf, gpt_signature, 8)) {
        u64 entry_lba = (u64)readl((uptr)buf + 72) |
                        ((u64)readl((uptr)buf + 76) << 32);
        u32 entry_count = readl((uptr)buf + 80);
        u32 entry_size = readl((uptr)buf + 84);
        u32 j;

        nr_partitions = 0;
        for (j = 0; j < entry_count && j < 32; j++) {
            u8 entry[128];
            u64 lba = entry_lba + (j * entry_size) / 512;
            u32 off = (j * entry_size) % 512;
            if (bd->read(bd, lba, 1, buf) != 0) {
                continue;
            }
            memcpy(entry, buf + off, sizeof(entry));
            {
                int empty = 1, k;
                for (k = 0; k < 16; k++) {
                    if (entry[k]) {
                        empty = 0;
                    }
                }
                if (empty) {
                    continue;
                }
            }
            if (nr_partitions >= MAX_PARTITIONS) {
                break;
            }
            partitions[nr_partitions].index = j + 1;
            partitions[nr_partitions].start_lba =
                (u64)readl((uptr)entry + 32) | ((u64)readl((uptr)entry + 36) << 32);
            partitions[nr_partitions].block_count =
                (u64)readl((uptr)entry + 40) | ((u64)readl((uptr)entry + 44) << 32);
            partitions[nr_partitions].esp = !memcmp(entry, esp_type, 16);
            partitions[nr_partitions].srtos = !memcmp(entry, srtos_type, 16);
            partitions[nr_partitions].bootable = 0;
            nr_partitions++;
            found++;
        }
    }
    kfree(buf);
    return found ? nr_partitions : -1;
}

struct srt_partition *part_get(int idx)
{
    return (idx >= 0 && idx < nr_partitions) ? &partitions[idx] : 0;
}

int part_count(void)
{
    return nr_partitions;
}

void part_dump(void)
{
    int i;
    printk("  #  START        BLOCKS       TYPE\n");
    for (i = 0; i < nr_partitions; i++) {
        printk("  %d  %-10u  %-10u  %s%s\n", partitions[i].index,
               (u32)partitions[i].start_lba, (u32)partitions[i].block_count,
               partitions[i].esp ? "EFI-system" : "",
               partitions[i].srtos ? "SRTOS-data" : "");
    }
}
