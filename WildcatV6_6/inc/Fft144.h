#ifndef WILDCAT_FFT144_H
#define WILDCAT_FFT144_H
#include "Frame144.h"
#include "Clock144.h"
/* Shares Normal's fr/fi and renderer; owns no sample array. IRQ figures cover
 * TIM2/TIM4 handler-body cycles while enabled; exception entry/exit are
 * excluded, and instrumentation adds some overhead. */
typedef struct {
    volatile uint32_t enabled,irq_cycles,irq_calls;
    uint32_t cycles,max_cycles,updates,irq_permille;
    uint32_t sampled_at,sampled_irq,sampled_calls,epoch,start,rate_hz,last_done;
    unsigned ready,source,setting,clipped;
    char status[36];
} Fft144State;
extern Fft144State Fft144;
void Fft144_Reset(void);
int Fft144_Update(const Frame144 *,unsigned start,unsigned source,unsigned setting,uint32_t now);
uint32_t Fft144_Frequency(unsigned bin);
void Fft144_Labels(unsigned cursor);
void Process_Fft(unsigned source,unsigned shift);
/* No interrupt masking: Cortex-M3 aligned 32-bit accesses are atomic, and
 * TIM2/TIM4 share preemption priority. DWT is enabled by the 144 owner. */
static inline uint32_t Fft144_IrqBegin(void)
{ return Fft144.enabled?DWT_CYCLES:0; }
static inline void Fft144_IrqEnd(uint32_t started)
{ if(started){Fft144.irq_cycles+=(uint32_t)(DWT_CYCLES-started);++Fft144.irq_calls;} }
#endif
