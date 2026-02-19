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

#include "features/action/action.h"
#include "apps/fm/fm.h"
#include "features/generic/generic.h"
#include "features/audio/audio.h"
#include "drivers/bsp/bk1080.h"
#include "drivers/bsp/bk4819.h"
#include "features/storage/storage.h"
#include "drivers/bsp/py25q16.h"
#include "drivers/bsp/gpio.h"
#include "features/radio/functions.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define FM_CHANNELS_MAX 20

uint16_t          gFM_Channels[FM_CHANNELS_MAX];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
uint16_t          gFM_RestoreCountdown_10ms;
uint16_t          gFmAutoMuteCountdown_10ms;
bool              gFM_AutoMuted;

// Extended F-mode settings
uint8_t           gFmAudioProfile = 0;         // 0:75us, 1:50us, 2:RAW, 3:BASS
uint8_t           gFmSoftMuteAttenuation = 0; // 0:16dB, 1:14dB, 2:12dB, 3:10dB
uint8_t           gFmSoftMuteRate = 0;        // 0:Fastest, 1:Fast, 2:Slow, 3:Slowest
uint8_t           gFmSeekRSSIThreshold = 10;  // 0-255
uint8_t           gFmSeekSNRThreshold = 2;    // 0-15
uint8_t           gFmSpacing = 0;             // 0:200k (default), 1:100k, 2:50k

bool              gFmFunctionMode = false;

static uint8_t s_fm_update_tick = 0;

const uint8_t BUTTON_STATE_PRESSED = 1 << 0;
const uint8_t BUTTON_STATE_HELD = 1 << 1;

const uint8_t BUTTON_EVENT_PRESSED = BUTTON_STATE_PRESSED;
const uint8_t BUTTON_EVENT_HELD = BUTTON_STATE_PRESSED | BUTTON_STATE_HELD;
const uint8_t BUTTON_EVENT_SHORT =  0;
const uint8_t BUTTON_EVENT_LONG =  BUTTON_STATE_HELD;

static void Key_FUNC(KEY_Code_t Key, uint8_t state);

bool FM_CheckValidChannel(uint8_t Channel)
{
    return  Channel < ARRAY_SIZE(gFM_Channels) && 
            gFM_Channels[Channel] >= 6400 && 
            gFM_Channels[Channel] <= 10800;
}

uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction)
{
    for (unsigned i = 0; i < ARRAY_SIZE(gFM_Channels); i++) {
        if (Channel == 0xFF)
            Channel = ARRAY_SIZE(gFM_Channels) - 1;
        else if (Channel >= ARRAY_SIZE(gFM_Channels))
            Channel = 0;
        if (FM_CheckValidChannel(Channel))
            return Channel;
        Channel += Direction;
    }

    return 0xFF;
}

int FM_ConfigureChannelState(void)
{
    if (gEeprom.FM_IsMrMode) {
        const uint8_t Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel, FM_CHANNEL_UP);
        if (Channel == 0xFF) {
            gEeprom.FM_IsMrMode = false;
            gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
            return -1;
        }
        gEeprom.FM_SelectedChannel  = Channel;
        gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
    } else {
        gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
    }

    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
    return 0;
}

void FM_TurnOff(void)
{
    gFmRadioMode              = false;
    gFM_ScanState             = FM_SCAN_OFF;
    gFM_RestoreCountdown_10ms = 0;

    AUDIO_AudioPathOff();
    gEnableSpeaker = false;

    BK1080_Init0();
    BK4819_PickRXFilterPathBasedOnFrequency(gRxVfo->freq_config_RX.Frequency);

    gUpdateStatus  = true;

    #ifdef ENABLE_BOOT_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
    #endif
}

