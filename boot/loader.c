/*
 * SurfaceRT_OS - UEFI loader for the Surface RT (NVIDIA Tegra 3 / ARMv7-A)
 *
 * Responsibilities, in order:
 *   1. talk to the firmware (console, GOP, SimpleFileSystem)
 *   2. read /SRTOS/BOOT.CFG and /SRTOS/KERNEL.ELF from the EFI system partition
 *   3. place the kernel at a page aligned physical address and relocate it
 *   4. build a self contained srt_bootinfo (memory map, framebuffer, clocks)
 *   5. ExitBootServices() and hand over to the kernel
 *
 * After ExitBootServices nothing here may call firmware code any more, so all
 * diagnostics from that point on go straight to the Tegra UART register block.
 */
#include "efi.h"
#include "elfload.h"
#include <srtos/bootinfo.h>
#include <srtos/string.h>

#define LOADER_VERSION "srtos-loader 1.0"
#define BOOT_DIR L"\\SRTOS"
#define DEFAULT_KERNEL_BASE 0x82000000ull

/* Tegra 3 UART-A (debug console).  Clocked at 2.6 MHz by the bootloader. */
#define TEGRA_UART_A 0x70006000u
#define UART_THR 0x00
#define UART_LSR 0x14
#define UART_LSR_THRE 0x20

static efi_system_table *g_st;
static efi_boot_services *g_bs;
static efi_simple_text_output *g_con;
static efi_handle g_image_handle;
static struct srt_framebuffer g_fb;
static u32 g_uart_ready;

/* ------------------------------------------------------------------ output --- */
static void uart_init(void)
{
    /* The firmware normally leaves UART-A running; we only need the data port
     * and probe LSR to decide whether it is usable. */
    volatile u32 *lsr = (volatile u32 *)(uintptr_t)(TEGRA_UART_A + UART_LSR);
    /* read LSR twice: an unclocked block returns 0 */
    if (lsr[0] == 0 && lsr[0] == 0) {
        g_uart_ready = 0;
        return;
    }
    g_uart_ready = 1;
}

static void uart_putc(char c)
{
    volatile u32 *thr;
    volatile u32 *lsr;
    int spin = 100000;
    if (!g_uart_ready) {
        return;
    }
    thr = (volatile u32 *)(uintptr_t)(TEGRA_UART_A + UART_THR);
    lsr = (volatile u32 *)(uintptr_t)(TEGRA_UART_A + UART_LSR);
    while (!(lsr[0] & UART_LSR_THRE) && spin--) {
    }
    thr[0] = (u32)(unsigned char)c;
}

static void uart_write(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void con_write_utf8(const char *s)
{
    uint16_t buf[256];
    size_t n = 0;
    const char *p = s;

    uart_write(s);
    if (!g_con) {
        return;
    }
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        uint16_t out = c;
        if (c >= 0xC0) { /* minimal UTF-8 -> UCS-2 (BMP only) */
            if (c >= 0xE0 && p[0] && p[1]) {
                out = (uint16_t)(((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) |
                                 (p[1] & 0x3F));
                p += 2;
            } else if (p[0]) {
                out = (uint16_t)(((c & 0x1F) << 6) | (p[0] & 0x3F));
                p += 1;
            }
        }
        if (n >= 250) {
            buf[n] = 0;
            g_con->output_string(g_con, buf);
            n = 0;
        }
        if (out == '\n') {
            buf[n++] = '\r';
        }
        buf[n++] = out;
    }
    if (n) {
        buf[n] = 0;
        g_con->output_string(g_con, buf);
    }
}

static void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    con_write_utf8(buf);
}

static void delay_ms(unsigned ms)
{
    if (g_bs && g_bs->stall) {
        g_bs->stall(ms * 1000);
    }
}

/* ------------------------------------------------------------------- files --- */
static efi_file *fs_root;

static int fs_mount(void)
{
    static efi_guid fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    static efi_guid li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image *li = 0;
    efi_simple_file_system *fs = 0;
    efi_status st;

    st = g_bs->handle_protocol(g_image_handle, &li_guid, (void **)&li);
    if (EFI_ERROR(st) || !li) {
        log_printf("loader: no LoadedImage protocol (0x%llx)\n", st);
        return -1;
    }
    st = g_bs->handle_protocol(li->device_handle, &fs_guid, (void **)&fs);
    if (EFI_ERROR(st) || !fs) {
        log_printf("loader: no SimpleFileSystem on the boot device (0x%llx)\n", st);
        return -1;
    }
    st = fs->open_volume(fs, &fs_root);
    if (EFI_ERROR(st) || !fs_root) {
        log_printf("loader: cannot open the EFI system partition (0x%llx)\n", st);
        return -1;
    }
    return 0;
}

