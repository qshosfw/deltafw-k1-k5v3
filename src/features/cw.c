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

#include "cw.h"

#ifdef ENABLE_CW_KEYER

#include <string.h>
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/system.h"
#include "audio.h"
#include "radio.h"
#include "functions.h"
#include "core/misc.h"

// Global CW context
static CW_Context_t gCW;
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
    
    // allow '?' to be stored and displayed
    if (c == 0) return;
    
    if (gCW.textLen >= CW_DECODE_BUF_SIZE) {
        // Shift buffer
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
    if (gCW.symbolLen >= 8) return; // Max 8 symbols
    gCW.symbolBuf[gCW.symbolLen++] = s;
    gCW.symbolBuf[gCW.symbolLen] = '\0';
    gUpdateDisplay = true;
}

// Internal: Start TX with proper setup (like BK4819_TransmitTone)
static void CW_StartTX(void)
{
    // Set up TX like FUNCTION_Transmit does for other modes
    FUNCTION_Select(FUNCTION_TRANSMIT);
    
    // Now set up tone output using exact BK4819_TransmitTone pattern:
    BK4819_EnterTxMute();
    
    // Set up tone registers
    BK4819_WriteRegister(BK4819_REG_70, 
        BK4819_REG_70_MASK_ENABLE_TONE1 | (66u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    BK4819_WriteRegister(BK4819_REG_71, (uint16_t)(CW_TONE_FREQ_HZ * 10.32444f));
    
    // Enable sidetone
    BK4819_SetAF(BK4819_AF_BEEP);
    
    // Enable TX link (mic disabled, tone output enabled)
    BK4819_EnableTXLink();
    
    // Audio path for sidetone
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    
    // Wait for TX to stabilize (reduced for better responsiveness)
    // SYSTEM_DelayMs(50); // Removed as it blocks 10ms tick and UI
    
    gCW.state = CW_STATE_TX_STARTING;
    gCW.timer_10ms = 2;  // 20ms startup delay instead of 50ms
}

// Internal: Stop TX
static void CW_StopTX(void)
{
    // Mute everything first
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_SetAF(BK4819_AF_MUTE);
    
    // Explicitly disable TX Link (and usage of REG_30)
    // This helps prevent noise when switching back to RX
    BK4819_WriteRegister(BK4819_REG_30, 0);
    
    // End transmission
    FUNCTION_Select(FUNCTION_FOREGROUND);
    
    gCW.state = CW_STATE_IDLE;
}

// Internal: Start playing a tone
static void CW_ToneOn(void)
{
    BK4819_ExitTxMute();
}

// Internal: Stop playing tone (stay in TX)
static void CW_ToneOff(void)
{
    BK4819_EnterTxMute();
}

// Internal: Queue helper
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

// Public API

void CW_Init(void)
{
    memset(&gCW, 0, sizeof(gCW));
    gCW.state = CW_STATE_IDLE;
    gCW.avgDotMs = CW_DOT_MS; // Initialize with default speed
    gCW.avgNoiseRSSI = 40; // Sensible default noise floor
}

void CW_SetDitPaddle(bool pressed)
{
    gCW.paddle.dit = pressed;
    // If queue is empty and idle, we can kickstart it immediately (optional)
}

void CW_SetDahPaddle(bool pressed)
{
    gCW.paddle.dah = pressed;
}

void CW_StraightKeyDown(void)
{
    // Straight key bypasses queue logic for start, but uses queue for stop scheduling?
    // User requested "3 keys collect / add to queue".
    // For PTT/Straight, we usually just want immediate TX.
    // But let's stick to the queue API for consistency if possible, or just direct control.
    // PTT straight key usually overrides everything.
    CW_QueuePush(CW_ELEM_STRAIGHT_START);
}

void CW_StraightKeyUp(void)
{
    CW_QueuePush(CW_ELEM_STRAIGHT_STOP);
}

// Internal: Process paddles and fill queue
static void CW_ProcessPaddles(void)
{
    // Latch any paddle press that isn't already latched
    if (gCW.paddle.dit) gCW.paddle.latchDit = true;
    if (gCW.paddle.dah) gCW.paddle.latchDah = true;
    
    // If we are in IDLE, the state machine in Tick10ms will kick off 
    // the next element if either latch is set.
}

void CW_Tick10ms(void)
{
    // -------------------------------------------------------------------------
    // RX Processing (only when not transmitting and in CW mode)
    // -------------------------------------------------------------------------
    if (gCW.state == CW_STATE_IDLE && FUNCTION_IsRx() && gRxVfo->Modulation == MODULATION_CW) {
        uint16_t rssi = BK4819_GetRSSI();
        
        // Dynamic threshold: Signal is present if it's significantly above the noise floor
        // We use a small hysterisis by requiring a larger jump to start and a smaller gap to end.
        uint16_t triggerThresh = gCW.avgNoiseRSSI + 8; // ~4dB jump (0.5dB per unit)
        uint16_t holdThresh = gCW.avgNoiseRSSI + 4;    // ~2dB hold
        
        bool signalPresent = gCW.rxSignalOn ? (rssi >= holdThresh) : (rssi >= triggerThresh);
        
        // Floor-seeking noise floor tracker:
        // Drop quickly to follow downward noise trends (fast adaptation)
        // Rise very slowly to ignore the actual Morse signals (slow adaptation)
        if (rssi < gCW.avgNoiseRSSI) {
            gCW.avgNoiseRSSI = (gCW.avgNoiseRSSI * 3 + rssi) / 4;
        } else if (!signalPresent) {
            // Only rise if no signal is detected, and do it very slowly
            gCW.avgNoiseRSSI = (gCW.avgNoiseRSSI * 127 + rssi) / 128;
        }

        if (signalPresent) {
            if (!gCW.rxSignalOn) {
                // Signal started
                gCW.rxSignalOn = true;
                gCW.rxSignalTimer_10ms = 0;
                gCW.rxGapTimer_10ms = 0;
            } else {
                gCW.rxSignalTimer_10ms++;
            }
        } else {
            if (gCW.rxSignalOn) {
                // Signal stopped - classify element
                gCW.rxSignalOn = false;
                uint16_t ms = gCW.rxSignalTimer_10ms * 10;
                
                // Dynamic breakpoints based on current tracked speed
                uint16_t dotLen = gCW.avgDotMs;
                if (dotLen < 30) dotLen = 30; // Max ~40 WPM limit for stability
                
                uint16_t threshold = (dotLen * 3) / 2; // Breakpoint at 1.5x dot
                bool isDah = ms >= threshold;

                if (!isDah) {
                    // Fast adaptation for dots (alpha = 0.5)
                    gCW.avgDotMs = (gCW.avgDotMs + ms) / 2;
                } else if (ms < dotLen * 5) {
                    // Also adapt to dahs if reasonable (alpha = 0.5 for the 1/3 dash length)
                    gCW.avgDotMs = (gCW.avgDotMs + (ms / 3)) / 2;
                }
                
                // Clamp to reasonable WPM (5 to 50 WPM -> 240ms to 24ms)
                if (gCW.avgDotMs < 20) gCW.avgDotMs = 20;
                if (gCW.avgDotMs > 250) gCW.avgDotMs = 250;
                
                // Add to decoder
                if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE) {
                    gCW.decodeBuf[gCW.decodeCount++] = isDah ? 1 : 0;
                }
                
                // Add to live symbol buffer
                CW_AddSymbol(isDah ? '-' : '.');
                
                gCW.rxGapTimer_10ms = 0;
            } else {
                gCW.rxGapTimer_10ms++;
                uint16_t gapMs = gCW.rxGapTimer_10ms * 10;
                uint16_t dotLen = gCW.avgDotMs;

                // Dynamic thresholds: 2.5x dot for char gap, 5x dot for word gap
                // (Using slightly tighter tolerances than 3x/7x for better live feel)
                if (gapMs >= (dotLen * 5) / 2) { 
                    if (gCW.decodeCount > 0) {
                        CW_AddDecodedChar(CW_DecodeElements());
                        gCW.decodeCount = 0;
                    }
                }
                if (gapMs >= (dotLen * 5)) {
                    // Only add one space
                    if (gCW.textLen > 0 && gCW.textBuf[gCW.textLen-1] != ' ') {
                        CW_AddDecodedChar(' ');
                    }
                    // Reset gap timer so we don't spam spaces
                    gCW.rxGapTimer_10ms = 0; 
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // TX Processing
    // -------------------------------------------------------------------------

    // Update paddle latches
    CW_ProcessPaddles();

    // Check if we need to start TX
    if (gCW.state == CW_STATE_IDLE && (gCW.paddle.latchDit || gCW.paddle.latchDah || gCW.queueCount > 0)) {
        CW_StartTX();
        return;
    }
    
    // Process TX state machine
    switch (gCW.state) {
        case CW_STATE_TX_STARTING:
            if (gCW.timer_10ms > 0) {
                gCW.timer_10ms--;
            } else {
                gCW.state = CW_STATE_GAP; // Jump to gap to pick up first element
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
            
            // Gap logic moved to a shared block or IDLE below

            if (gCW.timer_10ms >= gCW.duration_10ms) {
                // Gap complete, decide next element (Iambic priority)
                bool playDit = false;
                bool playDah = false;
                static bool lastWasDit = false;

                if (gCW.paddle.latchDit && gCW.paddle.latchDah) {
                    if (lastWasDit) playDah = true; else playDit = true;
                } else if (gCW.paddle.latchDit) {
                    playDit = true;
                } else if (gCW.paddle.latchDah) {
                    playDah = true;
                } else {
                    // Check queue for programmed/straight
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
                    gCW.paddle.latchDit = false;
                    lastWasDit = true;
                    CW_ToneOn();
                    gCW.state = CW_STATE_PLAYING_TONE;
                    gCW.duration_10ms = CW_DOT_MS / 10;
                    gCW.timer_10ms = 0;
                    gCW.gapTimer_10ms = 0;
                    if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE)
                        gCW.decodeBuf[gCW.decodeCount++] = 0;
                    CW_AddSymbol('.');
                } else if (playDah) {
                    gCW.paddle.latchDah = false;
                    lastWasDit = false;
                    CW_ToneOn();
                    gCW.state = CW_STATE_PLAYING_TONE;
                    gCW.duration_10ms = CW_DASH_MS / 10;
                    gCW.timer_10ms = 0;
                    gCW.gapTimer_10ms = 0;
                    if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE)
                        gCW.decodeBuf[gCW.decodeCount++] = 1;
                    CW_AddSymbol('-');
                } else {
                    // Nothing to play, wait for hang timer in IDLE
                    gCW_HangTimer_10ms = 0;
                    gCW.state = CW_STATE_IDLE;
                }
            }
            break;
            
        case CW_STATE_IDLE:
            if (FUNCTION_IsTx()) {
                gCW.gapTimer_10ms++;
                uint16_t gapMs = gCW.gapTimer_10ms * 10;
                // For TX, use the standard CW_DOT_MS as base
                uint16_t dotLen = CW_DOT_MS;

                if (gapMs >= (dotLen * 25) / 10) { // 2.5x dot
                    if (gCW.decodeCount > 0) {
                        CW_AddDecodedChar(CW_DecodeElements());
                        gCW.decodeCount = 0;
                    }
                }
                if (gapMs >= (dotLen * 5)) { // 5x dot
                    if (gCW.textLen > 0 && gCW.textBuf[gCW.textLen-1] != ' ') {
                        CW_AddDecodedChar(' ');
                    }
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
            // Check for stop in queue
            if (gCW.queueCount > 0 && gCW.queue[gCW.queueHead] == CW_ELEM_STRAIGHT_STOP) {
                // Ensure minimum duration (one dot length) for auditability
                if (gCW.straightTimer_10ms * 10 < CW_DOT_MS) {
                    // Don't pop yet, wait until timer reaches CW_DOT_MS
                    if (gCW.straightTimer_10ms * 10 < CW_DOT_MS) {
                        return; 
                    }
                }

                CW_Element_t dummy;
                CW_QueuePop(&dummy);
                CW_ToneOff();
                // Record element based on duration
                uint16_t ms = gCW.straightTimer_10ms * 10;
                bool isDah = ms >= 150;
                if (gCW.decodeCount < CW_ELEMENT_BUF_SIZE)
                    gCW.decodeBuf[gCW.decodeCount++] = isDah ? 1 : 0;
                CW_AddSymbol(isDah ? '-' : '.');
                
                gCW.state = CW_STATE_GAP;
                gCW.timer_10ms = 0;
                gCW.duration_10ms = CW_ELEMENT_GAP_MS / 10;
                gCW.gapTimer_10ms = 0;
            }
            break;
            
        default:
            break;
    }
}

bool CW_IsBusy(void)
{
    return gCW.state != CW_STATE_IDLE || gCW.queueCount > 0;
}

const char* CW_GetDecodedText(void)
{
    return gCW.textBuf;
}

const char* CW_GetSymbolBuffer(void)
{
    return gCW.symbolBuf;
}

void CW_ClearDecoded(void)
{
    gCW.textLen = 0;
    gCW.textBuf[0] = '\0';
    gCW.symbolLen = 0;
    gCW.symbolBuf[0] = '\0';
}

#endif // ENABLE_CW_KEYER
