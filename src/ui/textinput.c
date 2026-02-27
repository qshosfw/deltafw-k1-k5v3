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
static bool gTextInputMultiline = false;
static uint16_t inputTick = 0;
static uint16_t lastKeyTime = 0;
static int16_t scrollOffsetY = 0;
static int16_t scrollOffsetX = 0;



static bool cursorVisible = true;

bool TextInput_Tick(void) {
    if (!gTextInputActive) return false;
    inputTick++;
    if (inputTick % 25 == 0) {
        cursorVisible = !cursorVisible;
        return true;
    }
    return false;
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
    
    if (inputIndex < len) {
        memmove(gTextInputBuffer + inputIndex + 1, gTextInputBuffer + inputIndex, len - inputIndex + 1);
    } else {
        gTextInputBuffer[inputIndex + 1] = '\0';
    }
    
    gTextInputBuffer[inputIndex++] = c;
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
            strlen(gTextInputBuffer + inputIndex));
    cursorVisible = true; 
    blinkCounter = 0;
}

void TextInput_Init(char *buffer, uint8_t maxLen, bool ignoreFirstMenuReleaseArg, void (*callback)(void)) {
    TextInput_InitEx(buffer, maxLen, ignoreFirstMenuReleaseArg, true, false, false, callback);
}

void TextInput_InitEx(char *buffer, uint8_t maxLen, bool ignoreFirstMenuReleaseArg, bool showLength, bool forceFull, bool multiline, void (*callback)(void)) {
    gTextInputBuffer = buffer;
    gTextInputMaxLen = maxLen;
    gTextInputCallback = callback;
    gTextInputShowLength = showLength;
    gTextInputForceFull = forceFull;
    gTextInputMultiline = multiline;
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
    scrollOffsetY = 0;
    scrollOffsetX = 0;
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
    if (gEeprom.SET_NAV == 0) { // K1 layout (L/R) might need inverted logic for up/down visual
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
            case KEY_MENU:
                if (!gTextInputMultiline) {
                    if (lastLongPressedKey != key) {
                        ConfirmCurrentChar();
                        if (gTextInputCallback) gTextInputCallback();
                        TextInput_Deinit();
                        lastLongPressedKey = key;
                    }
                    return true;
                }
                break;
                
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

            case KEY_EXIT:
                if (lastKey != 0xFF) {
                    Backspace();
                    lastKey = 0xFF;
                    keyPressCount = 0;
                } else if (inputIndex > 0) {
                    Backspace();
                }
                return true;

            case KEY_MENU:
                if (ignoreFirstMenuRelease) {
                    ignoreFirstMenuRelease = false;
                    return true;
                }
                
                ConfirmCurrentChar();
                if (gTextInputMultiline) {
                    // Short press M = Newline
                    InsertChar('\n');
                } else {
                    // Single line M = Enter/Callback (handled via short press if not long pressed)
                    if (gTextInputCallback) gTextInputCallback();
                    TextInput_Deinit();
                }
                return true;

            default:
                break;
        }
    }

    // Long press handled as release override
    if (bKeyPressed && bKeyHeld) {
        if (key == KEY_MENU && !ignoreFirstMenuRelease) {
            // M Long Press = SEND / SUBMIT
            ConfirmCurrentChar();
            if (gTextInputCallback) gTextInputCallback();
            TextInput_Deinit();
            lastLongPressedKey = KEY_MENU;
            return true;
        }
    }

    return false;
}

