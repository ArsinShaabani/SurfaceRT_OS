/*
 * SurfaceRT_OS host unit tests.
 *
 * Everything here is portable code that also ships in the kernel: the heap, the
 * printf engine, the string routines, the SRDP receive engine and the ELF
 * relocation loader.  `make test` builds and runs this plus the Python tests.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <srtos/mm.h>
#include <srtos/string.h>
#include <srtos/srdp.h>
#include <srtos/srdp_core.h>
#include <srtos/sd_bits.h>
#include "../boot/elfload.h"

static int g_failures;
static int g_checks;

#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

#define CHECK_EQ(a, b) do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        g_checks++; \
        if (_a != _b) { \
            printf("  FAIL %s:%d  %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, \
                   #a, _a, #b, _b); \
            g_failures++; \
        } \
    } while (0)

/* --------------------------------------------------------------- strings --- */
static void test_strings(void)
{
    char buf[64];
    printf("strings\n");
    CHECK_EQ(strlen("hello"), 5);
    CHECK_EQ(memcmp("abc", "abd", 2), 0);
    CHECK(memcmp("abc", "abd", 3) < 0);
    CHECK_EQ(strlcpy(buf, "surface", sizeof(buf)), 7);
    CHECK_EQ(strcmp(buf, "surface"), 0);
    CHECK_EQ(strcasecmp("Tegra", "tEGRA"), 0);
    CHECK(strstr("SurfaceRT_OS", "RT_OS") != NULL);
    CHECK_EQ(strtoul("0x82000000", NULL, 0), 0x82000000ul);
    memmove(buf + 1, buf, 8);
    CHECK_EQ(strcmp(buf + 1, "surface"), 0);
}

static void test_printf(void)
{
    char buf[128];
    printf("printf\n");
    snprintf(buf, sizeof(buf), "%d %u %x %s", -42, 42u, 0xBEEFu, "ok");
    CHECK_EQ(strcmp(buf, "-42 42 beef ok"), 0);
    snprintf(buf, sizeof(buf), "%08x", 0x1234);
    CHECK_EQ(strcmp(buf, "00001234"), 0);
    snprintf(buf, sizeof(buf), "%-6s|%6s|", "ab", "cd");
    CHECK_EQ(strcmp(buf, "ab    |    cd|"), 0);
    snprintf(buf, sizeof(buf), "%llu", 1234567890123ull);
    CHECK_EQ(strcmp(buf, "1234567890123"), 0);
    snprintf(buf, sizeof(buf), "%p", (void *)0x82000000ul);
    CHECK_EQ(strcmp(buf, "0x82000000"), 0);
    snprintf(buf, sizeof(buf), "%5.2s", "abcdef");
    CHECK_EQ(strcmp(buf, "   ab"), 0);
}

/* ------------------------------------------------------------------ heap --- */
static void test_heap(void)
{
    static unsigned char arena[64 * 1024];
    void *a, *b, *c;

    printf("heap\n");
    heap_init(arena, sizeof(arena));
    a = kmalloc(1000);
    b = kmalloc(2000);
    c = kmalloc(100);
    CHECK(a && b && c);
    memset(a, 0xAA, 1000);
    memset(b, 0xBB, 2000);
    kfree(b);
    b = kmalloc(1800);
    CHECK(b != NULL);
    CHECK_EQ(((unsigned char *)a)[999], 0xAA);
    kfree(a);
    a = kmalloc(40000);
    CHECK(a != NULL);          /* coalescing must have produced a big block */
    kfree(a);
    kfree(b);
    kfree(c);
    {
        size_t used = 12345, peak = 0, total = 0;
        heap_stats(&used, &peak, &total);
        CHECK_EQ(used, 0);
        CHECK(peak > 3000);
    }
    /* double free must not corrupt the allocator */
    a = kmalloc(64);
    kfree(a);
    kfree(a);
    b = kmalloc(64);
    CHECK(b != NULL);
    kfree(b);
}

/* ------------------------------------------------------------------ srdp --- */
static void *sink_alloc(u32 size) { return malloc(size); }
static void sink_free(void *p) { free(p); }

struct srdp_hdr *pkt_hdr(unsigned char *buf, unsigned char type, unsigned len)
{
    struct srdp_hdr *h = (struct srdp_hdr *)buf;
    memset(h, 0, sizeof(*h));
    h->magic = SRDP_MAGIC;
    h->version = SRDP_VERSION;
    h->type = type;
    h->session = 7;
    h->payload_len = len;
    return h;
}

