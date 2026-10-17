#ifndef WILDCAT_CAPTURE144_H
#define WILDCAT_CAPTURE144_H
#include "Interlace144.h"
#include "CaptureWindow.h"
typedef struct {
    void *user;
    int (*configure)(void *user);
    int (*start)(void *user);
    uint32_t (*status)(void *user);
    int (*read_pair)(void *user, uint16_t *pair);
    uint32_t (*now)(void *user);
    int (*cancelled)(void *user);
} Capture144Ops;
/* Runtime full-word path; calibration keeps its original read_pair path. */
typedef struct {
    void *user;
    int (*configure)(void *user);
    int (*start)(void *user);
    uint32_t (*status)(void *user);
    int (*read_word)(void *user,uint32_t *word);
    uint32_t (*now)(void *user);
    int (*cancelled)(void *user);
} Capture144WordOps;
typedef enum { CAPTURE144_OK, CAPTURE144_ARGUMENT, CAPTURE144_UNSUPPORTED,
    CAPTURE144_CANCELLED, CAPTURE144_TIMEOUT, CAPTURE144_IO, CAPTURE144_PENDING } Capture144Status;
/* Cooperative real A/C/D capture, PerCnt=150. configure sets the trigger;
 * stop disarms it on every unsuccessful exit. All callbacks are bounded.
 * Initialize session to zero before its first use. The caller keeps
 * ops/user, session, window and DataBuf exclusively owned
 * until completion/abort. No valid frame is published during transfer. */
typedef struct {
    Capture144WordOps io;
    int (*stop)(void *);
} Capture144AsyncOps;
typedef struct {
    Capture144AsyncOps ops;
    uint32_t *words;
    CaptureWindow *window;
    uint64_t period;
    uint32_t epoch,started,wait_timeout,read_timeout,index;
    unsigned stage,trigger_delay;
} Capture144Session;
Capture144Status Capture144_Begin(Capture144Session *,const FpgaProfile *,
    const Capture144AsyncOps *,uint32_t *,uint32_t epoch,uint64_t period,
    uint32_t wait_timeout,uint32_t read_timeout,CaptureWindow *);
Capture144Status Capture144_Poll(Capture144Session *,unsigned read_budget);
/* After Begin, before Poll: configure has selected C/D; use the same frozen
 * alignment policy for the raw PerCnt-1 edge and the digital trace. */
int Capture144_DigitalTrigger(Capture144Session *,unsigned channel,int32_t shift_q16);
/* Select the measured event pipeline before the first read. Pulse modes
 * 4..7 qualify the closing edge one FPGA clock after edge/level modes. */
int Capture144_TriggerKind(Capture144Session *,unsigned kind);
Capture144Status Capture144_Abort(Capture144Session *);
/* Synchronous bus reads only. Input includes the 152 history pairs after
 * ONE separate priming read. Timeout covers waiting and all bus reads;
 * callbacks themselves must not block. Unsigned time supports wraparound.
 * Caller restores hardware on every return and discards partial output.
 */
Capture144Status Capture144_RecordFifo(const FpgaProfile *,const Capture144Ops *,uint16_t *,uint32_t,uint32_t);
Capture144Status Capture144_Record(const FpgaProfile *profile,
    const Capture144Ops *ops, uint16_t *pairs, uint32_t count, uint32_t timeout);
/* UNCONDITION only: ONE discarded prime, then count<=4096 actual data reads.
 * Preserve 18 bits, publish [152,count) only after a complete successful read.
 * Owner exclusively supplies DataBuf; errors invalidate metadata for epoch.
 * Neither Full nor RAM size proves a hardware-trigger/pretrigger window.
 */
Capture144Status Capture144_RecordWindow(const FpgaProfile *,const Capture144WordOps *,
    uint32_t *words,uint32_t count,uint32_t timeout,uint32_t epoch,
    uint64_t period_ps_q16,CaptureWindow *);
#endif
