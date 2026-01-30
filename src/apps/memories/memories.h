#ifndef APPS_MEMORIES_H
#define APPS_MEMORIES_H

#include <stdbool.h>
#include "drivers/bsp/keyboard.h"

void MEMORIES_Init(void);
void MEMORIES_Deinit(void);
void MEMORIES_Render(void);
void MEMORIES_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
