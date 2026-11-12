#!/usr/bin/env python3
"""srdisplay sender - stream this computer's screen to a Surface RT running SRTOS.

    python3 sender.py --host 192.168.1.50           # real screen capture
    python3 sender.py --host 192.168.1.50 --source test
    python3 sender.py --host 127.0.0.1 --source test --frames 120 --stats

Screen capture uses Pillow's ImageGrab when available (X11, Windows, macOS); if
it is missing the built in test pattern is used, which is also what the CI runs
against the host simulator.

No third party dependency is strictly required: numpy only speeds up the dirty
rectangle diff and the RLE encoder.
"""

import argparse
import os
import random
import select
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srdp

try:
    import numpy as np
except ImportError:  # pragma: no cover - optional acceleration
    np = None

try:
    from PIL import Image, ImageDraw, ImageGrab
    HAVE_PIL = True
except ImportError:  # pragma: no cover
    HAVE_PIL = False


class Sender:
    def __init__(self, host, ctrl_port=srdp.CTRL_PORT, video_port=None, fps=30,
                 rle=True, session=None, verbose=False):
        self.host = host
        self.ctrl_port = ctrl_port
        self.video_port = video_port or (ctrl_port + 1)
        self.fps = fps
        self.rle = rle
        self.session = session or random.randint(1, 0x7FFFFFFF)
        self.verbose = verbose
        self.ctrl = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.video = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.video.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
        self.device = None
        self.frames = 0
        self.sent_bytes = 0
        self.dropped = 0
        self.rtt_ms = 0.0
        self.keyframe_requests = 0
        self.force_keyframe = True
        self.cookie = 1

    # ------------------------------------------------------------- session --
    def connect(self, width, height, timeout=5.0):
        deadline = time.time() + timeout
        hello = srdp.hello(width, height, fps=self.fps, session=self.session)
        while time.time() < deadline:
            self.ctrl.sendto(hello, (self.host, self.ctrl_port))
            ready, _, _ = select.select([self.ctrl], [], [], 0.3)
            if ready:
                data, _addr = self.ctrl.recvfrom(2048)
                ack = srdp.parse_hello_ack(data)
                if ack:
                    self.device = ack
                    self.session = ack["session"]
                    self.ctrl.sendto(srdp.config(self.rle, 32, 16, self.fps,
                                                 self.session),
                                     (self.host, self.ctrl_port))
                    if self.verbose:
                        print("device: %dx%d, max datagram %d" %
                              (ack["width"], ack["height"],
                               ack["max_datagram"]))
                    return ack
        return None

    def poll(self):
        """Drain replies: PONG statistics and KEYFRAME_REQ."""
        while True:
            ready, _, _ = select.select([self.ctrl], [], [], 0)
            if not ready:
                break
            data, _ = self.ctrl.recvfrom(2048)
            if len(data) < srdp.HDR.size:
                continue
            magic, ver, ptype, _f, _s, _q, _t, _l = srdp.HDR.unpack_from(data, 0)
            if magic != srdp.MAGIC:
                continue
            if ptype == srdp.T_PONG:
                pong = srdp.parse_pong(data)
                if pong and pong["cookie"] == self.cookie:
                    self.rtt_ms = (time.time() - self.ping_sent) * 1000.0
                    self.device_stats = pong
            elif ptype == srdp.T_KEYFRAME_REQ:
                self.keyframe_requests += 1
                self.force_keyframe = True

    def send_ping(self):
        self.cookie = (self.cookie + 1) & 0xFFFFFFFF
        self.ping_sent = time.time()
        self.ctrl.sendto(srdp.ping(self.cookie, self.session),
                         (self.host, self.ctrl_port))

    # --------------------------------------------------------------- frames --
    def send_rect(self, index, x, y, w, h, payload, encoding):
        fragments = srdp.fragments_for(len(payload))
        if fragments > 4096:
            print("warning: %d fragments for one rectangle" % fragments)
        per = srdp.MAX_DATAGRAM - srdp.HDR.size - srdp.RECT_DATA.size
        self.video.sendto(srdp.rect_begin(index, x, y, w, h, encoding, fragments,
                                          len(payload), self.session),
                          (self.host, self.video_port))
        offset = 0
        for frag in range(fragments):
            chunk = payload[offset:offset + per]
            start = offset
            offset += len(chunk)
            # RAW rectangles address *pixels*, RLE rectangles address bytes
            field = start // 4 if encoding == srdp.ENC_RAW else start
            self.video.sendto(srdp.rect_data(index, frag, field, chunk,
                                             self.session),
                              (self.host, self.video_port))
            self.sent_bytes += len(chunk)
        return fragments

    def split_bands(self, rects, rows=128, max_rects=60):
        """Cut tall rectangles into horizontal bands.

        A 1366x768 raw rectangle is ~3000 datagrams; losing any one of them
        costs the whole rectangle.  Bands of `rows` lines keep the fragment
        count per rectangle small, so loss stays local and the sink can still
        show most of a frame.
        """
        out = []
        for (x, y, w, h, payload) in rects:
            stride = w * 4
            if h <= rows or len(rects) >= max_rects:
                out.append((x, y, w, h, payload))
                continue
            for off in range(0, h, rows):
                bh = min(rows, h - off)
                out.append((x, y + off, w, bh, payload[off * stride:(off + bh) * stride]))
        return out[:max_rects]

    def send_frame(self, rects):
        """rects: list of (x, y, w, h, bytes BGRA)."""
        rects = self.split_bands(rects)
        now = int(time.time() * 1000) & 0xFFFFFFFF
        self.video.sendto(srdp.frame_begin(self.frames, len(rects), self.session,
                                           now), (self.host, self.video_port))
        for i, (x, y, w, h, payload) in enumerate(rects):
            enc = srdp.ENC_RAW
            if self.rle:
                pixels = struct.unpack("<%dI" % (w * h), payload)
                encoded = srdp.rle_encode(pixels, w)
                if encoded is not None:
                    payload = encoded
                    enc = srdp.ENC_RLE
            self.send_rect(i, x, y, w, h, payload, enc)
        self.video.sendto(srdp.frame_end(self.frames, len(rects), 0, self.session),
                          (self.host, self.video_port))
        self.frames += 1

    def send_full_frame(self, frame, width, height):
        """frame: bytes BGRA, width*height*4."""
        self.send_frame([(0, 0, width, height, frame)])

    # ------------------------------------------------------------- sources --
    def capture_screen(self, width, height):
        if not HAVE_PIL:
            return None
        try:
            img = ImageGrab.grab()
        except Exception:
            return None
        img = img.convert("RGB").resize((width, height))
        if np is not None:
            arr = np.asarray(img, dtype=np.uint8)
            bgra = np.empty((height, width, 4), dtype=np.uint8)
            bgra[:, :, 0] = arr[:, :, 2]
            bgra[:, :, 1] = arr[:, :, 1]
            bgra[:, :, 2] = arr[:, :, 0]
            bgra[:, :, 3] = 255
            return bgra.tobytes()
        rgb = img.tobytes()
        out = bytearray(width * height * 4)
        out[0::4] = rgb[2::3]
        out[1::4] = rgb[1::3]
        out[2::4] = rgb[0::3]
        out[3::4] = b"\xff" * (width * height)
        return bytes(out)

    def test_pattern(self, width, height, t):
        """Deterministic animated pattern - no display or library required."""
        if np is not None:
            y, x = np.mgrid[0:height, 0:width]
            frame = np.empty((height, width, 4), dtype=np.uint8)
            frame[:, :, 0] = (x + int(t * 120)) & 0xFF          # B
            frame[:, :, 1] = (y * 2) & 0xFF                    # G
            frame[:, :, 2] = ((x ^ y) + int(t * 60)) & 0xFF     # R
            frame[:, :, 3] = 255
            box = 240
            cx = int((width - box) / 2 + (width - box) / 2 * 0.8 *
                     __import__("math").sin(t * 1.4))
            cy = int((height - box) / 2 + (height - box) / 2 * 0.8 *
                     __import__("math").cos(t * 1.1))
            frame[cy:cy + box, cx:cx + box, :] = 40
            frame[cy + 8:cy + box - 8, cx + 8:cx + box - 8, 2] = 220
            frame[0:4, :, :] = 255
            return frame.tobytes()
        # pure python fallback: slower, so use a coarse pattern
        out = bytearray(width * height * 4)
        shift = int(t * 40)
        for row in range(height):
            base = row * width * 4
            g = (row * 2) & 0xFF
            for col in range(width):
                o = base + col * 4
                out[o] = (col + shift) & 0xFF
                out[o + 1] = g
                out[o + 2] = (col ^ row) & 0xFF
                out[o + 3] = 255
        return bytes(out)