void FM_EraseChannels(void)
{
    Storage_SectorErase(REC_FM_CHANNELS);
    memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;

    gFmPlayCountdown_10ms = (gFM_ScanState == FM_SCAN_OFF) ? fm_play_countdown_noscan_10ms : fm_play_countdown_scan_10ms;

    gScheduleFM                 = false;
    gFM_FoundFrequency          = false;
    gAskToSave                  = false;
    gAskToDelete                = false;

    if (!bFlag) {
        uint16_t s = (gFmSpacing == 0) ? 20 : (gFmSpacing == 1) ? 10 : 5;
        Frequency += Step * s;
        if (Frequency < 6400)
            Frequency = 10800;
        else if (Frequency > 10800)
            Frequency = 6400;
    }

    gEeprom.FM_FrequencyPlaying = Frequency;
    gFM_ScanState = Step;

    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
}

void FM_PlayAndUpdate(void)
{
    gFM_ScanState = FM_SCAN_OFF;

    if (gFM_AutoScan) {
        gEeprom.FM_IsMrMode        = true;
        gEeprom.FM_SelectedChannel = 0;
    }

    FM_ConfigureChannelState();
    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
    SETTINGS_SaveFM();

    gFmPlayCountdown_10ms = 0;
    gScheduleFM           = false;
    gAskToSave            = false;

    AUDIO_AudioPathOn();
    gEnableSpeaker   = true;
}

int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
    int ret = -1;
    const uint16_t Test2 = BK1080_ReadRegister(BK1080_REG_07);
    const uint16_t Deviation = BK1080_REG_07_GET_FREQD(Test2);

    if (BK1080_REG_07_GET_SNR(Test2) <= gFmSeekSNRThreshold) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency      = Frequency;
        return ret;
    }

    const uint16_t Status = BK1080_ReadRegister(BK1080_REG_10);
    if ((Status & BK1080_REG_10_MASK_AFCRL) != BK1080_REG_10_AFCRL_NOT_RAILED || BK1080_REG_10_GET_RSSI(Status) < gFmSeekRSSIThreshold) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency      = Frequency;
        return ret;
    }

    if (Deviation >= 280 && Deviation <= 3815) {
        BK1080_FrequencyDeviation = Deviation;
        BK1080_BaseFrequency      = Frequency;
        return ret;
    }

    const uint16_t ABS_MIN = 6400;
    const uint16_t s = (gFmSpacing == 0) ? 20 : (gFmSpacing == 1) ? 10 : 5;
    if (Frequency > ABS_MIN && (Frequency - BK1080_BaseFrequency) == s) {
        if (BK1080_FrequencyDeviation & 0x800 || (BK1080_FrequencyDeviation < 20)) {
            BK1080_FrequencyDeviation = Deviation;
            BK1080_BaseFrequency      = Frequency;
            return ret;
        }
    }

    if (Frequency >= ABS_MIN && (BK1080_BaseFrequency - Frequency) == s) {
        if ((BK1080_FrequencyDeviation & 0x800) == 0 || (BK1080_FrequencyDeviation > 4075)) {
            BK1080_FrequencyDeviation = Deviation;
            BK1080_BaseFrequency      = Frequency;
            return ret;
        }
    }

    ret = 0;
    BK1080_FrequencyDeviation = Deviation;
    BK1080_BaseFrequency      = Frequency;
    return ret;
}

