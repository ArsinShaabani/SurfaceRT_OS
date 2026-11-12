/*
 * SRDP/1 receive engine - portable, kernel independent.
 *
 * This is the part of the wireless display sink that actually parses packets:
 * it validates every length and offset, reassembles rectangles in a back buffer
 * and reports which rectangles became dirty.  Keeping it free of kernel
 * dependencies means the exact same code runs on the tablet, in the host
 * simulator (sim/) and in the unit tests.
 */
#ifndef SRTOS_SRDP_CORE_H
#define SRTOS_SRDP_CORE_H

#include <srtos/types.h>
#include <srtos/srdp.h>

#define SRDP_DIRTY_MAX 64
#define SRDP_RLE_MAX (512 * 1024)

struct srdp_dirty {
    u16 x, y, w, h;
};

struct srdp_rect {
    int active;
    u16 index;
    u16 x, y, w, h;
    u8 encoding;
    u16 fragments;
    u8 frags_seen;
    u32 data_len;
    u32 received;
    u8 *rle;
    u32 rle_size;
};

struct srdp_sink {
    u32 *back;            /* w * h pixels, host endianness ARGB          */
    u32 width, height;
    void *(*alloc)(u32 size);
    void (*free)(void *p);

    /* session state */
    int session;
    u32 host_ip;
    u16 host_ctrl_port;

    /* statistics */
    u32 frames;
    u32 dropped_rects;
    u32 dropped_frames;
    u32 bytes_in;
    u32 last_frame_id;
    u32 keyframe_requests;

    /* pending work for the blitter */
    struct srdp_dirty dirty[SRDP_DIRTY_MAX];
    u32 dirty_count;
    struct srdp_rect rect;
};

/* every event that needs a reply or a blit */
enum srdp_ev_kind {
    SRDP_EV_NONE = 0,
    SRDP_EV_HELLO,        /* start a session, answer with HELLO_ACK */
    SRDP_EV_DIRTY,        /* rectangle(s) ready in back[]            */
    SRDP_EV_FRAME,        /* a frame completed                       */
    SRDP_EV_PING,         /* answer with PONG                        */
    SRDP_EV_BYE,          /* session ended                           */
    SRDP_EV_KEYFRAME_REQ, /* ask the host for a full refresh         */
};

struct srdp_event {
    u8 kind;
    u32 cookie;
    u32 src_ip;
    u16 src_port;
    u32 reason;
};

int srdp_sink_init(struct srdp_sink *s, u32 width, u32 height,
                   void *(*alloc)(u32), void (*freep)(void *));
void srdp_sink_reset(struct srdp_sink *s);
int srdp_sink_feed(struct srdp_sink *s, const u8 *packet, u32 len,
                   struct srdp_event *ev);
int srdp_build_hello_ack(const struct srdp_sink *s, u8 *out, u32 out_len,
                         u8 format, u16 max_datagram);
int srdp_build_pong(const struct srdp_sink *s, u32 cookie, u8 *out, u32 out_len);
int srdp_build_keyframe_req(u32 session, u32 reason, u8 *out, u32 out_len);

/* helpers shared with the tests */
int srdp_rle_decode(const u8 *src, u32 len, u32 *dst, u32 dst_pixels);

#endif /* SRTOS_SRDP_CORE_H */