def diff_rects(prev, cur, width, height, threshold=0, max_rects=32):
    """Return dirty rectangles as (x, y, w, h, bytes).

    With numpy the whole frame is compared at once and rectangles are grown row
    by row; without it we fall back to a full frame update (still correct).
    """
    if prev is None or np is None or len(prev) != len(cur):
        return [(0, 0, width, height, cur)]
    a = np.frombuffer(prev, dtype=np.uint32).reshape((height, width))
    b = np.frombuffer(cur, dtype=np.uint32).reshape((height, width))
    changed = a != b
    if not changed.any():
        return []
    rows = np.where(changed.any(axis=1))[0]
    rects = []
    start = rows[0]
    last = rows[0]
    for row in rows[1:]:
        if row != last + 1 or len(rects) >= max_rects - 1:
            cols = np.where(changed[start:last + 1].any(axis=0))[0]
            x0, x1 = int(cols[0]), int(cols[-1]) + 1
            rects.append((x0, int(start), x1 - x0, int(last - start + 1),
                          b[start:last + 1, x0:x1].tobytes()))
            start = row
        last = row
    cols = np.where(changed[start:last + 1].any(axis=0))[0]
    x0, x1 = int(cols[0]), int(cols[-1]) + 1
    rects.append((x0, int(start), x1 - x0, int(last - start + 1),
                  b[start:last + 1, x0:x1].tobytes()))
    return rects


