#!/usr/bin/env python3
"""
elf2pe.py - convert a position independent ELF32/ARM shared object into a
UEFI PE32 application image (Machine = ARMNT).

Why this exists
---------------
The UEFI firmware of the Microsoft Surface RT (NVIDIA Tegra 3 / T30) only
executes PE/COFF images from the EFI System Partition.  The GNU Arm Embedded
toolchain shipped by every distro is configured with the `armelf` BFD target
only, i.e. it cannot emit `pei-arm-little`.  Instead of forcing users to
install a special SDK, we link the loader as a *position independent* ELF
(`ld -shared -Bsymbolic`) whose only dynamic relocations are base relative,
and re-emit that as PE here.

The conversion is deliberately conservative:

  * PE RVA of every byte equals its ELF virtual address.  That keeps every
    PC-relative reference and every relocated addend valid, so no instruction
    rewriting is needed - only base relocations have to be emitted.
  * R_ARM_RELATIVE / R_ARM_ABS32            -> IMAGE_REL_BASED_HIGHLOW
  * R_ARM_MOVW_ABS_NC + R_ARM_MOVT_ABS pair -> IMAGE_REL_BASED_ARM_MOV32
  * Anything else is a hard error: silently mis-relocating a boot loader is
    how you get silent data aborts, so we fail loudly instead.

Usage:
    tools/elf2pe.py BOOT.ELF BOOTARM.EFI [--subsystem N] [--verify-only]
    tools/elf2pe.py --selftest
"""

import argparse
import struct
import sys

# ---------------------------------------------------------------- ELF bits ---
ET_DYN = 3
PT_LOAD, PT_DYNAMIC = 1, 2
EM_ARM = 40
PF_X, PF_W = 1, 2
SHT_REL = 9
SHF_ALLOC = 2

R_ARM_ABS32 = 2
R_ARM_RELATIVE = 23
R_ARM_MOVW_ABS_NC = 43
R_ARM_MOVT_ABS = 44
R_ARM_MOVW_PREL_NC = 45
R_ARM_MOVT_PREL = 46

DT_NULL, DT_REL, DT_RELSZ, DT_RELENT = 0, 17, 18, 19

# ------------------------------------------------------------------ PE bits ---
IMAGE_FILE_MACHINE_ARMNT = 0x01C4
PE32_MAGIC = 0x10B

IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004
IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008
IMAGE_FILE_32BIT_MACHINE = 0x0100
IMAGE_FILE_DEBUG_STRIPPED = 0x0200

IMAGE_SUBSYSTEM_EFI_APPLICATION = 10
IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11
IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

DIR_BASERELOC = 5
DIR_ENTRIES = 16

IMAGE_REL_BASED_ABSOLUTE = 0
IMAGE_REL_BASED_HIGHLOW = 3
IMAGE_REL_BASED_ARM_MOV32 = 5

PAGE_SIZE = 0x1000

# magic, linker ver, sizes, entry/base, imagebase, alignments, versions,
# win32ver, sizes, checksum, subsystem/dllchars, stack, heap, flags, nrva
OPT_FMT = "<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII"
assert struct.calcsize(OPT_FMT) == 96, struct.calcsize(OPT_FMT)


