#ifndef FREQINPUT_H
#define FREQINPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "../drivers/bsp/keyboard.h"

// Callback when frequency is confirmed
typedef void (*FreqInputCallback)(uint32_t frequency);

// Initialize frequency input mode
// freq: starting frequency in Hz (e.g., 14600000 for 146.00000 MHz)
// callback: called when user confirms with MENU
void FreqInput_Init(uint32_t freq, FreqInputCallback callback);

// Check if frequency input is active
bool FreqInput_IsActive(void);

// Handle key input
// Returns true if input was handled
bool FreqInput_HandleInput(KEY_Code_t key, bool key_pressed, bool key_held);

// Render the frequency input display
void FreqInput_Render(void);

// Cancel frequency input without calling callback
void FreqInput_Cancel(void);

// Get current frequency being edited
uint32_t FreqInput_GetFrequency(void);

#endif /* FREQINPUT_H */
