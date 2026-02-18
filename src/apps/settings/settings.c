/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
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

#include "features/dtmf/dtmf.h"
#ifdef ENABLE_FMRADIO
    #include "apps/fm/fm.h"
#endif
#include "drivers/bsp/bk1080.h"
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/py25q16.h"
#include "features/storage/storage.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#include "ui/menu.h"

#ifdef ENABLE_RESET_CHANNEL_FUNCTION
static const uint32_t gDefaultFrequencyTable[] =
{
    14500000,    //
    14550000,    //
    43300000,    //
    43320000,    //
    43350000     //
};
#endif

EEPROM_Config_t gEeprom = { 0 };

static inline uint8_t LIMIT(uint8_t value, uint8_t limit, uint8_t defaultValue)
{
    return (value < limit) ? value : defaultValue;
}

static inline uint8_t RANGE(uint8_t value, uint8_t min, uint8_t max, uint8_t defaultValue)
{
    return (value >= min && value <= max) ? value : defaultValue;
}

static inline uint16_t PERIOD(uint8_t value_div10, uint16_t defaultValue)
{
    return (value_div10 < 101) ? (uint16_t)value_div10 * 10U : defaultValue;
}

void SETTINGS_InitEEPROM(void)
{
    SettingsMain_t mainConfig;
    SettingsExtra_t extraConfig;
    ScanList_t scanList;
    FLockConfig_t flockConfig;
    uint8_t Data[16];

    // REC_SETTINGS_MAIN (0x004000)
    Storage_ReadRecord(REC_SETTINGS_MAIN, &mainConfig, 0, sizeof(mainConfig));
    
    // REC_AUDIO_SETTINGS (0x00A000)
    Storage_ReadRecord(REC_AUDIO_SETTINGS, Data, 0, 8);
    gSetting_set_audio = LIMIT(Data[0], 5, 0);

    gEeprom.CHAN_1_CALL          = IS_MR_CHANNEL(mainConfig.fields.CHAN_1_CALL) ? mainConfig.fields.CHAN_1_CALL : MR_CHANNEL_FIRST;
    gEeprom.SQUELCH_LEVEL        = LIMIT(mainConfig.fields.SQUELCH_LEVEL, 10, 1);
    gEeprom.TX_TIMEOUT_TIMER     = RANGE(mainConfig.fields.TX_TIMEOUT_TIMER, 5, 179, 11);
    
    #ifdef ENABLE_NOAA
        gEeprom.NOAA_AUTO_SCAN   = LIMIT(mainConfig.fields.NOAA_AUTO_SCAN, 2, false);
    #endif

    gEeprom.KEY_LOCK = mainConfig.fields.KEY_LOCK;
    #ifdef ENABLE_RESCUE_OPERATIONS
        gEeprom.MENU_LOCK = mainConfig.fields.MENU_LOCK;
        gEeprom.SET_KEY = LIMIT(mainConfig.fields.SET_KEY, 5, 0);
    #endif
    gEeprom.SET_NAV = mainConfig.fields.SET_NAV;

    #ifdef ENABLE_VOX
        gEeprom.VOX_SWITCH       = LIMIT(mainConfig.fields.VOX_SWITCH, 2, false);
        gEeprom.VOX_LEVEL        = LIMIT(mainConfig.fields.VOX_LEVEL, 10, 1);
    #endif
    gEeprom.MIC_SENSITIVITY      = LIMIT(mainConfig.fields.MIC_SENSITIVITY, 5, 4);

    gEeprom.BACKLIGHT_MAX         = mainConfig.fields.BACKLIGHT_MAX <= 10 ? mainConfig.fields.BACKLIGHT_MAX : 10;
    gEeprom.BACKLIGHT_MIN         = mainConfig.fields.BACKLIGHT_MIN < gEeprom.BACKLIGHT_MAX ? mainConfig.fields.BACKLIGHT_MIN : 0;

#ifdef ENABLE_BLMIN_TMP_OFF
    gEeprom.BACKLIGHT_MIN_STAT    = BLMIN_STAT_ON;
#endif
    gEeprom.CHANNEL_DISPLAY_MODE  = LIMIT(mainConfig.fields.CHANNEL_DISPLAY_MODE, 4, MDF_FREQUENCY);
    gEeprom.CROSS_BAND_RX_TX      = LIMIT(mainConfig.fields.CROSS_BAND_RX_TX, 3, CROSS_BAND_OFF);
    gEeprom.BATTERY_SAVE          = LIMIT(mainConfig.fields.BATTERY_SAVE, 6, 4);
    gEeprom.DUAL_WATCH            = LIMIT(mainConfig.fields.DUAL_WATCH, 3, DUAL_WATCH_CHAN_A);
    gEeprom.BACKLIGHT_TIME        = LIMIT(mainConfig.fields.BACKLIGHT_TIME, 62, 12);

    #ifdef ENABLE_NARROWER_BW_FILTER
        gEeprom.TAIL_TONE_ELIMINATION = mainConfig.fields.TAIL_TONE_ELIMINATION;
        gSetting_set_nfm = mainConfig.fields.NFM;
    #else
        gEeprom.TAIL_TONE_ELIMINATION = mainConfig.fields.TAIL_TONE_ELIMINATION;
    #endif

    #ifdef ENABLE_BOOT_RESUME_STATE
        gEeprom.VFO_OPEN = mainConfig.fields.VFO_OPEN;
        gEeprom.CURRENT_STATE = mainConfig.fields.CURRENT_STATE;
        gEeprom.CURRENT_LIST = mainConfig.fields.CURRENT_LIST;
    #else
        gEeprom.VFO_OPEN              = mainConfig.fields.VFO_OPEN;
    #endif

    // 0E80..0E87 (VFO Indices)
    Storage_ReadRecord(REC_VFO_INDICES, Data, 0, 8);
    gEeprom.ScreenChannel[0]   = IS_VALID_CHANNEL(Data[0]) ? Data[0] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.ScreenChannel[1]   = IS_VALID_CHANNEL(Data[3]) ? Data[3] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.MrChannel[0]       = IS_MR_CHANNEL(Data[1])    ? Data[1] : MR_CHANNEL_FIRST;
    gEeprom.MrChannel[1]       = IS_MR_CHANNEL(Data[4])    ? Data[4] : MR_CHANNEL_FIRST;
    gEeprom.FreqChannel[0]     = IS_FREQ_CHANNEL(Data[2])  ? Data[2] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
    gEeprom.FreqChannel[1]     = IS_FREQ_CHANNEL(Data[5])  ? Data[5] : (FREQ_CHANNEL_FIRST + BAND6_400MHz);
#ifdef ENABLE_NOAA
    gEeprom.NoaaChannel[0] = IS_NOAA_CHANNEL(Data[6])  ? Data[6] : NOAA_CHANNEL_FIRST;
    gEeprom.NoaaChannel[1] = IS_NOAA_CHANNEL(Data[7])  ? Data[7] : NOAA_CHANNEL_FIRST;
#endif

#ifdef ENABLE_FMRADIO
    {   // 0E88..0E8F
        struct
        {
            uint16_t selFreq;
            uint8_t  selChn;
            uint8_t  isMrMode:1;
            uint8_t  band:2;
            //uint8_t  space:2;
        } __attribute__((packed)) fmCfg;
        Storage_ReadRecord(REC_FM_CONFIG, &fmCfg, 0, 4);

        gEeprom.FM_Band = fmCfg.band;
        //gEeprom.FM_Space = fmCfg.space;
        gEeprom.FM_SelectedFrequency = 
            (fmCfg.selFreq >= BK1080_GetFreqLoLimit(gEeprom.FM_Band) && fmCfg.selFreq <= BK1080_GetFreqHiLimit(gEeprom.FM_Band)) ? 
                fmCfg.selFreq : BK1080_GetFreqLoLimit(gEeprom.FM_Band);
            
        gEeprom.FM_SelectedChannel = fmCfg.selChn;
        gEeprom.FM_IsMrMode        = fmCfg.isMrMode;
    }

    // 0E40..0E67
    Storage_ReadRecord(REC_FM_CHANNELS, gFM_Channels, 0, sizeof(gFM_Channels));
    FM_ConfigureChannelState();
#endif

    // REC_SETTINGS_EXTRA (0x0E90)
    Storage_ReadRecord(REC_SETTINGS_EXTRA, &extraConfig, 0, sizeof(extraConfig));
    
    gEeprom.BEEP_CONTROL                 = extraConfig.fields.BEEP_CONTROL;
    gEeprom.KEY_M_LONG_PRESS_ACTION      = LIMIT(extraConfig.fields.KEY_M_LONG_PRESS_ACTION, ACTION_OPT_LEN, ACTION_OPT_NONE);
    gEeprom.KEY_1_SHORT_PRESS_ACTION     = LIMIT(extraConfig.fields.KEY_1_SHORT_PRESS_ACTION, ACTION_OPT_LEN, ACTION_OPT_MONITOR);
    gEeprom.KEY_1_LONG_PRESS_ACTION      = LIMIT(extraConfig.fields.KEY_1_LONG_PRESS_ACTION, ACTION_OPT_LEN, ACTION_OPT_NONE);
    gEeprom.KEY_2_SHORT_PRESS_ACTION     = LIMIT(extraConfig.fields.KEY_2_SHORT_PRESS_ACTION, ACTION_OPT_LEN, ACTION_OPT_SCAN);
    gEeprom.KEY_2_LONG_PRESS_ACTION      = LIMIT(extraConfig.fields.KEY_2_LONG_PRESS_ACTION, ACTION_OPT_LEN, ACTION_OPT_NONE);
    gEeprom.SCAN_RESUME_MODE             = LIMIT(extraConfig.fields.SCAN_RESUME_MODE, 105, 14);
    gEeprom.AUTO_KEYPAD_LOCK             = LIMIT(extraConfig.fields.AUTO_KEYPAD_LOCK, 41, 0);
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    gEeprom.POWER_ON_DISPLAY_MODE        = LIMIT(extraConfig.fields.POWER_ON_DISPLAY_MODE, 6, POWER_ON_DISPLAY_MODE_VOLTAGE);
#else
    gEeprom.POWER_ON_DISPLAY_MODE        = LIMIT(extraConfig.fields.POWER_ON_DISPLAY_MODE, 4, POWER_ON_DISPLAY_MODE_VOLTAGE);
#endif

    #ifdef ENABLE_PWRON_PASSWORD
        gEeprom.POWER_ON_PASSWORD = extraConfig.fields.POWER_ON_PASSWORD;
    #endif

    #ifdef ENABLE_VOICE
    gEeprom.VOICE_PROMPT = LIMIT(extraConfig.fields.VOICE_PROMPT, 3, VOICE_PROMPT_ENGLISH);
    #endif

    #ifdef ENABLE_RSSI_BAR
        if((extraConfig.fields.S0_LEVEL < 200 && extraConfig.fields.S0_LEVEL > 90) && 
           (extraConfig.fields.S9_LEVEL < extraConfig.fields.S0_LEVEL - 9 && extraConfig.fields.S0_LEVEL < 160 && extraConfig.fields.S9_LEVEL > 50)) {
            gEeprom.S0_LEVEL = extraConfig.fields.S0_LEVEL;
            gEeprom.S9_LEVEL = extraConfig.fields.S9_LEVEL;
        }
        else {
            gEeprom.S0_LEVEL = 130;
            gEeprom.S9_LEVEL = 76;
        }
    #endif

    #if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        gEeprom.ALARM_MODE = LIMIT(extraConfig.fields.ALARM_MODE, 2, ALARM_MODE_SITE);
    #endif
    #ifdef ENABLE_CUSTOM_ROGER
        gEeprom.ROGER = LIMIT(extraConfig.fields.ROGER, ROGER_MODE_CUSTOM3, ROGER_MODE_OFF);
    #elif defined(ENABLE_EXTRA_ROGER)
        gEeprom.ROGER = LIMIT(extraConfig.fields.ROGER, ROGER_MODE_UV5RC, ROGER_MODE_OFF);
    #else
        gEeprom.ROGER = LIMIT(extraConfig.fields.ROGER, ROGER_MODE_MDC, ROGER_MODE_OFF);
    #endif
    gEeprom.REPEATER_TAIL_TONE_ELIMINATION = LIMIT(extraConfig.fields.REPEATER_TAIL_TONE_ELIMINATION, 21, 0);
    gEeprom.TX_VFO = LIMIT(extraConfig.fields.TX_VFO, 2, 0);
    gEeprom.BATTERY_TYPE = LIMIT(extraConfig.fields.BATTERY_TYPE, BATTERY_TYPE_UNKNOWN, BATTERY_TYPE_1600_MAH);

    gEeprom.DTMF_SIDE_TONE = extraConfig.fields.DTMF_SIDE_TONE;
#ifdef ENABLE_DTMF_CALLING
    gEeprom.DTMF_SEPARATE_CODE = DTMF_ValidateCodes((char *)&extraConfig.fields.DTMF_SEPARATE_CODE, 1) ? extraConfig.fields.DTMF_SEPARATE_CODE : '*';
    gEeprom.DTMF_GROUP_CALL_CODE = DTMF_ValidateCodes((char *)&extraConfig.fields.DTMF_GROUP_CALL_CODE, 1) ? extraConfig.fields.DTMF_GROUP_CALL_CODE : '#';
    gEeprom.DTMF_DECODE_RESPONSE = LIMIT(extraConfig.fields.DTMF_DECODE_RESPONSE, 4, 0);
    gEeprom.DTMF_auto_reset_time = LIMIT(extraConfig.fields.DTMF_AUTO_RESET_TIME, 61, 10);
#endif
    gEeprom.DTMF_PRELOAD_TIME = PERIOD(extraConfig.fields.DTMF_PRELOAD_TIME_DIV10, 300);
    gEeprom.DTMF_FIRST_CODE_PERSIST_TIME = PERIOD(extraConfig.fields.DTMF_FIRST_CODE_PERSIST_TIME_DIV10, 100);
    gEeprom.DTMF_HASH_CODE_PERSIST_TIME = PERIOD(extraConfig.fields.DTMF_HASH_CODE_PERSIST_TIME_DIV10, 100);
    gEeprom.DTMF_CODE_PERSIST_TIME = PERIOD(extraConfig.fields.DTMF_CODE_PERSIST_TIME_DIV10, 100);
    gEeprom.DTMF_CODE_INTERVAL_TIME = PERIOD(extraConfig.fields.DTMF_CODE_INTERVAL_TIME_DIV10, 100);
#ifdef ENABLE_DTMF_CALLING
    gEeprom.PERMIT_REMOTE_KILL = extraConfig.fields.PERMIT_REMOTE_KILL;
#endif

#ifdef ENABLE_DTMF_CALLING
    // DTMF Codes
    Storage_ReadRecord(REC_ANI_DTMF_ID, Data, 0, sizeof(gEeprom.ANI_DTMF_ID));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.ANI_DTMF_ID))) {
        memcpy(gEeprom.ANI_DTMF_ID, Data, sizeof(gEeprom.ANI_DTMF_ID));
    } else {
        strcpy(gEeprom.ANI_DTMF_ID, "123");
    }

    Storage_ReadRecord(REC_KILL_CODE, Data, 0, sizeof(gEeprom.KILL_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.KILL_CODE))) {
        memcpy(gEeprom.KILL_CODE, Data, sizeof(gEeprom.KILL_CODE));
    } else {
        strcpy(gEeprom.KILL_CODE, "ABCD9");
    }

    Storage_ReadRecord(REC_REVIVE_CODE, Data, 0, sizeof(gEeprom.REVIVE_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.REVIVE_CODE))) {
        memcpy(gEeprom.REVIVE_CODE, Data, sizeof(gEeprom.REVIVE_CODE));
    } else {
        strcpy(gEeprom.REVIVE_CODE, "9DCBA");
    }