static void test_srdp_raw(void)
{
    enum { W = 64, H = 32 };
    static unsigned int back[W * H];
    struct srdp_sink sink;
    struct srdp_event ev;
    unsigned char pkt[2048];
    unsigned lanes = 4, frags, i;
    unsigned bytes = W * H * 4;
    unsigned per = SRDP_MAX_DATAGRAM - sizeof(struct srdp_hdr) -
                   sizeof(struct srdp_rect_data);

    printf("srdp raw\n");
    srdp_sink_init(&sink, W, H, sink_alloc, sink_free);
    sink.back = back;
    frags = (bytes + per - 1) / per;

    pkt_hdr(pkt, SRDP_T_FRAME_BEGIN, sizeof(struct srdp_frame_begin));
    {
        struct srdp_frame_begin fb = { 1, 1, 0 };
        memcpy(pkt + sizeof(struct srdp_hdr), &fb, sizeof(fb));
    }
    CHECK_EQ(srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_frame_begin), &ev), 0);

    pkt_hdr(pkt, SRDP_T_RECT_BEGIN, sizeof(struct srdp_rect_begin));
    {
        struct srdp_rect_begin rb = { 0, 0, 0, W, H, SRDP_ENC_RAW, 0, (u16)frags,
                                      bytes };
        memcpy(pkt + sizeof(struct srdp_hdr), &rb, sizeof(rb));
    }
    CHECK_EQ(srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_begin), &ev), 0);

    for (i = 0; i < frags; i++) {
        unsigned off = i * per;
        unsigned n = (off + per > bytes) ? bytes - off : per;
        struct srdp_rect_data rd = { 0, (u16)i, off / 4 };
        pkt_hdr(pkt, SRDP_T_RECT_DATA, sizeof(rd) + n);
        memcpy(pkt + sizeof(struct srdp_hdr), &rd, sizeof(rd));
        memset(pkt + sizeof(struct srdp_hdr) + sizeof(rd), (int)(i & 0xFF), n);
        srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(rd) + n, &ev);
    }
    CHECK(ev.kind == SRDP_EV_DIRTY || sink.dirty_count == 1);
    CHECK_EQ(sink.dirty[0].w, W);
    CHECK_EQ(sink.dirty[0].h, H);
    CHECK_EQ(back[W + 1] & 0xFF, 0x00);
    (void)lanes;
}

static void test_srdp_rle(void)
{
    enum { W = 40, H = 20 };
    static unsigned int back[W * H];
    struct srdp_sink sink;
    struct srdp_event ev;
    unsigned char pkt[512];
    unsigned char rle[64];
    unsigned rle_len = 0;
    unsigned i, run;

    printf("srdp rle\n");
    /* 40*20 = 800 pixels: 300 red, 500 blue */
    for (run = 0; run < 2; run++) {
        unsigned count = run ? 500 : 300;
        unsigned value = run ? 0xFF0000FFu : 0xFFFF0000u;
        rle[rle_len++] = (unsigned char)(count & 0xFF);
        rle[rle_len++] = (unsigned char)(count >> 8);
        rle[rle_len++] = (unsigned char)(value & 0xFF);
        rle[rle_len++] = (unsigned char)((value >> 8) & 0xFF);
        rle[rle_len++] = (unsigned char)((value >> 16) & 0xFF);
        rle[rle_len++] = (unsigned char)((value >> 24) & 0xFF);
    }

    srdp_sink_init(&sink, W, H, sink_alloc, sink_free);
    sink.back = back;
    memset(back, 0, sizeof(back));

    pkt_hdr(pkt, SRDP_T_RECT_BEGIN, sizeof(struct srdp_rect_begin));
    {
        struct srdp_rect_begin rb = { 0, 0, 0, W, H, SRDP_ENC_RLE, 0, 1, rle_len };
        memcpy(pkt + sizeof(struct srdp_hdr), &rb, sizeof(rb));
    }
    CHECK_EQ(srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_begin), &ev), 0);

    pkt_hdr(pkt, SRDP_T_RECT_DATA, sizeof(struct srdp_rect_data) + rle_len);
    {
        struct srdp_rect_data rd = { 0, 0, 0 };
        memcpy(pkt + sizeof(struct srdp_hdr), &rd, sizeof(rd));
        memcpy(pkt + sizeof(struct srdp_hdr) + sizeof(rd), rle, rle_len);
    }
    srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_data) + rle_len, &ev);
    CHECK_EQ(sink.dirty_count, 1);
    CHECK_EQ(back[0], 0xFFFF0000u);
    CHECK_EQ(back[299], 0xFFFF0000u);
    CHECK_EQ(back[300], 0xFF0000FFu);
    CHECK_EQ(back[799], 0xFF0000FFu);
    for (i = 0; i < 3; i++) {
        CHECK(back[i + 100] == 0xFFFF0000u);
    }
}

