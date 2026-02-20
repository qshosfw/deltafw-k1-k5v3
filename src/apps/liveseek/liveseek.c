#include "liveseek.h"
#include "drivers/bsp/bk4819.h"
#include "drivers/bsp/st7565.h"
#include "drivers/bsp/systick.h"
#include "core/misc.h"
#include "core/scheduler.h"
#include "features/radio/radio.h"
#include "features/app/app.h"
#include "apps/settings/settings.h"
#include "ui/ui.h"
#include "ui/helper.h"
#include "features/audio/audio.h"
#include "ui/ag_graphics.h"
#include "drivers/bsp/system.h"
#include <string.h>

#include "core/board.h"

// uvk5cec exact constants
#define STOP_RSSI_LIMIT 50
#define STOP_RSSI_TIME 500
#define COMBUFF_LENGTH 128
#define LIVESEEK_TIMEOUT_MS 800

// Dedicated static buffer for safety
static uint8_t s_CommBuff[COMBUFF_LENGTH];

static uint32_t s_CommBuffLastUseTime = 0;
static uint32_t s_rssiStartFreq = 0;
static uint32_t s_addRssiCount = 0;
static int8_t   s_lastSeekDirection = 0;
static bool     s_IsActive = false;
static uint8_t  s_CommValue1 = 0;

static uint16_t CEC_GetRssi(void)
{
    int wait_count = 0;
    while (((BK4819_ReadRegister(0x63) & 0xFF) >= 255) && wait_count++ < 100)
        SYSTICK_DelayUs(100);

    return BK4819_GetRSSI();
}

void LiveSeek_Init(void)
{
    s_IsActive = false;
    s_addRssiCount = 0;
    memset(s_CommBuff, 0, sizeof(s_CommBuff));
}

static void FormatFreq(char *str, uint32_t freq)
{
    uint32_t mhz = freq / 100000;
    uint32_t khz = (freq / 100) % 1000;
    
    if (mhz >= 100) {
        str[0] = (mhz / 100) + '0';
        str[1] = ((mhz / 10) % 10) + '0';
        str[2] = (mhz % 10) + '0';
    } else {
        str[0] = ' ';
        str[1] = (mhz / 10) + '0';
        str[2] = (mhz % 10) + '0';
    }
    str[3] = '.';
    str[4] = (khz / 100) + '0';
    str[5] = ((khz / 10) % 10) + '0';
    str[6] = (khz % 10) + '0';
    str[7] = '\0';
}

void LiveSeek_Apply(int8_t direction)
{
    if (gEeprom.LIVESEEK_MODE == LIVESEEK_OFF)
        return;

    // Fast Mute to prevent "sweep click" before we reset the DSP/Registers
    // This is the "other way" to prevent clicking while allowing full hardware resets
    uint16_t reg48 = BK4819_ReadRegister(BK4819_REG_48);
    BK4819_WriteRegister(BK4819_REG_48, 0); 

    BK4819_SetFrequency(gTxVfo->freq_config_RX.Frequency);
    
    // Restored: Always do full turn-on to ensure squelch state machines clear
    BK4819_RX_TurnOn();

    int32_t tmpRssi = (int32_t)CEC_GetRssi() / 3;
    if (tmpRssi < 0) tmpRssi = 0;

    int _applyOption = 11 + direction; // 10 or 12
    uint32_t now = SYSTICK_GetTick();

    if (s_lastSeekDirection != _applyOption || (now - s_CommBuffLastUseTime > LIVESEEK_TIMEOUT_MS))
    {
        memset(s_CommBuff, 0, sizeof(s_CommBuff));
        s_rssiStartFreq = gTxVfo->freq_config_RX.Frequency;
        s_addRssiCount = 0;
        s_CommValue1 = gCurrentFunction;
        s_IsActive = true;
    }

    s_addRssiCount++;
    s_lastSeekDirection = _applyOption;

    int _insertIndex = COMBUFF_LENGTH - 1;
    if (s_addRssiCount < COMBUFF_LENGTH / 2)
    {
        _insertIndex = COMBUFF_LENGTH / 2 + (int)s_addRssiCount;
        if (_insertIndex >= COMBUFF_LENGTH) _insertIndex = COMBUFF_LENGTH - 1;
    }
    else
    {
        memmove(&s_CommBuff[0], &s_CommBuff[1], COMBUFF_LENGTH - 1);
    }
    s_CommBuff[_insertIndex] = (tmpRssi > 255) ? 255 : (uint8_t)tmpRssi;
    s_CommBuffLastUseTime = now;

    // Restore Audio Gate state AFTER hardware reset
    BK4819_WriteRegister(BK4819_REG_48, reg48);

    if (gEeprom.SQUELCH_LEVEL == 0)
    {
        if (s_addRssiCount > 2)
            APP_StartListening(FUNCTION_MONITOR);
    }
    else 
    {
        if (tmpRssi > STOP_RSSI_LIMIT)
        {
            APP_StartListening(FUNCTION_MONITOR);
            SYSTEM_DelayMs(STOP_RSSI_TIME);
            RADIO_SetupRegisters(true);
        }
        else if (gMonitor)
        {
            // Squelch Clear: If signal dropped, force close monitor
            gMonitor = false;
            RADIO_SetupRegisters(true);
        }
    }
}

void LiveSeek_TimeSlice(void)
{
    if (s_IsActive) {
        uint32_t now = SYSTICK_GetTick();
        if (now - s_CommBuffLastUseTime > LIVESEEK_TIMEOUT_MS) {
            s_IsActive = false;
        }
    }
}

static void SafePixel(uint8_t x, uint8_t y, bool black)
{
    if (x < 128 && y < 64) {
        UI_DrawPixelBuffer(gFrameBuffer, x, y, black);
    }
}

