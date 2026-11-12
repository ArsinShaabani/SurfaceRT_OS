/* Portable ELF32/ARM loader - see elfload.h */
#include "elfload.h"

#define EI_NIDENT 16
#define EM_ARM 40
#define ET_EXEC 2
#define PT_LOAD 1
#define SHT_REL 9
#define SHF_ALLOC 0x2

#define R_ARM_PC24 1
#define R_ARM_ABS32 2
#define R_ARM_REL32 3
#define R_ARM_THM_CALL 10
#define R_ARM_RELATIVE 23
#define R_ARM_CALL 28
#define R_ARM_JUMP24 29
#define R_ARM_THM_JUMP24 30
#define R_ARM_PREL31 42
#define R_ARM_MOVW_ABS_NC 43
#define R_ARM_MOVT_ABS 44
#define R_ARM_MOVW_PREL_NC 45
#define R_ARM_MOVT_PREL 46

/* Relocations that encode a PC relative branch or offset: the displacement is
 * already correct after moving the whole image, so nothing to do. */
static int is_pc_relative(uint32_t type)
{
    switch (type) {
    case R_ARM_PC24:
    case R_ARM_REL32:
    case R_ARM_THM_CALL:
    case R_ARM_CALL:
    case R_ARM_JUMP24:
    case R_ARM_THM_JUMP24:
    case R_ARM_PREL31:
    case R_ARM_MOVW_PREL_NC:
    case R_ARM_MOVT_PREL:
        return 1;
    default:
        return 0;
    }
}

typedef struct {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf32_ehdr;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
        p_align;
} elf32_phdr;

typedef struct {
    uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
        sh_info, sh_addralign, sh_entsize;
} elf32_shdr;

typedef struct {
    uint32_t r_offset, r_info;
} elf32_rel;

void elf_memcpy(void *dst, const void *src, uintptr_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
}

void elf_memset(void *dst, int c, uintptr_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)c;
    }
}

static int in_range(uintptr_t v, uintptr_t base, uintptr_t size)
{
    return v >= base && v - base < size;
}

/* MOVW/MOVT immediate accessors (ARMv7 encoding). */
static uint32_t movw_imm(uint32_t insn)
{
    return (((insn >> 16) & 0xF) << 12) | (insn & 0xFFF);
}

static uint32_t movt_imm(uint32_t insn)
{
    return (((insn >> 16) & 0xF) << 12) | (insn & 0xFFF);
}

static uint32_t set_movw_imm(uint32_t insn, uint32_t imm)
{
    return (insn & 0xFFF0F000u) | (((imm >> 12) & 0xF) << 16) | (imm & 0xFFF);
}

static uint32_t set_movt_imm(uint32_t insn, uint32_t imm)
{
    return (insn & 0xFFF0F000u) | (((imm >> 12) & 0xF) << 16) | (imm & 0xFFF);
}

