#ifndef AG_GRAPHICS_H
#define AG_GRAPHICS_H

#include "../drivers/bsp/st7565.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stddef.h>

#include "gfxfont.h"

typedef enum {
  POS_L,
  POS_C,
  POS_R,
} TextPos;

typedef enum {
  C_CLEAR,
  C_FILL,
  C_INVERT,
} Color;

typedef struct {
  int16_t x;
  int16_t y;
} Cursor;

void AG_PutPixel(uint8_t x, uint8_t y, uint8_t fill);
bool AG_GetPixel(uint8_t x, uint8_t y);

void AG_DrawVLine(int16_t x, int16_t y, int16_t h, Color color);
void AG_DrawHLine(int16_t x, int16_t y, int16_t w, Color color);
void AG_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color color);
void AG_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);
void AG_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color);

void AG_PrintSmall(uint8_t x, uint8_t y, const char *str);
void AG_PrintMedium(uint8_t x, uint8_t y, const char *str);
void AG_PrintMediumBold(uint8_t x, uint8_t y, const char *str);
void AG_PrintBigDigits(uint8_t x, uint8_t y, const char *str);
void AG_PrintBiggestDigits(uint8_t x, uint8_t y, const char *str);

void AG_PrintSmallEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                  const char *str);
void AG_PrintMediumEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                   const char *str);
void AG_PrintMediumBoldEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                       const char *str);
void AG_PrintBigDigitsEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                      const char *str);
void AG_PrintBiggestDigitsEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                          const char *str);
void AG_PrintSymbolsEx(uint8_t x, uint8_t y, TextPos posLCR, Color color,
                    const char *str);

#endif /* AG_GRAPHICS_H */
