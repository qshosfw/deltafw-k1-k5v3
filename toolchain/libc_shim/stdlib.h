#ifndef _DELTAFW_LIBC_STDLIB_H
#define _DELTAFW_LIBC_STDLIB_H

#include <stddef.h>

int    abs(int j);
long   labs(long j);
int    atoi(const char *nptr);
long   atol(const char *nptr);
long   strtol(const char *__restrict nptr, char **__restrict endptr, int base);
unsigned long strtoul(const char *__restrict nptr, char **__restrict endptr, int base);

void  *malloc(size_t size);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);
void   free(void *ptr);

void   qsort(void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* _DELTAFW_LIBC_STDLIB_H */
