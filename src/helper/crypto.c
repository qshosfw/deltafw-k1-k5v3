#include "crypto.h"
#include <string.h>
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/systick.h"
#include "drivers/cmsis/Device/PY32F071/Include/py32f0xx.h"
#include "drivers/hal/Inc/py32f071_ll_adc.h"
#include "drivers/hal/Inc/py32f071_ll_bus.h"
#include "drivers/hal/Inc/py32f071_ll_rcc.h"
#include "drivers/hal/Inc/py32f071_ll_utils.h"

// Credits:
// ChaCha20 and Poly1305-Donna adapted from jhviggo/chacha20-poly1305
// Poly1305-Donna originally by floodyberry

// --- Utilities ---

uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

static uint32_t U8TO32_LITTLE(const uint8_t *p) {
  return (((uint32_t)((p)[0])) | ((uint32_t)((p)[1]) << 8) | \
          ((uint32_t)((p)[2]) << 16) | ((uint32_t)((p)[3]) << 24));
}

static void U32TO8_LITTLE(uint8_t *p, uint32_t v) {
  p[0] = (v) & 0xFF; p[1] = ((v) >> 8) & 0xFF;
  p[2] = ((v) >> 16) & 0xFF; p[3] = ((v) >> 24) & 0xFF;
}

#define ROTATE(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

#define QUARTERROUND(x, a, b, c, d) \
    x[a] += x[b]; x[d] = ROTATE(x[d] ^ x[a], 16); \
    x[c] += x[d]; x[b] = ROTATE(x[b] ^ x[c], 12); \
    x[a] += x[b]; x[d] = ROTATE(x[d] ^ x[a], 8); \
    x[c] += x[d]; x[b] = ROTATE(x[b] ^ x[c], 7);

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// --- ChaCha20 (RFC 8439) ---

void chacha20_setup(chacha20_ctx *ctx, const uint8_t *key, uint32_t length, const uint8_t nonce[12]) {
    const char *constants = "expand 32-byte k";
    
    ctx->state[0]  = U8TO32_LITTLE((const uint8_t *)constants + 0);
    ctx->state[1]  = U8TO32_LITTLE((const uint8_t *)constants + 4);
    ctx->state[2]  = U8TO32_LITTLE((const uint8_t *)constants + 8);
    ctx->state[3]  = U8TO32_LITTLE((const uint8_t *)constants + 12);
    
    // Key (256-bit / 32 bytes)
    for (int i=0; i<8; i++) {
        ctx->state[4+i] = U8TO32_LITTLE(key + i*4);
    }

    // Counter (32-bit - starting at 1 per spec, but we allow manual set via set_counter, usually 0 or 1 init)
    // RFC 8439 Section 2.4 says initial counter is 1
    ctx->state[12] = 1;
    
    // Nonce (96-bit)
    ctx->state[13] = U8TO32_LITTLE(nonce + 0);
    ctx->state[14] = U8TO32_LITTLE(nonce + 4);
    ctx->state[15] = U8TO32_LITTLE(nonce + 8);
    
    ctx->available = 0;
}

void chacha20_set_counter(chacha20_ctx *ctx, uint32_t counter) {
    ctx->state[12] = counter;
    ctx->available = 0;
}

void chacha20_block(chacha20_ctx *ctx, uint32_t output[16]) {
    uint32_t x[16];
    memcpy(x, ctx->state, 64);

    for (int i = 0; i < 10; i++) {
        QUARTERROUND(x, 0, 4, 8, 12);
        QUARTERROUND(x, 1, 5, 9, 13);
        QUARTERROUND(x, 2, 6, 10, 14);
        QUARTERROUND(x, 3, 7, 11, 15);
        QUARTERROUND(x, 0, 5, 10, 15);
        QUARTERROUND(x, 1, 6, 11, 12);
        QUARTERROUND(x, 2, 7, 8, 13);
        QUARTERROUND(x, 3, 4, 9, 14);
    }

    for (int i = 0; i < 16; i++) {
        output[i] = x[i] + ctx->state[i];
    }
    
    // Increment Block Counter
    ctx->state[12]++;
    // Warning: RFC 8439 doesn't specify stopping on overflow, but protocol limits
    // Typically max bytes is very large.
}

