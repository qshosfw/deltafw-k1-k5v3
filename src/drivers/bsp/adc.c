#include "adc.h"
#include "drivers/hal/Inc/py32f071_ll_adc.h"
#include "drivers/hal/Inc/py32f071_ll_bus.h"
#include "drivers/hal/Inc/py32f071_ll_gpio.h"
#include "drivers/hal/Inc/py32f071_ll_rcc.h"

// Temperature Calibration Addresses (from factory)
#define TS_CAL1_ADDR  ((uint16_t*)0x1FFF3228)  // 30 C 
#define TS_CAL2_ADDR  ((uint16_t*)0x1FFF3230)  // 105 C
#define VREFINT_VOLTS 1.2f                 // Typical 1.2V

void ADC_Init(void)
{
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_0 | LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);

    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_ADC1);
    LL_RCC_SetADCClockSource(LL_RCC_ADC_CLKSOURCE_PCLK_DIV4);

    LL_ADC_SetCommonPathInternalCh(ADC1_COMMON, LL_ADC_PATH_INTERNAL_NONE);
    LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC1, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_SetSequencersScanMode(ADC1, LL_ADC_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerDiscont(ADC1, LL_ADC_REG_SEQ_DISCONT_DISABLE);
    
    LL_ADC_StartCalibration(ADC1);
    while (LL_ADC_IsCalibrationOnGoing(ADC1));

    LL_ADC_Enable(ADC1);
}

void ADC_Enable(void)
{
    LL_ADC_Enable(ADC1);
}

void ADC_Disable(void)
{
    LL_ADC_Disable(ADC1);
}

void ADC_Start(void)
{
    LL_ADC_REG_StartConversionSWStart(ADC1);
}

void ADC_SoftReset(void)
{
    // PY32 doesn't have a specific ADC soft reset bit in ADC registers, 
    // but we can toggle the peripheral reset in RCC if needed.
}

uint16_t ADC_GetValue(uint32_t channel)
{
    // This assumes the channel is already configured. 
    // For a generic BSP, it usually implies the last conversion result.
    return LL_ADC_REG_ReadConversionData12(ADC1);
}

uint16_t ADC_ReadChannel(uint32_t channel)
{
    if (!(ADC1->CR2 & ADC_CR2_ADON)) {
        ADC_Enable();
    }

    // Configure channel
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, channel);

    // Set sampling time (long enough for internal sensors)
    LL_ADC_SetChannelSamplingTime(ADC1, channel, LL_ADC_SAMPLINGTIME_239CYCLES_5);

    // Enable internal paths if needed
    if (channel == LL_ADC_CHANNEL_TEMPSENSOR) {
        LL_ADC_SetCommonPathInternalCh(ADC1_COMMON, LL_ADC_PATH_INTERNAL_TEMPSENSOR);
    } else if (channel == LL_ADC_CHANNEL_VREFINT) {
        LL_ADC_SetCommonPathInternalCh(ADC1_COMMON, LL_ADC_PATH_INTERNAL_VREFINT);
    }

    ADC_Start();

    // Poll for EOS (approx 2ms timeout)
    for (int i = 0; i < 10000; i++) {
        if (LL_ADC_IsActiveFlag_EOS(ADC1)) {
            uint16_t res = LL_ADC_REG_ReadConversionData12(ADC1);
            LL_ADC_ClearFlag_EOS(ADC1);
            return res;
        }
    }

    return 0;
}

float ADC_GetVref(void)
{
    uint16_t v_raw = ADC_ReadChannel(LL_ADC_CHANNEL_VREFINT);
    if (v_raw == 0) return 3.3f; // Fallback to 3.3V if read fails
    return (VREFINT_VOLTS * 4095.0f) / (float)v_raw;
}

float ADC_GetTemp(void)
{
    uint16_t ts_cal1 = *TS_CAL1_ADDR; // ADC raw value at 30 C
    uint16_t ts_cal2 = *TS_CAL2_ADDR; // ADC raw value at 105 C
    uint16_t ts_data = ADC_ReadChannel(LL_ADC_CHANNEL_TEMPSENSOR);
    float vdda = ADC_GetVref();
    
    // TSCAL values are measured at 3.3V. 
    // We normalize the current reading to the 3.3V calibration baseline.
    float ts_data_norm = (float)ts_data * vdda / 3.3f;

    // Safety check for unprogrammed/corrupt calibration
    if (ts_cal2 <= ts_cal1 || ts_cal1 == 0xFFFF || ts_cal1 == 0) {
        // Fallback to typical values: 0.75V at 30C, 2.5mV/C
        // (ts_data_norm * 3300 / 4095) is in mV
        return (ts_data_norm * 3300.0f / 4095.0f - 750.0f) / 2.5f + 30.0f;
    }

    // Linear interpolation
    return 30.0f + (ts_data_norm - (float)ts_cal1) * (105.0f - 30.0f) / (float)(ts_cal2 - ts_cal1);
}
