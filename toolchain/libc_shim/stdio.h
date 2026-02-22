#ifndef _DELTAFW_LIBC_STDIO_H
#define _DELTAFW_LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct _FILE FILE;

int sprintf(char *__restrict str, const char *__restrict format, ...);
int snprintf(char *__restrict str, size_t size, const char *__restrict format, ...);
int vsprintf(char *__restrict str, const char *__restrict format, va_list ap);
int vsnprintf(char *__restrict str, size_t size, const char *__restrict format, va_list ap);
int printf(const char *__restrict format, ...);

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* _DELTAFW_LIBC_STDIO_H */
