/* SurfaceRT_OS - basic types and compiler helpers */
#ifndef SRTOS_TYPES_H
#define SRTOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;
typedef unsigned long uptr;
typedef signed long sptr;
typedef unsigned int usize;
typedef signed int ssize;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((uptr)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((uptr)(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define CLAMP(v, lo, hi) MIN(MAX((v), (lo)), (hi))

#define BIT(n) (1u << (n))
#define BIT64(n) (1ull << (n))

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __section(x) __attribute__((section(x)))
#define __unused __attribute__((unused))
#define __noreturn __attribute__((noreturn))
#define __must_check __attribute__((warn_unused_result))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define barrier() __asm__ __volatile__("" ::: "memory")

static inline u32 readl(uptr addr) { return *(volatile u32 *)addr; }
static inline void writel(u32 v, uptr addr) { *(volatile u32 *)addr = v; }
static inline u16 readw(uptr addr) { return *(volatile u16 *)addr; }
static inline void writew(u16 v, uptr addr) { *(volatile u16 *)addr = v; }
static inline u8 readb(uptr addr) { return *(volatile u8 *)addr; }
static inline void writeb(u8 v, uptr addr) { *(volatile u8 *)addr = v; }

#define readl_relaxed readl
#define writel_relaxed(v, a) writel((v), (a))

/* compiler barrier + dsb for device access ordering */
static inline void mmio_barrier(void)
{
    __asm__ __volatile__("dsb sy" ::: "memory");
}

#endif
