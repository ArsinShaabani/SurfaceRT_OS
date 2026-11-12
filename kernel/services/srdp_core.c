/*
 * SRDP/1 receive engine - see srdp_core.h
 *
 * The parser is written defensively: every field that comes from the network is
 * checked against the session geometry before it is used, so a hostile or
 * broken sender can only ever cost us a dropped rectangle.
 */
#include <srtos/srdp_core.h>
#include <srtos/string.h>

static int rect_ok(struct srdp_sink *s, u32 x, u32 y, u32 w, u32 h)
{
    return w && h && (u64)x + w <= s->width && (u64)y + h <= s->height;
}

int srdp_rle_decode(const u8 *src, u32 len, u32 *dst, u32 dst_pixels)
{
    u32 written = 0, pos = 0;

    while (pos + 6 <= len && written < dst_pixels) {
        u16 count = (u16)(src[pos] | (src[pos + 1] << 8));
        u32 pixel;
        pos += 2;
        pixel = (u32)src[pos] | ((u32)src[pos + 1] << 8) |
                ((u32)src[pos + 2] << 16) | ((u32)src[pos + 3] << 24);
        pos += 4;
        if (!count) {
            continue;
        }
        if (written + count > dst_pixels) {
            count = (u16)(dst_pixels - written);
        }
        while (count--) {
            dst[written++] = pixel;
        }
    }
    return written == dst_pixels ? 0 : -1;
}

int srdp_sink_init(struct srdp_sink *s, u32 width, u32 height,
                   void *(*alloc)(u32), void (*freep)(void *))
{
    if (!s) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->width = width;
    s->height = height;
    s->alloc = alloc;
    s->free = freep;
    return 0;
}

void srdp_sink_reset(struct srdp_sink *s)
{
    if (!s) {
        return;
    }
    s->session = 0;
    s->dirty_count = 0;
    s->rect.active = 0;
    if (s->rect.rle && s->free) {
        s->free(s->rect.rle);
    }
    s->rect.rle = 0;
    s->rect.rle_size = 0;
}

static void mark_dirty(struct srdp_sink *s, u32 x, u32 y, u32 w, u32 h)
{
    if (s->dirty_count < SRDP_DIRTY_MAX) {
        s->dirty[s->dirty_count].x = (u16)x;
        s->dirty[s->dirty_count].y = (u16)y;
        s->dirty[s->dirty_count].w = (u16)w;
        s->dirty[s->dirty_count].h = (u16)h;
        s->dirty_count++;
    }
}

static int rect_begin(struct srdp_sink *s, const struct srdp_rect_begin *rb)
{
    if (!rect_ok(s, rb->x, rb->y, rb->w, rb->h) || rb->fragments == 0) {
        return -1;
    }
    if (s->rect.active) {
        s->dropped_rects++;
    }
    {
        /* keep the RLE staging buffer across rectangles, but forget the state */
        u8 *keep_rle = s->rect.rle;
        u32 keep_size = s->rect.rle_size;
        memset(&s->rect, 0, sizeof(s->rect));
        s->rect.rle = keep_rle;
        s->rect.rle_size = keep_size;
    }
    s->rect.active = 1;
    s->rect.index = rb->rect_index;
    s->rect.x = rb->x;
    s->rect.y = rb->y;
    s->rect.w = rb->w;
    s->rect.h = rb->h;
    s->rect.encoding = rb->encoding;
    s->rect.fragments = rb->fragments;
    s->rect.data_len = rb->data_len;

    if (rb->encoding == SRDP_ENC_RLE) {
        if (!rb->data_len || rb->data_len > SRDP_RLE_MAX || !s->alloc) {
            s->rect.active = 0;
            return -1;
        }
        if (!s->rect.rle || s->rect.rle_size < rb->data_len) {
            if (s->rect.rle && s->free) {
                s->free(s->rect.rle);
            }
            s->rect.rle = (u8 *)s->alloc(rb->data_len);
            s->rect.rle_size = s->rect.rle ? rb->data_len : 0;
        }
        if (!s->rect.rle) {
            s->rect.active = 0;
            return -1;
        }
    } else if (rb->encoding == SRDP_ENC_RAW) {
        if (rb->data_len != (u32)rb->w * rb->h * 4u) {
            s->rect.active = 0;
            return -1;
        }
    } else {
        s->rect.active = 0;
        return -1;
    }
    return 0;
}

