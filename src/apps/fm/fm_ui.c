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

#ifdef ENABLE_FMRADIO

#include <string.h>

#include "apps/fm/fm.h"
#include "drivers/bsp/bk1080.h"
#include "drivers/bsp/st7565.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#include "apps/fm/fm_ui.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/font.h"
#include "ui/ui.h"
#include "ui/status.h"

static uint8_t s_fm_audio_peak = 0;
static uint8_t s_fm_audio_peak_timer = 0;

void UI_DrawFMSeekBar(uint16_t frequency)
{
    const uint8_t X_CENTER = 64;
    const uint8_t Y_TOP = 42;
    const uint8_t Y_BOT = 63;
    const uint16_t f_min_100k = 640;  // 64.0 MHz in 100kHz units
    const uint16_t f_max_100k = 1080; // 108.0 MHz in 100kHz units

    // Horizontal straight lines
    UI_DrawLineBuffer(gFrameBuffer, 0, Y_TOP, 127, Y_TOP, true);
    //UI_DrawLineBuffer(gFrameBuffer, 0, Y_BOT, 127, Y_BOT, true);

    // Zoom: Pixels per 100kHz
    // 200k: 1MHz = 10px, 100k: 1MHz = 20px, 50k: 1MHz = 40px
    uint8_t zoom = (gFmSpacing == 0) ? 1 : (gFmSpacing == 1) ? 2 : 4;
    int16_t current_f_100k = (int16_t)(frequency / 10);
    int16_t rem_px = (frequency % 10) * zoom / 10;

    int16_t units_half = (64 / zoom) + 1;
    int16_t start_f = current_f_100k - units_half;
    int16_t end_f = current_f_100k + units_half;

    // Fixed Pointer
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER - 2, Y_TOP - 6, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER - 1, Y_TOP - 6, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER,     Y_TOP - 6, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER + 1, Y_TOP - 6, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER + 2, Y_TOP - 6, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER - 1, Y_TOP - 5, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER,     Y_TOP - 5, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER + 1, Y_TOP - 5, true);
    UI_DrawPixelBuffer(gFrameBuffer, X_CENTER,     Y_TOP - 4, true);
    UI_DrawLineBuffer(gFrameBuffer, X_CENTER, Y_TOP - 3, X_CENTER, Y_BOT, true);

    for (int16_t f = start_f; f <= end_f; f++) {
        if (f < f_min_100k || f > f_max_100k) continue;
        
        int16_t x = X_CENTER + (f - current_f_100k) * zoom - rem_px;
        if (x < 0 || x > 127) continue;

        if (f % 10 == 0) { // 1 MHz Tick
            UI_DrawLineBuffer(gFrameBuffer, x, Y_TOP, x, Y_TOP + 6, true);
           // UI_DrawLineBuffer(gFrameBuffer, x, Y_BOT - 6, x, Y_BOT, true);
            
            // Labels spaced out to avoid collision
            if (zoom < 4 || (f % 20 == 0)) {
                char str[4];
                uint16_t mhz = f / 10;
                NUMBER_ToDecimal(str, mhz, (mhz >= 100) ? 3 : 2, false);
                
                uint8_t w = strlen(str) * 4;
                uint8_t lx = x - (w / 2);
                uint8_t ly = Y_TOP + 7;
                for (uint8_t bx = (lx > 0) ? lx - 1 : 0; bx <= lx + w && bx < 128; bx++) {
                    for (uint8_t by = ly - 1; by <= ly + 6; by++) {
                        gFrameBuffer[by / 8][bx] &= ~(1 << (by % 8));
                    }
                }
                UI_PrintStringSmallest(str, lx, ly, false, true);
            }
        } else if (gFmSpacing != 0 && (f % 5 == 0)) { // 0.5 MHz Subtick
            UI_DrawLineBuffer(gFrameBuffer, x, Y_TOP, x, Y_TOP + 4, true);
            //UI_DrawLineBuffer(gFrameBuffer, x, Y_BOT - 4, x, Y_BOT, true);
        } else {
            // Adaptive Grid based on spacing
            uint8_t sub = (gFmSpacing == 0) ? 2 : 1; 
            if (f % sub == 0) {
                 uint8_t h = (gFmSpacing == 0) ? 3 : 2;
                 UI_DrawLineBuffer(gFrameBuffer, x, Y_TOP, x, Y_TOP + h, true);
                 //UI_DrawLineBuffer(gFrameBuffer, x, Y_BOT - h, x, Y_BOT, true);
            }
        }
    }
}