#endif

    Storage_ReadRecord(REC_DTMF_UP_CODE, Data, 0, sizeof(gEeprom.DTMF_UP_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_UP_CODE))) {
        memcpy(gEeprom.DTMF_UP_CODE, Data, sizeof(gEeprom.DTMF_UP_CODE));
    } else {
        strcpy(gEeprom.DTMF_UP_CODE, "12345");
    }

    Storage_ReadRecord(REC_DTMF_DOWN_CODE, Data, 0, sizeof(gEeprom.DTMF_DOWN_CODE));
    if (DTMF_ValidateCodes((char *)Data, sizeof(gEeprom.DTMF_DOWN_CODE))) {
        memcpy(gEeprom.DTMF_DOWN_CODE, Data, sizeof(gEeprom.DTMF_DOWN_CODE));
    } else {
        strcpy(gEeprom.DTMF_DOWN_CODE, "54321");
    }

    // SCAN LIST Record (0x0F18)
    Storage_ReadRecord(REC_SCAN_LIST, &scanList, 0, sizeof(scanList));
    gEeprom.SCAN_LIST_DEFAULT = LIMIT(scanList.fields.SCAN_LIST_DEFAULT, 6, 0);
    for (int i = 0; i < 3; i++) {
        gEeprom.SCAN_LIST_ENABLED[0] = (scanList.fields.SCAN_LIST_ENABLED >> i) & 1;
        gEeprom.SCANLIST_PRIORITY_CH1[i] = scanList.fields.lists[i].PRIORITY_CH1;
        gEeprom.SCANLIST_PRIORITY_CH2[i] = scanList.fields.lists[i].PRIORITY_CH2;
    }

    // F LOCK Record (0x0F40)
    Storage_ReadRecord(REC_F_LOCK, &flockConfig, 0, sizeof(flockConfig));
    gSetting_F_LOCK = LIMIT(flockConfig.fields.F_LOCK, F_LOCK_LEN, F_LOCK_DEF);
