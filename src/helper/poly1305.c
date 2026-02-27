#include "poly1305.h"
#include <string.h>

static inline uint32_t load32_le(const uint8_t *s) {
    return s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
}

static inline void store32_le(uint8_t *d, uint32_t v) {
    d[0] = v; d[1] = v >> 8; d[2] = v >> 16; d[3] = v >> 24;
}

static void secure_memzero(void *v, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) *p++ = 0;
}

static int constant_time_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *c1 = (const uint8_t *)a;
    const uint8_t *c2 = (const uint8_t *)b;
    uint8_t result = 0;
    for (size_t i = 0; i < n; i++) {
        result |= c1[i] ^ c2[i];
    }
    return result; 
}

void poly1305_init(poly1305_context *ctx, const uint8_t key[32]) {
    ctx->r[0] = (load32_le(&key[0])) & 0x3ffffff;
    ctx->r[1] = (load32_le(&key[3]) >> 2) & 0x3ffff03;
    ctx->r[2] = (load32_le(&key[6]) >> 4) & 0x3ffc0ff;
    ctx->r[3] = (load32_le(&key[9]) >> 6) & 0x3f03fff;
    ctx->r[4] = (load32_le(&key[12]) >> 8) & 0x00fffff;

    ctx->h[0] = 0; ctx->h[1] = 0; ctx->h[2] = 0; ctx->h[3] = 0; ctx->h[4] = 0;

    ctx->pad[0] = load32_le(&key[16]);
    ctx->pad[1] = load32_le(&key[20]);
    ctx->pad[2] = load32_le(&key[24]);
    ctx->pad[3] = load32_le(&key[28]);
    
    ctx->leftover = 0;
    ctx->final = 0;
}

static void poly1305_process(poly1305_context *ctx, const uint8_t *m, size_t bytes) {
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];
    uint32_t r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2], r3 = ctx->r[3], r4 = ctx->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t hibit = ctx->final ? 0 : (1UL << 24);

    while (bytes >= 16) {
        uint64_t d0, d1, d2, d3, d4;
        
        h0 += (load32_le(m + 0)) & 0x3ffffff;
        h1 += (load32_le(m + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(m + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(m + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(m + 12) >> 8) | hibit;

        d0 = ((uint64_t)h0 * r0) + ((uint64_t)h1 * s4) + ((uint64_t)h2 * s3) + ((uint64_t)h3 * s2) + ((uint64_t)h4 * s1);
        d1 = ((uint64_t)h0 * r1) + ((uint64_t)h1 * r0) + ((uint64_t)h2 * s4) + ((uint64_t)h3 * s3) + ((uint64_t)h4 * s2);
        d2 = ((uint64_t)h0 * r2) + ((uint64_t)h1 * r1) + ((uint64_t)h2 * r0) + ((uint64_t)h3 * s4) + ((uint64_t)h4 * s3);
        d3 = ((uint64_t)h0 * r3) + ((uint64_t)h1 * r2) + ((uint64_t)h2 * r1) + ((uint64_t)h3 * r0) + ((uint64_t)h4 * s4);
        d4 = ((uint64_t)h0 * r4) + ((uint64_t)h1 * r3) + ((uint64_t)h2 * r2) + ((uint64_t)h3 * r1) + ((uint64_t)h4 * r0);

        uint32_t c;
        h0 = (uint32_t)d0 & 0x3ffffff; c = (uint32_t)(d0 >> 26);
        d1 += c; h1 = (uint32_t)d1 & 0x3ffffff; c = (uint32_t)(d1 >> 26);
        d2 += c; h2 = (uint32_t)d2 & 0x3ffffff; c = (uint32_t)(d2 >> 26);
        d3 += c; h3 = (uint32_t)d3 & 0x3ffffff; c = (uint32_t)(d3 >> 26);
        d4 += c; h4 = (uint32_t)d4 & 0x3ffffff; c = (uint32_t)(d4 >> 26);
        h0 += c * 5; c = (h0 >> 26); h0 &= 0x3ffffff; h1 += c;

        m += 16;
        bytes -= 16;
    }
    
    ctx->h[0] = h0; ctx->h[1] = h1; ctx->h[2] = h2; ctx->h[3] = h3; ctx->h[4] = h4;
}

void poly1305_update(poly1305_context *ctx, const uint8_t *m, size_t bytes) {
    if (ctx->leftover) {
        size_t want = 16 - ctx->leftover;
        if (want > bytes) want = bytes;
        memcpy(ctx->buffer + ctx->leftover, m, want);
        bytes -= want;
        m += want;
        ctx->leftover += want;
        if (ctx->leftover < 16) return;
        poly1305_process(ctx, ctx->buffer, 16);
        ctx->leftover = 0;
    }
    if (bytes >= 16) {
        size_t want = bytes & ~(16 - 1);
        poly1305_process(ctx, m, want);
        m += want;
        bytes -= want;
    }
    if (bytes) {
        memcpy(ctx->buffer + ctx->leftover, m, bytes);
        ctx->leftover += bytes;
    }
}

void poly1305_finish(poly1305_context *ctx, uint8_t mac[16]) {
    if (ctx->leftover) {
        ctx->buffer[ctx->leftover++] = 1;
        while (ctx->leftover < 16) ctx->buffer[ctx->leftover++] = 0;
        ctx->final = 1;
        poly1305_process(ctx, ctx->buffer, 16);
    }

    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];
    uint32_t c = h1 >> 26; h1 &= 0x3ffffff; h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff; h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1UL << 24);

    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

    h0 = ((h0) | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;

    uint64_t f = (uint64_t)h0 + ctx->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + ctx->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + ctx->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + ctx->pad[3] + (f >> 32); h3 = (uint32_t)f;

    store32_le(mac + 0, h0);
    store32_le(mac + 4, h1);
    store32_le(mac + 8, h2);
    store32_le(mac + 12, h3);
    
    secure_memzero(ctx, sizeof(poly1305_context));
}

int poly1305_verify(const uint8_t mac1[16], const uint8_t mac2[16]) {
    return constant_time_memcmp(mac1, mac2, 16);
}
