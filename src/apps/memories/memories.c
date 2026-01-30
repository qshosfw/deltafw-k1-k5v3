#include <string.h>
#include <stdio.h>

#include "apps/memories/memories.h"
#include "ui/ag_menu.h"
#include "ui/ag_graphics.h"
#include "ui/textinput.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "radio.h"
#include "apps/settings/settings.h"
#include "drivers/bsp/st7565.h"
#include "core/misc.h"
#include "external/printf/printf.h"

#ifndef MR_CHANNEL_LAST
#define MR_CHANNEL_LAST 199
#endif

// Forward declarations
static void Memories_RenderItem(uint16_t index, uint8_t visIndex);
static bool Memories_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held);

// Main Channel List Menu
static Menu memoriesMenu = {
    .title = "Memories",
    .items = NULL,
    .num_items = MR_CHANNEL_LAST + 1,
    .render_item = Memories_RenderItem,
    .action = Memories_Action,
    .itemHeight = MENU_ITEM_H,
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y
};

// Text Input State
static char editBuffer[17];
static uint16_t editIndex = 0xFFFF;

// Channel Details Menu
static uint16_t detailChannelIndex = 0;
static char detailTitle[20];

static void RenameCallback(void) {
    if (editIndex <= MR_CHANNEL_LAST) {
        SETTINGS_SaveChannelName((uint8_t)editIndex, editBuffer);
    }
    editIndex = 0xFFFF;
}

static void Detail_Select(void) {
    gEeprom.MrChannel[gEeprom.TX_VFO] = detailChannelIndex;
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = detailChannelIndex;
    gEeprom.FreqChannel[gEeprom.TX_VFO] = detailChannelIndex;
    RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    gRequestDisplayScreen = DISPLAY_MAIN;
}

static void Detail_Rename(void) {
    SETTINGS_FetchChannelName(editBuffer, detailChannelIndex);
    editIndex = detailChannelIndex;
    TextInput_Init(editBuffer, 10, RenameCallback);
}

static void Detail_Delete(void) {
    // Clear channel by writing invalid data
    VFO_Info_t empty;
    memset(&empty, 0xFF, sizeof(empty));
    SETTINGS_SaveChannel(detailChannelIndex, 0, &empty, 1);
    AG_MENU_Back();
}

static bool DA_Select(const MenuItem *item, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key != KEY_MENU) return false;
    if (key_pressed && !key_held) {
        Detail_Select();
    }
    return true; // Consume both press and release
}

static bool DA_Rename(const MenuItem *item, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key != KEY_MENU) return false;
    if (key_pressed && !key_held) {
        Detail_Rename();
    }
    return true; // Consume both press and release
}

static bool DA_Delete(const MenuItem *item, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key != KEY_MENU) return false;
    if (key_pressed && !key_held) {
        Detail_Delete();
    }
    return true; // Consume both press and release
}

static const MenuItem channelDetailItems[] = {
    {"Select", 0, NULL, NULL, NULL, DA_Select},
    {"Rename", 0, NULL, NULL, NULL, DA_Rename},
    {"Delete", 0, NULL, NULL, NULL, DA_Delete},
};

static Menu channelDetailMenu = {
    .title = NULL, // Set dynamically
    .items = channelDetailItems,
    .num_items = sizeof(channelDetailItems) / sizeof(channelDetailItems[0]),
    .itemHeight = MENU_ITEM_H,
    .x = 0, .y = MENU_Y, .width = LCD_WIDTH, .height = LCD_HEIGHT - MENU_Y
};

static void EnterDetailMenu(uint16_t index) {
    detailChannelIndex = index;
    
    // Build dynamic title with channel name or number
    char name[17];
    SETTINGS_FetchChannelName(name, index);
    if (strlen(name) > 0) {
        snprintf(detailTitle, sizeof(detailTitle), "%s", name);
    } else {
        snprintf(detailTitle, sizeof(detailTitle), "CH-%03u", index + 1);
    }
    channelDetailMenu.title = detailTitle;
    
    channelDetailMenu.i = 0;
    AG_MENU_EnterMenu(&channelDetailMenu);
}

