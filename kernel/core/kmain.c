/*
 * SurfaceRT_OS - kernel main
 *
 * Bring-up order matters:
 *   memory -> heap -> platform drivers -> file systems -> threads -> services
 */
#include <srtos/kernel.h>
#include <srtos/arch.h>
#include <srtos/mm.h>
#include <srtos/irq.h>
#include <srtos/sched.h>
#include <srtos/clock.h>
#include <srtos/vfs.h>
#include <srtos/string.h>
#include <srtos/platform.h>
#include <srtos/svc.h>

#define HEAP_PAGES 4096u /* 16 MiB kernel heap */

static void banner(const struct srt_bootinfo *bi)
{
    printk("\n");
    printk("  ============================================================\n");
    printk("   SurfaceRT_OS " SRTOS_VERSION "  \"" SRTOS_CODENAME "\"\n");
    printk("   Microsoft Surface RT - NVIDIA Tegra 3 (T30), 4x Cortex-A9\n");
    printk("  ============================================================\n");
    if (bi) {
        printk("  RAM      : %u MiB at 0x%08x\n", (u32)(bi->ram_size / (1024 * 1024)),
               (u32)bi->ram_base);
        if (bi->fb.phys) {
            printk("  Display  : %ux%u %s at 0x%08x\n", bi->fb.width, bi->fb.height,
                   bi->fb.format == SRT_FB_BGRX ? "BGRX" : "RGBX",
                   (u32)bi->fb.phys);
        } else {
            printk("  Display  : none (headless)\n");
        }
    }
    printk("\n");
}

static void housekeeping(void *arg)
{
    (void)arg;
    for (;;) {
        msleep(5000);
        pr_debug("housekeeping: %u pages free, %d threads\n",
                 pmm_free_page_count(), sched_thread_count());
    }
}

void kmain(const struct srt_bootinfo *bi)
{
    void *heap;
    size_t used, peak, total;

    banner(bi);
    pmm_init(bi);

    heap = pmm_alloc_pages(HEAP_PAGES);
    if (!heap) {
        panic("cannot reserve the kernel heap");
    }
    heap_init(heap, HEAP_PAGES * PAGE_SIZE);
    if (heap_selftest() != 0) {
        pr_warn("heap: self test failed, continuing\n");
    }
    heap_stats(&used, &peak, &total);
    pr_info("heap: %u KiB arena, %u bytes in use\n", (u32)(total / 1024), (u32)used);

    irq_init();
    platform_init();

    vfs_init();
    platform_mount_root();

    sched_init();
    svc_manager_init();

    thread_create("shell", (void (*)(void *))shell_thread_main, 0, 12288);
    if (platform_has_display()) {
        thread_create("dispsink", (void (*)(void *))disp_thread_main, 0, 32768);
    }
    thread_create("net", (void (*)(void *))net_thread_main, 0, 8192);
    thread_create("housekeeping", housekeeping, 0, 4096);

    heap_stats(&used, &peak, &total);
    pr_info("heap: %u bytes used, peak %u, uptime %llu ms\n", (u32)used,
            (u32)peak, clock_uptime_ms());

    sched_start();
}
