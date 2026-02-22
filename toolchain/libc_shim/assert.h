#ifndef _DELTAFW_LIBC_ASSERT_H
#define _DELTAFW_LIBC_ASSERT_H

/* Firmware assert — just loop forever on failure (no OS to call abort()) */
#ifdef NDEBUG
  #define assert(expr) ((void)0)
#else
  #define assert(expr) ((expr) ? ((void)0) : __assert_fail())
  static inline void __assert_fail(void) { while(1) {} }
#endif

#endif /* _DELTAFW_LIBC_ASSERT_H */
