#include "features/storage.h"
#include "drivers/bsp/py25q16.h"
#include <string.h>

static const RecordDescriptor_t gEepromMap[] = {
#define FIXED(a, s, c1, s1, c2, s2)  { .type = ALLOC_FIXED,  .size = (s), .fixed  = { .addr = (a) } }
#define LINEAR(a, s, c1, s1, c2, s2) { .type = ALLOC_LINEAR, .size = (s), .linear = { .addr = (a), .count = (c1), .stride = (s1) } }
#define DIM2(a, s, c1, s1, c2, s2)   { .type = ALLOC_DIM2,   .size = (s), .dim2   = { .addr = (a), .count1 = (c1), .stride1 = (s1), .count2 = (c2), .stride2 = (s2) } }
#ifdef ENABLE_STORAGE_ENCRYPTION
#define X(name, enc, type, addr, size, c1, s1, c2, s2) [REC_##name] = type(addr, size, c1, s1, c2, s2), [REC_##name].encryption = enc,
#else
#define X(name, enc, type, addr, size, c1, s1, c2, s2) [REC_##name] = type(addr, size, c1, s1, c2, s2),
#endif
    STORAGE_RECORDS(X)
#undef X
#undef FIXED
#undef LINEAR
#undef DIM2
};

#include "apps/security/passcode.h"
#include "drivers/bsp/system.h" // For KickWatchdog? No, storage.c doesn't use it yet.

// Forward declaration
void Storage_MigrateRecord(RecordID_t id);

// Helper to encrypt/decrypt buffer in place using XOR with stream generated from Key + Address
// This is a simple counter-mode style encryption to allow random access properties.
// Key: 32 bytes from Passcode_GetSessionKey() OR CPUID.
// Nonce/IV: Record Address + Offset (to ensure each byte has unique keystream).
#ifdef ENABLE_STORAGE_ENCRYPTION
#include "helper/crypto.h"
#include "helper/identifier.h"

static uint8_t gCpuIdKey[32];
static bool gCpuIdKeyValid = false;

static void Storage_CryptEx(RecordID_t id, uint32_t absoluteAddr, uint8_t *buffer, uint32_t len) {
    if (id >= REC_MAX) return;
    const RecordDescriptor_t *desc = &gEepromMap[id];
    if (desc->encryption == ENC_PLAIN) return;

    uint8_t key[32];
    if (desc->encryption == ENC_CPUID) {
        if (!gCpuIdKeyValid) {
            Passcode_DeriveKEK("", gCpuIdKey);
            gCpuIdKeyValid = true;
        }
        memcpy(key, gCpuIdKey, 32);
    } else {
        uint8_t *mk = Passcode_GetMasterKey();
        if (mk == NULL) return; 
        memcpy(key, mk, 32);
    }
    
    chacha20_ctx ctx;
    uint8_t keystream[64];
    uint32_t currentAddr = absoluteAddr;
    uint32_t endAddr = absoluteAddr + len;
    
    chacha20_init(&ctx, key, (uint8_t*)"\0\0\0\0\0\0\0\0\0\0\0\0", currentAddr / 64);

    while (currentAddr < endAddr) {
        chacha20_block(ctx.state, keystream);
        uint16_t blockOffset = currentAddr % 64;
        uint16_t bytes = 64 - blockOffset;
        if (currentAddr + bytes > endAddr) bytes = endAddr - currentAddr;
        
        for (uint16_t i = 0; i < bytes; i++) {
            buffer[(currentAddr - absoluteAddr) + i] ^= keystream[blockOffset + i];
        }
        currentAddr += bytes;
        ctx.state[12]++;
    }
    memset(key, 0, 32);
}

static void Storage_Crypt(RecordID_t id, uint32_t offset, uint8_t *buffer, uint32_t len) {
    Storage_CryptEx(id, Storage_GetAddress(id, 0) + offset, buffer, len);
}
#endif


static uint8_t gStorageDirtyFlags[(REC_MAX + 7) / 8];

uint32_t Storage_GetAddress(RecordID_t id, uint16_t index) {
    if (id >= REC_MAX) return 0xFFFFFFFF;
    
    const RecordDescriptor_t *desc = &gEepromMap[id];
    
    switch(desc->type) {
        case ALLOC_FIXED:
            return (index == 0) ? desc->fixed.addr : 0xFFFFFFFF;
            
        case ALLOC_LINEAR:
            return (index < desc->linear.count) ? 
                (desc->linear.addr + ((uint32_t)index * desc->linear.stride)) : 0xFFFFFFFF;
            
        case ALLOC_DIM2: {
            uint16_t i1 = index >> 8;
            uint16_t i2 = index & 0xFF;
            if (i1 < desc->dim2.count1 && i2 < desc->dim2.count2) {
                return desc->dim2.addr + ((uint32_t)i1 * desc->dim2.stride1) + ((uint32_t)i2 * desc->dim2.stride2);
            }
            return 0xFFFFFFFF;
        }
    }
    
    return 0xFFFFFFFF;
}