#ifndef ENABLE_CUSTOM_FIRMWARE_MODS
    gSetting_350TX = LIMIT(flockConfig.fields.TX_350, 2, false);
    gSetting_200TX = LIMIT(flockConfig.fields.TX_200, 2, false);
    gSetting_500TX = LIMIT(flockConfig.fields.TX_500, 2, false);
    gSetting_ScrambleEnable = LIMIT(flockConfig.fields.SCRAMBLE_EN, 2, true);
#endif
#ifdef ENABLE_DTMF_CALLING
    gSetting_KILLED = LIMIT(flockConfig.fields.KILLED, 2, false);
#endif
    gSetting_350EN = LIMIT(flockConfig.fields.EN_350, 2, true);
    gSetting_live_DTMF_decoder = flockConfig.fields.LIVE_DTMF_DECODER;
    gSetting_battery_text = flockConfig.fields.BATTERY_TEXT;
#ifdef ENABLE_MIC_BAR
    gSetting_mic_bar = flockConfig.fields.MIC_BAR;
#endif
#ifdef ENABLE_AM_FIX
    gSetting_AM_fix = flockConfig.fields.AM_FIX;
#endif
    gSetting_backlight_on_tx_rx = flockConfig.fields.BACKLIGHT_ON_TX_RX;

    if (!gEeprom.VFO_OPEN)
    {
        gEeprom.ScreenChannel[0] = gEeprom.MrChannel[0];
        gEeprom.ScreenChannel[1] = gEeprom.MrChannel[1];
    }

    // REC_MR_ATTRIBUTES (0x0D60)
    Storage_ReadRecord(REC_MR_ATTRIBUTES, gMR_ChannelAttributes, 0, sizeof(gMR_ChannelAttributes));
    for(uint16_t i = 0; i < ARRAY_SIZE(gMR_ChannelAttributes); i++) {
        ChannelAttributes_t *att = &gMR_ChannelAttributes[i];
        if(att->__val == 0xff){
            if (IS_MR_CHANNEL(i))
                continue; // Keep 0xFF for empty MR channels
            
            att->__val = 0;
            att->band = i - FREQ_CHANNEL_FIRST; // Each VFO slot gets its proper band
        }
        gMR_ChannelExclude[i] = false;
    }

    // 0x00A000 (AUDIO PROFILE)
    Storage_ReadRecord(REC_AUDIO_SETTINGS, gCustomAesKey, 0, sizeof(gCustomAesKey));
    bHasCustomAesKey = false;
    #ifndef ENABLE_CUSTOM_FIRMWARE_MODS
        for (unsigned int i = 0; i < ARRAY_SIZE(gCustomAesKey); i++)
        {
            if (gCustomAesKey[i] != 0xFFFFFFFFu)
            {
                bHasCustomAesKey = true;
                break;
            }
        }
    #endif

    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        Storage_ReadRecord(REC_CUSTOM_SETTINGS, Data, 0, 8);
        gSetting_set_pwr = (((Data[7] & 0xF0) >> 4) < 7) ? ((Data[7] & 0xF0) >> 4) : 0;
        gSetting_set_ptt = (((Data[7] & 0x0F)) < 3) ? ((Data[7] & 0x0F)) : 0;
        gSetting_set_tot = (((Data[6] & 0xF0) >> 4) < 4) ? ((Data[6] & 0xF0) >> 4) : 0;
        gSetting_set_eot = (((Data[6] & 0x0F)) < 4) ? ((Data[6] & 0x0F)) : 0;
        
        int tmp = (Data[5] & 0xF0) >> 4;
        #ifdef ENABLE_INVERTED_LCD_MODE
            gSetting_set_inv = (tmp >> 0) & 0x01;
        #else
            gSetting_set_inv = 0;
        #endif
        gSetting_set_lck = (tmp >> 1) & 0x01;
        // gSetting_set_met = (tmp >> 2) & 0x01; // Removed
        // gSetting_set_gui = (tmp >> 3) & 0x01; // Removed

        #ifdef ENABLE_LCD_CONTRAST_OPTION
            int ctr_value = Data[5] & 0x0F;
            gSetting_set_ctr = (ctr_value > 0 && ctr_value < 16) ? ctr_value : 10;
        #else
            gSetting_set_ctr = 10;
        #endif

        gSetting_set_tmr = Data[4] & 0x01;
        #ifdef ENABLE_DEEP_SLEEP_MODE
            gSetting_set_off = (Data[4] >> 1) > 120 ? 60 : (Data[4] >> 1); 
        #endif
        
        gEeprom.LIVESEEK_MODE = (Data[5] >> 6) & 0x03;

        gSetting_set_ptt_session = gSetting_set_ptt;
        gEeprom.KEY_LOCK_PTT = gSetting_set_lck;
    #endif
}

