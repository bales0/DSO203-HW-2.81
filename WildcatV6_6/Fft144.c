#include "Fft144.h"
#include "Function.h"
#include "Menu.h"
#include "Process.h"
#include <string.h>
extern const u8 Window[512];
Fft144State Fft144;
void Fft144_Reset(void)
{
    Fft144.enabled=0;
    memset(&Fft144,0,sizeof Fft144);
    memset(fi,0,FFTSize*sizeof *fi);
    strcpy(Fft144.status,"FFT waiting for capture");
}
uint32_t Fft144_Frequency(unsigned bin)
{
    if(!Fft144.ready || bin>256)return 0;
    return (uint32_t)(((uint64_t)Fft144.rate_hz*bin+256u)/512u);
}
static unsigned Decimal(char *out,unsigned at,uint32_t value)
{
    char digits[10];unsigned n=0;
    do{digits[n++]=(char)('0'+value%10u);value/=10u;}while(value);
    while(n)out[at++]=digits[--n];
    out[at]=0;return at;
}
static void Status(uint32_t now)
{
    uint32_t elapsed=now-Fft144.sampled_at,irq=Fft144.irq_cycles;
    unsigned n;char *text=Fft144.status;
    if(elapsed)Fft144.irq_permille=(uint32_t)((uint64_t)(irq-Fft144.sampled_irq)*1000u/elapsed);
    if(Fft144.irq_permille>1000)Fft144.irq_permille=1000;
    Fft144.sampled_at=now;Fft144.sampled_irq=irq;Fft144.sampled_calls=Fft144.irq_calls;
    memcpy(text,"FFT ",4);n=Decimal(text,4,Fft144.cycles/72000u);
    text[n++]='.';text[n++]=(char)('0'+(Fft144.cycles/7200u)%10u);
    memcpy(text+n,"ms IRQ ",7);n+=7;n=Decimal(text,n,(Fft144.irq_permille+5u)/10u);
    text[n++]='%';if(Fft144.clipped){memcpy(text+n," CLIP",5);n+=5;}text[n]=0;
}
int Fft144_Update(const Frame144 *frame,unsigned start,unsigned source,unsigned setting,uint32_t now)
{
    Scope144Source samples;int64_t sum=0,value;int32_t q,mean;
    unsigned i,clipped=0,shift=0;uint32_t peak=0;uint32_t started,finished;
    if(!Scope144_IsFft(source) || setting>9 || !Frame144_Source(frame,&samples) || samples.count<512 ||
        start>samples.count-512)return 0;
    Fft144.enabled=1;
    if(Fft144.ready && ((Fft144.epoch==frame->capture.epoch && Fft144.start==start &&
        Fft144.source==source && Fft144.setting==setting) || (uint32_t)(now-Fft144.last_done)<7200000u))return 0;
    started=DWT_CYCLES;
    if(!Fft144.updates){Fft144.sampled_at=started;Fft144.sampled_irq=Fft144.irq_cycles;}
    for(i=0;i<512;++i){if(!samples.read(samples.user,start+i,&q))return 0;sum+=q;}
    mean=(int32_t)(sum/512);
    AutoFFT=(u8)setting;
    if(setting>1)FFTGain=(u8)(setting-2);else if(!setting)FFTGain=8;
    if(FFTGain>8)FFTGain=8;
    for(i=0;i<512;++i) {
        unsigned clip=0;
        if(!Frame144_Read(frame,start+i,&q,&clip))return 0;
        clipped|=clip;
        /* Work in calibrated ADC counts (Q10) before the Hann window, with
         * DC removed over this exact window. Rescale before integer FFT. */
        value=((int64_t)q-mean)*Window[i]*(1u<<(2u+FFTGain));
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
    finished=DWT_CYCLES;Fft144.cycles=finished-started;
    if(Fft144.cycles>Fft144.max_cycles)Fft144.max_cycles=Fft144.cycles;
    Fft144.rate_hz=(uint32_t)((1000000000000ULL*65536u+samples.period_ps_q16/2u)/samples.period_ps_q16);
    Fft144.epoch=frame->capture.epoch;Fft144.start=start;Fft144.source=source;Fft144.setting=setting;
    Fft144.clipped=clipped;Fft144.ready=1;Fft144.last_done=finished;++Fft144.updates;
    Status(finished);return 1;
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