static void Key_DIGITS(KEY_Code_t Key, uint8_t state)
{
    enum { STATE_FREQ_MODE, STATE_MR_MODE, STATE_SAVE };

    // F-key logic: Long press acts as F+Key
    if (state == BUTTON_EVENT_LONG) {
        gWasFKeyPressed = true;
        Key_FUNC(Key, BUTTON_EVENT_SHORT);
        return;
    }

    if (state == BUTTON_EVENT_SHORT && !gWasFKeyPressed) {
        uint8_t State;

        if (gAskToDelete) {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }

        if (gAskToSave) State = STATE_SAVE;
        else {
            if (gFM_ScanState != FM_SCAN_OFF) {
                gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                return;
            }
            State = gEeprom.FM_IsMrMode ? STATE_MR_MODE : STATE_FREQ_MODE;
        }

        INPUTBOX_Append(Key);
        gRequestDisplayScreen = DISPLAY_FM;

        if (State == STATE_FREQ_MODE) {
            if (gInputBoxIndex == 1) {
                if (gInputBox[0] > 1) {
                    gInputBox[1] = gInputBox[0];
                    gInputBox[0] = 0;
                    gInputBoxIndex = 2;
                }
            }
            else if (gInputBoxIndex >= 4) {
                uint32_t Frequency;
                // Normalize 4-digit input (e.g. 1041 -> 10410)
                if (gInputBoxIndex == 4) INPUTBOX_Append(0);
                
                gInputBoxIndex = 0;
                Frequency = StrToUL(INPUTBOX_GetAscii());

                if (Frequency < 6400 || 10800 < Frequency) {
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                    return;
                }

                gEeprom.FM_SelectedFrequency = (uint16_t)Frequency;
                gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
                BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
                gRequestSaveFM = true;
                return;
            }
        }
        else if (gInputBoxIndex == 2) {
            uint8_t Channel;
            gInputBoxIndex = 0;
            Channel = ((gInputBox[0] * 10) + gInputBox[1]) - 1;

            if (State == STATE_MR_MODE) {
                if (FM_CheckValidChannel(Channel)) {
                    gEeprom.FM_SelectedChannel = Channel;
                    gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
                    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
                    gRequestSaveFM = true;
                    return;
                }
            }
            else if (Channel < FM_CHANNELS_MAX) {
                gFM_ChannelPosition = Channel;
                return;
            }
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        }
    }
    else
        Key_FUNC(Key, state);
}

static void Key_FUNC(KEY_Code_t Key, uint8_t state)
{
    if (state == BUTTON_EVENT_SHORT || state == BUTTON_EVENT_HELD) {
        bool fMode = gWasFKeyPressed;
        gWasFKeyPressed = false; // Reset F key after use

        gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
        gUpdateStatus         = true;
        gRequestDisplayScreen = DISPLAY_FM;

        switch (Key) {
            case KEY_0: ACTION_FM(); break;
            case KEY_1:
                if (fMode) {
                    gFmAudioProfile = (gFmAudioProfile + 1) % 4;
                    BK1080_SetAudioProfile(gFmAudioProfile);
                } else {
                    gEeprom.FM_Band++;
                    if(gEeprom.FM_Band > 3) gEeprom.FM_Band = 0;
                    gRequestSaveFM = true;
                }
                break;
            case KEY_2:
                if (fMode) {
                    gFmSoftMuteAttenuation = (gFmSoftMuteAttenuation + 1) % 4;
                    BK1080_SetSoftMute(gFmSoftMuteRate, gFmSoftMuteAttenuation);
                }
                break;
            case KEY_3:
                gEeprom.FM_IsMrMode = !gEeprom.FM_IsMrMode;
                if (!FM_ConfigureChannelState()) {
                    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
                    gRequestSaveFM = true;
                }
                else gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                break;
            case KEY_4:
                if (fMode) {
                    gFmSoftMuteRate = (gFmSoftMuteRate + 1) % 4;
                    BK1080_SetSoftMute(gFmSoftMuteRate, gFmSoftMuteAttenuation);
                }
                break;
            case KEY_5:
                if (fMode) {
                    gFmSeekRSSIThreshold = (gFmSeekRSSIThreshold + 5);
                    if (gFmSeekRSSIThreshold > 100) gFmSeekRSSIThreshold = 0;
                    BK1080_SetSeekThresholds(gFmSeekRSSIThreshold, gFmSeekSNRThreshold);
                }
                break;
            case KEY_6:
                if (fMode) {
                    gFmSeekSNRThreshold = (gFmSeekSNRThreshold + 1) % 16;
                    BK1080_SetSeekThresholds(gFmSeekRSSIThreshold, gFmSeekSNRThreshold);
                }
                break;
            case KEY_7:
                if (fMode) {
                    gFmSpacing = (gFmSpacing + 1) % 3; // 200k, 100k, 50k
                    uint16_t s = (gFmSpacing == 0) ? 20 : (gFmSpacing == 1) ? 10 : 5;
                    gEeprom.FM_FrequencyPlaying = (gEeprom.FM_FrequencyPlaying / s) * s;
                    gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
                    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
                    gRequestSaveFM = true;
                }
                break;
            case KEY_STAR:
                if (gFM_ScanState != FM_SCAN_OFF) {
                    gFM_AutoScan = false;
                    gFM_ScanState = FM_SCAN_OFF;
                    FM_PlayAndUpdate();
                } else {
                    gFM_AutoScan = (fMode || state == BUTTON_EVENT_HELD);
                    FM_Tune(gEeprom.FM_FrequencyPlaying, 1, false);
                }
                break;
            default:
                gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                break;
        }
    }
}

