#ifndef DRIVER_ADC_H
#define DRIVER_ADC_H

#include <stdint.h>
#include <stdbool.h>

/* --- Re-restored BSP API --- */

void ADC_Init(void);
void ADC_Enable(void);
void ADC_Disable(void);
void ADC_Start(void);
void ADC_SoftReset(void);

uint16_t ADC_ReadChannel(uint32_t channel);
uint16_t ADC_GetValue(uint32_t channel);

/* --- Specialized Measurement functions --- */

/**
 * @brief Get internal temperature in Celsius
 */
float ADC_GetTemp(void);

/**
 * @brief Get VDDA voltage in Volts
 */
float ADC_GetVref(void);

#endif
