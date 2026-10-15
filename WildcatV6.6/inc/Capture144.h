#ifndef WILDCAT_CAPTURE144_H
#define WILDCAT_CAPTURE144_H
#include "Interlace144.h"
typedef struct {
    void *user;
    int (*configure)(void *user);
    int (*start)(void *user);
    uint32_t (*status)(void *user);
    int (*read_pair)(void *user, uint16_t *pair);
    uint32_t (*now)(void *user);
    int (*cancelled)(void *user);
} Capture144Ops;
typedef enum { CAPTURE144_OK, CAPTURE144_ARGUMENT, CAPTURE144_UNSUPPORTED,
    CAPTURE144_CANCELLED, CAPTURE144_TIMEOUT, CAPTURE144_IO } Capture144Status;
/* Synchronous bus reads only. Input includes the 152 history pairs after
 * ONE separate priming read. Timeout covers waiting and all bus reads;
 * callbacks themselves must not block. Unsigned time supports wraparound.
 * Caller restores hardware on every return and discards partial output.
 */
Capture144Status Capture144_RecordFifo(const FpgaProfile *,const Capture144Ops *,uint16_t *,uint32_t,uint32_t);
Capture144Status Capture144_Record(const FpgaProfile *profile,
    const Capture144Ops *ops, uint16_t *pairs, uint32_t count, uint32_t timeout);
#endif
