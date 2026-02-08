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

#include <string.h>
#include <stdlib.h>  // abs()

#include "apps/scanner/chFrScanner.h"
#include "features/dtmf.h"
#ifdef ENABLE_AM_FIX
    #include "am_fix.h"
#endif
#include "ui/bitmaps.h"
#include "board.h"
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "apps/battery/battery.h"
#include "core/misc.h"
#include "radio.h"
#include "apps/settings/settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"
#include "audio.h"

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    #include "drivers/bsp/system.h"
#endif

#ifdef ENABLE_CW_KEYER
    #include "features/cw.h"
#endif

center_line_t center_line = CENTER_LINE_NONE;

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    static int8_t RxBlink;
    static int8_t RxBlinkLed = 0;
    static int8_t RxBlinkLedCounter;
    static int8_t RxLine;
    static uint32_t RxOnVfofrequency;

    bool isMainOnlyInputDTMF = false;

    static bool isMainOnly()
    {
        return (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) && (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF);
    }
#endif

const int8_t dBmCorrTable[7] = {
            -15, // band 1
            -25, // band 2
            -20, // band 3
            -4, // band 4
            -7, // band 5
            -6, // band 6
             -1  // band 7
        };

const char *VfoStateStr[] = {
       [VFO_STATE_NORMAL]="",
       [VFO_STATE_BUSY]="BUSY",
       [VFO_STATE_BAT_LOW]="BAT LOW",
       [VFO_STATE_TX_DISABLE]="TX DISABLE",
       [VFO_STATE_TIMEOUT]="TIMEOUT",
       [VFO_STATE_ALARM]="ALARM",
       [VFO_STATE_VOLTAGE_HIGH]="VOLT HIGH"
};

// ***************************************************************************

static void DrawSmallAntennaAndBars(uint8_t *p, unsigned int level)
{
    if(level>6)
        level = 6;

    memcpy(p, BITMAP_Antenna, ARRAY_SIZE(BITMAP_Antenna));

    for(uint8_t i = 1; i <= level; i++) {
        char bar = (0xff << (6-i)) & 0x7F;
        memset(p + 2 + i*3, bar, 2);
    }
}
#if defined ENABLE_AUDIO_BAR || defined ENABLE_RSSI_BAR

static void DrawLevelBar(uint8_t xpos, uint8_t line, uint8_t level, uint8_t bars)
{
#ifndef ENABLE_CUSTOM_FIRMWARE_MODS
    const char hollowBar[] = {
        0b01111111,
        0b01000001,
        0b01000001,
        0b01111111
    };
#endif
    
    uint8_t *p_line = gFrameBuffer[line];
    level = MIN(level, bars);

    for(uint8_t i = 0; i < level; i++) {
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        if(gSetting_set_met)
        {
            const char hollowBar[] = {
                0b01111111,
                0b01000001,
                0b01000001,
                0b01111111
            };

            if(i < bars - 4) {
                for(uint8_t j = 0; j < 4; j++)
                    p_line[xpos + i * 5 + j] = (~(0x7F >> (i + 1))) & 0x7F;
            }
            else {
                memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
            }
        }
        else
        {
            const char hollowBar[] = {
                0b00111110,
                0b00100010,
                0b00100010,
                0b00111110
            };

            const char simpleBar[] = {
                0b00111110,
                0b00111110,
                0b00111110,
                0b00111110
            };

            if(i < bars - 4) {
                memcpy(p_line + (xpos + i * 5), &simpleBar, ARRAY_SIZE(simpleBar));
            }
            else {
                memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
            }
        }
#else
        if(i < bars - 4) {
            for(uint8_t j = 0; j < 4; j++)
                p_line[xpos + i * 5 + j] = (~(0x7F >> (i+1))) & 0x7F;
        }
        else {
            memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
        }
#endif
    }
}
#endif

#ifdef ENABLE_AUDIO_BAR

// Approximation of a logarithmic scale using integer arithmetic
uint8_t log2_approx(unsigned int value) {
    uint8_t log = 0;
    while (value >>= 1) {
        log++;
    }
    return log;
}

#ifdef ENABLE_CW_KEYER
void UI_DisplayCW(uint8_t line)
{
    const char *decoded = CW_GetDecodedText();
    const char *symbols = CW_GetSymbolBuffer();
    bool showCursor = (gFlashLightBlinkCounter % 40) < 20;
    
    // Clear line buffer
    memset(gFrameBuffer[line], 0, LCD_WIDTH);

    int symLen = symbols[0] ? strlen(symbols) : 0;
    // Width of ">" + symbols in Smallest font (4px per char)
    int symWidth = symLen ? (symLen + 1) * 4 : 0;
    
    // Available width for decoded text: 128 - 4(left) - 4(right) - symbolsWidth - 8(cursor)
    int maxDecWidth = 120 - symWidth - 8;
    int maxDecChars = maxDecWidth / 6;
    if (maxDecChars < 0) maxDecChars = 0;

    int decLen = decoded ? strlen(decoded) : 0;
    const char *decStart = (decLen > maxDecChars) ? (decoded + decLen - maxDecChars) : decoded;
    
    // 1. Decoded text (SmallNormal)
    UI_PrintStringSmallNormal(decStart, 4, 0, line);
    
    int decLenSeen = strlen(decStart);
    uint8_t cursorX = 4 + (decLenSeen * 6);
    
    // 2. Block cursor (SmallNormal)
    if (showCursor) {
        UI_PrintStringSmallNormal("\x7F", cursorX, 0, line);
    }

    // 3. Current symbols (Smallest font)
    if (symLen) {
        char prompt[16];
        snprintf(prompt, sizeof(prompt), " %s", symbols);
        // Right-aligned with 4px padding
        UI_PrintStringSmallest(prompt, 128 - 4 - symWidth, line * 8 + 1, false, true);
    }
    
    ST7565_BlitLine(line);
}
#endif

