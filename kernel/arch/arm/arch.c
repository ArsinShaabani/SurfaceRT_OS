/* SurfaceRT_OS - ARMv7 boot hand-off from the UEFI loader */
#include <srtos/arch.h>
#include <srtos/kernel.h>
#include <srtos/string.h>
#include <srtos/mm.h>
#include <srtos/irq.h>
#include <srtos/clock.h>

void mmu_set_bootinfo(const struct srt_bootinfo *bi);
void cmdline_set_bootinfo(const struct srt_bootinfo *bi);
void uart_init_early(const struct srt_bootinfo *bi);
void kmain(const struct srt_bootinfo *bi);

void arm_early_init(const struct srt_bootinfo *bi, u32 magic)
{
    uart_init_early(bi);
    arm_vectors_install();

    printk("\n");
    printk("                    SurfaceRT_OS " SRTOS_VERSION "\n");
    printk("     an ARMv7 kernel for the Microsoft Surface RT (Tegra 3)\n");
    printk("\n");

    if (magic != SRT_BOOT_MAGIC || !bi || bi->magic != SRT_BOOT_MAGIC) {
        panic("bad boot hand-off (magic 0x%08x, bootinfo %p)", magic, bi);
    }
    cmdline_set_bootinfo(bi);
    printk("boot: %s, bootinfo @ %p, cmdline \"%s\"\n", bi->loader_version, bi,
           bi->cmdline);

    mmu_set_bootinfo(bi);
    mmu_init();
    l2_cache_init();
    gic_init();
    clock_init(bi);
    kmain(bi);
}
