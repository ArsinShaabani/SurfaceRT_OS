# SurfaceRT_OS

An operating system for the ARM UEFI Surface tablets, whose headline feature is
turning the tablet into a **wireless second screen**.  No SDK, no CMake, no
vendor toolchain: `make` produces a bootable USB image, and the tablet can
install the OS onto its own eMMC from that stick.

| Device | SoC | CPU | Platform id |
|---|---|---|---|
| **Surface RT** (2012, first generation) | NVIDIA Tegra 3 (T30) | 4x Cortex-A9 | `tegra30` (default) |
| **Surface 2**, the tablet usually called **"Surface RT 2"** (2013) | NVIDIA Tegra 4 (T114) | 4x Cortex-A15 | `tegra114` |

Both are built from the same tree and both are first-class targets;
`docs/SURFACE-MODELS.md` explains how to tell the two tablets apart and exactly
what differs in the kernel (compiler target, timer, L2, EL2 hand-off).  The x86
Surface Pro family is **not** supported - different firmware, different boot
chain.

```
  make            ->  dist/srtos-usb.img      (bootable, GPT + FAT32 ESP)
  dd ...          ->  USB stick
  boot            ->  loader -> kernel -> shell on the panel and UART
  install ...     ->  the same image on the internal eMMC
  sender.py       ->  tablet becomes a wireless monitor
```

---

## خلاصه به فارسی

این پروژه یک سیستم‌عامل کامل برای تبلت‌های ARM سرفیس است و **دو مدل** را پشتیبانی
می‌کند:

* **Surface RT** (نسل اول، ۲۰۱۲) → Tegra 3 (T30)، چهار هسته Cortex-A9 → پلتفرم `tegra30` (پیش‌فرض)
* **Surface 2** که همان «Surface RT 2» است (۲۰۱۳) → Tegra 4 (T114)، چهار هسته Cortex-A15 → پلتفرم `tegra114`

(سرفیس پرو با پردازنده اینتل پشتیبانی نمی‌شود؛ فریمورش و زنجیره بوتش کاملاً متفاوت است.)

این سیستم‌عامل:

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

روی **لینوکس / مک**:

```sh
./toolchain/fetch-toolchain.sh           # فقط بار اول
make                                     # برای Surface RT (Tegra 3)
make PLATFORM=tegra114 OUT=dist/tegra114 # برای Surface 2 / «Surface RT 2» (Tegra 4)
sudo dd if=dist/srtos-usb.img of=/dev/sdX bs=4M conv=fsync status=progress

# روی تبلت، بعد از بوت از فلش و اتصال کابل سریال (115200):
srtos> net dhcp
srtos> disp start

# روی کامپیوتر:
python3 host/srdisplay/sender.py --host <IP تبلت> --stats
```

روی **ویندوز** (بدون WSL، فقط پایتون ۳ لازم است):

```bat
build.cmd                                        :: Surface RT (Tegra 3)
py scripts\build.py all --platform tegra114 --out dist\tegra114
                                                 :: Surface 2 / «Surface RT 2» (Tegra 4)
:: خروجی: dist\srtos-usb.img  ->  با Rufus در حالت DD روی فلش بنویسید
```

اگر پیام `no arm-none-eabi toolchain found` دیدید، یعنی هنوز کامپایلر نصب نیست؛
یکی از این‌ها کافی است:

```bat
py scripts\build.py all --fetch     :: دانلود + تأیید SHA-256 + بیلد در یک قدم
py scripts\fetch_toolchain.py       :: فقط نصب تول‌چین (~۲۹۰MB، بدون نیاز به ادمین)
py scripts\build.py all --cross C:\msys64\mingw64\bin   :: یا استفاده از نصب موجود
```

بدون اینترنت؟ فایل `xpack-arm-none-eabi-gcc-13.2.1-1.1-win32-x64.zip` را جای دیگر
بگیرید و بدهید: `py scripts\fetch_toolchain.py --archive path\file.zip`
تست‌ها: `build.cmd test`، پاک‌سازی: `build.cmd clean`، وضعیت تول‌چین:
`py scripts\build.py doctor`.

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
| `tests/` | 57 host C checks + 48 Python checks |

## Build

| Host | First time | Then |
|---|---|---|
| Linux / macOS | `./toolchain/fetch-toolchain.sh` | `make` |
| Windows (cmd) | `build.cmd` (fetches the toolchain itself) | `build.cmd` |
| Windows (PowerShell) | `.\build.ps1` | `.\build.ps1` |
| Any OS, explicit | - | `py scripts\build.py all` |

```sh
make test        # host test suites (57 C checks + 48 python checks)
make sim         # display sink simulator
make doctor      # which toolchain was detected
```

The build itself is a single cross platform driver, `scripts/build.py`, so
`make`, `build.cmd` and `build.ps1` produce identical artefacts.  See
[docs/BUILD.md](docs/BUILD.md) for the Windows details (execution policy, MSYS2,
`--cross`, host compiler for the simulator).

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
| [SURFACE-MODELS](docs/SURFACE-MODELS.md) | which Surface is which, and how the Tegra 3 / Tegra 4 ports differ |
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
