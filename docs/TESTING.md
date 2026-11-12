# Testing

```sh
make test      # host C tests + python tests
make sim       # build the display sink simulator
```

## What the C tests cover (`tests/test_main.c`)

| Test | What it proves |
|---|---|
| `strings`, `printf` | the freestanding libc replacement behaves like the real thing (widths, precision, `%llu`, `%p`) |
| `heap` | allocation, coalescing, 40 KiB block reuse after fragmentation, double-free safety |
| `srdp raw` | a fragmented rectangle reassembles byte-exactly into the back buffer |
| `srdp rle` | run length decoding, exact pixel placement, dirty rect reported |
| `srdp malformed` | 2000 fuzzed packets plus out-of-panel rectangles, oversized payloads and orphan fragments never crash or write outside the back buffer |
| `elfload` | loads the real `KERNEL.ELF` at a 1 MiB bias and verifies **every** `R_ARM_ABS32` site equals `file value + bias` (198 of them) |

## What the Python tests cover (`tests/pytests/run_tests.py`)

| Test | What it proves |
|---|---|
| `elf2pe` | the converter self test: relocations, section layout, entry point |
| `fat32` | builder and an *independent* reader agree; long file names; boot sector fields |
| `gpt` | primary + backup headers, protective MBR, header CRC |
| `srdp wire format` | header/struct sizes match the C header, handshake layout, fragment maths, RLE round trip |
| `image assembly` | a full `mkimage.py` run: ESP mounted by the reader, `BOOTARM.EFI` parsed as ARMNT/EFI-app PE, `KERNEL.ELF` extracted and checked |

## End-to-end display test (no tablet)

```sh
make sim
build/sim/srtos-sim --width 320 --height 240 --frames 3 --out /tmp &
python3 host/srdisplay/sender.py --host 127.0.0.1 --source test \
        --width 320 --height 240 --frames 3
```

Expected output from the simulator:

```
srtos-sim: session 1234567 from 127.0.0.1:…
srtos-sim: frame 1, 0 rects, 0 dropped, 301 kB
srtos-sim: 3 frames, 6 rect blits, 0 dropped rects, 0 dropped frames,
           905 kB, 3 ppm written, 0 keyframe requests
```

and `/tmp/frame-0001.ppm` contains the picture the panel would have shown.

## Testing on hardware

1. Flash `dist/srtos-usb.img`, boot, connect UART-A at 115200 8N1.
2. `drivers` prints the driver/mmu summary; `blk` lists the eMMC and microSD;
   `fb` the panel; `net` the interfaces; `mem` the allocator state.
3. `disp test` paints colour bars - a fast way to check the framebuffer path
   without any network.
4. `log 8` switches to debug logging for the driver you are chasing.
