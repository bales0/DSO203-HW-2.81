#include "Interlace144.h"

int Interlace144_Available(const FpgaProfile *profile)
{
    return FpgaProfile_Has(profile, FPGA_CAP_FIFO | FPGA_CAP_INTERLACE);
}
int Interlace144_Unpack(const FpgaProfile *profile, uint16_t pair, uint8_t output[2])
{
    if (!output || !Interlace144_Available(profile)) return 0;
    output[0] = (uint8_t)(pair >> 8);
    output[1] = (uint8_t)pair;
    return 1;
}

static int ValidWindow(uint32_t count)
{
    return count >= INTERLACE144_DISCARD_PAIRS + INTERLACE144_MIN_PAIRS &&
           count <= INTERLACE144_MAX_PAIRS;
}

/* Reject aliasing before writing any output; all lengths are bounded. */
static int Overlaps(const void *a, uint32_t a_size, const void *b, uint32_t b_size)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < a_size : x - y < b_size;
}

static Interlace144Status DcMean(const uint16_t *pairs, uint32_t count,
                                int32_t *a_q10, int32_t *b_q10)
{
    uint32_t i, sum_a = 0, sum_b = 0;
    for (i = INTERLACE144_DISCARD_PAIRS; i < count; ++i) {
        uint32_t a = pairs[i] & 255u, b = pairs[i] >> 8;
        if (a == 0 || a == 255 || b == 0 || b == 255)
            return INTERLACE144_DC_CLIPPED;
        sum_a += a;
        sum_b += b;
    }
    count -= INTERLACE144_DISCARD_PAIRS;
    *a_q10 = (int32_t)(sum_a * INTERLACE144_Q10 / count);
    *b_q10 = (int32_t)(sum_b * INTERLACE144_Q10 / count);
    if (*a_q10 < 10 * INTERLACE144_Q10 || *a_q10 > 245 * INTERLACE144_Q10 ||
        *b_q10 < 10 * INTERLACE144_Q10 || *b_q10 > 245 * INTERLACE144_Q10)
        return INTERLACE144_DC_CLIPPED;
    return INTERLACE144_OK;
}

Interlace144Status Interlace144_Calibrate(const FpgaProfile *profile,
    uint32_t input_key, const uint16_t *low, uint32_t low_count,
    const uint16_t *high, uint32_t high_count, Interlace144Calibration *cal)
{
    Interlace144Calibration next;
    Interlace144Status status;
    int32_t low_a, low_b, high_a, high_b, delta_a, delta_b;
    if (!cal) return INTERLACE144_ARGUMENT;
    /* Aliased destinations are rejected without modifying their inputs. */
    if ((profile && Overlaps(cal, sizeof *cal, profile, sizeof *profile)) ||
        (low && low_count <= INTERLACE144_MAX_PAIRS &&
         Overlaps(cal, sizeof *cal, low, low_count * sizeof *low)) ||
        (high && high_count <= INTERLACE144_MAX_PAIRS &&
         Overlaps(cal, sizeof *cal, high, high_count * sizeof *high)))
        return INTERLACE144_ARGUMENT;
    cal->valid = 0;
    if (!profile || !low || !high) return INTERLACE144_ARGUMENT;
    if (!Interlace144_Available(profile)) return INTERLACE144_UNSUPPORTED;
    if (!ValidWindow(low_count) || !ValidWindow(high_count)) return INTERLACE144_WINDOW;
    status = DcMean(low, low_count, &low_a, &low_b);
    if (status != INTERLACE144_OK) return status;
    status = DcMean(high, high_count, &high_a, &high_b);
    if (status != INTERLACE144_OK) return status;
    delta_a = high_a - low_a;
    delta_b = high_b - low_b;
    if (delta_a < 8 * INTERLACE144_Q10 || delta_b < 8 * INTERLACE144_Q10)
        return INTERLACE144_DC_RESPONSE;
    next.gain_q10 = delta_a * INTERLACE144_Q10 / delta_b;
    if (next.gain_q10 < 512 || next.gain_q10 > 2048) return INTERLACE144_GAIN;
    next.bias_q10 = low_a - (int32_t)((int64_t)low_b * next.gain_q10 / INTERLACE144_Q10);
    next.family = profile->family;
    next.protocol = profile->protocol;
    next.implementation = profile->implementation;
    next.input_key = input_key;
    next.valid = 1;
    *cal = next;
    return INTERLACE144_OK;
}

Interlace144Status Interlace144_Process(const FpgaProfile *profile,
    uint32_t input_key, const Interlace144Calibration *cal,
    const uint16_t *pairs, uint32_t pair_count, int32_t *output,
    uint32_t capacity, Interlace144Stats *stats)
{
    Interlace144Stats next = {0, 0, 0, 0, 0};
    uint32_t i, output_size, input_size;
    if (!profile || !cal || !pairs || !output || !stats) return INTERLACE144_ARGUMENT;
    if (!Interlace144_Available(profile)) return INTERLACE144_UNSUPPORTED;
    if (!ValidWindow(pair_count)) return INTERLACE144_WINDOW;
    next.pairs = pair_count - INTERLACE144_DISCARD_PAIRS;
    next.samples = 2 * next.pairs;
    if (capacity < next.samples) return INTERLACE144_CAPACITY;
    if (cal->valid != 1 || cal->family != profile->family ||
        cal->protocol != profile->protocol || cal->implementation != profile->implementation ||
        cal->input_key != input_key || cal->gain_q10 < 512 || cal->gain_q10 > 2048 ||
        cal->bias_q10 < -510 * INTERLACE144_Q10 || cal->bias_q10 > 255 * INTERLACE144_Q10)
        return INTERLACE144_CALIBRATION;
    output_size = next.samples * sizeof *output;
    input_size = pair_count * sizeof *pairs;
    if (Overlaps(output, output_size, pairs, input_size) ||
        Overlaps(output, output_size, cal, sizeof *cal) ||
        Overlaps(output, output_size, profile, sizeof *profile) ||
        Overlaps(output, output_size, stats, sizeof *stats) ||
        Overlaps(stats, sizeof *stats, pairs, input_size) ||
        Overlaps(stats, sizeof *stats, cal, sizeof *cal) ||
        Overlaps(stats, sizeof *stats, profile, sizeof *profile))
        return INTERLACE144_ARGUMENT;
    for (i = INTERLACE144_DISCARD_PAIRS; i < pair_count; ++i) {
        uint32_t a = pairs[i] & 255u, b = pairs[i] >> 8;
        int32_t corrected = (int32_t)b * cal->gain_q10 + cal->bias_q10;
        uint32_t index = 2 * (i - INTERLACE144_DISCARD_PAIRS);
        output[index] = corrected;
        output[index + 1] = (int32_t)a * INTERLACE144_Q10;
        next.clips_a += a == 0 || a == 255;
        next.clips_b += b == 0 || b == 255;
        next.corrected_outside_adc += corrected < 0 || corrected > 255 * INTERLACE144_Q10;
    }
    *stats = next;
    return INTERLACE144_OK;
}