static void Key_EXIT(uint8_t state)
{
    if (state != BUTTON_EVENT_SHORT) return;
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    if (gFM_ScanState == FM_SCAN_OFF) {
        if (gInputBoxIndex == 0) {
            if (!gAskToSave && !gAskToDelete) {
                ACTION_FM();
                return;
            }
            gAskToSave   = false;
            gAskToDelete = false;
        }
        else {
            gInputBox[--gInputBoxIndex] = 10;
        }
    }
    else {
        FM_PlayAndUpdate();
    }
    gRequestDisplayScreen = DISPLAY_FM;
}

static void Key_MENU(uint8_t state)
{
    if (state != BUTTON_EVENT_SHORT) return;
    gRequestDisplayScreen = DISPLAY_FM;
    gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;

    if (gFM_ScanState == FM_SCAN_OFF) {
        if (!gEeprom.FM_IsMrMode) {
            if (gAskToSave) {
                gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
                gRequestSaveFM = true;
            }
            gAskToSave = !gAskToSave;
        }
        else {
            if (gAskToDelete) {
                gFM_Channels[gEeprom.FM_SelectedChannel] = 0xFFFF;
                FM_ConfigureChannelState();
                BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
                gRequestSaveFM = true;
            }
            gAskToDelete = !gAskToDelete;
        }
    }
    else {
        if (gFM_AutoScan || !gFM_FoundFrequency) {
            gBeepToPlay    = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            gInputBoxIndex = 0;
            return;
        }
        if (gAskToSave) {
            gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
            gRequestSaveFM = true;
        }
        gAskToSave = !gAskToSave;
    }
}

static void Key_UP_DOWN(uint8_t state, int8_t Step)
{
    if (state == BUTTON_EVENT_PRESSED) {
        if (gInputBoxIndex) {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    } else if (gInputBoxIndex || (state != BUTTON_EVENT_HELD && state != BUTTON_EVENT_PRESSED)) return;

    if (gAskToSave) {
        gRequestDisplayScreen = DISPLAY_FM;
        gFM_ChannelPosition   = NUMBER_AddWithWraparound(gFM_ChannelPosition, Step, 0, FM_CHANNELS_MAX - 1);
        return;
    }

    if (gFM_ScanState != FM_SCAN_OFF) {
        if (gFM_AutoScan) {
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }
        FM_Tune(gEeprom.FM_FrequencyPlaying, Step, false);
        gRequestDisplayScreen = DISPLAY_FM;
        return;
    }

    if (gEeprom.FM_IsMrMode) {
        const uint8_t Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel + Step, Step);
        if (Channel == 0xFF || gEeprom.FM_SelectedChannel == Channel) goto Bail;
        gEeprom.FM_SelectedChannel  = Channel;
        gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
    }
    else {
        // Global Continuous VFO Tuning (multiples of spacing)
        uint16_t s = (gFmSpacing == 0) ? 20 : (gFmSpacing == 1) ? 10 : 5;
        uint16_t Frequency = gEeprom.FM_SelectedFrequency + Step * s;
        if (Frequency < 6400) Frequency = 10800;
        else if (Frequency > 10800) Frequency = 6400;
        gEeprom.FM_FrequencyPlaying  = Frequency;
        gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
    }
    gRequestSaveFM = true;

Bail:
    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
    gRequestDisplayScreen = DISPLAY_FM;
}

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    uint8_t state = bKeyPressed + 2 * bKeyHeld;
    switch (Key) {
        case KEY_0...KEY_9: Key_DIGITS(Key, state); break;
        case KEY_STAR: Key_FUNC(Key, state); break;
        case KEY_MENU: Key_MENU(state); break;
        case KEY_UP: Key_UP_DOWN(state, (gEeprom.SET_NAV == 0) ? -1 : 1); break;
        case KEY_DOWN: Key_UP_DOWN(state, (gEeprom.SET_NAV == 0) ? 1 : -1); break;
        case KEY_EXIT: Key_EXIT(state); break;
        case KEY_F:
            if (bKeyPressed && !bKeyHeld) {
                gWasFKeyPressed = !gWasFKeyPressed;
                gUpdateStatus = true;
                gUpdateDisplay = true;
            }
            break;
        case KEY_PTT: GENERIC_Key_PTT(bKeyPressed); break;
        default: if (!bKeyHeld && bKeyPressed) gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL; break;
    }
}

