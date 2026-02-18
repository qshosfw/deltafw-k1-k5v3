#include <string.h>
#include "ui/textinput.h"
#include "ui/ag_graphics.h"
#include "drivers/bsp/st7565.h"
#include "apps/settings/settings.h"
#include "features/audio/audio.h"
#include "helper.h"

// State
static char *gTextInputBuffer = NULL;
static uint8_t gTextInputMaxLen = 15;
static bool gTextInputActive = false;
static void (*gTextInputCallback)(void) = NULL;
static bool gTextInputShowLength = true;
static bool gTextInputForceFull = false;
static uint16_t inputTick = 0;
static uint16_t lastKeyTime = 0;



bool TextInput_Tick(void) {
    if (!gTextInputActive) return false;
    inputTick++;
    // Redraw every 500ms (50 ticks) for blink logic toggle
    // But to catch the toggle edge exactly, we return true periodically?
    // Actually, just returning check:
    return (inputTick % 25 == 0); // Update every 250ms
}

// T9 Character Sets
static const char *t9Letters[10] = {
    " ",        // 0
    ".,?!'-",   // 1
    "abc",      // 2
    "def",      // 3
    "ghi",      // 4
    "jkl",      // 5
    "mno",      // 6
    "pqrs",     // 7
    "tuv",      // 8
    "wxyz"      // 9
};

static const char *t9LettersUpper[10] = {
    " ",
    ".,?!'-",
    "ABC",
    "DEF",
    "GHI",
    "JKL",
    "MNO",
    "PQRS",
    "TUV",
    "WXYZ"
};

static const char *t9Symbols[10] = {
    " ",
    ".,?!'-",
    "@#$",
    "%&*",
    "()[]",
    "<>{}",
    "/\\|",
    "+-=",
    "\"'`",
    ":;_"
};

static const char *t9Numbers[10] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

typedef enum {
    CHARSET_UPPER,
    CHARSET_LOWER,
    CHARSET_SYMBOLS,
    CHARSET_NUMBERS
} CharsetMode;

static CharsetMode currentCharset = CHARSET_UPPER;
static const char **currentSet = t9LettersUpper;

static uint8_t inputIndex = 0;
static uint8_t lastKey = 0xFF;
static uint8_t keyPressCount = 0;
static bool cursorBlink = true;
static uint8_t blinkCounter = 0;
static bool ignoreFirstMenuRelease = true;
static CharsetMode previousCharset = CHARSET_UPPER;
static KEY_Code_t lastLongPressedKey = KEY_INVALID;  // To ignore release after long press

static void UpdateCharset(void) {
    switch (currentCharset) {
        case CHARSET_UPPER:   currentSet = t9LettersUpper; break;
        case CHARSET_LOWER:   currentSet = t9Letters; break;
        case CHARSET_SYMBOLS: currentSet = t9Symbols; break;
        case CHARSET_NUMBERS: currentSet = t9Numbers; break;
    }
}

static void ConfirmCurrentChar(void) {
    lastKey = 0xFF;
    keyPressCount = 0;
    cursorBlink = true;  // Keep cursor visible on input
    blinkCounter = 0;
}

static void InsertChar(char c) {
    if (!gTextInputBuffer) return;
    size_t len = strlen(gTextInputBuffer);
    if (len >= gTextInputMaxLen) return;
    
    gTextInputBuffer[inputIndex++] = c;
    gTextInputBuffer[inputIndex] = '\0';
}

static void ReplaceCurrentChar(char c) {
    if (!gTextInputBuffer || inputIndex == 0) return;
    gTextInputBuffer[inputIndex - 1] = c;
    cursorBlink = true;
    blinkCounter = 0;
}

static void Backspace(void) {
    if (!gTextInputBuffer || inputIndex == 0) return;
    inputIndex--;
    memmove(gTextInputBuffer + inputIndex, gTextInputBuffer + inputIndex + 1,
            gTextInputMaxLen - inputIndex);
    cursorBlink = true;  // Keep cursor visible
    blinkCounter = 0;
}

