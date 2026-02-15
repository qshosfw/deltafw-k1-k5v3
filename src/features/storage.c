#include "features/storage.h"
#include "drivers/bsp/py25q16.h"
#include <string.h>

static const RecordDescriptor_t gEepromMap[] = {
#define FIXED(a, s, c1, s1, c2, s2)  { .type = ALLOC_FIXED,  .size = (s), .fixed  = { .addr = (a) } }
#define LINEAR(a, s, c1, s1, c2, s2) { .type = ALLOC_LINEAR, .size = (s), .linear = { .addr = (a), .count = (c1), .stride = (s1) } }
#define DIM2(a, s, c1, s1, c2, s2)   { .type = ALLOC_DIM2,   .size = (s), .dim2   = { .addr = (a), .count1 = (c1), .stride1 = (s1), .count2 = (c2), .stride2 = (s2) } }
#define X(name, type, addr, size, c1, s1, c2, s2) [REC_##name] = type(addr, size, c1, s1, c2, s2),
    STORAGE_RECORDS(X)
#undef X
#undef FIXED
#undef LINEAR
#undef DIM2
};

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
    return true;
}

bool Storage_ReadRecord(RecordID_t id, void *pDest, uint16_t offset, uint16_t len) {
    return Storage_ReadRecordIndexed(id, 0, pDest, offset, len);
}

bool Storage_WriteRecordIndexed(RecordID_t id, uint16_t index, const void *pSrc, uint16_t offset, uint16_t len) {
    uint32_t addr = Storage_GetAddress(id, index);
    if (addr == 0xFFFFFFFF) return false;
    
    PY25Q16_WriteBuffer(addr + offset, (void*)pSrc, len, false);
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
