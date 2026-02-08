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

#ifndef CW_H
#define CW_H

#include <stdint.h>
#include <stdbool.h>

#ifdef ENABLE_CW_MOD_KEYER

// CW Timing Parameters (~15 WPM)
#define CW_TONE_FREQ_HZ         600     // Standard CW sidetone frequency
#define CW_DOT_MS               80      // Standard dot duration
#define CW_DASH_MS              240     // Standard dash = 3x dot
#define CW_ELEMENT_GAP_MS       80      // Gap between elements = 1 dot
#define CW_CHAR_GAP_MS          240     // Gap between characters = 3 dots
#define CW_WORD_GAP_MS          560     // Gap between words = 7 dots

#define CW_QUEUE_SIZE           32      // Element queue size
#define CW_DECODE_BUF_SIZE      22      // Decoded text buffer (fills one line)
#define CW_ELEMENT_BUF_SIZE     8       // Elements per character for decoder

// Element types in queue
typedef enum {
    CW_ELEM_DIT,
    CW_ELEM_DAH,
    CW_ELEM_STRAIGHT_START,     // Start straight key tone
    CW_ELEM_STRAIGHT_STOP       // Stop straight key tone
} CW_Element_t;

// Keyer state
typedef enum {
    CW_STATE_IDLE,              // Not transmitting
    CW_STATE_TX_STARTING,       // TX ramp-up delay
    CW_STATE_PLAYING_TONE,      // Tone currently playing
    CW_STATE_GAP,               // Inter-element gap
    CW_STATE_STRAIGHT_TONE      // Straight key tone (variable duration)
} CW_State_t;

// CW Module Context
typedef struct {
    CW_State_t      state;
    
    // Element queue
    CW_Element_t    queue[CW_QUEUE_SIZE];
    uint8_t         queueHead;
    uint8_t         queueTail;
    uint8_t         queueCount;
    
    // Current element timing
    uint16_t        timer_10ms;
    uint16_t        duration_10ms;
    
    // Paddle state (for iambic generation)
    struct {
        bool dit;
        bool dah;
        bool latchDit;      // Memory for fast taps
        bool latchDah;
        bool lastDit;       // For transition detection if needed
        bool lastDah;
    } paddle;
    
    // Straight key state
    bool            straightKeyDown;
    uint16_t        straightTimer_10ms;
    
    // Gap timing for decoder
    uint16_t        gapTimer_10ms;
    
    // Adaptive WPM tracking
    uint16_t        avgDotMs;
    uint16_t        rxDotMs;
    
    // Decoder element buffer
    uint8_t         decodeBuf[CW_ELEMENT_BUF_SIZE];
    uint8_t         decodeCount;
    
    // Decoded text output
    char            textBuf[CW_DECODE_BUF_SIZE + 1];
    uint8_t         textLen;
    
    // Live symbol buffer (for display of current . - sequence)
    char            symbolBuf[9];
    uint8_t         symbolLen;
    
    // RX Decode State
    bool            rxSignalOn;
    uint16_t        rxSignalTimer_10ms;
    uint16_t        rxGapTimer_10ms;
    uint16_t        avgNoiseRSSI;
} CW_Context_t;

// Main API
void CW_Init(void);
void CW_Tick10ms(void);         // Called every 10ms

// Key inputs (state based for iambic)
void CW_SetDitPaddle(bool pressed);
void CW_SetDahPaddle(bool pressed);
void CW_StraightKeyDown(void);
void CW_StraightKeyUp(void);

// Query
bool CW_IsBusy(void);           // Is there work to do?
const char* CW_GetDecodedText(void);
const char* CW_GetSymbolBuffer(void); // Get current symbol sequence
void CW_ClearDecoded(void);

#endif // ENABLE_CW_MOD_KEYER

#endif // CW_H
