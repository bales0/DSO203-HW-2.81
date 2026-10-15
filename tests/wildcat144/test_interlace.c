#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Interlace144.h"
#define COUNT (INTERLACE144_DISCARD_PAIRS + 512u)
#define KEY 0x1234u
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint16_t low[INTERLACE144_MAX_PAIRS], high[INTERLACE144_MAX_PAIRS], frame[INTERLACE144_MAX_PAIRS];
static int32_t output[2 * INTERLACE144_MAX_PAIRS + 1];
static void fill(uint16_t *data, unsigned n, unsigned a, unsigned b)
{
    unsigned i;
    for (i = 0; i < n; ++i) data[i] = i < INTERLACE144_DISCARD_PAIRS ? 0xffffu : (uint16_t)(a | b << 8);
}
static Interlace144Calibration calibrate(FpgaProfile *p, unsigned la, unsigned lb, unsigned ha, unsigned hb)
{
    Interlace144Calibration cal;
    fill(low, COUNT, la, lb); fill(high, COUNT, ha, hb);
    CHECK(Interlace144_Calibrate(p, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_OK);
    return cal;
}
int main(void)
{
    FpgaProfile w11 = FpgaProfile_Decode(0x5731, 0x0101, 0);
    FpgaProfile w12 = FpgaProfile_Decode(0x5731, 0x0101, 0x0102);
    FpgaProfile unknown = FpgaProfile_Decode(0x5731, 0x0101, 0x0103);
    FpgaProfile legacy = FpgaProfile_Decode(0x5731, 0, 0);
    Interlace144Calibration cal, saved;
    Interlace144Stats stats, before;
    unsigned i, block;
    CHECK(Interlace144_Available(&w11) && Interlace144_Available(&w12));
    CHECK(!Interlace144_Available(&unknown) && !Interlace144_Available(&legacy));
    CHECK(!Interlace144_Available(NULL));
    cal = calibrate(&w11, 40, 40, 100, 80);
    CHECK(cal.gain_q10 == 1536 && cal.bias_q10 == -20480 && cal.valid == 1);
    fill(frame, COUNT, 33, 50);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_OK);
    CHECK(stats.pairs == 512 && stats.samples == 1024 && !stats.clips_a && !stats.clips_b);
    CHECK(output[0] == 55 * 1024 && output[1] == 33 * 1024);
    CHECK(output[1022] == 55 * 1024 && output[1023] == 33 * 1024);
    frame[INTERLACE144_DISCARD_PAIRS] = 0;
    frame[INTERLACE144_DISCARD_PAIRS + 1] = 0xffff;
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_OK);
    CHECK(stats.clips_a == 2 && stats.clips_b == 2 && stats.corrected_outside_adc == 2);
    CHECK(output[0] == -20 * 1024 && output[2] == 371200);
    /* Reject errors without publishing partial data/statistics. */
    before = stats; output[0] = 123456;
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1023, &stats) == INTERLACE144_CAPACITY);
    CHECK(output[0] == 123456 && !memcmp(&before, &stats, sizeof stats));
    CHECK(Interlace144_Process(&unknown, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_UNSUPPORTED);
    CHECK(Interlace144_Process(&w11, KEY+1, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_CALIBRATION);
    CHECK(Interlace144_Process(&w12, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_CALIBRATION);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, 152, output, 1024, &stats) == INTERLACE144_WINDOW);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, 0xffffffffu, output, 0xffffffffu, &stats) == INTERLACE144_WINDOW);
    CHECK(Interlace144_Process(&w11, KEY, &cal, NULL, COUNT, output, 1024, &stats) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(NULL, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(&w11, KEY, NULL, frame, COUNT, output, 1024, &stats) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, NULL, 1024, &stats) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, NULL) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, (int32_t *)frame, 1024, &stats) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, (Interlace144Stats *)output) == INTERLACE144_ARGUMENT);
    CHECK(output[0] == 123456 && !memcmp(&before, &stats, sizeof stats));
    saved = cal; cal.valid = 0;
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_CALIBRATION);
    cal = saved; cal.gain_q10 = 0x7fffffff;
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_CALIBRATION);
    cal = saved; cal.bias_q10 = (-2147483647 - 1);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_CALIBRATION);
    /* Every raw 16-bit word, both supported identities, with sentinel. */
    for (i = 0; i < 2; ++i) {
        FpgaProfile *profile = i ? &w12 : &w11;
        cal = calibrate(profile, 40, 40, 80, 80);
        for (block = 0; block < 128; ++block) {
            unsigned j;
            fill(frame, COUNT, 0, 0);
            for (j = 0; j < 512; ++j) frame[152+j] = (uint16_t)(block * 512 + j);
            output[1024] = 987654;
            CHECK(Interlace144_Process(profile, KEY, &cal, frame, COUNT, output, 1024, &stats) == INTERLACE144_OK);
            for (j = 0; j < 512; ++j) {
                unsigned raw = block * 512 + j;
                CHECK(output[2*j] == (int32_t)(raw >> 8) * 1024);
                CHECK(output[2*j+1] == (int32_t)(raw & 255) * 1024);
            }
            CHECK(output[1024] == 987654 && stats.samples == 1024);
        }
    }
    /* Minimum/maximum windows and unequal calibration lengths. */
    fill(low, 168, 40, 40); fill(high, 4096, 80, 80);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, 168, high, 4096, &cal) == INTERLACE144_OK);
    CHECK(cal.gain_q10 == 1024 && cal.bias_q10 == 0);
    fill(frame, 4096, 70, 80); output[7888] = 999;
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, 4096, output, 7888, &stats) == INTERLACE144_OK);
    CHECK(stats.pairs == 3944 && stats.samples == 7888 && output[7888] == 999);
    CHECK(Interlace144_Process(&w11, KEY, &cal, frame, 168, output, 32, &stats) == INTERLACE144_OK);
    CHECK(stats.pairs == 16 && stats.samples == 32);
    /* Invalid DC captures must invalidate previously valid calibration. */
    fill(low, COUNT, 40, 40); fill(high, COUNT, 44, 44);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_DC_RESPONSE && !cal.valid);
    fill(high, COUNT, 30, 30);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_DC_RESPONSE);
    fill(high, COUNT, 57, 48);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_GAIN);
    fill(high, COUNT, 48, 57);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_GAIN);
    fill(high, COUNT, 48, 56);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_OK && cal.gain_q10 == 512);
    fill(high, COUNT, 56, 48);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_OK && cal.gain_q10 == 2048);
    high[152] = 0;
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_DC_CLIPPED && !cal.valid);
    fill(high, COUNT, 246, 246);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_DC_CLIPPED);
    CHECK(Interlace144_Calibrate(&unknown, KEY, low, COUNT, high, COUNT, &cal) == INTERLACE144_UNSUPPORTED);
    CHECK(Interlace144_Calibrate(&w11, KEY, NULL, COUNT, high, COUNT, &cal) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, 167, high, COUNT, &cal) == INTERLACE144_WINDOW);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, 4097, &cal) == INTERLACE144_WINDOW);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, NULL) == INTERLACE144_ARGUMENT);
    CHECK(Interlace144_Calibrate(&w11, KEY, low, COUNT, high, COUNT, (Interlace144Calibration *)low) == INTERLACE144_ARGUMENT);
    printf("PASS Wildcat 144: %u checks, two profiles, all 65536 raw words per profile\n", checks);
    return 0;
}