static void UI_DrawBigFreqPixel(const char *string, uint8_t X, uint8_t Y)
{
    const unsigned int charWidth = 13;
    bool bCanDisplay = false;

    for (const char *p = string; *p; p++) {
        char c = *p;
        if (c == '-') c = '9' + 1;
        if (bCanDisplay || c != ' ') {
            bCanDisplay = true;
            if (c >= '0' && c <= '9' + 1) {
                for (uint8_t i = 0; i < 13; i++) {
                    uint16_t col = (uint16_t)gFontBigDigits[c - '0'][i] | ((uint16_t)gFontBigDigits[c - '0'][i + 13] << 8);
                    for (uint8_t j = 0; j < 15; j++) {
                        if (col & (1 << j)) {
                            UI_DrawPixelBuffer(gFrameBuffer, X + i, Y + j, true);
                        }
                    }
                }
            } else if (c == '.') {
                X++; // Padding
                UI_DrawPixelBuffer(gFrameBuffer, X, Y + 13, true);
                UI_DrawPixelBuffer(gFrameBuffer, X + 1, Y + 13, true);
                UI_DrawPixelBuffer(gFrameBuffer, X, Y + 14, true);
                UI_DrawPixelBuffer(gFrameBuffer, X + 1, Y + 14, true);
                X += 2;
                continue;
            }
        }
        X += charWidth;
    }
}

void UI_DrawFMMetrics(uint16_t dev, uint8_t rssi, uint8_t snr)
{
    const uint8_t START_X = 2;
    const uint8_t BAR_Y = 28;
    const uint8_t BAR_H = 6;
    
    // Logarithmic non-linear width scaling for deviation
    static const uint8_t widths[] = {10, 8, 6, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2};
    static const uint16_t thresholds[] = {1, 10, 20, 35, 50, 70, 95, 125, 160, 205, 260, 330, 420, 540, 700, 950};

    uint32_t khz = (uint32_t)dev * 148 / 1000;
    uint8_t segments = 0;
    for (uint8_t i = 0; i < 16; i++) {
        if (khz >= thresholds[i]) segments = i + 1;
        else break;
    }

    if (segments > s_fm_audio_peak) {
        s_fm_audio_peak = segments;
        s_fm_audio_peak_timer = 15; 
    } else if (s_fm_audio_peak_timer > 0) {
        s_fm_audio_peak_timer--;
    } else if (s_fm_audio_peak > 0) {
        s_fm_audio_peak--;
    }

    // Draw DEV (Audio) Bar Segments
    uint8_t x = START_X;
    for (uint8_t i = 0; i < 16; i++) {
        bool lit = (i < segments);
        bool isPeak = (i + 1 == s_fm_audio_peak);
        
        if (lit || isPeak) {
            for (uint8_t dx = 0; dx < widths[i]; dx++) {
                if (x + dx < 128) {
                    for (uint8_t dy = 0; dy < BAR_H; dy++) {
                        gFrameBuffer[(BAR_Y + dy) / 8][x + dx] |= (1 << ((BAR_Y + dy) % 8));
                    }
                }
            }
        }
        x += widths[i] + 1;
    }

    // Draw Mini RSSI/SNR bars following the audio bar (Right-to-Left)
    // Non-linear (Squared) scaling for higher sensitivity at the low end
    const uint8_t RIGHT_X = 86;
    const uint8_t RIGHT_W = 40;
    const uint8_t RIGHT_END = RIGHT_X + RIGHT_W;
    
    // SNR (0-31 típ.): Top 2 pixels (Y=28, 29)
    // len = sqrt(val/31) * RIGHT_W -> using squared lookup or approx
    uint32_t snr_sq = (uint32_t)snr * 1000 / 31;
    uint8_t snr_len = (snr_sq * snr_sq * RIGHT_W) / 1000000;
    if (snr_len < 2 && snr > 0) snr_len = 2; // Min visibility
    if (snr_len > RIGHT_W) snr_len = RIGHT_W;

    for (uint8_t dx = 0; dx < snr_len; dx++) {
        gFrameBuffer[(BAR_Y + 0) / 8][RIGHT_END - 1 - dx] |= (1 << ((BAR_Y + 0) % 8));
        gFrameBuffer[(BAR_Y + 1) / 8][RIGHT_END - 1 - dx] |= (1 << ((BAR_Y + 1) % 8));
    }

    // 1px Spacing at BAR_Y + 2 (Y=30)
    
    // RSSI (0-100 típ.): Lower 2 pixels (Y=31, 32)
    uint32_t rssi_sq = (uint32_t)rssi * 1000 / 100;
    uint8_t rssi_len = (rssi_sq * rssi_sq * RIGHT_W) / 1000000;
    if (rssi_len < 2 && rssi > 0) rssi_len = 2;
    if (rssi_len > RIGHT_W) rssi_len = RIGHT_W;

    for (uint8_t dx = 0; dx < rssi_len; dx++) {
        gFrameBuffer[(BAR_Y + 3) / 8][RIGHT_END - 1 - dx] |= (1 << ((BAR_Y + 3) % 8));
        gFrameBuffer[(BAR_Y + 4) / 8][RIGHT_END - 1 - dx] |= (1 << ((BAR_Y + 4) % 8));
    }
}

