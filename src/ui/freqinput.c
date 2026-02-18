#include "freqinput.h"
#include "ag_graphics.h"
#include "../drivers/bsp/st7565.h"
#include "features/radio/frequencies.h"
#include "../apps/settings/settings.h"
#include <string.h>

// State
static bool active = false;
static uint32_t frequency = 0;
static FreqInputCallback callback = NULL;
static uint8_t cursor_pos = 0;  // 0-7 for 8 digits (XXX.XXXXX)

// Digit positions in the frequency (Hz)
// Position 0 = 100 MHz, Position 7 = 10 Hz
static const uint32_t digit_multipliers[] = {
    10000000,   // 0: 100 MHz
    1000000,    // 1: 10 MHz
    100000,     // 2: 1 MHz
    10000,      // 3: 100 kHz
    1000,       // 4: 10 kHz
    100,        // 5: 1 kHz
    10,         // 6: 100 Hz
    1           // 7: 10 Hz
};

#define NUM_DIGITS 8

void FreqInput_Init(uint32_t freq, FreqInputCallback cb) {
    frequency = freq;
    callback = cb;
    cursor_pos = 0;
    active = true;
}

bool FreqInput_IsActive(void) {
    return active;
}

void FreqInput_Cancel(void) {
    active = false;
    callback = NULL;
}

uint32_t FreqInput_GetFrequency(void) {
    return frequency;
}

static uint8_t get_digit(uint32_t freq, uint8_t pos) {
    if (pos >= NUM_DIGITS) return 0;
    return (freq / digit_multipliers[pos]) % 10;
}

static uint32_t set_digit(uint32_t freq, uint8_t pos, uint8_t digit) {
    if (pos >= NUM_DIGITS || digit > 9) return freq;
    
    uint8_t old_digit = get_digit(freq, pos);
    int32_t diff = (int32_t)(digit - old_digit) * digit_multipliers[pos];
    
    return freq + diff;
}

bool FreqInput_HandleInput(KEY_Code_t key, bool key_pressed, bool key_held) {
    if (!active) return false;
    
    // Only handle key press, not release or hold
    if (!key_pressed || key_held) return true;
    
    switch (key) {
        case KEY_EXIT:
            // Cancel without saving
            FreqInput_Cancel();
            return true;
            
        case KEY_MENU:
            // Confirm and call callback
            if (callback) {
                callback(frequency);
            }
            active = false;
            return true;
            
        case KEY_UP:
            // Move cursor right (Next) - Match TextInput
            if (cursor_pos < NUM_DIGITS - 1) cursor_pos++;
            return true;
            
        case KEY_DOWN:
            // Move cursor left (Prev) - Match TextInput
            if (cursor_pos > 0) cursor_pos--;
            return true;
            
        case KEY_0:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9: {
            // Enter digit at cursor position
            uint8_t digit = key - KEY_0;
            frequency = set_digit(frequency, cursor_pos, digit);
            
            // Auto-advance cursor
            if (cursor_pos < NUM_DIGITS - 1) cursor_pos++;
            return true;
        }
        
        default:
            break;
    }
    
    return true;  // Consume all keys when active
}

void FreqInput_Render(void) {
    if (!active) return;
    
    // Clear screen
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    
    // Title
    AG_PrintMediumEx(LCD_WIDTH / 2, 16, POS_C, C_FILL, "Frequency");
    
    // Draw numbers
    const uint8_t digit_w = 10;
    uint8_t x = (LCD_WIDTH - (NUM_DIGITS * digit_w + 6)) / 2;
    const uint8_t y = 36;

    for (uint8_t i = 0; i < NUM_DIGITS; i++) {
        // Skip the decimal point position
        if (i == 3) {
            AG_PrintMediumEx(x, y, POS_L, C_FILL, ".");
            x += 6;
        }
        
        uint8_t val = get_digit(frequency, i);
        char ch[2] = {(char)('0' + val), 0};
        
        if (i == cursor_pos) {
            // Draw inverted (cursor position)
            AG_FillRect(x - 1, y - 10, digit_w, 12, C_FILL);
            AG_PrintMediumEx(x, y, POS_L, C_CLEAR, ch);
        } else {
            AG_PrintMediumEx(x, y, POS_L, C_FILL, ch);
        }
        
        x += digit_w;
    }
    
    // MHz label
    AG_PrintSmall(x + 2, y, "MHz");
    
    // Use same style as TextInput for key hints
    const uint8_t HINT_Y = 48;
    const uint8_t HINT_Y2 = 56;
    
    // Navigation keys - respect SET_NAV setting
    // SET_NAV == 0: UP/DOWN, SET_NAV == 1: LEFT/RIGHT (physically wired to UP/DOWN logic)
    const char *navLabel = gEeprom.SET_NAV ? "L/R" : "U/D";
    
    // Row 1: Navigation and digits
    AG_FillRect(2, HINT_Y, 14, 7, C_FILL);
    AG_PrintSmallEx(9, HINT_Y + 5, POS_C, C_INVERT, navLabel);
    AG_PrintSmall(18, HINT_Y + 5, "Move");
    
    AG_FillRect(54, HINT_Y, 18, 7, C_FILL);
    AG_PrintSmallEx(63, HINT_Y + 5, POS_C, C_INVERT, "0-9");
    AG_PrintSmall(74, HINT_Y + 5, "Digit");
    
    // Row 2: Menu and Exit
    AG_FillRect(2, HINT_Y2, 7, 7, C_FILL);
    AG_PrintSmallEx(5, HINT_Y2 + 5, POS_C, C_INVERT, "M");
    AG_PrintSmall(11, HINT_Y2 + 5, "OK");
    
    AG_FillRect(54, HINT_Y2, 7, 7, C_FILL);
    AG_PrintSmallEx(57, HINT_Y2 + 5, POS_C, C_INVERT, "E");
    AG_PrintSmall(63, HINT_Y2 + 5, "Cancel");
}