void SETTINGS_LoadCalibration(void)
{
//  uint8_t Mic;

    // 0x1EC0
    Storage_ReadRecord(REC_CALIB_RSSI_3, gEEPROM_RSSI_CALIB[3], 0, 8);
    memcpy(gEEPROM_RSSI_CALIB[4], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[5], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[6], gEEPROM_RSSI_CALIB[3], 8);

    // 0x1EC8
    Storage_ReadRecord(REC_CALIB_RSSI_0, gEEPROM_RSSI_CALIB[0], 0, 8);
    memcpy(gEEPROM_RSSI_CALIB[1], gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[2], gEEPROM_RSSI_CALIB[0], 8);

    // 0x1F40
    Storage_ReadRecord(REC_CALIB_BATTERY, gBatteryCalibration, 0, 12);
    if (gBatteryCalibration[0] >= 5000)
    {
        gBatteryCalibration[0] = 1900;
        gBatteryCalibration[1] = 2000;
    }
    gBatteryCalibration[5] = 2300;

    #ifdef ENABLE_VOX
        // 0x1F50
        Storage_ReadRecordIndexed(REC_CALIB_VOX1, gEeprom.VOX_LEVEL, &gEeprom.VOX1_THRESHOLD, 0, 2);
        // 0x1F68
        Storage_ReadRecordIndexed(REC_CALIB_VOX0, gEeprom.VOX_LEVEL, &gEeprom.VOX0_THRESHOLD, 0, 2);
    #endif

    //PY25Q16_ReadBuffer(0x1F80 + gEeprom.MIC_SENSITIVITY, &Mic, 1);
    //gEeprom.MIC_SENSITIVITY_TUNING = (Mic < 32) ? Mic : 15;
    gEeprom.MIC_SENSITIVITY_TUNING = gMicGain_dB2[gEeprom.MIC_SENSITIVITY];

    {
        CalibrationMisc_t Misc;

        // radio 1 .. 04 00 46 00 50 00 2C 0E
        // radio 2 .. 05 00 46 00 50 00 2C 0E
        // 0x1F88
        Storage_ReadRecord(REC_CALIB_MISC, &Misc, 0, sizeof(Misc));

        gEeprom.BK4819_XTAL_FREQ_LOW = (Misc.fields.BK4819_XtalFreqLow >= -1000 && Misc.fields.BK4819_XtalFreqLow <= 1000) ? Misc.fields.BK4819_XtalFreqLow : 0;
        gEEPROM_1F8A                 = Misc.fields.LnaCalibration & 0x01FF;
        gEEPROM_1F8C                 = Misc.fields.MixCalibration & 0x01FF;
        gEeprom.VOLUME_GAIN          = (Misc.fields.VOLUME_GAIN < 64) ? Misc.fields.VOLUME_GAIN : 58;
        gEeprom.DAC_GAIN             = (Misc.fields.DAC_GAIN    < 16) ? Misc.fields.DAC_GAIN    : 8;

        #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
            gEeprom.VOLUME_GAIN_BACKUP   = gEeprom.VOLUME_GAIN;
        #endif

        BK4819_WriteRegister(BK4819_REG_3B, 22656 + gEeprom.BK4819_XTAL_FREQ_LOW);
//      BK4819_WriteRegister(BK4819_REG_3C, gEeprom.BK4819_XTAL_FREQ_HIGH);
    }
}

uint32_t SETTINGS_FetchChannelFrequency(const int channel)
{
    struct
    {
        uint32_t frequency;
        uint32_t offset;
    } __attribute__((packed)) info;

    Storage_ReadRecordIndexed(REC_CHANNEL_DATA, channel, &info, 0, sizeof(info));

    return info.frequency;
}

