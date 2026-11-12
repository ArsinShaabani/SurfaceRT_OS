/* SurfaceRT_OS - block devices */
#ifndef SRTOS_BLOCK_H
#define SRTOS_BLOCK_H

#include <srtos/types.h>
#include <srtos/list.h>

#define BLOCK_NAME_LEN 16

struct block_device {
    char name[BLOCK_NAME_LEN];
    u32 block_size;   /* usually 512 */
    u64 block_count;
    void *priv;
    int (*read)(struct block_device *bd, u64 lba, u32 count, void *buf);
    int (*write)(struct block_device *bd, u64 lba, u32 count, const void *buf);
    int (*flush)(struct block_device *bd);
    struct list_head node;
};

void block_init(void);
int block_register(struct block_device *bd);
struct block_device *block_get(const char *name);
struct block_device *block_get_index(int idx);
int block_count(void);
void block_dump(void);

#endif
