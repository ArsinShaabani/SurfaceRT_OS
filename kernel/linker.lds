/*
 * SurfaceRT_OS kernel link script.
 *
 * The kernel is a plain static ARMv7 image linked at 0x82000000 (a region that
 * the Surface RT firmware leaves free).  `--emit-relocs` keeps the relocation
 * tables in the file so the UEFI loader can place the image anywhere and patch
 * the absolute references (see boot/elfload.c).
 */
OUTPUT_ARCH(arm)
ENTRY(_start)

KERNEL_BASE = 0x82000000;

SECTIONS
{
    . = KERNEL_BASE;
    __kernel_start = .;

    .text : ALIGN(32) {
        __text_start = .;
        KEEP(*(.text.vectors))
        KEEP(*(.text.start))
        *(.text .text.*)
        __text_end = .;
    }

    .rodata : ALIGN(64) {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    .shell_cmds : ALIGN(4) {
        KEEP(*(.shell_cmds))
    }

    .data : ALIGN(4096) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }

    .bss : ALIGN(64) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /* Driver init table: pointers to init functions, filled in at link time. */
    .init_array : ALIGN(4) {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(SORT(.init_array.*)))
        __init_array_end = .;
    }

    /* Service/module export table (symbols offered to loadable services). */
    .srt_export : ALIGN(4) {
        __export_start = .;
        KEEP(*(.srt_export))
        __export_end = .;
    }

    . = ALIGN(4096);
    __kernel_end = .;

    /* Boot stack lives in .bss region (declared in start.S). */

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.eh_frame*)
        *(.comment)
        *(.note*)
        *(.debug_*)
    }
}