void SETTINGS_FetchChannelName(char *s, const int channel)
{
    if (s == NULL)
        return;

    s[0] = 0;

    if (channel < 0)
        return;

    if (!RADIO_CheckValidChannel(channel, false, 0))
        return;

    // 0x0F50
    Storage_ReadRecordIndexed(REC_CHANNEL_NAMES, channel, s, 0, 10);

    int i;
    for (i = 0; i < 10; i++)
        if (s[i] < 32 || s[i] > 127)
            break;                // invalid char

    s[i--] = 0;                   // null term

    while (i >= 0 && s[i] == 32)  // trim trailing spaces
        s[i--] = 0;               // null term
}

void SETTINGS_FactoryReset(bool bIsAll)
{
    // 0000 - 0c80
    Storage_SectorErase(REC_CHANNEL_DATA);
    // 0c80 - 0d60
    Storage_SectorErase(REC_MR_ATTRIBUTES); // Assuming 0x1000 is part of or before REC_MR_ATTRIBUTES
    // 0d60 - 0e30
    if (bIsAll)
    {
        Storage_SectorErase(REC_MR_ATTRIBUTES);
    }
    // 0e40 - 0e68
    if (bIsAll)
    {
        Storage_SectorErase(REC_FM_CHANNELS);
    }
    // 0e70 - 0e80
    Storage_SectorErase(REC_SETTINGS_MAIN);
    // 0e80 - 0e88
    Storage_SectorErase(REC_VFO_INDICES);
    // 0e88 - 0e90
    if (bIsAll)
    {
        Storage_SectorErase(REC_FM_CONFIG);
    }
    // 0e90 - 0ee0
    do
    {
        uint8_t Buf[0x50];
        memset(Buf, 0xff, 0x50);
        // 0EA0 - 0EA8 : keep
        Storage_ReadRecord(REC_SETTINGS_EXTRA, Buf + 0x10, 0x10, 8);
        // 0EB0 - 0ED0 : keep
        Storage_ReadRecord(REC_SETTINGS_EXTRA, Buf + 0x20, 0x20, 0x20);
        Storage_WriteRecord(REC_SETTINGS_EXTRA, Buf, 0, 0x50);
    } while (0);
    // 0ee0 - 0f18 : keep
    // 0f18 - 0f20
    if (bIsAll)
    {
        Storage_SectorErase(REC_SCAN_LIST);
    }
    // 0f30 - 0f40 : keep
    // 0f40 - 0f48 : keep
    // 0f50 - 1bd0
    if (bIsAll)
    {
        Storage_SectorErase(REC_CHANNEL_NAMES);
    }
    // 1c00 - 1d00 : keep

    if (bIsAll)
    {
        RADIO_InitInfo(gRxVfo, FREQ_CHANNEL_FIRST + BAND6_400MHz, 43350000);

        #ifdef ENABLE_RESET_CHANNEL_FUNCTION
            // set the first few memory channels
            for (i = 0; i < ARRAY_SIZE(gDefaultFrequencyTable); i++)
            {
                const uint32_t Frequency   = gDefaultFrequencyTable[i];
                gRxVfo->freq_config_RX.Frequency = Frequency;
                gRxVfo->freq_config_TX.Frequency = Frequency;
                gRxVfo->Band               = FREQUENCY_GetBand(Frequency);
                SETTINGS_SaveChannel(MR_CHANNEL_FIRST + i, 0, gRxVfo, 2);
            }
        #endif

        #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
            Storage_SectorErase(REC_CUSTOM_SETTINGS);
        #endif
    }

    // Prevent reset to restart in RO mode...
    #ifdef ENABLE_RESCUE_OPERATIONS
        {
            uint8_t buf[0x10];

            Storage_ReadRecord(REC_SETTINGS_MAIN, buf, 0, sizeof(buf));
            // bit 1 = MENU_LOCK => on le force à 0
            buf[4] &= (uint8_t)~0x02;
            Storage_WriteRecord(REC_SETTINGS_MAIN, buf, 0, sizeof(buf));

            // cohérence RAM
            gEeprom.MENU_LOCK = 0;
        }
    #endif
}

#ifdef ENABLE_FMRADIO
void SETTINGS_SaveFM(void)
    {
        union {
            struct {
                uint16_t selFreq;
                uint8_t  selChn;
                uint8_t  isMrMode:1;
                uint8_t  band:2;
                //uint8_t  space:2;
            };
            uint8_t __raw[8];
        } __attribute__((packed)) fmCfg;

        memset(fmCfg.__raw, 0xFF, sizeof(fmCfg.__raw));
        fmCfg.selChn   = gEeprom.FM_SelectedChannel;
        fmCfg.selFreq  = gEeprom.FM_SelectedFrequency;
        fmCfg.isMrMode = gEeprom.FM_IsMrMode;
        fmCfg.band     = gEeprom.FM_Band;
        // fmCfg.space    = gEeprom.FM_Space;
        // 0E88
        Storage_WriteRecord(REC_FM_CONFIG, fmCfg.__raw, 0, 8);

        // 0E40
        Storage_WriteRecord(REC_FM_CHANNELS, gFM_Channels, 0, sizeof(gFM_Channels));
    }
#endif

void SETTINGS_SaveVfoIndices(void)
{
    uint8_t State[8];

    #ifndef ENABLE_NOAA
        // 0x0E80
        Storage_ReadRecord(REC_VFO_INDICES, State, 0, sizeof(State));
    #endif

    State[0] = gEeprom.ScreenChannel[0];
    State[1] = gEeprom.MrChannel[0];
    State[2] = gEeprom.FreqChannel[0];
    State[3] = gEeprom.ScreenChannel[1];
    State[4] = gEeprom.MrChannel[1];
    State[5] = gEeprom.FreqChannel[1];
    #ifdef ENABLE_NOAA
        State[6] = gEeprom.NoaaChannel[0];
        State[7] = gEeprom.NoaaChannel[1];
    #endif

    // 0x0E80
    Storage_WriteRecord(REC_VFO_INDICES, State, 0, 8);
}