void TextInput_Init(char *buffer, uint8_t maxLen, bool ignoreFirstMenuReleaseArg, void (*callback)(void)) {
    TextInput_InitEx(buffer, maxLen, ignoreFirstMenuReleaseArg, true, false, callback);
}

void TextInput_InitEx(char *buffer, uint8_t maxLen, bool ignoreFirstMenuReleaseArg, bool showLength, bool forceFull, void (*callback)(void)) {
    gTextInputBuffer = buffer;
    gTextInputMaxLen = maxLen;
    gTextInputCallback = callback;
    gTextInputShowLength = showLength;
    gTextInputForceFull = forceFull;
    gTextInputActive = true;
    inputIndex = strlen(buffer);
    currentCharset = CHARSET_UPPER;
    UpdateCharset();
    lastKey = 0xFF;
    keyPressCount = 0;
    cursorBlink = true;
    ignoreFirstMenuRelease = ignoreFirstMenuReleaseArg;
    lastLongPressedKey = KEY_INVALID;
    inputTick = 0;
    lastKeyTime = 0;
}

bool TextInput_IsActive(void) {
    return gTextInputActive;
}

void TextInput_Deinit(void) {
    gTextInputActive = false;
    gTextInputBuffer = NULL;
    gTextInputCallback = NULL;
}

char* TextInput_GetBuffer(void) {
    return gTextInputBuffer;
}

