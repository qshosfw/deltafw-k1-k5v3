#ifndef UI_HEXDUMP_H
#define UI_HEXDUMP_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/bsp/keyboard.h"

// Function pointer for reading data
// returns true on success
typedef bool (*HexDump_ReadCb)(uint32_t offset, uint8_t *buffer, uint16_t size);

void HexDump_Render(const char *title, HexDump_ReadCb read_cb, uint32_t total_size, int scroll_y);

// Wrapper for Full Screen UI
void UI_DisplayHexDump(void);
void UI_HexDump_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