static inline void chacha20_xor(uint8_t *keystream, const uint8_t **in, uint8_t **out, size_t length) {
    for (size_t i = 0; i < length; i++) {
        *(*out)++ = *(*in)++ ^ keystream[i];
    }
}

void chacha20_encrypt_bytes(chacha20_ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t length) {
    if (!length) return;
    
    uint8_t *ks8 = (uint8_t *)ctx->keystream;

    // Use available keystream bytes
    if (ctx->available) {
        uint32_t amount = MIN(length, ctx->available);
        size_t offset = 64 - ctx->available;
        for (uint32_t i = 0; i < amount; i++) {
            *out++ = *in++ ^ ks8[offset + i];
        }
        ctx->available -= amount;
        length -= amount;
    }

    // Generate full blocks
    while (length >= 64) {
        chacha20_block(ctx, ctx->keystream); // Generate new block into keystream buffer
        // Apply XOR directly from block output (in native endianness? No, needs standard endianness)
        // chacha20_block outputs uint32 host endian.
        // We need little-endian byte stream.
        // Convert uint32 block to bytes
        for(int i=0; i<16; i++) {
           U32TO8_LITTLE(ks8 + i*4, ctx->keystream[i]);
        }
        
        for (int i = 0; i < 64; i++) {
            *out++ = *in++ ^ ks8[i];
        }
        length -= 64;
    }

    // Handle remaining bytes
    if (length) {
        chacha20_block(ctx, ctx->keystream);
        for(int i=0; i<16; i++) {
           U32TO8_LITTLE(ks8 + i*4, ctx->keystream[i]);
        }
        for (uint32_t i = 0; i < length; i++) {
            *out++ = *in++ ^ ks8[i];
        }
        ctx->available = 64 - length;
    }
}

void chacha20_decrypt_bytes(chacha20_ctx *ctx, const uint8_t *in, uint8_t *out, uint32_t length) {
    chacha20_encrypt_bytes(ctx, in, out, length);
}

// --- Poly1305 (Donna 32-bit) ---

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    size_t leftover;
    unsigned char buffer[16];
    unsigned char final;
} poly1305_state_internal_t;

static uint32_t U8TO32(const unsigned char *p) {
    return (((uint32_t)(p[0])) | ((uint32_t)(p[1]) << 8) | ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24));
}

