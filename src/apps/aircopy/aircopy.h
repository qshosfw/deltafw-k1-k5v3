#ifndef APPS_AIRCOPY_AIRCOPY_H
#define APPS_AIRCOPY_AIRCOPY_H

#include <stdbool.h>
#include <stdint.h>

#define AIRCOPY_RETRY_COUNT     50
#define AIRCOPY_PACKET_SIZE     64
#define AIRCOPY_PACKET_HEADER_SIZE 2 // CMD + OFFSET

enum {
    AIRCOPY_CMD_START = 1,
    AIRCOPY_CMD_DATA,
    AIRCOPY_CMD_COMPLETE,
};

typedef enum {
    AIRCOPY_STATE_NONE,
    AIRCOPY_STATE_INIT,
    AIRCOPY_STATE_RX,
    AIRCOPY_STATE_TX,
    AIRCOPY_STATE_COMPLETE,
} AIRCOPY_State_t;

typedef enum {
    AIRCOPY_WRITE_STRUCT,
    AIRCOPY_WRITE_BYTES
} AIRCOPY_WriteMode_t;

typedef struct {
    uint16_t start_offset;
    uint16_t end_offset;      // Exclusive
    AIRCOPY_WriteMode_t write_mode;
    uint8_t packet_size; // unused?
} AIRCOPY_Segment_t;

#define MAX_AIRCOPY_SEGMENTS 10

typedef struct {
    AIRCOPY_Segment_t segments[MAX_AIRCOPY_SEGMENTS];
    uint8_t num_segments;
    uint16_t total_blocks;
} AIRCOPY_TransferMap_t;


extern AIRCOPY_State_t gAirCopyState;
extern uint16_t        gAirCopyBlockNumber;
extern uint16_t        gErrorsDuringAirCopy;
extern uint8_t         gFSK_Buffer[AIRCOPY_PACKET_SIZE];

void AIRCOPY_Process(void);
const AIRCOPY_TransferMap_t *AIRCOPY_GetCurrentMap(void);

#endif