static efi_status fs_open(const uint16_t *path, efi_file **out)
{
    return fs_root->open(fs_root, out, path, EFI_OPEN_MODE_READ, 0);
}

/* Reads a whole file into pool memory.  Returns NULL on error. */
static void *fs_read_all(const uint16_t *path, size_t *out_len)
{
    static efi_guid info_guid = EFI_FILE_INFO_GUID;
    efi_file *f = 0;
    efi_file_info *info = 0;
    size_t info_size = 0;
    void *buf = 0;
    efi_status st;

    st = fs_open(path, &f);
    if (EFI_ERROR(st) || !f) {
        return 0;
    }
    st = f->get_info(f, &info_guid, &info_size, 0);
    if (st != EFI_BUFFER_TOO_SMALL && EFI_ERROR(st)) {
        f->close(f);
        return 0;
    }
    if (g_bs->allocate_pool(EFI_LOADER_DATA, info_size, (void **)&info) != EFI_SUCCESS) {
        f->close(f);
        return 0;
    }
    st = f->get_info(f, &info_guid, &info_size, info);
    if (EFI_ERROR(st)) {
        g_bs->free_pool(info);
        f->close(f);
        return 0;
    }
    info_size = (size_t)info->file_size;
    if (!info_size || g_bs->allocate_pool(EFI_LOADER_DATA, info_size, &buf) != EFI_SUCCESS) {
        g_bs->free_pool(info);
        f->close(f);
        return 0;
    }
    st = f->read(f, &info_size, buf);
    g_bs->free_pool(info);
    f->close(f);
    if (EFI_ERROR(st)) {
        g_bs->free_pool(buf);
        return 0;
    }
    *out_len = info_size;
    return buf;
}

static size_t utf8_to_ucs2(const char *in, uint16_t *out, size_t cap)
{
    size_t n = 0;
    while (*in && n + 1 < cap) {
        out[n++] = (uint16_t)(unsigned char)*in++;
    }
    out[n] = 0;
    return n;
}

/* --------------------------------------------------------------------- GOP --- */
static int gop_setup(void)
{
    static efi_guid gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    efi_graphics_output *gop = 0;
    efi_status st;
    uint32_t i, best = 0xFFFFFFFFu;

    st = g_bs->locate_protocol(&gop_guid, 0, (void **)&gop);
    if (EFI_ERROR(st) || !gop || !gop->mode) {
        log_printf("loader: no GOP, running headless on the UART\n");
        return -1;
    }
    /* Prefer the panel's native 1366x768 mode; otherwise keep the current one. */
    for (i = 0; i < gop->mode->max_mode; i++) {
        efi_gop_mode_info *mi = 0;
        size_t sz = 0;
        if (EFI_ERROR(gop->query_mode(gop, i, &sz, &mi)) || !mi) {
            continue;
        }
        if (mi->horizontal_resolution == 1366 && mi->vertical_resolution == 768) {
            best = i;
            break;
        }
    }
    if (best != 0xFFFFFFFFu && best != gop->mode->mode) {
        if (EFI_ERROR(gop->set_mode(gop, best))) {
            log_printf("loader: could not switch to 1366x768, staying at mode %u\n",
                       (unsigned)gop->mode->mode);
        }
    }
    g_fb.phys = gop->mode->frame_buffer_base;
    g_fb.width = gop->mode->info->horizontal_resolution;
    g_fb.height = gop->mode->info->vertical_resolution;
    g_fb.stride = gop->mode->info->pixels_per_scan_line;
    g_fb.bpp = 32;
    g_fb.format = (gop->mode->info->pixel_format == 1) ? SRT_FB_BGRX
                  : (gop->mode->info->pixel_format == 0) ? SRT_FB_RGBX
                  : SRT_FB_UNKNOWN;
    log_printf("loader: framebuffer %ux%u stride %u @ 0x%llx (%s)\n",
               (unsigned)g_fb.width, (unsigned)g_fb.height, (unsigned)g_fb.stride,
               (unsigned long long)g_fb.phys,
               g_fb.format == SRT_FB_BGRX ? "BGRX" : "RGBX");
    return 0;
}

