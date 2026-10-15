#ifndef WILDCAT_INTERLACE144_H
#define WILDCAT_INTERLACE144_H
#include "FpgaProfile.h"
/* Pure RAM processing. No ADC configuration, acquisition or flash writes. */
#define INTERLACE144_RATE 144000000u
#define INTERLACE144_DISCARD_PAIRS 152u
#define INTERLACE144_MAX_PAIRS 4096u
#define INTERLACE144_MIN_PAIRS 16u
#define INTERLACE144_Q10 1024

typedef enum {
    INTERLACE144_OK = 0,
    INTERLACE144_ARGUMENT,
    INTERLACE144_UNSUPPORTED,
    INTERLACE144_WINDOW,
    INTERLACE144_CAPACITY,
    INTERLACE144_DC_RESPONSE,
    INTERLACE144_DC_CLIPPED,
    INTERLACE144_GAIN,
    INTERLACE144_CALIBRATION
} Interlace144Status;

typedef struct {
    int32_t gain_q10, bias_q10;
    uint16_t family, protocol, implementation;
    uint32_t input_key;
    int valid;
} Interlace144Calibration;

typedef struct {
    uint32_t pairs, samples;
    uint32_t clips_a, clips_b, corrected_outside_adc;
} Interlace144Stats;

int Interlace144_Available(const FpgaProfile *profile);
int Interlace144_Unpack(const FpgaProfile *profile, uint16_t pair, uint8_t output[2]);
/* Each input starts AFTER the caller's FIFO pointer priming read, and still
 * includes 152 invalid pretrigger pairs. Applies to Trig_Always only.
 * FIFO word layout: A low byte, B high byte, no digital/status bits.
 * input_key is the caller's acquisition configuration token (including
 * input range, offsets, routing, interlace mode and clock). Changing any
 * setting must change this token and require fresh calibration.
 * Failure invalidates calibration unless destination is null/aliased, in
 * which case nothing is written. No partial coefficients are published.
 */
Interlace144Status Interlace144_Calibrate(const FpgaProfile *profile,
    uint32_t input_key, const uint16_t *low, uint32_t low_count,
    const uint16_t *high, uint32_t high_count, Interlace144Calibration *cal);
/* Output is signed Q10 ADC counts: corrected B[i], raw A[i], B[i+1], A[i+1].
 * Keep sub-count precision and out-of-range corrected values; no saturation.
 * Capacity is in int32_t samples. Buffers must be disjoint. On error output
 * and stats stay unchanged. Calibration is relative B->A, never voltage.
 */
Interlace144Status Interlace144_Process(const FpgaProfile *profile,
    uint32_t input_key, const Interlace144Calibration *cal,
    const uint16_t *pairs, uint32_t pair_count, int32_t *output,
    uint32_t capacity, Interlace144Stats *stats);
#endif