void UI_DisplayAudioBar(void)
{
    if (gSetting_mic_bar)
    {
        if(gLowBattery && !gLowBatteryConfirmed)
            return;

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        RxBlinkLed = 0;
        RxBlinkLedCounter = 0;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        unsigned int line;
        if (isMainOnly())
        {
            line = 5;
        }
        else
        {
            line = 3;
        }
#else
        const unsigned int line = 3;
#endif

        if (gCurrentFunction != FUNCTION_TRANSMIT ||
            gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
            || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
            )
        {
            return;  // screen is in use
        }

#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        if (gAlarmState != ALARM_STATE_OFF)
            return;
#endif

        uint8_t *p_line = gFrameBuffer[line];
        memset(p_line, 0, LCD_WIDTH);

#ifdef ENABLE_CW_KEYER
        if (gTxVfo->Modulation == MODULATION_CW) {
            UI_DisplayCW(line);
            return;
        }
#endif

        // Matoz-style Mic Bar logic corrected
        // Read register 0x6F for AF level
        // Range approx 26 (noise) to 127 (max)
        uint8_t afDB = BK4819_ReadRegister(0x6F) & 0b1111111;
        
        // Calculate constrained pixel width (Max 81px to stop at X=105, text at 109)
        // ConvertDomain(val, min, max, out_min, out_max)
        uint8_t afPX = ConvertDomain(afDB, 26, 127, 0, 81);

        const uint8_t BAR_LEFT_MARGIN = 24;
        
        for (int i = 0; i < afPX; i += 4) {
            uint8_t x = BAR_LEFT_MARGIN + i;
            if (x + 3 > 105) break; // Safety stop before text area

            p_line[x] = p_line[x + 2] = 0b00111110;
            p_line[x + 1] = 0b00111110; // Solid bar for visibility
        }

        // Display dB value
        char String[16];
        sprintf(String, "%d dB", afDB);
        UI_PrintStringSmallest(String, 109, line * 8 + 1, false, true);

        if (gCurrentFunction == FUNCTION_TRANSMIT)
            ST7565_BlitFullScreen();
    }
}
#endif


// RSSI helpers for Matoz style bar
static const uint8_t U8RssiMap[] = { 121, 115, 109, 103, 97, 91, 85, 79, 73, 63 };

static uint8_t DBm2S(int dbm) {
    uint8_t i = 0;
    dbm *= -1;
    for (i = 0; i < ARRAY_SIZE(U8RssiMap); i++) {
        if (dbm >= U8RssiMap[i]) return i;
    }
    return i;
}

static int Rssi2DBm(uint16_t rssi) {
    return (rssi / 2) - 160 + dBmCorrTable[gRxVfo->Band];
}

void DisplayRSSIBar(const bool now)
{
#if defined(ENABLE_RSSI_BAR)
    const unsigned int line = 3;
    uint8_t *p_line = gFrameBuffer[line];
    char str[16];

    // Clear the line
    memset(p_line, 0, 128);

    // Get RSSI and convert
    int16_t rssi = BK4819_GetRSSI();
    int dBm = Rssi2DBm(rssi);
    uint8_t s = DBm2S(dBm);

    // Draw Bar (Matoz style)
    const uint8_t BAR_LEFT_MARGIN = 24;
    for (int i = BAR_LEFT_MARGIN, sv = 1; i < BAR_LEFT_MARGIN + s * 4; i += 4, sv++) {
        if (i + 3 < 128) {
            p_line[i] = p_line[i + 2] = 0b00111110;
            p_line[i + 1] = (sv > 9) ? 0b00100010 : 0b00111110;
        }
    }

    // Draw dBm text (Tiny) at X=110
    sprintf(str, "%d %s", dBm, "dBm");
    UI_PrintStringSmallest(str, 100, line * 8 + 1, false, true);

    // Draw S-unit text (Tiny) at X=3
    if (s < 10) {
        sprintf(str, "S%u", s);
    } else {
        sprintf(str, "S9+%u0", s - 9);
    }
    UI_PrintStringSmallest(str, 3, line * 8 + 1, false, true);

    if (now) ST7565_BlitLine(line);
#endif
}



