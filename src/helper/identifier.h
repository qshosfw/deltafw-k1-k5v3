#ifndef IDENTIFIER_H
#define IDENTIFIER_H

#ifdef ENABLE_IDENTIFIER

#include <stdint.h>
#include <stddef.h>

// Comprehensive device information structure for UART/UI
typedef struct {
    uint64_t Serial;
    char     Version[16];
    char     CommitHash[8];
    char     BuildDate[16];
} DeviceInfo_t;

// Reads the unique CPU ID (first 16 bytes)
void GetCpuId(uint8_t *dest, int count);

// Fills the comprehensive device information structure
void GetDeviceInfo(DeviceInfo_t *info);

// Generates a deterministic 64-bit serial from the CPU ID
uint64_t GetSerial(void);

// Derives a MAC address from the serial (Locally Administered, Unicast)
void GetMacAddress(uint8_t mac[6]);

// Generates a Crockford Base32 representation of the serial with checksum
// out must be at least 15 bytes (13 chars + null)
void GetCrockfordSerial(char *out);

#endif // ENABLE_IDENTIFIER

#endif // IDENTIFIER_H
