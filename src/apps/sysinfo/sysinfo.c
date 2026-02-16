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
#include "external/printf/printf.h"
#include "helper/identifier.h"
#include "helper/crypto.h"
#include "ui/hexdump.h"

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
        case INFO_LICENSE:  return "License";
        default:            return "";
    }
}

static void GetInfoValue(InfoItem item, char* buf, size_t buflen) {
    switch (item) {
        case INFO_VERSION:
            snprintf(buf, buflen, "%s", Version);
            break;
        case INFO_DATE:
            snprintf(buf, buflen, "%s", BuildDate);
            break;
        case INFO_COMMIT:
            // Display short commit hash (7 chars max)
            snprintf(buf, buflen, "%.7s", GitCommit);
            break;
#ifdef ENABLE_IDENTIFIER
        case INFO_SERIAL: {
            GetCrockfordSerial(buf);
            break;
        }
        case INFO_MAC: {
            uint8_t mac[6];
            GetMacAddress(mac);
            snprintf(buf, buflen, "%02X:%02X:%02X:%02X:%02X:%02X", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            break;
        }
#endif
        case INFO_BATTERY: {
            uint16_t voltage = gBatteryVoltageAverage;
            uint8_t percent = BATTERY_VoltsToPercent(voltage);
            snprintf(buf, buflen, "%u.%02uV %u%%", voltage / 100, voltage % 100, percent);
            break;
        }
        case INFO_CHARGING:
            snprintf(buf, buflen, "%s", gIsCharging ? "Yes" : "No");
            break;
        case INFO_TEMP: {
            // Temperature from internal sensor
            float temp = ADC_GetTemp();
            int whole = (int)temp;
            int frac = (int)((temp - whole) * 10);
            if (frac < 0) frac = -frac;
            
            // Handle negative zero case (e.g. -0.5) where integer part is 0 but sign is needed
            if (temp < 0 && whole == 0) {
                snprintf(buf, buflen, "-%d.%d C", whole, frac);
            } else {
                snprintf(buf, buflen, "%d.%d C", whole, frac);
            }
            break;
        }
        case INFO_RAM: {
            // Simple estimate: stack pointer region usage
            uint32_t used = 14016;  // From build output, or use runtime estimate
            uint32_t total = 16 * 1024;
            snprintf(buf, buflen, "%lu/%luK", (unsigned long)(used / 1024), (unsigned long)(total / 1024));
            break;
        }
        case INFO_LICENSE:
            snprintf(buf, buflen, "GNU GPL v3");
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
    AG_PrintMedium(3, baseline_y, "%s", label);
    
    // Value on right (small font)
    AG_PrintSmallEx(LCD_WIDTH - 5, baseline_y, POS_R, C_FILL, "%s", value);
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