static void test_srdp_malformed(void)
{
    struct srdp_sink sink;
    struct srdp_event ev;
    unsigned char pkt[1024];
    unsigned i;
    static unsigned int back[16 * 16];

    printf("srdp malformed input\n");
    srdp_sink_init(&sink, 16, 16, sink_alloc, sink_free);
    sink.back = back;

    /* wrong magic, truncated header, lying payload length */
    memset(pkt, 0, sizeof(pkt));
    CHECK_EQ(srdp_sink_feed(&sink, pkt, 64, &ev), -1);
    pkt_hdr(pkt, SRDP_T_RECT_BEGIN, 4096);
    CHECK_EQ(srdp_sink_feed(&sink, pkt, 40, &ev), -1);

    /* rectangle outside the panel */
    pkt_hdr(pkt, SRDP_T_RECT_BEGIN, sizeof(struct srdp_rect_begin));
    {
        struct srdp_rect_begin rb = { 0, 15, 15, 64, 64, SRDP_ENC_RAW, 0, 1, 64 * 64 * 4 };
        memcpy(pkt + sizeof(struct srdp_hdr), &rb, sizeof(rb));
    }
    srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_begin), &ev);
    CHECK_EQ(sink.rect.active, 0);
    CHECK(sink.dropped_rects >= 1);

    /* data for a rectangle that was never announced */
    pkt_hdr(pkt, SRDP_T_RECT_DATA, sizeof(struct srdp_rect_data) + 8);
    CHECK_EQ(srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_data) + 8, &ev), -1);

    /* raw data longer than the rectangle */
    pkt_hdr(pkt, SRDP_T_RECT_BEGIN, sizeof(struct srdp_rect_begin));
    {
        struct srdp_rect_begin rb = { 0, 0, 0, 4, 4, SRDP_ENC_RAW, 0, 1, 4 * 4 * 4 };
        memcpy(pkt + sizeof(struct srdp_hdr), &rb, sizeof(rb));
    }
    srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_begin), &ev);
    pkt_hdr(pkt, SRDP_T_RECT_DATA, sizeof(struct srdp_rect_data) + 200);
    {
        struct srdp_rect_data rd = { 0, 0, 0 };
        memcpy(pkt + sizeof(struct srdp_hdr), &rd, sizeof(rd));
    }
    srdp_sink_feed(&sink, pkt, sizeof(struct srdp_hdr) + sizeof(struct srdp_rect_data) + 200, &ev);
    CHECK_EQ(sink.rect.active, 0);

    /* random garbage must never crash or leak into the framebuffer */
    for (i = 0; i < 2000; i++) {
        unsigned len = (i * 37u) % 512u;
        unsigned j;
        for (j = 0; j < len; j++) {
            pkt[j] = (unsigned char)((i * 131u + j * 17u) & 0xFF);
        }
        if (len >= (unsigned)sizeof(struct srdp_hdr) && (i & 1)) {
            pkt_hdr(pkt, (unsigned char)(1 + (i % 11)), len - sizeof(struct srdp_hdr));
        }
        srdp_sink_feed(&sink, pkt, len, &ev);
    }
    CHECK(1);
}

/* ------------------------------------------------------------------ sd --- */
static void test_sd_csd(void)
{
    /* CSD v2 layout: c_size = bits 48..69, read_bl_len = 80..83, c_size_mult =
     * 39..41.  A 16 GB card reports c_size = 0x0070FF (28895) -> ~14.8 GiB. */
    u32 resp[4] = { 0, 0, 0, 0 };
    u32 c_size = 0x0070FFu;
    u32 block_nr = 0x0F;
    int bit;

    printf("sd csd\n");
    for (bit = 0; bit < 22; bit++) {
        if (c_size & (1u << bit)) {
            int pos = 48 + bit;
            resp[pos / 32] |= 1u << (pos % 32);
        }
    }
    for (bit = 0; bit < 4; bit++) {
        if (block_nr & (1u << bit)) {
            int pos = 80 + bit;
            resp[pos / 32] |= 1u << (pos % 32);
        }
    }
    CHECK_EQ(sd_extract_bits(resp, 48, 69), c_size);
    CHECK_EQ(sd_extract_bits(resp, 80, 83), block_nr);
    CHECK_EQ(sd_extract_bits(resp, 0, 3), 0);
    /* the capacity the driver derives from it: (c_size+1) * 512 KiB */
    CHECK_EQ(((u64)(sd_extract_bits(resp, 48, 69) + 1) * 1024ull * 512ull) >> 30,
             14ull);
    /* out of range requests must be rejected, not silently shifted */
    CHECK_EQ(sd_extract_bits(resp, 100, 140), 0);
    CHECK_EQ(sd_extract_bits(resp, -1, 4), 0);
}

