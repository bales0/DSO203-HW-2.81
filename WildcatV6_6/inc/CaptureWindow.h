#ifndef WILDCAT_CAPTURE_WINDOW_H
#define WILDCAT_CAPTURE_WINDOW_H
#include <stdint.h>

#define CAPTURE_WINDOW_FIFO_WORDS 4096u
#define CAPTURE_WINDOW_Q16_ONE 65536u

typedef enum {
    CAPTURE_WINDOW_EMPTY, CAPTURE_WINDOW_ARMED, CAPTURE_WINDOW_READY,
    CAPTURE_WINDOW_TIMEOUT, CAPTURE_WINDOW_CANCELLED, CAPTURE_WINDOW_ERROR
} CaptureWindowStatus;
typedef enum {
    CAPTURE_PHASE_UNKNOWN, CAPTURE_PHASE_NATIVE,
    CAPTURE_PHASE_B_A, CAPTURE_PHASE_A_B
} CaptureWindowPhase;
typedef enum {
    CAPTURE_TRIGGER_NONE, CAPTURE_TRIGGER_SOFTWARE, CAPTURE_TRIGGER_HARDWARE_A,
    CAPTURE_TRIGGER_HARDWARE_C, CAPTURE_TRIGGER_HARDWARE_D
} CaptureWindowTrigger;

#define CAPTURE_WINDOW_PRETRIGGER 1u
#define CAPTURE_WINDOW_TRUNCATED 2u
#define CAPTURE_WINDOW_FROZEN 4u
#define CAPTURE_WINDOW_HAS_TRIGGER 8u
#define CAPTURE_WINDOW_FIFO_READY 4u
#define CAPTURE_WINDOW_FIFO_FULL 8u
#define CAPTURE_WINDOW_FIFO_START 32u

/* RAM metadata, never a flash/file format. The owner supplies the epoch;
 * READY publishes a complete frame, FROZEN preserves that epoch in HOLD.
 * Raw 18-bit FIFO words live in the existing DataBuf, NOT in this structure.
 * Normal, 144 and calibration must own that buffer exclusively. Priming
 * reads are not counted in fifo_words. Valid words are the half-open range
 * [valid_begin, valid_begin+valid_words) of the words actually read.
 * All Q16 positions are relative to the first VALID temporal sample.
 * Native mode has one time sample/word; interlace has two in sample_phase
 * order. The owner must verify that phase and choose the valid interval;
 * these types do not infer either from RAM capacity or a Full flag.
 * sample_period_ps_q16 stores picoseconds/sample with 16 fractional bits.
 * TRUNCATED describes omitted capture data, not a clipped display window.
 * PRETRIGGER promises valid samples before the declared trigger; AUTO with
 * no found edge has trigger_kind NONE, trigger_q16=0, HAS_TRIGGER clear.
 */
typedef struct {
    uint64_t sample_period_ps_q16;
    uint32_t epoch;
    uint32_t fifo_words, valid_begin, valid_words, time_samples;
    uint32_t fifo_status;
    uint32_t trigger_q16, first_visible_q16;
    uint8_t status, sample_phase, trigger_kind, flags;
} CaptureWindow;

/* Clears all old-frame coordinates/counts before a new epoch or a failure.
 * READY is rejected as a reset state and becomes ERROR. Publication is an
 * explicit owner action after acquisition and validation, not a reset.
 */
void CaptureWindow_Reset(CaptureWindow *,uint32_t epoch,CaptureWindowStatus);
int CaptureWindow_Valid(const CaptureWindow *);
#endif
