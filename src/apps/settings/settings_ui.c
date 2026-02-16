
#include "settings_ui.h"
#include "../../ui/ag_menu.h"
#include "../../drivers/bsp/st7565.h"
#include "../../external/printf/printf.h"
#include "../../apps/settings/settings.h"
#include "../../ui/menu.h"
#include "../../ui/ui.h"
#include "../../drivers/bsp/bk4819.h"
#include "../../core/misc.h"
#include "../../frequencies.h"
#include "../../audio.h"
#include "../../dcs.h" // For CTCSS/DCS tables
#include "../../dcs.h" // For CTCSS/DCS tables
#include "../../ui/helper.h" // For frequency helpers
#ifdef ENABLE_EEPROM_HEXDUMP
#include "../../ui/hexdump.h"
#include "../../drivers/bsp/i2c.h" // For EEPROM_ReadBuffer if available or define local
// Usually standard EEPROM read is I2C_ReadBuffer or similar.
// Let's assume EEPROM_ReadBuffer exists as in user example or define a wrapper.
extern void EEPROM_ReadBuffer(uint32_t Address, uint8_t *pBuffer, uint32_t Size);
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#define INC_DEC(val, min, max, inc) do { \
    if (inc) { if (val < (max)) val++; else val = (min); } \
    else     { if (val > (min)) val--; else val = (max); } \
} while(0)

// Reference to DCS/CTCSS tables
// They are extern in dcs.h

// --- Helper Functions ---



