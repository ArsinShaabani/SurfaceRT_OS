/*
 * SurfaceRT_OS - physical page allocator
 *
 * One bit per 4 KiB page, covering the physical range reported by the loader.
 * The bitmap is statically sized for 2 GiB (the maximum amount of RAM a
 * Surface RT can have) which costs 64 KiB of .bss - a fair trade for simplest
 * possible allocation of contiguous runs (DMA buffers, page tables, the
 * framebuffer back buffers).
 */
#include <srtos/mm.h>
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/string.h>

#define MAX_PAGES (2u * 1024u * 1024u)   /* 2 GiB / 4 KiB */
#define BITMAP_WORDS (MAX_PAGES / 32u)

static u32 page_bitmap[BITMAP_WORDS];   /* 1 = used */
static uptr g_base;
static u32 g_pages;                     /* pages covered by the bitmap */
static u32 g_used;
static u32 g_reserved;

static inline int test_bit(u32 n)
{
    return (page_bitmap[n / 32] >> (n % 32)) & 1u;
}

static inline void set_bit(u32 n)
{
    page_bitmap[n / 32] |= BIT(n % 32);
}

static inline void clear_bit(u32 n)
{
    page_bitmap[n / 32] &= ~BIT(n % 32);
}

void pmm_init(const struct srt_bootinfo *bi)
{
    uptr end = 0;
    u32 i, pages;

    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    if (!bi || !bi->mem_region_count) {
        /* no memory map: assume 512 MiB at the Tegra 3 RAM base */
        g_base = 0x80000000u;
        g_pages = 512u * 1024u * 1024u / PAGE_SIZE;
        for (i = 0; i < g_pages; i++) {
            clear_bit(i);
        }
        g_used = 0;
        return;
    }

    g_base = (uptr)bi->ram_base & PAGE_MASK;
    for (i = 0; i < bi->mem_region_count; i++) {
        const struct srt_mem_region *r = &bi->mem[i];
        if (r->type == SRT_MEM_USABLE || r->type == SRT_MEM_ACPI ||
            r->type == SRT_MEM_RUNTIME || r->type == SRT_MEM_FRAMEBUFFER ||
            r->type == SRT_MEM_RESERVED) {
            uptr e = r->base + r->size;
            if (e > end) {
                end = e;
            }
        }
    }
    if (end <= g_base) {
        g_base = 0x80000000u;
        end = g_base + 512u * 1024u * 1024u;
    }
    pages = (u32)((end - g_base) / PAGE_SIZE);
    if (pages > MAX_PAGES) {
        pages = MAX_PAGES;
    }
    g_pages = pages;

    /* everything free first */
    for (i = 0; i < g_pages; i++) {
        clear_bit(i);
    }
    /* then reserve what is not usable */
    for (i = 0; i < bi->mem_region_count; i++) {
        const struct srt_mem_region *r = &bi->mem[i];
        uptr start, stop;
        u32 p;

        if (r->type == SRT_MEM_USABLE) {
            continue;
        }
        start = ALIGN_DOWN((uptr)r->base, PAGE_SIZE);
        stop = ALIGN_UP((uptr)r->base + (uptr)r->size, PAGE_SIZE);
        if (start < g_base) {
            start = g_base;
        }
        if (stop > g_base + (uptr)g_pages * PAGE_SIZE) {
            stop = g_base + (uptr)g_pages * PAGE_SIZE;
        }
        for (p = (u32)((start - g_base) / PAGE_SIZE);
             p < (u32)((stop - g_base) / PAGE_SIZE) && p < g_pages; p++) {
            set_bit(p);
            g_reserved++;
        }
    }

    /* reserve the kernel image and the page tables */
    {
        extern char __kernel_start[], __kernel_end[];
        uptr kstart = ALIGN_DOWN((uptr)__kernel_start, PAGE_SIZE);
        uptr kend = ALIGN_UP((uptr)__kernel_end, PAGE_SIZE);
        uptr p;
        for (p = kstart; p < kend; p += PAGE_SIZE) {
            u32 idx = (u32)((p - g_base) / PAGE_SIZE);
            if (idx < g_pages && !test_bit(idx)) {
                set_bit(idx);
                g_reserved++;
            }
        }
        printk("pmm: kernel image 0x%08x-0x%08x reserved\n", (u32)kstart,
               (u32)kend);
    }
    g_used = 0;
    printk("pmm: %u pages (%u MiB) from 0x%08x, %u reserved\n", g_pages,
           g_pages * 4u / 1024u, (u32)g_base, g_reserved);
}

void *pmm_alloc_pages_align(u32 count, u32 align)
{
    u32 run = 0, i, step = align / PAGE_SIZE;

    if (!count || !step) {
        return 0;
    }
    for (i = 0; i < g_pages; i++) {
        if (test_bit(i)) {
            run = 0;
            continue;
        }
        if (run == 0 && (i % step)) {
            continue;
        }
        if (++run == count) {
            u32 start = i + 1 - count;
            u32 k;
            for (k = start; k <= i; k++) {
                set_bit(k);
            }
            g_used += count;
            return (void *)(g_base + (uptr)start * PAGE_SIZE);
        }
    }
    return 0;
}

void *pmm_alloc_pages(u32 count)
{
    return pmm_alloc_pages_align(count, PAGE_SIZE);
}

void *pmm_alloc_zeroed(u32 count)
{
    void *p = pmm_alloc_pages(count);
    if (p) {
        memset(p, 0, count * PAGE_SIZE);
    }
    return p;
}

void pmm_free_pages(void *addr, u32 count)
{
    uptr a = (uptr)addr;
    u32 i, idx;

    if (!addr || a < g_base) {
        return;
    }
    idx = (u32)((a - g_base) / PAGE_SIZE);
    for (i = 0; i < count && idx + i < g_pages; i++) {
        if (test_bit(idx + i)) {
            clear_bit(idx + i);
            if (g_used) {
                g_used--;
            }
        }
    }
}

u32 pmm_total_pages(void)
{
    return g_pages;
}

u32 pmm_free_page_count(void)
{
    return g_pages > g_used ? g_pages - g_used - g_reserved : 0;
}

void pmm_dump_stats(void)
{
    printk("pmm: %u pages total, %u used, %u reserved, %u free (%u MiB free)\n",
           g_pages, g_used, g_reserved, pmm_free_page_count(),
           pmm_free_page_count() * 4u / 1024u);
}