void SETTINGS_SaveSettings(void)
{
    SettingsMain_t mainConfig;
    SettingsExtra_t extraConfig;
    ScanList_t scanList;
    FLockConfig_t flockConfig;
    uint8_t SecBuf[0x50];

    // REC_SETTINGS_MAIN (0x004000)
    memset(mainConfig.raw, 0xff, sizeof(mainConfig.raw));

    mainConfig.fields.CHAN_1_CALL      = gEeprom.CHAN_1_CALL;
    mainConfig.fields.SQUELCH_LEVEL    = gEeprom.SQUELCH_LEVEL;
    mainConfig.fields.TX_TIMEOUT_TIMER = gEeprom.TX_TIMEOUT_TIMER;
    #ifdef ENABLE_NOAA
        mainConfig.fields.NOAA_AUTO_SCAN = gEeprom.NOAA_AUTO_SCAN;
    #else
        mainConfig.fields.NOAA_AUTO_SCAN = false;
    #endif

    mainConfig.fields.KEY_LOCK = gEeprom.KEY_LOCK;
    #ifdef ENABLE_RESCUE_OPERATIONS
        mainConfig.fields.MENU_LOCK = gEeprom.MENU_LOCK;
        mainConfig.fields.SET_KEY   = gEeprom.SET_KEY & 0x0F;
    #endif
    mainConfig.fields.SET_NAV = gEeprom.SET_NAV;

    #ifdef ENABLE_VOX
        mainConfig.fields.VOX_SWITCH = gEeprom.VOX_SWITCH;
        mainConfig.fields.VOX_LEVEL  = gEeprom.VOX_LEVEL;
    #else
        mainConfig.fields.VOX_SWITCH = false;
        mainConfig.fields.VOX_LEVEL  = 0;
    #endif
    mainConfig.fields.MIC_SENSITIVITY = gEeprom.MIC_SENSITIVITY;

    mainConfig.fields.BACKLIGHT_MIN = gEeprom.BACKLIGHT_MIN;
    mainConfig.fields.BACKLIGHT_MAX = gEeprom.BACKLIGHT_MAX;
    mainConfig.fields.CHANNEL_DISPLAY_MODE = gEeprom.CHANNEL_DISPLAY_MODE;
    mainConfig.fields.CROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
    mainConfig.fields.BATTERY_SAVE     = gEeprom.BATTERY_SAVE;
    mainConfig.fields.DUAL_WATCH       = gEeprom.DUAL_WATCH;

    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        if(!gSaveRxMode)
        {
            mainConfig.fields.CROSS_BAND_RX_TX = gCB;
            mainConfig.fields.DUAL_WATCH = gDW;
        }
        if(gBackLight)
        {
            mainConfig.fields.BACKLIGHT_TIME = gBacklightTimeOriginal;
        }
        else
        {
            mainConfig.fields.BACKLIGHT_TIME = gEeprom.BACKLIGHT_TIME;
        }
    #else
        mainConfig.fields.BACKLIGHT_TIME = gEeprom.BACKLIGHT_TIME;
    #endif

    #ifdef ENABLE_NARROWER_BW_FILTER
        mainConfig.fields.TAIL_TONE_ELIMINATION = gEeprom.TAIL_TONE_ELIMINATION & 0x01;
        mainConfig.fields.NFM = gSetting_set_nfm & 0x03;
    #else
        mainConfig.fields.TAIL_TONE_ELIMINATION = gEeprom.TAIL_TONE_ELIMINATION;
    #endif

    #ifdef ENABLE_BOOT_RESUME_STATE
        mainConfig.fields.VFO_OPEN = gEeprom.VFO_OPEN & 0x01;
        mainConfig.fields.CURRENT_STATE = gEeprom.CURRENT_STATE & 0x07;
        mainConfig.fields.CURRENT_LIST = gEeprom.SCAN_LIST_DEFAULT & 0x07;
    #else
        mainConfig.fields.VFO_OPEN = gEeprom.VFO_OPEN;
    #endif

    Storage_WriteRecord(REC_SETTINGS_MAIN, mainConfig.raw, 0, sizeof(mainConfig.raw));

    // REC_SETTINGS_EXTRA (0x0E90)
    Storage_ReadRecord(REC_SETTINGS_EXTRA, &extraConfig, 0, sizeof(extraConfig));

    extraConfig.fields.BEEP_CONTROL = gEeprom.BEEP_CONTROL;
    extraConfig.fields.KEY_M_LONG_PRESS_ACTION = gEeprom.KEY_M_LONG_PRESS_ACTION;
    extraConfig.fields.KEY_1_SHORT_PRESS_ACTION = gEeprom.KEY_1_SHORT_PRESS_ACTION;
    extraConfig.fields.KEY_1_LONG_PRESS_ACTION = gEeprom.KEY_1_LONG_PRESS_ACTION;
    extraConfig.fields.KEY_2_SHORT_PRESS_ACTION = gEeprom.KEY_2_SHORT_PRESS_ACTION;
    extraConfig.fields.KEY_2_LONG_PRESS_ACTION = gEeprom.KEY_2_LONG_PRESS_ACTION;
    extraConfig.fields.SCAN_RESUME_MODE = gEeprom.SCAN_RESUME_MODE;
    extraConfig.fields.AUTO_KEYPAD_LOCK = gEeprom.AUTO_KEYPAD_LOCK;
    extraConfig.fields.POWER_ON_DISPLAY_MODE = gEeprom.POWER_ON_DISPLAY_MODE;

    #ifdef ENABLE_PWRON_PASSWORD
        extraConfig.fields.POWER_ON_PASSWORD = gEeprom.POWER_ON_PASSWORD;
    #endif

#ifdef ENABLE_VOICE
    extraConfig.fields.VOICE_PROMPT = gEeprom.VOICE_PROMPT;
#endif
#ifdef ENABLE_RSSI_BAR
    extraConfig.fields.S0_LEVEL = gEeprom.S0_LEVEL;
    extraConfig.fields.S9_LEVEL = gEeprom.S9_LEVEL;
#endif

    #if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        extraConfig.fields.ALARM_MODE = gEeprom.ALARM_MODE;
    #endif
    extraConfig.fields.ROGER = gEeprom.ROGER;
    extraConfig.fields.REPEATER_TAIL_TONE_ELIMINATION = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
    extraConfig.fields.TX_VFO = gEeprom.TX_VFO;
    extraConfig.fields.BATTERY_TYPE = gEeprom.BATTERY_TYPE;

    extraConfig.fields.DTMF_SIDE_TONE = gEeprom.DTMF_SIDE_TONE;
#ifdef ENABLE_DTMF_CALLING
    extraConfig.fields.DTMF_SEPARATE_CODE = gEeprom.DTMF_SEPARATE_CODE;
    extraConfig.fields.DTMF_GROUP_CALL_CODE = gEeprom.DTMF_GROUP_CALL_CODE;
    extraConfig.fields.DTMF_DECODE_RESPONSE = gEeprom.DTMF_DECODE_RESPONSE;
    extraConfig.fields.DTMF_AUTO_RESET_TIME = gEeprom.DTMF_auto_reset_time;
#endif
    extraConfig.fields.DTMF_PRELOAD_TIME_DIV10 = gEeprom.DTMF_PRELOAD_TIME / 10U;
    extraConfig.fields.DTMF_FIRST_CODE_PERSIST_TIME_DIV10 = gEeprom.DTMF_FIRST_CODE_PERSIST_TIME / 10U;
    extraConfig.fields.DTMF_HASH_CODE_PERSIST_TIME_DIV10 = gEeprom.DTMF_HASH_CODE_PERSIST_TIME / 10U;
    extraConfig.fields.DTMF_CODE_PERSIST_TIME_DIV10 = gEeprom.DTMF_CODE_PERSIST_TIME / 10U;
    extraConfig.fields.DTMF_CODE_INTERVAL_TIME_DIV10 = gEeprom.DTMF_CODE_INTERVAL_TIME / 10U;
#ifdef ENABLE_DTMF_CALLING
    extraConfig.fields.PERMIT_REMOTE_KILL = gEeprom.PERMIT_REMOTE_KILL;
#endif

    Storage_WriteRecord(REC_SETTINGS_EXTRA, extraConfig.raw, 0, sizeof(extraConfig.raw));

    // SCAN LIST Record (0x0F18)
    memset(scanList.raw, 0xff, sizeof(scanList.raw));
    scanList.fields.SCAN_LIST_DEFAULT = gEeprom.SCAN_LIST_DEFAULT;
    
    uint8_t enabledLists = 0;
    for (int i = 0; i < 3; i++) {
        if (gEeprom.SCAN_LIST_ENABLED[i]) {
            enabledLists |= (1 << i);
        }
        scanList.fields.lists[i].PRIORITY_CH1 = gEeprom.SCANLIST_PRIORITY_CH1[i];
        scanList.fields.lists[i].PRIORITY_CH2 = gEeprom.SCANLIST_PRIORITY_CH2[i];
    }
    scanList.fields.SCAN_LIST_ENABLED = enabledLists;

    Storage_WriteRecord(REC_SCAN_LIST, scanList.raw, 0, sizeof(scanList.raw));

    // F LOCK Record (0x0F40)
    memset(flockConfig.raw, 0xff, sizeof(flockConfig.raw));
    flockConfig.fields.F_LOCK = gSetting_F_LOCK;
#ifndef ENABLE_CUSTOM_FIRMWARE_MODS
    flockConfig.fields.TX_350 = gSetting_350TX;
    flockConfig.fields.TX_200 = gSetting_200TX;
    flockConfig.fields.TX_500 = gSetting_500TX;
    flockConfig.fields.SCRAMBLE_EN = gSetting_ScrambleEnable;
#endif
#ifdef ENABLE_DTMF_CALLING
    flockConfig.fields.KILLED = gSetting_KILLED;
#endif
    flockConfig.fields.EN_350 = gSetting_350EN;
    
    flockConfig.fields.LIVE_DTMF_DECODER = gSetting_live_DTMF_decoder;
    flockConfig.fields.BATTERY_TEXT = gSetting_battery_text & 7u;
#ifdef ENABLE_MIC_BAR
    flockConfig.fields.MIC_BAR = gSetting_mic_bar;
#endif
#ifdef ENABLE_AM_FIX
    flockConfig.fields.AM_FIX = gSetting_AM_fix;
#endif
    flockConfig.fields.BACKLIGHT_ON_TX_RX = gSetting_backlight_on_tx_rx & 3u;

    Storage_WriteRecord(REC_F_LOCK, flockConfig.raw, 0, sizeof(flockConfig.raw));


#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    // 0x1FF0 (Custom Settings)
    Storage_ReadRecord(REC_CUSTOM_SETTINGS, SecBuf, 0, 8);

#ifdef ENABLE_DEEP_SLEEP_MODE 
    SecBuf[4] = (gSetting_set_off << 1) | (gSetting_set_tmr & 0x01);
#else
    SecBuf[4] = gSetting_set_tmr ? (1 << 0) : 0;
#endif

    uint8_t flags = (gSetting_set_inv << 0) |
                     (gSetting_set_lck << 1) |
                     ((gEeprom.LIVESEEK_MODE & 0x03) << 2);

    SecBuf[5] = ((flags << 4) | (gSetting_set_ctr & 0x0F));
    SecBuf[6] = ((gSetting_set_tot << 4) | (gSetting_set_eot & 0x0F));
    SecBuf[7] = ((gSetting_set_pwr << 4) | (gSetting_set_ptt & 0x0F));

    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;

    Storage_WriteRecord(REC_CUSTOM_SETTINGS, SecBuf, 0, 8);
#endif

#ifdef ENABLE_SYSTEM_INFO_MENU
    SETTINGS_WriteCurrentVol();
#endif

    Storage_ReadRecord(REC_AUDIO_SETTINGS, SecBuf, 0, 8);
    SecBuf[0] = gSetting_set_audio;
    Storage_WriteRecord(REC_AUDIO_SETTINGS, SecBuf, 0, 8);
}

