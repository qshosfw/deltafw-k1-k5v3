#include <string.h>
#include "ui/textinput.h"
#include "ui/ag_graphics.h"
#include "drivers/bsp/st7565.h"
#include "external/printf/printf.h"

// State
static char *gTextInputBuffer = NULL;
static uint8_t gTextInputMaxLen = 15;
static bool gTextInputActive = false;
static void (*gTextInputCallback)(void) = NULL;

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
}

static void Backspace(void) {
    if (!gTextInputBuffer || inputIndex == 0) return;
    inputIndex--;
    memmove(gTextInputBuffer + inputIndex, gTextInputBuffer + inputIndex + 1,
            gTextInputMaxLen - inputIndex);
}

void TextInput_Init(char *buffer, uint8_t maxLen, void (*callback)(void)) {
    gTextInputBuffer = buffer;
    gTextInputMaxLen = maxLen;
    gTextInputCallback = callback;
    gTextInputActive = true;
    inputIndex = strlen(buffer);
    currentCharset = CHARSET_UPPER;
    UpdateCharset();
    lastKey = 0xFF;
    keyPressCount = 0;
    cursorBlink = true;
}

bool TextInput_IsActive(void) {
    return gTextInputActive;
}

void TextInput_Deinit(void) {
    gTextInputActive = false;
    gTextInputBuffer = NULL;
    gTextInputCallback = NULL;
}

bool TextInput_HandleInput(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld) {
    if (!gTextInputActive) return false;

    // Long press handlers
    if (bKeyHeld && bKeyPressed) {
        switch (key) {
            case KEY_EXIT:
                if (gTextInputBuffer) memset(gTextInputBuffer, 0, gTextInputMaxLen + 1);
                inputIndex = 0;
                ConfirmCurrentChar();
                return true;
            default:
                break;
        }
    }

    // Key release handlers (main input)
    if (!bKeyPressed && !bKeyHeld) {
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

            if (keyNum == lastKey && keyPressCount > 0) {
                // Cycle through characters
                keyPressCount = (keyPressCount % numChars);
                ReplaceCurrentChar(chars[keyPressCount]);
                keyPressCount++;
            } else {
                // New key - confirm previous, insert new
                ConfirmCurrentChar();
                if (strlen(gTextInputBuffer) < gTextInputMaxLen) {
                    InsertChar(chars[0]);
                    lastKey = keyNum;
                    keyPressCount = 1;
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
                currentCharset = CHARSET_SYMBOLS;
                UpdateCharset();
                return true;

            case KEY_SIDE1:
                ConfirmCurrentChar();
                currentCharset = CHARSET_NUMBERS;
                UpdateCharset();
                return true;

            case KEY_UP:
                if (inputIndex < strlen(gTextInputBuffer)) {
                    ConfirmCurrentChar();
                    inputIndex++;
                }
                return true;

            case KEY_DOWN:
                if (inputIndex > 0) {
                    ConfirmCurrentChar();
                    inputIndex--;
                }
                return true;

            case KEY_EXIT:
                if (lastKey != 0xFF) {
                    ConfirmCurrentChar();
                } else if (inputIndex > 0) {
                    Backspace();
                } else {
                    TextInput_Deinit();
                }
                return true;

            case KEY_MENU:
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
    const uint8_t INPUT_Y = 20;
    const uint8_t CHAR_W = 6;

    // Header: charset indicator and count
    const char *charsetName = "ABC";
    switch (currentCharset) {
        case CHARSET_UPPER:   charsetName = "ABC"; break;
        case CHARSET_LOWER:   charsetName = "abc"; break;
        case CHARSET_SYMBOLS: charsetName = "#@$"; break;
        case CHARSET_NUMBERS: charsetName = "123"; break;
    }
    AG_PrintSmall(2, 16, "%s", charsetName);
    AG_PrintSmallEx(LCD_WIDTH - 2, 16, POS_R, C_FILL, "%u/%u", charCount, gTextInputMaxLen);

    // Input line
    AG_DrawHLine(4, INPUT_Y + 10, LCD_WIDTH - 8, C_FILL);

    // Input text
    for (size_t i = 0; i < charCount; i++) {
        AG_PrintMedium(4 + i * CHAR_W, INPUT_Y + 8, "%c", gTextInputBuffer[i]);
    }

    // Cursor blink
    blinkCounter++;
    if (blinkCounter > 30) {
        cursorBlink = !cursorBlink;
        blinkCounter = 0;
    }
    if (cursorBlink) {
        uint8_t cursorX = 4 + inputIndex * CHAR_W - 1;
        AG_DrawVLine(cursorX, INPUT_Y, 9, C_FILL);
    }

    // Compact 3x3 T9 Grid (bottom of screen)
    const uint8_t GRID_Y = 40;
    const uint8_t CELL_W = 42;
    const uint8_t CELL_H = 8;

    for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            uint8_t keyIdx = row * 3 + col + 1; // 1-9
            uint8_t xPos = col * CELL_W + 2;
            uint8_t yPos = GRID_Y + row * CELL_H;

            // Key number in inverted box
            AG_FillRect(xPos, yPos, 7, 7, C_FILL);
            AG_PrintSmallEx(xPos + 3, yPos + 6, POS_C, C_INVERT, "%u", keyIdx);

            // Characters preview (max 4 chars)
            const char *chars = currentSet[keyIdx];
            char preview[5];
            strncpy(preview, chars, 4);
            preview[4] = '\0';
            AG_PrintSmall(xPos + 9, yPos + 6, "%s", preview);
        }
    }

    // Bottom row: special keys
    const uint8_t BOTTOM_Y = GRID_Y + 3 * CELL_H;
    
    // * = Symbols
    AG_FillRect(2, BOTTOM_Y, 7, 7, C_FILL);
    AG_PrintSmallEx(5, BOTTOM_Y + 6, POS_C, C_INVERT, "*");
    AG_PrintSmall(11, BOTTOM_Y + 6, "sym");

    // 0 = Space
    AG_FillRect(44, BOTTOM_Y, 7, 7, C_FILL);
    AG_PrintSmallEx(47, BOTTOM_Y + 6, POS_C, C_INVERT, "0");
    AG_PrintSmall(53, BOTTOM_Y + 6, "%s", currentSet[0]);

    // F = Case
    AG_FillRect(86, BOTTOM_Y, 7, 7, C_FILL);
    AG_PrintSmallEx(89, BOTTOM_Y + 6, POS_C, C_INVERT, "F");
    AG_PrintSmall(95, BOTTOM_Y + 6, currentCharset == CHARSET_UPPER ? "abc" : "ABC");

    ST7565_BlitFullScreen();
}