bool Storage_ReadRecordIndexed(RecordID_t id, uint16_t index, void *pDest, uint16_t offset, uint16_t len) {
    uint32_t addr = Storage_GetAddress(id, index);
    if (addr == 0xFFFFFFFF) return false;
    
    PY25Q16_ReadBuffer(addr + offset, pDest, len);
    
    // Decrypt if needed
#ifdef ENABLE_STORAGE_ENCRYPTION
    if (Passcode_IsMigrated(id)) {
        Storage_CryptEx(id, addr + offset, (uint8_t*)pDest, len);
    }
#endif
    
    return true;
}

bool Storage_ReadRecord(RecordID_t id, void *pDest, uint16_t offset, uint16_t len) {
    return Storage_ReadRecordIndexed(id, 0, pDest, offset, len);
}

bool Storage_WriteRecordIndexed(RecordID_t id, uint16_t index, const void *pSrc, uint16_t offset, uint16_t len) {
    uint32_t addr = Storage_GetAddress(id, index);
    if (addr == 0xFFFFFFFF) return false;
    
    // We need to encrypt before writing.
    // But pSrc is const. We need a temp buffer.
    // If len is large, this is stack heavy.
    // Most writes are small. Max record size is 80 bytes (SettingsExtra).
    // Let's use a temp buffer on stack.
    
    // Optimization: If NO encryption, bypass copy.
#ifdef ENABLE_STORAGE_ENCRYPTION
    if (id < REC_MAX && gEepromMap[id].encryption == ENC_PLAIN) {
        PY25Q16_WriteBuffer(addr + offset, (void*)pSrc, len, false);
        return true;
    }
#else
    PY25Q16_WriteBuffer(addr + offset, (void*)pSrc, len, false);
    return true;
#endif
    
    uint8_t tempBuf[128]; 
    if (len > 128) return false; // Safety limit
    
    memcpy(tempBuf, pSrc, len);
    
#ifdef ENABLE_STORAGE_ENCRYPTION
    if (id < REC_MAX && gEepromMap[id].encryption != ENC_PLAIN) {
        if (gEepromMap[id].encryption == ENC_PASSCODE && Passcode_IsLocked() && Passcode_IsSet()) return false;
        if (!Passcode_IsMigrated(id) && id != REC_PASSCODE) Storage_MigrateRecord(id);
        Storage_CryptEx(id, addr + offset, tempBuf, len);
        Passcode_SetMigrated(id);
        Passcode_SaveConfig();
    }
#endif
    
    PY25Q16_WriteBuffer(addr + offset, tempBuf, len, false);
    return true;
}

bool Storage_WriteRecord(RecordID_t id, const void *pSrc, uint16_t offset, uint16_t len) {
    return Storage_WriteRecordIndexed(id, 0, pSrc, offset, len);
}

void Storage_SetDirty(RecordID_t id) {
    if (id < REC_MAX) {
        gStorageDirtyFlags[id / 8] |= (1 << (id % 8));
    }
}

bool Storage_IsDirty(RecordID_t id) {
    if (id < REC_MAX) {
        return (gStorageDirtyFlags[id / 8] & (1 << (id % 8))) != 0;
    }
    return false;
}

void Storage_ClearDirty(RecordID_t id) {
    if (id < REC_MAX) {
        gStorageDirtyFlags[id / 8] &= ~(1 << (id % 8));
    }
}

void Storage_Commit(RecordID_t id) {
    Storage_ClearDirty(id);
}

void Storage_SectorErase(RecordID_t id) {
    uint32_t addr = Storage_GetAddress(id, 0);
    if (addr != 0xFFFFFFFF) {
        PY25Q16_SectorErase(addr);
    }
}

StorageEnc_t Storage_GetEncryptionType(RecordID_t id) {
    if (id >= REC_MAX) return ENC_PLAIN;
#ifdef ENABLE_STORAGE_ENCRYPTION
    return (StorageEnc_t)gEepromMap[id].encryption;
#else
    return ENC_PLAIN;
#endif
}

uint16_t Storage_GetCount(RecordID_t id) {
    if (id >= REC_MAX) return 0;
    const RecordDescriptor_t *desc = &gEepromMap[id];
    if (desc->type == ALLOC_FIXED) return 1;
    if (desc->type == ALLOC_LINEAR) return desc->linear.count;
    if (desc->type == ALLOC_DIM2) return (uint32_t)desc->dim2.count1 * desc->dim2.count2;
    return 0;
}

uint32_t Storage_GetRecordSize(RecordID_t id) {
    if (id >= REC_MAX) return 0;
    const RecordDescriptor_t *desc = &gEepromMap[id];
    return desc->size;
}

void Storage_MigrateRecord(RecordID_t id) {
#ifdef ENABLE_STORAGE_ENCRYPTION
    if (id >= REC_MAX) return;
    if (gEepromMap[id].encryption == ENC_PLAIN) return;
    if (Passcode_IsMigrated(id) && id != REC_PASSCODE) return;
    if (gEepromMap[id].encryption == ENC_PASSCODE && Passcode_IsLocked() && Passcode_IsSet()) return;

    uint32_t totalSize = (uint32_t)Storage_GetCount(id) * Storage_GetRecordSize(id);
    uint32_t addr = Storage_GetAddress(id, 0);
    uint8_t buf[64];
    for (uint32_t offset = 0; offset < totalSize; offset += 64) {
        uint32_t slice = (totalSize - offset > 64) ? 64 : totalSize - offset;
        PY25Q16_ReadBuffer(addr + offset, buf, slice);
        Storage_CryptEx(id, addr + offset, buf, slice);
        PY25Q16_WriteBuffer(addr + offset, buf, slice, false);
    }
    Passcode_SetMigrated(id);
#endif
}