static void U32TO8(unsigned char *p, uint32_t v) {
    p[0] = (v) & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

void poly1305_init(poly1305_context *ctx, const unsigned char key[32]) {
    poly1305_state_internal_t *st = (poly1305_state_internal_t *)ctx;
    st->r[0] = (U8TO32(&key[0])) & 0x3ffffff;
    st->r[1] = (U8TO32(&key[3]) >> 2) & 0x3ffff03;
    st->r[2] = (U8TO32(&key[6]) >> 4) & 0x3ffc0ff;
    st->r[3] = (U8TO32(&key[9]) >> 6) & 0x3f03fff;
    st->r[4] = (U8TO32(&key[12]) >> 8) & 0x00fffff;
    st->h[0] = 0; st->h[1] = 0; st->h[2] = 0; st->h[3] = 0; st->h[4] = 0;
    st->pad[0] = U8TO32(&key[16]);
    st->pad[1] = U8TO32(&key[20]);
    st->pad[2] = U8TO32(&key[24]);
    st->pad[3] = U8TO32(&key[28]);
    st->leftover = 0;
    st->final = 0;
}

static void poly1305_blocks(poly1305_state_internal_t *st, const unsigned char *m, size_t bytes) {
    const uint32_t hibit = (st->final) ? 0 : (1UL << 24);
    uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
    uint64_t d0, d1, d2, d3, d4;
    uint32_t c;

    while (bytes >= 16) {
        h0 += (U8TO32(m + 0)) & 0x3ffffff;
        h1 += (U8TO32(m + 3) >> 2) & 0x3ffffff;
        h2 += (U8TO32(m + 6) >> 4) & 0x3ffffff;
        h3 += (U8TO32(m + 9) >> 6) & 0x3ffffff;
        h4 += (U8TO32(m + 12) >> 8) | hibit;

        d0 = ((uint64_t)h0 * r0) + ((uint64_t)h1 * s4) + ((uint64_t)h2 * s3) + ((uint64_t)h3 * s2) + ((uint64_t)h4 * s1);
        d1 = ((uint64_t)h0 * r1) + ((uint64_t)h1 * r0) + ((uint64_t)h2 * s4) + ((uint64_t)h3 * s3) + ((uint64_t)h4 * s2);
        d2 = ((uint64_t)h0 * r2) + ((uint64_t)h1 * r1) + ((uint64_t)h2 * r0) + ((uint64_t)h3 * s4) + ((uint64_t)h4 * s3);
        d3 = ((uint64_t)h0 * r3) + ((uint64_t)h1 * r2) + ((uint64_t)h2 * r1) + ((uint64_t)h3 * r0) + ((uint64_t)h4 * s4);
        d4 = ((uint64_t)h0 * r4) + ((uint64_t)h1 * r3) + ((uint64_t)h2 * r2) + ((uint64_t)h3 * r1) + ((uint64_t)h4 * r0);

        h0 = (uint32_t)d0 & 0x3ffffff; c = (uint32_t)(d0 >> 26);
        d1 += c; h1 = (uint32_t)d1 & 0x3ffffff; c = (uint32_t)(d1 >> 26);
        d2 += c; h2 = (uint32_t)d2 & 0x3ffffff; c = (uint32_t)(d2 >> 26);
        d3 += c; h3 = (uint32_t)d3 & 0x3ffffff; c = (uint32_t)(d3 >> 26);
        d4 += c; h4 = (uint32_t)d4 & 0x3ffffff; c = (uint32_t)(d4 >> 26);
        h0 += c * 5; c = (h0 >> 26); h0 &= 0x3ffffff; h1 += c;

        m += 16; bytes -= 16;
    }
    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

void poly1305_update(poly1305_context *ctx, const unsigned char *m, size_t bytes) {
    poly1305_state_internal_t *st = (poly1305_state_internal_t *)ctx;
    if (st->leftover) {
        size_t want = 16 - st->leftover;
        if (want > bytes) want = bytes;
        for (size_t i = 0; i < want; i++) st->buffer[st->leftover + i] = m[i];
        bytes -= want; m += want; st->leftover += want;
        if (st->leftover < 16) return;
        poly1305_blocks(st, st->buffer, 16);
        st->leftover = 0;
    }
    if (bytes >= 16) {
        size_t want = bytes & ~(16 - 1);
        poly1305_blocks(st, m, want);
        m += want; bytes -= want;
    }
    if (bytes) {
        for (size_t i = 0; i < bytes; i++) st->buffer[st->leftover + i] = m[i];
        st->leftover += bytes;
    }
}

void poly1305_finish(poly1305_context *ctx, unsigned char mac[16]) {
    poly1305_state_internal_t *st = (poly1305_state_internal_t *)ctx;
    uint32_t h0, h1, h2, h3, h4, c, g0, g1, g2, g3, g4, mask;
    uint64_t f;

    if (st->leftover) {
        size_t i = st->leftover;
        st->buffer[i++] = 1;
        while (i < 16) st->buffer[i++] = 0;
        st->final = 1;
        poly1305_blocks(st, st->buffer, 16);
    }

    h0 = st->h[0]; h1 = st->h[1]; h2 = st->h[2]; h3 = st->h[3]; h4 = st->h[4];
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff; h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    g4 = h4 + c - (1UL << 24);

    mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

    h0 = ((h0      ) | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >>  6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 <<  8)) & 0xffffffff;

    f = (uint64_t)h0 + st->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + st->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + st->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + st->pad[3] + (f >> 32); h3 = (uint32_t)f;

    U32TO8(mac + 0, h0); U32TO8(mac + 4, h1); U32TO8(mac + 8, h2); U32TO8(mac + 12, h3);
}

void poly1305_auth(unsigned char mac[16], const unsigned char *m, size_t bytes, const unsigned char key[32]) {
    poly1305_context ctx;
    poly1305_init(&ctx, key);
    poly1305_update(&ctx, m, bytes);
    poly1305_finish(&ctx, mac);
}

int poly1305_verify(const unsigned char mac1[16], const unsigned char mac2[16]) {
    unsigned int dif = 0;
    for (int i = 0; i < 16; i++) dif |= (mac1[i] ^ mac2[i]);
    return dif == 0;
}

// --- TRNG ---

static uint8_t TRNG_EntropyPool[32];
static uint32_t TRNG_EntropyIndex;

// DRBG State (ChaCha20 based)
static uint8_t DRBG_Key[32];
static uint8_t DRBG_Nonce[12];
static uint32_t DRBG_BlockCounter;
static uint32_t DRBG_ReseedCounter;

static float TRNG_LastTemp = 0.0f;
static float TRNG_LastVref = 0.0f;

// PY32F071 typical constants (Datasheet values to be verified)
// Using standard PY32/STM32F0 values in absence of working CAL reading
#define VREFINT_TYP_MV     1200.0f
#define TS_V30_MV          750.0f   // Approx 0.75V at 30C
#define TS_SLOPE_UV        2500.0f  // 2.5 mV/C

#define DRBG_RESEED_INTERVAL  16    // Reseed every 16 output blocks

// Helper to mix data into entropy pool
static void TRNG_MixEntropy(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        TRNG_EntropyPool[TRNG_EntropyIndex] ^= data[i];
        TRNG_EntropyIndex = (TRNG_EntropyIndex + 1) % 32;
    }
    // Simple fast mix to spread bits
    uint32_t *p32 = (uint32_t*)TRNG_EntropyPool;
    for(int i=0; i<8; i++) {
        p32[i] = (uint32_t)fmix64((uint64_t)p32[i]);
    }
}