void SETTINGS_SaveChannel(uint8_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode)
{
#ifdef ENABLE_NOAA
    if (IS_NOAA_CHANNEL(Channel))
        return;
#endif

    if (Mode >= 2 || IS_FREQ_CHANNEL(Channel)) { // copy VFO to a channel
        ChannelData_t data;
        memset(data.raw, 0, sizeof(data.raw));

        data.fields.frequency        = pVFO->freq_config_RX.Frequency;
        data.fields.offset           = pVFO->TX_OFFSET_FREQUENCY;
        data.fields.rx_code          = pVFO->freq_config_RX.Code;
        data.fields.tx_code          = pVFO->freq_config_TX.Code;
        data.fields.rx_code_type     = pVFO->freq_config_RX.CodeType;
        data.fields.tx_code_type     = pVFO->freq_config_TX.CodeType;
        data.fields.modulation       = pVFO->Modulation;
        data.fields.offset_direction = pVFO->TX_OFFSET_FREQUENCY_DIRECTION;
        data.fields.reverse          = pVFO->FrequencyReverse;
        data.fields.bandwidth        = pVFO->CHANNEL_BANDWIDTH;
        data.fields.power            = pVFO->OUTPUT_POWER;
        data.fields.busy_lock        = pVFO->BUSY_CHANNEL_LOCK;
        data.fields.tx_lock          = pVFO->TX_LOCK;
        data.fields.dtmf_ptt_id      = pVFO->DTMF_PTT_ID_TX_MODE;
#ifdef ENABLE_DTMF_CALLING
        data.fields.dtmf_decoding    = pVFO->DTMF_DECODING_ENABLE;
#endif
        data.fields.step             = pVFO->STEP_SETTING;
        data.fields.scramble         = pVFO->SCRAMBLING_TYPE;

        if (IS_MR_CHANNEL(Channel)) {
            Storage_WriteRecordIndexed(REC_CHANNEL_DATA, Channel, data.raw, 0, sizeof(data.raw));
        } else {
            uint16_t storage_idx = ((Channel - FREQ_CHANNEL_FIRST) << 8) | VFO;
            Storage_WriteRecordIndexed(REC_VFO_DATA, storage_idx, data.raw, 0, sizeof(data.raw));
        }

        SETTINGS_UpdateChannel(Channel, pVFO, true, true, true);

        if (IS_MR_CHANNEL(Channel)) {
#ifndef ENABLE_KEEP_MEM_NAME
            // clear/reset the channel name
            SETTINGS_SaveChannelName(Channel, "");
#else
            if (Mode >= 3) {
                SETTINGS_SaveChannelName(Channel, pVFO->Name);
            }
#endif
        }
    }
}