#ifdef ENABLE_STORAGE_ENCRYPTION
// Helper to find record by address
static RecordID_t Storage_FindRecordByAddress(uint32_t addr, uint32_t *pStartAddr, uint32_t *pTotalSize) {
    for (int i = 0; i < REC_MAX; i++) {
        const RecordDescriptor_t *desc = &gEepromMap[i];
        uint32_t start = Storage_GetAddress((RecordID_t)i, 0);
        uint32_t size = Storage_GetCount((RecordID_t)i) * desc->size;
        
        if (addr >= start && addr < (start + size)) {
            if (pStartAddr) *pStartAddr = start;
            if (pTotalSize) *pTotalSize = size;
            return (RecordID_t)i;
        }
    }
    return REC_MAX;
}

void Storage_ReadBufferRaw(uint32_t addr, void *pDest, uint32_t len) {
    PY25Q16_ReadBuffer(addr, pDest, len);
    
    uint32_t currentAddr = addr;
    uint8_t *pData = (uint8_t *)pDest;
    uint32_t remaining = len;
    
    while (remaining > 0) {
        uint32_t recStart, recSize;
        RecordID_t id = Storage_FindRecordByAddress(currentAddr, &recStart, &recSize);
        
        if (id != REC_MAX && gEepromMap[id].encryption != ENC_PLAIN && Passcode_IsMigrated(id)) {
            uint32_t inRecLen = recSize - (currentAddr - recStart);
            uint32_t processLen = (remaining < inRecLen) ? remaining : inRecLen;
            
            Storage_CryptEx(id, currentAddr, pData, processLen);
            
            currentAddr += processLen;
            pData += processLen;
            remaining -= processLen;
        } else {
            uint32_t skip = 1; 
            if (id != REC_MAX) {
                skip = recSize - (currentAddr - recStart);
            } else {
                uint32_t nextStart = 0xFFFFFFFF;
                for(int i=0; i<REC_MAX; i++) {
                    uint32_t s = Storage_GetAddress((RecordID_t)i, 0);
                    if (s > currentAddr && s < nextStart) nextStart = s;
                }
                if (nextStart != 0xFFFFFFFF) skip = nextStart - currentAddr;
                else skip = remaining;
            }
            if (skip > remaining) skip = remaining;
            currentAddr += skip;
            pData += skip;
            remaining -= skip;
        }
    }
}

void Storage_WriteBufferRaw(uint32_t addr, const void *pSrc, uint32_t len, bool Append) {
    uint32_t currentAddr = addr;
    const uint8_t *pData = (const uint8_t *)pSrc;
    uint32_t remaining = len;
    
    uint8_t tempBuf[128];
    
    while (remaining > 0) {
        uint32_t recStart, recSize;
        RecordID_t id = Storage_FindRecordByAddress(currentAddr, &recStart, &recSize);
        uint32_t processLen = (remaining > 128) ? 128 : remaining;
        
        if (id != REC_MAX && gEepromMap[id].encryption != ENC_PLAIN) {
            uint32_t inRecLen = recSize - (currentAddr - recStart);
            if (processLen > inRecLen) processLen = inRecLen;
            
            // SECURITY: Same check as WriteRecord
            if (gEepromMap[id].encryption == ENC_PASSCODE && Passcode_IsLocked() && Passcode_IsSet()) {
                // Skip writing this chunk
            } else {
                if (!Passcode_IsMigrated(id) && id != REC_PASSCODE) {
                    Storage_MigrateRecord(id);
                }
                memcpy(tempBuf, pData, processLen);
                Storage_CryptEx(id, currentAddr, tempBuf, processLen);
                PY25Q16_WriteBuffer(currentAddr, tempBuf, processLen, Append);
                Passcode_SetMigrated(id);
                Passcode_SaveConfig();
            }
        } else {
            if (id != REC_MAX) {
                uint32_t inRecLen = recSize - (currentAddr - recStart);
                if (processLen > inRecLen) processLen = inRecLen;
            } else {
                uint32_t nextStart = 0xFFFFFFFF;
                for(int i=0; i<REC_MAX; i++) {
                    uint32_t s = Storage_GetAddress((RecordID_t)i, 0);
                    if (s > currentAddr && s < nextStart) nextStart = s;
                }
                if (nextStart != 0xFFFFFFFF && (nextStart - currentAddr) < processLen) 
                    processLen = nextStart - currentAddr;
            }
            
            PY25Q16_WriteBuffer(currentAddr, (void*)pData, processLen, Append);
        }
        
        currentAddr += processLen;
        pData += processLen;
        remaining -= processLen;
    }
}
#endif
