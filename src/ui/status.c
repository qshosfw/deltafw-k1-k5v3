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

#include "apps/scanner/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "apps/fm/fm.h"
#endif
#include "apps/scanner/scanner.h"
#include "ui/bitmaps.h"
#include "drivers/bsp/keyboard.h"
#include "drivers/bsp/st7565.h"
#include "features/radio/functions.h"
#include "apps/battery/battery.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#include "apps/battery/battery_ui.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "ui/status.h"
#include "ui/ag_menu.h"
#include "apps/security/passcode.h"

static const char *gStatusTitleOverride = NULL;

void UI_SetStatusTitle(const char *title) {
    gStatusTitleOverride = title;
}

#ifdef ENABLE_RX_TX_TIMER_DISPLAY
#ifndef ENABLE_FIRMWARE_DEBUG_LOGGING
static void convertTime(uint8_t *line, uint8_t type) 
{
    uint16_t t = (type == 0) ? (gTxTimerCountdown_500ms / 2) : (3600 - gRxTimerCountdown_500ms / 2);

    uint8_t m = t / 60;
    uint8_t s = t - (m * 60); // Replace modulo with subtraction for efficiency

    gStatusLine[0] = gStatusLine[7] = gStatusLine[14] = 0x00; // Quick fix on display (on scanning I, II, etc.)

    char str[6];
    // sprintf(str, "%02u:%02u", m, s);
    NUMBER_ToDecimal(str, m, 2, true);
    str[2] = ':';
    NUMBER_ToDecimal(str + 3, s, 2, true);
    UI_PrintStringSmallBufferNormal(str, line);

    gUpdateStatus = true;
}
#endif
#endif


void UI_DisplayStatus() {
    gUpdateStatus = false;
    memset(gStatusLine, 0, sizeof(gStatusLine));

    char str[42] = "";
    char *p = str;

    if (gStatusTitleOverride) {
        strncpy(str, gStatusTitleOverride, sizeof(str)-1);
    } else if (AG_MENU_IsActive()) {
        AG_MENU_GetPath(str, sizeof(str));
#ifdef ENABLE_PASSCODE
    } else if (Passcode_IsLocked()) {
        strcpy(str, "Enter Passcode");
#endif
    } else {
#ifdef ENABLE_NOAA
    if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) { // NOAA SCAN indicator
        *p++ = 'N';
        *p++ = 'O';
        *p++ = 'A';
        *p++ = 'A';
        *p++ = ' ';
    }
    else if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        *p++ = 'S';
        *p++ = ' ';
    }
#else
    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        *p++ = 'S';
        *p++ = ' ';
    }
#endif

    if (gEeprom.KEY_LOCK) {
        *p++ = 'L';
        *p++ = ' ';
    }
    else if (gWasFKeyPressed) {
        // F-key will be drawn separately with inverted background
        // Skip adding to string, handled after UI_PrintStringSmallest
    }

#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH) {
        *p++ = 'V';
        *p++ = 'O';
        *p++ = 'X';
        *p++ = ' ';
    }
#endif

    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
         *p++ = 'D';
         *p++ = 'W';
         *p++ = ' ';
    } else if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) {
         *p++ = 'X';
         *p++ = 'B';
         *p++ = ' ';
    }
    
    *p = '\0';
    }

    if (gEeprom.RX_VFO == 0 && gEeprom.TX_VFO == 0) {
        // Main VFO A
    }
    UI_PrintStringSmallest(str, 0, 0, true, true);
    
    // Draw inverted F-key if pressed (white F on black background box)
    if (gWasFKeyPressed && !gEeprom.KEY_LOCK && !AG_MENU_IsActive()) {
        // Find position after any prior text (L, NOAA, S, etc.)
        uint8_t x = strlen(str) * 4;  // 4px per char in smallest font
        
        // Draw solid background box (5px wide x 6 rows tall)
        for (uint8_t i = 0; i < 5; i++) {
            gStatusLine[x + i] |= 0b00111111;  // Fill 6 rows (bits 0-5)
        }
        
        // Draw 'F' character from gFont3x5 inverted (XOR to make white on black)
        // gFont3x5 'F' = {0x1f, 0x05, 0x05} - offset (x+1) to center in 5px box
        gStatusLine[x + 1] ^= 0x1f;  // Column 0 of 'F'
        gStatusLine[x + 2] ^= 0x05;  // Column 1 of 'F'
        gStatusLine[x + 3] ^= 0x05;  // Column 2 of 'F'
    }

    // Battery Display
    // Modes:
    // 0: NONE
    // 1: VOLTAGE
    // 2: PERCENT
    // 3: ICON
    // 4: I+VOLT
    // 5: I+PERC
    // 6: VOLT (Duplicate?)
    // 7: PERC (Duplicate?)

    bool show_icon = (gSetting_battery_text >= 3 && gSetting_battery_text <= 5);
    bool show_volt = (gSetting_battery_text == 1 || gSetting_battery_text == 4 || gSetting_battery_text == 6);
    bool show_perc = (gSetting_battery_text == 2 || gSetting_battery_text == 5 || gSetting_battery_text == 7);

    // If icon is shown, it occupies 115-127 approx (13 px)
    // text should end at 113.
    // If no icon, text should end at 127.

    if (show_icon) {
        UI_DisplayBattery(gBatteryDisplayLevel, gLowBatteryBlink);
    }

    if (show_volt || show_perc) {
        char bat_str[10];
        if (show_volt) {
             const uint16_t voltage = (gBatteryVoltageAverage <= 999) ? gBatteryVoltageAverage : 999;
             // sprintf(bat_str, "%u.%02uV", voltage / 100, voltage % 100);
             NUMBER_ToDecimal(bat_str, voltage / 100, 1, false);
             bat_str[1] = '.';
             NUMBER_ToDecimal(bat_str + 2, voltage % 100, 2, true);
             strcat(bat_str, "V");
        } else { // show_perc
             // sprintf(bat_str, "%u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
             NUMBER_ToDecimal(bat_str, BATTERY_VoltsToPercent(gBatteryVoltageAverage), 3, false);
             strcat(bat_str, "%");
        }

        uint8_t str_len = strlen(bat_str);
        uint8_t str_width = str_len * 4;
        uint8_t x_pos = (show_icon) ? (115 - str_width - 2) : (128 - str_width - 1);
        
        UI_PrintStringSmallest(bat_str, x_pos, 0, true, true);
    }
    
    
    // Draw separator line at y=6 (Row 7)
    for (int i = 0; i < 128; i++) {
        gStatusLine[i] |= (1 << 6);
    }

    ST7565_BlitStatusLine();
}

