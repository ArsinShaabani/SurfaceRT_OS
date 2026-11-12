/*
 * SurfaceRT_OS - kernel heap
 *
 * First fit allocator with boundary tags, an address ordered free list and
 * immediate coalescing.  It is deliberately small and dependency free so the
 * host test suite (tests/test_heap.c) can exercise exactly the same code.
 *
 * Every allocation is 8 byte aligned and carryied a magic number, so a double
 * free or a buffer underrun is detected instead of silently corrupting the
 * allocator.
 */
#include <srtos/mm.h>
#include <srtos/string.h>

#ifndef SRTOS_HOST
#include <srtos/arch.h>
#define heap_lock() arm_irq_save()
#define heap_unlock(f) arm_irq_restore(f)
#else
#define heap_lock() 0u
#define heap_unlock(f) ((void)(f))
#endif

#define HEAP_MAGIC 0x48454150u   /* "HEAP" */
#define HEAP_ALIGN 8u
#define MIN_SPLIT (sizeof(struct heap_block) + 32u)

struct heap_block {
    u32 magic;
    u32 size; /* payload bytes, excluding this header */
    u32 used;
    u32 pad;
    struct heap_block *next;      /* physical next */
    struct heap_block *prev;      /* physical prev */
    struct heap_block *free_next; /* free list      */
    struct heap_block *free_prev;
};

static struct heap_block *g_first;
static struct heap_block *g_free;
static size_t g_total;
static size_t g_used;
static size_t g_peak;
static int g_initialised;

static inline size_t hdr_size(void)
{
    return ALIGN_UP(sizeof(struct heap_block), HEAP_ALIGN);
}

static void free_list_insert(struct heap_block *b)
{
    struct heap_block *p = 0, *c = g_free;
    while (c && c < b) {
        p = c;
        c = c->free_next;
    }
    b->free_prev = p;
    b->free_next = c;
    if (p) {
        p->free_next = b;
    } else {
        g_free = b;
    }
    if (c) {
        c->free_prev = b;
    }
}

static void free_list_remove(struct heap_block *b)
{
    if (b->free_prev) {
        b->free_prev->free_next = b->free_next;
    } else {
        g_free = b->free_next;
    }
    if (b->free_next) {
        b->free_next->free_prev = b->free_prev;
    }
    b->free_next = b->free_prev = 0;
}

static void try_merge(struct heap_block *b)
{
    if (b->next && !b->next->used) {
        struct heap_block *n = b->next;
        free_list_remove(n);
        b->size += hdr_size() + n->size;
        b->next = n->next;
        if (b->next) {
            b->next->prev = b;
        }
        n->magic = 0;
    }
    if (b->prev && !b->prev->used) {
        try_merge(b->prev);
    }
}

void heap_init(void *base, size_t size)
{
    uptr start = ALIGN_UP((uptr)base, HEAP_ALIGN);
    uptr end = ALIGN_DOWN((uptr)base + size, HEAP_ALIGN);

    if (end <= start + hdr_size() + 64) {
        g_initialised = 0;
        return;
    }
    g_first = (struct heap_block *)start;
    g_first->magic = HEAP_MAGIC;
    g_first->size = (u32)(end - start - hdr_size());
    g_first->used = 0;
    g_first->next = 0;
    g_first->prev = 0;
    g_first->free_next = 0;
    g_first->free_prev = 0;
    g_free = g_first;
    g_total = g_first->size;
    g_used = 0;
    g_peak = 0;
    g_initialised = 1;
}

static void *alloc_locked(size_t want)
{
    struct heap_block *b;
    size_t need = ALIGN_UP(want ? want : 1, HEAP_ALIGN);

    for (b = g_free; b; b = b->free_next) {
        if (b->size >= need) {
            break;
        }
    }
    if (!b) {
        return 0;
    }
    free_list_remove(b);

    if (b->size >= need + MIN_SPLIT) {
        struct heap_block *nb = (struct heap_block *)((char *)b + hdr_size() + need);
        nb->magic = HEAP_MAGIC;
        nb->size = (u32)(b->size - need - hdr_size());
        nb->used = 0;
        nb->prev = b;
        nb->next = b->next;
        nb->free_next = nb->free_prev = 0;
        if (nb->next) {
            nb->next->prev = nb;
        }
        b->next = nb;
        b->size = (u32)need;
        free_list_insert(nb);
    }
    b->used = 1;
    g_used += b->size;
    if (g_used > g_peak) {
        g_peak = g_used;
    }
    return (char *)b + hdr_size();
}

void *kmalloc(size_t size)
{
    u32 flags;
    void *p;
    if (!g_initialised) {
        return 0;
    }
    flags = heap_lock();
    p = alloc_locked(size);
    heap_unlock(flags);
    return p;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p) {
        memset(p, 0, size);
    }
    return p;
}

void *kcalloc(size_t n, size_t size)
{
    if (size && n > (size_t)-1 / size) {
        return 0;
    }
    return kzalloc(n * size);
}

void kfree(void *ptr)
{
    struct heap_block *b;
    u32 flags;

    if (!ptr || !g_initialised) {
        return;
    }
    b = (struct heap_block *)((char *)ptr - hdr_size());
    if (b->magic != HEAP_MAGIC) {
        /* double free or a pointer that never came from kmalloc */
        return;
    }
    flags = heap_lock();
    if (!b->used) {
        heap_unlock(flags);
        return;
    }
    b->used = 0;
    g_used -= b->size;
    free_list_insert(b);
    try_merge(b);
    heap_unlock(flags);
}

void *krealloc(void *ptr, size_t size)
{
    struct heap_block *b;
    void *np;

    if (!ptr) {
        return kmalloc(size);
    }
    if (!size) {
        kfree(ptr);
        return 0;
    }
    b = (struct heap_block *)((char *)ptr - hdr_size());
    if (b->magic != HEAP_MAGIC) {
        return 0;
    }
    if (b->size >= size) {
        return ptr;
    }
    if (b->next && !b->next->used &&
        b->size + hdr_size() + b->next->size >= size) {
        struct heap_block *n = b->next;
        u32 flags = heap_lock();
        free_list_remove(n);
        g_used += n->size + hdr_size();
        b->size += hdr_size() + n->size;
        b->next = n->next;
        if (b->next) {
            b->next->prev = b;
        }
        n->magic = 0;
        heap_unlock(flags);
        return ptr;
    }
    np = kmalloc(size);
    if (!np) {
        return 0;
    }
    memcpy(np, ptr, b->size < size ? b->size : size);
    kfree(ptr);
    return np;
}

void heap_stats(size_t *used, size_t *peak, size_t *total)
{
    if (used) {
        *used = g_used;
    }
    if (peak) {
        *peak = g_peak;
    }
    if (total) {
        *total = g_total;
    }
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)kmalloc(n);
    if (d) {
        memcpy(d, s, n);
    }
    return d;
}

int heap_selftest(void)
{
    u32 *a, *b, *c;
    size_t used0, peak, total;
    int i;

    heap_stats(&used0, &peak, &total);
    a = (u32 *)kmalloc(1000);
    b = (u32 *)kmalloc(2000);
    c = (u32 *)kmalloc(64);
    if (!a || !b || !c) {
        return -1;
    }
    for (i = 0; i < 250; i++) {
        a[i] = (u32)i;
    }
    kfree(b);
    b = (u32 *)kmalloc(1500);
    if (!b) {
        return -2;
    }
    kfree(a);
    kfree(b);
    kfree(c);
    heap_stats(&used0, &peak, &total);
    return used0 == 0 ? 0 : -3;
}
