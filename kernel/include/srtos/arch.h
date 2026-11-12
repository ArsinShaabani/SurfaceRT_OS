/* SurfaceRT_OS - ARM architecture helpers */
#ifndef SRTOS_ARCH_H
#define SRTOS_ARCH_H

#include <srtos/types.h>
#include <srtos/bootinfo.h>

void arm_early_init(const struct srt_bootinfo *bi, u32 magic);
void arm_vectors_install(void);
void arm_switch_context(u32 **save_sp, u32 *new_sp);
void arm_thread_trampoline(void);
u32 arm_cpu_id(void);

int mmu_init(void);
void mmu_dump(void);
void mmu_map_section(uptr vaddr, uptr paddr, u32 attrs);
void mmu_switch(u32 *table);

u32 *mmu_l1_table(void);

/* memory attributes */
#define MMU_ATTR_NORMAL_WB 0x0000040Eu /* TEX=1 C=1 B=1, AP=11            */
#define MMU_ATTR_NORMAL_WT 0x0000040Au /* TEX=0 C=1 B=0                   */
#define MMU_ATTR_DEVICE 0x00000416u    /* TEX=0 C=0 B=1 S=1 XN=1          */
#define MMU_ATTR_STRONGLY_ORDERED 0x00000412u
#define MMU_ATTR_MASK 0x000FFC1Fu

void cache_invalidate_all(void);
void dcache_clean_all(void);
void dcache_invalidate_all(void);
void icache_invalidate_all(void);
void icache_invalidate_range(uptr start, uptr end);
void dcache_clean_range(uptr start, uptr end);
void dcache_invalidate_range(uptr start, uptr end);
void dcache_clean_invalidate_range(uptr start, uptr end);
void flush_cache_all(void);
void cp15_barrier_sync(void);
void l2_cache_init(void);

static inline u32 arm_read_cpsr(void)
{
    u32 v;
    __asm__ __volatile__("mrs %0, cpsr" : "=r"(v));
    return v;
}

static inline u32 arm_read_spsr(void)
{
    u32 v;
    __asm__ __volatile__("mrs %0, spsr" : "=r"(v));
    return v;
}

static inline void wfi(void) { __asm__ __volatile__("wfi"); }
static inline void wfe(void) { __asm__ __volatile__("wfe"); }
static inline void sev(void) { __asm__ __volatile__("sev"); }

static inline u32 arm_irq_save(void)
{
    u32 f;
    __asm__ __volatile__("mrs %0, cpsr\n\tcpsid i" : "=r"(f) : : "memory");
    return f;
}

static inline void arm_irq_restore(u32 f)
{
    __asm__ __volatile__("msr cpsr_c, %0" : : "r"(f) : "memory");
}

#endif