void MEMORIES_Init(void) {
    memoriesMenu.i = 0;
    AG_MENU_Init(&memoriesMenu);
}

void MEMORIES_Deinit(void) {
    AG_MENU_Deinit();
}

static void Memories_RenderItem(uint16_t index, uint8_t visIndex) {
    const uint8_t y = MENU_Y + visIndex * MENU_ITEM_H;
    const uint8_t baseline_y = y + MENU_ITEM_H - 2;

    bool valid = RADIO_CheckValidChannel(index, false, 0);

    if (!valid) {
        AG_PrintSmall(3, baseline_y, "%03u -", index + 1);
        return;
    }

    char name[17];
    SETTINGS_FetchChannelName(name, index);
    uint32_t freq = SETTINGS_FetchChannelFrequency(index);

    char mainLabel[24];
    char rightLabel[16];
    
    switch (gEeprom.CHANNEL_DISPLAY_MODE) {
        case MDF_NAME:
        case MDF_NAME_FREQ:
            if (strlen(name) > 0) {
                snprintf(mainLabel, sizeof(mainLabel), "%03u %s", index + 1, name);
                snprintf(rightLabel, sizeof(rightLabel), "%u.%05u", freq/100000, freq%100000);
            } else {
                snprintf(mainLabel, sizeof(mainLabel), "%03u %u.%05u", index + 1, freq/100000, freq%100000);
                rightLabel[0] = '\0';
            }
            break;
        case MDF_FREQUENCY:
            snprintf(mainLabel, sizeof(mainLabel), "%03u %u.%05u", index + 1, freq/100000, freq%100000);
            if (strlen(name) > 0) {
                snprintf(rightLabel, sizeof(rightLabel), "%s", name);
            } else {
                rightLabel[0] = '\0';
            }
            break;
        case MDF_CHANNEL:
        default:
            if (strlen(name) > 0) {
                snprintf(mainLabel, sizeof(mainLabel), "%03u %s", index + 1, name);
            } else {
                snprintf(mainLabel, sizeof(mainLabel), "%03u %u.%05u", index + 1, freq/100000, freq%100000);
            }
            rightLabel[0] = '\0';
            break;
    }

    // Main label: Medium font (like Launcher/Settings)
    AG_PrintMedium(3, baseline_y, "%s", mainLabel);
    
    // Right label: Small font
    if (strlen(rightLabel) > 0) {
        AG_PrintSmallEx(LCD_WIDTH - 5, baseline_y, POS_R, C_FILL, "%s", rightLabel);
    }
}


static bool Memories_Action(uint16_t index, KEY_Code_t key, bool key_pressed, bool key_held) {
    if (key == KEY_EXIT) {
        if (key_pressed && !key_held) {
            AG_MENU_Back();
        }
        return true; // Consume both press and release
    }

    if (key == KEY_MENU) {
        if (key_pressed && !key_held) {
            if (RADIO_CheckValidChannel(index, false, 0)) {
                EnterDetailMenu(index);
            }
        }
        return true; // Consume both press and release
    }

    return false;
}

void MEMORIES_Render(void) {
    if (TextInput_IsActive()) {
        TextInput_Render();
    } else {
        AG_MENU_Render();
    }
    ST7565_BlitFullScreen();
}

void MEMORIES_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
    if (TextInput_IsActive()) {
        TextInput_HandleInput(Key, bKeyPressed, bKeyHeld);
        gUpdateDisplay = true;
        return;
    }

    if (AG_MENU_HandleInput(Key, bKeyPressed, bKeyHeld)) {
        gUpdateDisplay = true;
    }

    if (!AG_MENU_IsActive()) {
        gRequestDisplayScreen = DISPLAY_MAIN;
    }
}