#ifdef ENABLE_AGC_SHOW_DATA
void UI_MAIN_PrintAGC(bool now)
{
    char buf[20];
    memset(gFrameBuffer[3], 0, 128);
    union {
        struct {
            uint16_t _ : 5;
            uint16_t agcSigStrength : 7;
            int16_t gainIdx : 3;
            uint16_t agcEnab : 1;
        };
        uint16_t __raw;
    } reg7e;
    reg7e.__raw = BK4819_ReadRegister(0x7E);
    uint8_t gainAddr = reg7e.gainIdx < 0 ? 0x14 : 0x10 + reg7e.gainIdx;
    union {
        struct {
            uint16_t pga:3;
            uint16_t mixer:2;
            uint16_t lna:3;
            uint16_t lnaS:2;
        };
        uint16_t __raw;
    } agcGainReg;
    agcGainReg.__raw = BK4819_ReadRegister(gainAddr);
    int8_t lnaShortTab[] = {-28, -24, -19, 0};
    int8_t lnaTab[] = {-24, -19, -14, -9, -6, -4, -2, 0};
    int8_t mixerTab[] = {-8, -6, -3, 0};
    int8_t pgaTab[] = {-33, -27, -21, -15, -9, -6, -3, 0};
    int16_t agcGain = lnaShortTab[agcGainReg.lnaS] + lnaTab[agcGainReg.lna] + mixerTab[agcGainReg.mixer] + pgaTab[agcGainReg.pga];

    sprintf(buf, "%d%2d %2d %2d %3d", reg7e.agcEnab, reg7e.gainIdx, -agcGain, reg7e.agcSigStrength, BK4819_GetRSSI());
    UI_PrintStringSmallNormal(buf, 2, 0, 3);
    if(now)
        ST7565_BlitLine(3);
}
#endif

void UI_MAIN_TimeSlice500ms(void)
{
    if(gScreenToDisplay==DISPLAY_MAIN) {
#ifdef ENABLE_AGC_SHOW_DATA
        UI_MAIN_PrintAGC(true);
        return;
#endif

        if(FUNCTION_IsRx()) {
            DisplayRSSIBar(true);
        }
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS // Blink Green Led for white...
        else if(gSetting_set_eot > 0 && RxBlinkLed == 2)
        {
            if(RxBlinkLedCounter <= 8)
            {
                if(RxBlinkLedCounter % 2 == 0)
                {
                    if(gSetting_set_eot > 1 )
                    {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    }
                }
                else
                {
                    if(gSetting_set_eot > 1 )
                    {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                    }

                    if(gSetting_set_eot == 1 || gSetting_set_eot == 3)
                    {
                        switch(RxBlinkLedCounter)
                        {
                            case 1:
                            AUDIO_PlayBeep(BEEP_400HZ_30MS);
                            break;

                            case 3:
                            AUDIO_PlayBeep(BEEP_400HZ_30MS);
                            break;

                            case 5:
                            AUDIO_PlayBeep(BEEP_500HZ_30MS);
                            break;

                            case 7:
                            AUDIO_PlayBeep(BEEP_600HZ_30MS);
                            break;
                        }
                    }
                }
                RxBlinkLedCounter += 1;
            }
            else
            {
                RxBlinkLed = 0;
            }
        }
#endif
    }
}

// ***************************************************************************