class ToolError(Exception):
    pass


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:4] != b"\x7fELF":
            raise ToolError("not an ELF file")
        if data[4] != 1 or data[5] != 1:
            raise ToolError("only ELF32 little endian is supported")
        (self.e_type, self.e_machine, _ver, self.e_entry, e_phoff, e_shoff,
         _flags, _ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
         e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if self.e_machine != EM_ARM:
            raise ToolError("not an ARM ELF (machine=%d)" % self.e_machine)
        if self.e_type != ET_DYN:
            raise ToolError("ELF must be position independent: link with "
                            "'ld -shared -Bsymbolic'")

        self.phdrs = []
        for i in range(e_phnum):
            off = e_phoff + i * e_phentsize
            (p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_flags,
             p_align) = struct.unpack_from("<IIIIIIII", data, off)
            self.phdrs.append(dict(type=p_type, offset=p_offset, vaddr=p_vaddr,
                                   filesz=p_filesz, memsz=p_memsz,
                                   flags=p_flags, align=p_align))

        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
             sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
                "<IIIIIIIIII", data, off)
            self.sections.append(dict(name_off=sh_name, type=sh_type,
                                      flags=sh_flags, addr=sh_addr,
                                      offset=sh_offset, size=sh_size,
                                      link=sh_link, info=sh_info,
                                      ent=sh_entsize))
        self.strtab = b""
        if e_shstrndx < len(self.sections):
            st = self.sections[e_shstrndx]
            self.strtab = data[st["offset"]:st["offset"] + st["size"]]
        for s in self.sections:
            s["name"] = self._str(s["name_off"])

        self.loads = [p for p in self.phdrs if p["type"] == PT_LOAD]
        if not self.loads:
            raise ToolError("ELF has no PT_LOAD segments")

    def _str(self, off):
        if not self.strtab:
            return ""
        end = self.strtab.find(b"\0", off)
        return self.strtab[off:end].decode("latin1")

    def read(self, vaddr, size):
        for p in self.loads:
            if p["vaddr"] <= vaddr and vaddr + size <= p["vaddr"] + p["filesz"]:
                rel = vaddr - p["vaddr"]
                return self.data[p["offset"] + rel:p["offset"] + rel + size]
        raise ToolError("vaddr 0x%x (+%d) is not inside a file-backed segment"
                        % (vaddr, size))

    def read_u32(self, vaddr):
        return struct.unpack("<I", self.read(vaddr, 4))[0]

    def section_by_name(self, name):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def relocations(self):
        """Return [(vaddr, elf_reloc_type)] for every dynamic relocation."""
        rels = []
        shdrs = [s for s in self.sections if s["type"] == SHT_REL]
        if shdrs:
            for s in shdrs:
                for i in range(s["size"] // 8):
                    r_offset, r_info = struct.unpack_from(
                        "<II", self.data, s["offset"] + i * 8)
                    rels.append((r_offset, r_info & 0xFF))
            return rels

        # Fall back to PT_DYNAMIC (useful when section headers are stripped).
        dyn = self.section_by_name(".dynamic")
        if dyn is None:
            for p in self.phdrs:
                if p["type"] == PT_DYNAMIC:
                    dyn = dict(offset=p["offset"], size=p["filesz"])
        if dyn is None:
            return rels
        rel_addr = rel_sz = rel_ent = None
        for i in range(dyn["size"] // 8):
            tag, val = struct.unpack_from("<II", self.data, dyn["offset"] + i * 8)
            if tag == DT_NULL:
                break
            if tag == DT_REL:
                rel_addr = val
            elif tag == DT_RELSZ:
                rel_sz = val
            elif tag == DT_RELENT:
                rel_ent = val
        if rel_addr is None or not rel_sz:
            return rels
        rel_ent = rel_ent or 8
        blob = self.read(rel_addr, rel_sz)
        for i in range(rel_sz // rel_ent):
            r_offset, r_info = struct.unpack_from("<II", blob, i * rel_ent)
            rels.append((r_offset, r_info & 0xFF))
        return rels


def build_pe(elf, subsystem, section_align, file_align=0x20):
    loads = sorted(elf.loads, key=lambda p: p["vaddr"])
    base = min(p["vaddr"] for p in loads)
    if base < section_align:
        raise ToolError(
            "first PT_LOAD starts at 0x%x, which collides with the PE headers; "
            "link with a script that places .text at >= 0x1000" % base)

    # ---- base relocations -------------------------------------------------
    by_off = {}
    for off, typ in elf.relocations():
        by_off.setdefault(off, typ)

    used = set()
    entries = []          # (pe_type, rva)
    for off in sorted(by_off):
        if off in used:
            continue
        typ = by_off[off]
        if typ in (R_ARM_RELATIVE, R_ARM_ABS32):
            entries.append((IMAGE_REL_BASED_HIGHLOW, off))
            used.add(off)
        elif typ == R_ARM_MOVW_ABS_NC and by_off.get(off + 4) == R_ARM_MOVT_ABS:
            entries.append((IMAGE_REL_BASED_ARM_MOV32, off))
            used.add(off)
            used.add(off + 4)
        elif typ in (R_ARM_MOVW_PREL_NC, R_ARM_MOVT_PREL):
            used.add(off)                      # PC relative: load address free
        else:
            raise ToolError("unsupported relocation R_ARM_%d at vaddr 0x%x"
                            % (typ, off))

    pages = {}
    for typ, rva in entries:
        pages.setdefault(rva & ~(PAGE_SIZE - 1), []).append(
            (typ, rva & (PAGE_SIZE - 1)))
    reloc = bytearray()
    for page in sorted(pages):
        body = bytearray()
        for typ, off in sorted(pages[page], key=lambda e: e[1]):
            body += struct.pack("<H", (typ << 12) | (off & 0xFFF))
        if len(body) % 4:
            body += struct.pack("<H", IMAGE_REL_BASED_ABSOLUTE << 12)
        reloc += struct.pack("<II", page, 8 + len(body)) + body

    # ---- sections (mirror the ELF virtual layout exactly) ------------------
    pe_sections = []
    for p in loads:
        if p["vaddr"] % section_align:
            raise ToolError("segment vaddr 0x%x is not %d-byte aligned"
                            % (p["vaddr"], section_align))
        raw = elf.data[p["offset"]:p["offset"] + p["filesz"]] if p["filesz"] else b""
        if not p["filesz"]:
            name = ".bss"
            chars = (IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                     IMAGE_SCN_MEM_WRITE)
        elif p["flags"] & PF_X:
            name = ".text"
            chars = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                     IMAGE_SCN_MEM_READ)
        elif p["flags"] & PF_W:
            name = ".data"
            chars = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                     IMAGE_SCN_MEM_WRITE)
        else:
            name = ".rdata"
            chars = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ)
        pe_sections.append(dict(name=name, va=p["vaddr"], vsize=p["memsz"],
                                data=raw, chars=chars))
    pe_sections.sort(key=lambda s: s["va"])

    if not reloc:
        # A PE image without a .reloc section is treated as RELOCS_STRIPPED by
        # most firmwares, which then refuse to load it anywhere but at
        # ImageBase.  A lone ABSOLUTE padding entry is a no-op fixup that keeps
        # the image loadable at any base (our code is fully PC relative).
        reloc = bytearray(struct.pack("<III", 0, 12, 0))

    prev_end = 0
    for s in pe_sections:
        if s["va"] < prev_end:
            raise ToolError("overlapping segments around vaddr 0x%x" % s["va"])
        prev_end = s["va"] + max(s["vsize"], len(s["data"]))

    reloc_rva = reloc_size = 0
    if reloc:
        reloc_rva = align_up(prev_end, section_align)
        reloc_size = len(reloc)
        pe_sections.append(dict(name=".reloc", va=reloc_rva, vsize=reloc_size,
                                data=bytes(reloc),
                                chars=(IMAGE_SCN_CNT_INITIALIZED_DATA |
                                       IMAGE_SCN_MEM_READ)))
        prev_end = reloc_rva + reloc_size

    # ---- headers -----------------------------------------------------------
    opt_size = struct.calcsize(OPT_FMT) + DIR_ENTRIES * 8
    headers_end = 0x40 + 4 + 20 + opt_size + 40 * len(pe_sections)
    size_of_headers = align_up(headers_end, file_align)
    if size_of_headers > pe_sections[0]["va"]:
        raise ToolError("PE headers (0x%x bytes) do not fit below the first "
                        "section at 0x%x" % (size_of_headers, pe_sections[0]["va"]))

    size_of_image = align_up(prev_end, section_align)
    entry_rva = elf.e_entry   # RVA == ELF vaddr because ImageBase is 0
    if not 0 < entry_rva < size_of_image:
        raise ToolError("bad entry point 0x%x (size_of_image=0x%x)"
                        % (entry_rva, size_of_image))

    code = sum(len(s["data"]) for s in pe_sections
               if s["chars"] & IMAGE_SCN_MEM_EXECUTE)
    init = sum(len(s["data"]) for s in pe_sections
               if s["chars"] & IMAGE_SCN_CNT_INITIALIZED_DATA)
    uninit = sum(s["vsize"] for s in pe_sections
                 if s["chars"] & IMAGE_SCN_CNT_UNINITIALIZED_DATA)
    base_of_code = pe_sections[0]["va"]
    data_vas = [s["va"] for s in pe_sections
                if s["chars"] & IMAGE_SCN_CNT_INITIALIZED_DATA]
    base_of_data = min(data_vas) if data_vas else base_of_code

    out = bytearray()
    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)
    out += dos + b"PE\0\0"
    characteristics = (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_32BIT_MACHINE |
                       IMAGE_FILE_LINE_NUMS_STRIPPED |
                       IMAGE_FILE_LOCAL_SYMS_STRIPPED | IMAGE_FILE_DEBUG_STRIPPED)
    out += struct.pack("<HHIIIHH", IMAGE_FILE_MACHINE_ARMNT, len(pe_sections),
                       0, 0, 0, opt_size, characteristics)
    out += struct.pack(OPT_FMT, PE32_MAGIC, 2, 0,
                       code & 0xFFFFFFFF, init & 0xFFFFFFFF, uninit & 0xFFFFFFFF,
                       entry_rva, base_of_code, base_of_data,
                       0,                                # ImageBase (reloc'd)
                       section_align, file_align,
                       0, 0, 0, 0, 0, 0,                 # OS/image/subsys ver
                       0,                                # Win32VersionValue
                       size_of_image, size_of_headers, 0,  # sizes, checksum
                       subsystem, 0,                     # subsystem, dll chars
                       0x00010000, 0x00010000,           # stack reserve/commit
                       0x00010000, 0x00010000,           # heap reserve/commit
                       0, DIR_ENTRIES)
    dirs = bytearray(DIR_ENTRIES * 8)
    struct.pack_into("<II", dirs, DIR_BASERELOC * 8, reloc_rva, reloc_size)
    out += dirs

    raws = []
    raw_ptr = size_of_headers
    for s in pe_sections:
        raw = s["data"]
        padded = raw + b"\0" * (align_up(len(raw), file_align) - len(raw))
        raws.append(padded)
        out += struct.pack("<8sIIIIIIHHI", s["name"].encode("ascii")[:8],
                           s["vsize"], s["va"], len(padded),
                           raw_ptr if padded else 0, 0, 0, 0, 0, s["chars"])
        raw_ptr += len(padded)

    out += b"\0" * (size_of_headers - len(out))
    for raw in raws:
        out += raw
    assert len(out) == raw_ptr, (len(out), raw_ptr)
    return bytes(out)