static void rect_raw_data(struct srdp_sink *s, const struct srdp_rect_data *rd,
                          const u8 *data, u32 data_len)
{
    u32 rect_pixels = (u32)s->rect.w * s->rect.h;
    u32 pix_off = rd->offset;
    u32 pixels = data_len / 4u;
    u32 done = 0;

    if ((data_len & 3u) || (u64)pix_off + pixels > rect_pixels) {
        s->rect.active = 0;
        s->dropped_rects++;
        return;
    }
    while (done < pixels) {
        u32 abs = pix_off + done;
        u32 row = abs / s->rect.w;
        u32 col = abs % s->rect.w;
        u32 chunk = pixels - done;
        u32 *dst;
        if (chunk > s->rect.w - col) {
            chunk = s->rect.w - col;
        }
        dst = s->back + (uptr)(s->rect.y + row) * s->width + s->rect.x + col;
        memcpy(dst, data + (uptr)done * 4u, chunk * 4u);
        done += chunk;
    }
    s->rect.received += data_len;
}

static void rect_complete(struct srdp_sink *s)
{
    if (s->rect.encoding == SRDP_ENC_RLE) {
        u32 pixels = (u32)s->rect.w * s->rect.h;
        u32 *scratch;
        u32 row;

        if (!s->alloc) {
            s->rect.active = 0;
            return;
        }
        scratch = (u32 *)s->alloc(pixels * 4u);
        if (!scratch) {
            s->rect.active = 0;
            s->dropped_rects++;
            return;
        }
        if (srdp_rle_decode(s->rect.rle, s->rect.received, scratch, pixels) != 0) {
            if (s->free) {
                s->free(scratch);
            }
            s->rect.active = 0;
            s->dropped_rects++;
            return;
        }
        for (row = 0; row < s->rect.h; row++) {
            memcpy(s->back + (uptr)(s->rect.y + row) * s->width + s->rect.x,
                   scratch + (uptr)row * s->rect.w, (u32)s->rect.w * 4u);
        }
        if (s->free) {
            s->free(scratch);
        }
    }
    mark_dirty(s, s->rect.x, s->rect.y, s->rect.w, s->rect.h);
    s->rect.active = 0;
}

int srdp_sink_feed(struct srdp_sink *s, const u8 *packet, u32 len,
                   struct srdp_event *ev)
{
    const struct srdp_hdr *h;
    const u8 *payload;
    u32 plen;

    if (ev) {
        memset(ev, 0, sizeof(*ev));
        ev->kind = SRDP_EV_NONE;
    }
    if (!s || !packet || len < sizeof(*h)) {
        return -1;
    }
    h = (const struct srdp_hdr *)packet;
    if (h->magic != SRDP_MAGIC || h->version != SRDP_VERSION) {
        return -1;
    }
    if (h->payload_len > len - sizeof(*h)) {
        return -1;
    }
    payload = packet + sizeof(*h);
    plen = h->payload_len;
    s->bytes_in += plen;

