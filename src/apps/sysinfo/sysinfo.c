#include <string.h>
#include <stdio.h>

#include "apps/sysinfo/sysinfo.h"
#include "ui/ag_menu.h"
#include "ui/ag_graphics.h"
#include "ui/ui.h"
#include "ui/helper.h"
#include "core/version.h"
#include "core/misc.h"
#include "apps/battery/battery.h"
#include "drivers/bsp/st7565.h"
#include "drivers/bsp/adc.h"
#include "apps/settings/settings.h"
#include "apps/settings/settings.h"
#include "helper/identifier.h"
#include "helper/crypto.h"
#include "ui/hexdump.h"
#include "apps/security/passcode.h"

// Forward declarations
static void SysInfo_RenderItem(uint16_t index, uint8_t visIndex);
static bool SysInfo_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held);

typedef enum {
    INFO_VERSION,
    INFO_DATE,
    INFO_COMMIT,
#ifdef ENABLE_IDENTIFIER
    INFO_SERIAL,
    INFO_MAC,
#endif
    INFO_BATTERY,
    INFO_CHARGING,
    INFO_TEMP,
    INFO_RAM,
#ifdef ENABLE_PASSCODE
    INFO_MK_HASH,
    INFO_MIGRATED,
#endif
    INFO_LICENSE,
    INFO_COUNT
} InfoItem;

// CPU Unique ID at 0x1FFF3000 (256 bytes available, we use first 16)


// RAM usage from linker symbols
extern uint32_t _end;       // End of used RAM
extern uint32_t _estack;    // End of RAM

static const char* GetInfoLabel(InfoItem item) {
    switch (item) {
        case INFO_VERSION:  return "Version";
        case INFO_DATE:     return "Built";
        case INFO_COMMIT:   return "Commit";
#ifdef ENABLE_IDENTIFIER
        case INFO_SERIAL:   return "Serial";
        case INFO_MAC:      return "MAC";
#endif
        case INFO_BATTERY:  return "Battery";
        case INFO_CHARGING: return "Charging";
        case INFO_TEMP:     return "Temp";
        case INFO_RAM:      return "RAM";
#ifdef ENABLE_PASSCODE
        case INFO_MK_HASH:  return "MK Hash";
        case INFO_MIGRATED: return "Migrated";
#endif
        case INFO_LICENSE:  return "License";
        default:            return "";
    }
}

static void GetInfoValue(InfoItem item, char* buf, size_t buflen) {
    switch (item) {
        case INFO_VERSION:
            strcpy(buf, Version);
            break;
        case INFO_DATE:
            strcpy(buf, BuildDate);
            break;
        case INFO_COMMIT:
            strncpy(buf, GitCommit, 7);
            buf[7] = '\0';
            break;
#ifdef ENABLE_IDENTIFIER
        case INFO_SERIAL: {
            GetCrockfordSerial(buf);
            break;
        }
        case INFO_MAC: {
            uint8_t mac[6];
            GetMacAddress(mac);
            for (int i = 0; i < 6; i++) {
                NUMBER_ToHex(buf + i * 3, mac[i], 2);
                if (i < 5) buf[i * 3 + 2] = ':';
            }
            buf[17] = '\0';
            break;
        }
#endif
        case INFO_BATTERY: {
            uint16_t voltage = gBatteryVoltageAverage;
            UI_FormatVoltage(buf, voltage * 10);
            strcat(buf, " ");
            NUMBER_ToDecimal(buf + strlen(buf), BATTERY_VoltsToPercent(voltage), 3, false);
            strcat(buf, "%");
            break;
        }
        case INFO_CHARGING:
            strcpy(buf, gIsCharging ? "Yes" : "No");
            break;
        case INFO_TEMP: {
            UI_FormatTemp(buf, ADC_GetTemp());
            break;
        }
        case INFO_RAM: {
            uint32_t used = 14016;
            uint32_t total = 16 * 1024;
            NUMBER_ToDecimal(buf, used / 1024, 2, false);
            strcat(buf, "/");
            NUMBER_ToDecimal(buf + strlen(buf), total / 1024, 2, false);
            strcat(buf, "K");
            break;
        }
#ifdef ENABLE_PASSCODE
        case INFO_MK_HASH:
            NUMBER_ToHex(buf, (uint32_t)Passcode_GetMasterKeyHash(), 8);
            break;
        case INFO_MIGRATED: {
            int count = 0;
            for(int i=0; i<REC_MAX; i++) if (Passcode_IsMigrated(i)) count++;
            NUMBER_ToDecimal(buf, count, 2, false);
            strcat(buf, "/");
            NUMBER_ToDecimal(buf + strlen(buf), REC_MAX, 2, false);
            break;
        }
#endif
        case INFO_LICENSE:
            strcpy(buf, "GNU GPL v3");
            break;
        default:
            buf[0] = '\0';
            break;
    }
}

