#include <string.h>
#include <stdio.h>

#include "apps/sysinfo/sysinfo.h"
#include "ui/ag_menu.h"
#include "ui/ag_graphics.h"
#include "ui/ui.h"
#include "core/version.h"
#include "core/misc.h"
#include "apps/battery/battery.h"
#include "drivers/bsp/st7565.h"
#include "apps/settings/settings.h"
#include "external/printf/printf.h"

// Forward declarations
static void SysInfo_RenderItem(uint16_t index, uint8_t visIndex);
static bool SysInfo_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held);

typedef enum {
    INFO_VERSION,
    INFO_DATE,
    INFO_COMMIT,
    INFO_SERIAL,
    INFO_BATTERY,
    INFO_CURRENT,
    INFO_CHARGING,
    INFO_RAM,
    INFO_LICENSE,
    INFO_COUNT
} InfoItem;

// CPU Unique ID at 0x1FFF3000 (256 bytes available, we use first 16)
static void GetCpuId(uint32_t *dest, int count) {
    uint32_t *src = (uint32_t *)0x1FFF3000;
    for (int i = 0; i < count; i++) {
        dest[i] = src[i];
    }
}

// RAM usage from linker symbols
extern uint32_t _end;       // End of used RAM
extern uint32_t _estack;    // End of RAM

static const char* GetInfoLabel(InfoItem item) {
    switch (item) {
        case INFO_VERSION:  return "Version";
        case INFO_DATE:     return "Built";
        case INFO_COMMIT:   return "Commit";
        case INFO_SERIAL:   return "Serial";
        case INFO_BATTERY:  return "Battery";
        case INFO_CURRENT:  return "Current";
        case INFO_CHARGING: return "Charging";
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
        case INFO_SERIAL: {
            uint32_t cpuId[4];
            GetCpuId(cpuId, 4);
            // Use all 4 words XORed into 2 for 16-char hex serial
            snprintf(buf, buflen, "%08lX%08lX", 
                (unsigned long)(cpuId[0] ^ cpuId[2]), 
                (unsigned long)(cpuId[1] ^ cpuId[3]));
            break;
        }
        case INFO_BATTERY: {
            uint16_t voltage = gBatteryVoltageAverage;
            uint8_t percent = BATTERY_VoltsToPercent(voltage);
            snprintf(buf, buflen, "%u.%02uV %u%%", voltage / 100, voltage % 100, percent);
            break;
        }
        case INFO_CURRENT: {
            // Display raw ADC value for current sensing
            snprintf(buf, buflen, "ADC: %u", gBatteryCurrent);
            break;
        }
        case INFO_CHARGING:
            snprintf(buf, buflen, "%s", gChargingWithTypeC ? "USB-C" : "No");
            break;
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

static bool SysInfo_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key == KEY_EXIT) {
        if (key_pressed && !key_held) {
            AG_MENU_Back();
        }
        return true;
    }
    return false;
}

void SYSINFO_Render(void) {
    AG_MENU_Render();
    ST7565_BlitFullScreen();
}

void SYSINFO_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
    if (AG_MENU_HandleInput(Key, bKeyPressed, bKeyHeld)) {
        gUpdateDisplay = true;
    }

    if (!AG_MENU_IsActive()) {
        gRequestDisplayScreen = DISPLAY_MAIN;
    }
}
