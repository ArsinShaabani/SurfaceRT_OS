/* SurfaceRT_OS - file system interfaces */
#ifndef SRTOS_FS_H
#define SRTOS_FS_H

#include <srtos/types.h>
#include <srtos/block.h>

struct srt_partition {
    int index;
    u64 start_lba;
    u64 block_count;
    int esp;
    int srtos;
    int bootable;
};

int part_scan(struct block_device *bd);
struct srt_partition *part_get(int idx);
int part_count(void);
void part_dump(void);

/* FAT32 */
int fat32_mount(struct block_device *bd, u64 start_lba, const char *mountpoint);
int fat32_mount_root(void);
int fat32_unmount_all(void);
void fat32_dump(void);

/* SRFS - the native SurfaceRT_OS file system (read/write) */
int srfs_mount(struct block_device *bd, u64 start_lba, const char *mountpoint);
int srfs_format(struct block_device *bd, u64 start_lba, const char *label);
void srfs_dump(void);

#endif
