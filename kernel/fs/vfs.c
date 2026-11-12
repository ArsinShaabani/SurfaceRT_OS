/* SurfaceRT_OS - virtual file system core */
#include <srtos/vfs.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/string.h>

static LIST_HEAD(mounts);

void vfs_init(void)
{
    list_init(&mounts);
    pr_info("vfs: ready\n");
}

struct vfs_node *vfs_node_alloc(const char *name, u32 type)
{
    struct vfs_node *n = (struct vfs_node *)kzalloc(sizeof(*n));
    if (!n) {
        return 0;
    }
    strlcpy(n->name, name, VFS_NAME_MAX);
    n->type = type;
    n->refs = 1;
    list_init(&n->children);
    list_init(&n->node);
    return n;
}

void vfs_node_free(struct vfs_node *n)
{
    if (!n) {
        return;
    }
    if (n->ops.release) {
        n->ops.release(n);
    }
    kfree(n);
}

void vfs_attach(struct vfs_node *parent, struct vfs_node *child)
{
    if (!parent || !child) {
        return;
    }
    child->parent = parent;
    list_add_tail(&child->node, &parent->children);
}

int vfs_mount(const char *path, struct vfs_node *root)
{
    struct vfs_mount *m;

    if (!root) {
        return -1;
    }
    m = (struct vfs_mount *)kzalloc(sizeof(*m));
    if (!m) {
        return -1;
    }
    strlcpy(m->path, path, sizeof(m->path));
    m->root = root;
    list_add_tail(&m->node, &mounts);
    pr_info("vfs: mounted %s\n", path);
    return 0;
}

int vfs_unmount(const char *path)
{
    struct vfs_mount *m, *tmp;
    list_for_each_entry_safe(m, tmp, &mounts, node)
    {
        if (!strcmp(m->path, path)) {
            list_del(&m->node);
            vfs_node_free(m->root);
            kfree(m);
            return 0;
        }
    }
    return -1;
}

static struct vfs_mount *mount_for(const char *path, const char **rest)
{
    struct vfs_mount *m, *best = 0;
    size_t best_len = 0, len;

    list_for_each_entry(m, &mounts, node)
    {
        len = strlen(m->path);
        if (!strncmp(path, m->path, len) && len >= best_len) {
            best = m;
            best_len = len;
        }
    }
    if (best) {
        const char *r = path + best_len;
        while (*r == '/') {
            r++;
        }
        *rest = r;
    }
    return best;
}

static struct vfs_node *walk(struct vfs_node *dir, const char *path)
{
    char component[VFS_NAME_MAX];
    struct vfs_node *cur = dir;

    while (*path && cur) {
        size_t n = 0;
        while (*path == '/') {
            path++;
        }
        if (!*path) {
            break;
        }
        while (*path && *path != '/') {
            if (n + 1 < sizeof(component)) {
                component[n++] = *path;
            }
            path++;
        }
        component[n] = 0;
        if (!cur->ops.lookup) {
            return 0;
        }
        cur = cur->ops.lookup(cur, component);
    }
    return cur;
}

struct vfs_node *vfs_lookup(const char *path)
{
    const char *rest = path;
    struct vfs_mount *m = mount_for(path, &rest);
    if (!m) {
        return 0;
    }
    if (!*rest) {
        return m->root;
    }
    return walk(m->root, rest);
}

int vfs_read_file(const char *path, void *buf, u32 max, u32 *out_len)
{
    struct vfs_node *n = vfs_lookup(path);
    int r;
    if (!n) {
        return ENOENT;
    }
    if (n->type != VFS_FILE || !n->ops.read) {
        return EINVAL;
    }
    if (n->size < max) {
        max = (u32)n->size;
    }
    r = n->ops.read(n, 0, buf, max);
    if (r < 0) {
        return r;
    }
    if (out_len) {
        *out_len = (u32)r;
    }
    return 0;
}

int vfs_write_file(const char *path, const void *buf, u32 len)
{
    struct vfs_node *n = vfs_lookup(path);
    if (!n) {
        return ENOENT;
    }
    if (n->type != VFS_FILE || !n->ops.write) {
        return EINVAL;
    }
    return n->ops.write(n, 0, buf, len);
}

int vfs_stat(const char *path, u32 *type, u64 *size)
{
    struct vfs_node *n = vfs_lookup(path);
    if (!n) {
        return ENOENT;
    }
    if (type) {
        *type = n->type;
    }
    if (size) {
        *size = n->size;
    }
    return 0;
}

void vfs_list(const char *path)
{
    struct vfs_node *n = vfs_lookup(path);
    u32 i = 0, type = 0, size = 0;
    char name[VFS_NAME_MAX];

    if (!n) {
        printk("ls: %s: not found\n", path);
        return;
    }
    if (n->type != VFS_DIR) {
        printk("%-32s %10u\n", n->name, (u32)n->size);
        return;
    }
    if (!n->ops.readdir) {
        printk("ls: %s: not a directory\n", path);
        return;
    }
    while (n->ops.readdir(n, i++, name, sizeof(name), &size, &type) == 0) {
        printk("%c %-30s %10u\n", type == VFS_DIR ? 'd' : '-', name, size);
    }
}

void vfs_dump(void)
{
    struct vfs_mount *m;
    printk("vfs: mounts:\n");
    list_for_each_entry(m, &mounts, node)
    {
        printk("  %-20s -> %s\n", m->path, m->root ? m->root->name : "?");
    }
}
