/* Actual 512-point native FFT and actual calibrated Frame144 source.
 * CPU cycles and IRQ traffic are simulated; this does not benchmark DS203. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Fft144.h"
#include "Function.h"
#include "Menu.h"
#include "Process.h"
static unsigned checks;static uint32_t clock_cycles=72000000;
uint32_t native_test_demcr,native_test_dwt;
uint32_t NativeTestCycles(void){clock_cycles+=720;return clock_cycles;}
u8 Current,Detail[14];menu Title[13][4];
const char F_UNIT[12]={'H','z',0,'K','@',0,'M','@',0,'G','@',0};
u8 ChartLogic(void){return 0;}
void Native_FftInt2Str(char *,s32,const char *,u8,u8,s8);
static s32 formatted[4];static unsigned formats;
void Int2Str(char *out,s32 value,const char *unit,u8 n,u8 mode,s8 shift)
{formatted[formats++%4]=value;Native_FftInt2Str(out,value,unit,n,mode,shift);}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void Legacy_Process_Fft(void);
static uint32_t words[4096];static Frame144 frame;
static int Ideal(unsigned n,unsigned bin,unsigned amp,unsigned second)
{return 120+(int)lround(amp*sin(6.283185307179586*bin*n/512.0)+second*sin(6.283185307179586*79*n/512.0));}
static void Fixture(unsigned bin,unsigned amp,unsigned second,unsigned phase,unsigned mismatch)
{
    unsigned i,a,b,ai,bi;
    Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024};
    Frame144_Reset(&frame,1);frame.words=words;frame.range=0;frame.calibration=cal;
    frame.capture.sample_period_ps_q16=(1000000000000ULL*65536+72000000)/144000000;
    frame.capture.fifo_words=4096;frame.capture.valid_begin=152;frame.capture.valid_words=1024;
    frame.capture.time_samples=2048;frame.capture.status=CAPTURE_WINDOW_READY;
    frame.capture.sample_phase=(uint8_t)phase;
    memset(words,0xff,sizeof words);
    for(i=0;i<1024;++i) {
        ai=2*i+(phase==CAPTURE_PHASE_B_A);bi=2*i+(phase==CAPTURE_PHASE_A_B);
        a=(unsigned)Ideal(ai,bin,amp,second);b=(unsigned)Ideal(bi,bin,amp,second);
        if(mismatch==1)b+=7;
        if(mismatch==2)b=(b+64)/2;
        words[152+i]=a|(b<<8)|0x30000;
    }
    if(mismatch==1)frame.calibration.bias_q10=-7*1024;
    if(mismatch==2){frame.calibration.gain_q10=2048;frame.calibration.bias_q10=-64*1024;}
    CHECK(Frame144_Valid(&frame));
}
static void Compute(unsigned source,unsigned setting)
{clock_cycles+=8000000;CHECK(Fft144_Update(&frame,0,source,setting,clock_cycles));}
int main(void)
{
    unsigned source,setting,i,k,phase,mismatch;static s32 saved[512],input[512],reference[8][256];uint32_t random=9;
    LoadNwave();
    /* Unchanged Normal vectors for every FFT source and log/linear setting. */
    for(source=11;source<16;++source)for(setting=0;setting<10;++setting)for(k=0;k<6;++k) {
        AutoFFT=(u8)setting;_4_source=(s16)source;
        for(i=0;i<512;++i){random=random*1664525u+1013904223u;input[i]=(int32_t)((random>>16)%1001)-500;}
        if(k<4)for(i=0;i<512;++i)input[i]=(s32)(1000*sin(6.283185307179586*(k*7+3)*i/512.0));
        memcpy(fr,input,sizeof input);Legacy_Process_Fft();memcpy(saved,fi,sizeof saved);
        memcpy(fr,input,sizeof input);Process_Fft(source,0);CHECK(!memcmp(saved,fi,sizeof saved));
    }
    /* Actual B->A/A->B data, DC removal, offset/gain correction and two windows. */
    for(source=14;source<=15;++source)for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase)
    for(mismatch=0;mismatch<3;++mismatch)for(k=0;k<8;++k) {
        static const unsigned bins[]={3,8,16,31,64,100,200,240};
        Fft144_Reset();Fixture(bins[k],40,0,phase,mismatch);Compute(source,2);
        CHECK(imax==(int)bins[k]);CHECK(Fft144.ready && Fft144.enabled && !Fft144.clipped);
        if(phase==CAPTURE_PHASE_B_A && mismatch==0)memcpy(reference[k],fi,sizeof reference[k]);
        else for(i=0;i<256;++i)CHECK(abs(fi[i]-reference[k][i])<=(mismatch==2?8:0));
        CHECK(Fft144_Frequency(bins[k])==((uint64_t)144000000u*bins[k]+256u)/512u);
        for(i=0;i<256;++i)CHECK(fi[i]>=0 && fi[i]<=198);
        ++frame.capture.epoch;clock_cycles+=8000000;
        CHECK(Fft144_Update(&frame,512,source,2,clock_cycles));CHECK(imax==(int)bins[k]);
    }
    /* Every analog range and absolute gain/offset accepts the same adapter;
     * a changed epoch owns its calibration and its frequency scale. */
    for(i=0;i<8;++i) {
        Fft144_Reset();Fixture(16,25,0,CAPTURE_PHASE_B_A,1);frame.range=i;
        frame.calibration.a_gain_q10=512+(int32_t)i*128;frame.calibration.a_bias_q10=(int32_t)i*1000;
        CHECK(Frame144_Valid(&frame));Compute(14,2);CHECK(imax==16 && !Fft144.clipped);
    }
    Fft144_Reset();Fixture(11,40,15,CAPTURE_PHASE_B_A,1);Compute(14,2);
    CHECK(imax==11 && fi[79]>fi[75]+10);CHECK(Fft144_Frequency(256)==72000000);
    formats=0;Fft144_Labels(16);CHECK(formatted[0]==72000000 && formatted[1]==8437500);
    CHECK(formatted[2]==4500000 && formatted[3]==3093750);
    CHECK(strstr(NFreqStr,"M@") && fabs(strtod(NFreqStr,0)-72.0)<0.001);
    CHECK(strstr(FreqDivStr,"M@") && fabs(strtod(FreqDivStr,0)-8.438)<0.001);
    CHECK(strstr(FreqT1Str,"M@") && fabs(strtod(FreqT1Str,0)-4.5)<0.001);
    Fft144_Labels(300);CHECK(!strcmp(FreqT1Str,"--"));
    /* Same frozen epoch is not recomputed. A new epoch cannot exceed 10 Hz. */
    {uint32_t count=Fft144.updates,done=Fft144.last_done;
     CHECK(!Fft144_Update(&frame,0,14,2,done+8000000) && Fft144.updates==count);
     ++frame.capture.epoch;
     CHECK(!Fft144_Update(&frame,0,14,2,done+7199999));
     clock_cycles=done+7200000;CHECK(Fft144_Update(&frame,0,14,2,clock_cycles));
     CHECK(Fft144.updates==count+1);
     CHECK(!Fft144_Update(&frame,0,15,2,Fft144.last_done+1));
     clock_cycles+=8000000;CHECK(Fft144_Update(&frame,0,15,2,clock_cycles));}
    /* Rollover-safe limiter and unsigned IRQ accounting with known duty. */
    Fft144.last_done=0xfffffff0u;++frame.capture.epoch;clock_cycles=0x00800000u;
    CHECK(Fft144_Update(&frame,0,15,2,clock_cycles));
    {uint32_t began,before=Fft144.irq_cycles;
     began=Fft144_IrqBegin();clock_cycles+=72000;Fft144_IrqEnd(began);
     CHECK(Fft144.irq_cycles-before==72720 && Fft144.irq_calls==1);
     Fft144.enabled=0;CHECK(!Fft144_IrqBegin());Fft144.enabled=1;
     ++frame.capture.epoch;clock_cycles+=8000000;CHECK(Fft144_Update(&frame,0,15,2,clock_cycles));
     CHECK(Fft144.irq_permille>0 && Fft144.irq_permille<=1000 && strstr(Fft144.status,"IRQ"));}
    for(setting=0;setting<10;++setting){Fft144_Reset();Fixture(0,0,0,CAPTURE_PHASE_B_A,1);Compute(14,setting);for(i=0;i<256;++i)CHECK(!fi[i]);}
    Fft144_Reset();Fixture(31,70,0,CAPTURE_PHASE_B_A,0);Compute(14,0);CHECK(imax==31 && !Fft144.clipped);
    ++frame.capture.epoch;words[152]=0xff00;clock_cycles+=8000000;
    CHECK(Fft144_Update(&frame,0,14,0,clock_cycles) && Fft144.clipped && strstr(Fft144.status,"CLIP"));
    /* Frozen sample period, not the live clock or selected time/div. */
    frame.capture.sample_period_ps_q16=1000000000000ULL*65536/144014400u;
    ++frame.capture.epoch;clock_cycles+=8000000;
    CHECK(Fft144_Update(&frame,0,14,2,clock_cycles));CHECK(Fft144_Frequency(256)==72007200);
    CHECK(!Fft144_Update(&frame,0,12,2,clock_cycles+8000000));
    CHECK(!Fft144_Update(&frame,frame.capture.time_samples-511,14,2,clock_cycles+8000000));
    frame.capture.status=CAPTURE_WINDOW_ARMED;CHECK(!Fft144_Update(&frame,0,14,2,clock_cycles+8000000));
    Fft144_Reset();CHECK(!Fft144.ready && !Fft144.enabled && !Fft144_Frequency(1));
    for(i=0;i<512;++i)CHECK(!fi[i]);
    printf("PASS FFT144: %u checks; actual native FFT, legacy vectors, corrected ADC order/gain/DC, peaks/Hz, HOLD/cache/10Hz/wrap, IRQ accounting, invalid frames; simulated cycles only\n",checks);
    return 0;
}
