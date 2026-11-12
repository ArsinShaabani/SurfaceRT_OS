/* SurfaceRT_OS - ARMv7 cache and TLB maintenance */
#include <srtos/arch.h>
#include <srtos/kernel.h>
#include <srtos/string.h>
#include "tegra30.h"

#define L1_LINE 32u

void cache_invalidate_all(void)
{
    u32 clidr, ways, sets, level, way, set;
    __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 0" : : "r"(0)); /* ICIALLU */
    __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 6" : : "r"(0)); /* BPIALL  */

    __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
    for (level = 0; level < 7; level++) {
        if (((clidr >> (level * 3)) & 7u) < 2u) {
            continue;
        }
        __asm__ __volatile__("mcr p15, 2, %0, c0, c0, 0" : : "r"(level << 1));
        __asm__ __volatile__("isb");
        __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ways));
        sets = 256u * (((ways >> 13) & 7u) + 1u);
        ways = ((ways >> 3) & 0x3FFu) + 1u;
        for (way = 0; way < ways; way++) {
            for (set = 0; set < sets; set++) {
                u32 sw = (level << 1) | (set << 5) | (way << 30);
                __asm__ __volatile__("mcr p15, 0, %0, c7, c6, 2" : : "r"(sw));
            }
        }
        __asm__ __volatile__("mcr p15, 2, %0, c0, c0, 0" : : "r"(0));
        __asm__ __volatile__("isb");
    }
    __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 0" : : "r"(0)); /* TLBIALL */
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");
}

void dcache_clean_all(void)
{
    u32 clidr, ccsidr, sets, ways, level, way, set;
    __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
    for (level = 0; level < 7; level++) {
        if (((clidr >> (level * 3)) & 7u) < 2u) {
            continue;
        }
        __asm__ __volatile__("mcr p15, 2, %0, c0, c0, 0" : : "r"(level << 1));
        __asm__ __volatile__("isb");
        __asm__ __volatile__("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
        sets = 256u * (((ccsidr >> 13) & 7u) + 1u);
        ways = ((ccsidr >> 3) & 0x3FFu) + 1u;
        for (way = 0; way < ways; way++) {
            for (set = 0; set < sets; set++) {
                u32 sw = (level << 1) | (set << 5) | (way << 30);
                __asm__ __volatile__("mcr p15, 0, %0, c7, c10, 2" : : "r"(sw));
            }
        }
        __asm__ __volatile__("mcr p15, 2, %0, c0, c0, 0" : : "r"(0));
        __asm__ __volatile__("isb");
    }
    __asm__ __volatile__("dsb");
}

void dcache_invalidate_all(void)
{
    /* set/way invalidation is only safe before the caches are enabled */
    cache_invalidate_all();
}

void icache_invalidate_all(void)
{
    __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 0" : : "r"(0));
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");
}

void icache_invalidate_range(uptr start, uptr end)
{
    uptr p = start & ~(L1_LINE - 1);
    for (; p < end; p += L1_LINE) {
        __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 1" : : "r"(p));
    }
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");
}

void dcache_clean_range(uptr start, uptr end)
{
    uptr p = start & ~(L1_LINE - 1);
    for (; p < end; p += L1_LINE) {
        __asm__ __volatile__("mcr p15, 0, %0, c7, c10, 1" : : "r"(p));
    }
    __asm__ __volatile__("dsb");
}

void dcache_invalidate_range(uptr start, uptr end)
{
    uptr p = start & ~(L1_LINE - 1);
    for (; p < end; p += L1_LINE) {
        __asm__ __volatile__("mcr p15, 0, %0, c7, c6, 1" : : "r"(p));
    }
    __asm__ __volatile__("dsb");
}

void dcache_clean_invalidate_range(uptr start, uptr end)
{
    uptr p = start & ~(L1_LINE - 1);
    for (; p < end; p += L1_LINE) {
        __asm__ __volatile__("mcr p15, 0, %0, c7, c14, 1" : : "r"(p));
    }
    __asm__ __volatile__("dsb");
}

void flush_cache_all(void)
{
    dcache_clean_all();
    icache_invalidate_all();
}

void cp15_barrier_sync(void)
{
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");
}

void l2_cache_init(void)
{
    volatile u32 *l2 = (volatile u32 *)T30_L2C_BASE;
    u32 aux;

    l2[L2C_INV_WAY / 4] = 0xFFFFFFFFu;
    while (l2[L2C_INV_WAY / 4] != 0) {
    }
    aux = l2[L2C_AUX_CTRL / 4];
    aux &= ~0x00FF0000u;      /* latency: keep the firmware value        */
    aux |= (1u << 16) |      /* 16-way associativity                  */
           (0u << 17);
    l2[L2C_AUX_CTRL / 4] = aux;
    l2[L2C_CTRL / 4] = L2C_CTRL_ENABLE;
    __asm__ __volatile__("dsb");
    pr_info("l2c: PL310 enabled (aux=0x%08x)\n", aux);
}
