# SurfaceRT_OS

An operating system for the **Microsoft Surface RT** (NVIDIA Tegra 3 / T30,
ARMv7-A) whose headline feature is turning the tablet into a **wireless second
screen**.  No SDK, no CMake, no vendor toolchain: `make` produces a bootable USB
image, and the tablet can install the OS onto its own eMMC from that stick.

```
  make            ->  dist/srtos-usb.img      (bootable, GPT + FAT32 ESP)
  dd ...          ->  USB stick
  boot            ->  loader -> kernel -> shell on the panel and UART
  install ...     ->  the same image on the internal eMMC
  sender.py       ->  tablet becomes a wireless monitor
```

---

## خلاصه به فارسی

این پروژه یک سیستم‌عامل کامل برای Surface RT (پردازنده Tegra 3) است که:

* با یک دستور `make` و **بدون هیچ SDK خاصی** ساخته می‌شود (اگر کراس‌کامپایلر
  نداشته باشید، `./toolchain/fetch-toolchain.sh` یک ابزارشارژ پین‌شده و
  چک‌سام‌دار را دانلود می‌کند)؛
* خروجی آن یک ایمیج USB بوت‌شو است (GPT + پارتیشن FAT32 برای UEFI) که با `dd`
  روی فلش نوشته می‌شود؛
* از روی همان فلش می‌توانید سیستم را روی حافظه داخلی تبلت نصب کنید
  (`install /install/srtos-disk.img 1` در کنسول سریال)؛
* سرویس **مانیتور بی‌سیم** دارد: تبلت صفحه‌ی کامپیوتر شما را روی پنل خودش نشان
  می‌دهد (پروتکل SRDP/1 روی UDP، بدون نیاز به هیچ فریمور اضافه).

### شروع سریع

```sh
./toolchain/fetch-toolchain.sh      # فقط بار اول
make                                # کرنل + لودر + ایمیج‌ها
sudo dd if=dist/srtos-usb.img of=/dev/sdX bs=4M conv=fsync status=progress

# روی تبلت، بعد از بوت از فلش و اتصال کابل سریال (115200):
srtos> net dhcp
srtos> disp start

# روی کامپیوتر:
python3 host/srdisplay/sender.py --host <IP تبلت> --stats
```

### نکته‌ی مهم و صادقانه در مورد بوت

Surface RT از کارخانه با **Secure Boot قفل** عرضه شده و فریمورش تصویر بدون
امضا را اجرا نمی‌کند. ایمیج ما استاندارد است (هم در مسیر
`\EFI\BOOT\BOOTARM.EFI` و هم در مسیر `\EFI\Microsoft\Boot\bootmgfw.efi` قرار
می‌گیرد)، اما برای بوت روی دستگاه قفل‌شده باید Secure Boot غیرفعال/دور زده شده
باشد. جزئیات و گزینه‌ها در `docs/BOOT-AND-SECUREBOOT.md` آمده است؛ این محدودیت
خود دستگاه است، نه پروژه.

جزئیات کامل وضعیت (چه چیزی تست شده و چه چیزی هنوز روی سخت‌افزار واقعی اجرا
نشده) در `docs/STATUS.md` آمده است.

---

## What is inside

| Component | What it does |
|---|---|
| `boot/` | UEFI loader: GOP framebuffer, ESP reader, relocating ELF loader, `ExitBootServices` |
| `tools/elf2pe.py` | self contained ELF32/ARM -> PE32 (ARMNT) converter with base relocations |
| `kernel/arch/arm` | boot, vectors, MMU (1 MiB sections), GIC-400, PL310, calibrated timers, context switch |
| `kernel/core` | page allocator, heap, threads, IRQ core, printk, bootinfo/cmdline |
| `kernel/drivers` | UART, GPIO, I2C, MAX77663 PMIC, SDHCI (eMMC/microSD/SDIO), display controller, framebuffer, EHCI+HID, 88W8797, thermal/fuse |
| `kernel/net` | ARP, IPv4, ICMP, UDP, DHCP client, netdev abstraction |
| `kernel/fs` | VFS, FAT32 (read + in-place write), SRFS, GPT/MBR parsing |
| `kernel/services` | shell, **wireless display sink**, installer, service manager + ELF module loader |
| `services/hello` | example loadable service module (host API, versioned ABI) |
| `host/srdisplay` | sender: screen capture, dirty rectangles, RLE, SRDP/1 encoder |
| `sim/` | host simulator running the real sink engine over real UDP |
| `scripts/` | FAT32 builder, GPT writer, image assembler (pure Python) |
| `tests/` | 51 host C checks + 48 Python checks |

## Build

```sh
./toolchain/fetch-toolchain.sh   # once (or use your distro's gcc-arm-none-eabi)
make                             # everything
make test                        # host test suites
make sim                         # display sink simulator
```

Outputs (in `dist/`): `BOOTARM.EFI`, `esp.img`, `srtos-disk.img`,
`srtos-usb.img`, `MANIFEST.txt` (sizes + SHA-256).

## The wireless display

```sh
# tablet
srtos> net dhcp            # or: net ip 192.168.1.50 255.255.255.0 192.168.1.1
srtos> disp start

# computer
python3 host/srdisplay/sender.py --host 192.168.1.50 --stats
```

The sink decodes dirty rectangles into a 4 MiB back buffer and blits them at
native 1366x768.  Loss only costs the affected rectangle, and the sink asks for a
keyframe when it drops one.  Protocol: `docs/PROTOCOL.md`; usage and tuning:
`docs/WIRELESS-DISPLAY.md`.

You can try the whole pipeline with no tablet at all:

```sh
make sim
build/sim/srtos-sim --width 320 --height 240 --frames 3 --out /tmp &
python3 host/srdisplay/sender.py --host 127.0.0.1 --source test \
        --width 320 --height 240 --frames 3
# srtos-sim: 3 frames, 6 rect blits, 0 dropped rects, 3 ppm written
```

## Extending it

* **service (kernel thread)** - one file in `kernel/services/`, start it in `kmain()`;
* **loadable module** - `services/mymod/mymod.c` exporting
  `srt_module_init(host_api, abi)`, loaded at run time with `svc load`;
* **driver** - add the register block to `tegra30.h`, write `drivers/<class>/x.c`,
  call it from `platform_init()`;
* **shell command** - append one line to the table in `kernel/services/shell.c`.

See `docs/EXTENDING.md` for worked examples.

## Documentation

| Document | Contents |
|---|---|
| [STATUS](docs/STATUS.md) | what works, what is verified, what is still open |
| [BUILD](docs/BUILD.md) | toolchain, targets, how the pieces fit |
| [BOOT-AND-SECUREBOOT](docs/BOOT-AND-SECUREBOOT.md) | the Surface RT boot reality |
| [INSTALL](docs/INSTALL.md) | USB boot, eMMC install, recovery |
| [HARDWARE](docs/HARDWARE.md) | Tegra 3 register map and Surface RT peripherals |
| [DRIVERS](docs/DRIVERS.md) | per-driver state and what to validate first |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | boot flow, memory, scheduler, services |
| [WIRELESS-DISPLAY](docs/WIRELESS-DISPLAY.md) | using and tuning the sink |
| [PROTOCOL](docs/PROTOCOL.md) | SRDP/1 wire format |
| [EXTENDING](docs/EXTENDING.md) | services, modules, drivers, SMP, user space |
| [TESTING](docs/TESTING.md) | what the test suites prove |

## License

MIT - see `LICENSE`.