static void Settings_GetValueStr(uint8_t settingId, char *buf, uint8_t bufLen) {
    if (!buf) return;
    buf[0] = '\0';

    switch (settingId) {
        // --- Sound ---
        case MENU_SQL:
            snprintf(buf, bufLen, "%d", gEeprom.SQUELCH_LEVEL);
            break;
        case MENU_BEEP:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gEeprom.BEEP_CONTROL]);
            break;
        case MENU_ROGER:
            snprintf(buf, bufLen, "%s", gSubMenu_ROGER[gEeprom.ROGER]);
            break;
        case MENU_VOX:
            if (!gEeprom.VOX_SWITCH) snprintf(buf, bufLen, "OFF");
            else snprintf(buf, bufLen, "%d", gEeprom.VOX_LEVEL);
            break;
        case MENU_MIC:
            snprintf(buf, bufLen, "+%u.%udB", gMicGain_dB2[gEeprom.MIC_SENSITIVITY] / 2, gMicGain_dB2[gEeprom.MIC_SENSITIVITY] % 2);
            break;
        #ifdef ENABLE_MIC_BAR
        case MENU_MIC_BAR:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_mic_bar]);
            break;
        #endif
        #ifdef ENABLE_VOICE
        case MENU_VOICE:
             snprintf(buf, bufLen, "%s", gSubMenu_VOICE[gEeprom.VOICE_PROMPT]);
             break;
        #endif
        case MENU_STE:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gEeprom.TAIL_TONE_ELIMINATION]);
            break;
        case MENU_RP_STE:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gEeprom.REPEATER_TAIL_TONE_ELIMINATION > 0]);
            break;
        case MENU_1_CALL:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gEeprom.CHAN_1_CALL != 0]); // Simplistic
            break;
        #ifdef ENABLE_ALARM
        case MENU_AL_MOD:
            snprintf(buf, bufLen, "%s", gSubMenu_AL_MOD[gEeprom.ALARM_MODE]);
            break;
        #endif

        // --- Display ---
        case MENU_SET_AUD:
            snprintf(buf, bufLen, "%s", gSubMenu_SET_AUD[gSetting_set_audio]);
            break;
        case MENU_ABR:
            if (gEeprom.BACKLIGHT_TIME == 0) snprintf(buf, bufLen, "OFF");
            else if (gEeprom.BACKLIGHT_TIME >= 61) snprintf(buf, bufLen, "ON");
            else snprintf(buf, bufLen, "%ds", gEeprom.BACKLIGHT_TIME * 5);
            break;
        case MENU_ABR_MAX:
            snprintf(buf, bufLen, "%d", gEeprom.BACKLIGHT_MAX);
            break;
        case MENU_ABR_MIN:
            snprintf(buf, bufLen, "%d", gEeprom.BACKLIGHT_MIN);
            break;
        case MENU_MDF:
            snprintf(buf, bufLen, "%s", gSubMenu_MDF[gEeprom.CHANNEL_DISPLAY_MODE]);
            break;
        case MENU_BAT_TXT:
             snprintf(buf, bufLen, "%s", gSubMenu_BAT_TXT[gSetting_battery_text]);
             break;
        case MENU_PONMSG:
             snprintf(buf, bufLen, "%s", gSubMenu_PONMSG[gEeprom.POWER_ON_DISPLAY_MODE]);
             break;
        case MENU_ABR_ON_TX_RX:
             snprintf(buf, bufLen, "%s", gSubMenu_RX_TX[gSetting_backlight_on_tx_rx]);
             break;
         #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        case MENU_SET_CTR:
             snprintf(buf, bufLen, "%d", gSetting_set_ctr);
             break;
        case MENU_SET_INV:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_set_inv]);
             break;
        case MENU_SET_LCK:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_LCK[gSetting_set_lck]);
             break;
        case MENU_SET_TMR:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_set_tmr]);
             break;
        #ifdef ENABLE_NARROWER_BW_FILTER
        case MENU_SET_NFM:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_NFM[gSetting_set_nfm]);
             break;
        #endif
        case MENU_SET_PWR:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_PWR[gSetting_set_pwr]);
             break;
        case MENU_SET_PTT:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_PTT[gSetting_set_ptt]);
             break;
        case MENU_SET_TOT:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_TOT[gSetting_set_tot]);
             break;
        case MENU_SET_EOT:
             snprintf(buf, bufLen, "%s", gSubMenu_SET_TOT[gSetting_set_eot]);
             break;
         #endif
        #ifdef ENABLE_DEEP_SLEEP_MODE
        case MENU_SET_OFF:
             if (gSetting_set_off == 0) snprintf(buf, bufLen, "OFF");
             else snprintf(buf, bufLen, "%d min", gSetting_set_off);
             break;
        #endif
        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
             {
                 uint8_t val = 0;
                 if (settingId == MENU_F1SHRT)      val = gEeprom.KEY_1_SHORT_PRESS_ACTION;
                 else if (settingId == MENU_F1LONG) val = gEeprom.KEY_1_LONG_PRESS_ACTION;
                 else if (settingId == MENU_F2SHRT) val = gEeprom.KEY_2_SHORT_PRESS_ACTION;
                 else if (settingId == MENU_F2LONG) val = gEeprom.KEY_2_LONG_PRESS_ACTION;
                 else if (settingId == MENU_MLONG)  val = gEeprom.KEY_M_LONG_PRESS_ACTION;
                 
                 // Find name by ID in gSubMenu_SIDEFUNCTIONS
                 const char* name = "NONE";
                 for(int i=0; i<gSubMenu_SIDEFUNCTIONS_size; i++) {
                     if(gSubMenu_SIDEFUNCTIONS[i].id == val) {
                         name = gSubMenu_SIDEFUNCTIONS[i].name;
                         break;
                     }
                 }
                 snprintf(buf, bufLen, "%s", name);
             }
             break;

        // --- Radio ---
        case MENU_STEP:
            {
               uint16_t step = gStepFrequencyTable[gTxVfo->STEP_SETTING];
               snprintf(buf, bufLen, "%d.%02u", step / 100, step % 100);
            }
            break;
        case MENU_W_N:
             snprintf(buf, bufLen, "%s", gSubMenu_W_N[gTxVfo->CHANNEL_BANDWIDTH]);
             break;
        case MENU_TXP:
             snprintf(buf, bufLen, "%s", gSubMenu_TXP[gTxVfo->OUTPUT_POWER]);
             break;
        case MENU_SFT_D:
             snprintf(buf, bufLen, "%s", gSubMenu_SFT_D[gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION]);
             break;
        case MENU_OFFSET:
             {
                 uint32_t off = gTxVfo->TX_OFFSET_FREQUENCY;
                 snprintf(buf, bufLen, "%lu.%05lu", off / 100000, off % 100000);
             }
             break;
        case MENU_BCL:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gTxVfo->BUSY_CHANNEL_LOCK]);
             break;
        case MENU_AM:
             snprintf(buf, bufLen, "%s", gModulationStr[gTxVfo->Modulation]);
             break;
        case MENU_SC_REV:
             if (gEeprom.SCAN_RESUME_MODE < 3) {
                 const char* modes[] = {"TO", "CO", "SE"};
                 snprintf(buf, bufLen, "%s", modes[gEeprom.SCAN_RESUME_MODE]);
             } else {
                 snprintf(buf, bufLen, "TIME");
             }
             break;
        case MENU_R_DCS:
        case MENU_T_DCS:
            {
                uint8_t code = (settingId == MENU_R_DCS) ? gTxVfo->pRX->Code : gTxVfo->pTX->Code;
                uint8_t type = (settingId == MENU_R_DCS) ? gTxVfo->pRX->CodeType : gTxVfo->pTX->CodeType;
                
                if (type != CODE_TYPE_DIGITAL && type != CODE_TYPE_REVERSE_DIGITAL) snprintf(buf, bufLen, "OFF");
                else {
                   snprintf(buf, bufLen, "D%03o%c", DCS_Options[code], type == CODE_TYPE_REVERSE_DIGITAL ? 'I' : 'N');
                }
            }
            break;
        case MENU_R_CTCS:
        case MENU_T_CTCS:
            {
                 uint8_t code = (settingId == MENU_R_CTCS) ? gTxVfo->pRX->Code : gTxVfo->pTX->Code;
                 uint8_t type = (settingId == MENU_R_CTCS) ? gTxVfo->pRX->CodeType : gTxVfo->pTX->CodeType;
                 
                 if (type != CODE_TYPE_CONTINUOUS_TONE) snprintf(buf, bufLen, "OFF");
                 else snprintf(buf, bufLen, "%u.%uHz", CTCSS_Options[code] / 10, CTCSS_Options[code] % 10);
            }
            break;
        case MENU_SCR:
             if (gSetting_ScrambleEnable && gTxVfo->SCRAMBLING_TYPE > 0 && gTxVfo->SCRAMBLING_TYPE <= 10) 
                snprintf(buf, bufLen, "%s", gSubMenu_SCRAMBLER[gTxVfo->SCRAMBLING_TYPE]);
             else snprintf(buf, bufLen, "OFF");
             break;
        case MENU_COMPAND:
             snprintf(buf, bufLen, "%s", gSubMenu_RX_TX[gTxVfo->Compander]);
             break;
        #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        case MENU_TX_LOCK:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_F_LOCK == F_LOCK_ALL]); // Simplified
             break;
        case MENU_350EN:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_350EN]);
             break;
        #endif

        // --- System ---
        case MENU_TOT:
             snprintf(buf, bufLen, "%ds", (gEeprom.TX_TIMEOUT_TIMER + 1) * 15);
             break;
        case MENU_AUTOLK:
             if (gEeprom.AUTO_KEYPAD_LOCK) snprintf(buf, bufLen, "%ds", gEeprom.AUTO_KEYPAD_LOCK * 15);
             else snprintf(buf, bufLen, "OFF");
             break;
        case MENU_TDR:
             // Logic repeated from original
             if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF && gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) snprintf(buf, bufLen, "OFF");
             else if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) snprintf(buf, bufLen, "CROSS");
             else snprintf(buf, bufLen, "CHAN %c", gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_A ? 'A' : 'B');
             break;
        case MENU_SAVE:
             if (gEeprom.BATTERY_SAVE == 0) snprintf(buf, bufLen, "OFF");
             else snprintf(buf, bufLen, "1:%d", gEeprom.BATTERY_SAVE);
             break;
        case MENU_BATTYP:
             snprintf(buf, bufLen, "%s", gSubMenu_BATTYP[gEeprom.BATTERY_TYPE]);
             break;
        case MENU_SET_NAV:
             {
                 const char* modes[] = {"K1 (L/R)", "K5 (U/D)"};
                 snprintf(buf, bufLen, "%s", modes[gEeprom.SET_NAV]);
             }
             break;

        // --- DTMF ---
         case MENU_UPCODE:
            snprintf(buf, bufLen, "%.8s", gEeprom.DTMF_UP_CODE);
            break;
         case MENU_DWCODE:
            snprintf(buf, bufLen, "%.8s", gEeprom.DTMF_DOWN_CODE);
            break;
         case MENU_PTT_ID:
            snprintf(buf, bufLen, "%s", gSubMenu_PTT_ID[gTxVfo->DTMF_PTT_ID_TX_MODE]);
            break;
         case MENU_D_ST:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gEeprom.DTMF_SIDE_TONE]);
            break;
         #ifdef ENABLE_DTMF_CALLING
         case MENU_ANI_ID:
            snprintf(buf, bufLen, "%.8s", gEeprom.ANI_DTMF_ID);
            break;
         case MENU_D_RSP:
            snprintf(buf, bufLen, "%s", gSubMenu_D_RSP[gEeprom.DTMF_DECODE_RESPONSE]);
            break;
         case MENU_D_HOLD:
            snprintf(buf, bufLen, "%ds", gEeprom.DTMF_auto_reset_time);
            break;
         case MENU_D_PRE:
            snprintf(buf, bufLen, "%dms", gEeprom.DTMF_PRELOAD_TIME);
            break;
         case MENU_D_DCD:
            snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gTxVfo->DTMF_DECODING_ENABLE]);
            break;
         #endif
         case MENU_D_LIVE_DEC:
             snprintf(buf, bufLen, "%s", gSubMenu_OFF_ON[gSetting_live_DTMF_decoder]);
             break;



        // Custom Actions
        #ifdef ENABLE_EEPROM_HEXDUMP
        case MENU_MEMVIEW: // ID for MemView
             // This logic needs to be in Action handler not UpdateValue.
             // Wait, Settings_UpdateValue is called for changeVal (Left/Right/Select on options).
             // Actions (Enter key on purely action items) usually handled in menu system or specialized.
             // settings_ui.c uses generic menu system.
             // We need to intercept the Action for this item.
             // But existing items (e.g. Nav Layout) use changeVal for toggling.
             // Items with M_ITEM_ACTION usually trigger something.
             // Root menu items open submenus.
             // Let's see how submenus are opened relative to this.
             // Menu system handles submenu opening.
             // For custom screen, we might need a custom callback or global handler.
             // `changeVal` is for value modification.
             // `action` in menu struct is null for items.
             break;
        #endif
    }
}

