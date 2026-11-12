/*
 * SurfaceRT_OS - service manager and loadable module loader
 */
#include <srtos/svc.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/vfs.h>
#include <srtos/net.h>
#include <srtos/sched.h>
#include <srtos/clock.h>
#include <srtos/string.h>
#include <srtos/fs.h>
#include <srtos/fb.h>

#define SVC_MAX 12
#define MODULE_LOAD_BASE 0x90000000u

static struct srt_service *services[SVC_MAX];
static int nr_services;
static struct srt_module modules[SRT_MODULE_MAX];
static int nr_modules;
static struct srt_host_api host_api;

static u32 lcg_state = 0x12345678u;

static u32 api_random(void)
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state;
}

static int api_fb_info(struct srt_frame_info *info)
{
    if (!info) {
        return -1;
    }
    info->width = fb_width();
    info->height = fb_height();
    info->stride = fb_stride();
    info->bpp = 32;
    return 0;
}

static int api_fb_blit(u32 x, u32 y, u32 w, u32 h, const void *pixels, u32 stride)
{
    return fb_blit(x, y, w, h, pixels, stride);
}

static void api_fb_fill(u32 x, u32 y, u32 w, u32 h, u32 argb)
{
    fb_fill_rect(x, y, w, h, argb);
}

static int api_udp_open(u16 port, void *cb, void *arg)
{
    return udp_socket_open(port, (udp_rx_fn)cb, arg);
}

static u32 api_net_ip(const char *ifname)
{
    struct net_device *nd = net_get(ifname);
    return nd ? nd->ip : 0;
}

static int api_thread_create(const char *name, void (*fn)(void *), void *arg,
                             u32 stack)
{
    return thread_create(name, fn, arg, stack) ? 0 : -1;
}

static void build_host_api(void)
{
    memset(&host_api, 0, sizeof(host_api));
    host_api.abi_version = SRT_MODULE_ABI;
    host_api.size = sizeof(host_api);
    host_api.printk = printk;
    host_api.kmalloc = kmalloc;
    host_api.kzalloc = kzalloc;
    host_api.kfree = kfree;
    host_api.msleep = msleep;
    host_api.uptime_ms = clock_uptime_ms;
    host_api.random = api_random;
    host_api.fb_info = api_fb_info;
    host_api.fb_blit = api_fb_blit;
    host_api.fb_fill = api_fb_fill;
    host_api.udp_open = api_udp_open;
    host_api.udp_close = udp_socket_close;
    host_api.udp_send = udp_send;
    host_api.net_ip = api_net_ip;
    host_api.vfs_read = vfs_read_file;
    host_api.vfs_write = vfs_write_file;
    host_api.thread_create = api_thread_create;
}

void svc_manager_init(void)
{
    nr_services = 0;
    nr_modules = 0;
    memset(modules, 0, sizeof(modules));
    build_host_api();
    disp_sink_register();
    pr_info("svc: manager ready (module ABI %d, host API %u bytes)\n",
            SRT_MODULE_ABI, (u32)sizeof(host_api));
}

struct srt_host_api *svc_host_api(void)
{
    return &host_api;
}

int svc_register(struct srt_service *svc)
{
    if (!svc || nr_services >= SVC_MAX) {
        return -1;
    }
    services[nr_services++] = svc;
    return 0;
}

int svc_start(const char *name)
{
    int i;
    for (i = 0; i < nr_services; i++) {
        if (!strcmp(services[i]->name, name)) {
            if (services[i]->running) {
                return 0;
            }
            if (services[i]->init && services[i]->init() != 0) {
                pr_err("svc: %s failed to start\n", name);
                return -1;
            }
            services[i]->running = 1;
            pr_info("svc: %s %s started\n", services[i]->name, services[i]->version);
            return 0;
        }
    }
    return -1;
}

int svc_start_all(void)
{
    int i, ok = 0;
    for (i = 0; i < nr_services; i++) {
        if (svc_start(services[i]->name) == 0) {
            ok++;
        }
    }
    return ok;
}

void svc_list(void)
{
    int i;
    printk("built in services:\n");
    for (i = 0; i < nr_services; i++) {
        printk("  %-12s %-8s %s\n", services[i]->name, services[i]->version,
               services[i]->running ? "running" : "stopped");
    }
    printk("loadable modules: %d\n", nr_modules);
    for (i = 0; i < nr_modules; i++) {
        printk("  %-12s abi %u %s\n", modules[i].name, modules[i].abi,
               modules[i].running ? "running" : "loaded");
    }
}

void svc_dump(void)
{
    svc_list();
}

int svc_module_count(void)
{
    return nr_modules;
}

struct srt_module *svc_module_at(int idx)
{
    return (idx >= 0 && idx < nr_modules) ? &modules[idx] : 0;
}

