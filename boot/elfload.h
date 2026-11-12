/*
 * Portable ELF32/ARM image loader with relocation.
 *
 * The kernel is linked statically (fast, no GOT) but with `--emit-relocs`, so
 * the loader can place it at *any* 4 KiB aligned physical address and fix up
 * the absolute references.  This keeps the kernel independent of whatever
 * memory layout the Surface RT firmware happens to hand out.
 *
 * The code here has no UEFI and no libc dependency, which lets the host test
 * suite exercise exactly the same code path that runs on the tablet.
 */
#ifndef SRTOS_ELFLOAD_H
#define SRTOS_ELFLOAD_H

#include <stdint.h>

struct elf_image {
    uintptr_t entry;      /* physical entry point (already biased) */
    uintptr_t load_base;  /* where the image was placed             */
    uintptr_t link_base;  /* link-time base of the image            */
    uintptr_t span;       /* bytes reserved                         */
    uintptr_t file_size;  /* bytes copied from the file             */
    uint32_t relocations; /* applied absolute relocations           */
};

/* Allocate `size` bytes, preferably at `want`; return 0 on success. */
typedef int (*elf_alloc_fn)(uintptr_t want, uintptr_t size, uintptr_t *got,
                            void *ctx);

void elf_memcpy(void *dst, const void *src, uintptr_t n);
void elf_memset(void *dst, int c, uintptr_t n);

/*
 * Load `file` at (preferably) `want_base` using `alloc`.
 * Returns 0 on success or one of the ELF_E* codes below.
 */
int elf_load_image(const void *file, uintptr_t file_size, uintptr_t want_base,
                   elf_alloc_fn alloc, void *ctx, struct elf_image *out);

#define ELF_OK 0
#define ELF_E_TOO_SMALL -1
#define ELF_E_BAD_MAGIC -2
#define ELF_E_BAD_CLASS -3
#define ELF_E_NOT_ARM -4
#define ELF_E_NOT_EXEC -5
#define ELF_E_NO_SEGMENTS -6
#define ELF_E_ALLOC -7
#define ELF_E_BAD_RANGE -8
#define ELF_E_RELOC_UNSUPPORTED -9
#define ELF_E_TRUNCATED -10

#endif /* SRTOS_ELFLOAD_H */
