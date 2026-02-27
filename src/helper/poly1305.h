#ifndef POLY1305_H
#define POLY1305_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    size_t leftover;
    uint8_t buffer[16];
    uint8_t final;
} poly1305_context;

void poly1305_init(poly1305_context *ctx, const uint8_t key[32]);
void poly1305_update(poly1305_context *ctx, const uint8_t *m, size_t bytes);
void poly1305_finish(poly1305_context *ctx, uint8_t mac[16]);
int poly1305_verify(const uint8_t mac1[16], const uint8_t mac2[16]);

#endif // POLY1305_H
