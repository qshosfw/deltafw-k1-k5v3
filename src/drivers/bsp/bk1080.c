/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "bk1080.h"
#include "drivers/bsp/gpio.h"
#include "drivers/bsp/i2c.h"
#include "drivers/bsp/system.h"
#include "core/misc.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

static const uint16_t BK1080_RegisterTable[] =
{
    0x011E, 0x0000, 0x0201, 0x0000, 0x4010, 0x000B, 0xCE00, 0x0000,
    0x5B11, 0x0000, 0x411E, 0x0000, 0xCE00, 0x0000, 0x0000, 0x1000,
    0x3197, 0x0000, 0x13FF, 0x9852, 0x0000, 0x0000, 0x0008, 0x0000,
    0x51E1, 0xA8BC, 0x2645, 0x00E4, 0x1CD8, 0x3A50, 0xEAE0, 0x3000,
    0x0200, 0x0000,
};

static bool gIsInitBK1080;

uint16_t BK1080_BaseFrequency;
uint16_t BK1080_FrequencyDeviation;

void BK1080_Init0(void)
{
    BK1080_Init(0,0/*,0*/);
}

void BK1080_Init(uint16_t freq, uint8_t band/*, uint8_t space*/)
{
    unsigned int i;

    if (freq) {
        // GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080);

        if (!gIsInitBK1080) {
            for (i = 0; i < ARRAY_SIZE(BK1080_RegisterTable); i++)
                BK1080_WriteRegister(i, BK1080_RegisterTable[i]);

            SYSTEM_DelayMs(250);

            BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA83C);
            BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA8BC);

            SYSTEM_DelayMs(60);

            gIsInitBK1080 = true;
        }
        else {
            BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0201);
        }

        #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
            BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, gMute ? 0x0A10 : 0x0A1F);
        #else
            BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, 0x0A1F);
        #endif
        BK1080_SetFrequency(freq, band, 0);
    }
    else {
        BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0241);
        // GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
    }
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
    uint8_t Value[2];

    I2C_Start();
    I2C_Write(0x80);
    I2C_Write((Register << 1) | I2C_READ);
    I2C_ReadBuffer(Value, sizeof(Value));
    I2C_Stop();

    return (Value[0] << 8) | Value[1];
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
    I2C_Start();
    I2C_Write(0x80);
    I2C_Write((Register << 1) | I2C_WRITE);
    Value = ((Value >> 8) & 0xFF) | ((Value & 0xFF) << 8);
    I2C_WriteBuffer(&Value, sizeof(Value));
    I2C_Stop();
}

void BK1080_Mute(bool Mute)
{
    BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, Mute ? 0x4201 : 0x0201);
}

void BK1080_SetFrequency(uint16_t frequency, uint8_t band, uint8_t spacing)
{
    // High Precision: frequency passed in 10kHz units (e.g. 104.10 MHz = 10410)
    // Auto-select band for unified coverage
    uint8_t auto_band = (frequency < 7600) ? 3 : 1;
    uint16_t limit_10k = (auto_band == 3) ? 6400 : 7600;

    // spacing: 0:200k, 1:100k, 2:50k
    uint16_t spacing_khz = (spacing == 0) ? 200 : (spacing == 1) ? 100 : 50;

    // channel = (f_kHz - l_kHz) / spacing_kHz
    uint16_t channel = (uint16_t)(((uint32_t)frequency * 10 - (uint32_t)limit_10k * 10) / spacing_khz);

    uint16_t regval = BK1080_ReadRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2);
    // Band (7:6), Space (5:4). Clear bits 7:4.
    regval = (regval & ~0x00F0u) | ((uint16_t)(auto_band & 3) << 6) | ((uint16_t)(spacing & 3) << 4);

    BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, regval);
    BK1080_WriteRegister(BK1080_REG_03_CHANNEL, channel);
    
    SYSTEM_DelayMs(10);
    BK1080_WriteRegister(BK1080_REG_03_CHANNEL, channel | 0x8000); // TUNE
}