void UI_DisplayMain(void)
{
    char               String[22];

    center_line = CENTER_LINE_NONE;
#ifdef ENABLE_CW_KEYER
    if (gTxVfo->Modulation == MODULATION_CW || gRxVfo->Modulation == MODULATION_CW) {
        center_line = CENTER_LINE_CW;
    }
#endif

    // clear the screen
    UI_DisplayClear();

    if(gLowBattery && !gLowBatteryConfirmed) {
        UI_DisplayPopup("LOW BATTERY");
        ST7565_BlitFullScreen();
        return;
    }

#ifndef ENABLE_CUSTOM_FIRMWARE_MODS
    if (gEeprom.KEY_LOCK && gKeypadLocked > 0)
    {   // tell user how to unlock the keyboard
        UI_PrintString("Long press #", 0, LCD_WIDTH, 1, 8);
        UI_PrintString("to unlock",    0, LCD_WIDTH, 3, 8);
        ST7565_BlitFullScreen();
        return;
    }
#else
    if (gEeprom.KEY_LOCK && gKeypadLocked > 0)
    {   // tell user how to unlock the keyboard
        uint8_t shift = 3;

        /*
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
        SYSTEM_DelayMs(50);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        SYSTEM_DelayMs(50);
        */

        if(isMainOnly())
        {
            shift = 5;
        }
        //memcpy(gFrameBuffer[shift] + 2, gFontKeyLock, sizeof(gFontKeyLock));
        UI_PrintStringSmallBold("UNLOCK KEYBOARD", 12, 0, shift);
        //memcpy(gFrameBuffer[shift] + 120, gFontKeyLock, sizeof(gFontKeyLock));

        /*
        for (uint8_t i = 12; i < 116; i++)
        {
            gFrameBuffer[shift][i] ^= 0xFF;
        }
        */
    }
#endif

    unsigned int activeTxVFO = gRxVfoIsActive ? gEeprom.RX_VFO : gEeprom.TX_VFO;

    for (unsigned int vfo_num = 0; vfo_num < 2; vfo_num++)
    {
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        const unsigned int line0 = 0;  // text screen line
        const unsigned int line1 = 4;
        unsigned int line;
        if (isMainOnly())
        {
            line       = 0;
        }
        else
        {
            line       = (vfo_num == 0) ? line0 : line1;
        }
        const bool         isMainVFO  = (vfo_num == gEeprom.TX_VFO);
        uint8_t           *p_line0    = gFrameBuffer[line + 0];
        uint8_t           *p_line1    = gFrameBuffer[line + 1];

        enum Vfo_txtr_mode mode       = VFO_MODE_NONE;      
        char              attrStr[32]; // Buffer for concatenated attributes
        attrStr[0] = 0;                // Initialize empty
#else
        const unsigned int line0 = 0;  // text screen line
        const unsigned int line1 = 4;
        const unsigned int line       = (vfo_num == 0) ? line0 : line1;
        const bool         isMainVFO  = (vfo_num == gEeprom.TX_VFO);
        uint8_t           *p_line0    = gFrameBuffer[line + 0];
        uint8_t           *p_line1    = gFrameBuffer[line + 1];
        enum Vfo_txtr_mode mode       = VFO_MODE_NONE;
#endif

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    if (isMainOnly())
    {
        if (activeTxVFO != vfo_num)
        {
            continue;
        }
    }
#endif

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        if (activeTxVFO != vfo_num || isMainOnly())
#else
        if (activeTxVFO != vfo_num) // this is not active TX VFO
#endif
        {
#ifdef ENABLE_SCAN_RANGES
            if(gScanRangeStart) {

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
                //if(IS_FREQ_CHANNEL(gEeprom.ScreenChannel[0]) && IS_FREQ_CHANNEL(gEeprom.ScreenChannel[1])) {
                if(IS_FREQ_CHANNEL(gEeprom.ScreenChannel[activeTxVFO])) {

                    uint8_t shift = 0;

                    if (isMainOnly())
                    {
                        shift = 3;
                    }

                    UI_PrintString("ScnRng", 5, 0, line + shift, 8);
                    sprintf(String, "%3u.%05u", gScanRangeStart / 100000, gScanRangeStart % 100000);
                    UI_PrintStringSmallNormal(String, 56, 0, line + shift);
                    sprintf(String, "%3u.%05u", gScanRangeStop / 100000, gScanRangeStop % 100000);
                    UI_PrintStringSmallNormal(String, 56, 0, line + shift + 1);

                    if (!isMainOnly())
                        continue;
                }
                else
                {
                    gScanRangeStart = 0;
                }
#else
                UI_PrintString("ScnRng", 5, 0, line, 8);
                sprintf(String, "%3u.%05u", gScanRangeStart / 100000, gScanRangeStart % 100000);
                UI_PrintStringSmallNormal(String, 56, 0, line);
                sprintf(String, "%3u.%05u", gScanRangeStop / 100000, gScanRangeStop % 100000);
                UI_PrintStringSmallNormal(String, 56, 0, line + 1);
                continue;
#endif
            }
#endif


            if (gDTMF_InputMode
#ifdef ENABLE_DTMF_CALLING
                || gDTMF_CallState != DTMF_CALL_STATE_NONE || gDTMF_IsTx
#endif
            ) {
                char *pPrintStr = "";
                // show DTMF stuff
#ifdef ENABLE_DTMF_CALLING
                char Contact[16];
                if (!gDTMF_InputMode) {
                    if (gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT) {
                        pPrintStr = DTMF_FindContact(gDTMF_String, Contact) ? Contact : gDTMF_String;
                    } else if (gDTMF_CallState == DTMF_CALL_STATE_RECEIVED || gDTMF_CallState == DTMF_CALL_STATE_RECEIVED_STAY){
                        pPrintStr = DTMF_FindContact(gDTMF_Callee, Contact) ? Contact : gDTMF_Callee;
                    }else if (gDTMF_IsTx) {
                        pPrintStr = gDTMF_String;
                    }
                }

                UI_PrintString(pPrintStr, 2, 0, 2 + (vfo_num * 3), 8);

                pPrintStr = "";
                if (!gDTMF_InputMode) {
                    if (gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT) {
                        pPrintStr = (gDTMF_State == DTMF_STATE_CALL_OUT_RSP) ? "CALL OUT(RSP)" : "CALL OUT";
                    } else if (gDTMF_CallState == DTMF_CALL_STATE_RECEIVED || gDTMF_CallState == DTMF_CALL_STATE_RECEIVED_STAY) {
                        sprintf(String, "CALL FRM:%s", (DTMF_FindContact(gDTMF_Caller, Contact)) ? Contact : gDTMF_Caller);
                        pPrintStr = String;
                    } else if (gDTMF_IsTx) {
                        pPrintStr = (gDTMF_State == DTMF_STATE_TX_SUCC) ? "DTMF TX(SUCC)" : "DTMF TX";
                    }
                }
                else
#endif
                {
                    sprintf(String, ">%s", gDTMF_InputBox);
                    pPrintStr = String;
                }

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
                if (isMainOnly())
                {
                    UI_PrintString(pPrintStr, 2, 0, 5, 8);
                    isMainOnlyInputDTMF = true;
                    center_line = CENTER_LINE_IN_USE;
                }
                else
                {
                    UI_PrintString(pPrintStr, 2, 0, 0 + (vfo_num * 3), 8);
                    isMainOnlyInputDTMF = false;
                    center_line = CENTER_LINE_IN_USE;
                    continue;
                }
#else
                UI_PrintString(pPrintStr, 2, 0, 0 + (vfo_num * 3), 8);
                center_line = CENTER_LINE_IN_USE;
                continue;
#endif
            }

            // Matoz-style VFO indicator box (19px wide)
            if (isMainVFO) {
                // Selected VFO: solid 19px box
                memset(p_line0, 127, 19);
            }
        }
        else // active TX VFO
        {   // highlight the selected/used VFO with a marker
            // Matoz-style VFO indicator box (19px wide)
            if (isMainVFO) {
                // Selected VFO: solid 19px box
                memset(p_line0, 127, 19);
            } else if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
                // Not selected but dual-watch active: hollow rectangle with corners
                p_line0[0] = 0b01111111;
                p_line0[1] = 0b01000001;
                p_line0[17] = 0b01000001;
                p_line0[18] = 0b01111111;
            }
        }

        uint32_t frequency = gEeprom.VfoInfo[vfo_num].pRX->Frequency;

        if(TX_freq_check(frequency) != 0 && gEeprom.VfoInfo[vfo_num].TX_LOCK == true)
        {
            if(isMainOnly())
                memcpy(p_line0 + 14, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
            else
                memcpy(p_line0 + 24, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
        }

        if (gCurrentFunction == FUNCTION_TRANSMIT)
        {   // transmitting

#ifdef ENABLE_ALARM
            if (gAlarmState == ALARM_STATE_SITE_ALARM)
                mode = VFO_MODE_RX;
            else
#endif
            {
                if (activeTxVFO == vfo_num)
                {   // show the TX symbol
                    mode = VFO_MODE_TX;
                    UI_PrintStringSmallBold("TX", 0, 0, line + 1);
                }
            }
        }
        else
        {   // receiving .. show the RX symbol
            mode = VFO_MODE_RX;
            //if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num) {
            if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num && VfoState[vfo_num] == VFO_STATE_NORMAL) {
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
                RxBlinkLed = 1;
                RxBlinkLedCounter = 0;
                RxLine = line;
                RxOnVfofrequency = frequency;
                if(!isMainVFO)
                {
                    RxBlink = 1;
                }
                else
                {
                    RxBlink = 0;
                }
                // Matoz-style RX indicator at line+1, X=0 (removed old RSSI-based RX display)
                UI_PrintStringSmallBold("RX", 0, 0, line + 1);
#else
                UI_PrintStringSmallBold("RX", 0, 0, line + 1);
#endif
            }
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
            else
            {
                if(RxOnVfofrequency == frequency && !isMainOnly())
                {
                    //UI_PrintStringSmallNormal(">>", 8, 0, line);
                    //memcpy(p_line0 + 14, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
                }

                if(RxBlinkLed == 1)
                    RxBlinkLed = 2;
            }
#endif
        }

        // Matoz-style channel label inside indicator box
        bool filled = isMainVFO;  // solid box means inverted text
        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
        {   // channel mode - show M001, M002, etc.
            const bool inputting = gInputBoxIndex != 0 && gEeprom.TX_VFO == vfo_num;
            if (!inputting)
                sprintf(String, "M%03u", gEeprom.ScreenChannel[vfo_num] + 1);
            else
                sprintf(String, "M%.3s", INPUTBOX_GetAscii());
            UI_PrintStringSmallest(String, 2, line * 8 + 1, false, !filled);
        }
        else if (IS_FREQ_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
        {   // frequency mode - show VFO1, VFO2, etc.
            sprintf(String, "VFO%u", 1 + gEeprom.ScreenChannel[vfo_num] - FREQ_CHANNEL_FIRST);
            UI_PrintStringSmallest(String, 2, line * 8 + 1, false, !filled);
        }
#ifdef ENABLE_NOAA
        else
        {   // NOAA channel
            if (gInputBoxIndex == 0 || gEeprom.TX_VFO != vfo_num)
                sprintf(String, "N%u", 1 + gEeprom.ScreenChannel[vfo_num] - NOAA_CHANNEL_FIRST);
            else
                sprintf(String, "N%u%u", '0' + gInputBox[0], '0' + gInputBox[1]);
            UI_PrintStringSmallest(String, 4, line * 8 + 1, false, !filled);
        }
#endif

        // ************

        enum VfoState_t state = VfoState[vfo_num];

#ifdef ENABLE_ALARM
        if (gCurrentFunction == FUNCTION_TRANSMIT && gAlarmState == ALARM_STATE_SITE_ALARM) {
            if (activeTxVFO == vfo_num)
                state = VFO_STATE_ALARM;
        }
#endif
        if (state != VFO_STATE_NORMAL)
        {
            if (state < ARRAY_SIZE(VfoStateStr))
                UI_PrintString(VfoStateStr[state], 31, 0, line, 8);
        }
        else if (gInputBoxIndex > 0 && IS_FREQ_CHANNEL(gEeprom.ScreenChannel[vfo_num]) && gEeprom.TX_VFO == vfo_num)
        {   // user entering a frequency
            const char * ascii = INPUTBOX_GetAscii();
            bool isGigaF = frequency>=_1GHz_in_KHz;
            sprintf(String, "%.*s.%.3s", 3 + isGigaF, ascii, ascii + 3 + isGigaF);
#ifdef ENABLE_BIG_FREQ
            if(!isGigaF) {
                // show the remaining 2 small frequency digits
                UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                String[7] = 0;
                // show the main large frequency digits
                UI_DisplayFrequencyStr(String, 32, line, false);
            }
            else
#endif
            {
                // show the frequency in the main font
                UI_PrintString(String, 32, 0, line, 8);
            }

            continue;
        }
        else
        {
            if (gCurrentFunction == FUNCTION_TRANSMIT)
            {   // transmitting
                if (activeTxVFO == vfo_num)
                    frequency = gEeprom.VfoInfo[vfo_num].pTX->Frequency;
            }

            if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
            {   // it's a channel

                #ifdef ENABLE_RESCUE_OPERATIONS
                    if(gEeprom.MENU_LOCK == false) {
                #endif

                if(gMR_ChannelExclude[gEeprom.ScreenChannel[vfo_num]] == false)
                {
                    // Scanlist text indicators
                    const ChannelAttributes_t att = gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];
                    
                    if (att.scanlist1) strcat(attrStr, "1");
                    if (att.scanlist2) strcat(attrStr, "2");
                    if (att.scanlist3) strcat(attrStr, "3");
                }
                else
                {
                    strcat(attrStr, "X");
                }

                #ifdef ENABLE_RESCUE_OPERATIONS
                {
                    }
                }
                #endif

                // compander symbol
#ifndef ENABLE_BIG_FREQ
                if (att.compander)
                    memcpy(p_line0 + 120 + LCD_WIDTH, BITMAP_compand, sizeof(BITMAP_compand));
#else
                // TODO:  // find somewhere else to put the symbol
#endif

                switch (gEeprom.CHANNEL_DISPLAY_MODE)
                {
                    case MDF_FREQUENCY: // show the channel frequency
                        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
#ifdef ENABLE_BIG_FREQ
                        if(frequency < _1GHz_in_KHz) {
                            // show the remaining 2 small frequency digits
                            UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                            String[7] = 0;
                            // show the main large frequency digits
                            UI_DisplayFrequencyStr(String, 32, line, false);
                        }
                        else
#endif
                        {
                            // show the frequency in the main font
                            UI_PrintString(String, 32, 0, line, 8);
                        }

                        break;

                    case MDF_CHANNEL:   // show the channel number
                        sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
                        UI_PrintString(String, 32, 0, line, 8);
                        break;

                    case MDF_NAME:      // show the channel name
                    case MDF_NAME_FREQ: // show the channel name and frequency

                        SETTINGS_FetchChannelName(String, gEeprom.ScreenChannel[vfo_num]);
                        if (String[0] == 0)
                        {   // no channel name, show the channel number instead
                            sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
                        }

                        if (gEeprom.CHANNEL_DISPLAY_MODE == MDF_NAME) {
                            UI_PrintString(String, 32, 0, line, 8);
                        }
                        else {
                            // MDF_NAME_FREQ
                            // Matoz Style: Name (SmallBold) + Freq (SmallNormal) both at X=39
                            
                            // Name - Small Bold Font
                             UI_PrintStringSmallBold(String, 39, 0, line);
                         
                            // Frequency - Small Normal Font
                            sprintf(String, "%03u.%05u", frequency / 100000, frequency % 100000);
                            UI_PrintStringSmallNormal(String, 39, 0, line + 1);
                        }
                        break;
                }
            }
            else
            {   // frequency mode
                sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);

#ifdef ENABLE_BIG_FREQ
                if(frequency < _1GHz_in_KHz) {
                    // show the remaining 2 small frequency digits
                    UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                    String[7] = 0;
                    // show the main large frequency digits
                UI_DisplayFrequencyStr(String, 32, line, false);
                }
                else
#endif
                {
                    // show the frequency in the main font
                    UI_PrintString(String, 32, 0, line, 8);
                }

                // show the channel symbols
                const ChannelAttributes_t att = gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];
                if (att.compander)
#ifdef ENABLE_BIG_FREQ
                    memcpy(p_line0 + 120, BITMAP_compand, sizeof(BITMAP_compand));
#else
                    memcpy(p_line0 + 120 + LCD_WIDTH, BITMAP_compand, sizeof(BITMAP_compand));
#endif
            }
        }

        // ************

        {   // show the TX/RX level
            int8_t Level = -1;

            if (mode == VFO_MODE_TX)
            {   // TX power level
                /*
                switch (gRxVfo->OUTPUT_POWER)
                {
                    case OUTPUT_POWER_LOW1:     Level = 2; break;
                    case OUTPUT_POWER_LOW2:     Level = 2; break;
                    case OUTPUT_POWER_LOW3:     Level = 2; break;
                    case OUTPUT_POWER_LOW4:     Level = 2; break;
                    case OUTPUT_POWER_LOW5:     Level = 2; break;
                    case OUTPUT_POWER_MID:      Level = 4; break;
                    case OUTPUT_POWER_HIGH:     Level = 6; break;
                }

                if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_MID) {
                    Level = 4;
                } else if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_HIGH) {
                    Level = 6;
                } else {
                    Level = 2;
                }
                */
                Level = gRxVfo->OUTPUT_POWER - 1;
            }
            else
            if (mode == VFO_MODE_RX)
            {   // RX signal level
                #ifndef ENABLE_RSSI_BAR
                    // bar graph
                    if (gVFO_RSSI_bar_level[vfo_num] > 0)
                        Level = gVFO_RSSI_bar_level[vfo_num];
                #endif
            }
            //if(Level >= 0)
                //DrawSmallAntennaAndBars(p_line1 + LCD_WIDTH, Level);
        }

        // ************

        // Consolidated Attribute Line (Matoz Style + ScanList Text)
        // Order: ScanList, SCR, DTMF, R, Offset, BW, Power, DCS/CTCSS
        
        char AttrStr[64];
        memset(AttrStr, 0, sizeof(AttrStr));
        const VFO_Info_t *vfoInfo = &gEeprom.VfoInfo[vfo_num];

        // 1. Scan List (S1, S2, S12)
        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num])) {
            const ChannelAttributes_t att = gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];
            if (att.scanlist1 && att.scanlist2) {
                strcat(AttrStr, "S12 ");
            } else if (att.scanlist1) {
                strcat(AttrStr, "S1 ");
            } else if (att.scanlist2) {
                strcat(AttrStr, "S2 ");
            }
        }

        // 2. Scramble
        if (vfoInfo->SCRAMBLING_TYPE > 0 && gSetting_ScrambleEnable) {
            strcat(AttrStr, "SCR ");
        }

        // 3. DTMF
        #ifdef ENABLE_DTMF_CALLING
        if (vfoInfo->DTMF_DECODING_ENABLE || gSetting_KILLED) {
            strcat(AttrStr, "DTMF ");
        }
        #endif

        // 4. Reverse
        if (vfoInfo->FrequencyReverse) {
            strcat(AttrStr, "R ");
        }

        // 5. Offset Direction (+, -, D)
        const char *dir_list[] = {"", "+", "-"}; // Standard
        if (vfoInfo->freq_config_RX.Frequency != vfoInfo->freq_config_TX.Frequency) {
            if (vfoInfo->TX_OFFSET_FREQUENCY_DIRECTION < 3)
                strcat(AttrStr, dir_list[vfoInfo->TX_OFFSET_FREQUENCY_DIRECTION]);
            strcat(AttrStr, " ");
        }

        // 6. Bandwidth (Matoz labels)
        // bwNames: 25k, 12.5k, 8.33k, 6.25k
        const char *bwNames[] = {"25k", "12.5k", "8.33k", "6.25k"};
        if (vfoInfo->CHANNEL_BANDWIDTH < 4) {
            strcat(AttrStr, bwNames[vfoInfo->CHANNEL_BANDWIDTH]);
            strcat(AttrStr, " ");
        }

        // 7. Power (Matoz labels)
        // powerNames: LOW, MID, HIGH
        const char *powerNames[] = {"LOW", "MID", "HIGH"};
        // Logic to handle custom user power levels if needed, or simple mapping
        uint8_t pwrIndex = vfoInfo->OUTPUT_POWER;
        if (pwrIndex > 2) pwrIndex = 2; // Safety cap
        strcat(AttrStr, powerNames[pwrIndex]);
        strcat(AttrStr, " ");

        // 8. DCS/CTCSS
        // dcsNames: "", "CT", "DCS", "DCS"
        const char *dcsNames[] = {"", "CT", "DCS", "DCS"};
        const FREQ_Config_t *pConfig = (gCurrentFunction == FUNCTION_TRANSMIT) ? vfoInfo->pTX : vfoInfo->pRX;
        if (pConfig->CodeType < 4 && pConfig->CodeType > 0) {
            strcat(AttrStr, dcsNames[pConfig->CodeType]);
            // Maybe append code value? User asked for "labels text", assumed simple "DCS" or specifics? 
            // Matoz printed just "DCS" or "CT" in line 271 logic (dcsNames[type]).
            // I'll stick to just keys for now to save space.
        }

        // Print Consolidated String
        // Y Position: (line + 2) * 8 + 1 (Attributes row)
        // X Position: Centered
        // Char width = 4px (3px font + 1px space)
        
        uint8_t len = strlen(AttrStr);
        uint8_t x_pos = (LCD_WIDTH - (len * 4)) / 2;
        if (x_pos > LCD_WIDTH) x_pos = 0; // Overflow check if string > 32 chars

        UI_PrintStringSmallest(AttrStr, x_pos, (line + 2) * 8 + 1, false, true);

        // Modulation (Top Right - Keep separate as per Matoz style)
        const char *modNames[] = {"FM", "AM", "USB", "BYP", "RAW", "DSB", "CW"};
        if (vfoInfo->Modulation < 7) {
             UI_PrintStringSmallest(modNames[vfoInfo->Modulation], 116, 2 + vfo_num * 32, false, true);
        }
