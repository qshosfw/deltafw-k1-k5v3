#ifndef LIVESEEK_H
#define LIVESEEK_H

#include <stdint.h>
#include <stdbool.h>

enum LiveSeekMode_t {
    LIVESEEK_OFF = 0,
    LIVESEEK_RCV,       // Speaker only
    LIVESEEK_SPECTRUM,  // Spectrum visualization
};

void LiveSeek_Init(void);
void LiveSeek_Apply(int8_t direction);
void LiveSeek_TimeSlice(void);
void LiveSeek_DrawSpectrum(void);
bool LiveSeek_IsActive(void);

#endif
