#ifndef UI_TEXTINPUT_H
#define UI_TEXTINPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "drivers/bsp/keyboard.h"

void TextInput_Init(char *buffer, uint8_t maxLen, void (*callback)(void));
bool TextInput_IsActive(void);
void TextInput_Deinit(void);
bool TextInput_HandleInput(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld);
void TextInput_Render(void);
bool TextInput_Tick(void); // Returns true if redraw needed

#endif