def main(argv=None):
    ap = argparse.ArgumentParser(description="stream a screen to SRTOS")
    ap.add_argument("--host", required=True, help="tablet IP address")
    ap.add_argument("--port", type=int, default=srdp.CTRL_PORT)
    ap.add_argument("--source", choices=["screen", "test"], default="screen")
    ap.add_argument("--width", type=int, default=1366)
    ap.add_argument("--height", type=int, default=768)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--no-rle", action="store_true")
    ap.add_argument("--frames", type=int, default=0, help="0 = run forever")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    sender = Sender(args.host, args.port, fps=args.fps, rle=not args.no_rle,
                    verbose=args.verbose)
    src = args.source
    if src == "screen" and not HAVE_PIL:
        print("Pillow is not installed - falling back to the test pattern")
        src = "test"
    if src == "test" and np is None:
        print("numpy is not installed: reduce --width/--height for a usable fps")

    print("connecting to %s:%d ..." % (args.host, args.port))
    ack = sender.connect(args.width, args.height)
    if not ack:
        print("no answer from the tablet. is `disp start` running?")
        return 1
    width, height = ack["width"], ack["height"]
    print("streaming %dx%d at %d fps (device session %d)" %
          (width, height, args.fps, ack["session"]))

    prev = None
    frame_time = 1.0 / max(1, args.fps)
    next_ping = time.time() + 1.0
    start = time.time()
    frames = 0
    try:
        while args.frames == 0 or frames < args.frames:
            tick = time.time()
            sender.poll()
            if tick >= next_ping:
                sender.send_ping()
                next_ping = tick + 1.0

            if src == "test":
                cur = sender.test_pattern(width, height, tick - start)
            else:
                cur = sender.capture_screen(width, height)
                if cur is None:
                    cur = sender.test_pattern(width, height, tick - start)

            rects = diff_rects(prev if not sender.force_keyframe else None,
                               cur, width, height)
            sender.force_keyframe = False
            if rects:
                sender.send_frame(rects)
                prev = cur
                frames += 1

            if args.stats and frames % max(1, args.fps) == 0 and frames:
                elapsed = time.time() - start
                stats = getattr(sender, "device_stats", None)
                print("frames %d  %.1f fps  sent %.0f kB  rtt %.1f ms  "
                      "keyframes %d  device %s" %
                      (frames, frames / elapsed, sender.sent_bytes / 1024,
                       sender.rtt_ms, sender.keyframe_requests,
                       "%d frames" % stats["frames"] if stats else "?"))
            sleep = frame_time - (time.time() - tick)
            if sleep > 0:
                time.sleep(sleep)
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        sender.ctrl.sendto(srdp.packet(srdp.T_BYE, b"", session=sender.session),
                           (args.host, args.port))
    elapsed = max(0.001, time.time() - start)
    print("sent %d frames in %.1f s (%.1f fps), %.1f MB" %
          (frames, elapsed, frames / elapsed, sender.sent_bytes / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
