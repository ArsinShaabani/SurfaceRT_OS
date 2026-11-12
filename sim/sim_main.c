/*
 * SurfaceRT_OS host simulator for the wireless display sink.
 *
 * Runs the *exact* SRDP receive engine that ships in the kernel (srdp_core.c)
 * on the development machine, listening on the real UDP ports, and dumps what
 * the panel would show into PPM files.  That makes the wireless display feature
 * testable without a Surface RT:
 *
 *   ./build/sim/srtos-sim --frames 30 --out /tmp/frames
 *   python3 host/srdisplay/sender.py --host 127.0.0.1 --source test --frames 30
 *
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <srtos/srdp.h>
#include <srtos/srdp_core.h>

static struct srdp_sink g_sink;
static unsigned g_dirty_blits;
static unsigned g_frames_written;
static unsigned g_keyframes;

static void *sink_alloc(u32 size)
{
    return malloc(size);
}

static void sink_free(void *p)
{
    free(p);
}

static void write_ppm(const char *dir, unsigned index)
{
    char path[512];
    FILE *f;
    unsigned x, y;

    snprintf(path, sizeof(path), "%s/frame-%04u.ppm", dir, index);
    f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", g_sink.width, g_sink.height);
    for (y = 0; y < g_sink.height; y++) {
        for (x = 0; x < g_sink.width; x++) {
            u32 px = g_sink.back[(size_t)y * g_sink.width + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(px >> 16);
            rgb[1] = (unsigned char)(px >> 8);
            rgb[2] = (unsigned char)(px >> 0);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    g_frames_written++;
}

static void send_reply(int sock, struct sockaddr_in *to, const void *data, int len)
{
    if (len > 0) {
        sendto(sock, data, (size_t)len, 0, (struct sockaddr *)to, sizeof(*to));
    }
}

int main(int argc, char **argv)
{
    unsigned width = 1366, height = 768;
    unsigned max_frames = 0, port = SRDP_CTRL_PORT;
    const char *out_dir = "/tmp";
    int ctrl, video;
    struct sockaddr_in addr;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            width = (unsigned)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            height = (unsigned)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            max_frames = (unsigned)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (unsigned)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--width N] [--height N] [--frames N] "
                            "[--port N] [--out DIR]\n", argv[0]);
            return 2;
        }
    }

    srdp_sink_init(&g_sink, width, height, sink_alloc, sink_free);
    g_sink.back = (u32 *)calloc((size_t)width * height, 4);
    if (!g_sink.back) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    ctrl = socket(AF_INET, SOCK_DGRAM, 0);
    video = socket(AF_INET, SOCK_DGRAM, 0);
    {
        /* a full 1366x768 frame arrives as ~3000 datagrams; give the kernel
         * room to hold them while we decode */
        int rcvbuf = 8 * 1024 * 1024;
        setsockopt(ctrl, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        setsockopt(video, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(ctrl, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind ctrl %u: %s\n", port, strerror(errno));
        return 1;
    }
    addr.sin_port = htons((unsigned short)(port + 1));
    if (bind(video, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind video %u: %s\n", port + 1, strerror(errno));
        return 1;
    }
    printf("srtos-sim: sink ready on udp %u/%u, panel %ux%u\n", port, port + 1,
           width, height);
    fflush(stdout);

    for (;;) {
        struct pollfd fds[2];
        int rc;

        fds[0].fd = ctrl;
        fds[0].events = POLLIN;
        fds[1].fd = video;
        fds[1].events = POLLIN;
        rc = poll(fds, 2, 5000);
        if (rc == 0) {
            printf("srtos-sim: idle timeout, %u frames received\n", g_sink.frames);
            break;
        }
        for (i = 0; i < 2; i++) {
            unsigned char buf[2048];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            struct srdp_event ev;
            ssize_t n;

            if (!(fds[i].revents & POLLIN)) {
                continue;
            }
            n = recvfrom(fds[i].fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
            if (n <= 0) {
                continue;
            }
            if (srdp_sink_feed(&g_sink, buf, (u32)n, &ev) != 0) {
                continue;
            }
            switch (ev.kind) {
            case SRDP_EV_HELLO: {
                unsigned char out[128];
                int len = srdp_build_hello_ack(&g_sink, out, sizeof(out),
                                              SRDP_FMT_BGRA, SRDP_MAX_DATAGRAM);
                g_sink.host_ip = from.sin_addr.s_addr;
                g_sink.host_ctrl_port = ntohs(from.sin_port);
                send_reply(ctrl, &from, out, len);
                printf("srtos-sim: session %d from %s:%u\n", g_sink.session,
                       inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                fflush(stdout);
                break;
            }
            case SRDP_EV_DIRTY:
                g_dirty_blits += g_sink.dirty_count;
                g_sink.dirty_count = 0;
                break;
            case SRDP_EV_FRAME:
                if (g_sink.frames <= max_frames + 1 &&
                    (g_sink.frames % 10 == 1 || max_frames <= 3)) {
                    write_ppm(out_dir, g_sink.frames);
                }
                printf("srtos-sim: frame %u, %u rects, %u dropped, %u kB\n",
                       g_sink.frames, ev.reason, g_sink.dropped_rects,
                       g_sink.bytes_in / 1024);
                fflush(stdout);
                break;
            case SRDP_EV_PING: {
                unsigned char out[128];
                int len = srdp_build_pong(&g_sink, ev.cookie, out, sizeof(out));
                send_reply(ctrl, &from, out, len);
                break;
            }
            case SRDP_EV_KEYFRAME_REQ:
                g_keyframes++;
                break;
            case SRDP_EV_BYE:
                printf("srtos-sim: session closed by host\n");
                fflush(stdout);
                break;
            default:
                break;
            }
        }
        if (max_frames && g_sink.frames >= max_frames) {
            break;
        }
    }

    printf("srtos-sim: %u frames, %u rect blits, %u dropped rects, "
           "%u dropped frames, %u kB, %u ppm written, %u keyframe requests\n",
           g_sink.frames, g_dirty_blits, g_sink.dropped_rects,
           g_sink.dropped_frames, g_sink.bytes_in / 1024, g_frames_written,
           g_keyframes);
    return g_sink.frames > 0 ? 0 : 1;
}
