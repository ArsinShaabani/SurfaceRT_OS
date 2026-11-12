/* SurfaceRT_OS - physical page allocator and kernel heap */
#ifndef SRTOS_MM_H
#define SRTOS_MM_H

#include <srtos/types.h>
#include <srtos/bootinfo.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1u << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

void pmm_init(const struct srt_bootinfo *bi);
void *pmm_alloc_pages(u32 count);
void *pmm_alloc_pages_align(u32 count, u32 align);
void *pmm_alloc_zeroed(u32 count);
void pmm_free_pages(void *addr, u32 count);
u32 pmm_total_pages(void);
u32 pmm_free_page_count(void);
void pmm_dump_stats(void);

void heap_init(void *base, size_t size);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
void heap_stats(size_t *used, size_t *peak, size_t *total);
int heap_selftest(void);

/* Simple bump allocator used by very early boot code. */
void *early_alloc(size_t size, size_t align);
void early_alloc_set_region(void *base, size_t size);

static inline void *page_align(void *p)
{
    return (void *)(((uptr)p + PAGE_SIZE - 1) & PAGE_MASK);
}

#endif
