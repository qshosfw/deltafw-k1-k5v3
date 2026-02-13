#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- ChaCha20 ---

typedef struct {
    uint32_t state[16];
    uint32_t keystream[16];
    size_t available;
} chacha20_ctx;

void chacha20_setup(chacha20_ctx *ctx, const uint8_t *key, uint32_t length, const uint8_t nonce[12]);
void chacha20_set_counter(chacha20_ctx *ctx, uint32_t counter);
void chacha20_encrypt_bytes(chacha20_ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t length);
void chacha20_decrypt_bytes(chacha20_ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t length);

// --- Poly1305 ---

typedef struct {
    size_t aligner;
    unsigned char opaque[136];
} poly1305_context;

void poly1305_init(poly1305_context *ctx, const unsigned char key[32]);
void poly1305_update(poly1305_context *ctx, const unsigned char *m, size_t bytes);
void poly1305_finish(poly1305_context *ctx, unsigned char mac[16]);
void poly1305_auth(unsigned char mac[16], const unsigned char *m, size_t bytes, const unsigned char key[32]);
int poly1305_verify(const unsigned char mac1[16], const unsigned char mac2[16]);

// --- TRNG ---

void TRNG_Init(void);
uint32_t TRNG_GetU32(void);
void TRNG_Fill(void *buffer, size_t size);

// --- Entropy Debug ---
float TRNG_GetTemp(void);  // Returns temperature in Celsius
float TRNG_GetVref(void);  // Returns internal Vref in Volts

// --- Utilities ---

uint64_t fmix64(uint64_t k);

#ifdef __cplusplus
}
#endif

#endif // CRYPTO_H