static void Settings_UpdateValue(uint8_t settingId, bool up) {
    switch (settingId) {
        // --- Sound ---
        case MENU_SET_AUD:
            INC_DEC(gSetting_set_audio, 0, 4, up);
            RADIO_SetModulation(gRxVfo->Modulation);
            break;
        case MENU_SQL:
            INC_DEC(gEeprom.SQUELCH_LEVEL, 0, 9, up);
            break;
        case MENU_BEEP:
             gEeprom.BEEP_CONTROL = !gEeprom.BEEP_CONTROL;
             break;
        case MENU_ROGER:
             INC_DEC(gEeprom.ROGER, 0, ROGER_MODE_MDC, up);
             break;
        case MENU_VOX:
            if (up) {
                if (!gEeprom.VOX_SWITCH) { gEeprom.VOX_SWITCH = true; gEeprom.VOX_LEVEL = 1; }
                else if (gEeprom.VOX_LEVEL < 9) gEeprom.VOX_LEVEL++;
                else gEeprom.VOX_SWITCH = false;
            } else {
                if (!gEeprom.VOX_SWITCH) { gEeprom.VOX_SWITCH = true; gEeprom.VOX_LEVEL = 9; }
                else if (gEeprom.VOX_LEVEL > 1) gEeprom.VOX_LEVEL--;
                else gEeprom.VOX_SWITCH = false;
            }
            break;
        case MENU_MIC:
            INC_DEC(gEeprom.MIC_SENSITIVITY, 0, 4, up);
            break;
        #ifdef ENABLE_MIC_BAR
        case MENU_MIC_BAR:
            gSetting_mic_bar = !gSetting_mic_bar;
            break;
        #endif
        #ifdef ENABLE_VOICE
        case MENU_VOICE:
             INC_DEC(gEeprom.VOICE_PROMPT, 0, 2, up);
             break;
        #endif
        case MENU_STE:
             gEeprom.TAIL_TONE_ELIMINATION = !gEeprom.TAIL_TONE_ELIMINATION;
             break;
        case MENU_RP_STE:
             INC_DEC(gEeprom.REPEATER_TAIL_TONE_ELIMINATION, 0, 10, up); // 0-10
             break;
        #ifdef ENABLE_ALARM
        case MENU_AL_MOD:
             gEeprom.ALARM_MODE = !gEeprom.ALARM_MODE;
             break;
        #endif

        // --- Display ---
        case MENU_ABR:
             INC_DEC(gEeprom.BACKLIGHT_TIME, 0, 61, up);
             if (gEeprom.BACKLIGHT_TIME < 61) BACKLIGHT_TurnOn();
             break;
        case MENU_ABR_MAX:
             INC_DEC(gEeprom.BACKLIGHT_MAX, 1, 10, up);
             if (gEeprom.BACKLIGHT_MIN >= gEeprom.BACKLIGHT_MAX) gEeprom.BACKLIGHT_MIN = gEeprom.BACKLIGHT_MAX - 1;
             BACKLIGHT_TurnOn();
             break;
        case MENU_ABR_MIN:
             INC_DEC(gEeprom.BACKLIGHT_MIN, 0, 9, up);
             if (gEeprom.BACKLIGHT_MIN >= gEeprom.BACKLIGHT_MAX) gEeprom.BACKLIGHT_MAX = gEeprom.BACKLIGHT_MIN + 1;
             BACKLIGHT_TurnOn();
             break;
        case MENU_MDF:
             INC_DEC(gEeprom.CHANNEL_DISPLAY_MODE, 0, 3, up);
             break;
        case MENU_BAT_TXT:
             INC_DEC(gSetting_battery_text, 0, 7, up); // 0-7
             break;
        case MENU_PONMSG:
             INC_DEC(gEeprom.POWER_ON_DISPLAY_MODE, 0, ARRAY_SIZE(gSubMenu_PONMSG)-1, up);
             break;
        case MENU_ABR_ON_TX_RX:
             INC_DEC(gSetting_backlight_on_tx_rx, 0, 3, up);
             break;
        #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
        case MENU_SET_CTR:
             INC_DEC(gSetting_set_ctr, 0, 15, up);
             ST7565_ContrastAndInv();
             break;
        case MENU_SET_INV:
             gSetting_set_inv = !gSetting_set_inv;
             ST7565_ContrastAndInv();
             break;
        case MENU_SET_LCK:
             gSetting_set_lck = !gSetting_set_lck;
             break;
        case MENU_SET_TMR:
             gSetting_set_tmr = !gSetting_set_tmr;
             break;
        #ifdef ENABLE_NARROWER_BW_FILTER
        case MENU_SET_NFM:
             gSetting_set_nfm = !gSetting_set_nfm;
             break;
        #endif
        case MENU_SET_PWR:
             INC_DEC(gSetting_set_pwr, 0, ARRAY_SIZE(gSubMenu_SET_PWR) - 1, up);
             break;
        case MENU_SET_PTT:
             INC_DEC(gSetting_set_ptt, 0, ARRAY_SIZE(gSubMenu_SET_PTT) - 1, up);
             gSetting_set_ptt_session = gSetting_set_ptt;
             break;
        case MENU_SET_TOT:
             INC_DEC(gSetting_set_tot, 0, ARRAY_SIZE(gSubMenu_SET_TOT) - 1, up);
             break;
        case MENU_SET_EOT:
             INC_DEC(gSetting_set_eot, 0, ARRAY_SIZE(gSubMenu_SET_TOT) - 1, up);
             break;
        #endif
        #ifdef ENABLE_DEEP_SLEEP_MODE
        case MENU_SET_OFF:
             INC_DEC(gSetting_set_off, 0, 120, up);
             break;
        #endif
        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
             {
                 uint8_t *val = NULL;
                 if (settingId == MENU_F1SHRT)      val = &gEeprom.KEY_1_SHORT_PRESS_ACTION;
                 else if (settingId == MENU_F1LONG) val = &gEeprom.KEY_1_LONG_PRESS_ACTION;
                 else if (settingId == MENU_F2SHRT) val = &gEeprom.KEY_2_SHORT_PRESS_ACTION;
                 else if (settingId == MENU_F2LONG) val = &gEeprom.KEY_2_LONG_PRESS_ACTION;
                 else if (settingId == MENU_MLONG)  val = &gEeprom.KEY_M_LONG_PRESS_ACTION;
                 
                 if (val) {
                     uint8_t currentId = *val;
                     int idx = 0;
                     for(int i=0; i<gSubMenu_SIDEFUNCTIONS_size; i++) {
                         if(gSubMenu_SIDEFUNCTIONS[i].id == currentId) {
                             idx = i;
                             break;
                         }
                     }
                     INC_DEC(idx, 0, gSubMenu_SIDEFUNCTIONS_size - 1, up);
                     *val = gSubMenu_SIDEFUNCTIONS[idx].id;
                 }
             }
             break;

        // --- Radio ---
        case MENU_STEP:
             {
                 uint8_t idx = gTxVfo->STEP_SETTING;
                 INC_DEC(idx, 0, STEP_N_ELEM - 1, up);
                 gTxVfo->STEP_SETTING = idx;
             }
             break;
        case MENU_W_N:
             INC_DEC(gTxVfo->CHANNEL_BANDWIDTH, 0, 1, up);
             break;
        case MENU_TXP:
             INC_DEC(gTxVfo->OUTPUT_POWER, 0, 2, up);
             break;
        case MENU_SFT_D:
             INC_DEC(gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION, 0, 2, up);
             break;
        case MENU_OFFSET:
             if (up) gTxVfo->TX_OFFSET_FREQUENCY += 10000;
             else if (gTxVfo->TX_OFFSET_FREQUENCY >= 10000) gTxVfo->TX_OFFSET_FREQUENCY -= 10000;
             break;
        case MENU_BCL:
             gTxVfo->BUSY_CHANNEL_LOCK = !gTxVfo->BUSY_CHANNEL_LOCK;
             break;
        case MENU_AM:
             INC_DEC(gTxVfo->Modulation, 0, MODULATION_UKNOWN - 1, up);
             break;
        case MENU_SC_REV:
             INC_DEC(gEeprom.SCAN_RESUME_MODE, 0, 2, up);
             break;
        case MENU_COMPAND:
             INC_DEC(gTxVfo->Compander, 0, 3, up);
             break;
#ifdef ENABLE_SCRAMBLER
        case MENU_SCR:
             INC_DEC(gTxVfo->SCRAMBLING_TYPE, 0, 10, up);
             gSetting_ScrambleEnable = (gTxVfo->SCRAMBLING_TYPE > 0);
             if (gTxVfo->Modulation == MODULATION_FM) {
                 if (gSetting_ScrambleEnable)
                     BK4819_EnableScramble(gTxVfo->SCRAMBLING_TYPE - 1);
                 else
                     BK4819_DisableScramble();
             }
             break;
#endif

        case MENU_R_DCS:
        case MENU_T_DCS:
             {
                 uint8_t *type = (settingId == MENU_R_DCS) ? &gTxVfo->pRX->CodeType : &gTxVfo->pTX->CodeType;
                 uint8_t *code = (settingId == MENU_R_DCS) ? &gTxVfo->pRX->Code : &gTxVfo->pTX->Code;
                 
                 // DCS Cycle: OFF -> DxxxN -> DxxxI -> OFF
                 if (*type == CODE_TYPE_OFF) {
                     if(up) { *type = CODE_TYPE_DIGITAL; *code = 0; }
                     else   { *type = CODE_TYPE_REVERSE_DIGITAL; *code = 103; }
                 } else if (*type == CODE_TYPE_DIGITAL) {
                     if (up) {
                         if (*code < 103) (*code)++;
                         else { *type = CODE_TYPE_REVERSE_DIGITAL; *code = 0; }
                     } else {
                         if (*code > 0) (*code)--;
                         else { *type = CODE_TYPE_OFF; }
                     }
                 } else if (*type == CODE_TYPE_REVERSE_DIGITAL) {
                     if (up) {
                         if (*code < 103) (*code)++;
                         else { *type = CODE_TYPE_OFF; }
                     } else {
                         if (*code > 0) (*code)--;
                         else { *type = CODE_TYPE_DIGITAL; *code = 103; }
                     }
                 } else {
                     // If currently CTCSS, switch to DCS
                     *type = CODE_TYPE_DIGITAL; *code = 0;
                 }
                 
                 // Apply to hardware if needed (saving happens at end)
             }
             break;
        case MENU_R_CTCS:
        case MENU_T_CTCS:
             {
                 uint8_t *type = (settingId == MENU_R_CTCS) ? &gTxVfo->pRX->CodeType : &gTxVfo->pTX->CodeType;
                 uint8_t *code = (settingId == MENU_R_CTCS) ? &gTxVfo->pRX->Code : &gTxVfo->pTX->Code;

                 // CTCSS Cycle: OFF -> 67.0 -> ... -> 254.1 -> OFF
                 if (*type != CODE_TYPE_CONTINUOUS_TONE) {
                     if(up) { *type = CODE_TYPE_CONTINUOUS_TONE; *code = 0; }
                     else   { *type = CODE_TYPE_CONTINUOUS_TONE; *code = 49; }
                 } else {
                     if (up) {
                         if (*code < 49) (*code)++;
                         else *type = CODE_TYPE_OFF;
                     } else {
                         if (*code > 0) (*code)--;
                         else *type = CODE_TYPE_OFF;
                     }
                 }
             }
             break;

        // --- System ---
        case MENU_TOT:
             INC_DEC(gEeprom.TX_TIMEOUT_TIMER, 0, 11, up);
             break;
        case MENU_AUTOLK:
             INC_DEC(gEeprom.AUTO_KEYPAD_LOCK, 0, 40, up);
             break;
        case MENU_TDR:
              {
                  uint8_t state = 0;
                  if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) state = 3;
                  else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_B) state = 2;
                  else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_A) state = 1;
                  else state = 0;
                  INC_DEC(state, 0, 3, up);
                  gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
                  gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;
                  if (state == 1) gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_A;
                  else if (state == 2) gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_B;
                  else if (state == 3) gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_CHAN_B; 
              }
              break;
         case MENU_SAVE:
              INC_DEC(gEeprom.BATTERY_SAVE, 0, 4, up);
              break;
         case MENU_BATTYP:
              INC_DEC(gEeprom.BATTERY_TYPE, 0, 4, up);
              break;
         case MENU_SET_NAV:
              gEeprom.SET_NAV = !gEeprom.SET_NAV;
              break;

        // --- DTMF ---
         case MENU_PTT_ID:
              INC_DEC(gTxVfo->DTMF_PTT_ID_TX_MODE, 0, ARRAY_SIZE(gSubMenu_PTT_ID)-1, up);
              break;
         case MENU_D_ST:
              gEeprom.DTMF_SIDE_TONE = !gEeprom.DTMF_SIDE_TONE;
              break;
         #ifdef ENABLE_DTMF_CALLING
         case MENU_D_RSP:
              INC_DEC(gEeprom.DTMF_DECODE_RESPONSE, 0, ARRAY_SIZE(gSubMenu_D_RSP)-1, up);
              break;
         case MENU_D_HOLD:
              INC_DEC(gEeprom.DTMF_auto_reset_time, 5, 60, up);
              break;
         case MENU_D_PRE:
              {
                 uint16_t val = gEeprom.DTMF_PRELOAD_TIME / 10;
                 INC_DEC(val, 3, 99, up);
                 gEeprom.DTMF_PRELOAD_TIME = val * 10;
              }
              break;
         case MENU_D_DCD:
              gTxVfo->DTMF_DECODING_ENABLE = !gTxVfo->DTMF_DECODING_ENABLE;
              break;
         #endif
         case MENU_D_LIVE_DEC:
              gSetting_live_DTMF_decoder = !gSetting_live_DTMF_decoder;
              break;

        default:
             break;
    }
    
    // Live Save
    SETTINGS_SaveSettings();
    SETTINGS_SaveVfoIndices(); 
}


