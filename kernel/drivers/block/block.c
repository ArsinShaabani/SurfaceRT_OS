/* SurfaceRT_OS - block device registry */
#include <srtos/block.h>
#include <srtos/kernel.h>
#include <srtos/string.h>

#define BLOCK_MAX 8

static struct block_device *devices[BLOCK_MAX];
static int nr_devices;

void block_init(void)
{
    int i;
    for (i = 0; i < BLOCK_MAX; i++) {
        devices[i] = 0;
    }
    nr_devices = 0;
}

int block_register(struct block_device *bd)
{
    if (!bd || nr_devices >= BLOCK_MAX) {
        return -1;
    }
    devices[nr_devices++] = bd;
    pr_info("block: %s %u blocks of %u bytes (%u MiB)\n", bd->name,
           (u32)bd->block_count, bd->block_size,
           (u32)((bd->block_count * bd->block_size) >> 20));
    return 0;
}

struct block_device *block_get(const char *name)
{
    int i;
    for (i = 0; i < nr_devices; i++) {
        if (!strcmp(devices[i]->name, name)) {
            return devices[i];
        }
    }
    return 0;
}

struct block_device *block_get_index(int idx)
{
    return (idx >= 0 && idx < nr_devices) ? devices[idx] : 0;
}

int block_count(void)
{
    return nr_devices;
}

void block_dump(void)
{
    int i;
    printk("  NAME      SIZE          BLOCK\n");
    for (i = 0; i < nr_devices; i++) {
        printk("  %-8s  %6u MiB    %u\n", devices[i]->name,
               (u32)((devices[i]->block_count * devices[i]->block_size) >> 20),
               devices[i]->block_size);
    }
}
