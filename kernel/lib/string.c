/* SurfaceRT_OS - freestanding string routines */
#include <srtos/string.h>

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    while (n--) {
        *dp++ = *sp++;
    }
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    if (dp == sp || !n) {
        return d;
    }
    if (dp < sp) {
        while (n--) {
            *dp++ = *sp++;
        }
    } else {
        dp += n;
        sp += n;
        while (n--) {
            *--dp = *--sp;
        }
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c) {
            return (void *)p;
        }
        p++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) {
        i++;
    }
    return i;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) {
    }
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && (*d = *s)) {
        d++;
        s++;
        n--;
    }
    while (n--) {
        *d++ = 0;
    }
    return r;
}

size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t len = strlen(s);
    if (n) {
        size_t cp = len < n - 1 ? len : n - 1;
        memcpy(d, s, cp);
        d[cp] = 0;
    }
    return len;
}

size_t strlcat(char *d, const char *s, size_t n)
{
    size_t dl = strnlen(d, n);
    size_t sl = strlen(s);
    if (dl == n) {
        return n + sl;
    }
    if (sl < n - dl) {
        memcpy(d + dl, s, sl + 1);
    } else {
        memcpy(d + dl, s, n - dl - 1);
        d[n - 1] = 0;
    }
    return dl + sl;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) {
            return (char *)s;
        }
        if (!*s) {
            return 0;
        }
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c) {
            last = s;
        }
        if (!*s) {
            return (char *)last;
        }
    }
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) {
        return (char *)h;
    }
    for (; *h; h++) {
        if (!strncmp(h, n, nl)) {
            return (char *)h;
        }
    }
    return 0;
}

char *strsep(char **sp, const char *delim)
{
    char *s = *sp;
    char *p;
    if (!s) {
        return 0;
    }
    p = s;
    while (*p && !strchr(delim, *p)) {
        p++;
    }
    if (*p) {
        *p = 0;
        *sp = p + 1;
    } else {
        *sp = 0;
    }
    return s;
}

int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    int neg = 0;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    for (;; s++) {
        int d;
        if (isdigit((unsigned char)*s)) {
            d = *s - '0';
        } else if (isxdigit((unsigned char)*s)) {
            d = tolower((unsigned char)*s) - 'a' + 10;
        } else {
            break;
        }
        if (d >= base) {
            break;
        }
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) {
        *end = (char *)s;
    }
    return neg ? (unsigned long)(-(long)v) : v;
}

long strtol(const char *s, char **end, int base)
{
    return (long)strtoul(s, end, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}
