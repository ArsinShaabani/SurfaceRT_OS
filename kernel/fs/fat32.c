/*
 * SurfaceRT_OS - FAT32 driver
 *
 * Read support is complete (boot sector, FAT chains, cluster runs, 8.3 names).
 * Writes are allowed but conservative: a file may be rewritten in place as long
 * as it does not need to grow, which covers logs and configuration on the EFI
 * system partition without any risk of corrupting a Windows install that shares
 * the volume.
 *
 * Every name shipped on the SRTOS image is 8.3 clean (KERNEL.ELF, BOOT.CFG,
 * SRTOS/...), which keeps both this driver and scripts/mkimage.py simple.
 */
#include <srtos/fs.h>
#include <srtos/vfs.h>
#include <srtos/block.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/irq.h>
#include <srtos/string.h>

#define FAT_EOC 0x0FFFFFF8u
#define ATTR_VOLUME 0x08
#define ATTR_DIR 0x10
#define ATTR_LFN 0x0F
#define ATTR_ARCHIVE 0x20
#define MAX_FAT_MOUNTS 4

struct fat32_fs {
    struct block_device *bd;
    char mountpoint[32];
    u32 sector_size;
    u32 sectors_per_cluster;
    u32 cluster_size;
    u32 reserved_sectors;
    u32 num_fats;
    u32 fat_size;
    u32 root_cluster;
    u32 total_clusters;
    u64 fat_lba;
    u64 data_lba;
    int writable;
    u8 *window;          /* one cluster scratch buffer */
    spinlock_t lock;
    char label[12];
};

static struct fat32_fs *g_mounts[MAX_FAT_MOUNTS];
static int g_nr_mounts;

static inline u64 cluster_lba(struct fat32_fs *fs, u32 cluster)
{
    return fs->data_lba + (u64)(cluster - 2) * fs->sectors_per_cluster;
}

static inline u32 sectors_per_block(struct fat32_fs *fs)
{
    return fs->sector_size / 512u;
}

static int fat_read_sector(struct fat32_fs *fs, u64 lba, u8 *buf)
{
    return fs->bd->read(fs->bd, lba, sectors_per_block(fs), buf);
}

static int __unused fat_write_sector(struct fat32_fs *fs, u64 lba, u8 *buf)
{
    if (!fs->writable) {
        return -1;
    }
    return fs->bd->write(fs->bd, lba, sectors_per_block(fs), buf);
}

static int fat_next_cluster(struct fat32_fs *fs, u32 cluster, u32 *next)
{
    u8 sector[512];
    u64 off = (u64)cluster * 4u;
    u64 lba = fs->fat_lba + (off / fs->sector_size);
    u32 in_sector = (u32)(off % fs->sector_size);

    if (fs->sector_size > sizeof(sector)) {
        return -1;
    }
    if (fat_read_sector(fs, lba, sector) != 0) {
        return -1;
    }
    *next = readl((uptr)sector + in_sector) & 0x0FFFFFFFu;
    return 0;
}

static int fat_load_cluster(struct fat32_fs *fs, u32 cluster)
{
    return fs->bd->read(fs->bd, cluster_lba(fs, cluster), fs->sectors_per_cluster,
                        fs->window);
}

static void fat_name_to_str(const u8 *entry, char *out, u32 out_len)
{
    u32 n = 0, i;
    for (i = 0; i < 8 && entry[i] != ' '; i++) {
        if (n + 1 < out_len) {
            out[n++] = (char)tolower(entry[i]);
        }
    }
    if (entry[8] != ' ' && entry[8] != 0) {
        if (n + 1 < out_len) {
            out[n++] = '.';
        }
        for (i = 8; i < 11 && entry[i] != ' '; i++) {
            if (n + 1 < out_len) {
                out[n++] = (char)tolower(entry[i]);
            }
        }
    }
    out[n] = 0;
}

static int fat_name_match(const u8 *entry, const char *name)
{
    char fat[16], want[16];
    u32 i;
    for (i = 0; i < sizeof(want) - 1 && name[i]; i++) {
        want[i] = (char)tolower((unsigned char)name[i]);
    }
    want[i] = 0;
    fat_name_to_str(entry, fat, sizeof(fat));
    return !strcmp(fat, want);
}

static u32 entry_cluster(const u8 *e)
{
    return ((u32)readw((uptr)e + 20) << 16) | readw((uptr)e + 26);
}

