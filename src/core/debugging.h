#ifndef DEBUGGING_H
#define DEBUGGING_H

#ifdef ENABLE_UART

#include "drivers/bsp/uart.h"
#include <string.h>
#include "drivers/bsp/bk4819.h"
#include "features/am_fix/am_fix.h"
#include "ui/helper.h"

static inline void LogUart(const char *const str)
{
    UART_Send(str, strlen(str));
}

// LogUartf removed as it depends on printf

static inline void LogRegUart(uint16_t reg)
{
    uint16_t regVal = BK4819_ReadRegister(reg);
    char buf[32];
    strcpy(buf, "reg");
    NUMBER_ToHex(buf + 3, reg, 2);
    strcat(buf, ": ");
    NUMBER_ToHex(buf + strlen(buf), regVal, 4);
    strcat(buf, "\n");
    LogUart(buf);
}

static inline void LogPrint()
{
    uint16_t rssi = BK4819_GetRSSI();
    uint16_t reg7e = BK4819_ReadRegister(0x7E);
    char buf[64];
    strcpy(buf, "7E:");
    NUMBER_ToHex(buf + 3, reg7e, 4);
    strcat(buf, " RSSI:");
    NUMBER_ToDecimal(buf + strlen(buf), rssi, 3, false);
    strcat(buf, "\n");
    LogUart(buf);
}

#endif

#endif
