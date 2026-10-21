/* Exact digital values against the old independent C/D scans. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "Frame144.h"
static unsigned checks;static uint32_t words[4096];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int Legacy_DigitalMeasure(const Frame144 *,unsigned,Scope144Measurements *);
static void Compare(const Frame144 *f)
{
    Scope144Measurements legacy[2],single[2],pair[2],sentinel;unsigned ch,mask,expected;int ok[2];
    memset(&sentinel,0xa5,sizeof sentinel);
    for(ch=0;ch<2;++ch){ok[ch]=Legacy_DigitalMeasure(f,ch,&legacy[ch]);CHECK(Frame144_DigitalMeasure(f,ch,&single[ch])==ok[ch]);CHECK(!memcmp(&legacy[ch],&single[ch],sizeof legacy[ch]));}
    for(mask=0;mask<4;++mask){
        pair[0]=pair[1]=sentinel;expected=((mask&1)&&ok[0]?1u:0u)|((mask&2)&&ok[1]?2u:0u);
        CHECK(Frame144_DigitalMeasurePair(f,mask&1?&pair[0]:0,mask&2?&pair[1]:0)==expected);
        for(ch=0;ch<2;++ch)CHECK(!memcmp(&pair[ch],mask&(1u<<ch)?&legacy[ch]:&sentinel,sizeof sentinel));
    }
}
int main(void)
{
    static const unsigned counts[]={1,2,3,7,16,64,513,4094};
    static const int shifts[]={-2097153,-2097152,-2097151,-131073,-131072,-65537,-1,0,1,65535,65536,131071,131072,2097151,2097152,2097153,INT_MIN,INT_MAX};
    Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024,0,0};
    Frame144 f;unsigned n,kind,i,k,phase;uint32_t random=17;
    Frame144_Reset(&f,1);f.words=words;f.range=0;f.calibration=cal;
    f.capture.fifo_words=4096;f.capture.valid_begin=2;f.capture.status=CAPTURE_WINDOW_READY;
    f.capture.sample_period_ps_q16=1000000000000ULL*65536/144000000;
    for(n=0;n<sizeof counts/sizeof counts[0];++n)for(kind=0;kind<12;++kind) {
        f.capture.valid_words=counts[n];f.capture.time_samples=counts[n]*2;
        for(i=0;i<4096;++i) {
            unsigned c=0,d=0;
            switch(kind){
            case 0:break;case 1:c=d=1;break;
            case 2:c=i>=counts[n]/2;d=!c;break;
            case 3:c=i%31<3;d=i%17<8;break;
            case 4:c=i%31<15;d=i%17<16;break;
            case 5:c=i%31<28;d=i%17<1;break;
            case 6:c=i&1;d=!c;break;
            case 7:c=i==counts[n]/2;d=i==counts[n]/3;break;
            case 8:c=i%11<3;d=i%37<21;break;
            case 9:random=random*1664525u+1013904223u;c=random>>31;d=(random>>30)&1;break;
            case 10:c=(i%7)<5;break;
            case 11:d=(i%19)<12;break;
            }
            words[i]=120u|(125u<<8)|(c<<16)|(d<<17)|((i&15)<<24);
        }
        for(k=0;k<sizeof shifts/sizeof shifts[0];++k)for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase){
            f.capture.sample_phase=(uint8_t)phase;f.digital_shift_q16=shifts[k];Compare(&f);
        }
    }
    f.digital_shift_q16=FRAME144_DIGITAL_SHIFT_Q16;
    for(k=0;k<10;++k){Frame144 broken=f;switch(k){case 0:broken.words=0;break;case 1:broken.range=8;break;case 2:broken.calibration.valid=0;break;case 3:broken.capture.status=CAPTURE_WINDOW_EMPTY;break;case 4:broken.capture.valid_begin=4096;break;case 5:broken.capture.time_samples--;break;case 6:broken.capture.sample_phase=CAPTURE_PHASE_NATIVE;break;case 7:broken.capture.sample_period_ps_q16=0;break;case 8:broken.capture.first_visible_q16=UINT_MAX;break;case 9:broken.capture.flags=255;break;}Compare(&broken);}
    Compare(0);CHECK(!Frame144_DigitalMeasure(&f,0,0));
    {Scope144Measurements x;memset(&x,0xa5,sizeof x);CHECK(!Frame144_DigitalMeasure(&f,2,&x));CHECK(!x.samples);}
    printf("PASS digital pair: %u checks; exact legacy C/D results, combined/one/no channels, 4094 words, fractional/negative shifts, tails/duty/no period, both phases, invalid frames; no device IO\n",checks);return 0;
}
