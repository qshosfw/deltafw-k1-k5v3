#include "signal_quality.h"
#include "drivers/bsp/bk4819.h"
#include "features/radio/radio.h"

static float s_smoothed_quality = 0.0f;
static uint32_t s_last_update_ms = 0;

// Ballistics: Polling @ 50ms
// Attack: ~20ms (with 50ms poll, this is effectively nearly instant)
// Decay: ~500ms -> alpha = 1 - exp(-poll_interval / decay_time) 
// alpha_decay = 1 - exp(-50/500) = 0.095
static const float ALPHA_ATTACK = 0.8f; 
static const float ALPHA_DECAY = 0.1f;

void SIGNAL_QUALITY_Init(void) {
    s_smoothed_quality = 0.0f;
    s_last_update_ms = 0;
}

extern uint32_t SYSTICK_GetTick(void);

void SIGNAL_QUALITY_Update(void) {
    uint32_t now = SYSTICK_GetTick();
    if (now - s_last_update_ms < 50) return; // 50ms polling interval (20Hz)
    s_last_update_ms = now;

    // 1. Get Power (Absolute Normalization)
    int16_t rssi_dbm = BK4819_GetRSSI_dBm();
    // Correct for AGC if not done in GetRSSI_dBm
    rssi_dbm -= BK4819_GetRxGain_dB();

    // 2. SNR/Quality Weighting
    uint8_t noise = BK4819_GetExNoiseIndicator();
    uint8_t glitch = BK4819_GetGlitchIndicator();

    // Mapping:
    // -121 dBm (12dB SINAD) -> Start of 1st bar
    // -85 dBm (Full quieting) -> 5 bars
    // Range is 36 dB. 
    
    float q = 0;
    if (rssi_dbm > -125) {
        q = (float)(rssi_dbm + 125) * (100.0f / 45.0f); // Map -125..-80 to 0..100
    }

    // Penalize for noise/glitch (SNR estimation)
    // Noise is 0..127, Glitch is 0..255
    float penalty = (float)noise * 0.8f + (float)glitch * 0.2f;
    if (penalty > q) q = 0;
    else q -= penalty;

    if (q > 100.0f) q = 100.0f;
    if (q < 0.0f) q = 0.0f;

    // 3. Asymmetrical EMA Smoothing
    float alpha = (q > s_smoothed_quality) ? ALPHA_ATTACK : ALPHA_DECAY;
    s_smoothed_quality = (alpha * q) + ((1.0f - alpha) * s_smoothed_quality);
}

uint8_t SIGNAL_QUALITY_GetLevel(void) {
    if (s_smoothed_quality < 5.0f) return 0;
    if (s_smoothed_quality < 20.0f) return 1;
    if (s_smoothed_quality < 40.0f) return 2;
    if (s_smoothed_quality < 65.0f) return 3;
    if (s_smoothed_quality < 85.0f) return 4;
    return 5;
}