void SETTINGS_SaveBatteryCalibration(const uint16_t * batteryCalibration)
{
    // 0x1F40
    Storage_WriteRecord(REC_CALIB_BATTERY, batteryCalibration, 0, 12);
}

void SETTINGS_SaveChannelName(uint8_t channel, const char * name)
{
    uint8_t buf[16] = {0};
    memcpy(buf, name, MIN(strlen(name), 10u));
    // 0x0F50
    Storage_WriteRecordIndexed(REC_CHANNEL_NAMES, channel, buf, 0, 0x10);
}
void SETTINGS_UpdateChannel(uint8_t channel, const VFO_Info_t *pVFO, bool keep, bool check, bool save)
{
#ifdef ENABLE_NOAA
    if (!IS_NOAA_CHANNEL(channel))
#endif
    {
        ChannelAttributes_t att;
        att.__val = 0; // default attributes

        if (keep) {
            att.band     = pVFO->Band;
            att.scanlist1 = pVFO->SCANLIST1_PARTICIPATION;
            att.scanlist3 = pVFO->SCANLIST3_PARTICIPATION;
            att.scanlist2 = pVFO->SCANLIST2_PARTICIPATION;
            att.compander = pVFO->Compander;

            if (check) {
                ChannelAttributes_t state;
                Storage_ReadRecordIndexed(REC_MR_ATTRIBUTES, channel, &state, 0, 1);
                if (state.__val == att.__val)
                    return; // no change in the attributes
            }
        } else {
             att.band = 0x7; // invalid/default band
        }

#ifndef ENABLE_CUSTOM_FIRMWARE_MODS
        save = true;
#endif
        if(save)
        {
             Storage_WriteRecordIndexed(REC_MR_ATTRIBUTES, channel, &att, 0, 1);
        }

        gMR_ChannelAttributes[channel] = att;

        if (IS_MR_CHANNEL(channel)) {   // it's a memory channel
            if (!keep) {
                // clear/reset the channel name
                SETTINGS_SaveChannelName(channel, "");
            }
        }
    }
}

void SETTINGS_WriteBuildOptions(void)
{
    uint8_t State[8];

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    // 0x1FF0
    Storage_ReadRecord(REC_CUSTOM_SETTINGS, State, 0, sizeof(State));
#endif
    
State[0] = 0
#ifdef ENABLE_FMRADIO
    | (1 << 0)
#endif
#ifdef ENABLE_NOAA
    | (1 << 1)
#endif
#ifdef ENABLE_VOICE
    | (1 << 2)
#endif
#ifdef ENABLE_VOX
    | (1 << 3)
#endif
#ifdef ENABLE_ALARM
    | (1 << 4)
#endif
#ifdef ENABLE_TX1750
    | (1 << 5)
#endif
#ifdef ENABLE_PWRON_PASSWORD
    | (1 << 6)
#endif
#ifdef ENABLE_DTMF_CALLING
    | (1 << 7)
#endif
;

State[1] = 0
#ifdef ENABLE_FLASHLIGHT
    | (1 << 0)
#endif
#ifdef ENABLE_WIDE_RX
    | (1 << 1)
#endif
#ifdef ENABLE_BYP_RAW_DEMODULATORS
    | (1 << 2)
#endif
#ifdef ENABLE_APP_BREAKOUT_GAME
    | (1 << 3)
#endif
#ifdef ENABLE_AM_FIX
    | (1 << 4)
#endif
#ifdef ENABLE_SPECTRUM
    | (1 << 5)
#endif
#ifdef ENABLE_RESCUE_OPERATIONS
    | (1 << 6)
#endif
;
    Storage_WriteRecord(REC_CUSTOM_SETTINGS, State, 0, sizeof(State));
}

#ifdef ENABLE_BOOT_RESUME_STATE
    void SETTINGS_WriteCurrentState(void)
    {
        SettingsMain_t config;
        Storage_ReadRecord(REC_SETTINGS_MAIN, &config, 0, sizeof(config));
        config.fields.VFO_OPEN = gEeprom.VFO_OPEN;
        config.fields.CURRENT_STATE = gEeprom.CURRENT_STATE;
        config.fields.CURRENT_LIST = gEeprom.SCAN_LIST_DEFAULT;
        Storage_WriteRecord(REC_SETTINGS_MAIN, &config, 0, sizeof(config));
    }
#endif

#ifdef ENABLE_SYSTEM_INFO_MENU
    void SETTINGS_WriteCurrentVol(void)
    {
        CalibrationMisc_t misc;
        Storage_ReadRecord(REC_CALIB_MISC, &misc, 0, sizeof(misc));
        misc.fields.VOLUME_GAIN = gEeprom.VOLUME_GAIN;
        Storage_WriteRecord(REC_CALIB_MISC, &misc, 0, sizeof(misc));
    }
#endif

#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
void SETTINGS_ResetTxLock(void)
{
    // TODO: This is expensive operation!

#define SETTINGS_ResetTxLock_BATCH 10

    uint8_t Buf[0xc80 / SETTINGS_ResetTxLock_BATCH];
    const uint32_t BatchSize = 0xc80 / SETTINGS_ResetTxLock_BATCH;
    const uint32_t BatchChCnt = BatchSize / 0x10;

    for (uint32_t i = 0; i < SETTINGS_ResetTxLock_BATCH; i++)
    {
        uint32_t Offset = i * BatchSize;
        Storage_ReadRecordIndexed(REC_CHANNEL_DATA, Offset / 16, Buf, 0, sizeof(Buf));

        uint8_t *State;
        for (uint8_t channel = 0; channel < BatchChCnt; channel++)
        {
            uint16_t OffsetVFO = channel * 16;
            State = Buf + OffsetVFO;
            State[4] |= (1 << 6);
        }

        Storage_WriteRecordIndexed(REC_CHANNEL_DATA, Offset / 16, Buf, 0, sizeof(Buf));
    }

#undef SETTINGS_ResetTxLock_BATCH
}
#endif
