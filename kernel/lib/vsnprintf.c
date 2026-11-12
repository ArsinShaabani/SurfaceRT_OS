/*
 * SurfaceRT_OS - tiny printf engine
 *
 * Freestanding and shared by the UEFI loader, the kernel and the host test
 * suite.  Supports: %d %i %u %x %X %o %p %s %c %% with '-', '0', '+', ' ',
 * '#', width, precision, and the h/hh/l/ll/z modifiers.
 */
#include <srtos/string.h>

struct out {
    char *buf;
    size_t size;   /* total buffer size including the NUL */
    size_t pos;    /* current write offset */
};

static void out_ch(struct out *o, char c)
{
    if (o->pos + 1 < o->size) {
        o->buf[o->pos] = c;
    }
    o->pos++;
}

static void out_num(struct out *o, unsigned long long v, unsigned base, int upper,
                    int neg, int width, int prec, int zero, int left,
                    int plus, int space, int prefix)
{
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, i, pad, zeros;

    if (v == 0 && prec != 0) {
        tmp[n++] = '0';
    }
    while (v) {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    if (!n) {
        n = 0;
    }
    zeros = prec > n ? prec - n : 0;
    pad = width - (n + zeros + neg + prefix);
    if (!left) {
        while (pad-- > 0) {
            out_ch(o, zero ? '0' : ' ');
        }
    }
    if (neg) {
        out_ch(o, '-');
    } else if (plus) {
        out_ch(o, '+');
    } else if (space) {
        out_ch(o, ' ');
    }
    if (prefix) {
        out_ch(o, '0');
        out_ch(o, upper ? 'X' : 'x');
    }
    for (i = 0; i < zeros; i++) {
        out_ch(o, '0');
    }
    for (i = n - 1; i >= 0; i--) {
        out_ch(o, tmp[i]);
    }
    if (left) {
        while (pad-- > 0) {
            out_ch(o, ' ');
        }
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o;
    o.buf = buf;
    o.size = size;
    o.pos = 0;

    while (*fmt) {
        int left = 0, zero = 0, plus = 0, space = 0, prefix = 0;
        int width = 0, prec = -1, len = 0;
        char c = *fmt++;

        if (c != '%') {
            out_ch(&o, c);
            continue;
        }
        /* flags */
        for (;;) {
            c = *fmt;
            if (c == '-') {
                left = 1;
            } else if (c == '0') {
                zero = 1;
            } else if (c == '+') {
                plus = 1;
            } else if (c == ' ') {
                space = 1;
            } else if (c == '#') {
                prefix = 1;
            } else {
                break;
            }
            fmt++;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (isdigit((unsigned char)*fmt)) {
                width = width * 10 + (*fmt++ - '0');
            }
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (isdigit((unsigned char)*fmt)) {
                    prec = prec * 10 + (*fmt++ - '0');
                }
            }
        }
        for (;;) {
            c = *fmt;
            if (c == 'h') {
                len--;
            } else if (c == 'l') {
                len++;
            } else if (c == 'z' || c == 't') {
                len = 1;
            } else {
                break;
            }
            fmt++;
        }

        c = *fmt++;
        switch (c) {
        case 'd':
        case 'i': {
            long long v = (len >= 2) ? va_arg(ap, long long)
                        : (len == 1) ? va_arg(ap, long)
                                     : va_arg(ap, int);
            int neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-v)
                                        : (unsigned long long)v;
            out_num(&o, uv, 10, 0, neg, width, prec, zero, left, plus, space, 0);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v = (len >= 2) ? va_arg(ap, unsigned long long)
                                 : (len == 1) ? va_arg(ap, unsigned long)
                                              : va_arg(ap, unsigned int);
            unsigned base = (c == 'o') ? 8 : ((c == 'u') ? 10 : 16);
            int pfx = prefix && (c == 'x' || c == 'X') && v;
            out_num(&o, v, base, c == 'X', 0, width, prec, zero, left, 0, 0, pfx);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(ap, void *);
            out_num(&o, v, 16, 0, 0, width ? width : 8, prec, 1, left, 0, 0, 1);
            break;
        }
        case 'c':
            out_ch(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int n;
            if (!s) {
                s = "(null)";
            }
            n = (int)strlen(s);
            if (prec >= 0 && n > prec) {
                n = prec;
            }
            if (!left) {
                for (int i = n; i < width; i++) {
                    out_ch(&o, ' ');
                }
            }
            for (int i = 0; i < n; i++) {
                out_ch(&o, s[i]);
            }
            if (left) {
                for (int i = n; i < width; i++) {
                    out_ch(&o, ' ');
                }
            }
            break;
        }
        case '%':
            out_ch(&o, '%');
            break;
        case 0:
            goto done;
        default:
            out_ch(&o, '%');
            out_ch(&o, c);
            break;
        }
    }
done:
    if (o.size) {
        size_t end = o.pos < o.size - 1 ? o.pos : o.size - 1;
        o.buf[end] = 0;
    }
    return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1 / 2, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}
