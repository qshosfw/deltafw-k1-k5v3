/* Minimal libc function implementations for Zig/LLD linking.
 * These replace newlib's nano.specs functions that the firmware uses.
 * Only the functions actually referenced by the firmware are included.
 */
#include <stddef.h>

/* ── String functions ──────────────────────────────────────────────── */

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

char *strcpy(char *restrict dest, const char *restrict src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *restrict dest, const char *restrict src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

char *strcat(char *restrict dest, const char *restrict src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : (void *)0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return (void *)0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return (void *)0;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

/* ── Math ──────────────────────────────────────────────────────────── */

int abs(int j) {
    return (j < 0) ? -j : j;
}

/* ── Startup stub ──────────────────────────────────────────────────── */

/* __libc_init_array is called by the startup assembly to run C++ constructors.
 * For pure C firmware it's a no-op, but must exist. */
void __libc_init_array(void) {
    /* Walk .preinit_array and .init_array */
    extern void (*__preinit_array_start[])(void);
    extern void (*__preinit_array_end[])(void);
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);

    size_t count = __preinit_array_end - __preinit_array_start;
    for (size_t i = 0; i < count; i++)
        __preinit_array_start[i]();

    count = __init_array_end - __init_array_start;
    for (size_t i = 0; i < count; i++)
        __init_array_start[i]();
}