int elf_load_image(const void *file, uintptr_t file_size, uintptr_t want_base,
                   elf_alloc_fn alloc, void *ctx, struct elf_image *out)
{
    const uint8_t *base = (const uint8_t *)file;
    const elf32_ehdr *eh;
    uintptr_t span_end, span_start;
    uintptr_t got = 0;
    intptr_t bias;
    uint32_t i, reloc_count = 0;
    int rc;

    if (!file || !out) {
        return ELF_E_TOO_SMALL;
    }
    if (file_size < sizeof(elf32_ehdr)) {
        return ELF_E_TOO_SMALL;
    }
    eh = (const elf32_ehdr *)base;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F') {
        return ELF_E_BAD_MAGIC;
    }
    if (eh->e_ident[4] != 1 || eh->e_ident[5] != 1) {
        return ELF_E_BAD_CLASS; /* only ELF32 little endian */
    }
    if (eh->e_machine != EM_ARM) {
        return ELF_E_NOT_ARM;
    }
    if (eh->e_type != ET_EXEC) {
        return ELF_E_NOT_EXEC;
    }
    if (!eh->e_phnum || eh->e_phentsize != sizeof(elf32_phdr) ||
        eh->e_phoff + (uintptr_t)eh->e_phnum * sizeof(elf32_phdr) > file_size) {
        return ELF_E_NO_SEGMENTS;
    }

    /* Find the address span covered by all loadable segments. */
    span_start = ~(uintptr_t)0;
    span_end = 0;
    for (i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr *ph = (const elf32_phdr *)(base + eh->e_phoff +
                                                   i * sizeof(elf32_phdr));
        if (ph->p_type != PT_LOAD || !ph->p_memsz) {
            continue;
        }
        if (ph->p_offset + (uintptr_t)ph->p_filesz > file_size) {
            return ELF_E_TRUNCATED;
        }
        if (ph->p_vaddr < span_start) {
            span_start = ph->p_vaddr;
        }
        if (ph->p_vaddr + ph->p_memsz > span_end) {
            span_end = ph->p_vaddr + ph->p_memsz;
        }
    }
    if (span_start >= span_end) {
        return ELF_E_NO_SEGMENTS;
    }
    span_start &= ~(uintptr_t)0xFFFu;
    span_end = (span_end + 0xFFFu) & ~(uintptr_t)0xFFFu;

    if (!want_base) {
        want_base = span_start;
    }
    want_base &= ~(uintptr_t)0xFFFu;
    rc = alloc(want_base, span_end - span_start, &got, ctx);
    if (rc != 0 || !got) {
        return ELF_E_ALLOC;
    }
    bias = (intptr_t)got - (intptr_t)span_start;
    out->relocations = 0;

    for (i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr *ph = (const elf32_phdr *)(base + eh->e_phoff +
                                                   i * sizeof(elf32_phdr));
        uint8_t *dst;
        if (ph->p_type != PT_LOAD || !ph->p_memsz) {
            continue;
        }
        dst = (uint8_t *)(uintptr_t)(ph->p_vaddr + bias);
        if (ph->p_filesz) {
            elf_memcpy(dst, base + ph->p_offset, ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            elf_memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    /* Apply absolute relocations so the image runs at its new address. */
    if (bias != 0 && eh->e_shoff && eh->e_shnum) {
        const elf32_shdr *sh;
        if (eh->e_shoff + (uintptr_t)eh->e_shnum * sizeof(elf32_shdr) > file_size) {
            return ELF_E_TRUNCATED;
        }
        sh = (const elf32_shdr *)(base + eh->e_shoff);
        for (i = 0; i < eh->e_shnum; i++) {
            const elf32_rel *rels;
            uint32_t j, n;
            if (sh[i].sh_type != SHT_REL || !sh[i].sh_size) {
                continue;
            }
            if (sh[i].sh_offset + (uintptr_t)sh[i].sh_size > file_size) {
                return ELF_E_TRUNCATED;
            }
            rels = (const elf32_rel *)(base + sh[i].sh_offset);
            n = sh[i].sh_size / sizeof(elf32_rel);
            for (j = 0; j < n; j++) {
                uint32_t type = rels[j].r_info & 0xFF;
                uintptr_t target = (uintptr_t)(rels[j].r_offset + bias);

                if (is_pc_relative(type)) {
                    continue;
                }
                if (!in_range(target, got, span_end - span_start)) {
                    return ELF_E_BAD_RANGE;
                }
                switch (type) {
                case R_ARM_ABS32:
                case R_ARM_RELATIVE:
                    *(uint32_t *)(uintptr_t)target += (uint32_t)bias;
                    reloc_count++;
                    break;
                case R_ARM_MOVW_ABS_NC: {
                    /* The upper half lives in the neighbouring MOVT; rebuild the
                     * full address, add the bias and split it again so that
                     * carries are correct. */
                    uint32_t lo_imm = movw_imm(*(uint32_t *)(uintptr_t)target);
                    uint32_t hi_imm = 0;
                    uint32_t found = 0, k, m;
                    for (k = 0; k < eh->e_shnum && !found; k++) {
                        const elf32_rel *r2;
                        uint32_t n2;
                        if (sh[k].sh_type != SHT_REL || !sh[k].sh_size) {
                            continue;
                        }
                        r2 = (const elf32_rel *)(base + sh[k].sh_offset);
                        n2 = sh[k].sh_size / sizeof(elf32_rel);
                        for (m = 0; m < n2; m++) {
                            if (r2[m].r_offset == rels[j].r_offset + 4 &&
                                (r2[m].r_info & 0xFF) == R_ARM_MOVT_ABS) {
                                uintptr_t t2 = (uintptr_t)(rels[j].r_offset + 4 + bias);
                                if (!in_range(t2, got, span_end - span_start)) {
                                    return ELF_E_BAD_RANGE;
                                }
                                hi_imm = movt_imm(*(uint32_t *)(uintptr_t)t2);
                                found = 1;
                                break;
                            }
                        }
                    }
                    {
                        uint32_t full = (hi_imm << 16) | lo_imm;
                        full += (uint32_t)bias;
                        *(uint32_t *)(uintptr_t)target =
                            set_movw_imm(*(uint32_t *)(uintptr_t)target, full & 0xFFFF);
                        if (found) {
                            uintptr_t t2 = (uintptr_t)(rels[j].r_offset + 4 + bias);
                            *(uint32_t *)(uintptr_t)t2 =
                                set_movt_imm(*(uint32_t *)(uintptr_t)t2,
                                            (full >> 16) & 0xFFFF);
                        }
                    }
                    reloc_count++;
                    break;
                }
                case R_ARM_MOVT_ABS:
                    /* handled together with its MOVW; ignore here */
                    break;
                default:
                    return ELF_E_RELOC_UNSUPPORTED;
                }
            }
        }
    }

    out->entry = (uintptr_t)(eh->e_entry + bias);
    out->load_base = got;
    out->link_base = span_start;
    out->span = span_end - span_start;
    out->file_size = file_size;
    out->relocations = reloc_count;
    return ELF_OK;
}