#ifdef ENABLE_AGC_SHOW_DATA
    center_line = CENTER_LINE_IN_USE;
    UI_MAIN_PrintAGC(false);
#endif

    if (center_line == CENTER_LINE_NONE)
    {   // we're free to use the middle line

        const bool rx = FUNCTION_IsRx();

#ifdef ENABLE_AUDIO_BAR
        if (gSetting_mic_bar && gCurrentFunction == FUNCTION_TRANSMIT) {
            center_line = CENTER_LINE_AUDIO_BAR;
            UI_DisplayAudioBar();
        }
        else
#endif

#if defined(ENABLE_AM_FIX) && defined(ENABLE_AM_FIX_SHOW_DATA)
        if (rx && gEeprom.VfoInfo[gEeprom.RX_VFO].Modulation == MODULATION_AM && gSetting_AM_fix)
        {
            if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                )
                return;

            center_line = CENTER_LINE_AM_FIX_DATA;
            AM_fix_print_data(gEeprom.RX_VFO, String);
            UI_PrintStringSmallNormal(String, 2, 0, 3);
        }
        else
#endif

#ifdef ENABLE_RSSI_BAR
        if (rx) {
            center_line = CENTER_LINE_RSSI;
            DisplayRSSIBar(false);
        }
        else
#endif
        if (rx || gCurrentFunction == FUNCTION_FOREGROUND || gCurrentFunction == FUNCTION_POWER_SAVE)
        {
            #if 1
                if (gSetting_live_DTMF_decoder && gDTMF_RX_live[0] != 0)
                {   // show live DTMF decode
                    const unsigned int len = strlen(gDTMF_RX_live);
                    const unsigned int idx = (len > (17 - 5)) ? len - (17 - 5) : 0;  // limit to last 'n' chars

                    if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                        || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                        )
                        return;

                    center_line = CENTER_LINE_DTMF_DEC;

                    sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
                    if (isMainOnly())
                    {
                        UI_PrintStringSmallNormal(String, 2, 0, 5);
                    }
                    else
                    {
                        UI_PrintStringSmallNormal(String, 2, 0, 3);
                    }
#else
                    UI_PrintStringSmallNormal(String, 2, 0, 3);

#endif
                }
            #else
                if (gSetting_live_DTMF_decoder && gDTMF_RX_index > 0)
                {   // show live DTMF decode
                    const unsigned int len = gDTMF_RX_index;
                    const unsigned int idx = (len > (17 - 5)) ? len - (17 - 5) : 0;  // limit to last 'n' chars

                    if (gScreenToDisplay != DISPLAY_MAIN ||
                        gDTMF_CallState != DTMF_CALL_STATE_NONE)
                        return;

                    center_line = CENTER_LINE_DTMF_DEC;

                    sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
                    UI_PrintStringSmallNormal(String, 2, 0, 3);
                }
            #endif