bool TextInput_HandleInput(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld) {
    if (!gTextInputActive) return false;

    // Determine Up/Down keys based on SET_NAV
    KEY_Code_t keyUp   = KEY_UP;
    KEY_Code_t keyDown = KEY_DOWN;
    
    // Check if SET_NAV (gEeprom.SET_NAV) swaps them?
    // Usually SET_NAV allows swapping up/down or left/right semantics.
    // Assuming SET_NAV = true means "Inverted" or "K1 Style" where keys might be swapped visually.
    // If user says "in k1 right button moves right cursor", and usually "Right" button is physically mapped to UP or DOWN depending on model.
    // Let's assume standard behavior: SET_NAV swaps them.
    if (gEeprom.SET_NAV) { // Swapped
        keyUp   = KEY_DOWN;
        keyDown = KEY_UP;
    }

    // If any key is pressed (not held), we clear the "ignore first release" flag for MENU
    // because this means we are now actively interacting with the UI.
    if (bKeyPressed && !bKeyHeld) {
        ignoreFirstMenuRelease = false;
    }

    // Long press handlers
    if (bKeyHeld && bKeyPressed) {
        switch (key) {
            case KEY_EXIT:
                // Long press EXIT = cancel and exit without saving
                TextInput_Deinit();
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
                // Long press number: insert digit directly
                if (lastLongPressedKey != key) {
                    ConfirmCurrentChar();
                    char c = '0' + (key - KEY_0);
                    if (strlen(gTextInputBuffer) < gTextInputMaxLen) {
                        InsertChar(c);
                        lastLongPressedKey = key; // Mark/Suppress subsequent release
                    }
                }
                return true;
            }
                
            default:
                break;
        }
    }

    // Key release handlers (main input)
    if (!bKeyPressed && !bKeyHeld) {
        // If this key was handled as a long press, ignore the release
        if (lastLongPressedKey == key) {
            lastLongPressedKey = KEY_INVALID; // Reset
            return true;
        }
        // Also reset if it was some other key (just in case)
        if (lastLongPressedKey != KEY_INVALID) {
             lastLongPressedKey = KEY_INVALID;
        }

        // Check Up/Down first since they are dynamic
        if (key == keyUp) {
            if (inputIndex < strlen(gTextInputBuffer)) {
                ConfirmCurrentChar();
                inputIndex++;
            }
            return true;
        } else if (key == keyDown) {
            if (inputIndex > 0) {
                ConfirmCurrentChar();
                inputIndex--;
            }
            return true;
        }

        uint8_t keyNum = 0xFF;
        
        switch (key) {
            case KEY_0: keyNum = 0; break;
            case KEY_1: keyNum = 1; break;
            case KEY_2: keyNum = 2; break;
            case KEY_3: keyNum = 3; break;
            case KEY_4: keyNum = 4; break;
            case KEY_5: keyNum = 5; break;
            case KEY_6: keyNum = 6; break;
            case KEY_7: keyNum = 7; break;
            case KEY_8: keyNum = 8; break;
            case KEY_9: keyNum = 9; break;
            default: break;
        }

        if (keyNum != 0xFF) {
            const char *chars = currentSet[keyNum];
            size_t numChars = strlen(chars);
            
            if (numChars == 0) return true;

            // T9 Cycle Logic with Timeout
            // Timeout: 100 ticks = 1 second
            bool timedOut = (inputTick - lastKeyTime > 100);

            if (keyNum == lastKey && keyPressCount > 0 && !timedOut) {
                // Cycle through characters
                keyPressCount = (keyPressCount % numChars);
                ReplaceCurrentChar(chars[keyPressCount]);
                keyPressCount++;
                lastKeyTime = inputTick; // Reset timer
            } else {
                // New key or Timeout - confirm previous, insert new
                ConfirmCurrentChar();
                if (strlen(gTextInputBuffer) < gTextInputMaxLen) {
                    InsertChar(chars[0]);
                    lastKey = keyNum;
                    keyPressCount = 1;
                    lastKeyTime = inputTick;
                }
            }
            return true;
        }

        switch (key) {
            case KEY_F:
                ConfirmCurrentChar();
                if (currentCharset == CHARSET_UPPER) {
                    currentCharset = CHARSET_LOWER;
                } else if (currentCharset == CHARSET_LOWER) {
                    currentCharset = CHARSET_UPPER;
                }
                UpdateCharset();
                return true;

            case KEY_STAR:
                ConfirmCurrentChar();
                // Cycle: Letters -> Numbers -> Symbols -> Letters
                if (currentCharset == CHARSET_UPPER || currentCharset == CHARSET_LOWER) {
                    previousCharset = currentCharset;
                    currentCharset = CHARSET_NUMBERS;
                } else if (currentCharset == CHARSET_NUMBERS) {
                    currentCharset = CHARSET_SYMBOLS;
                } else {
                    currentCharset = previousCharset;
                }
                UpdateCharset();
                return true;

            case KEY_SIDE1:
                ConfirmCurrentChar();
                currentCharset = CHARSET_NUMBERS;
                UpdateCharset();
                return true;

            // KEY_UP and KEY_DOWN are handled dynamically above

            case KEY_EXIT:
                // Short press EXIT = backspace (including cancel pending char)
                if (lastKey != 0xFF) {
                    // Cancel pending char and backspace
                    Backspace();
                    lastKey = 0xFF;
                    keyPressCount = 0;
                } else if (inputIndex > 0) {
                    Backspace();
                }
                // If nothing to backspace, do nothing (don't exit)
                return true;

            case KEY_MENU:
                if (ignoreFirstMenuRelease) {
                    ignoreFirstMenuRelease = false;
                    return true;
                }
                if (gTextInputForceFull && strlen(gTextInputBuffer) < gTextInputMaxLen) {
                    AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                    return true;
                }
                ConfirmCurrentChar();
                if (gTextInputCallback) {
                    gTextInputCallback();
                }
                TextInput_Deinit();
                return true;

            default:
                break;
        }
    }

    return false;
}

