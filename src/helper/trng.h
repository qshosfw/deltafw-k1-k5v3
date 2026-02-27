#ifndef TRNG_H
#define TRNG_H

#include <stdint.h>
#include <stddef.h>

void TRNG_Init(void);
uint32_t TRNG_GetU32(void);
void TRNG_Fill(void *buffer, size_t size);

#endif // TRNG_H
