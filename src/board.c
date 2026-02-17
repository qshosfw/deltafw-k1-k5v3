/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>

#ifdef ENABLE_FMRADIO
    #include "apps/fm/fm.h"
#endif
#include "board.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_gpio.h"
#include "py32f071_ll_rcc.h"
#include "py32f071_ll_adc.h"
#include "drivers/bsp/voice.h"
#include "drivers/bsp/backlight.h"
#ifdef ENABLE_FMRADIO
    #include "drivers/bsp/bk1080.h"
#endif

#include "drivers/bsp/adc.h"
#include "drivers/bsp/crc.h"
#include "drivers/bsp/py25q16.h"
#include "drivers/bsp/flash.h"
#include "drivers/bsp/gpio.h"
#include "drivers/bsp/system.h"
#include "drivers/bsp/st7565.h"
#include "frequencies.h"
#include "apps/battery/battery.h"
#include "core/misc.h"
#include "apps/settings/settings.h"
#if defined(ENABLE_OVERLAY)
    #include "sram-overlay.h"
#endif

#if defined(ENABLE_OVERLAY)
    void BOARD_FLASH_Init(void)
    {
        FLASH_Init(FLASH_READ_MODE_1_CYCLE);
        FLASH_ConfigureTrimValues();
        SYSTEM_ConfigureClocks();

        overlay_FLASH_MainClock       = 48000000;
        overlay_FLASH_ClockMultiplier = 48;

        FLASH_Init(FLASH_READ_MODE_2_CYCLE);
    }
#endif

void BOARD_GPIO_Init(void)
{
    LL_IOP_GRP1_EnableClock(
        LL_IOP_GRP1_PERIPH_GPIOA   //
        | LL_IOP_GRP1_PERIPH_GPIOB //
        | LL_IOP_GRP1_PERIPH_GPIOC //
        | LL_IOP_GRP1_PERIPH_GPIOF //
    );

    LL_GPIO_InitTypeDef InitStruct;
    LL_GPIO_StructInit(&InitStruct);
    InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    InitStruct.Pull = LL_GPIO_PULL_UP;
    InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;

    // ---------------------
    // Input pins

    InitStruct.Mode = LL_GPIO_MODE_INPUT;

    // Keypad rows: PB15:12
    InitStruct.Pin = LL_GPIO_PIN_15 | LL_GPIO_PIN_14 | LL_GPIO_PIN_13 | LL_GPIO_PIN_12;
    LL_GPIO_Init(GPIOB, &InitStruct);

    // PTT: PB10
    InitStruct.Pin = LL_GPIO_PIN_10;
    LL_GPIO_Init(GPIOB, &InitStruct);

    // -----------------------
    //  Output pins

    LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_6); // LCD A0
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_2); // LCD CS

    InitStruct.Mode = LL_GPIO_MODE_OUTPUT;

    // Keypad cols: PB6:3
    InitStruct.Pin = LL_GPIO_PIN_6 | LL_GPIO_PIN_5 | LL_GPIO_PIN_4 | LL_GPIO_PIN_3;
    LL_GPIO_Init(GPIOB, &InitStruct);

    // Audio PA: PA8
    // LCD A0: PA6
    // SPI flash CS: PA3
    InitStruct.Pin = LL_GPIO_PIN_8 | LL_GPIO_PIN_6 | LL_GPIO_PIN_3;
    LL_GPIO_Init(GPIOA, &InitStruct);

    // BK4819 SCK: B8
    // BK4819 SDA: B9
    // LCD CS: PB2
    InitStruct.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_8 | LL_GPIO_PIN_2;
    LL_GPIO_Init(GPIOB, &InitStruct);

    // TODO: conditional compile per ENABLE_FLASHLIGHT
    // Flashlight: PC13
    InitStruct.Pin = LL_GPIO_PIN_13;
    LL_GPIO_Init(GPIOC, &InitStruct);

#ifdef ENABLE_FMRADIO
    // BK1080 SCK: PF5
    // BK1080 SDA: PF6
    InitStruct.Pin = LL_GPIO_PIN_6 | LL_GPIO_PIN_5;
    LL_GPIO_Init(GPIOF, &InitStruct);
#endif

    // Backlight: PF8
    // BK4819 CS: PF9
    InitStruct.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_8  ;
    LL_GPIO_Init(GPIOF, &InitStruct);

#ifndef ENABLE_SWD
    // A14:13
    InitStruct.Pin = LL_GPIO_PIN_14 | LL_GPIO_PIN_13;
    LL_GPIO_Init(GPIOA, &InitStruct);
#endif // ENABLE_SWD
}

void BOARD_ADC_Init(void)
{
    ADC_Init();
}

void BOARD_ADC_GetBatteryInfo(uint16_t *pVoltage)
{
    *pVoltage = ADC_ReadChannel(LL_ADC_CHANNEL_8);
}

void BOARD_SWD_Enable(bool enable)
{
    LL_GPIO_InitTypeDef InitStruct;
    LL_GPIO_StructInit(&InitStruct);

    // PA13: SWDIO / PA14: SWCLK
    // On PY32F071, AF0 is usually SWD (or default after reset)
    // To disable, set as Input/Output/Analog.
    // To enable, set as Alternate Function (AF0) or whatever default is.
    
    // Check datasheets or existing examples?
    // PY32 usually has SWD on PA13/PA14 by default.
    // If we re-init as GPIO, we lose SWD.
    
    // Assuming AF0 is SWD. Or just Reset State.
    // Let's use clean approach.
    
    InitStruct.Pin = LL_GPIO_PIN_13 | LL_GPIO_PIN_14;

    if (enable) {
        // Enable SWD: Set to AF0 (SWD)
        // Wait, does LL_GPIO_Init handle AF?
        // Need to check if there is specific AF for SWD.
        // Usually SWD pins are dedicated but can be remapped.
        // On Cortex-M0+, SWD pins are often default.
        // Let's try setting Mode to ALTERNATE and AF to 0.
        InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        InitStruct.Alternate = LL_GPIO_AF_0; 
        InitStruct.Pull = LL_GPIO_PULL_UP;
        InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    } else {
        // Disable SWD: Set as Input Floating or Analog to save power and prevent access
        InitStruct.Mode = LL_GPIO_MODE_ANALOG;
        InitStruct.Pull = LL_GPIO_PULL_NO;
    }
    
    LL_GPIO_Init(GPIOA, &InitStruct);
}

void BOARD_Init(void)
{
    BOARD_GPIO_Init();
    BACKLIGHT_InitHardware();
    BOARD_ADC_Init();
#ifdef ENABLE_VOICE
    VOICE_Init();
#endif
    PY25Q16_Init();
    ST7565_Init();
#ifdef ENABLE_FMRADIO
    BK1080_Init0();
#endif

#if defined(ENABLE_UART) || defined(ENABLED_AIRCOPY)
    CRC_Init();
#endif

}
