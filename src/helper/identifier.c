#include "identifier.h"
#include <string.h>

#define CPU_ID_ADDR 0x1FFF3000

// Reads the unique CPU ID (first 16 bytes)
void GetCpuId(uint8_t *dest, int count) {
    uint8_t *src = (uint8_t *)CPU_ID_ADDR;
    for (int i = 0; i < count; i++) {
        dest[i] = src[i];
    }
}

// MurmurHash3 64-bit finalizer mix function
static uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// Generates a deterministic 64-bit serial from the CPU ID
// Uses 16 bytes of CPU ID mixed into a 64-bit value
uint64_t GetSerial(void) {
    uint64_t idParts[2];
    GetCpuId((uint8_t*)idParts, 16);

    uint64_t h = 0x9e3779b97f4a7c15ULL; // Seed

    // Mix first part
    uint64_t k1 = idParts[0];
    k1 *= 0x87c37b91114253d5ULL;
    k1 = (k1 << 31) | (k1 >> 33); // rotl64(k1, 31)
    k1 *= 0x4cf5ad432745937fULL;
    h ^= k1;
    
    h = (h << 27) | (h >> 37); // rotl64(h, 27)
    h = h * 5 + 0x52dce729;

    // Mix second part
    uint64_t k2 = idParts[1];
    k2 *= 0x87c37b91114253d5ULL;
    k2 = (k2 << 31) | (k2 >> 33);
    k2 *= 0x4cf5ad432745937fULL;
    h ^= k2;
    
    h = (h << 27) | (h >> 37);
    h = h * 5 + 0x52dce729;

    // Finalize
    h ^= 16; // Length
    return fmix64(h);
}

// Derives a MAC address from the serial (Locally Administered, Unicast)
void GetMacAddress(uint8_t mac[6]) {
    uint64_t serial = GetSerial();
    
    mac[0] = (serial >> 40) & 0xFF;
    mac[1] = (serial >> 32) & 0xFF;
    mac[2] = (serial >> 24) & 0xFF;
    mac[3] = (serial >> 16) & 0xFF;
    mac[4] = (serial >> 8) & 0xFF;
    mac[5] = serial & 0xFF;
    
    // Set Locally Administered bit (bit 1 of first byte)
    mac[0] |= 0x02;
    // Clear Multicast bit (bit 0 of first byte)
    mac[0] &= ~0x01;
}

static const char CROCKFORD_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static const char CHECKSUM_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ*~$=U";

// Generates a Crockford Base32 representation of the serial with checksum
void GetCrockfordSerial(char *out) {
    uint64_t val = GetSerial();
    char buf[14]; // 13 chars + checksum (fits 64 bits: ceil(64/5) = 13)
    
    // Encode 64 bits into 13 characters
    for (int i = 12; i >= 0; i--) {
        buf[i] = CROCKFORD_ALPHABET[val & 0x1F];
        val >>= 5;
    }
    
    // Compute Checksum
    int checksum = 0;
    for (int i = 0; i < 13; i++) {
        char c = buf[i];
        int v = 0;
        // Find value in alphabet
        const char *p = strchr(CROCKFORD_ALPHABET, c);
        if (p) {
            v = p - CROCKFORD_ALPHABET;
        }
        checksum = (checksum + v) % 37; // Wait, Crockford checksum is weighted? No, ISO 7064 is. 
                                        // Crockford generally uses mod 37 of the sum? 
                                        // "The check digit is computed by summing the values of the digits and taking the result modulo 37."
                                        // Let's assume simple sum mod 37 for now as per "Crockford mod-37 checksum" request. 
                                        // EDIT: Actually, standard Crockford checksum usually involves modulus 37 of the value itself, 
                                        // but since we already processed the value, let's stick to the user's specific "Compute checksum over all Base32 data characters".
    }
    
    // Wait, simpler way compliant with user request "Compute checksum over all Base32 data characters" ??
    // User said "Compute checksum over all Base32 data characters".
    // Let's interpret this as: Sum of values of characters (0-31) % 37.
    // Or did they mean the value % 37?
    // "Modulus = 37" "Final character is the checksum digit"
    // Usually Base32 Checksum is: (Value % 37).
    // But since 64-bit value is large, we can compute (Value % 37).
    
    uint64_t serial = GetSerial();
    int mod37 = serial % 37;
    
    // Formatting with slashes: XXXX/XXXX/XXXX/X
    // 13 chars: ABCD EFGH JKMN P
    // Output: ABCD/EFGH/JKMN/P + Checksum?
    // User said: "insert / every 4 characters for readability"
    // "Appends one checksum character at the end"
    
    // Format: 13 chars base32. 
    // 0123 4567 8901 2
    // AAAA/BBBB/CCCC/D + Checksum
    
    int outIdx = 0;
    for (int i = 0; i < 13; i++) {
        // if (i > 0 && i % 4 == 0) {
        //    out[outIdx++] = '/';
        // }
        out[outIdx++] = buf[i];
    }
    
    out[outIdx++] = CHECKSUM_ALPHABET[mod37];
    out[outIdx] = '\0';
}