void BK1080_GetFrequencyDeviation(uint16_t Frequency)
{
    BK1080_BaseFrequency      = Frequency;
    BK1080_FrequencyDeviation = BK1080_ReadRegister(BK1080_REG_07) / 16;
}

uint16_t BK1080_GetFreqLoLimit(uint8_t band)
{
    static const uint16_t lim[] = {875, 760, 760, 640};
    return lim[band % 4];
}

uint16_t BK1080_GetFreqHiLimit(uint8_t band)
{
    static const uint16_t lim[] = {1080, 1080, 900, 760};
    return lim[band % 4];
}

uint8_t BK1080_GetRSSI(void)
{
    return BK1080_REG_10_GET_RSSI(BK1080_ReadRegister(BK1080_REG_10));
}

uint8_t BK1080_GetSNR(void)
{
    return BK1080_REG_07_GET_SNR(BK1080_ReadRegister(BK1080_REG_07));
}

uint16_t BK1080_GetAudioLevel(void)
{
    return BK1080_REG_07_GET_FREQD(BK1080_ReadRegister(BK1080_REG_07));
}

bool BK1080_IsStereo(void)
{
    return BK1080_REG_10_GET_STEN(BK1080_ReadRegister(BK1080_REG_10));
}

void BK1080_SetAudioProfile(uint8_t profile)
{
    uint16_t reg02 = BK1080_ReadRegister(BK1080_REG_02_POWER_CONFIGURATION);
    uint16_t reg04 = BK1080_ReadRegister(BK1080_REG_04_SYSTEM_CONFIGURATION1);
    
    // Default: Reset BASS(12), MONO(13) in Reg 02 and DEBPS(13), DE(11) in Reg 04
    reg02 &= ~((1 << 12) | (1 << 13));
    reg04 &= ~((1 << 13) | (1 << 11));

    switch(profile) {
        case 0: // 75us (USA)
            break; 
        case 1: // 50us (EU)
            reg04 |= (1 << 11);
            break;
        case 2: // RAW (De-emphasis Bypass)
            reg04 |= (1 << 13);
            break;
        case 3: // BASS + Mono (for single spk)
            reg02 |= (1 << 12) | (1 << 13);
            break;
    }
    
    BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, reg02);
    BK1080_WriteRegister(BK1080_REG_04_SYSTEM_CONFIGURATION1, reg04);
}

void BK1080_SetSoftMute(uint8_t rate, uint8_t attenuation)
{
    uint16_t reg06 = BK1080_ReadRegister(BK1080_REG_06_SYSTEM_CONFIGURATION3);
    
    // rate [15:14], attenuation [13:12]
    reg06 &= ~0xF000;
    reg06 |= ((uint16_t)(rate & 3) << 14) | ((uint16_t)(attenuation & 3) << 12);
    
    BK1080_WriteRegister(BK1080_REG_06_SYSTEM_CONFIGURATION3, reg06);
}

void BK1080_SetSeekThresholds(uint8_t rssi_th, uint8_t snr_th)
{
    // SEEKTH is in REG_05 [15:8]
    uint16_t reg05 = BK1080_ReadRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2);
    reg05 &= 0x00FF;
    reg05 |= ((uint16_t)rssi_th << 8);
    BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, reg05);

    // SKSNR is in REG_06 [7:4]
    uint16_t reg06 = BK1080_ReadRegister(BK1080_REG_06_SYSTEM_CONFIGURATION3);
    reg06 &= ~0x00F0;
    reg06 |= ((uint16_t)(snr_th & 0xF) << 4);
    BK1080_WriteRegister(BK1080_REG_06_SYSTEM_CONFIGURATION3, reg06);
}

void BK1080_SetVolume(uint8_t volume)
{
    uint16_t reg05 = BK1080_ReadRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2);
    
    // volume [3:0] in REG_05
    reg05 &= ~0x000F;
    reg05 |= (volume & 0x0F);
    
    BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, reg05);
}

