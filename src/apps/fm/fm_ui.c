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
#include "ui/ui.h"

void UI_DisplayFM(void)
{
    char String[16] = {0};
    char *pPrintStr = String;
    UI_DisplayClear();

    UI_PrintString("FM", 2, 0, 0, 8);

    uint32_t lo = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    uint32_t hi = BK1080_GetFreqHiLimit(gEeprom.FM_Band);
    
    // Formatting manually or using helpers for lo-hi band display
    strcpy(String, "    . -    M");
    NUMBER_ToDecimal(String, lo / 10, 3, false);
    if (gEeprom.FM_Band == 0) String[4] = '5';
    NUMBER_ToDecimal(String + 6, hi / 10, 3, false);
    
    UI_PrintStringSmallNormal(String, 1, 0, 6);

    //uint8_t spacings[] = {20,10,5};
    //sprintf(String, "%d0k", spacings[gEeprom.FM_Space % 3]);
    //UI_PrintStringSmallNormal(String, 127 - 4*7, 0, 6);

    if (gAskToSave) {
        pPrintStr = "SAVE?";
    } else if (gAskToDelete) {
        pPrintStr = "DEL?";
    } else if (gFM_ScanState == FM_SCAN_OFF) {
        if (gEeprom.FM_IsMrMode) {
            strcpy(String, "MR(CH  )");
            NUMBER_ToDecimal(String + 5, gEeprom.FM_SelectedChannel + 1, 2, true);
            pPrintStr = String;
        } else {
            pPrintStr = "VFO";
            for (unsigned int i = 0; i < 20; i++) {
                if (gEeprom.FM_FrequencyPlaying == gFM_Channels[i]) {
                    strcpy(String, "VFO(CH  )");
                    NUMBER_ToDecimal(String + 6, i + 1, 2, true);
                    pPrintStr = String;
                    break;
                }
            }
        }
    } else if (gFM_AutoScan) {
        strcpy(String, "A-SCAN( )");
        NUMBER_ToDecimal(String + 7, gFM_ChannelPosition + 1, 1, true);
        pPrintStr = String;
    } else {
        pPrintStr = "M-SCAN";
    }

    UI_PrintString(pPrintStr, 0, 127, 3, 10); // memory, vfo, scan

    memset(String, 0, sizeof(String));
    if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
        UI_GenerateChannelString(String, gFM_ChannelPosition);
    } else if (gAskToDelete) {
        strcpy(String, "CH-  ");
        NUMBER_ToDecimal(String + 3, gEeprom.FM_SelectedChannel + 1, 2, true);
    } else {
        if (gInputBoxIndex == 0) {
            strcpy(String, "   . ");
            NUMBER_ToDecimal(String, gEeprom.FM_FrequencyPlaying / 10, 3, false);
            String[4] = (gEeprom.FM_FrequencyPlaying % 10) + '0';
        } else {
            const char * ascii = INPUTBOX_GetAscii();
            String[0] = ascii[0];
            String[1] = ascii[1];
            String[2] = ascii[2];
            String[3] = '.';
            String[4] = ascii[3];
            String[5] = '\0';
        }

        UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0, false);  // frequency
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintString(String, 0, 127, 1, 10);

    ST7565_BlitFullScreen();
}

#endif