# ------------------------------------------------------------------- verify ---
def parse_pe(data):
    if data[:2] != b"MZ":
        raise ToolError("missing MZ signature")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ToolError("missing PE signature")
    off = e_lfanew + 4
    (machine, nsec, _ts, _psym, _nsym, opt_size,
     chars) = struct.unpack_from("<HHIIIHH", data, off)
    off += 20
    (magic, _lvmaj, _lvmin, size_code, size_init, size_uninit, entry, base_code,
     base_data, image_base, sect_align, file_align, _v1, _v2, _v3, _v4, _v5,
     _v6, _win32, size_image, size_headers, _cksum, subsystem, _dllc, _sr, _sc,
     _hr, _hc, _lf, nrva) = struct.unpack_from(OPT_FMT, data, off)
    off += struct.calcsize(OPT_FMT)
    dirs = [struct.unpack_from("<II", data, off + i * 8) for i in range(nrva)]
    off += nrva * 8
    sections = []
    for _ in range(nsec):
        (name, vsize, va, rawsize, rawptr, _pr, _pl, _nr, _nl,
         sc) = struct.unpack_from("<8sIIIIIIHHI", data, off)
        off += 40
        sections.append(dict(name=name.rstrip(b"\0").decode("latin1"),
                             vsize=vsize, va=va, rawsize=rawsize,
                             rawptr=rawptr, chars=sc))
    return dict(magic=magic, machine=machine, sections=sections, entry=entry,
                image_base=image_base, sect_align=sect_align,
                file_align=file_align, size_image=size_image,
                size_headers=size_headers, subsystem=subsystem, dirs=dirs,
                characteristics=chars, size_code=size_code,
                size_init=size_init, size_uninit=size_uninit,
                base_of_code=base_code, base_of_data=base_data)