void FM_Play(void)
{
    if (FM_CheckFrequencyLock(gEeprom.FM_FrequencyPlaying, 6400) == 0) {
        if (!gFM_AutoScan) {
            gFmPlayCountdown_10ms = 0;
            gFM_FoundFrequency    = true;
            if (!gEeprom.FM_IsMrMode) gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
            AUDIO_AudioPathOn();
            gEnableSpeaker = true;
            GUI_SelectNextDisplay(DISPLAY_FM);
            return;
        }
        if (gFM_ChannelPosition < FM_CHANNELS_MAX) gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;
        if (gFM_ChannelPosition >= FM_CHANNELS_MAX) { FM_PlayAndUpdate(); GUI_SelectNextDisplay(DISPLAY_FM); return; }
    }

    if (gFM_AutoScan && gEeprom.FM_FrequencyPlaying >= 10800) FM_PlayAndUpdate();
    else FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_ScanState, false);
    GUI_SelectNextDisplay(DISPLAY_FM);
}

void FM_CheckAutoMute(void)
{
    if (!gFmRadioMode) return;
    if (gFM_ScanState == FM_SCAN_OFF) {
        if (++s_fm_update_tick >= 2) { s_fm_update_tick = 0; gUpdateDisplay = true; gUpdateStatus = true; }
    }
    if (gFM_ScanState != FM_SCAN_OFF) return;

    if (g_SquelchLost) {
        if (!gFM_AutoMuted) { BK1080_Mute(true); AUDIO_AudioPathOff(); gFM_AutoMuted = true; gRequestDisplayScreen = DISPLAY_MAIN; }
        gFmAutoMuteCountdown_10ms = 200;
    } else if (gFM_AutoMuted) {
        if (gFmAutoMuteCountdown_10ms == 0) {
            gFM_AutoMuted = false; BK1080_Mute(false); AUDIO_AudioPathOn();
            gRequestDisplayScreen = DISPLAY_FM; gUpdateDisplay = true;
        }
    }
}

void FM_Start(void)
{
    gDualWatchActive          = false;
    gFmRadioMode              = true;
    gFM_ScanState             = FM_SCAN_OFF;
    gFM_RestoreCountdown_10ms = 0;

    BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band);
    BK4819_PickRXFilterPathBasedOnFrequency(10320000); 
    
    BK1080_SetAudioProfile(gFmAudioProfile);
    BK1080_SetSoftMute(gFmSoftMuteRate, gFmSoftMuteAttenuation);
    BK1080_SetSeekThresholds(gFmSeekRSSIThreshold, gFmSeekSNRThreshold);
    BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band, gFmSpacing);
    BK1080_SetVolume(11);

    AUDIO_AudioPathOn();
    gEnableSpeaker       = true;
    gUpdateStatus        = true;

    #ifdef ENABLE_BOOT_RESUME_STATE
        gEeprom.CURRENT_STATE = 3;
        SETTINGS_WriteCurrentState();
    #endif
}

#endif