void TextInput_Render(void) {
    if (!gTextInputActive || !gTextInputBuffer) return;

    // 0. Setup Context
    const bool isK1 = (gEeprom.SET_NAV == 0);
    const uint8_t HEADER_Y = 14;
    const uint8_t CHAR_W = 6;
    const uint8_t LINE_H = 10;
    const uint8_t INPUT_X = 4;
    const uint8_t INPUT_Y = 18;
    const uint8_t VISIBLE_WIDTH = LCD_WIDTH - 12;
    
    // Grid is shown on non-K1, or on K1 when labels are non-obvious (symbols/numbers)
    bool showGrid = !isK1 || (currentCharset == CHARSET_NUMBERS || currentCharset == CHARSET_SYMBOLS);

    // Max visible lines based on K1 mode and grid visibility
    uint8_t visibleLinesCap = isK1 ? (showGrid ? 2 : 4) : 2;
    if (gTextInputMultiline) visibleLinesCap = isK1 ? (showGrid ? 3 : 5) : 3;

    AG_FillRect(0, 8, LCD_WIDTH, LCD_HEIGHT - 8, C_CLEAR);

    // 1. Charset / Length Header
    const char *charsetName = (currentCharset == CHARSET_UPPER) ? "ABC" : 
                             (currentCharset == CHARSET_LOWER) ? "abc" :
                             (currentCharset == CHARSET_NUMBERS) ? "123" : "#@$";
    AG_PrintSmall(2, HEADER_Y, charsetName);
    
    if (gTextInputShowLength) {
        char countBuf[10];
        uint8_t charCount = strlen(gTextInputBuffer);
        NUMBER_ToDecimal(countBuf, charCount, charCount >= 10 ? 2 : 1, false);
        strcat(countBuf, "/");
        char maxBuf[5];
        NUMBER_ToDecimal(maxBuf, gTextInputMaxLen, gTextInputMaxLen >= 10 ? 2 : 1, false);
        strcat(countBuf, maxBuf);
        AG_PrintSmallEx(LCD_WIDTH - 2, HEADER_Y, POS_R, C_FILL, countBuf);
    }

    // 2. Wrap/Scroll Analysis
    uint16_t curX = 0, curY = 0;
    uint16_t cursorDrawX = 0, cursorDrawY = 0;
    uint8_t lineCount = 1;
    
    for (uint16_t i = 0; i <= strlen(gTextInputBuffer); i++) {
        if (i == inputIndex) {
            cursorDrawX = curX;
            cursorDrawY = curY * LINE_H;
        }
        if (i == strlen(gTextInputBuffer)) break;

        char c = gTextInputBuffer[i];
        bool doWrap = (gTextInputMultiline && (curX + CHAR_W > VISIBLE_WIDTH));
        
        if (c == '\n' || doWrap) {
            curX = 0;
            curY++;
            lineCount++;
        } else {
            curX += CHAR_W;
        }
    }

    // Vertical Auto-scroll (Multiline)
    if (curY >= scrollOffsetY + visibleLinesCap) {
        scrollOffsetY = curY - visibleLinesCap + 1;
    } else if (curY < scrollOffsetY) {
        scrollOffsetY = curY;
    }

    // Horizontal Auto-scroll (Single line)
    if (!gTextInputMultiline) {
        if (cursorDrawX >= scrollOffsetX + VISIBLE_WIDTH) {
            scrollOffsetX = cursorDrawX - VISIBLE_WIDTH + CHAR_W;
        } else if (cursorDrawX < scrollOffsetX) {
            scrollOffsetX = cursorDrawX;
        }
    } else {
        scrollOffsetX = 0;
    }

    // 3. Render Text
    curX = 0; curY = 0;
    for (uint16_t i = 0; i < strlen(gTextInputBuffer); i++) {
        char c = gTextInputBuffer[i];
        bool doWrap = (gTextInputMultiline && (curX + CHAR_W > VISIBLE_WIDTH));
        
        if (c == '\n' || doWrap) {
            curX = 0;
            curY++;
        }
        
        // Vertical Clip
        if (curY >= scrollOffsetY && curY < scrollOffsetY + visibleLinesCap) {
            int16_t dx = INPUT_X + curX - scrollOffsetX;
            // Horizontal Clip for Single Line
            if (dx + CHAR_W > 0 && dx < LCD_WIDTH - 4) {
                if (c != '\n') {
                    char buf[2] = {c, 0};
                    AG_PrintMedium(dx, INPUT_Y + (curY - scrollOffsetY) * LINE_H + 8, buf);
                }
            }
        }
        
        if (c != '\n') curX += CHAR_W;
    }

    // 4. Cursor
    if (cursorVisible || (inputTick - lastKeyTime < 50)) {
        int16_t vy = cursorDrawY / LINE_H;
        if (vy >= scrollOffsetY && vy < scrollOffsetY + visibleLinesCap) {
            int16_t dx = INPUT_X + cursorDrawX - scrollOffsetX;
            if (dx >= INPUT_X && dx <= LCD_WIDTH - 4) {
                AG_DrawVLine(dx, INPUT_Y + (vy - scrollOffsetY) * LINE_H, 10, C_FILL);
            }
        }
    }

    // 5. Scrollbar (Vertical only for multiline)
    if (gTextInputMultiline && lineCount > visibleLinesCap) {
        uint8_t sbH = visibleLinesCap * LINE_H;
        AG_DrawVLine(LCD_WIDTH - 2, INPUT_Y, sbH, C_FILL);
        uint8_t thumbH = (visibleLinesCap * sbH) / lineCount;
        if (thumbH < 4) thumbH = 4;
        uint8_t thumbY = INPUT_Y + (scrollOffsetY * (sbH - thumbH)) / (lineCount - visibleLinesCap);
        AG_FillRect(LCD_WIDTH - 3, thumbY, 3, thumbH, C_FILL);
    }

    // 6. T9 Hints
    const uint8_t GRID_Y = 36;
    const uint8_t CELL_W = 42;
    const uint8_t CELL_H = 7;

    if (showGrid) {
        for (uint8_t r = 0; r < 3; r++) {
            for (uint8_t c = 0; c < 3; c++) {
                uint8_t key = r * 3 + c + 1;
                uint8_t x = c * CELL_W + 2;
                uint8_t y = GRID_Y + r * CELL_H;
                AG_FillRect(x, y, 7, 6, C_FILL);
                char kbuf[2] = {'0' + key, 0};
                AG_PrintSmallEx(x + 3, y + 5, POS_C, C_INVERT, kbuf);
                AG_PrintSmall(x + 9, y + 5, currentSet[key]);
            }
        }
    }

    // Bottom Row Hints
    const uint8_t BY = (isK1 && !showGrid) ? 56 : (GRID_Y + 3 * CELL_H);
    
    // [* Mode]
    AG_FillRect(2, BY, 7, 6, C_FILL);
    AG_PrintSmallEx(5, BY + 5, POS_C, C_INVERT, "*");
    const char *mName = (currentCharset == CHARSET_NUMBERS) ? "SYM" : 
                        (currentCharset == CHARSET_SYMBOLS) ? (previousCharset == CHARSET_UPPER ? "ABC" : "abc") : "123";
    AG_PrintSmall(11, BY + 5, mName);

    // [Center M: Enter] or [0 Space] (Aligned to grid column 1)
    if (isK1) {
        AG_FillRect(44, BY, 7, 6, C_FILL);
        AG_PrintSmallEx(47, BY + 5, POS_C, C_INVERT, "M");
        AG_PrintSmall(53, BY + 5, "Enter");
    } else {
        AG_FillRect(44, BY, 7, 6, C_FILL);
        AG_PrintSmallEx(47, BY + 5, POS_C, C_INVERT, "0");
        AG_PrintSmall(53, BY + 5, "Space");
    }

    // [# Case] (Aligned to grid column 2)
    AG_FillRect(86, BY, 7, 6, C_FILL);
    AG_PrintSmallEx(89, BY + 5, POS_C, C_INVERT, "#");
    const char *caseTarget = (currentCharset == CHARSET_UPPER) ? "abc" : (currentCharset == CHARSET_LOWER ? "ABC" : "Case");
    AG_PrintSmall(95, BY + 5, caseTarget);

    ST7565_BlitFullScreen();
}
