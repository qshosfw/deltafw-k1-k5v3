/* Minimal freestanding libc headers for Zig/Clang cross-compilation.
 * These shims provide just enough declarations for the firmware C code
 * to compile under Zig's Clang. The actual implementations come from
 * the linker (newlib/nano.specs in Docker, or builtins).
 */
#ifndef _DELTAFW_LIBC_STRING_H
#define _DELTAFW_LIBC_STRING_H

#include <stddef.h>

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char  *strcpy(char *__restrict dest, const char *__restrict src);
char  *strncpy(char *__restrict dest, const char *__restrict src, size_t n);
char  *strcat(char *__restrict dest, const char *__restrict src);
char  *strncat(char *__restrict dest, const char *__restrict src, size_t n);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);

#endif /* _DELTAFW_LIBC_STRING_H */
