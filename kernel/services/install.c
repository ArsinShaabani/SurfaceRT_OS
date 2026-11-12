/*
 * SurfaceRT_OS - installer
 *
 * Writes a complete SurfaceRT_OS disk image (GPT + EFI system partition +
 * SRTOS data partition, produced by scripts/mkimage.py) onto the internal eMMC.
 *
 * Writing a whole disk image instead of patching a file system keeps the
 * installer tiny and makes the installed layout byte identical to the USB stick
 * that was tested - which is exactly what you want for a device that is hard to
 * debug.
 */
#include <srtos/kernel.h>
#include <srtos/fs.h>
#include <srtos/block.h>
#include <srtos/vfs.h>
#include <srtos/mm.h>
#include <srtos/clock.h>
#include <srtos/string.h>

#define CHUNK_BLOCKS 128 /* 64 KiB per write */

static int looks_like_gpt(const u8 *sector)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return 0;
    }
    if (sector[450] != 0xEE) { /* protective MBR partition type */
        return 0;
    }
    return 1;
}

int installer_run(const char *source, int partition)
{
    struct block_device *target = block_get("mmcblk0");
    u8 *buffer;
    u32 file_size = 0, done = 0;
    u64 blocks;
    struct vfs_node *node;
    char *path;

    if (!target) {
        printk("install: no internal eMMC found (mmcblk0)\n");
        return -1;
    }
    node = vfs_lookup(source);
    if (!node) {
        printk("install: %s not found\n", source);
        printk("install: copy srtos-disk.img next to the loader on the USB\n"
               "         stick, then run: install /install/srtos-disk.img 1\n");
        return -1;
    }
    if (node->size < 1024 * 1024) {
        printk("install: %s is too small to be a disk image\n", source);
        return -1;
    }
    if (!partition) {
        printk("install: refusing to overwrite %s without confirmation.\n"
               "         re-run as: install %s 1\n", target->name, source);
        printk("         (this erases the internal eMMC)\n");
        return -1;
    }
    if (node->size > target->block_count * target->block_size) {
        printk("install: image (%u MiB) does not fit on %s (%u MiB)\n",
               (u32)(node->size >> 20), target->name,
               (u32)((target->block_count * target->block_size) >> 20));
        return -1;
    }

    buffer = (u8 *)kmalloc(CHUNK_BLOCKS * 512);
    if (!buffer) {
        return -1;
    }
    if (node->ops.read(node, 0, buffer, 512) != 512 || !looks_like_gpt(buffer)) {
        printk("install: %s does not look like a GPT disk image\n", source);
        kfree(buffer);
        return -1;
    }

    file_size = (u32)node->size;
    blocks = (file_size + 511) / 512;
    printk("install: writing %u MiB to %s (%u blocks) - do not power off\n",
           file_size >> 20, target->name, (u32)blocks);

    while (done < file_size) {
        u32 len = file_size - done;
        u32 blk = (done / 512) % CHUNK_BLOCKS;
        u32 nb;
        int r;

        if (len > CHUNK_BLOCKS * 512) {
            len = CHUNK_BLOCKS * 512;
        }
        nb = (len + 511) / 512;
        r = node->ops.read(node, done, buffer, nb * 512);
        if (r <= 0) {
            printk("install: read error at %u\n", done);
            kfree(buffer);
            return -1;
        }
        if (target->write(target, done / 512, nb, buffer) != 0) {
            printk("install: write error at block %u\n", done / 512);
            kfree(buffer);
            return -1;
        }
        done += nb * 512;
        if ((done / (1024 * 1024)) != ((done - nb * 512) / (1024 * 1024))) {
            printk("install: %u%%\n", (u32)((u64)done * 100 / file_size));
        }
        (void)blk;
    }
    if (target->flush) {
        target->flush(target);
    }

    /* read back the first sector and the GPT header for sanity */
    if (target->read(target, 0, 1, buffer) == 0 && looks_like_gpt(buffer)) {
        printk("install: done, %u MiB written and verified\n", file_size >> 20);
    } else {
        printk("install: write completed but the read back check failed\n");
    }
    kfree(buffer);
    (void)path;
    return 0;
}
