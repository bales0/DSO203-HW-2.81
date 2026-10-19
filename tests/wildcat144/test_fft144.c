/* Actual 512-point native FFT and actual calibrated Frame144 source.
 * The limiter clock is simulated; this does not benchmark DS203. */
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
static int coefficients[2][127];
static const double pi=3.14159265358979323846;
extern const u8 Window[512];
static unsigned Step(unsigned base){return base==2?4u:2u;}
/* Independent full-coefficient oracle; production stores symmetric halves. */
static void Coefficients(void)
{
 unsigned base,j,n,half;double h[127],sum;int total;
 for(base=0;base<2;++base){
  n=base?127:63;half=n/2;sum=0;total=0;
  for(j=0;j<n;++j){double m=(double)j-half,cut=base?.1125:.225;
   h[j]=(j==half?2*cut:sin(2*pi*cut*m)/(pi*m))*(.54-.46*cos(2*pi*j/(n-1)));sum+=h[j];}
  for(j=0;j<n;++j){coefficients[base][j]=(int)lround(h[j]*32768/sum);total+=coefficients[base][j];}
  coefficients[base][half]+=32768-total;
 }
}
static double Response(unsigned base,double f)
{
 unsigned j,n=base?127:63;double re=0,im=0;
 for(j=0;j<n;++j){re+=coefficients[base][j]*cos(2*pi*f*j);im-=coefficients[base][j]*sin(2*pi*f*j);}
 return sqrt(re*re+im*im)/32768;
}
static void FilterResponse(void)
{
 unsigned base,j;double f,peak=0,low=1,high=1;
 for(base=0;base<2;++base){unsigned d=base?4:2;peak=0;low=high=1;
  CHECK(fabs(Response(base,0)-1)<1e-12);
  for(j=0;j<=4096;++j){
   f=.5/d+(.5-.5/d)*j/4096;double r=Response(base,f);if(r>peak)peak=r;
   r=Response(base,.4/d*j/4096);if(r<low)low=r;if(r>high)high=r;
  }
  CHECK(peak<.01 && low>pow(10.,-.05/20) && high<pow(10.,.05/20));
  printf("FIR %u:1: stop %.2f dB, pass %.3f..%.3f dB (quantized Q15)\n",d,20*log10(peak),20*log10(low),20*log10(high));
 }
}
/* raw_bin specifies cycles per 512 raw samples; this independently exposes
 * the factor-of-two/four difference in the resulting FFT bin position. */
static void Fixture(unsigned raw_bin,unsigned amp,unsigned phase,unsigned mismatch,unsigned noise)
{
 unsigned i,a,b,ai,bi;uint32_t random=19;
 Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024,0,0};
 Frame144_Reset(&frame,1);frame.words=words;frame.range=0;frame.calibration=cal;
 frame.capture.sample_period_ps_q16=(1000000000000ULL*65536+72000000)/144000000;
 frame.capture.fifo_words=4096;frame.capture.valid_begin=2;frame.capture.valid_words=4094;
 frame.capture.time_samples=8188;frame.capture.status=CAPTURE_WINDOW_READY;frame.capture.sample_phase=(u8)phase;
 memset(words,0xff,sizeof words);
 for(i=0;i<4094;++i){
  ai=2*i+(phase==CAPTURE_PHASE_B_A);bi=2*i+(phase==CAPTURE_PHASE_A_B);
  if(noise){random=random*1664525u+1013904223u;a=50+(random>>24)%140;random=random*1664525u+1013904223u;b=50+(random>>24)%140;}
  else{a=(unsigned)(120+lround(amp*sin(2*pi*raw_bin*ai/512.)));b=(unsigned)(120+lround(amp*sin(2*pi*raw_bin*bi/512.)));}
  if(mismatch==1)b+=7;
  if(mismatch==2)b=(b+64)/2;
  words[2+i]=a|(b<<8)|0x30000;
 }
 if(mismatch==1)frame.calibration.bias_q10=-7*1024;
 if(mismatch==2){frame.calibration.gain_q10=2048;frame.calibration.bias_q10=-64*1024;}
 CHECK(Frame144_Valid(&frame));
}
static unsigned Plan(unsigned base,unsigned desired)
{
 unsigned d=Step(base),half=16*d-1,last=frame.capture.time_samples-1-half-511*d;
 if(desired<half)desired=half;
 if(desired>last)desired=last;
 return desired;
}
/* Full convolution over public calibrated reads, not the production ring.
 * No FIFO calls and no production instrumentation are used by this oracle. */