/* ------------------------------------------------------------------ config --- */
struct boot_cfg {
    char kernel[64];       /* file name below \\SRTOS        */
    char cmdline[SRT_CMDLINE_MAX];
    uint64_t kernel_base;  /* preferred physical load address  */
    unsigned timeout_ms;
};

static void cfg_defaults(struct boot_cfg *c)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->kernel, "KERNEL.ELF", sizeof(c->kernel));
    strlcpy(c->cmdline, "console=fb,uart loglevel=info", sizeof(c->cmdline));
    c->kernel_base = DEFAULT_KERNEL_BASE;
    c->timeout_ms = 0;
}

static void cfg_parse(struct boot_cfg *c, const char *text)
{
    const char *p = text;
    while (*p) {
        const char *eol = p;
        char line[320];
        size_t len;
        while (*eol && *eol != '\n' && *eol != '\r') {
            eol++;
        }
        len = (size_t)(eol - p);
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = 0;
        p = *eol ? eol + 1 : eol;

        {
            char *s = line;
            char *eq;
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            if (*s == '#' || *s == ';' || !*s) {
                continue;
            }
            eq = strchr(s, '=');
            if (!eq) {
                continue;
            }
            *eq = 0;
            {
                char *k = s;
                char *v = eq + 1;
                char *end = v + strlen(v);
                while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
                    *--end = 0;
                }
                while (*v == ' ' || *v == '\t') {
                    v++;
                }
                if (!strcasecmp(k, "kernel")) {
                    strlcpy(c->kernel, v, sizeof(c->kernel));
                } else if (!strcasecmp(k, "cmdline")) {
                    strlcpy(c->cmdline, v, sizeof(c->cmdline));
                } else if (!strcasecmp(k, "kernel_base")) {
                    c->kernel_base = strtoul(v, 0, 0);
                } else if (!strcasecmp(k, "timeout")) {
                    c->timeout_ms = (unsigned)strtoul(v, 0, 10) * 1000u;
                }
            }
        }
    }
}

/* --------------------------------------------------------------- allocator --- */
static int efi_alloc_pages(uintptr_t want, uintptr_t size, uintptr_t *got, void *ctx)
{
    uint64_t pages = EFI_SIZE_TO_PAGES(size);
    uint64_t addr = (uint64_t)want;
    efi_status st;
    (void)ctx;

    if (want) {
        st = g_bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &addr);
        if (!EFI_ERROR(st)) {
            *got = (uintptr_t)addr;
            return 0;
        }
        log_printf("loader: 0x%llx is busy (0x%llx), letting the firmware choose\n",
                   (unsigned long long)want, st);
    }
    addr = 0;
    st = g_bs->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA, pages, &addr);
    if (EFI_ERROR(st)) {
        return -1;
    }
    *got = (uintptr_t)addr;
    return 0;
}

/* ------------------------------------------------------------- memory map ---- */
static int memmap_fill(struct srt_bootinfo *bi)
{
    size_t size = 0, keysize = 0, descsize = 0;
    uint32_t key = 0, i;
    efi_memory_descriptor *map = 0;
    efi_status st;

    st = g_bs->get_memory_map(&size, 0, &keysize, &descsize, &key);
    if (st != EFI_BUFFER_TOO_SMALL) {
        return -1;
    }
    size += 2 * descsize;
    if (g_bs->allocate_pool(EFI_LOADER_DATA, size, (void **)&map) != EFI_SUCCESS) {
        return -1;
    }
    st = g_bs->get_memory_map(&size, map, &keysize, &descsize, &key);
    if (EFI_ERROR(st)) {
        g_bs->free_pool(map);
        return -1;
    }
    bi->mem_region_count = 0;
    for (i = 0; (size_t)i < size / descsize &&
                bi->mem_region_count < SRT_MAX_MEM_REGIONS; i++) {
        efi_memory_descriptor *d = (efi_memory_descriptor *)((char *)map +
                                                           (size_t)i * descsize);
        struct srt_mem_region *r = &bi->mem[bi->mem_region_count];
        uint32_t type;
        switch (d->type) {
        case 7:  /* conventional */
        case 1:  /* loader code */
        case 2:  /* loader data */
        case 3:  /* boot services code */
        case 4:  /* boot services data */
            type = SRT_MEM_USABLE;
            break;
        case 9:
            type = SRT_MEM_ACPI;
            break;
        case 5:
        case 6:
            type = SRT_MEM_RUNTIME;
            break;
        case 11:
        case 12:
            type = SRT_MEM_DEVICE;
            break;
        default:
            type = SRT_MEM_RESERVED;
            break;
        }
        if (!d->number_of_pages) {
            continue;
        }
        r->base = d->physical_start;
        r->size = d->number_of_pages * EFI_PAGE_SIZE;
        r->type = type;
        r->flags = 0;
        bi->mem_region_count++;
    }
    g_bs->free_pool(map);
    return 0;
}

