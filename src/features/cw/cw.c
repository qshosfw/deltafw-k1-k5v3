/* Copyright 2025 deltafw
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

#include "features/cw/cw.h"

#ifdef ENABLE_CW_KEYER

#include <string.h>
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/system.h"
#include "features/audio/audio.h"
#include "features/radio/radio.h"
#include "features/radio/functions.h"
#include "core/misc.h"
#include "drivers/bsp/st7565.h"
#include "ui/helper.h"
#include "ui/main.h"

extern bool gUpdateDisplay;
extern volatile uint16_t gFlashLightBlinkCounter;

// Global CW context
CW_Context_t gCW;
static uint16_t gCW_HangTimer_10ms = 0;
#define CW_HANG_TIME_MS 500

// Morse lookup table
static const struct {
    char character;
    const char *pattern;
} MorseTable[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."}, {'0', "-----"},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'-', "-....-"}, {'(', "-.--."}, {')', "-.--.-"}, {':', "---..."},
    {'=', "-...-"}, {'+', ".-.-."}, {'@', ".--.-."},
    {0, NULL}
};

// Internal: Decode elements to character
static char CW_DecodeElements(void)
{
    if (gCW.decodeCount == 0 || gCW.decodeCount > CW_ELEMENT_BUF_SIZE) return '?';
    
    char pattern[8];
    for (uint8_t i = 0; i < gCW.decodeCount; i++) {
        pattern[i] = gCW.decodeBuf[i] ? '-' : '.';
    }
    pattern[gCW.decodeCount] = '\0';
    
    for (int i = 0; MorseTable[i].character != 0; i++) {
        if (strcmp(pattern, MorseTable[i].pattern) == 0) {
            return MorseTable[i].character;
        }
    }
    return '?';
}

// Internal: Add decoded character to text buffer
static void CW_AddDecodedChar(char c)
{
    // Clear symbol buffer when character is decoded
    gCW.symbolLen = 0;
    gCW.symbolBuf[0] = '\0';
    
    if (c == 0) return;
    
    if (gCW.textLen >= CW_DECODE_BUF_SIZE) {
        memmove(gCW.textBuf, gCW.textBuf + 1, CW_DECODE_BUF_SIZE - 1);
        gCW.textLen--;
    }
    gCW.textBuf[gCW.textLen++] = c;
    gCW.textBuf[gCW.textLen] = '\0';
    gUpdateDisplay = true;
}

// Internal: Add symbol to live buffer (e.g. '.' or '-')
static void CW_AddSymbol(char s)
{
    if (gCW.symbolLen >= 8) return; 
    gCW.symbolBuf[gCW.symbolLen++] = s;
    gCW.symbolBuf[gCW.symbolLen] = '\0';
    gUpdateDisplay = true;
}

// Internal: Start TX with proper setup
static void CW_StartTX(void)
{
    FUNCTION_Select(FUNCTION_TRANSMIT);
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 
        BK4819_REG_70_MASK_ENABLE_TONE1 | (66u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    BK4819_WriteRegister(BK4819_REG_71, (uint16_t)(CW_TONE_FREQ_HZ * 10.32444f));
    BK4819_SetAF(BK4819_AF_BEEP);
    BK4819_EnableTXLink();
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    
    gCW.state = CW_STATE_TX_STARTING;
    gCW.timer_10ms = 2;  
}

// Internal: Stop TX
static void CW_StopTX(void)
{
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    FUNCTION_Select(FUNCTION_FOREGROUND);
    gCW.state = CW_STATE_IDLE;
}

static void CW_ToneOn(void) { BK4819_ExitTxMute(); }
static void CW_ToneOff(void) { BK4819_EnterTxMute(); }

static bool CW_QueuePush(CW_Element_t elem)
{
    if (gCW.queueCount >= CW_QUEUE_SIZE) return false;
    gCW.queue[gCW.queueTail] = elem;
    gCW.queueTail = (gCW.queueTail + 1) % CW_QUEUE_SIZE;
    gCW.queueCount++;
    return true;
}

static bool CW_QueuePop(CW_Element_t *elem)
{
    if (gCW.queueCount == 0) return false;
    *elem = gCW.queue[gCW.queueHead];
    gCW.queueHead = (gCW.queueHead + 1) % CW_QUEUE_SIZE;
    gCW.queueCount--;
    return true;
}

void CW_Init(void)
{
    memset(&gCW, 0, sizeof(gCW));
    gCW.state = CW_STATE_IDLE;
    gCW.avgDotMs = 60;  // Start at 20 WPM
    gCW.avgDashMs = 180;
    gCW.avgNoiseRSSI = 50; 
    gCW.rxNoiseFloor = 10;
    gCW.rxSignalPeak = 30;
    gCW.avgNoiseIndicator = 40;
    gCW.debug = true;
}

void CW_SetDitPaddle(bool pressed) { gCW.paddle.dit = pressed; }
void CW_SetDahPaddle(bool pressed) { gCW.paddle.dah = pressed; }
void CW_StraightKeyDown(void) { CW_QueuePush(CW_ELEM_STRAIGHT_START); }
void CW_StraightKeyUp(void) { CW_QueuePush(CW_ELEM_STRAIGHT_STOP); }

static void CW_ProcessPaddles(void)
{
    if (gCW.paddle.dit) gCW.paddle.latchDit = true;
    if (gCW.paddle.dah) gCW.paddle.latchDah = true;
}

void CW_Tick10ms(void)
{
    // -------------------------------------------------------------------------
    // RX Processing
    // -------------------------------------------------------------------------
    // AGC Management: Disable for CW to get sharp R/A drops, restore otherwise.
    bool inCwMode = (gRxVfo->Modulation == MODULATION_CW);
    bool isRxOrForeground = (FUNCTION_IsRx() || gCurrentFunction == FUNCTION_FOREGROUND);
    
    if (inCwMode && isRxOrForeground) {
        if (!gCW.wasAgcEnabled) { // Borrowing this as "is AGC currently disabled by us"
            BK4819_SetAGC(false);
            gCW.wasAgcEnabled = true;
        }
    } else if (gCW.wasAgcEnabled) {
        BK4819_SetAGC(true);
        gCW.wasAgcEnabled = false;
    }

    if (gCW.state == CW_STATE_IDLE && isRxOrForeground && inCwMode) {
        uint16_t rssi = BK4819_GetRSSI();
        uint8_t noise = BK4819_GetExNoiseIndicator();
        uint8_t afTxRx = BK4819_GetAfTxRx();
        
        gCW.lastRSSI = rssi;
        gCW.lastNoise = noise;
        gCW.lastAf = afTxRx;
        gUpdateDisplay = true; 
        
        bool startup = (gCW.rxSignalTimer_10ms == 0 && gCW.rxGapTimer_10ms < 200); 
        
        bool rssiTrigger = (rssi >= gCW.avgNoiseRSSI + 12); 
        bool rssiHold    = (rssi >= gCW.avgNoiseRSSI + 6);  
        
        // --- Envelope Follower for AF Level (A) ---
        // noiseFloor: slowly tracks when signal is OFF
        // signalPeak: fast attack, slow leak when signal is ON
        if (!gCW.rxSignalOn) {
            // Update noise floor slowly
            gCW.rxNoiseFloor = (gCW.rxNoiseFloor * 63 + afTxRx) / 64;
            if (gCW.rxNoiseFloor < 2) gCW.rxNoiseFloor = 2;
        } else {
            // Update peak: fast attack
            if (afTxRx > gCW.rxSignalPeak) gCW.rxSignalPeak = afTxRx;
            // Slow leak (decay) to follow fading
            else gCW.rxSignalPeak = (gCW.rxSignalPeak * 511 + afTxRx) / 512;
            
            if (gCW.rxSignalPeak < gCW.rxNoiseFloor + 10) gCW.rxSignalPeak = gCW.rxNoiseFloor + 10;
        }

        // Schmitt Trigger Logic
        uint16_t threshold = gCW.rxNoiseFloor + (gCW.rxSignalPeak - gCW.rxNoiseFloor) / 2;
        bool afTrigger = (afTxRx > threshold + 2);
        bool afHold    = (afTxRx > threshold - 2);

        // 3. Noise Indicator (M) - Primary Carrier Release
        bool mStartTrigger = (noise < gCW.avgNoiseIndicator - 16);
        bool mHoldTrigger  = (noise < gCW.avgNoiseIndicator - 8);

        bool signalDetected;
        if (!gCW.rxSignalOn) {
            // Signal START requires AF trigger AND M drop (AND RSSI jump for safety)
            signalDetected = afTrigger && mStartTrigger && rssiTrigger;
        } else {
            // Signal HOLD: prioritize AF and M release
            if (rssi > gCW.avgNoiseRSSI + 100) {
                signalDetected = afHold && mHoldTrigger;
            } else {
                signalDetected = (rssiHold || afHold) && mHoldTrigger;
            }
        }

        // Glitch Filter / Debouncer (30ms)
        if (signalDetected != gCW.rxSignalOn) {
            gCW.rxGlitchTimer_10ms++;
            if (gCW.rxGlitchTimer_10ms >= 3 || startup) { 
                gCW.rxSignalOn = signalDetected;
                gCW.rxGlitchTimer_10ms = 0;
                
                if (gCW.rxSignalOn) {
                    gCW.rxSignalTimer_10ms = 0;
                    gCW.rxGapTimer_10ms = 0;
                } else {
                    // FALLING EDGE: Classify element
                    uint16_t ms = gCW.rxSignalTimer_10ms * 10;
                    if (ms > 20 && ms < 1000) {
                        uint16_t distDot = (ms > gCW.avgDotMs) ? (ms - gCW.avgDotMs) : (gCW.avgDotMs - ms);
                        uint16_t distDash = (ms > gCW.avgDashMs) ? (ms - gCW.avgDashMs) : (gCW.avgDashMs - ms);
                        bool isDah = (distDash < distDot);

                        if (!isDah) {
                            gCW.avgDotMs = (gCW.avgDotMs * 7 + ms) / 8;
                            if (gCW.avgDashMs < gCW.avgDotMs * 2) gCW.avgDashMs = gCW.avgDotMs * 3;
                        } else {
                            gCW.avgDashMs = (gCW.avgDashMs * 7 + ms) / 8;
                            if (gCW.avgDotMs > gCW.avgDashMs / 2) gCW.avgDotMs = gCW.avgDashMs / 3;
                        }
                        if (gCW.avgDotMs < 20) gCW.avgDotMs = 20;
                        if (gCW.avgDotMs > 250) gCW.avgDotMs = 250;
                        if (gCW.avgDashMs < 60) gCW.avgDashMs = 60;
                        if (gCW.avgDashMs > 750) gCW.avgDashMs = 750;

                        if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE) 
                            gCW.decodeBuf[gCW.decodeCount++] = isDah ? 1 : 0;
                        CW_AddSymbol(isDah ? '-' : '.');
                    }
                    gCW.rxGapTimer_10ms = 0;
                }
            }
        } else {
            gCW.rxGlitchTimer_10ms = 0;
        }

        if (gCW.rxSignalOn) {
            gCW.rxSignalTimer_10ms++;
        } else {
            // Floor Tracking for RSSI and M
            uint16_t alpha = startup ? 7 : 63; 
            if (rssi > gCW.avgNoiseRSSI) alpha = 15; 
            gCW.avgNoiseRSSI = (gCW.avgNoiseRSSI * alpha + rssi) / (alpha + 1);
            gCW.avgNoiseIndicator = (gCW.avgNoiseIndicator * alpha + noise) / (alpha + 1);
            if (gCW.avgNoiseRSSI < 10) gCW.avgNoiseRSSI = 10;

            // Gap Processing
            gCW.rxGapTimer_10ms++;
            uint16_t gapMs = gCW.rxGapTimer_10ms * 10;
            uint16_t dotLen = gCW.avgDotMs;
            if (gapMs >= (dotLen * 5) / 2) { 
                if (gCW.decodeCount > 0) {
                    CW_AddDecodedChar(CW_DecodeElements());
                    gCW.decodeCount = 0;
                }
            }
            if (gapMs >= (dotLen * 5)) {
                if (gCW.textLen > 0 && gCW.textBuf[gCW.textLen-1] != ' ') {
                    CW_AddDecodedChar(' ');
                }
                gCW.rxGapTimer_10ms = 0; 
            }
        }
    }

    // -------------------------------------------------------------------------
    // TX Processing
    // -------------------------------------------------------------------------
    CW_ProcessPaddles();

    if (gCW.state == CW_STATE_IDLE && (gCW.paddle.latchDit || gCW.paddle.latchDah || gCW.queueCount > 0)) {
        CW_StartTX();
        return;
    }
    
    switch (gCW.state) {
        case CW_STATE_TX_STARTING:
            if (gCW.timer_10ms > 0) gCW.timer_10ms--;
            else {
                gCW.state = CW_STATE_GAP; 
                gCW.timer_10ms = 0;
                gCW.duration_10ms = 0;
            }
            break;
            
        case CW_STATE_PLAYING_TONE:
            gCW.timer_10ms++;
            if (gCW.timer_10ms >= gCW.duration_10ms) {
                CW_ToneOff();
                gCW.state = CW_STATE_GAP;
                gCW.timer_10ms = 0;
                gCW.duration_10ms = CW_ELEMENT_GAP_MS / 10;
            }
            break;
            
        case CW_STATE_GAP:
            gCW.timer_10ms++;
            gCW.gapTimer_10ms++;
            if (gCW.timer_10ms >= gCW.duration_10ms) {
                bool playDit = false, playDah = false;
                static bool lastWasDit = false;

                if (gCW.paddle.latchDit && gCW.paddle.latchDah) { if (lastWasDit) playDah = true; else playDit = true; }
                else if (gCW.paddle.latchDit) playDit = true;
                else if (gCW.paddle.latchDah) playDah = true;
                else {
                    CW_Element_t elem;
                    if (CW_QueuePop(&elem)) {
                        if (elem == CW_ELEM_DIT) playDit = true;
                        else if (elem == CW_ELEM_DAH) playDah = true;
                        else if (elem == CW_ELEM_STRAIGHT_START) {
                            CW_ToneOn();
                            gCW.state = CW_STATE_STRAIGHT_TONE;
                            gCW.straightTimer_10ms = 0;
                            return;
                        }
                    }
                }

                if (playDit) {
                    gCW.paddle.latchDit = false; lastWasDit = true;
                    CW_ToneOn(); gCW.state = CW_STATE_PLAYING_TONE;
                    gCW.duration_10ms = CW_DOT_MS / 10; gCW.timer_10ms = 0; gCW.gapTimer_10ms = 0;
                    if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE) gCW.decodeBuf[gCW.decodeCount++] = 0;
                    CW_AddSymbol('.');
                } else if (playDah) {
                    gCW.paddle.latchDah = false; lastWasDit = false;
                    CW_ToneOn(); gCW.state = CW_STATE_PLAYING_TONE;
                    gCW.duration_10ms = CW_DASH_MS / 10; gCW.timer_10ms = 0; gCW.gapTimer_10ms = 0;
                    if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE) gCW.decodeBuf[gCW.decodeCount++] = 1;
                    CW_AddSymbol('-');
                } else {
                    gCW_HangTimer_10ms = 0;
                    gCW.state = CW_STATE_IDLE;
                }
            }
            break;
            
        case CW_STATE_IDLE:
            if (FUNCTION_IsTx()) {
                gCW.gapTimer_10ms++;
                uint16_t gapMs = gCW.gapTimer_10ms * 10;
                uint16_t dotLen = CW_DOT_MS;
                if (gapMs >= (dotLen * 25) / 10) { 
                    if (gCW.decodeCount > 0) { CW_AddDecodedChar(CW_DecodeElements()); gCW.decodeCount = 0; }
                }
                if (gapMs >= (dotLen * 5)) {
                    if (gCW.textLen > 0 && gCW.textBuf[gCW.textLen-1] != ' ') CW_AddDecodedChar(' ');
                    gCW.gapTimer_10ms = 0;
                }
                gCW_HangTimer_10ms++;
                if (gCW_HangTimer_10ms * 10 >= CW_HANG_TIME_MS && gCW.queueCount == 0 && !gCW.paddle.dit && !gCW.paddle.dah && !gCW.straightKeyDown) {
                    CW_StopTX();
                }
            }
            break;
            
        case CW_STATE_STRAIGHT_TONE:
            gCW.straightTimer_10ms++;
            if (gCW.queueCount > 0 && gCW.queue[gCW.queueHead] == CW_ELEM_STRAIGHT_STOP) {
                if (gCW.straightTimer_10ms * 10 < CW_DOT_MS) return; 
                CW_Element_t dummy; CW_QueuePop(&dummy);
                CW_ToneOff();
                uint16_t ms = gCW.straightTimer_10ms * 10;
                bool isDah = ms >= 150;
                if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE) gCW.decodeBuf[gCW.decodeCount++] = isDah ? 1 : 0;
                CW_AddSymbol(isDah ? '-' : '.');
                gCW.state = CW_STATE_GAP; gCW.timer_10ms = 0; gCW.duration_10ms = CW_ELEMENT_GAP_MS / 10; gCW.gapTimer_10ms = 0;
            }
            break;
        default: break;
    }
}

bool CW_IsBusy(void) { return gCW.state != CW_STATE_IDLE || gCW.queueCount > 0; }
const char* CW_GetDecodedText(void) { return gCW.textBuf; }
const char* CW_GetSymbolBuffer(void) { return gCW.symbolBuf; }
void CW_ClearDecoded(void) { gCW.textLen = 0; gCW.textBuf[0] = '\0'; gCW.symbolLen = 0; gCW.symbolBuf[0] = '\0'; }

void UI_DisplayCW(uint8_t line)
{
    const char *decoded = CW_GetDecodedText();
    const char *symbols = CW_GetSymbolBuffer();
    bool showCursor = (gFlashLightBlinkCounter % 40) < 20;
    memset(gFrameBuffer[line], 0, LCD_WIDTH);
    int symLen = symbols[0] ? strlen(symbols) : 0;
    int symWidth = symLen ? (symLen + 1) * 4 : 0;
    int maxDecChars = (120 - symWidth - 8) / 6;
    if (maxDecChars < 0) maxDecChars = 0;
    int decLen = decoded ? strlen(decoded) : 0;
    const char *decStart = (decLen > maxDecChars) ? (decoded + decLen - maxDecChars) : decoded;
    UI_PrintStringSmallNormal(decStart, 4, 0, line);
    if (showCursor) UI_PrintStringSmallNormal("\x7F", 4 + (strlen(decStart) * 6), 0, line);
    if (symLen) {
        char prompt[16]; prompt[0] = '>'; strcpy(prompt + 1, symbols);
        UI_PrintStringSmallest(prompt, 128 - 4 - symWidth, line * 8 + 1, false, true);
    }
    if (gCW.debug) {
        char debugStr[32];
        uint16_t threshold = gCW.rxNoiseFloor + (gCW.rxSignalPeak - gCW.rxNoiseFloor) / 2;
        // sprintf(debugStr, "R:%u/%u T:%u P:%u F:%u M:%u/%u %u", 
        //   gCW.lastRSSI, gCW.avgNoiseRSSI + 12,
        //   threshold, gCW.rxSignalPeak, gCW.rxNoiseFloor,
        //   gCW.lastNoise, gCW.avgNoiseIndicator + 12,
        //   gFlashLightBlinkCounter % 10);
        strcpy(debugStr, "R:");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.lastRSSI, 1, false);
        strcat(debugStr, "/");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.avgNoiseRSSI + 12, 1, false);
        strcat(debugStr, " T:");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), threshold, 1, false);
        strcat(debugStr, " P:");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.rxSignalPeak, 1, false);
        strcat(debugStr, " F:");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.rxNoiseFloor, 1, false);
        strcat(debugStr, " M:");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.lastNoise, 1, false);
        strcat(debugStr, "/");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gCW.avgNoiseIndicator + 12, 1, false);
        strcat(debugStr, " ");
        NUMBER_ToDecimal(debugStr + strlen(debugStr), gFlashLightBlinkCounter % 10, 1, false);
        uint8_t debugLine = (line > 0) ? line - 1 : line + 1;
        memset(gFrameBuffer[debugLine], 0, LCD_WIDTH);
        UI_PrintStringSmallest(debugStr, 4, debugLine * 8 + 1, false, true);
        ST7565_BlitLine(debugLine);
    }
    ST7565_BlitLine(line);
}

#endif // ENABLE_CW_KEYER
