/*
 * Linker script for the UEFI loader.
 *
 * The image is linked as a shared object so that ld emits relative
 * relocations, which tools/elf2pe.py converts into PE base relocations.
 * The first byte of code sits at 0x1000 so that the PE headers (which occupy
 * RVA 0 .. SizeOfHeaders) do not overlap the code.
 */
ENTRY(efi_main)
SECTIONS
{
    . = 0x1000;
    .text : {
        *(.text.efi_main)
        *(.text .text.*)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.data.rel.ro .data.rel.ro.*)
        *(.got .got.*)
        *(.data .data.*)
    }

    . = ALIGN(0x1000);
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    . = ALIGN(0x1000);
    .rel.dyn : {
        *(.rel.dyn)
        *(.rel.*)
    }

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.eh_frame*)
        *(.comment)
        *(.note*)
        *(.interp)
        *(.dynamic)
        *(.hash)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.gnu.version*)
    }
}
