# Building SurfaceRT_OS

The whole build is `make`.  No SDK, no package manager, no CMake, no Ninja.

## Requirements

* Linux or macOS (WSL works too)
* `make`, `python3` (3.8+)
* an ARM cross compiler: either
  * **nothing at all** - run `./toolchain/fetch-toolchain.sh` once; it downloads a
    pinned, self-contained `arm-none-eabi-gcc` 13.2.1 into `.toolchain/` and
    verifies its SHA-256, or
  * `gcc-arm-none-eabi` from your distribution (`make CROSS_COMPILE=arm-none-eabi-`)

No mtools, no sgdisk, no mkfs.vfat: the FAT32 and GPT writers live in
`scripts/` and are plain Python.

## Build

```sh
./toolchain/fetch-toolchain.sh   # once, ~250 MB download
make                             # loader + kernel + modules + images
```

What lands where:

```
build/boot/BOOTARM.EFI      the UEFI loader (PE32/ARM)
build/boot/boot.elf         the same loader as ELF (for debugging)
build/kernel/KERNEL.ELF     the kernel, linked at 0x82000000, relocatable
build/kernel/kernel.map     link map
build/services/HELLO.SVC    example loadable service
build/sim/srtos-sim         host simulator for the display sink
dist/BOOTARM.EFI            copy of the loader
dist/esp.img                40 MiB FAT32 EFI system partition
dist/srtos-disk.img         136 MiB GPT image for the internal eMMC
dist/srtos-usb.img          264 MiB GPT USB image (ESP + INSTALL partition)
dist/MANIFEST.txt           sizes and SHA-256 of every artefact
```

## Useful targets

```sh
make image        # rebuild only the images
make test         # 51 host checks (C) + 48 checks (Python)
make sim          # build the display-sink simulator
make clean
```

## How the pieces fit together

```
  C sources            tool              output
  ---------            ----              ------
  boot/*.c      ->  arm-none-eabi-gcc -> boot.elf (position independent)
                                         |
                                         +-> tools/elf2pe.py -> BOOTARM.EFI
                                             (ELF32/ARM -> PE32 ARMNT, emits
                                              PE base relocations)

  kernel/*.c    ->  arm-none-eabi-gcc -> KERNEL.ELF
                     ld --emit-relocs    (keeps relocation tables so the loader
                                          can place the kernel anywhere)

  services/*.c  ->  gcc -shared -fPIC  -> NAME.SVC
                                          (exports srt_module_init, talks to the
                                           kernel only through a host API table)

  scripts/mkimage.py -> FAT32 + GPT -> esp.img / srtos-disk.img / srtos-usb.img
```

### Why a custom ELF->PE converter

The `arm-none-eabi` binutils build that every distribution ships is configured
with the `armelf` BFD target only, so it cannot emit `pei-arm-little`.  Rather
than force users to install llvm-mingw or a special edk2 toolchain, the loader is
linked as a *position independent* shared object (whose only dynamic
relocations are base relative) and `tools/elf2pe.py` re-emits it as PE32:

* it keeps the PE RVA space identical to the ELF virtual addresses, so no
  instruction rewriting is needed;
* `R_ARM_RELATIVE`/`R_ARM_ABS32` become `IMAGE_REL_BASED_HIGHLOW`, and
  `R_ARM_MOVW_ABS_NC`+`R_ARM_MOVT_ABS` pairs become `IMAGE_REL_BASED_ARM_MOV32`;
* anything else is a hard error - silently mis-relocating a boot loader is how
  you get silent data aborts;
* `tools/elf2pe.py --selftest` and the Python test suite verify the output.

### Changing sizes

```sh
python3 scripts/mkimage.py --esp-size $((64*1024*1024)) \
                           --data-size $((512*1024*1024)) \
                           --install-size $((768*1024*1024))
```

FAT32 needs at least 65525 clusters; with 512-byte clusters that means the ESP
cannot be smaller than ~32 MiB.  Larger partitions simply get more clusters.