def verify_pe(data, expect_subsystem):
    info = parse_pe(data)
    errs = []
    if info["magic"] != PE32_MAGIC:
        errs.append("optional header magic 0x%x is not PE32" % info["magic"])
    if info["machine"] != IMAGE_FILE_MACHINE_ARMNT:
        errs.append("machine 0x%x is not ARMNT (0x1c4)" % info["machine"])
    if info["subsystem"] != expect_subsystem:
        errs.append("subsystem %d != %d" % (info["subsystem"], expect_subsystem))
    if info["image_base"] != 0:
        errs.append("image base 0x%x != 0" % info["image_base"])
    if info["sect_align"] < info["file_align"]:
        errs.append("SectionAlignment < FileAlignment")
    if info["size_headers"] % info["file_align"]:
        errs.append("SizeOfHeaders is not FileAlignment aligned")
    if info["size_image"] % info["sect_align"]:
        errs.append("SizeOfImage is not SectionAlignment aligned")
    names = [s["name"] for s in info["sections"]]
    if not names or names[0] != ".text":
        errs.append("first section is %r, expected .text" % (names[:1],))
    exec_secs = [s for s in info["sections"] if s["chars"] & IMAGE_SCN_MEM_EXECUTE]
    if not any(s["va"] <= info["entry"] < s["va"] + s["vsize"] for s in exec_secs):
        errs.append("entry 0x%x is outside every executable section"
                    % info["entry"])
    last_end = 0
    for s in info["sections"]:
        if s["va"] % info["sect_align"]:
            errs.append("section %s vaddr 0x%x misaligned" % (s["name"], s["va"]))
        if s["va"] < last_end:
            errs.append("section %s overlaps the previous one" % s["name"])
        if s["rawptr"] and s["rawptr"] % info["file_align"]:
            errs.append("section %s raw pointer misaligned" % s["name"])
        if s["rawptr"] + s["rawsize"] > len(data):
            errs.append("section %s extends past EOF" % s["name"])
        last_end = s["va"] + max(s["vsize"], s["rawsize"])
    if last_end > info["size_image"]:
        errs.append("sections extend past SizeOfImage")

    rva, size = info["dirs"][DIR_BASERELOC]
    count = 0
    npages = 0
    if rva and size:
        pos = None
        for s in info["sections"]:
            if s["va"] <= rva < s["va"] + s["rawsize"]:
                pos = s["rawptr"] + (rva - s["va"])
        if pos is None:
            errs.append("relocation directory RVA 0x%x is not backed by file data"
                        % rva)
            pos = 0; end = 0
        end = pos + size
        while pos < end:
            page, bsize = struct.unpack_from("<II", data, pos)
            if bsize < 8 or pos + bsize > end:
                errs.append("corrupt relocation block at 0x%x" % pos)
                break
            npages += 1
            for i in range((bsize - 8) // 2):
                ent, = struct.unpack_from("<H", data, pos + 8 + i * 2)
                typ = ent >> 12
                target = page + (ent & 0xFFF)
                if typ == IMAGE_REL_BASED_ABSOLUTE:
                    continue
                if typ not in (IMAGE_REL_BASED_HIGHLOW, IMAGE_REL_BASED_ARM_MOV32):
                    errs.append("reloc type %d at 0x%x" % (typ, target))
                if not [s for s in info["sections"]
                        if s["va"] <= target < s["va"] + s["vsize"]]:
                    errs.append("reloc at 0x%x is outside every section" % target)
                count += 1
            pos += bsize
    info["reloc_count"] = count
    info["reloc_pages"] = npages
    return info, errs


# ----------------------------------------------------------------- selftest ---
def _make_test_elf():
    """Hand-assemble a small PIC ELF: .text@0x1000, .data@0x2000, .rel.dyn."""
    text = struct.pack("<III", 0xE3000000, 0xE3400000, 0xE1A0F00E)
    data = struct.pack("<II", 0x1000, 0xDEADBEEF)
    shstr = b"\0.text\0.data\0.rel.dyn\0.shstrtab\0"
    names = {n: shstr.index(n.encode()) for n in (".text", ".data", ".rel.dyn",
                                                  ".shstrtab")}
    ehsize, phsize, shsize = 52, 32, 40
    phoff, text_off, data_off, rel_off, str_off = ehsize, 0x1000, 0x2000, 0x3000, 0x3100
    shoff = 0x3200
    rel_blob = b"".join(struct.pack("<II", o, t) for o, t in
                        ((0x1000, R_ARM_MOVW_ABS_NC), (0x1004, R_ARM_MOVT_ABS),
                         (0x2000, R_ARM_RELATIVE)))
    out = bytearray(shoff + 5 * shsize)
    out[0:4] = b"\x7fELF"
    out[4] = out[5] = out[6] = 1
    struct.pack_into("<HHIIIIIHHHHHH", out, 16, ET_DYN, EM_ARM, 1, 0x1000,
                     phoff, shoff, 0, ehsize, phsize, 2, shsize, 5, 4)
    struct.pack_into("<IIIIIIII", out, phoff, PT_LOAD, text_off, 0x1000, 0x1000,
                     len(text), len(text), PF_X, 0x1000)
    struct.pack_into("<IIIIIIII", out, phoff + phsize, PT_LOAD, data_off, 0x2000,
                     0x2000, len(data), len(data), PF_W | 4, 0x1000)
    out[text_off:text_off + len(text)] = text
    out[data_off:data_off + len(data)] = data
    out[rel_off:rel_off + len(rel_blob)] = rel_blob
    out[str_off:str_off + len(shstr)] = shstr

    def shdr(i, name, typ, flags, addr, off, size, ent, info=0):
        struct.pack_into("<IIIIIIIIII", out, shoff + i * shsize, name, typ, flags,
                         addr, off, size, 0, info, 4, ent)

    shdr(0, 0, 0, 0, 0, 0, 0, 0)
    shdr(1, names[".text"], 1, SHF_ALLOC | 4, 0x1000, text_off, len(text), 0)
    shdr(2, names[".data"], 1, SHF_ALLOC | 1, 0x2000, data_off, len(data), 0)
    shdr(3, names[".rel.dyn"], SHT_REL, 0, 0, rel_off, len(rel_blob), 8)
    shdr(4, names[".shstrtab"], 3, 0, 0, str_off, len(shstr), 0)
    return bytes(out)


def selftest():
    elf = Elf(_make_test_elf())
    pe = build_pe(elf, IMAGE_SUBSYSTEM_EFI_APPLICATION, 0x20)
    info, errs = verify_pe(pe, IMAGE_SUBSYSTEM_EFI_APPLICATION)
    names = [s["name"] for s in info["sections"]]
    checks = [
        (not errs, "verify errors: %s" % errs),
        (info["entry"] == 0x1000, "entry 0x%x != 0x1000" % info["entry"]),
        (info["reloc_count"] == 2, "relocs %d != 2" % info["reloc_count"]),
        (info["size_image"] == align_up(0x2020 + 24, 0x20),
         "size_of_image 0x%x" % info["size_image"]),
        (names == [".text", ".data", ".reloc"], "sections %s" % names),
        (info["reloc_pages"] == 2, "reloc pages %d" % info["reloc_pages"]),
    ]
    failed = [msg for ok, msg in checks if not ok]
    if failed:
        print("selftest FAILED:\n  " + "\n  ".join(failed))
        return 1
    print("selftest ok: %d bytes, sections=%s, relocs=%d in %d page(s)"
          % (len(pe), ",".join(names), info["reloc_count"], info["reloc_pages"]))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description="ELF32/ARM -> UEFI PE32 converter")
    ap.add_argument("input", nargs="?")
    ap.add_argument("output", nargs="?")
    ap.add_argument("--subsystem", type=int,
                    default=IMAGE_SUBSYSTEM_EFI_APPLICATION)
    ap.add_argument("--section-align", type=int, default=0x20)
    ap.add_argument("--verify-only", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.input or (not args.output and not args.verify_only):
        ap.error("input and output are required")

    try:
        if args.verify_only:
            with open(args.input, "rb") as f:
                data = f.read()
        else:
            with open(args.input, "rb") as f:
                elf = Elf(f.read())
            data = build_pe(elf, args.subsystem, args.section_align)
            with open(args.output, "wb") as f:
                f.write(data)
        info, errs = verify_pe(data, args.subsystem)
    except ToolError as exc:
        print("elf2pe: error: %s" % exc, file=sys.stderr)
        return 1

    if not args.quiet:
        print("elf2pe: %s -> %s" % (args.input, args.output or "(verify only)"))
        print("  machine=ARMNT subsystem=%d entry=0x%x size_of_image=0x%x"
              % (info["subsystem"], info["entry"], info["size_image"]))
        print("  sections: " + ", ".join(
            "%s@0x%x(raw %d, virt %d)" % (s["name"], s["va"], s["rawsize"],
                                           s["vsize"]) for s in info["sections"]))
        print("  base relocations: %d in %d page block(s)"
              % (info["reloc_count"], info["reloc_pages"]))
    if errs:
        for e in errs:
            print("elf2pe: verify: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
