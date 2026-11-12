# SRDP/1 - SurfaceRT Display Protocol

A loss tolerant screen streaming protocol for a 1.3 GHz ARMv7 tablet with no
hardware video decoder.  All integers are **little endian**.

Reference implementations:

* device: `kernel/services/srdp_core.c` (portable, also used by the simulator)
* host: `host/srdisplay/srdp.py`

## Transport

| Port | Purpose |
|---|---|
| UDP 47777 | control: `HELLO`, `HELLO_ACK`, `CONFIG`, `PING`/`PONG`, `KEYFRAME_REQ`, `BYE` |
| UDP 47778 | video: `FRAME_BEGIN`, `RECT_BEGIN`, `RECT_DATA`, `FRAME_END` |

A sender may use `--port N`, in which case video is `N+1`.

## Header (24 bytes, every packet)

```
offset size field
0      4    magic       0x50445253 ("SRDP")
4      1    version     1
5      1    type        see below
6      2    flags       reserved, 0
8      4    session     session id (device assigns, host echoes)
12     4    seq         frame id / sequence
16     4    timestamp_ms sender clock, informational
20     4    payload_len bytes after the header
```

## Types

| # | Name | Payload | Direction |
|---|---|---|---|
| 1 | `HELLO` | `u16 w, u16 h, u8 format, u8 fps, u16 max_datagram, u32 caps` (12 B) | host -> device |
| 2 | `HELLO_ACK` | `u16 w, u16 h, u8 format, u8 status, u16 max_datagram, u32 session` (12 B) | device -> host |
| 3 | `CONFIG` | `u8 rle, u8 dirty_rects, u16 tile, u32 target_fps` (8 B) | host -> device |
| 4 | `FRAME_BEGIN` | `u32 frame_id, u16 rect_count, u16 reserved` (8 B) | host -> device |
| 5 | `RECT_BEGIN` | `u16 index, x, y, w, h; u8 encoding; u8 reserved; u16 fragments; u32 data_len` (18 B) | host -> device |
| 6 | `RECT_DATA` | `u16 index, u16 fragment, u32 offset` + data | host -> device |
| 7 | `FRAME_END` | `u32 frame_id, u16 rects_complete, u16 dropped` (8 B) | host -> device |
| 8 | `PING` | `u32 cookie` | host -> device |
| 9 | `PONG` | `u32 cookie, u32 frames, u32 dropped, u32 fps_x100` | device -> host |
| 10 | `KEYFRAME_REQ` | `u32 reason` | device -> host |
| 11 | `BYE` | - | both |

## Session

The host sends `HELLO` with a candidate session id in `seq`.  The device adopts it
(so a restarting sender is detected), answers `HELLO_ACK` with the same id and the
panel geometry, and from then on ignores packets with a different session.  The
host then sends one `CONFIG`.

## Frames

```
FRAME_BEGIN  frame_id, rect_count
  RECT_BEGIN index, x, y, w, h, encoding, fragments, data_len
    RECT_DATA  index, fragment, offset, chunk      (fragments of <= 1376 bytes)
  RECT_BEGIN ...
FRAME_END    frame_id, rects_complete, dropped
```

`x/y/w/h` must lie fully inside the panel; anything else is dropped by the device
and answered with `KEYFRAME_REQ`.

### Encodings

* **RAW (0)** - `data_len` must equal `w*h*4`.  `offset` in `RECT_DATA` is a
  **pixel** offset inside the rectangle (row major).  A fragment of `n` bytes
  covers `n/4` pixels and is allowed to cross row boundaries; the device copies
  row by row.
* **RLE (1)** - `offset` in `RECT_DATA` is a **byte** offset into the encoded
  stream, which the device buffers (max 512 KiB) and decodes when the last
  fragment arrived.  A run is `u16 count` + `u32 pixel`; the decoded pixel count
  must equal `w*h` exactly, otherwise the rectangle is dropped.

Pixel byte order follows `format`: `0 = BGRA` (matches the panel, fastest),
`1 = RGBA` (device swaps channels).

## Loss handling

* UDP: a lost `RECT_DATA` leaves that rectangle incomplete; the device drops it at
  the next `RECT_BEGIN`/`FRAME_END` and increments its dropped counter.
* When the device drops a rectangle it sends `KEYFRAME_REQ`; a sender should
  answer with a full-frame (or full-dirty) update.
* `FRAME_END` resets the device's per-frame dirty list.
* Nothing in the protocol is acknowledged per packet - that is deliberate: for a
  screen stream it is cheaper to re-send a region than to wait for an ACK.

## Latency measurement

`PING`/`PONG` carries a cookie echoed by the device plus its frame/drop counters.
The reference sender prints RTT and the device's own counters once per second,
which makes it easy to see whether loss is on the network or the decode side.

## Bandwidth notes

A full 1366x768 raw frame is 4.2 MB, i.e. ~3000 datagrams.  The reference sender
therefore:

* splits tall rectangles into 128 line bands (loss stays local);
* prefers RLE when it actually shrinks a rectangle;
* honours `KEYFRAME_REQ` instead of blindly resending everything.

For reference: a 20 fps session that only changes the top 200 lines of the screen
stays around 30-60 Mbit/s, which is comfortable for 802.11n.
