#include "trng.h"
#include "chacha20.h"
#include <string.h>

// Hardware Drivers
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/systick.h"
#include "drivers/bsp/adc.h"
#include "drivers/cmsis/Device/PY32F071/Include/py32f0xx.h"
#include "drivers/hal/Inc/py32f071_ll_adc.h"
#include "drivers/hal/Inc/py32f071_ll_utils.h"

// Inline rotation (efficient on ARM)
static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void secure_memzero(void *v, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) *p++ = 0;
}

// State of the RNG
static struct {
    uint32_t state[16]; // ChaCha20 State (Constants, Key, Counter, Nonce)
    uint32_t reseed_counter;
    int      initialized;
} rng_state;

// Mix entropy directly into the RNG key state (Indices 4-11)
static void rng_mix_entropy(void) {
    uint32_t entropy = 0;
    
    // 1. BK4819 ExNoise Register (16-bit)
    entropy ^= (BK4819_ReadRegister(BK4819_REG_65) & 0xFFFF);
    
    // 2. ADC Noise (Thermal/Quantization)
    static uint8_t ch = 0;
    uint32_t ch_list[] = {LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_CHANNEL_VREFINT, LL_ADC_CHANNEL_1_3VCCA};
    entropy ^= (ADC_ReadChannel(ch_list[ch]) << 16); // Move ADC noise to high word
    ch = (ch + 1) % 3;
    
    // 3. Clock Jitter
    entropy ^= SysTick->VAL;

    // Mix into Key Area (Indices 4-11)
    static uint8_t mix_idx = 4;
    
    rng_state.state[mix_idx] = rotl32(rng_state.state[mix_idx] ^ entropy, 13);
    
    if(++mix_idx > 11) mix_idx = 4;
}

void TRNG_Init(void) {
    // 1. Setup ChaCha20 Constants
    rng_state.state[0] = 0x61707865; rng_state.state[1] = 0x3320646e;
    rng_state.state[2] = 0x79622d32; rng_state.state[3] = 0x6b206574;
    
    // 2. Seed Key from Hardware Unique ID
    rng_state.state[4] = LL_GetUID_Word0();
    rng_state.state[5] = LL_GetUID_Word1();
    rng_state.state[6] = LL_GetUID_Word2();
    // Fill remainder with SysTick
    for(int i=7; i<16; i++) rng_state.state[i] = SysTick->VAL;
    
    // 3. Initial Entropy Mix
    for(int i=0; i<32; i++) rng_mix_entropy();
    
    rng_state.initialized = 1;
    rng_state.reseed_counter = 0;
}

uint32_t TRNG_GetU32(void) {
    if(!rng_state.initialized) TRNG_Init();

    // 1. Generate Output Block (64 bytes)
    uint32_t output_block[16];
    chacha20_block(rng_state.state, (uint8_t*)output_block);
    
    // 2. Forward Secrecy (Fast Key Erasure)
    for(int i=4; i<12; i++) {
        rng_state.state[i] = output_block[i];
    }
    
    // 3. Advance Counter
    rng_state.state[12]++;
    
    // 4. Mix Fresh Entropy (Opportunistic)
    rng_mix_entropy();
    
    // 5. Force Reseed every 64 calls
    if (rng_state.reseed_counter++ > 64) {
        rng_state.reseed_counter = 0;
    }
    
    uint32_t result = output_block[0];
    
    // Wipe stack
    secure_memzero(output_block, sizeof(output_block));
    
    return result;
}

void TRNG_Fill(void *buffer, size_t size) {
    uint8_t *p = (uint8_t *)buffer;
    while(size >= 4) {
        uint32_t val = TRNG_GetU32();
        memcpy(p, &val, 4);
        p += 4;
        size -= 4;
    }
    if(size > 0) {
        uint32_t val = TRNG_GetU32();
        memcpy(p, &val, size);
    }
}
