#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Utilities ---
uint64_t fmix64(uint64_t k);

// --- ChaCha20 (RFC 8439) ---

typedef struct {
    uint32_t state[16];
} chacha20_ctx;

void chacha20_block(uint32_t *state, uint8_t *keystream);
void chacha20_init(chacha20_ctx *ctx, const uint8_t *key, const uint8_t *nonce, uint32_t counter);
void chacha20_encrypt(chacha20_ctx *ctx, const uint8_t *in, uint8_t *out, size_t len);

// --- Poly1305 (RFC 8439) ---

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

// --- TRNG (ChaCha20-DRBG with Forward Secrecy) ---

void TRNG_Init(void);
uint32_t TRNG_GetU32(void);
void TRNG_Fill(void *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // CRYPTO_H