void TextInput_Render(void) {
    if (!gTextInputActive || !gTextInputBuffer) return;

    // Clear screen
    AG_FillRect(0, 8, LCD_WIDTH, LCD_HEIGHT - 8, C_CLEAR);

    const size_t charCount = strlen(gTextInputBuffer);
    const uint8_t HEADER_Y = 14;
    const uint8_t INPUT_Y = 17;
    const uint8_t CHAR_W = 6;

    // Header: charset indicator and count
    const char *charsetName = "ABC";
    switch (currentCharset) {
        case CHARSET_UPPER:   charsetName = "ABC"; break;
        case CHARSET_LOWER:   charsetName = "abc"; break;
        case CHARSET_NUMBERS: charsetName = "123"; break;
        case CHARSET_SYMBOLS: charsetName = "#@$"; break;
    }
    AG_PrintSmall(2, HEADER_Y, charsetName);
    if (gTextInputShowLength) {
        char countBuf[12];
        // Format as count/max (e.g. 0/32)
        if (charCount < 10) {
            countBuf[0] = charCount + '0';
            countBuf[1] = '/';
            NUMBER_ToDecimal(countBuf + 2, gTextInputMaxLen, 2, false);
        } else {
            NUMBER_ToDecimal(countBuf, charCount, 2, false);
            countBuf[2] = '/';
            NUMBER_ToDecimal(countBuf + 3, gTextInputMaxLen, 2, false);
        }
        AG_PrintSmallEx(LCD_WIDTH - 2, HEADER_Y, POS_R, C_FILL, countBuf);
    }



    // Input line
    AG_DrawHLine(4, INPUT_Y + 10, LCD_WIDTH - 8, C_FILL);

    // Input text
    for (size_t i = 0; i < charCount; i++) {
        char ch_buf[2] = {gTextInputBuffer[i], 0};
        AG_PrintMedium(4 + i * CHAR_W, INPUT_Y + 8, ch_buf);
    }

    // Cursor blink (Time based)
    // Force ON if recently typed (within 500ms), otherwise blink 500ms period
    // This allows the cursor to stay visible while typing rapidly
    bool forceOn = (inputTick - lastKeyTime) < 50;
    if (forceOn || ((inputTick / 50) % 2 == 0)) {
        uint8_t cursorX = 4 + inputIndex * CHAR_W - 1;
        AG_DrawVLine(cursorX, INPUT_Y, 9, C_FILL);
    }

    // Compact 3x3 T9 Grid (bottom of screen)
    // Adjusted Y to fit everything on 64px height
    const uint8_t GRID_Y = 32;
    const uint8_t CELL_W = 42;
    const uint8_t CELL_H = 7;

    for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            uint8_t keyIdx = row * 3 + col + 1; // 1-9
            uint8_t xPos = col * CELL_W + 2;
            uint8_t yPos = GRID_Y + row * CELL_H;

            // Key number in inverted box
            AG_FillRect(xPos, yPos, 7, 6, C_FILL);
            char key_buf[2] = {'0' + keyIdx, 0};
            AG_PrintSmallEx(xPos + 3, yPos + 5, POS_C, C_INVERT, key_buf);

            // Characters preview (max 4 chars)
            const char *chars = currentSet[keyIdx];
            char preview[5];
            strncpy(preview, chars, 4);
            preview[4] = '\0';
            AG_PrintSmall(xPos + 9, yPos + 5, preview);
        }
    }

    // Bottom row: special keys (* 0 F)
    const uint8_t BOTTOM_Y = GRID_Y + 3 * CELL_H;
    
    // * = Cycle Mode
    AG_FillRect(2, BOTTOM_Y, 7, 6, C_FILL);
    AG_PrintSmallEx(5, BOTTOM_Y + 5, POS_C, C_INVERT, "*");
    
    const char *modeName = "abc";
    switch (currentCharset) {
        case CHARSET_UPPER:   modeName = "NUM"; break; 
        case CHARSET_LOWER:   modeName = "NUM"; break;
        case CHARSET_NUMBERS: modeName = "SYM"; break;
        case CHARSET_SYMBOLS: modeName = "ABC"; break;
    }
    AG_PrintSmall(11, BOTTOM_Y + 4, modeName);

    // 0 = Space
    AG_FillRect(44, BOTTOM_Y, 7, 6, C_FILL);
    AG_PrintSmallEx(47, BOTTOM_Y + 5, POS_C, C_INVERT, "0");
    AG_PrintSmall(53, BOTTOM_Y + 4, currentSet[0]);

    // F = Case
    AG_FillRect(86, BOTTOM_Y, 7, 6, C_FILL);
    AG_PrintSmallEx(89, BOTTOM_Y + 5, POS_C, C_INVERT, "F");
    AG_PrintSmall(95, BOTTOM_Y + 4, currentCharset == CHARSET_UPPER ? "abc" : "ABC");

    ST7565_BlitFullScreen();
}
