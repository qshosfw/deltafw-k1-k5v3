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
    #include "drivers/bsp/bk1080.h"
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
static bool gStatusTitleCentered = false;

void UI_SetStatusTitle(const char *title) {
    gStatusTitleOverride = title;
    gStatusTitleCentered = false;
}

void UI_SetStatusTitleCentered(const char *title) {
    gStatusTitleOverride = title;
    gStatusTitleCentered = true;
}


#include "features/radio/radio.h"

static uint8_t UI_GetStatusSignalLevel(void) {
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        // Map power levels (LOW, MID, HIGH) to bars (1, 3, 5)
        if (gCurrentVfo->OUTPUT_POWER == 0) return 1;
        if (gCurrentVfo->OUTPUT_POWER == 1) return 3;
        return 5;
    }

    if (FUNCTION_IsRx()) {
        uint8_t bar = gVFO_RSSI_bar_level[gEeprom.RX_VFO];
        // Radio RSSI bars are 0-6. Map to 1-5.
        if (bar > 5) return 5;
        if (bar > 0) return bar;
    }

    return 0;
}

void UI_DisplayStatus() {
    gUpdateStatus = false;
    memset(gStatusLine, 0, sizeof(gStatusLine));

    uint8_t x_off = 0;
    bool simplified = AG_MENU_IsActive();

    // 1. Draw Universal Antenna & Bars
    if (!simplified) {
        uint8_t signal = UI_GetStatusSignalLevel();
        UI_DrawAntenna(gStatusLine, signal);
        x_off = 15; // Icon(13) + Gap(2)
    }

    // 2. Center Title (if any)
    if (gStatusTitleOverride) {
        if (gStatusTitleCentered) {
            uint8_t w = strlen(gStatusTitleOverride) * 4;
            UI_PrintStringSmallest(gStatusTitleOverride, (128 - w) / 2, 0, true, true);
        } else {
            UI_PrintStringSmallest(gStatusTitleOverride, x_off, 0, true, true);
            x_off += strlen(gStatusTitleOverride) * 4 + 2;
        }
    } else if (AG_MENU_IsActive()) {
        char path[32];
        AG_MENU_GetPath(path, sizeof(path));
        uint8_t w = strlen(path) * 4;
        
        // Use the title centering flag natively for menus as well, but default to left-aligned.
        if (gStatusTitleCentered) {
            UI_PrintStringSmallest(path, (128 - w) / 2, 0, true, true);
        } else {
            UI_PrintStringSmallest(path, x_off + 2, 0, true, true);
            x_off += w + 4;
        }
    }

    if (simplified) {
        // Skip all other indicators for simplified menu view
        goto skip_indicators;
    }

    // 3. Flowing Left Indicators
    // We only draw these if they don't overlap too much or if not Centered
    // But typically they should flow from left to right.
    
#ifdef ENABLE_NOAA
    if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) {
        UI_PrintStringSmallest("NOAA", x_off, 0, true, true);
        x_off += 18;
    }
#endif

    if (gEeprom.KEY_LOCK) {
        UI_PrintStringSmallest("LOCK", x_off, 0, true, true);
        x_off += 18;
    } else if (gWasFKeyPressed) {
        // Draw inverted F-key box
        for (uint8_t i = 0; i < 5; i++) gStatusLine[x_off + i] |= 0b00111111;
        gStatusLine[x_off + 1] ^= 0x1f;
        gStatusLine[x_off + 2] ^= 0x05;
        gStatusLine[x_off + 3] ^= 0x05;
        x_off += 7;
    }

#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH) {
        UI_PrintStringSmallest("VOX", x_off, 0, true, true);
        x_off += 14;
    }
#endif

    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
        UI_PrintStringSmallest("DW", x_off, 0, true, true);
        x_off += 10;
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
    
skip_indicators:
    // Draw separator line at y=6 (Row 7)
    for (int i = 0; i < 128; i++) {
        gStatusLine[i] |= (1 << 6);
    }

    ST7565_BlitStatusLine();
}

