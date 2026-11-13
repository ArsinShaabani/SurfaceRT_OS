# Booting on a Surface RT - the honest version

> Looking for the step by step version?  `docs/UNLOCK-AND-BOOT.md` walks through
> preparing the stick, checking the tablet, the Golden Keys -> Yahallo unlock and
> the boot itself, with the tools in `tools/usb/`.

## What the firmware does

The Surface RT (Tegra 3, 2012) ships a 32-bit ARM UEFI firmware with **Secure
Boot enabled and locked**.

* It loads only PE/COFF images whose signature chains to the Microsoft
  production certificate that is fused into the device (`PK`/`KEK`/`db` are not
  user replaceable; there is no setup screen to enrol your own key).
* The default boot order looks for `\EFI\Microsoft\Boot\bootmgfw.efi` (Windows
  RT) and falls back to `\EFI\BOOT\BOOTARM.EFI`.
* There is a boot menu on power-on (volume-down held) that lets you pick a USB
  device, but *not* to disable signature enforcement.

Because of that, **no unsigned operating system can boot on a stock Surface RT**,
no matter how it is packaged. This is a device property, not a limitation of
this project. Anyone who tells you otherwise either has an unlocked device or is
using a Secure Boot bypass.

## What this project does about it

The images are laid out so that every realistic path works without rebuilding:

| File on the ESP | Why |
|---|---|
| `\EFI\BOOT\BOOTARM.EFI` | the standard removable-media path, used by every unlocked ARM UEFI device |
| `\EFI\Microsoft\Boot\bootmgfw.efi` | the path a Surface RT firmware looks at first; replacing it is the classic approach on a device whose Secure Boot has been neutralised |

`BOOTARM.EFI` is a well formed PE32 image for `IMAGE_FILE_MACHINE_ARMNT` with a
`.reloc` section, so it can be loaded at any base and, if you have a way to sign
it with a key the device trusts, signed like any other UEFI application.

## Realistic ways to run it

1. **Unlocked / Secure-Boot-disabled Surface RT.** Some units were unlocked by
   their owners (jailbreak tooling that patches the firmware policy, or the
   known 2016 bootmgr policy bypass). On such a unit, `make` -> `dd` to a USB
   stick -> boot from the stick works directly.
2. **Any other unlocked Tegra 3 UEFI device** (Tegra 3 development boards,
   Ouya-class hardware with an open bootloader). The platform file is isolated in
   `kernel/platform/tegra30/`, so a new board is a new file, not a rewrite.
3. **QEMU + edk2 (AAVMF/edk2-arm).** You can exercise the loader and the kernel's
   early boot on an emulated ARMv7 machine with a UEFI firmware; the Tegra
   specific drivers will not probe, which is exactly what the driver status
   table in `docs/DRIVERS.md` predicts.
4. **The host simulator** (`make sim`) for the userspace-facing feature set: the
   wireless display sink and the protocol stack run bit-identically to the
   tablet build.

## What we deliberately do not do

* We do not ship or embed any signing keys, Microsoft certificates, or
  firmware exploit. That would be both illegal and unmaintainable.
* We do not pretend that an unsigned image will boot on a locked device.

## If you control the signing chain

```sh
# sign the loader like any other UEFI application
sbsign --key db.key --cert db.crt --output dist/BOOTARM.EFI.signed \
       dist/BOOTARM.EFI
# then replace \EFI\BOOT\BOOTARM.EFI (and \EFI\Microsoft\Boot\bootmgfw.efi)
# on the ESP with the signed image
```

Nothing else in the boot chain needs to be signed: after `ExitBootServices()`
the kernel is plain code that the loader relocates into RAM.
