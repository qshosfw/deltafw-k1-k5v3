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

#include <assert.h>

#include "battery.h"
#include "drivers/bsp/backlight.h"
#include "drivers/bsp/st7565.h"
#include "functions.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#include "apps/battery/battery_ui.h"
#include "ui/menu.h"
#include "ui/ui.h"
//#include "debugging.h"

uint16_t          gBatteryCalibration[6];
uint16_t          gBatteryCurrentVoltage;
uint16_t          gBatteryCurrent;
uint16_t          gBatteryVoltages[4];
uint16_t          gBatteryVoltageAverage;
uint8_t           gBatteryDisplayLevel;
bool              gIsCharging;
bool              gLowBatteryBlink;
bool              gLowBattery;
bool              gLowBatteryConfirmed;
uint16_t          gBatteryCheckCounter;

typedef enum {
    BATTERY_LOW_INACTIVE,
    BATTERY_LOW_ACTIVE,
    BATTERY_LOW_CONFIRMED
} BatteryLow_t;

uint16_t          lowBatteryCountdown;
const uint16_t    lowBatteryPeriod = 30;

volatile uint16_t gPowerSave_10ms;

const uint16_t Voltage2PercentageTable[][7][2] = {
    [BATTERY_TYPE_1600_MAH] = {
        {828, 100},
        {814, 97 },
        {760, 25 },
        {729, 6  },
        {630, 0  },
        {0,   0  },
        {0,   0  },
    },

    [BATTERY_TYPE_2200_MAH] = {
        {832, 100},
        {813, 95 },
        {740, 60 },
        {707, 21 },
        {682, 5  },
        {630, 0  },
        {0,   0  },
    },

    [BATTERY_TYPE_3500_MAH] = {
        {837, 100},
        {826, 95 },
        {750, 50 },
        {700, 25 },
        {620, 5  },
        {600, 0  },
        {0,   0  },
    },

    // Estimated discharge curve for 1500 mAh K1 battery (improve this)
    [BATTERY_TYPE_1500_MAH] = {
        {828, 100},  // Fully charged (measured ~8.28V)
        {813, 97 },  // Top end
        {758, 25 },  // Mid level
        {726, 6  },  // Almost empty
        {630, 0  },  // Fully discharged (conservative)
        {0,   0  },
        {0,   0  },
    },

    // Estimated discharge curve for 2500 mAh K1 battery (improve this)
    [BATTERY_TYPE_2500_MAH] = {
        {839, 100},  // Fully charged (measured ~8.39V)
        {818, 95 },  // Top end (slightly raised vs 816)
        {745, 55 },  // Mid range
        {703, 25 },  // Low level
        {668, 5  },  // Almost empty
        {623, 0  },  // Fully discharged (between 630 and 600)
        {0,   0  },
    },
};

/* Useless (for compilator only)
static_assert(
    (ARRAY_SIZE(Voltage2PercentageTable[BATTERY_TYPE_1600_MAH]) ==
    ARRAY_SIZE(Voltage2PercentageTable[BATTERY_TYPE_2200_MAH])) &&
    (ARRAY_SIZE(Voltage2PercentageTable[BATTERY_TYPE_2200_MAH]) ==
    ARRAY_SIZE(Voltage2PercentageTable[BATTERY_TYPE_3500_MAH]))
    );
*/

unsigned int BATTERY_VoltsToPercent(const unsigned int voltage_10mV)
{
    const uint16_t (*crv)[2] = Voltage2PercentageTable[gEeprom.BATTERY_TYPE];
    const int mulipl = 1000;
    for (unsigned int i = 1; i < ARRAY_SIZE(Voltage2PercentageTable[BATTERY_TYPE_2200_MAH]); i++) {
        if (voltage_10mV > crv[i][0]) {
            const int a = (crv[i - 1][1] - crv[i][1]) * mulipl / (crv[i - 1][0] - crv[i][0]);
            const int b = crv[i][1] - a * crv[i][0] / mulipl;
            const int p = a * voltage_10mV / mulipl + b;
            return MIN(MAX(p, 0), 100);
        }
    }

    return 0;
}