static void TRNG_Reseed(void) {
    // Reseed: NewKey = ChaCha20(OldKey, Nonce, EntropyPool)
    // We encrypt the entropy pool with the current state to produce a new key
    chacha20_ctx ctx;
    chacha20_setup(&ctx, DRBG_Key, 32, DRBG_Nonce);
    
    // Encrypt the entropy pool in-place -> New Key
    // This allows the entropy to affect the next state cryptographically
    uint8_t new_key[32];
    chacha20_encrypt_bytes(&ctx, TRNG_EntropyPool, new_key, 32);
    
    memcpy(DRBG_Key, new_key, 32);
    
    // Reset counters and pool (partially to keep some history? No, full mixing done)
    DRBG_ReseedCounter = 0;
    
    // Changing Nonce is also good practice
    DRBG_Nonce[0] ^= new_key[0];
    DRBG_Nonce[1] ^= new_key[1];
}

#ifdef ENABLE_TRNG_SENSORS
static uint16_t TRNG_ReadADC(uint32_t channel) {
    // Enable ADC Clock
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_ADC1);
    
    // Ensure ADC is disabled to config
    if (LL_ADC_IsEnabled(ADC1)) {
        LL_ADC_Disable(ADC1);
    }
    
    // Configure Channel
    // Set Sequence Length to 1 (DISABLE scan) and set Rank 1 to channel
    LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, channel);
    
    // Set Sampling Time (Longer for internal sources)
    LL_ADC_SetChannelSamplingTime(ADC1, channel, LL_ADC_SAMPLINGTIME_239CYCLES_5);
    
    // Enable Internal paths if needed
    if (channel == LL_ADC_CHANNEL_TEMPSENSOR) {
        LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_TEMPSENSOR);
    } else if (channel == LL_ADC_CHANNEL_VREFINT) {
        LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_VREFINT);
    }
    
    // Enable ADC
    LL_ADC_Enable(ADC1);
    
    // Wait for enable/stabilization (approx 5-10us needed, stick to simple delay)
    for(volatile int i=0; i<2000; i++);
    
    // Start Conversion
    LL_ADC_REG_StartConversionSWStart(ADC1);
    
    // Wait for EOS
    uint32_t timeout = 10000;
    while (!LL_ADC_IsActiveFlag_EOS(ADC1) && timeout--) {}
    
    uint16_t result = LL_ADC_REG_ReadConversionData12(ADC1);
    LL_ADC_ClearFlag_EOS(ADC1);
    
    return result;
}

