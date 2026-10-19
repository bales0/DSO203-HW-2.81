#ifndef WILDCAT_FFT144_H
#define WILDCAT_FFT144_H
#include "Frame144.h"
#include "Clock144.h"
/* Shares Normal's fr/fi and renderer; owns no sample array.
 * last_done is the functional 10 Hz refresh limiter, not profiling. */
typedef struct {
    uint32_t epoch,start,rate_hz,last_done;
    unsigned ready,source,setting,clipped;
} Fft144State;
extern Fft144State Fft144;
void Fft144_Reset(void);
/* start requests the first FIR center in raw sample units. It is clamped
 * to a complete filtered window, including real history on both sides. */
int Fft144_Update(const Frame144 *,unsigned start,unsigned base,unsigned source,unsigned setting,uint32_t now);
uint32_t Fft144_Frequency(unsigned bin);
void Fft144_Labels(unsigned cursor);
void Process_Fft(unsigned source,unsigned shift);
#endif