/* --------------------------------------------------------------- elfload --- */
struct fake_alloc_ctx {
    unsigned char *arena;
    size_t size;
    uintptr_t want;
    int force_bias;
};

static int fake_alloc(uintptr_t want, uintptr_t size, uintptr_t *got, void *ctx)
{
    struct fake_alloc_ctx *c = (struct fake_alloc_ctx *)ctx;
    (void)want;
    if (size > c->size) {
        return -1;
    }
    *got = (uintptr_t)c->arena;
    return 0;
}

static void test_elfload(const char *kernel_path)
{
    FILE *f;
    long size;
    unsigned char *image;
    static unsigned char *arena;
    struct elf_image img;
    struct fake_alloc_ctx ctx;
    int rc;

    printf("elfload (%s)\n", kernel_path);
    f = fopen(kernel_path, "rb");
    if (!f) {
        printf("  SKIP: kernel image not built yet\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    image = malloc((size_t)size);
    if (fread(image, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(image);
        CHECK(0);
        return;
    }
    fclose(f);

    /* 64 MiB of address space is plenty for the kernel image */
    arena = malloc(64u * 1024 * 1024);
    memset(&ctx, 0, sizeof(ctx));
    ctx.arena = arena + 0x100000; /* a non-zero bias on purpose */
    ctx.size = 64u * 1024 * 1024 - 0x100000;

    rc = elf_load_image(image, (uintptr_t)size, 0x82000000u, fake_alloc, &ctx, &img);
    CHECK_EQ(rc, ELF_OK);
    CHECK_EQ(img.load_base, (uintptr_t)ctx.arena);
    CHECK_EQ(img.link_base, 0x82000000u);
    CHECK(img.entry >= img.load_base);
    CHECK(img.entry < img.load_base + img.span);
    CHECK(img.relocations > 100);   /* the kernel has thousands */

    /* Verify relocations the hard way: every R_ARM_ABS32 site in the ELF must
     * now contain the original file value plus the load bias. */
    {
        struct elf32_hdr_proxy {
            unsigned char ident[16];
            unsigned short type, machine;
            unsigned int version, entry, phoff, shoff, flags;
            unsigned short ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
        } *eh = (void *)image;
        unsigned short *ph = (unsigned short *)(image + eh->phoff);
        unsigned short *sh = (unsigned short *)(image + eh->shoff);
        unsigned int i, checked = 0;
        unsigned char *phb = (unsigned char *)ph;
        unsigned char *shb = (unsigned char *)sh;

        for (i = 0; i < eh->shnum; i++) {
            unsigned int *shdr = (unsigned int *)(shb + i * eh->shentsize);
            unsigned int type = shdr[1];
            unsigned int off = shdr[4], sz = shdr[5];
            unsigned int j;

            if (type != 9 /* SHT_REL */) {
                continue;
            }
            for (j = 0; j + 8 <= sz; j += 8) {
                unsigned int *rel = (unsigned int *)(image + off + j);
                unsigned int r_offset = rel[0];
                unsigned int r_type = rel[1] & 0xFF;
                unsigned int k;
                unsigned int file_val = 0;
                int found = 0;

                if (r_type != 2 /* R_ARM_ABS32 */) {
                    continue;
                }
                for (k = 0; k < eh->phnum; k++) {
                    unsigned int *phdr = (unsigned int *)(phb + k * eh->phentsize);
                    if (phdr[0] != 1) {
                        continue;
                    }
                    if (r_offset >= phdr[2] && r_offset + 4 <= phdr[2] + phdr[4]) {
                        file_val = *(unsigned int *)(image + phdr[1] +
                                                     (r_offset - phdr[2]));
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    continue;
                }
                {
                    unsigned int loaded = *(unsigned int *)(img.load_base +
                                                           (r_offset - img.link_base));
                    if (loaded != file_val + (unsigned int)(img.load_base -
                                                            img.link_base)) {
                        printf("  FAIL reloc at 0x%08x: 0x%08x != 0x%08x\n",
                               r_offset, loaded,
                               file_val + (unsigned int)(img.load_base -
                                                         img.link_base));
                        g_failures++;
                        g_checks++;
                    } else {
                        checked++;
                    }
                }
            }
        }
        printf("  %u absolute relocations verified\n", checked);
        CHECK(checked > 100);
    }

    free(arena);
    free(image);
}

int main(int argc, char **argv)
{
    const char *kernel = (argc > 1) ? argv[1] : "../build/kernel/KERNEL.ELF";

    printf("SurfaceRT_OS host tests\n\n");
    test_strings();
    test_printf();
    test_heap();
    test_srdp_raw();
    test_srdp_rle();
    test_srdp_malformed();
    test_sd_csd();
    test_elfload(kernel);

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
