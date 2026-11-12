"""SRDP/1 - the SurfaceRT Display Protocol, host side.

This module is the reference encoder.  It matches kernel/include/srtos/srdp.h
byte for byte; docs/PROTOCOL.md is the specification.

Design goals, in order:
  1. never stall the tablet: bounded packets, dirty rectangles, no retransmit
     storms - a lost fragment only costs one rectangle;
  2. predictable bandwidth: raw rectangles for small changes, RLE for the
     long flat runs you get from text and UI backgrounds;
  3. simple enough to be auditable: little endian, fixed headers.
"""

import struct

MAGIC = 0x50445253  # "SRDP"
VERSION = 1
CTRL_PORT = 47777
VIDEO_PORT = 47778
MAX_DATAGRAM = 1400

T_HELLO = 1
T_HELLO_ACK = 2
T_CONFIG = 3
T_FRAME_BEGIN = 4
T_RECT_BEGIN = 5
T_RECT_DATA = 6
T_FRAME_END = 7
T_PING = 8
T_PONG = 9
T_KEYFRAME_REQ = 10
T_BYE = 11

ENC_RAW = 0
ENC_RLE = 1
FMT_BGRA = 0
FMT_RGBA = 1

HDR = struct.Struct("<IBBHIIII")          # 24 bytes
HELLO = struct.Struct("<HHBBHI")          # 12 bytes
HELLO_ACK = struct.Struct("<HHBBHI")
CONFIG = struct.Struct("<BBHI")
FRAME_BEGIN = struct.Struct("<IHH")
RECT_BEGIN = struct.Struct("<HHHHHBBHI")
RECT_DATA = struct.Struct("<HHI")
FRAME_END = struct.Struct("<IHH")
PING = struct.Struct("<I")
PONG = struct.Struct("<IIII")
KEYFRAME_REQ = struct.Struct("<I")


def packet(ptype, payload=b"", session=0, seq=0, timestamp=0):
    return HDR.pack(MAGIC, VERSION, ptype, 0, session, seq, timestamp,
                    len(payload)) + payload


def hello(width, height, fmt=FMT_BGRA, fps=30, max_datagram=MAX_DATAGRAM,
          caps=0, session=0):
    return packet(T_HELLO, HELLO.pack(width, height, fmt, fps, max_datagram, caps),
                  session=session, seq=session)


def config(rle=True, dirty_rects=32, tile=16, fps=30, session=0):
    return packet(T_CONFIG, CONFIG.pack(1 if rle else 0, dirty_rects, tile, fps),
                  session=session)


def frame_begin(frame_id, rect_count, session, timestamp=0):
    return packet(T_FRAME_BEGIN, FRAME_BEGIN.pack(frame_id, rect_count, 0),
                  session=session, seq=frame_id, timestamp=timestamp)


def rect_begin(index, x, y, w, h, encoding, fragments, data_len, session):
    return packet(T_RECT_BEGIN,
                  RECT_BEGIN.pack(index, x, y, w, h, encoding, 0, fragments,
                                  data_len),
                  session=session)


def rect_data(index, fragment, offset, chunk, session):
    return packet(T_RECT_DATA, RECT_DATA.pack(index, fragment, offset) + chunk,
                  session=session)


def frame_end(frame_id, rects_complete, dropped, session):
    return packet(T_FRAME_END, FRAME_END.pack(frame_id, rects_complete, dropped),
                  session=session)


def ping(cookie, session):
    return packet(T_PING, PING.pack(cookie), session=session)


def parse_pong(data):
    if len(data) < HDR.size + PONG.size:
        return None
    magic, ver, ptype, _flags, session, _seq, _ts, plen = HDR.unpack_from(data, 0)
    if magic != MAGIC or ptype != T_PONG:
        return None
    cookie, frames, dropped, fps_x100 = PONG.unpack_from(data, HDR.size)
    return dict(cookie=cookie, frames=frames, dropped=dropped, fps_x100=fps_x100)


def parse_hello_ack(data):
    if len(data) < HDR.size + HELLO_ACK.size:
        return None
    magic, ver, ptype, _flags, _session, _seq, _ts, plen = HDR.unpack_from(data, 0)
    if magic != MAGIC or ptype != T_HELLO_ACK:
        return None
    w, h, fmt, status, max_dgram, session = HELLO_ACK.unpack_from(data, HDR.size)
    return dict(width=w, height=h, format=fmt, status=status,
                max_datagram=max_dgram, session=session)


def rle_encode(pixels, width, max_bytes=None):
    """Run length encode a rectangle of 0xAARRGGBB pixels.

    Returns None when RLE would not be smaller than the raw pixels.
    """
    out = bytearray()
    n = len(pixels)
    i = 0
    while i < n:
        run = 1
        value = pixels[i]
        while i + run < n and pixels[i + run] == value and run < 0xFFFF:
            run += 1
        out += struct.pack("<HI", run, value & 0xFFFFFFFF)
        i += run
        if max_bytes is not None and len(out) > max_bytes:
            return None
    return bytes(out) if len(out) < n * 4 else None


def fragments_for(data_len, max_datagram=MAX_DATAGRAM, header=RECT_DATA.size,
                  hdr=HDR.size):
    per = max_datagram - hdr - header
    if per <= 0:
        raise ValueError("datagram limit too small")
    return max(1, (data_len + per - 1) // per)