#ifdef ENABLE_SHOW_CHARGE_LEVEL
            else if (gChargingWithTypeC)
            {   // charging .. show the battery state
                if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                    || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                    )
                    return;

                center_line = CENTER_LINE_CHARGE_DATA;

                sprintf(String, "Charge %u.%02uV %u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
                UI_PrintStringSmallNormal(String, 2, 0, 3);
            }
#endif
        }
    }
#ifdef ENABLE_CW_KEYER
    else if (center_line == CENTER_LINE_CW)
    {
        UI_DisplayCW(isMainOnly() ? 5 : 3);
    }
#endif

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    //#ifdef ENABLE_RESCUE_OPERATIONS
    //if(gEeprom.MENU_LOCK == false)
    //{
    //#endif
    if (isMainOnly() && !gDTMF_InputMode)
    {
        sprintf(String, "VFO %s", activeTxVFO ? "B" : "A");
        UI_PrintStringSmallBold(String, 92, 0, 6);
        for (uint8_t i = 92; i < 128; i++)
        {
            gFrameBuffer[6][i] ^= 0x7F;
        }
    }
    //#ifdef ENABLE_RESCUE_OPERATIONS
    //}
    //#endif
#endif

    ST7565_BlitFullScreen();
}
}

// ***************************************************************************
