#ifndef WILDCAT_PHASE144_H
#define WILDCAT_PHASE144_H
#include "Interlace144.h"
#define PHASE144_MAX_LAG 16
#define PHASE144_ERROR_COUNT 33
#define PHASE144_MAX_FRAMES 3u
typedef struct {
    uint64_t errors[PHASE144_ERROR_COUNT];
    Interlace144Calibration calibration;
    uint32_t marker, frames, fit_samples, clips_a, clips_b;
    uint8_t min_a, max_a, min_b, max_b;
} Phase144State;
typedef struct {
    int32_t lag8;
    uint32_t near_best, error_q10[3], span_a, span_b, clips_a, clips_b;
    int valid;
} Phase144Result;
void Phase144_Reset(Phase144State *state);
/* As in INT144 v3: lag searches -2..+2 ADC periods in 1/8 steps.
 * Inputs retain the 152 history pairs; they are excluded from all metrics.
 * Up to three frames per state; calibration/identity/key must stay equal.
 * Nonoverlapping buffers required; validation errors do not change state.
 */
Interlace144Status Phase144_Add(const FpgaProfile *profile, uint32_t input_key,
    const Interlace144Calibration *cal, const uint16_t *pairs,
    uint32_t count, Phase144State *state);
Interlace144Status Phase144_Finish(const Phase144State *state, Phase144Result *result);
/* 0=inconclusive, -1=half-cycle with B earlier than A, +1=A earlier. */
int Phase144_Compare(const Phase144Result *separate, const Phase144Result *interlace);
#endif