/* ------------------------------------------------------------ ELF loader --- */
#define ET_EXEC 2
#define EM_ARM 40
#define PT_LOAD 1
#define SHT_REL 9
#define SHT_SYMTAB 2
#define R_ARM_ABS32 2
#define R_ARM_RELATIVE 23

typedef struct {
    u8 ident[16];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_ehdr;

typedef struct {
    u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
} elf_phdr;

typedef struct {
    u32 name, type, flags, addr, offset, size, link, info, addralign, entsize;
} elf_shdr;

typedef struct {
    u32 name, value, size, info, other, shndx;
} elf_sym;

typedef struct {
    u32 offset, info;
} elf_rel;

static int module_symbol_value(const u8 *img, u32 size, const char *want,
                               u32 *value)
{
    const elf_ehdr *eh = (const elf_ehdr *)img;
    const elf_shdr *sh;
    u32 i;

    sh = (const elf_shdr *)(img + eh->shoff);
    for (i = 0; i < eh->shnum; i++) {
        if (sh[i].type != SHT_SYMTAB) {
            continue;
        }
        {
            const elf_sym *syms = (const elf_sym *)(img + sh[i].offset);
            u32 n = sh[i].size / sizeof(elf_sym);
            const char *strtab = (const char *)(img + sh[sh[i].link].offset);
            u32 j;
            for (j = 0; j < n; j++) {
                if (!strcmp(strtab + syms[j].name, want) && syms[j].shndx) {
                    *value = syms[j].value;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static int __unused module_relocate(u8 *img, u32 size, u32 bias)
{
    const elf_ehdr *eh = (const elf_ehdr *)img;
    const elf_shdr *sh;
    u32 i;

    if (!bias || !eh->shoff || !eh->shnum) {
        return 0;
    }
    if (eh->shoff + eh->shnum * sizeof(elf_shdr) > size) {
        return -1;
    }
    sh = (const elf_shdr *)(img + eh->shoff);
    for (i = 0; i < eh->shnum; i++) {
        const elf_rel *rels;
        u32 n, j;
        if (sh[i].type != SHT_REL || !sh[i].size) {
            continue;
        }
        if (sh[i].offset + sh[i].size > size) {
            return -1;
        }
        rels = (const elf_rel *)(img + sh[i].offset);
        n = sh[i].size / sizeof(elf_rel);
        for (j = 0; j < n; j++) {
            u32 type = rels[j].info & 0xFF;
            u32 target = rels[j].offset;
            if (target + 4 > size) {
                return -1;
            }
            if (type == R_ARM_ABS32 || type == R_ARM_RELATIVE) {
                *(u32 *)(img + target) += bias;
            } else {
                pr_err("svc: unsupported module relocation %u\n", type);
                return -1;
            }
        }
    }
    return 0;
}

int svc_load_module(const char *path)
{
    u32 file_size = 0;
    u8 *file;
    const elf_ehdr *eh;
    u8 *image = 0;
    u32 span_start, span_end, pages, bias, entry_off;
    struct srt_module *m;
    int rc = -1;

    if (nr_modules >= SRT_MODULE_MAX) {
        pr_err("svc: module table full\n");
        return -1;
    }
    file = (u8 *)kmalloc(256 * 1024);
    if (!file) {
        return -1;
    }
    if (vfs_read_file(path, file, 256 * 1024, &file_size) != 0 || file_size < 64) {
        pr_err("svc: cannot read %s\n", path);
        kfree(file);
        return -1;
    }
    eh = (const elf_ehdr *)file;
    if (memcmp(eh->ident, "\x7f" "ELF", 4) || eh->machine != EM_ARM ||
        eh->type != ET_EXEC || eh->phentsize != sizeof(elf_phdr)) {
        pr_err("svc: %s is not an ARM ELF image\n", path);
        kfree(file);
        return -1;
    }

    span_start = 0xFFFFFFFFu;
    span_end = 0;
    {
        const elf_phdr *ph = (const elf_phdr *)(file + eh->phoff);
        u32 i;
        for (i = 0; i < eh->phnum; i++) {
            if (ph[i].type != PT_LOAD || !ph[i].memsz) {
                continue;
            }
            if (ph[i].vaddr < span_start) {
                span_start = ph[i].vaddr;
            }
            if (ph[i].vaddr + ph[i].memsz > span_end) {
                span_end = ph[i].vaddr + ph[i].memsz;
            }
        }
    }
    if (span_start >= span_end) {
        pr_err("svc: %s has no loadable segments\n", path);
        kfree(file);
        return -1;
    }
    pages = (span_end - span_start + PAGE_SIZE - 1) / PAGE_SIZE;
    image = (u8 *)pmm_alloc_pages(pages);
    if (!image) {
        pr_err("svc: out of memory loading %s\n", path);
        kfree(file);
        return -1;
    }
    memset(image, 0, pages * PAGE_SIZE);
    bias = (u32)(uptr)image - span_start;
    {
        const elf_phdr *ph = (const elf_phdr *)(file + eh->phoff);
        u32 i;
        for (i = 0; i < eh->phnum; i++) {
            if (ph[i].type != PT_LOAD || !ph[i].memsz) {
                continue;
            }
            if (ph[i].offset + ph[i].filesz > file_size) {
                goto out;
            }
            memcpy(image + (ph[i].vaddr - span_start) , file + ph[i].offset,
                   ph[i].filesz);
            if (ph[i].memsz > ph[i].filesz) {
                memset(image + (ph[i].vaddr - span_start) + ph[i].filesz, 0,
                       ph[i].memsz - ph[i].filesz);
            }
        }
    }

    /* Relocations are expressed against the link address, so shift the image
     * window: relocate() expects offsets relative to span_start. */
    {
        const elf_shdr *sh = (const elf_shdr *)(file + eh->shoff);
        u32 i;
        for (i = 0; i < eh->shnum; i++) {
            const elf_rel *rels;
            u32 n, j;
            if (sh[i].type != SHT_REL || !sh[i].size || bias == span_start) {
                continue;
            }
            rels = (const elf_rel *)(file + sh[i].offset);
            n = sh[i].size / sizeof(elf_rel);
            for (j = 0; j < n; j++) {
                u32 type = rels[j].info & 0xFF;
                u32 target = rels[j].offset - span_start;
                if (type == R_ARM_ABS32 || type == R_ARM_RELATIVE) {
                    if (target + 4 <= pages * PAGE_SIZE) {
                        *(u32 *)(image + target) += bias;
                    }
                } else {
                    pr_err("svc: unsupported relocation %u in %s\n", type, path);
                    goto out;
                }
            }
        }
    }

    if (module_symbol_value(file, file_size, "srt_module_init", &entry_off) != 0) {
        pr_err("svc: %s does not export srt_module_init\n", path);
        goto out;
    }

    m = &modules[nr_modules];
    memset(m, 0, sizeof(*m));
    {
        const char *base = strrchr(path, '/');
        strlcpy(m->name, base ? base + 1 : path, SRT_MODULE_NAME_MAX);
        /* strip the .svc suffix for display purposes */
        {
            char *dot = strrchr(m->name, '.');
            if (dot) {
                *dot = 0;
            }
        }
    }
    strlcpy(m->path, path, sizeof(m->path));
    m->abi = SRT_MODULE_ABI;
    m->image = image;
    m->image_size = pages * PAGE_SIZE;
    m->entry = image + entry_off - span_start;

    {
        typedef int (*mod_init_fn)(const struct srt_host_api *api, u32 abi);
        mod_init_fn init = (mod_init_fn)m->entry;
        int r = init(&host_api, SRT_MODULE_ABI);
        if (r != 0) {
            pr_err("svc: %s init returned %d\n", m->name, r);
            goto out;
        }
    }
    m->running = 1;
    nr_modules++;
    pr_info("svc: loaded module %s (%u KiB, entry 0x%08x)\n", m->name,
            m->image_size / 1024, (u32)(uptr)m->entry);
    rc = 0;
out:
    if (rc != 0 && image) {
        pmm_free_pages(image, pages);
    }
    kfree(file);
    return rc;
}

int svc_load_all(void)
{
    struct vfs_node *dir = vfs_lookup("/services");
    u32 i = 0, type = 0, size = 0;
    char name[64];
    int loaded = 0;

    if (!dir) {
        pr_info("svc: no /services directory\n");
        return 0;
    }
    while (dir->ops.readdir &&
           dir->ops.readdir(dir, i++, name, sizeof(name), &size, &type) == 0) {
        char path[96];
        if (type != VFS_FILE) {
            continue;
        }
        snprintf(path, sizeof(path), "/services/%s", name);
        if (svc_load_module(path) == 0) {
            loaded++;
        }
    }
    return loaded;
}

int svc_unload_module(const char *name)
{
    int i;
    for (i = 0; i < nr_modules; i++) {
        if (!strcmp(modules[i].name, name)) {
            /* Services are kernel threads; there is no safe way to reclaim a
             * running thread's stack yet, so we only allow unloading modules
             * that never started a thread of their own. */
            if (modules[i].running) {
                pr_warn("svc: %s is still running\n", name);
                return -1;
            }
            pmm_free_pages(modules[i].image,
                           modules[i].image_size / PAGE_SIZE);
            for (; i < nr_modules - 1; i++) {
                modules[i] = modules[i + 1];
            }
            nr_modules--;
            return 0;
        }
    }
    return -1;
}
