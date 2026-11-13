# Making the USB stick actually boot on a Surface RT / Surface 2

This is the practical, step by step version of
`docs/BOOT-AND-SECUREBOOT.md`.  It is written for the two Tegra tablets this
project supports:

| Tablet | SoC | Platform id | UEFI version Yahallo needs |
|---|---|---|---|
| Surface RT (2012) | Tegra 3 (T30) | `tegra30` | **3.31.500** |
| Surface 2, a.k.a. "Surface RT 2" (2013) | Tegra 4 (T114) | `tegra114` | **4.22.500** |

The short version: the firmware *will* boot from USB on both tablets, but only
images signed by Microsoft until Secure Boot is switched off.  Turning it off is
a solved problem on these Tegra devices - and it is a firmware/key operation,
not a patch to our OS.

---

## What this repository ships, and what it does not

| Step | Who provides it |
|---|---|
| A bootable 264 MiB USB image with a correct GPT + ESP | **us** (`dist/*/srtos-usb.img`) |
| Flashers for Windows and Linux, with read-back verification | **us** (`tools/usb/flash-*.ps1|sh`) |
| A stick verifier (GPT, ESP, loader PE, kernel platform marker) | **us** (`tools/usb/verify_stick.py`) |
| A device readiness check (model, UEFI version, Secure Boot, BitLocker) | **us** (`tools/usb/device-check.ps1`) |
| A permanent installer that replaces `bootmgfw.efi` on the tablet's ESP | **us** (`tools/usb/install-esp.ps1`) |
| The Secure Boot unlock itself (Golden Keys, Yahallo) | **you** - third party tools, see the links below. We do not redistribute Microsoft policy blobs or community exploit binaries. |

---

## Step 1 - build and flash the stick

One command builds everything and puts it in a single folder you can copy to the
machine that owns the USB writer:

```sh
make usb PLATFORM=tegra114 OUT=dist/tegra114      # Surface 2 / "RT 2"
make usb                                          # Surface RT
# -> dist/tegra114/usb/  = image + flashers + verifier + this guide + README
```

Windows equivalent:

```bat
py scripts\build.py all --platform tegra114 --out dist\tegra114 --usb-package
```

The folder contains `srtos-usb.img`, `flash-windows.ps1`, `flash-linux.sh`,
`device-check.ps1`, `install-esp.ps1`, `verify_stick.py`, `SHA256SUMS.txt`,
`README.txt` and this document.  Then flash it:

```sh
sudo tools/usb/flash-linux.sh dist/tegra114/usb/srtos-usb.img
```

```powershell
powershell -ExecutionPolicy Bypass -File dist\tegra114\usb\flash-windows.ps1
```

Both flashers only offer removable disks, refuse the system disk, dismount the
volumes, write, **read the image back and compare it**, and finally run
`verify_stick.py`.  If that script says `all checks passed`, the stick is good
and any remaining problem is the tablet's boot policy, not the stick.

---

## Step 2 - check the tablet

Copy `tools/usb/device-check.ps1` to the tablet and run it in an elevated
PowerShell.  It prints the model, the UEFI version, the Secure Boot state, the
Windows RT build and the BitLocker state, and tells you whether this tablet is
supported and whether the stick you inserted is built for it.

Expected before unlocking: `Secure Boot ... ON`.  That is why the stick does
nothing yet when you try the button combination.

---

## Step 3 - Golden Keys / Longhorn (Secure Boot debug policy)

Needs **Windows RT 8.1** on the tablet, with an installation that has **not been
updated past October 2016** (later updates actively block this; on Tegra devices
a Bare Metal Recovery image resets the state, on the Qualcomm devices - Dell XPS
10, Lumia 2520, Ativ Tab - it is permanent).

Procedure (from the community documentation, links below):

1. Download the *Secure Boot Debug Policy Applicator*, extract it **on the
tablet's own storage** (not on a USB stick or network share).
2. Right click `InstallPolicy.cmd` -> *Run as administrator*, wait; the device
   reboots by itself after ~30 s.
3. The policy applicator appears on the next boot: select **Accept and Install**
   with the volume keys, confirm with the Windows key.
4. Back in Windows, in an elevated command prompt:

   ```bat
   bcdedit /set {bootmgr} testsigning on
   bcdedit /set {default} testsigning on
   ```

5. Reboot.  A *Test Mode* watermark should appear in the corner of the desktop
   (it may be missing if Windows is not activated - that is cosmetic).

What this does: it installs a signed *debug policy* that puts the device into
Test Mode, so test-signed boot applications can run.  It does **not** by itself
disable Secure Boot.

---

## Step 4 - Yahallo (turns Secure Boot off permanently)

Yahallo is a TrustZone/UEFI variable-services exploit for **Tegra 3 and Tegra 4**
that permanently turns Secure Boot off.  Requirements, per its documentation:

* Surface RT with UEFI **3.31.500**, or Surface 2 with UEFI **4.22.500**
  (check with `msinfo32` -> *BIOS Version/Date*; update the UEFI first if needed)
* Golden Keys already applied (Step 3) - otherwise you get `0xc0000428`
* a FAT32 USB stick of 1 MB or more, with the Yahallo files in its root

Procedure:

1. Format a small stick as FAT32 and extract the Yahallo package to its root.
2. Power the tablet off, insert the stick.
3. Hold **Volume-Down**, press **Power**, release Volume-Down when the Surface
   logo appears.
4. Wait for the confirmation and reboot when prompted.
5. Verify: `msinfo32.exe` -> *Secure Boot State* should now read **Off**.

From this point the firmware will happily load an unsigned
`\EFI\BOOT\BOOTARM.EFI`.

---

## Step 5 - boot SurfaceRT_OS

1. Power off, insert the SurfaceRT_OS stick from Step 1.
2. Hold **Volume-Down**, press **Power**, release Volume-Down at the **Surface
   logo** (same combination for Surface RT and Surface 2).
3. The firmware boot menu appears; pick the USB device.
4. You should see the loader banner, then the kernel, then the shell on the
   panel and on UART-A at 115200 8N1.

If the firmware shows *"Windows cannot verify the digital signature for this
file"* (0xc0000428), Secure Boot is still on - go back to Step 4.
If it shows nothing at all, try the other USB port, and re-check the stick with
`python3 tools/usb/verify_stick.py <device>`.

---

## Step 6 (optional) - install to the internal eMMC

From the tablet's serial shell, with the stick still inserted:

```
srtos> blk
srtos> install /install/srtos-disk.img 1
```

The install writes the GPT + ESP + data partition to `mmcblk0` and verifies it.
Windows RT is gone after this - see `docs/INSTALL.md` for the recovery notes.

## Step 6b (optional) - keep Windows and dual boot from the internal ESP

Once Secure Boot is off, the tablet can start SRTOS from its own storage and keep
Windows RT intact.  The script refuses to run while BitLocker is on and is fully
reversible:

```powershell
:: preferred: keep Windows' boot manager, add a firmware boot entry
powershell -ExecutionPolicy Bypass -File tools\usb\install-esp.ps1 -Source D:\

:: only if the firmware ignores the extra entry: take over bootmgfw.efi
tools\usb\install-esp.ps1 -Source D:\ -ReplaceBootmgr

:: undo everything
powershell -ExecutionPolicy Bypass -File tools\usb\install-esp.ps1 -Restore
```

How the choice works:

* the default mode copies the loader to `\EFI\SRTOS\BOOTARM.EFI` and the
  kernel to `\SRTOS`, then registers a firmware entry with `bcdedit /copy`,
  `/set path` and `{fwbootmgr} displayorder`.  Windows keeps its own entry, so
  the firmware boot menu lists both;
* with `-ReplaceBootmgr` our loader takes the place the firmware always tries
  (`\EFI\Microsoft\Boot\bootmgfw.efi`).  The original is preserved as
  `bootmgfw-windows.efi`, and the loader reads the 16 byte selector
  `\SRTOS\DEFAULT`: `srtos` boots the kernel, `windows` chainloads the
  preserved Windows boot manager.  Flip it from the tablet's own shell:

  ```
  srtos> boot
  next boot: srtos   (usage: boot srtos|windows)
  srtos> boot windows
  boot: next boot -> windows (reboot to apply)
  ```

---

## Why this works (and what it costs)

* The Surface RT and Surface 2 are **end of life**; Microsoft and NVIDIA both
  declined to fix the Tegra TrustZone issue (2020), which is why this path
exists at all.
* Turning Secure Boot off weakens the device's boot integrity permanently: any
  code on a stick or on the ESP will run.  That is exactly what a hobby OS
  needs, and exactly what you should not do to a device holding data you care
  about.  Decrypt BitLocker and back up first.
* Some antivirus products flag the Yahallo binary (it is an exploit).  Download
  it from the project's own releases, never from a mirror.
* We do not ship any of those files.  Everything in this repository is ours and
  MIT licensed; the unlock tools are third party and under their own licences.

## Sources

* Boot button combinations: <https://windows-rt-devices.gitbook.io/windows/miscellaneous/boot-from-usb>
* Golden Keys / Longhorn: <https://windows-rt-devices.gitbook.io/windows/jailbreaks/golden-keys-longhorn>
* Yahallo: <https://windows-rt-devices.gitbook.io/windows/jailbreaks/yahallo>
* Bare Metal Recovery (if the tablet was updated past October 2016):
  <https://windows-rt-devices.gitbook.io/windows/miscellaneous/bare-metal-recovery>
* Our own take on the firmware and the signing chain: `docs/BOOT-AND-SECUREBOOT.md`
