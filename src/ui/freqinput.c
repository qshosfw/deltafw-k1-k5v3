#include "freqinput.h"
#include "ag_graphics.h"
#include "../drivers/bsp/st7565.h"
#include "../frequencies.h"
#include "../external/printf/printf.h"
#include <string.h>

// State
static bool active = false;
static uint32_t frequency = 0;
static FreqInputCallback callback = NULL;
static uint8_t cursor_pos = 0;  // 0-7 for 8 digits (XXX.XXXXX)

// Digit positions in the frequency (Hz)
// Position 0 = 100 MHz, Position 7 = 10 Hz
static const uint32_t digit_multipliers[] = {
    100000000,  // 0: 100 MHz
    10000000,   // 1: 10 MHz
    1000000,    // 2: 1 MHz
    100000,     // 3: 100 kHz
    10000,      // 4: 10 kHz
    1000,       // 5: 1 kHz
    100,        // 6: 100 Hz
    10          // 7: 10 Hz
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
            // Move cursor left
            if (cursor_pos > 0) cursor_pos--;
            return true;
            
        case KEY_DOWN:
            // Move cursor right
            if (cursor_pos < NUM_DIGITS - 1) cursor_pos++;
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
    AG_PrintMediumBold(LCD_WIDTH / 2, 12, POS_C, C_FILL, "Frequency");
    
    // Format: XXX.XXXXX MHz
    char freq_str[16];
    uint32_t mhz = frequency / 100000;
    uint32_t khz_frac = frequency % 100000;
    
    snprintf(freq_str, sizeof(freq_str), "%3u.%05u", mhz, khz_frac);
    
    // Draw frequency string with cursor highlight
    const uint8_t start_x = 20;
    const uint8_t y = 32;
    const uint8_t digit_w = 10;
    
    uint8_t x = start_x;
    uint8_t str_idx = 0;
    
    for (uint8_t i = 0; i < NUM_DIGITS; i++) {
        // Skip the decimal point position
        if (i == 3) {
            AG_PrintMediumBold(x, y, POS_L, C_FILL, ".");
            x += 6;
        }
        
        char ch[2] = {freq_str[str_idx], 0};
        
        if (i == cursor_pos) {
            // Draw inverted (cursor position)
            AG_FillRect(x - 1, y - 10, digit_w, 12, C_FILL);
            AG_PrintMediumBold(x, y, POS_L, C_CLEAR, "%s", ch);
        } else {
            AG_PrintMediumBold(x, y, POS_L, C_FILL, "%s", ch);
        }
        
        x += digit_w;
        str_idx++;
        
        // Skip the decimal in the string
        if (str_idx == 3) str_idx++;  // Skip '.'
    }
    
    // MHz label
    AG_PrintSmall(x + 5, y, "MHz");
    
    // Instructions
    AG_PrintSmall(LCD_WIDTH / 2, 50, POS_C, C_FILL, "UP/DN:move  0-9:digit");
    AG_PrintSmall(LCD_WIDTH / 2, 60, POS_C, C_FILL, "MENU:OK  EXIT:Cancel");
}