// --- Menu Item Callbacks ---
static void getVal(const MenuItem *item, char *buf, uint8_t buf_size) {
    Settings_GetValueStr(item->setting, buf, buf_size);
}
static void changeVal(const MenuItem *item, bool up) {
    Settings_UpdateValue(item->setting, up);
}

#ifdef ENABLE_EEPROM_HEXDUMP
static bool Action_MemView(const MenuItem *item, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key == KEY_MENU && key_pressed) {
        GUI_SelectNextDisplay(DISPLAY_HEXDUMP);
        return true;
    }
    return false;
}
#endif

// --- Menu Definitions ---

// Sound
static const MenuItem soundItems[] = {
    {"Squelch", MENU_SQL, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Key Beep", MENU_BEEP, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Roger", MENU_ROGER, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"VOX", MENU_VOX, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Mic Sens", MENU_MIC, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_MIC_BAR
    {"Mic Bar", MENU_MIC_BAR, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
    #ifdef ENABLE_VOICE
    {"Voice", MENU_VOICE, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #endif
    {"Tail Tone", MENU_STE, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Repeater Tone", MENU_RP_STE, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"1 Call", MENU_1_CALL, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
#ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Rx FM Audio", MENU_SET_AUD, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
#endif
    #ifdef ENABLE_ALARM
    {"Alarm", MENU_AL_MOD, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #endif
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Tail Alert",  MENU_SET_EOT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Beep Timer",    MENU_SET_TMR, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
};
static Menu soundMenu = {
    .title = "Audio", .items = soundItems, .num_items = ARRAY_SIZE(soundItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// Display
static const MenuItem displayItems[] = {
    {"Backlight Time", MENU_ABR, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Backlight Max", MENU_ABR_MAX, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Backlight Min", MENU_ABR_MIN, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Backlight Tx/Rx", MENU_ABR_ON_TX_RX, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Channel Label", MENU_MDF, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Battery Text", MENU_BAT_TXT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Power On Text", MENU_PONMSG, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Contrast", MENU_SET_CTR, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Invert", MENU_SET_INV, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
};
static Menu displayMenu = {
    .title = "Display", .items = displayItems, .num_items = ARRAY_SIZE(displayItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// Radio
static const MenuItem radioItems[] = {
    {"Step", MENU_STEP, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Bandwidth", MENU_W_N, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Power", MENU_TXP, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Rx DCS", MENU_R_DCS, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Rx CTCS", MENU_R_CTCS, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Tx DCS", MENU_T_DCS, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Tx CTCS", MENU_T_CTCS, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
#ifdef ENABLE_TX_OFFSET
    {"Offset Dir", MENU_SFT_D, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Offset Freq", MENU_OFFSET, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
#endif
    {"Busy Channel Lock", MENU_BCL, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Modulation", MENU_AM, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Scan Resume", MENU_SC_REV, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Compander", MENU_COMPAND, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
#ifdef ENABLE_SCRAMBLER
    {"Scrambler", MENU_SCR, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
#endif
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Tx Lock", MENU_TX_LOCK, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"350 En", MENU_350EN, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Power Logic", MENU_SET_PWR, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_NARROWER_BW_FILTER
    {"NFM Filter",  MENU_SET_NFM, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
    #endif
};
static Menu radioMenu = {
    .title = "Radio", .items = radioItems, .num_items = ARRAY_SIZE(radioItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// DTMF
static const MenuItem dtmfItems[] = {
    {"PTT ID", MENU_PTT_ID, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Side Tone", MENU_D_ST, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Live Dec", MENU_D_LIVE_DEC, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #ifdef ENABLE_DTMF_CALLING
    {"ANI ID", MENU_ANI_ID, getVal, NULL, NULL, NULL, M_ITEM_ACTION},
    {"Response", MENU_D_RSP, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Reset Time", MENU_D_HOLD, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Preload", MENU_D_PRE, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Decode", MENU_D_DCD, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
    {"Up Code", MENU_UPCODE, getVal, NULL, NULL, NULL, M_ITEM_ACTION},
    {"Dw Code", MENU_DWCODE, getVal, NULL, NULL, NULL, M_ITEM_ACTION},
};
static Menu dtmfMenu = {
    .title = "DTMF", .items = dtmfItems, .num_items = ARRAY_SIZE(dtmfItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// System
static const MenuItem systemItems[] = {
    {"Tx Timeout", MENU_TOT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Tx TO Alert", MENU_SET_TOT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #endif
    {"Auto Lock", MENU_AUTOLK, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Lock Mode",    MENU_SET_LCK, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
    {"Dual Watch", MENU_TDR, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Bat Save", MENU_SAVE, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"Bat Type", MENU_BATTYP, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #ifdef ENABLE_DEEP_SLEEP_MODE
    {"Deep Sleep",  MENU_SET_OFF, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    #endif
    #ifdef ENABLE_EEPROM_HEXDUMP
    {"Mem Hex Dump", MENU_MEMVIEW, NULL, NULL, NULL, Action_MemView, M_ITEM_ACTION},
    #endif
};
static Menu systemMenu = {
    .title = "System", .items = systemItems, .num_items = ARRAY_SIZE(systemItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// Buttons
static const MenuItem buttonItems[] = {
    #ifdef ENABLE_CUSTOM_FIRMWARE_MODS
    {"Push to Talk", MENU_SET_PTT, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    {"Nav Layout", MENU_SET_NAV, getVal, changeVal, NULL, NULL, M_ITEM_ACTION},
    #endif
    {"F1 Short",    MENU_F1SHRT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"F1 Long",     MENU_F1LONG, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"F2 Short",    MENU_F2SHRT, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"F2 Long",     MENU_F2LONG, getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
    {"M Long",      MENU_MLONG,  getVal, changeVal, NULL, NULL, M_ITEM_SELECT},
};
static Menu buttonMenu = {
    .title = "Buttons", .items = buttonItems, .num_items = ARRAY_SIZE(buttonItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

// Main
static const MenuItem rootItems[] = {
    {"Radio", 0, NULL, NULL, &radioMenu, NULL, M_ITEM_ACTION},
    {"Display", 0, NULL, NULL, &displayMenu, NULL, M_ITEM_ACTION},
    {"Sound", 0, NULL, NULL, &soundMenu, NULL, M_ITEM_ACTION},
    {"Buttons", 0, NULL, NULL, &buttonMenu, NULL, M_ITEM_ACTION},
    {"System", 0, NULL, NULL, &systemMenu, NULL, M_ITEM_ACTION},
    {"DTMF", 0, NULL, NULL, &dtmfMenu, NULL, M_ITEM_ACTION},
};

static Menu rootMenu = {
    .title = "Settings",
    .items = rootItems,
    .num_items = ARRAY_SIZE(rootItems),
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y, .itemHeight = MENU_ITEM_H
};

void SETTINGS_UI_Init(void) {
    AG_MENU_Init(&rootMenu);
}
