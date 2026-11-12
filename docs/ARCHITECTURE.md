# Architecture

```
+-------------------------------------------------------------+
|  services (kernel threads)                                   |
|  shell   wireless display sink   installer   loadable .SVC   |
+-------------------------------------------------------------+
|  VFS (FAT32, SRFS) | net (ARP/IPv4/ICMP/UDP/DHCP) | fb       |
+-------------------------------------------------------------+
|  drivers: uart gpio i2c pmic sdhci dc ehci sdio thermal      |
+-------------------------------------------------------------+
|  kernel core: threads, heap, page allocator, irq, timers      |
+-------------------------------------------------------------+
|  ARMv7-A: MMU (1 MiB sections), GIC-400, PL310, CP15          |
+-------------------------------------------------------------+
|  UEFI loader (BOOTARM.EFI) -> ExitBootServices -> kernel      |
+-------------------------------------------------------------+
```

## Boot flow

1. Firmware loads `\EFI\BOOT\BOOTARM.EFI` (or
   `\EFI\Microsoft\Boot\bootmgfw.efi`).
2. The loader prints a banner, switches the panel to 1366x768 when the GOP offers
   that mode, opens the ESP, reads `\SRTOS\BOOT.CFG` and `\SRTOS\KERNEL.ELF`.
3. `elf_load_image()` (the same code the host tests exercise) allocates pages at
   `kernel_base` (0x82000000), copies the segments, zeroes `.bss` and applies
   `R_ARM_ABS32` / `R_ARM_MOVW_ABS_NC` / `R_ARM_MOVT_ABS` relocations for whatever
   address it ended up at.
4. The loader builds a self contained `struct srt_bootinfo` (memory map,
   framebuffer, clocks, command line) and calls `ExitBootServices()` - retrying
   with a fresh memory map key if the firmware asks for one.
5. It jumps to the kernel with `r0 = bootinfo`, `r1 = SRT_BOOT_MAGIC`.
6. `_start` masks interrupts, drops the firmware's MMU/cache state, clears
   `.bss`, installs the exception vectors, sets up 1 MiB sections (RAM cacheable,
   everything below `0x80000000` device memory), enables the PL310, the GIC-400
   and the timers, then calls `kmain()`.

From that point the firmware is gone: no UEFI calls, no runtime services.

## Memory

* **Identity mapped.** Physical == virtual, 1 MiB sections, domain 0 = normal
  memory, domain 1 = device.  DMA, MMIO and the framebuffer all work without
  bounce buffers.
* **Page allocator** (`core/pmm.c`): one bit per 4 KiB page over the usable
  range (max 2 GiB), supports aligned multi-page allocations - used for the 4 MiB
  display back buffer and for module images.
* **Heap** (`core/heap.c`): first fit, boundary tags, address ordered free list,
  immediate coalescing, 8-byte alignment and a magic number per block so a
  double free is detected rather than corrupting the allocator.  16 MiB arena.

## Threads

Cooperative + preemptive round robin:

* `Cortex-A9 private timer` (PPI 29) ticks at `CONFIG_HZ = 1000`;
* the IRQ entry code saves a full frame **on the interrupted thread's stack**,
  so a context switch from inside the timer handler is just a stack swap;
* `arm_switch_context()` saves `r4-r11, lr`; new threads start in
  `arm_thread_trampoline`, which pops their entry point and argument.

This keeps the scheduler ~300 lines and avoids a separate interrupt stack.

## Timekeeping

The clock driver measures rather than assumes:

* the Tegra 1 MHz counter (`0x60005000`) calibrates the other timers if alive;
* the A9 global timer gives 64-bit uptime (microsecond resolution);
* the A9 private timer drives the scheduler tick;
* if the calibrator is dead, the documented T30 PERIPHCLK (CPU/2) is used.

## Services and extensibility

Two ways to add functionality:

1. **Built in** - a thread created in `kmain()` (shell, display sink, net).
2. **Loadable module** - an ELF file on the ESP loaded at run time
   (`svc load /services/hello.svc`).

A module never links against the kernel.  It exports

```c
int srt_module_init(const struct srt_host_api *api, u32 abi);
```

and receives a versioned table of function pointers (printk, kmalloc, msleep,
fb_blit, udp_send, vfs_read, thread_create, ...).  That makes the ABI explicit
and backwards compatible: a module built for ABI 1 keeps working when ABI 2 adds
fields at the end.  `services/hello/hello.c` is a 90 line example that prints,
draws on the panel and starts its own thread.

## Why no user space yet

A single Cortex-A9 without virtualisation extensions, 2 GiB of RAM and a 4 MiB
framebuffer leaves little room for the TLB pressure of per-process address
spaces.  The service model above gives the extensibility a device like this
needs while keeping the memory system simple.  `docs/EXTENDING.md` describes the
concrete steps (a second page table per service, SVC-based host calls) if
isolation is ever required.
