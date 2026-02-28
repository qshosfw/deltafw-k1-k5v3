#ifndef SIGNAL_QUALITY_H
#define SIGNAL_QUALITY_H

#include <stdint.h>

/**
 * @brief Initialize signal quality module
 */
void SIGNAL_QUALITY_Init(void);

/**
 * @brief Update signal quality metrics (call periodically in RX loop)
 */
void SIGNAL_QUALITY_Update(void);

/**
 * @brief Get current 1-5 signal quality level
 */
uint8_t SIGNAL_QUALITY_GetLevel(void);

#endif // SIGNAL_QUALITY_H