static void TRNG_ReadSensors(void) {
    // 1. Read Raw ADC
    uint16_t t_raw = TRNG_ReadADC(LL_ADC_CHANNEL_TEMPSENSOR);
    uint16_t v_raw = TRNG_ReadADC(LL_ADC_CHANNEL_VREFINT);
    uint16_t vcca_raw = TRNG_ReadADC(LL_ADC_CHANNEL_1_3VCCA);
    
    // 2. Mix into Entropy Pool
    uint8_t raw_bytes[6];
    memcpy(raw_bytes, &t_raw, 2);
    memcpy(&raw_bytes[2], &v_raw, 2);
    memcpy(&raw_bytes[4], &vcca_raw, 2);
    TRNG_MixEntropy(raw_bytes, 6);

    // 3. Compute Physical Values using Typical Constants
    if (v_raw == 0) v_raw = 1; // Prevent div by zero
    
    // VDDA = 3.3V * (VREFINT_CAL / VREFINT_DATA)
    // But we use Typical 1.2V for VREFINT_CAL equivalent
    // VDDA = 1.2V * 4096 / VREFINT_DATA
    float vdda_mv = (VREFINT_TYP_MV * 4095.0f) / (float)v_raw;
    TRNG_LastVref = vdda_mv / 1000.0f;
    
    // Temp Calculation
    // Vsense = Data * VDDA / 4096
    float vsense_mv = (float)t_raw * vdda_mv / 4095.0f;
    
    // T = ((Vsense - V30) / Slope) + 30
    TRNG_LastTemp = ((vsense_mv - TS_V30_MV) * 1000.0f / TS_SLOPE_UV) + 30.0f;
}
#else
static void TRNG_ReadSensors(void) { }
#endif

static void TRNG_FastEntropy(void) {
    uint32_t noise = BK4819_ReadRegister(BK4819_REG_65) & 0x007F; // Ex-noise
    uint32_t tick = SysTick->VAL;
    
    uint8_t data[8];
    memcpy(data, &noise, 4);
    memcpy(&data[4], &tick, 4);
    
    TRNG_MixEntropy(data, 8);
}

void TRNG_Init(void) {
    // Initial Seed from UID
    uint32_t uid[3];
    uid[0] = LL_GetUID_Word0();
    uid[1] = LL_GetUID_Word1();
    uid[2] = LL_GetUID_Word2();
    
    TRNG_MixEntropy((uint8_t*)uid, 12);
    
    #ifdef ENABLE_TRNG_SENSORS
    TRNG_ReadSensors(); 
    #endif
    
    // Initial Reseed
    TRNG_Reseed();
}

float TRNG_GetTemp(void) {
    TRNG_ReadSensors(); 
    return TRNG_LastTemp;
}

float TRNG_GetVref(void) {
    TRNG_ReadSensors(); 
    return TRNG_LastVref;
}

uint32_t TRNG_GetU32(void) {
    TRNG_FastEntropy(); // Continuous entropy gathering
    
    if (DRBG_ReseedCounter++ >= DRBG_RESEED_INTERVAL) {
        TRNG_Reseed();
    }
    
    chacha20_ctx ctx;
    // Nonce is static, but we vary it by counter or inside Reseed
    // Actually standard ChaCha RNG uses Counter as the block index
    // Here we use One-Shot encryption of a block index per call?
    // Let's use the DRBG_Key + DRBG_BlockCounter to generate output.
    
    // Setup Context
    uint32_t nonce_u32[3];
    memcpy(nonce_u32, DRBG_Nonce, 12);
    // Mix counter into nonce for uniqueness if key doesn't change often enough
    nonce_u32[0] ^= DRBG_BlockCounter++;
    
    chacha20_setup(&ctx, DRBG_Key, 32, (uint8_t*)nonce_u32);
    
    // Generate 4 bytes results
    uint32_t out;
    uint32_t zero = 0; // keystream from 0
    chacha20_encrypt_bytes(&ctx, (uint8_t*)&zero, (uint8_t*)&out, 4);
    
    return out;
}

void TRNG_Fill(void *buffer, size_t size) {
    uint8_t *p = (uint8_t *)buffer;
    while (size >= 4) {
        uint32_t val = TRNG_GetU32();
        memcpy(p, &val, 4);
        p += 4;
        size -= 4;
    }
    if (size > 0) {
        uint32_t val = TRNG_GetU32();
        memcpy(p, &val, size);
    }
}