static u32 entry_size(const u8 *e)
{
    return readl((uptr)e + 28);
}

/* Walk a directory's clusters and return the entry matching `name`.
 * `dir_cluster` 0 means "the root directory" of a FAT32 volume. */
static int fat_find(struct fat32_fs *fs, u32 dir_cluster, const char *name,
                    u8 *out_entry)
{
    u32 cluster = dir_cluster ? dir_cluster : fs->root_cluster;
    u32 guard = 0;

    while (cluster >= 2 && cluster < FAT_EOC && guard++ < 0x100000u) {
        u32 off;
        if (fat_load_cluster(fs, cluster) != 0) {
            return -1;
        }
        for (off = 0; off + 32 <= fs->cluster_size; off += 32) {
            u8 *e = fs->window + off;
            if (e[0] == 0) {
                return -1; /* end of directory */
            }
            if (e[0] == 0xE5 || (e[11] & ATTR_LFN) == ATTR_LFN ||
                (e[11] & ATTR_VOLUME)) {
                continue;
            }
            if (fat_name_match(e, name)) {
                memcpy(out_entry, e, 32);
                return 0;
            }
        }
        {
            u32 next;
            if (fat_next_cluster(fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
        }
    }
    return -1;
}

static int fat_read_chain(struct fat32_fs *fs, u32 first, u64 offset, void *buf,
                          u32 len)
{
    u32 cluster = first;
    u8 *dst = (u8 *)buf;

    while (len && cluster >= 2 && cluster < FAT_EOC) {
        if (offset >= fs->cluster_size) {
            u32 next;
            offset -= fs->cluster_size;
            if (fat_next_cluster(fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
            continue;
        }
        if (fat_load_cluster(fs, cluster) != 0) {
            return -1;
        }
        {
            u32 chunk = fs->cluster_size - (u32)offset;
            if (chunk > len) {
                chunk = len;
            }
            memcpy(dst, fs->window + offset, chunk);
            dst += chunk;
            len -= chunk;
            offset = 0;
        }
        if (len) {
            u32 next;
            if (fat_next_cluster(fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
        }
    }
    return 0;
}

static int fat_write_chain(struct fat32_fs *fs, u32 first, u64 offset,
                           const void *buf, u32 len)
{
    u32 cluster = first;
    const u8 *src = (const u8 *)buf;

    while (len && cluster >= 2 && cluster < FAT_EOC) {
        if (offset >= fs->cluster_size) {
            u32 next;
            offset -= fs->cluster_size;
            if (fat_next_cluster(fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
            continue;
        }
        if (fat_load_cluster(fs, cluster) != 0) {
            return -1;
        }
        {
            u32 chunk = fs->cluster_size - (u32)offset;
            if (chunk > len) {
                chunk = len;
            }
            memcpy(fs->window + offset, src, chunk);
            if (fs->bd->write(fs->bd, cluster_lba(fs, cluster),
                              fs->sectors_per_cluster, fs->window) != 0) {
                return -1;
            }
            src += chunk;
            len -= chunk;
            offset = 0;
        }
        if (len) {
            u32 next;
            if (fat_next_cluster(fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
        }
    }
    return len ? -1 : 0;
}

/* ------------------------------------------------------------- vfs glue --- */
struct fat_node_priv {
    struct fat32_fs *fs;
    u32 cluster;
    u32 size;
    u32 is_dir;
};

static int fat_vfs_read(struct vfs_node *n, u64 off, void *buf, u32 len)
{
    struct fat_node_priv *p = (struct fat_node_priv *)n->priv;
    if (!p || off >= n->size) {
        return 0;
    }
    if (off + len > n->size) {
        len = (u32)(n->size - off);
    }
    if (fat_read_chain(p->fs, p->cluster, off, buf, len) != 0) {
        return -1;
    }
    return (int)len;
}

static int fat_vfs_write(struct vfs_node *n, u64 off, const void *buf, u32 len)
{
    struct fat_node_priv *p = (struct fat_node_priv *)n->priv;
    if (!p || off + len > n->size) {
        return -1; /* growing files is intentionally unsupported */
    }
    return fat_write_chain(p->fs, p->cluster, off, buf, len);
}

static struct vfs_node *fat_make_node(struct fat32_fs *fs, const u8 *entry)
{
    struct vfs_node *n;
    struct fat_node_priv *p;
    char name[VFS_NAME_MAX];

    fat_name_to_str(entry, name, sizeof(name));
    n = vfs_node_alloc(name, (entry[11] & ATTR_DIR) ? VFS_DIR : VFS_FILE);
    if (!n) {
        return 0;
    }
    p = (struct fat_node_priv *)kzalloc(sizeof(*p));
    if (!p) {
        vfs_node_free(n);
        return 0;
    }
    p->fs = fs;
    p->cluster = entry_cluster(entry);
    p->size = entry_size(entry);
    p->is_dir = (entry[11] & ATTR_DIR) ? 1 : 0;
    n->priv = p;
    n->size = p->size;
    n->ops.read = p->is_dir ? 0 : fat_vfs_read;
    n->ops.write = p->is_dir ? 0 : fat_vfs_write;
    return n;
}

static struct vfs_node *fat_vfs_lookup(struct vfs_node *dir, const char *name)
{
    struct fat_node_priv *p = (struct fat_node_priv *)dir->priv;
    u8 entry[32];
    if (!p || !p->is_dir) {
        return 0;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        return dir;
    }
    if (fat_find(p->fs, p->cluster, name, entry) != 0) {
        return 0;
    }
    return fat_make_node(p->fs, entry);
}

static int fat_vfs_readdir(struct vfs_node *dir, u32 idx, char *name,
                           u32 name_len, u32 *size, u32 *type)
{
    struct fat_node_priv *p = (struct fat_node_priv *)dir->priv;
    u32 cluster, guard = 0, seen = 0;

    if (!p || !p->is_dir) {
        return -1;
    }
    cluster = p->cluster ? p->cluster : p->fs->root_cluster;
    while (cluster >= 2 && cluster < FAT_EOC && guard++ < 0x100000u) {
        u32 off;
        if (fat_load_cluster(p->fs, cluster) != 0) {
            return -1;
        }
        for (off = 0; off + 32 <= p->fs->cluster_size; off += 32) {
            u8 *e = p->fs->window + off;
            if (e[0] == 0) {
                return -1;
            }
            if (e[0] == 0xE5 || (e[11] & ATTR_LFN) == ATTR_LFN ||
                (e[11] & ATTR_VOLUME)) {
                continue;
            }
            if (seen++ != idx) {
                continue;
            }
            fat_name_to_str(e, name, name_len);
            if (size) {
                *size = entry_size(e);
            }
            if (type) {
                *type = (e[11] & ATTR_DIR) ? VFS_DIR : VFS_FILE;
            }
            return 0;
        }
        {
            u32 next;
            if (fat_next_cluster(p->fs, cluster, &next) != 0) {
                return -1;
            }
            cluster = next;
        }
    }
    return -1;
}

static void fat_vfs_release(struct vfs_node *n)
{
    if (n->priv) {
        kfree(n->priv);
        n->priv = 0;
    }
}

int fat32_mount(struct block_device *bd, u64 start_lba, const char *mountpoint)
{
    struct fat32_fs *fs;
    u8 *bs;
    struct vfs_node *root;
    struct fat_node_priv *rp;
    u32 total_sectors, fat_sectors, i;

    if (!bd || g_nr_mounts >= MAX_FAT_MOUNTS) {
        return -1;
    }
    bs = (u8 *)kmalloc(512);
    if (!bs) {
        return -1;
    }
    if (bd->read(bd, start_lba, 1, bs) != 0) {
        kfree(bs);
        return -1;
    }
    if (bs[510] != 0x55 || bs[511] != 0xAA || bs[0] == 0) {
        kfree(bs);
        return -1;
    }

    fs = (struct fat32_fs *)kzalloc(sizeof(*fs));
    if (!fs) {
        kfree(bs);
        return -1;
    }
    fs->bd = bd;
    fs->sector_size = readw((uptr)bs + 11);
    fs->sectors_per_cluster = bs[13];
    fs->reserved_sectors = readw((uptr)bs + 14);
    fs->num_fats = bs[16];
    fat_sectors = readw((uptr)bs + 22);
    total_sectors = readl((uptr)bs + 32);

    if (fs->sector_size != 512 || !fs->sectors_per_cluster ||
        (fs->sectors_per_cluster & (fs->sectors_per_cluster - 1)) ||
        !fs->num_fats || !readl((uptr)bs + 44)) {
        pr_warn("fat32: %s is not a FAT32 volume (bpb=%u spc=%u fats=%u)\n",
                bd->name, fs->sector_size, fs->sectors_per_cluster,
                fs->num_fats);
        kfree(fs);
        kfree(bs);
        return -1;
    }
    fs->fat_size = fat_sectors ? fat_sectors : readl((uptr)bs + 36);
    fs->root_cluster = readl((uptr)bs + 44);
    fs->cluster_size = fs->sector_size * fs->sectors_per_cluster;
    fs->total_clusters = total_sectors / fs->sectors_per_cluster;
    fs->fat_lba = start_lba + fs->reserved_sectors;
    fs->data_lba = fs->fat_lba + (u64)fs->fat_size * fs->num_fats;
    fs->writable = bd->write ? 1 : 0;
    fs->window = (u8 *)kmalloc(fs->cluster_size);
    if (!fs->window) {
        kfree(fs);
        kfree(bs);
        return -1;
    }
    for (i = 0; i < 11; i++) {
        fs->label[i] = (char)bs[71 + i];
    }
    fs->label[11] = 0;
    spin_lock_init(&fs->lock);
    strlcpy(fs->mountpoint, mountpoint, sizeof(fs->mountpoint));

    root = vfs_node_alloc("fat32", VFS_DIR);
    rp = (struct fat_node_priv *)kzalloc(sizeof(*rp));
    if (!root || !rp) {
        kfree(fs->window);
        kfree(fs);
        kfree(bs);
        return -1;
    }
    rp->fs = fs;
    rp->cluster = fs->root_cluster;
    rp->is_dir = 1;
    root->priv = rp;
    root->ops.lookup = fat_vfs_lookup;
    root->ops.readdir = fat_vfs_readdir;
    root->ops.release = fat_vfs_release;

    g_mounts[g_nr_mounts++] = fs;
    vfs_mount(mountpoint, root);
    pr_info("fat32: %s%s mounted at %s (%u KiB clusters, label \"%s\")\n",
            bd->name, mountpoint[1] ? "" : "", mountpoint, fs->cluster_size / 1024,
            fs->label);
    kfree(bs);
    return 0;
}

static int fat32_try_device(struct block_device *bd)
{
    int n, i, mounted = 0;

    if (fat32_mount(bd, 0, "/") == 0) {
        return 0; /* superfloppy: the file system starts at LBA 0 */
    }
    n = part_scan(bd);
    for (i = 0; i < n; i++) {
        struct srt_partition *p = part_get(i);
        if (!p) {
            continue;
        }
        if (!mounted && (p->esp || p->index == 1)) {
            if (fat32_mount(bd, p->start_lba, "/") == 0) {
                mounted = 1;
                continue;
            }
        }
        /* Any further FAT partition is exported at /install so that the
         * installer can find the disk image that ships on the USB stick. */
        if (mounted) {
            if (fat32_mount(bd, p->start_lba, "/install") == 0) {
                pr_info("fat32: partition %d mounted at /install\n", p->index);
                return 0;
            }
        }
    }
    return mounted ? 0 : -1;
}

/* Mounts the first FAT32 partition found, preferring the internal eMMC. */
int fat32_mount_root(void)
{
    int i, n = block_count();

    /* eMMC first, then microSD, then USB mass storage */
    for (i = 0; i < n; i++) {
        struct block_device *bd = block_get("mmcblk0");
        if (bd && fat32_try_device(bd) == 0) {
            return 0;
        }
        break;
    }
    for (i = 0; i < n; i++) {
        struct block_device *bd = block_get_index(i);
        if (bd && fat32_try_device(bd) == 0) {
            return 0;
        }
    }
    return -1;
}

int fat32_unmount_all(void)
{
    int i;
    for (i = 0; i < g_nr_mounts; i++) {
        vfs_unmount(g_mounts[i]->mountpoint);
        kfree(g_mounts[i]->window);
        kfree(g_mounts[i]);
        g_mounts[i] = 0;
    }
    g_nr_mounts = 0;
    return 0;
}

void fat32_dump(void)
{
    int i;
    for (i = 0; i < g_nr_mounts; i++) {
        struct fat32_fs *fs = g_mounts[i];
        printk("fat32: %s at %s, %u sectors/cluster, root cluster %u\n",
               fs->bd->name, fs->mountpoint, fs->sectors_per_cluster,
               fs->root_cluster);
    }
}