    switch (h->type) {
    case SRDP_T_HELLO: {
        struct srdp_hello hello;
        if (plen < sizeof(hello) || !ev) {
            return -1;
        }
        memcpy(&hello, payload, sizeof(hello));
        s->session = (int)(h->seq ? h->seq : 1);
        s->host_ctrl_port = 0;
        s->frames = 0;
        s->dropped_rects = 0;
        s->dropped_frames = 0;
        s->rect.active = 0;
        s->dirty_count = 0;
        ev->kind = SRDP_EV_HELLO;
        /* the transport layer fills in the source address */
        break;
    }
    case SRDP_T_FRAME_BEGIN: {
        struct srdp_frame_begin fb;
        if (plen < sizeof(fb)) {
            return -1;
        }
        memcpy(&fb, payload, sizeof(fb));
        s->last_frame_id = fb.frame_id;
        s->dirty_count = 0;
        break;
    }
    case SRDP_T_RECT_BEGIN: {
        struct srdp_rect_begin rb;
        if (plen < sizeof(rb)) {
            return -1;
        }
        memcpy(&rb, payload, sizeof(rb));
        if (rect_begin(s, &rb) != 0) {
            s->dropped_rects++;
            if (ev) {
                ev->kind = SRDP_EV_KEYFRAME_REQ;
                ev->reason = 40;
            }
        }
        break;
    }
    case SRDP_T_RECT_DATA: {
        struct srdp_rect_data rd;
        if (plen < sizeof(rd) || !s->rect.active) {
            return -1;
        }
        memcpy(&rd, payload, sizeof(rd));
        if (rd.rect_index != s->rect.index) {
            return -1;
        }
        s->rect.frags_seen++;
        if (s->rect.encoding == SRDP_ENC_RAW) {
            rect_raw_data(s, &rd, payload + sizeof(rd), plen - sizeof(rd));
        } else {
            u32 n = plen - sizeof(rd);
            if ((u64)rd.offset + n > s->rect.data_len) {
                s->rect.active = 0;
                s->dropped_rects++;
                return -1;
            }
            memcpy(s->rect.rle + rd.offset, payload + sizeof(rd), n);
            s->rect.received += n;
        }
        if (s->rect.active && s->rect.frags_seen >= s->rect.fragments) {
            rect_complete(s);
            if (ev) {
                ev->kind = SRDP_EV_DIRTY;
            }
        }
        break;
    }
    case SRDP_T_FRAME_END: {
        struct srdp_frame_end fe;
        u32 dirty = s->dirty_count;
        if (plen >= sizeof(fe)) {
            memcpy(&fe, payload, sizeof(fe));
            if (fe.dropped && (u32)fe.rects_complete + fe.dropped != 0 &&
                dirty < (u32)fe.rects_complete) {
                s->dropped_frames++;
            }
        }
        if (s->rect.active) {
            s->rect.active = 0;
            s->dropped_rects++;
        }
        s->frames++;
        if (ev) {
            ev->kind = SRDP_EV_FRAME;
            ev->reason = dirty;
        }
        break;
    }
    case SRDP_T_PING: {
        struct srdp_ping ping;
        if (plen < sizeof(ping) || !ev) {
            return -1;
        }
        memcpy(&ping, payload, sizeof(ping));
        ev->kind = SRDP_EV_PING;
        ev->cookie = ping.cookie;
        break;
    }
    case SRDP_T_BYE:
        srdp_sink_reset(s);
        if (ev) {
            ev->kind = SRDP_EV_BYE;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int put_header(u8 *out, u32 out_len, u8 type, u32 payload_len, u32 session)
{
    struct srdp_hdr *h;
    if (!out || out_len < sizeof(struct srdp_hdr) + payload_len) {
        return -1;
    }
    memset(out, 0, sizeof(struct srdp_hdr));
    h = (struct srdp_hdr *)out;
    h->magic = SRDP_MAGIC;
    h->version = SRDP_VERSION;
    h->type = type;
    h->session = session;
    h->payload_len = payload_len;
    return (int)(sizeof(*h) + payload_len);
}

int srdp_build_hello_ack(const struct srdp_sink *s, u8 *out, u32 out_len,
                         u8 format, u16 max_datagram)
{
    struct srdp_hello_ack ack;
    int n = put_header(out, out_len, SRDP_T_HELLO_ACK, sizeof(ack), s->session);
    if (n < 0) {
        return -1;
    }
    memset(&ack, 0, sizeof(ack));
    ack.width = (u16)s->width;
    ack.height = (u16)s->height;
    ack.format = format;
    ack.max_datagram = max_datagram;
    ack.session = (u32)s->session;
    memcpy(out + sizeof(struct srdp_hdr), &ack, sizeof(ack));
    return n;
}

int srdp_build_pong(const struct srdp_sink *s, u32 cookie, u8 *out, u32 out_len)
{
    struct srdp_pong pong;
    int n = put_header(out, out_len, SRDP_T_PONG, sizeof(pong), s->session);
    if (n < 0) {
        return -1;
    }
    memset(&pong, 0, sizeof(pong));
    pong.cookie = cookie;
    pong.frames = s->frames;
    pong.dropped = s->dropped_rects;
    memcpy(out + sizeof(struct srdp_hdr), &pong, sizeof(pong));
    return n;
}

int srdp_build_keyframe_req(u32 session, u32 reason, u8 *out, u32 out_len)
{
    struct srdp_keyframe_req req;
    int n = put_header(out, out_len, SRDP_T_KEYFRAME_REQ, sizeof(req), session);
    if (n < 0) {
        return -1;
    }
    req.reason = reason;
    memcpy(out + sizeof(struct srdp_hdr), &req, sizeof(req));
    return n;
}
