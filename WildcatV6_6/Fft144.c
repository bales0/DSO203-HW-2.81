#include "Fft144.h"
#include "Function.h"
#include "Menu.h"
#include "Process.h"
#include <string.h>
extern const u8 Window[512];
Fft144State Fft144;
/* Symmetric Hamming-windowed sinc, unity DC gain in Q15. Cutoffs
 * 0.225/0.1125 of the raw rate; 63/127 taps. Only half plus center is stored.
 * Stopband from the new Nyquist limit: >43 dB; passband to 0.8 Nyquist:
 * <0.05 dB ripple. Frame144 applies the unchanged ADC calibration first. */
static const int16_t fir2[32]={
    -4,-29,-5,36,21,-45,-48,49,
    90,-38,-146,0,206,76,-257,-198,
    277,368,-239,-579,110,818,149,-1064,
    -602,1292,1391,-1477,-3036,1598,10288,14764
};
static const int16_t fir4[64]={
    7,-2,-11,-15,-12,-3,10,19,
    20,11,-6,-24,-33,-26,-3,26,
    47,48,22,-20,-60,-76,-55,0,
    65,107,100,39,-53,-132,-156,-101,
    15,141,214,187,58,-121,-265,-293,
    -173,56,291,412,338,75,-271,-534,
    -564,-302,170,647,882,697,87,-739,
    -1410,-1518,-788,799,2951,5143,6775,7382
};
/* fi is scratch until Process_Fft clears it; fr receives the filtered
 * sequence. Its 128-cell ring covers the longest 127-tap convolution. */
static int __attribute__((section(".text.fft144_filter"))) FilterInput(const Scope144Source *samples,
    unsigned begin,unsigned step,unsigned half,unsigned *clipped)
{
    const int16_t *c=step==2?fir2:fir4;
    unsigned i,j,out=0,count=511u*step+2u*half+1u;
    for(i=0;i<count;++i) {
        int32_t q;int status=samples->read(samples->user,begin+i,&q);int64_t value;
        if(!status)return 0;
        *clipped|=!!(status&2);fi[i&127u]=(s32)q;
        if(i<2u*half || ((i-2u*half)&(step-1u)))continue;
        value=(int64_t)c[half]*fi[(i-half)&127u];
        for(j=0;j<half;++j)
            value+=(int64_t)c[j]*((int64_t)fi[(i-j)&127u]+fi[(i-2u*half+j)&127u]);
        value=value>=0?(value+16384)/32768:-((-value+16384)/32768);
        if(value<INT32_MIN || value>INT32_MAX)return 0;
        fr[out++]=(s32)value;
    }
    return out==512;
}

void Fft144_Reset(void)
{
    memset(&Fft144,0,sizeof Fft144);
    memset(fi,0,FFTSize*sizeof *fi);
}
uint32_t Fft144_Frequency(unsigned bin)
{
    if(!Fft144.ready || bin>256)return 0;
    return (uint32_t)(((uint64_t)Fft144.rate_hz*bin+256u)/512u);
}
int Fft144_Update(const Frame144 *frame,unsigned start,unsigned base,unsigned source,unsigned setting,uint32_t now)
{
    Scope144Source samples;int64_t sum=0,value;int32_t mean;
    unsigned i,clipped=0,shift=0,step,half,last,same;uint32_t peak=0,finished,rate;
    if(base>2 || !Scope144_IsFft(source) || setting>9 || !Frame144_Source(frame,&samples))return 0;
    /* Match Normal's 72/72/36 MSa/s at 0.1/0.2/0.5 us/div. Acquisition
     * remains interlaced; only the FFT input is filtered and decimated. */
    step=base==2?4u:2u;half=16u*step-1u;
    if(samples.count<511u*step+2u*half+1u)return 0;
    last=samples.count-1u-half-511u*step;
    if(start<half)start=half;
    if(start>last)start=last;
    rate=(uint32_t)((1000000000000ULL*65536u+samples.period_ps_q16*step/2u)/(samples.period_ps_q16*step));
    same=Fft144.start==start && Fft144.rate_hz==rate && Fft144.source==source && Fft144.setting==setting;
    if(Fft144.ready && same && Fft144.epoch==frame->capture.epoch)return 0;
    /* A changed axis cannot label an older FFT as current. Keep the limiter
     * after invalidation, including DWT rollover; zero rate denotes reset. */
    if(Fft144.rate_hz && (uint32_t)(now-Fft144.last_done)<7200000u) {
        if(!same)Fft144.ready=0;
        return 0;
    }
    Fft144.ready=0;
    if(!FilterInput(&samples,start-half,step,half,&clipped))return 0;
    for(i=0;i<512;++i)sum+=fr[i];
    mean=(int32_t)(sum/512);
    AutoFFT=(u8)setting;
    if(setting>1)FFTGain=(u8)(setting-2);else if(!setting)FFTGain=8;
    if(FFTGain>8)FFTGain=8;
    for(i=0;i<512;++i) {
        value=((int64_t)fr[i]-mean)*Window[i]*(1u<<(2u+FFTGain));
        value/=1024*(source==15?100:80);
        if(value>0x3fffffffu || value< -0x3fffffff)return 0;
        if((uint32_t)(value<0?-value:value)>peak)peak=(uint32_t)(value<0?-value:value);
        fr[i]=(s32)value;
    }
    /* Shared original magnitude/log/summed-bin rules use the selected source. */
    /* Keep all twiddle products in signed 32-bit range without hard-clipping
     * the signal. Restore this common scale before magnitude/log display. */
    while(peak>65535u){peak>>=1;++shift;}
    if(shift)for(i=0;i<512;++i)fr[i]/=(s32)(1u<<shift);
    Process_Fft(source,shift);
    PeakFreq=2;imax=0;
    for(i=1;i<256;++i)if(fi[i]>PeakFreq){PeakFreq=(short)fi[i];imax=(int)i;}
    if(setting==1) {
        if(PeakFreq>=198 && FFTGain) --FFTGain;
        else if(PeakFreq<100 && FFTGain<7)++FFTGain;
    }
    finished=DWT_CYCLES;
    Fft144.rate_hz=rate;
    Fft144.epoch=frame->capture.epoch;Fft144.start=start;Fft144.source=source;Fft144.setting=setting;
    Fft144.clipped=clipped;Fft144.ready=1;Fft144.last_done=finished;
    return 1;
}
void Fft144_Labels(unsigned cursor)
{
    if(!Fft144.ready)return;
    NFreq=Fft144_Frequency(256);
    Int2Str(NFreqStr,(s32)NFreq,F_UNIT,4,UNSIGN,0);
    Int2Str(FreqDivStr,(s32)Fft144_Frequency(30),F_UNIT,4,UNSIGN,0);
    if(cursor<256)Int2Str(FreqT1Str,(s32)Fft144_Frequency(cursor),F_UNIT,4,UNSIGN,0);
    else strcpy(FreqT1Str,"--");
    Int2Str(PeakFreqStr,(s32)Fft144_Frequency((unsigned)imax),F_UNIT,4,UNSIGN,0);
}