static unsigned Reference(unsigned base,unsigned first,unsigned source,unsigned setting)
{
 static int32_t raw[2200],filtered[512];unsigned d=Step(base),half=16*d-1,n=2*half+1;
 unsigned i,j,clips=0,clip;int64_t sum=0,value;int32_t mean;uint32_t peak=0;unsigned shift=0;
 for(i=0;i<511*d+n;++i){CHECK(Frame144_Read(&frame,first-half+i,&raw[i],&clip));clips|=clip;}
 for(i=0;i<512;++i){value=0;
  for(j=0;j<n;++j)value+=(int64_t)coefficients[d==4][j]*raw[i*d+j];
  filtered[i]=(int32_t)(value>=0?(value+16384)/32768:-((-value+16384)/32768));sum+=filtered[i];
 }
 mean=(int32_t)(sum/512);AutoFFT=(u8)setting;
 if(setting>1)FFTGain=(u8)(setting-2);else if(!setting)FFTGain=8;if(FFTGain>8)FFTGain=8;
 for(i=0;i<512;++i){value=((int64_t)filtered[i]-mean)*Window[i]*(1u<<(2u+FFTGain));value/=1024*(source==15?100:80);
  CHECK(value<=0x3fffffff && value>=-0x3fffffff);fr[i]=(s32)value;
  if((uint32_t)(value<0?-value:value)>peak)peak=(uint32_t)(value<0?-value:value);}
 while(peak>65535){peak>>=1;++shift;}if(shift)for(i=0;i<512;++i)fr[i]/=(s32)(1u<<shift);
 Process_Fft(source,shift);PeakFreq=2;imax=0;for(i=1;i<256;++i)if(fi[i]>PeakFreq){PeakFreq=(short)fi[i];imax=(int)i;}
 if(setting==1){if(PeakFreq>=198 && FFTGain)--FFTGain;else if(PeakFreq<100 && FFTGain<7)++FFTGain;}
 return clips;
}
static void Compute(unsigned base,unsigned desired,unsigned source,unsigned setting)
{
 static s32 expected_fr[512],expected_fi[512];static uint32_t original_words[4096];
 Frame144 original=frame;unsigned gain=FFTGain,automatic=AutoFFT,clips,expected_gain,first=Plan(base,desired);
 short expected_peak;int expected_index;uint32_t rate;
 memcpy(original_words,words,sizeof words);clock_cycles+=8000000;
 clips=Reference(base,first,source,setting);expected_gain=FFTGain;expected_peak=PeakFreq;expected_index=imax;
 memcpy(expected_fr,fr,sizeof fr[0]*512);memcpy(expected_fi,fi,sizeof fi[0]*512);
 FFTGain=(u8)gain;AutoFFT=(u8)automatic;
 CHECK(Fft144_Update(&frame,desired,base,source,setting,clock_cycles));
 CHECK(!memcmp(fr,expected_fr,sizeof expected_fr) && !memcmp(fi,expected_fi,sizeof expected_fi));
 CHECK(Fft144.ready && Fft144.clipped==clips && Fft144.start==first && Fft144.setting==setting && Fft144.source==source);
 CHECK(PeakFreq==expected_peak && imax==expected_index && FFTGain==expected_gain);
 rate=(uint32_t)((1000000000000ULL*65536+frame.capture.sample_period_ps_q16*Step(base)/2)/(frame.capture.sample_period_ps_q16*Step(base)));
 CHECK(Fft144.rate_hz==rate && Fft144.epoch==frame.capture.epoch && Fft144.last_done==clock_cycles);
 CHECK(!memcmp(&frame,&original,sizeof frame) && !memcmp(words,original_words,sizeof words));
 CHECK(Fft144.start>=16*Step(base)-1 && Fft144.start+511*Step(base)+16*Step(base)-1<frame.capture.time_samples);
}
static void Vectors(void)
{
 unsigned source,base,phase,mismatch,k,i;static const unsigned bins[]={3,8,16,31,48};
 for(source=14;source<=15;++source)for(base=0;base<3;++base)
 for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase)for(mismatch=0;mismatch<3;++mismatch)for(k=0;k<5;++k){
  Fft144_Reset();Fixture(bins[k],40,phase,mismatch,0);Compute(base,0,source,2);
  CHECK(imax==(int)(bins[k]*Step(base)) && !Fft144.clipped);
  CHECK(Fft144_Frequency((unsigned)imax)==((uint64_t)144000000*bins[k]+256)/512);
  for(i=0;i<256;++i)CHECK(fi[i]>=0 && fi[i]<=198);
  ++frame.capture.epoch;Compute(base,UINT32_MAX,source,2);
  CHECK(imax==(int)(bins[k]*Step(base)));
 }
 for(base=0;base<3;++base)for(k=0;k<10;++k){Fft144_Reset();Fixture(16,30,CAPTURE_PHASE_B_A,0,1);Compute(base,511,14,k);}
 for(base=0;base<3;++base)for(i=0;i<8;++i){
  Fft144_Reset();Fixture(16,25,CAPTURE_PHASE_A_B,1,0);frame.range=i;
  frame.calibration.a_gain_q10=512+(int32_t)i*128;frame.calibration.a_bias_q10=(int32_t)i*1000;
  Compute(base,777,15,2);
 }
 /* Suppression at above-Nyquist tones is checked against full convolution,
  * with DC and ADC clipping distinguished from filtered amplitude. */
 for(base=0;base<3;++base)for(k=0;k<5;++k){
  Fft144_Reset();Fixture(130+k*23,60,CAPTURE_PHASE_B_A,0,0);Compute(base,100,14,2);
 }
 for(base=0;base<3;++base){
  Fft144_Reset();Fixture(0,0,CAPTURE_PHASE_B_A,0,0);Compute(base,0,14,2);for(i=0;i<256;++i)CHECK(!fi[i]);
  ++frame.capture.epoch;words[2]=0;Compute(base,0,14,2);CHECK(Fft144.clipped);
  ++frame.capture.epoch;words[4095]=0xffff;Compute(base,UINT32_MAX,15,2);CHECK(Fft144.clipped);
 }
}
static void CacheAndAxis(void)
{
 static s32 frozen[512];uint32_t done;unsigned i;
 Fft144_Reset();Fixture(16,40,CAPTURE_PHASE_B_A,0,0);Compute(0,100,14,2);
 CHECK(Fft144_Frequency(256)==36000000 && Fft144_Frequency(30)==4218750);
 formats=0;Fft144_Labels(16);CHECK(formatted[0]==36000000 && formatted[1]==4218750 && formatted[2]==2250000);
 CHECK(strstr(NFreqStr,"M@") && fabs(strtod(NFreqStr,0)-36)<.001);
 done=Fft144.last_done;memcpy(frozen,fi,sizeof frozen);
 CHECK(!Fft144_Update(&frame,100,0,14,2,done+8000000));
 CHECK(!Fft144_Update(&frame,100,1,14,2,done+8000000));CHECK(!memcmp(frozen,fi,sizeof frozen));
 ++frame.capture.epoch;CHECK(!Fft144_Update(&frame,100,0,14,2,done+7199999));CHECK(Fft144.ready && Fft144.epoch!=frame.capture.epoch);
 clock_cycles=done+7200000;CHECK(Fft144_Update(&frame,100,0,14,2,clock_cycles));CHECK(Fft144.epoch==frame.capture.epoch);
 done=Fft144.last_done;memcpy(frozen,fi,sizeof frozen);
 CHECK(!Fft144_Update(&frame,100,2,14,2,done+1));CHECK(!Fft144.ready && !memcmp(frozen,fi,sizeof frozen));
 CHECK(!Fft144_Update(&frame,100,2,14,2,done+7199999));
 clock_cycles=done+7200000;CHECK(Fft144_Update(&frame,100,2,14,2,clock_cycles));
 CHECK(Fft144_Frequency(256)==18000000 && Fft144_Frequency(30)==2109375);
 Fft144.last_done=0xfffffff0u;++frame.capture.epoch;clock_cycles=0x800000;CHECK(Fft144_Update(&frame,100,2,15,2,clock_cycles));
 frame.capture.sample_period_ps_q16=1000000000000ULL*65536/144014400u;++frame.capture.epoch;Compute(0,100,14,2);
 CHECK(Fft144_Frequency(256)==36003600);
 Fft144_Reset();CHECK(!Fft144.ready && !Fft144_Frequency(1));for(i=0;i<512;++i)CHECK(!fi[i]);
}
static void Invalid(void)
{
 unsigned base,k,i;static s32 old[512];
 for(base=0;base<3;++base)for(k=0;k<12;++k){
  Frame144 bad;unsigned start=0,b=base,source=14,setting=2;Fft144State state;
  Fft144_Reset();Fixture(16,30,CAPTURE_PHASE_B_A,0,0);bad=frame;
  switch(k){case 0:bad.words=0;break;case 1:bad.range=8;break;case 2:bad.capture.status=CAPTURE_WINDOW_ARMED;break;
  case 3:bad.capture.sample_phase=0;break;case 4:bad.capture.sample_period_ps_q16=0;break;
  case 5:bad.calibration.gain_q10=0;break;case 6:bad.capture.valid_begin=4096;break;case 7:b=3;break;
  case 8:source=12;break;case 9:setting=10;break;
  case 10:bad.capture.valid_words=(543*Step(base)-2)/2;bad.capture.time_samples=2*bad.capture.valid_words;break;
  case 11:bad.capture.time_samples--;break;}
  for(i=0;i<512;++i)fr[i]=fi[i]=(s32)i;
  memcpy(old,fi,sizeof old);state=Fft144;
  CHECK(!Fft144_Update(&bad,start,b,source,setting,clock_cycles+8000000));
  CHECK(!memcmp(fi,old,sizeof old) && !memcmp(fr,old,sizeof old) && !memcmp(&state,&Fft144,sizeof state));
 }
 CHECK(!Fft144_Update(0,0,0,14,2,clock_cycles+8000000));
}
int main(void)
{
 unsigned source,setting,k,i;static s32 input[512],saved[512];uint32_t random=9;
 LoadNwave();Coefficients();FilterResponse();
 /* Preserve Normal's native FFT/postprocessing, all old sources and settings. */
 for(source=11;source<16;++source)for(setting=0;setting<10;++setting)for(k=0;k<6;++k){
  AutoFFT=(u8)setting;_4_source=(s16)source;
  for(i=0;i<512;++i){random=random*1664525u+1013904223u;input[i]=(s32)((random>>16)%1001)-500;}
  if(k<4)for(i=0;i<512;++i)input[i]=(s32)(1000*sin(2*pi*(k*7+3)*i/512));
  memcpy(fr,input,sizeof input);Legacy_Process_Fft();memcpy(saved,fi,sizeof saved);
  memcpy(fr,input,sizeof input);Process_Fft(source,0);CHECK(!memcmp(saved,fi,sizeof saved));
 }
 Vectors();CacheAndAxis();Invalid();
 printf("PASS FFT144: %u checks; full-convolution oracle, 2:1/4:1 antialias response, all bases/phases/calibration, Normal FFT unchanged, peaks/axis/clipping, XPOS edges, HOLD/cache/10Hz/wrap, invalid frames; host only\n",checks);
 return 0;
}