static void memmap_add_reserved(struct srt_bootinfo *bi, uint64_t base, uint64_t size,
                                uint32_t type)
{
    if (bi->mem_region_count >= SRT_MAX_MEM_REGIONS) {
        return;
    }
    bi->mem[bi->mem_region_count].base = base;
    bi->mem[bi->mem_region_count].size = size;
    bi->mem[bi->mem_region_count].type = type;
    bi->mem[bi->mem_region_count].flags = 0;
    bi->mem_region_count++;
}


/* ---------------------------------------------------------- chainloading --- */
/*
 * A UEFI device path for a file on the volume we were loaded from, built by
 * copying our own FilePath and appending a MEDIA/FILEPATH node.  Used to hand
 * control to another boot manager (see the dual boot note in BOOT.CFG).
 */
static void *device_path_for(const uint16_t *file)
{
    static efi_guid li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image *li = 0;
    uint8_t *src;
    size_t src_len, name_len, total, node_len, off;
    uint8_t *out;
    static const uint8_t end_node[4] = { 0x7F, 0xFF, 0x04, 0x00 };

    if (EFI_ERROR(g_bs->handle_protocol(g_image_handle, &li_guid, (void **)&li)) ||
        !li || !li->file_path) {
        return 0;
    }
    src = (uint8_t *)li->file_path;
    /* walk to the end node of our own path */
    src_len = 0;
    for (;;) {
        uint8_t sub = src[src_len + 1];
        uint16_t len = (uint16_t)(src[src_len + 2] | (src[src_len + 3] << 8));
        src_len += len;
        if (sub == 0x7F || len < 4 || src_len > 4096) {
            break;
        }
    }
    name_len = 0;
    while (file[name_len]) {
        name_len++;
    }
    node_len = 4 + (name_len + 1) * 2;
    total = src_len + node_len + sizeof(end_node);
    out = (uint8_t *)0;
    if (g_bs->allocate_pool(EFI_LOADER_DATA, total, (void **)&out) != EFI_SUCCESS) {
        return 0;
    }
    memset(out, 0, total);
    memcpy(out, src, src_len);
    off = src_len;
    out[off + 0] = 0x04;              /* MEDIA_DEVICE_PATH   */
    out[off + 1] = 0x04;              /* MEDIA_FILEPATH_DP   */
    out[off + 2] = (uint8_t)(node_len & 0xFF);
    out[off + 3] = (uint8_t)(node_len >> 8);
    memcpy(out + off + 4, file, (name_len + 1) * 2);
    off += node_len;
    memcpy(out + off, end_node, sizeof(end_node));
    return out;
}

/* Returns 0 if the other boot manager was started (it never returns). */
static int chainload(const uint16_t *path)
{
    void *dp = device_path_for(path);
    efi_handle handle = 0;
    efi_status st;
    size_t exit_size = 0;
    void *exit_data = 0;

    if (!dp || !g_bs->load_image) {
        return -1;
    }
    st = g_bs->load_image(0, g_image_handle, dp, 0, 0, &handle);
    if (EFI_ERROR(st) || !handle) {
        log_printf("loader: cannot load the other boot manager (0x%llx)\n", st);
        return -1;
    }
    log_printf("loader: handing over to it\n");
    st = g_bs->start_image(handle, &exit_size, &exit_data);
    log_printf("loader: it returned (0x%llx)\n", st);
    return -1;
}

