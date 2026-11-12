/* SurfaceRT_OS - freestanding string and format helpers */
#ifndef SRTOS_STRING_H
#define SRTOS_STRING_H

#include <srtos/types.h>
#include <stdarg.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);           /* uses kmalloc */
char *strsep(char **sp, const char *delim);

int isspace(int c);
int isdigit(int c);
int isxdigit(int c);
int isalpha(int c);
int isupper(int c);
int tolower(int c);
int toupper(int c);

long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int atoi(const char *s);

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int vsprintf(char *buf, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...) __printf(2, 3);

#endif