void BATTERY_GetReadings(const bool bDisplayBatteryLevel)
{
    static uint16_t ChargingCounter = 0;
    static uint16_t BaseVoltage = 0;
    static uint16_t TxCooldown = 0;
    static uint16_t RecoveryCountdown = 0;
    
    const uint8_t  PreviousBatteryLevel = gBatteryDisplayLevel;
    const uint16_t RawVoltage           = (gBatteryVoltages[0] + gBatteryVoltages[1] + gBatteryVoltages[2] + gBatteryVoltages[3]) / 4;
    const uint16_t NewVoltage           = (RawVoltage * 760) / gBatteryCalibration[3];

    // --- Advanced Charging Detection Logic ---

    // 1. TX Protection: Pause detection AND voltage averaging during TX and cooldown
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        TxCooldown = 12; // ~6 seconds cooldown (assuming 500ms tick)
        RecoveryCountdown = 30; // 15 seconds recovery phase
        return;
    }
    if (TxCooldown > 0) {
        TxCooldown--;
        return;
    }

    // Exponential Moving Average (EMA) for smoothing
    // Alpha = 1/4
    if (gBatteryVoltageAverage == 0) gBatteryVoltageAverage = NewVoltage;
    gBatteryVoltageAverage = (gBatteryVoltageAverage * 3 + NewVoltage) / 4;

    // 2. Recovery Phase (Anti-Bounceback)
    // Force invalidation of charging detection while voltage is recovering relative to the floor
    if (RecoveryCountdown > 0) {
        RecoveryCountdown--;
        BaseVoltage = gBatteryVoltageAverage; // Force sync to ride the wave up
        ChargingCounter = 0; // Prevent triggering
        return; // Skip normal detection
    }

    // 3. Slope / Resting Floor Detection
    if (BaseVoltage == 0) BaseVoltage = gBatteryVoltageAverage;

    const uint16_t Threshold = 5; // 50mV - Higher sensitivity with EMA

    if(gBatteryVoltageAverage > 890)
        gBatteryDisplayLevel = 7; // battery overvoltage
    else if(gBatteryVoltageAverage < 630 && (gEeprom.BATTERY_TYPE == BATTERY_TYPE_1600_MAH || gEeprom.BATTERY_TYPE == BATTERY_TYPE_2200_MAH))
        gBatteryDisplayLevel = 0; // battery critical
    else if(gBatteryVoltageAverage < 600 && (gEeprom.BATTERY_TYPE == BATTERY_TYPE_3500_MAH))
        gBatteryDisplayLevel = 0; // battery critical
    else {
        gBatteryDisplayLevel = 1;
        const uint8_t levels[] = {5,17,41,65,88};
        uint8_t perc = BATTERY_VoltsToPercent(gBatteryVoltageAverage);

        for(uint8_t i = 6; i >= 2; i--){
            if (perc > levels[i-2]) {
                gBatteryDisplayLevel = i;
                break;
            }
        }
    }

    if ((gScreenToDisplay == DISPLAY_MENU) && UI_MENU_GetCurrentMenuId() == MENU_VOL)
        gUpdateDisplay = true;

    // 1. TX Protection: Pause detection during TX and for a cooldown period
    // This block was moved and modified above.

    // 2. Slope / Resting Floor Detection
    // This block was moved and modified above.

    if (!gIsCharging) {
        if (gBatteryVoltageAverage > (BaseVoltage + Threshold)) {
            if (ChargingCounter < 10) ChargingCounter++;
        } else {
            if (ChargingCounter > 0) ChargingCounter--;
            // Track floor downwards immediately
            if (gBatteryVoltageAverage < BaseVoltage) {
                BaseVoltage = gBatteryVoltageAverage;
            } else {
                 // Slowly track base upwards if we are steady but below threshold
                 // This handles slow natural voltage recovery without triggering charge
                 static uint8_t driftCounter = 0;
                 if (++driftCounter > 10) { // Every ~5 seconds
                     BaseVoltage++;
                     driftCounter = 0;
                 }
            }
        }

        if (ChargingCounter >= 4) { // ~2 seconds of sustained high voltage (> Base + 50mV)
            gIsCharging = true;
            gUpdateStatus = true;
            gUpdateDisplay = true;
            BACKLIGHT_TurnOn();
            BaseVoltage = gBatteryVoltageAverage; // Reset base to the new "charging floor"
        }
    } else {
        // We are already in "Charging" state.
        // Track the peak voltage seen while charging.
        if (gBatteryVoltageAverage > BaseVoltage) {
            BaseVoltage = gBatteryVoltageAverage;
        } else if (gBatteryVoltageAverage < (BaseVoltage - 7)) {
            // Sudden drop of 7 units (70mV) from the peak - unplugged!
            gIsCharging = false;
            ChargingCounter = 0;
            gUpdateStatus = true;
            gUpdateDisplay = true;
            BaseVoltage = gBatteryVoltageAverage;
        }
    }
    // --------------------------------

    if (PreviousBatteryLevel != gBatteryDisplayLevel)
    {
        if(gBatteryDisplayLevel > 2)
            gLowBatteryConfirmed = false;
        else if (gBatteryDisplayLevel < 2)
        {
            gLowBattery = true;
        }
        else
        {
            gLowBattery = false;

            if (bDisplayBatteryLevel)
                UI_DisplayBattery(gBatteryDisplayLevel, gLowBatteryBlink);
        }

        if(!gLowBatteryConfirmed)
            gUpdateDisplay = true;

        lowBatteryCountdown = 0;
    }
}

void BATTERY_TimeSlice500ms(void)
{
    if (!gLowBattery) {
        return;
    }

    gLowBatteryBlink = ++lowBatteryCountdown & 1;

    UI_DisplayBattery(0, gLowBatteryBlink);

    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        return;
    }

    // not transmitting

    if (lowBatteryCountdown < lowBatteryPeriod) {
        if (lowBatteryCountdown == lowBatteryPeriod-1 && !gIsCharging && !gLowBatteryConfirmed) {
            AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP);
        }
        return;
    }

    lowBatteryCountdown = 0;

    if (gIsCharging) {
        return;
    }

    // not on charge
    if (!gLowBatteryConfirmed) {
        AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP);
#ifdef ENABLE_VOICE
        AUDIO_SetVoiceID(0, VOICE_ID_LOW_VOLTAGE);
#endif
    }

    if (gBatteryDisplayLevel != 0) {
#ifdef ENABLE_VOICE
        AUDIO_PlaySingleVoice(false);
#endif
        return;
    }

#ifdef ENABLE_VOICE
    AUDIO_PlaySingleVoice(true);
#endif

    gReducedService = true;

    FUNCTION_Select(FUNCTION_POWER_SAVE);

    ST7565_HardwareReset();

    if (gEeprom.BACKLIGHT_TIME < 61) {
        BACKLIGHT_TurnOff();
    }
}
