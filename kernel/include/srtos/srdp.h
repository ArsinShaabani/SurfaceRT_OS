/*
 * SurfaceRT_OS - SRDP/1, the SurfaceRT Display Protocol
 *
 * A small, loss tolerant screen streaming protocol designed for a 1366x768
 * panel driven by a 1.3 GHz Cortex-A9 without any video decoder:
 *
 *   host                                  Surface RT
 *   ----                                  ----------
 *   HELLO        ->  control port 47777
 *                <-  HELLO_ACK
 *   CONFIG       ->
 *   FRAME_BEGIN  ->  video port 47778
 *   RECT_BEGIN   ->
 *   RECT_DATA    ->  (fragmented, <= max_datagram bytes each)
 *   FRAME_END    ->
 *   PING         ->
 *                <-  PONG (latency + statistics)
 *
 * Each rectangle carries its own encoding and fragment count, so a lost
 * fragment invalidates exactly one rectangle of one frame - never the session.
 * When the sink detects loss it asks the host for a full refresh with
 * KEYFRAME_REQ.
 *
 * All integers are little endian.  See docs/PROTOCOL.md for the byte level
 * description; host/srdisplay/srdp.py implements the other side.
 */
#ifndef SRTOS_SRDP_H
#define SRTOS_SRDP_H

#include <srtos/types.h>

#define SRDP_MAGIC 0x50445253u /* "SRDP" */
#define SRDP_VERSION 1
#define SRDP_CTRL_PORT 47777
#define SRDP_VIDEO_PORT 47778
#define SRDP_MAX_DATAGRAM 1400
#define SRDP_MAX_RECTS 64
#define SRDP_MAX_FRAGMENTS 4096

enum srdp_type {
    SRDP_T_HELLO = 1,
    SRDP_T_HELLO_ACK = 2,
    SRDP_T_CONFIG = 3,
    SRDP_T_FRAME_BEGIN = 4,
    SRDP_T_RECT_BEGIN = 5,
    SRDP_T_RECT_DATA = 6,
    SRDP_T_FRAME_END = 7,
    SRDP_T_PING = 8,
    SRDP_T_PONG = 9,
    SRDP_T_KEYFRAME_REQ = 10,
    SRDP_T_BYE = 11,
};

enum srdp_encoding {
    SRDP_ENC_RAW = 0,
    SRDP_ENC_RLE = 1,
};

enum srdp_format {
    SRDP_FMT_BGRA = 0, /* byte order B,G,R,A - direct match for the panel */
    SRDP_FMT_RGBA = 1,
};

struct __packed srdp_hdr {
    u32 magic;
    u8 version;
    u8 type;
    u16 flags;
    u32 session;
    u32 seq;
    u32 timestamp_ms;
    u32 payload_len;
};

struct __packed srdp_hello {
    u16 width;
    u16 height;
    u8 format;
    u8 target_fps;
    u16 max_datagram;
    u32 caps;
};

struct __packed srdp_hello_ack {
    u16 width;
    u16 height;
    u8 format;
    u8 status;
    u16 max_datagram;
    u32 session;
};

struct __packed srdp_config {
    u8 rle;
    u8 dirty_rects;
    u16 tile;
    u32 target_fps;
};

struct __packed srdp_frame_begin {
    u32 frame_id;
    u16 rect_count;
    u16 reserved;
};

struct __packed srdp_rect_begin {
    u16 rect_index;
    u16 x;
    u16 y;
    u16 w;
    u16 h;
    u8 encoding;
    u8 reserved;
    u16 fragments;
    u32 data_len;
};

struct __packed srdp_rect_data {
    u16 rect_index;
    u16 fragment_index;
    u32 offset;
};

struct __packed srdp_frame_end {
    u32 frame_id;
    u16 rects_complete;
    u16 dropped;
};

struct __packed srdp_ping {
    u32 cookie;
};

struct __packed srdp_pong {
    u32 cookie;
    u32 frames;
    u32 dropped;
    u32 fps_x100;
};

struct __packed srdp_keyframe_req {
    u32 reason;
};

#endif /* SRTOS_SRDP_H */