static void SafeVLine(uint8_t x, uint8_t y1, uint8_t y2, bool black)
{
    if (x >= 128) return;
    if (y1 > y2) { uint8_t t = y1; y1 = y2; y2 = t; }
    if (y1 >= 64) return;
    if (y2 >= 64) y2 = 63;
    for (uint8_t y = y1; y <= y2; y++) {
        UI_DrawPixelBuffer(gFrameBuffer, x, y, black);
    }
}

void LiveSeek_DrawSpectrum(void)
{
    if (gEeprom.LIVESEEK_MODE < LIVESEEK_SPECTRUM || !s_IsActive || s_addRssiCount < 3)
        return;

    // Use opposite half of the seeking VFO
    // gEeprom.TX_VFO 0 = A, 1 = B
    uint8_t seekingVfo = gEeprom.TX_VFO;
    
    int startLine, lineCount;
    int startY, endY;
    int drawYPosition;
    int maxBarHeight;
    
    // In this firmware: VFO A is Top (Line 0-2), VFO B is Bottom (Line 4-6)
    if (seekingVfo == 0) { 
        // VFO A seeking -> Show on B (Lower Half: Lines 4-6)
        startLine = 4;
        lineCount = 3;
        startY = 32;
        endY = 55;
        drawYPosition = 55;
        maxBarHeight = 16;
    } else { 
        // VFO B seeking -> Show on A (Upper Half: Lines 0-2)
        startLine = 0;
        lineCount = 3;
        startY = 0;
        endY = 23;
        drawYPosition = 23;
        maxBarHeight = 16;
    }
    
    int _lowValue = 255;
    int peakValue = 0;
    int peakIndex = 0;

    for (int i = 0; i < COMBUFF_LENGTH; i++)
    {
        if (s_CommBuff[i] > 0 && _lowValue > s_CommBuff[i])
            _lowValue = s_CommBuff[i];
        
        if (s_CommBuff[i] > peakValue) {
            peakValue = s_CommBuff[i];
            peakIndex = i;
        }
    }
    if (_lowValue == 255) _lowValue = 0;

    // Clear designated space
    for (int line = startLine; line < startLine + lineCount; line++) {
        memset(gFrameBuffer[line], 0, 128);
    }

    // Solid Baseline Border
    for (int x = 0; x < 128; x++) {
        SafePixel((uint8_t)x, (uint8_t)drawYPosition, true);
    }

    // UX: Premium Checkerboard Dithering in working space
    for (int y = startY; y <= endY; y++) {
        for (int x = 0; x < 128; x++) {
            if ((x + y) % 4 == 0) {
                SafePixel((uint8_t)x, (uint8_t)y, true);
            }
        }
    }

    // Squelch Line (Dashed) - Relative to noise floor
    int relativeSq = STOP_RSSI_LIMIT - _lowValue;
    if (relativeSq < 0) relativeSq = 0;
    if (relativeSq > maxBarHeight) relativeSq = maxBarHeight;
    int sqY = drawYPosition - relativeSq; 

    for (int x = 0; x < 128; x += 4) {
        SafePixel((uint8_t)x, (uint8_t)sqY, true);
        SafePixel((uint8_t)x + 1, (uint8_t)sqY, true);
    }

    // Bars
    for (int i = 0; i < COMBUFF_LENGTH; i++)
    {
        int _drawXPosition = (s_lastSeekDirection == 10 ? 127 - i : i);
        int _drawYValue = s_CommBuff[i] - _lowValue;
        if (_drawYValue > maxBarHeight)
            _drawYValue = maxBarHeight;
        else if (_drawYValue < 0)
            _drawYValue = 0;
        
        if (_drawYValue > 0) {
            SafeVLine((uint8_t)_drawXPosition, (uint8_t)drawYPosition, (uint8_t)(drawYPosition - _drawYValue), true);
        }
    }

    // Peak Marker (Vertical Dither Line)
    if (peakValue > _lowValue) {
        int peakX = (s_lastSeekDirection == 10 ? 127 - peakIndex : peakIndex);
        
        for (int y = startY; y < drawYPosition; y++) {
            if (y % 2 == 0) {
                SafePixel((uint8_t)peakX, (uint8_t)y, true);
            }
        }
    }

    // Frequency display at top line of working area
    uint32_t _drawFreq = s_rssiStartFreq;
    int _drawTextPosition = 0;
    if (s_lastSeekDirection == 12) _drawTextPosition = 127 - (int)s_addRssiCount;
    else _drawTextPosition = (int)s_addRssiCount - 55;
    
    if (_drawTextPosition > 80) _drawTextPosition = 80;
    if (_drawTextPosition < 0) _drawTextPosition = 0;

    char str[16];
    FormatFreq(str, _drawFreq);

    // Clear background for frequency label (1px padding) 
    int labelX = _drawTextPosition;
    int labelY = (int)startLine * 8;
    int labelW = 50; // Total width for 1px padding each side (48px text + 2px padding)
    int labelH = 8;
    for (int fy = labelY - 1; fy < labelY + labelH; fy++) {
        // Clamp vertical clearing to stay within the working area bounds
        if (fy < startY || fy > endY) continue;
        for (int fx = labelX; fx < labelX + labelW; fx++) {
            SafePixel((uint8_t)fx, (uint8_t)fy, false);
        }
    }

    UI_PrintStringSmallNormal(str, (uint8_t)_drawTextPosition, 0, (uint8_t)startLine);
}

bool LiveSeek_IsActive(void)
{
    return s_IsActive && gEeprom.LIVESEEK_MODE != LIVESEEK_OFF;
}