static Menu sysinfoMenu = {
    .title = "System Info",
    .items = NULL,
    .num_items = INFO_COUNT,
    .render_item = SysInfo_RenderItem,
    .action = SysInfo_Action,
    .itemHeight = MENU_ITEM_H,
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y
};

void SYSINFO_Init(void) {
    sysinfoMenu.i = 0;
    AG_MENU_Init(&sysinfoMenu);
}

static void SysInfo_RenderItem(uint16_t index, uint8_t visIndex) {
    const uint8_t y = MENU_Y + visIndex * MENU_ITEM_H;
    const uint8_t baseline_y = y + MENU_ITEM_H - 2;

    if (index >= INFO_COUNT) return;

    const char* label = GetInfoLabel((InfoItem)index);
    char value[32];
    GetInfoValue((InfoItem)index, value, sizeof(value));

    // Label on left (medium font)
    AG_PrintMedium(3, baseline_y, label);
    
    // Value on right (small font)
    AG_PrintSmallEx(LCD_WIDTH - 5, baseline_y, POS_R, C_FILL, value);
}

static bool mShowCode = false;
static int mScrollY = 0;
#define LINE_H 8
#define HEADER_H 10
#define VISIBLE_LINES ((LCD_HEIGHT - HEADER_H) / LINE_H)
#define BYTES_PER_LINE 8
#define TOTAL_BYTES    16
#define TOTAL_LINES    (TOTAL_BYTES / BYTES_PER_LINE)

static bool SysInfo_CpuIdRead(uint32_t offset, uint8_t *buffer, uint16_t size) {
    uint8_t uid[16];
    GetCpuId(uid, 16);
    if (offset >= 16) return false;
    uint32_t copy_size = (offset + size > 16) ? (16 - offset) : size;
    memcpy(buffer, &uid[offset], copy_size);
    return true;
}

#ifdef ENABLE_IDENTIFIER
static void RenderSerialCode(void) {
    HexDump_Render("CPU Id", SysInfo_CpuIdRead, 16, mScrollY);
}
#endif

static bool SysInfo_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (mShowCode) {
        if (key_pressed) {
            if (key == KEY_EXIT) {
                 mShowCode = false;
                 return true;
            }
            if (key == KEY_UP) {
                if (mScrollY > 0) mScrollY--;
                return true;
            }
            if (key == KEY_DOWN) {
                if (mScrollY < TOTAL_LINES - VISIBLE_LINES) mScrollY++;
                return true;
            }
            // Any other key exits? user didn't specify. Standard behavior: Exit on EXIT/MENU?
            // Existing logic was "Any key exit", let's keep Exit/Menu/Back specific?
            // Or Keep "Any key exit" BUT allow Up/Down for scrolling.
            // "if (key_pressed) mShowCode = false" was the old logic.
            // Let's allow specific navigation and exit on others? 
            // Or just Up/Down scroll, Exit/Menu leaves.
        }
        return false;
    }

    if (key == KEY_EXIT) {
        if (key_pressed && !key_held) {
            AG_MENU_Back();
        }
        return true;
    }
    
#ifdef ENABLE_IDENTIFIER
    if (index == INFO_SERIAL && key == KEY_MENU && key_pressed) {
        mShowCode = true;
        mScrollY = 0;
        return true;
    }
#endif
    
    return false;
}

void SYSINFO_Render(void) {
#ifdef ENABLE_IDENTIFIER
    if (mShowCode) {
        RenderSerialCode();
    } else 
#endif
    {
        AG_MENU_Render();
        ST7565_BlitFullScreen();
    }
}

void SYSINFO_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
    // Intercept keys if the Code View is active
    if (mShowCode) {
        if (bKeyPressed) {
             SysInfo_Action(0, Key, bKeyPressed, bKeyHeld); // Index 0 ignored
        }
        return;
    }

    if (AG_MENU_HandleInput(Key, bKeyPressed, bKeyHeld)) {
        gUpdateDisplay = true;
    }

    if (!AG_MENU_IsActive()) {
        gRequestDisplayScreen = DISPLAY_MAIN;
    }
}
