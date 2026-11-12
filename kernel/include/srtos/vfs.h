/* SurfaceRT_OS - virtual file system */
#ifndef SRTOS_VFS_H
#define SRTOS_VFS_H

#include <srtos/types.h>
#include <srtos/list.h>
#include <srtos/errno.h>

#define VFS_NAME_MAX 64
#define VFS_PATH_MAX 256

enum vfs_type { VFS_FILE = 1, VFS_DIR = 2 };

struct vfs_node;

struct vfs_ops {
    int (*read)(struct vfs_node *n, u64 off, void *buf, u32 len);
    int (*write)(struct vfs_node *n, u64 off, const void *buf, u32 len);
    int (*readdir)(struct vfs_node *n, u32 idx, char *name, u32 name_len,
                   u32 *size, u32 *type);
    struct vfs_node *(*lookup)(struct vfs_node *dir, const char *name);
    int (*create)(struct vfs_node *dir, const char *name, u32 type);
    int (*unlink)(struct vfs_node *dir, const char *name);
    int (*truncate)(struct vfs_node *n, u64 size);
    void (*release)(struct vfs_node *n);
};

struct vfs_node {
    char name[VFS_NAME_MAX];
    u32 type;
    u64 size;
    void *priv;
    struct vfs_ops ops;
    struct vfs_node *parent;
    u32 refs;
    struct list_head node;   /* sibling list */
    struct list_head children;
};

struct vfs_mount {
    char path[VFS_PATH_MAX];
    struct vfs_node *root;
    struct list_head node;
};

void vfs_init(void);
int vfs_mount(const char *path, struct vfs_node *root);
int vfs_unmount(const char *path);
struct vfs_node *vfs_lookup(const char *path);
int vfs_read_file(const char *path, void *buf, u32 max, u32 *out_len);
int vfs_write_file(const char *path, const void *buf, u32 len);
int vfs_stat(const char *path, u32 *type, u64 *size);
void vfs_list(const char *path);
struct vfs_node *vfs_node_alloc(const char *name, u32 type);
void vfs_node_free(struct vfs_node *n);
void vfs_attach(struct vfs_node *parent, struct vfs_node *child);
void vfs_dump(void);

#endif