/* Read the 16 byte selector file; 1 = Windows boot manager requested. */
static int want_windows(void)
{
    uint16_t path[64];
    size_t len = 0;
    char *data;
    int result = 0;

    utf8_to_ucs2("\\SRTOS\\DEFAULT", path, 64);
    data = (char *)fs_read_all(path, &len);
    if (data) {
        if (len >= 7 && !strncasecmp(data, "windows", 7)) {
            result = 1;
        }
        g_bs->free_pool(data);
    }
    return result;
}

/* ------------------------------------------------------------------- main ---- */
typedef void (*kernel_entry_fn)(struct srt_bootinfo *bi, uint32_t magic);

static void load_and_jump(void)
{
    struct boot_cfg cfg;
    struct elf_image img;
    struct srt_bootinfo *bi;
    void *cfg_data, *kernel_data;
    size_t cfg_len = 0, kernel_len = 0, used;
    uint16_t path[80];
    uint64_t bi_phys = 0;
    int rc;

    cfg_defaults(&cfg);

    /* --- BOOT.CFG ------------------------------------------------------- */
    utf8_to_ucs2("\\SRTOS\\BOOT.CFG", path, 80);
    cfg_data = fs_read_all(path, &cfg_len);
    if (cfg_data) {
        cfg_parse(&cfg, (const char *)cfg_data);
        log_printf("loader: boot.cfg loaded (%u bytes)\n", (unsigned)cfg_len);
        g_bs->free_pool(cfg_data);
    } else {
        log_printf("loader: no \\SRTOS\\BOOT.CFG, using defaults\n");
    }

    if (cfg.timeout_ms) {
        delay_ms(cfg.timeout_ms);
    }

    /* Dual boot: \SRTOS\DEFAULT selects who boots.  "srtos" (or no file)
     * boots the kernel below; "windows" chainloads the original Windows boot
     * manager that install-esp.ps1 preserved as bootmgfw-windows.efi. */
    if (want_windows()) {
        uint16_t win_path[80];
        utf8_to_ucs2("\\EFI\\Microsoft\\Boot\\bootmgfw-windows.efi", win_path, 80);
        log_printf("loader: DEFAULT=windows, chainloading the Windows boot manager\n");
        if (chainload(win_path) != 0) {
            utf8_to_ucs2("\\EFI\\Microsoft\\Boot\\bootmgfw.efi", win_path, 80);
            if (chainload(win_path) != 0) {
                log_printf("loader: no Windows boot manager found, booting SRTOS\n");
            }
        }
    }

    /* --- kernel image --------------------------------------------------- */
    {
        char uname[96];
        snprintf(uname, sizeof(uname), "\\SRTOS\\%s", cfg.kernel);
        utf8_to_ucs2(uname, path, 80);
    }
    kernel_data = fs_read_all(path, &kernel_len);
    if (!kernel_data) {
        log_printf("loader: cannot read \\SRTOS\\%s - nothing to boot\n", cfg.kernel);
        return;
    }
    log_printf("loader: kernel image %u bytes\n", (unsigned)kernel_len);

    memset(&img, 0, sizeof(img));
    rc = elf_load_image(kernel_data, kernel_len, (uintptr_t)cfg.kernel_base,
                        efi_alloc_pages, 0, &img);
    if (rc != ELF_OK) {
        log_printf("loader: ELF load failed (%d)\n", rc);
        return;
    }
    log_printf("loader: kernel placed at 0x%lx (link 0x%lx) entry 0x%lx, "
               "%u relocations\n", (unsigned long)img.load_base,
               (unsigned long)img.link_base, (unsigned long)img.entry,
               (unsigned)img.relocations);

    /* --- bootinfo ------------------------------------------------------- */
    if (EFI_ERROR(g_bs->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                       EFI_SIZE_TO_PAGES(sizeof(*bi)), &bi_phys))) {
        log_printf("loader: cannot allocate bootinfo\n");
        return;
    }
    bi = (struct srt_bootinfo *)(uintptr_t)bi_phys;
    memset(bi, 0, sizeof(*bi));
    bi->magic = SRT_BOOT_MAGIC;
    bi->abi_version = SRT_BOOT_ABI;
    bi->size = sizeof(*bi);
    bi->loader_flags = 0;
    if (g_fb.phys) {
        bi->loader_flags |= SRT_BOOT_FLAG_FB_VALID;
    }
    bi->kernel_phys_base = img.load_base;
    bi->kernel_size = img.span;
    bi->kernel_link_base = img.link_base;
    bi->cpu_clock_hz = 1300000000ull; /* Tegra 3 T30: 4x 1.3 GHz          */
    bi->uart_clock_hz = 2600000ull;   /* UART-A source clock on Surface RT */
    bi->plt_clock_hz = 408000000ull;  /* peripheral (APB) clock           */
    bi->fb = g_fb;
    strlcpy(bi->cmdline, cfg.cmdline, sizeof(bi->cmdline));
    strlcpy(bi->loader_version, LOADER_VERSION, sizeof(bi->loader_version));

    if (memmap_fill(bi) != 0) {
        log_printf("loader: warning: memory map unavailable\n");
    }
    {
        uint64_t ram_base = ~0ull, ram_end = 0;
        for (used = 0; used < bi->mem_region_count; used++) {
            if (bi->mem[used].type == SRT_MEM_USABLE) {
                if (bi->mem[used].base < ram_base) {
                    ram_base = bi->mem[used].base;
                }
                if (bi->mem[used].base + bi->mem[used].size > ram_end) {
                    ram_end = bi->mem[used].base + bi->mem[used].size;
                }
            }
        }
        bi->ram_base = (ram_base == ~0ull) ? 0x80000000ull : ram_base;
        bi->ram_size = (ram_end > ram_base) ? ram_end - ram_base : 0;
    }
    memmap_add_reserved(bi, bi->kernel_phys_base, bi->kernel_size, SRT_MEM_RESERVED);
    memmap_add_reserved(bi, bi_phys, EFI_PAGE_SIZE, SRT_MEM_RESERVED);
    if (g_fb.phys) {
        memmap_add_reserved(bi, g_fb.phys,
                            (uint64_t)g_fb.stride * g_fb.height * 4,
                            SRT_MEM_FRAMEBUFFER);
    }
    log_printf("loader: %u memory regions, usable RAM 0x%llx-0x%llx\n",
               (unsigned)bi->mem_region_count, (unsigned long long)bi->ram_base,
               (unsigned long long)(bi->ram_base + bi->ram_size));

    g_bs->free_pool(kernel_data);

    /* --- hand over ------------------------------------------------------ */
    log_printf("loader: exiting boot services\n");
    for (used = 0; used < 4; used++) {
        efi_status st;
        size_t size = 0, keysize = 0, descsize = 0;
        uint32_t key = 0;
        void *map = 0;

        st = g_bs->get_memory_map(&size, 0, &keysize, &descsize, &key);
        if (st != EFI_BUFFER_TOO_SMALL) {
            log_printf("loader: get_memory_map failed (0x%llx)\n", st);
            return;
        }
        size += 2 * descsize;
        if (g_bs->allocate_pool(EFI_LOADER_DATA, size, &map) != EFI_SUCCESS) {
            log_printf("loader: out of memory while leaving boot services\n");
            return;
        }
        if (!EFI_ERROR(g_bs->get_memory_map(&size, map, &keysize, &descsize, &key))) {
            st = g_bs->exit_boot_services(g_image_handle, (size_t)key);
            if (!EFI_ERROR(st)) {
                g_bs = 0;
                g_con = 0;
                break;
            }
        }
        g_bs->free_pool(map);
    }
    if (g_bs) {
        log_printf("loader: ExitBootServices never succeeded, aborting\n");
        return;
    }

    uart_write("loader: jumping to the SurfaceRT_OS kernel\n");
    ((kernel_entry_fn)img.entry)(bi, SRT_BOOT_MAGIC);
    for (;;) {
    }
}

efi_status efi_main(efi_handle image_handle, efi_system_table *st)
{
    g_st = st;
    g_bs = st->boot_services;
    g_con = st->con_out;
    g_image_handle = image_handle;

    /* keep the firmware watchdog from rebooting us during a long boot */
    g_bs->set_watchdog_timer(0, 0, 0, 0);
    uart_init();

    log_printf("\n");
    log_printf("SurfaceRT_OS loader 1.0 (" __DATE__ " " __TIME__ ")\n");
    log_printf("target: Microsoft Surface RT / NVIDIA Tegra 3 (T30), ARMv7-A\n");
    log_printf("\n");

    if (fs_mount() != 0) {
        return EFI_LOAD_ERROR;
    }
    gop_setup();
    load_and_jump();

    log_printf("loader: stopping.  press the power button to reset.\n");
    delay_ms(3000);
    return EFI_SUCCESS;
}
