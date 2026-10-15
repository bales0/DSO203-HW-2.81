#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "Phase144.h"
#include "Text144.h"
#define COUNT 664u
#define KEY 0x14410480u
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint16_t data[4096];
static unsigned Triangle(int t) { t%=256; if(t<0)t+=256; return t<128 ? 32+(unsigned)t : 288-(unsigned)t; }
static void Wave(unsigned count,int lag,int correction)
{
    unsigned i;
    for(i=0;i<152;++i)data[i]=0xffff; /* History must never count as clipping. */
    for(i=152;i<count;++i) {
        unsigned a=Triangle((int)(i-152)*8), b=Triangle((int)(i-152)*8+lag);
        if(correction==1)b-=10;
        if(correction==2)b=(b+40)/2;
        data[i]=(uint16_t)(a|(b<<8));
    }
}
static Interlace144Calibration Cal(const FpgaProfile *p,int correction)
{
    Interlace144Calibration c={1024,0,0,0,0,KEY,1};
    c.family=p->family; c.protocol=p->protocol; c.implementation=p->implementation;
    if(correction==1)c.bias_q10=10240;
    if(correction==2){c.gain_q10=2048;c.bias_q10=-40960;}
    return c;
}
int main(void)
{
    FpgaProfile profiles[2];
    FpgaProfile unknown=FpgaProfile_Decode(0x5731,0x0101,0x0103);
    Phase144State state, saved;
    Phase144Result r, s, inter;
    Interlace144Calibration cal, changed;
    unsigned p, repeat, correction, i;
    int lag;
    char row[49], display[49], guarded[51], longtext[70];
    profiles[0]=FpgaProfile_Decode(0x5731,0x0101,0);
    profiles[1]=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    for(p=0;p<2;++p) for(lag=-16;lag<=16;++lag) {
        cal=Cal(&profiles[p],0); Wave(COUNT,lag,0); Phase144_Reset(&state);
        for(repeat=0;repeat<3;++repeat)
            CHECK(Phase144_Add(&profiles[p],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
        CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK);
        CHECK(r.lag8==lag && r.near_best==1);
        CHECK(r.valid==(lag>-16 && lag<16));
        CHECK(!r.clips_a && !r.clips_b && state.fit_samples==3*507);
        CHECK(state.errors[lag+16]==0);
        CHECK(Phase144_Add(&profiles[p],KEY,&cal,data,COUNT,&state)==INTERLACE144_ARGUMENT);
    }
    for(correction=1;correction<=2;++correction) for(lag=-4;lag<=4;lag+=4) {
        cal=Cal(&profiles[0],correction); Wave(COUNT,lag,correction); Phase144_Reset(&state);
        CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
        CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK);
        CHECK(r.valid && r.lag8==lag && state.errors[lag+16]==0);
    }
    cal=Cal(&profiles[0],0);
    Phase144_Reset(&state); Wave(168,0,0);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,168,&state)==INTERLACE144_OK);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && r.lag8==0);
    Phase144_Reset(&state); Wave(4096,-4,0);
    for(repeat=0;repeat<3;++repeat)CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,4096,&state)==INTERLACE144_OK);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && r.lag8==-4 && r.valid);
    /* Extreme permitted correction requires 64-bit error accumulation. */
    cal.gain_q10=2048;cal.bias_q10=255*1024;
    for(i=152;i<4096;++i)data[i]=(uint16_t)(20|(220u<<8));
    Phase144_Reset(&state);
    for(repeat=0;repeat<3;++repeat)CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,4096,&state)==INTERLACE144_OK);
    CHECK(state.errors[16]>0xffffffffULL);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && r.error_q10[1]==691200 && !r.valid);
    /* Constant or weak signals and clipping cannot produce valid phase. */
    cal=Cal(&profiles[0],0);
    for(i=152;i<COUNT;++i)data[i]=100u|(100u<<8);
    Phase144_Reset(&state);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && !r.valid && r.near_best==33);
    Wave(COUNT,-4,0);data[152]=0xff00;
    Phase144_Reset(&state);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && !r.valid && r.clips_a==1 && r.clips_b==1);
    for(i=152;i<COUNT;++i)data[i]=(uint16_t)((100+(i&1)) | ((100+(i&1))<<8));
    Phase144_Reset(&state);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_OK && !r.valid && r.span_a==1 && r.span_b==1);
    /* State and output stay unchanged on bad arguments or changed calibration. */
    Wave(COUNT,0,0); Phase144_Reset(&state);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_OK);
    saved=state; changed=cal; changed.bias_q10++;
    CHECK(Phase144_Add(&profiles[0],KEY,&changed,data,COUNT,&state)==INTERLACE144_CALIBRATION);
    CHECK(Phase144_Add(&profiles[0],KEY+1,&cal,data,COUNT,&state)==INTERLACE144_CALIBRATION);
    CHECK(Phase144_Add(&profiles[1],KEY,&cal,data,COUNT,&state)==INTERLACE144_CALIBRATION);
    CHECK(Phase144_Add(&unknown,KEY,&cal,data,COUNT,&state)==INTERLACE144_UNSUPPORTED);
    CHECK(Phase144_Add(NULL,KEY,&cal,data,COUNT,&state)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Add(&profiles[0],KEY,NULL,data,COUNT,&state)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,NULL,COUNT,&state)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,NULL)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,167,&state)==INTERLACE144_WINDOW);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,4097,&state)==INTERLACE144_WINDOW);
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,(uint16_t *)&state,COUNT,&state)==INTERLACE144_ARGUMENT);
    CHECK(!memcmp(&saved,&state,sizeof state));
    changed=cal;changed.valid=0;
    CHECK(Phase144_Add(&profiles[0],KEY,&changed,data,COUNT,&state)==INTERLACE144_CALIBRATION);
    changed=cal;changed.gain_q10=INT_MAX;
    CHECK(Phase144_Add(&profiles[0],KEY,&changed,data,COUNT,&state)==INTERLACE144_CALIBRATION);
    CHECK(Phase144_Finish(NULL,&r)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Finish(&state,NULL)==INTERLACE144_ARGUMENT);
    CHECK(Phase144_Finish(&state,(Phase144Result *)&state)==INTERLACE144_ARGUMENT);
    Phase144_Reset(&state);memset(&r,0x55,sizeof r);s=r;
    CHECK(Phase144_Finish(&state,&r)==INTERLACE144_WINDOW && !memcmp(&r,&s,sizeof r));
    state.marker=0;
    CHECK(Phase144_Add(&profiles[0],KEY,&cal,data,COUNT,&state)==INTERLACE144_ARGUMENT);
    Phase144_Reset(NULL);
    /* Both time orders accepted, but identical shifts must not be a PASS. */
    memset(&s,0,sizeof s);memset(&inter,0,sizeof inter);s.valid=1;inter.valid=1;
    inter.lag8=-4;CHECK(Phase144_Compare(&s,&inter)==-1);
    inter.lag8=4;CHECK(Phase144_Compare(&s,&inter)==1);
    inter.lag8=0;CHECK(Phase144_Compare(&s,&inter)==0);
    inter.lag8=6;CHECK(Phase144_Compare(&s,&inter)==0);
    inter.lag8=INT_MAX;CHECK(Phase144_Compare(&s,&inter)==0);
    inter.lag8=-4;s.valid=0;CHECK(Phase144_Compare(&s,&inter)==0);
    CHECK(Phase144_Compare(NULL,&inter)==0 && Phase144_Compare(&s,NULL)==0);
    /* A shorter text overwrites the complete previous row. */
    Text144_Row(display,"RAM calibration ready");
    Text144_Row(row,"Raw clips A+B: 0");memcpy(display,row,sizeof display);
    CHECK(!strncmp(display,"Raw clips A+B: 0",16));
    for(i=16;i<48;++i)CHECK(display[i]==' ');
    CHECK(display[48]==0 && strstr(display,"ready")==NULL);
    memset(guarded,'x',sizeof guarded);Text144_Row(guarded+1,NULL);
    CHECK(guarded[0]=='x' && guarded[50]=='x' && guarded[49]==0);
    for(i=1;i<=48;++i)CHECK(guarded[i]==' ');
    memset(longtext,'A',69);longtext[69]=0;Text144_Row(row,longtext);
    CHECK(row[48]==0);for(i=0;i<48;++i)CHECK(row[i]=='A');
    printf("PASS Wildcat phase/text: %u checks, all 33 shifts on both FPGA profiles\n",checks);
    return 0;
}
