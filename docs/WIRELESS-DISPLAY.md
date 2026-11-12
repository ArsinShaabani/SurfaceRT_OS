# Using the tablet as a wireless monitor

The tablet runs a **sink**: it listens for a screen stream and paints it on the
panel at native resolution.  The host runs a **sender**: it captures the screen
(or generates a test pattern), finds dirty rectangles and encodes them.

```
  +-------------------+        UDP 47777 control        +-------------------+
  |  sender.py        | --------------------------------> |  SRTOS dispsink  |
  |  capture + encode |        UDP 47778 video           |  decode + blit    |
  |                   | --------------------------------> |                   |
  |                   | <---- PONG / KEYFRAME_REQ ------- |                   |
  +-------------------+                                   +-------------------+
```

## On the tablet

```
srtos> net ip 192.168.1.50 255.255.255.0 192.168.1.1   # or: net dhcp
disp: running, session 0, host -, 0 frames, 0.00 fps ...
srtos> disp start
disp: sink listening on udp 47777 (control) and 47778 (video)
srtos> disp status
disp: running, session 1287361, host 192.168.1.10, 412 frames, 29.87 fps,
      3 rects dropped, 1 keyframes
```

The panel shows the idle screen with the IP address and the ports while waiting,
and a status line with fps and drop counters while streaming.

Useful shell commands:

| Command | Effect |
|---|---|
| `disp start [port]` | start the sink (default 47777) |
| `disp stop` | stop it |
| `disp status` | one line summary |
| `disp test` | paint a colour bar pattern locally |

## On the host

```sh
pip install pillow numpy          # optional but strongly recommended
python3 host/srdisplay/sender.py --host 192.168.1.50
```

Options worth knowing:

| Option | Meaning |
|---|---|
| `--source screen` / `test` | capture the desktop, or use the built-in animated pattern |
| `--fps 30` | frame budget (the sender sleeps to keep it) |
| `--no-rle` | disable run length encoding (useful to isolate a problem) |
| `--stats` | once per second: fps, sent kB, RTT, device counters |
| `--width/--height` | requested geometry; the device answers with its real panel size |

Without Pillow the sender falls back to the test pattern; without numpy it still
works but the dirty-rectangle diff and the RLE encoder are pure Python, so keep
the resolution modest (320x240 is smooth, 1366x768 is not).

## Trying it without a tablet

```sh
make sim
build/sim/srtos-sim --width 1366 --height 768 --frames 60 --out /tmp &
python3 host/srdisplay/sender.py --host 127.0.0.1 --source test --frames 60 --stats
ls /tmp/frame-*.ppm      # what the panel would have shown
```

The simulator links `kernel/services/srdp_core.c` unchanged, so a bug that shows
up there is a real bug in the tablet build.

## Tuning

* **Bandwidth**: `--fps` is the main knob.  Dirty rectangles already do most of
  the work for desktop use.
* **Loss**: the sink asks for a keyframe when it drops a rectangle; the sender
  answers by re-sending the whole frame on the next tick.  Persistent loss means
  the Wi-Fi link is saturated - lower `--fps`.
* **Latency**: the sender never waits for per-packet acknowledgements.  RTT comes
  from `PING`/`PONG` once a second.

## Why not Miracast?

Real Wi-Fi Display is RTSP + MPEG2-TS + H.264, and Tegra 3 does have a hardware
H.264 decoder (`VDE`) - but driving it needs the vendor firmware interface, and
the 88W8797 needs its own firmware before any Wi-Fi frame can be sent at all.
SRDP reaches the same user-visible result (the tablet becomes a second screen)
with zero firmware dependencies and 600 lines of code, and its encoder can later
be swapped for a hardware H.264 path without touching the display service.
