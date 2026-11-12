/*
 * SurfaceRT_OS - loader <-> kernel ABI
 *
 * This header is shared by the UEFI loader (boot/) and the kernel, so it must
 * stay free of kernel-only types.
 */
#ifndef SRTOS_BOOTINFO_H
#define SRTOS_BOOTINFO_H

#include <stdint.h>

#define SRT_BOOT_MAGIC 0x31545253u /* "SRT1" */
#define SRT_BOOT_ABI 1

#define SRT_MAX_MEM_REGIONS 64
#define SRT_CMDLINE_MAX 256

enum srt_mem_type {
    SRT_MEM_USABLE = 1,      /* conventional RAM, free for the kernel       */
    SRT_MEM_RESERVED = 2,    /* firmware tables, kernel image, ...          */
    SRT_MEM_ACPI = 3,
    SRT_MEM_RUNTIME = 4,
    SRT_MEM_FRAMEBUFFER = 5,
    SRT_MEM_DEVICE = 6,
};

enum srt_fb_format {
    SRT_FB_RGBX = 0,
    SRT_FB_BGRX = 1,
    SRT_FB_BITMASK = 2,
    SRT_FB_UNKNOWN = 3,
};

struct srt_mem_region {
    unsigned long long base;
    unsigned long long size;
    unsigned int type;   /* enum srt_mem_type */
    unsigned int flags;
};

struct srt_framebuffer {
    unsigned long long phys;   /* physical base of the linear framebuffer */
    unsigned int width;
    unsigned int height;
    unsigned int stride; /* pixels per scan line */
    unsigned int bpp;
    unsigned int format; /* enum srt_fb_format */
    unsigned int rotation;
};

struct srt_bootinfo {
    unsigned int magic;
    unsigned int abi_version;
    unsigned int size;             /* sizeof(struct srt_bootinfo)               */
    unsigned int loader_flags;
    unsigned long long kernel_phys_base; /* where the loader placed the kernel image  */
    unsigned long long kernel_size;
    unsigned long long kernel_link_base; /* link-time address of the kernel image     */
    unsigned long long ram_base;
    unsigned long long ram_size;
    unsigned long long cpu_clock_hz;
    unsigned long long uart_clock_hz;
    unsigned long long plt_clock_hz;
    unsigned int mem_region_count;
    unsigned int mem_region_capacity;
    struct srt_mem_region mem[SRT_MAX_MEM_REGIONS];
    struct srt_framebuffer fb;
    char cmdline[SRT_CMDLINE_MAX];
    char loader_version[32];
};

/* loader_flags */
#define SRT_BOOT_FLAG_FB_VALID (1u << 0)
#define SRT_BOOT_FLAG_SERIAL_BAIL (1u << 1)

#endif /* SRTOS_BOOTINFO_H */