void UI_DisplayFM(void)
{
    UI_DisplayClear();

    // Call actual status bar rendering (gStatusLine / Page 0)
    gUpdateStatus = true; // Ensure persistent labels and inverted F-box updating
    UI_DisplayStatus();

    // Big Frequency: Row 0 of app buffer (Page 1 in hardware)
    uint32_t freq = gEeprom.FM_FrequencyPlaying;
    char FreqStr[10];
    if (freq < 10000) { // < 100.00 MHz
        FreqStr[0] = (freq / 1000) % 10 + '0';
        FreqStr[1] = (freq / 100) % 10 + '0';
        FreqStr[2] = '.';
        FreqStr[3] = (freq / 10) % 10 + '0';
        FreqStr[4] = (freq % 10) + '0';
        FreqStr[5] = '\0';
    } else { // >= 100.00 MHz
        FreqStr[0] = (freq / 10000) % 10 + '0';
        FreqStr[1] = (freq / 1000) % 10 + '0';
        FreqStr[2] = (freq / 100) % 10 + '0';
        FreqStr[3] = '.';
        FreqStr[4] = (freq / 10) % 10 + '0';
        FreqStr[5] = (freq % 10) + '0';
        FreqStr[6] = '\0';
    }

    uint8_t freqWidth = (freq < 10000) ? (4 * 13 + 3) : (5 * 13 + 3);
    uint8_t totalWidth = freqWidth + 4 + 18;
    uint8_t startX = (128 - totalWidth) / 2;

    // Keep Frequency Y=0 and MHz Y=10 as set by USER
    UI_DrawBigFreqPixel(FreqStr, startX, 0);
    UI_PrintStringSmallest("MHz", startX + freqWidth + 3, 10, false, true); 

    char modeStr[48];
    modeStr[0] = '\0';
    if (BK1080_IsStereo()) strcpy(modeStr, "ST ");

    if (gRequestSaveFM) {
        strcat(modeStr, "SAVING..");
    } else if (gAskToSave) {
        char chNo[4];
        NUMBER_ToDecimal(chNo, gFM_ChannelPosition + 1, 2, true);
        strcat(modeStr, "SAVE FREQ TO CH-");
        strcat(modeStr, chNo);
        strcat(modeStr, "?");
    } else if (gAskToDelete) {
        char chNo[4];
        NUMBER_ToDecimal(chNo, gEeprom.FM_SelectedChannel + 1, 2, true);
        strcat(modeStr, "ERASE MEMORY CH-");
        strcat(modeStr, chNo);
        strcat(modeStr, "?");
    } else {
        // Dynamic Instrumentation Label at Y=18
        uint8_t rssi = BK1080_GetRSSI();
        uint8_t snr = BK1080_GetSNR();
        uint16_t dev_reg = BK1080_ReadRegister(BK1080_REG_07);
        uint32_t dev_khz = (uint32_t)BK1080_REG_07_GET_FREQD(dev_reg) * 148 / 1000;

        char temp[8];
        // RSSI with padding
        NUMBER_ToDecimal(temp, rssi, 2, true);
        strcat(modeStr, temp);
        strcat(modeStr, "uV ");

        // SNR with padding
        NUMBER_ToDecimal(temp, snr, 2, true);
        strcat(modeStr, temp);
        strcat(modeStr, "dB ");

        // Deviation with padding
        NUMBER_ToDecimal(temp, dev_khz, 3, true);
        strcat(modeStr, temp);
        strcat(modeStr, "kHz ");

        // Mode
        strcat(modeStr, gEeprom.FM_IsMrMode ? "MEM " : "VFO ");

        // Conditional Extra: AUTO or CH-NN
        if (gFM_AutoScan) {
            strcat(modeStr, "AUTO");
        } else if (gEeprom.FM_IsMrMode) {
            char chNo[4];
            NUMBER_ToDecimal(chNo, gEeprom.FM_SelectedChannel + 1, 2, true);
            strcat(modeStr, "CH-");
            strcat(modeStr, chNo);
        }
    }

    UI_PrintStringSmallest(modeStr, 64 - (strlen(modeStr) * 2), 18, false, true);

    uint8_t final_rssi = BK1080_GetRSSI();
    uint8_t final_snr = BK1080_GetSNR();
    uint16_t dev_reg_f = BK1080_ReadRegister(BK1080_REG_07);
    UI_DrawFMMetrics(BK1080_REG_07_GET_FREQD(dev_reg_f), final_rssi, final_snr);

    UI_DrawFMSeekBar(freq);

    ST7565_BlitFullScreen();
}

#endif
