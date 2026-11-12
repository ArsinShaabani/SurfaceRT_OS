/*
 * SurfaceRT_OS - ARMv7 short descriptor MMU setup
 *
 * We use 1 MiB sections for the whole 4 GiB address space: the kernel is
 * identity mapped, so physical == virtual everywhere.  That keeps DMA, MMIO
 * and the framebuffer trivial, and 1 MiB granularity is plenty for a device
 * whose RAM starts at 0x80000000 and whose peripherals live in 64 MiB windows.
 *
 *   domain 0: normal memory (RAM)
 *   domain 1: device / strongly ordered memory
 */
#include <srtos/arch.h>
#include <srtos/kernel.h>
#include <srtos/mm.h>
#include <srtos/string.h>
#include "tegra30.h"

#define SECTION_SHIFT 20
#define SECTION_SIZE (1u << SECTION_SHIFT)
#define DESC_SECTION 0x2u

static u32 l1_table[4096] __aligned(16384);

static const struct srt_bootinfo *g_bi;

u32 *mmu_l1_table(void)
{
    return l1_table;
}

void mmu_map_section(uptr vaddr, uptr paddr, u32 attrs)
{
    u32 idx = (u32)(vaddr >> SECTION_SHIFT);
    if (idx >= 4096) {
        return;
    }
    l1_table[idx] = ((u32)paddr & 0xFFF00000u) | (attrs & MMU_ATTR_MASK) |
                    DESC_SECTION;
    __asm__ __volatile__("mcr p15, 0, %0, c7, c10, 1" : : "r"(&l1_table[idx]));
}

static void map_range(uptr start, uptr end, u32 attrs)
{
    uptr a;
    for (a = start; a < end; a += SECTION_SIZE) {
        mmu_map_section(a, a, attrs);
    }
}

int mmu_init(void)
{
    u32 sctlr;

    memset(l1_table, 0, sizeof(l1_table));

    /* Devices first: everything below 0x80000000 is MMIO on Tegra 3. */
    map_range(0x00000000u, T30_RAM_BASE, MMU_ATTR_DEVICE);

    /* RAM: honour the memory map handed over by the loader. */
    if (g_bi) {
        u32 i;
        for (i = 0; i < g_bi->mem_region_count; i++) {
            const struct srt_mem_region *r = &g_bi->mem[i];
            uptr start = (uptr)r->base & ~(SECTION_SIZE - 1);
            uptr end = ((uptr)r->base + (uptr)r->size + SECTION_SIZE - 1) &
                       ~(SECTION_SIZE - 1);
            if (end <= T30_RAM_BASE) {
                continue;
            }
            if (start < T30_RAM_BASE) {
                start = T30_RAM_BASE;
            }
            if (r->type == SRT_MEM_USABLE || r->type == SRT_MEM_RESERVED ||
                r->type == SRT_MEM_ACPI || r->type == SRT_MEM_RUNTIME ||
                r->type == SRT_MEM_FRAMEBUFFER) {
                map_range(start, end, MMU_ATTR_NORMAL_WB);
            } else {
                map_range(start, end, MMU_ATTR_DEVICE);
            }
        }
    } else {
        map_range(T30_RAM_BASE, 0x100000000ull > T30_RAM_BASE + T30_RAM_SIZE_MAX
                                ? T30_RAM_BASE + T30_RAM_SIZE_MAX : 0xFFFF0000u,
                  MMU_ATTR_NORMAL_WB);
    }

    /* Translation table base: TTBR0 covers the whole space (TTBCR.N = 0). */
    __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 2" : : "r"(0));
    __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 0" : : "r"((u32)l1_table));
    /* domain access control: domains 0 and 1 are clients */
    __asm__ __volatile__("mcr p15, 0, %0, c3, c0, 0" : : "r"(0x00000005u));
    __asm__ __volatile__("isb");
    __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 0" : : "r"(0)); /* TLBIALL */
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");

    __asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= (1u << 0) |   /* M: MMU                */
             (1u << 2) |   /* C: data cache         */
             (1u << 12) |  /* I: instruction cache  */
             (1u << 11) |  /* Z: branch prediction  */
             (1u << 22);   /* U: unaligned access   */
    sctlr &= ~(1u << 1);   /* A: no alignment fault checking */
    __asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");

    pr_info("mmu: 1 MiB sections, sctlr=0x%08x, l1=0x%08x\n", sctlr, (u32)l1_table);
    return 0;
}

void mmu_switch(u32 *table)
{
    __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 0" : : "r"((u32)table));
    __asm__ __volatile__("dsb");
    __asm__ __volatile__("isb");
}

void mmu_set_bootinfo(const struct srt_bootinfo *bi)
{
    g_bi = bi;
}

void mmu_dump(void)
{
    u32 i, mapped = 0;
    printk("mmu: l1 table @ 0x%08x\n", (u32)l1_table);
    for (i = 0; i < 4096; i++) {
        if (l1_table[i]) {
            mapped++;
        }
    }
    printk("mmu: %u of 4096 sections mapped (%u MiB)\n", mapped,
           mapped);
    printk("mmu: ram [0x%08x..0x%08x) normal WB\n", T30_RAM_BASE, 0x80000000u);
    printk("mmu: mmio [0x00000000..0x%08x) device\n", T30_RAM_BASE);
}
