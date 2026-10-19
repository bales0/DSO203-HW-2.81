#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "UnifiedCal.h"
static unsigned checks,captures,writes,generators,fail_capture,clip_capture,save_fail,charger;
static UnifiedCal state;
static int response_percent,missing_b,curve,initial_bias,unstable;
static unsigned cancel_capture,bad_upper,upper_visits;
static int capture_status,flat_b;
static const uint32_t scale[8]={2000,4000,8000,20000,40000,80000,200000,400000};
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s stage=%u error=%u range=%u cmds=%u/%u raw=%ld/%ld\n",__LINE__,#x,state.stage,state.error,state.range,state.capture_commands[0],state.capture_commands[1],(long)state.capture_mean.a,(long)state.capture_mean.b);exit(1);}}while(0)
static int Capture(void *user,unsigned r,unsigned mode,unsigned ao,unsigned bo,CalMean *m)
{
    UnifiedCal *s=user;int32_t signal=s->stage==CAL_REFERENCE?s->reference/(int32_t)scale[r]:0;
    signal=signal*response_percent/100;
    CHECK(r<8);CHECK(mode<=1);CHECK(ao<=200 && bo<=200);++captures;
    if(captures==fail_capture)return 0;
    if(captures==cancel_capture)return -1;
    if(capture_status){m->a=99*1024;m->b=101*1024;m->clips=0;return capture_status;}
    m->a=((int32_t)ao+(mode?10:5)+signal)*1024;
    m->b=((int32_t)bo+(mode?13:7)+signal)*1024;
    if(!mode && curve) {
        int ca=(int)ao-94,cb=(int)bo-94;
        m->a+=curve*ca*ca*1024/500;m->b-=curve*cb*cb*1024/500;
    }
    if(!mode && captures<=2){m->a+=initial_bias*1024;m->b+=initial_bias*1024;}
    if(!mode && unstable && captures>4) {
        int shift=((captures-5)/4)%2?-1536:1536;m->a+=shift;m->b+=shift;
    }
    if(flat_b && !mode)m->b=((int32_t)bo*820+7*1024)+signal*1024;
    if(missing_b)m->b=((int32_t)bo+(mode?13:7))*1024;
    if(bad_upper && mode && !r && s->working_y[0][3] &&
       ao==(unsigned)CalNative_Command(s->adc_offset,s->native[0].offset[0],s->working_y[0][3])) {
        ++upper_visits;
        if(upper_visits==2 && ((bad_upper==1 && s->stage==CAL_GROUND) || (bad_upper==2 && s->stage==CAL_REFERENCE)))m->b-=8*1024;
    }
    m->clips=captures==clip_capture;return 1;
}
static int Gen(void *user,int on){(void)user;CHECK(on==0);++generators;return 1;}
static uint32_t Battery(void *user){(void)user;return 3700;}
static int Save(void *user,UnifiedCal *s,int high)
{
    (void)user;CHECK(s->complete==255);CHECK(s->range==8);++writes;charger=high;
    return save_fail;
}
static CalOps ops={&state,Capture,Gen,Battery,Save};
static void Init(int interlace)
{
    bad_upper=upper_visits=0;captures=writes=generators=fail_capture=clip_capture=save_fail=0;response_percent=100;missing_b=curve=initial_bias=unstable=0;cancel_capture=0;capture_status=flat_b=0;
    UnifiedCal_Init(&state,54,0,scale,interlace,0);
}
static void Ground(void)
{
    UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(state.stage==CAL_GROUND);
    UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(state.stage==CAL_REFERENCE && state.range==0);
}
static void References(void)
{
    unsigned r;for(r=0;r<8;++r) {
        CHECK(state.stage==CAL_REFERENCE && state.range==r);
        CHECK(state.reference==(int32_t)scale[r]*75);UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.complete==((1u<<(r+1))-1));CHECK(writes==0);
    }
    CHECK(state.stage==CAL_SAVE && state.range==8);
}
int main(void)
{
    unsigned r,ch,i;uint16_t gain;CalKeyGate gate={0};UnifiedCal before;
    for(i=0;i<2;++i) {
        Init(i);Ground();CHECK(captures==(i?168:96));
        References();CHECK(captures==(i?288:136));
        for(r=0;r<8;++r)for(ch=0;ch<2;++ch) {
            CHECK(state.native[ch].zero[r]==(ch?-7:-5));CHECK(state.native[ch].offset[r]==0);
            CHECK(state.native[ch].gain[r]==1024);CHECK(state.voltage_used[r][ch]==(int32_t)scale[r]*75);
            if(i){CHECK(Scope144_Valid(&state.pending144[r]));CHECK(state.pending144[r].bias_q10==-3072);}
        }
        UnifiedCal_Step(&state,i?CAL_CHARGER:CAL_OK,&ops);CHECK(state.stage==CAL_DONE && writes==1 && charger==i);
        UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(writes==1);
    }
    CHECK(CalKey_Filter(&gate,CAL_OK,1)==CAL_OK);
    for(i=0;i<30;++i)CHECK(CalKey_Filter(&gate,CAL_OK,1)==CAL_NONE);
    CHECK(CalKey_Filter(&gate,CAL_OK,0)==CAL_NONE); /* stale release event */
    CHECK(CalKey_Filter(&gate,CAL_OK,1)==CAL_OK);
    CHECK(CalKey_Filter(&gate,CAL_CANCEL,8)==CAL_CANCEL);
    /* A no-key redraw/timer tick must never change a value or touch hardware. */
    Init(1);Ground();before=state;i=captures;
    for(r=0;r<1000;++r)UnifiedCal_Step(&state,CAL_NONE,&ops);
    CHECK(!memcmp(&before,&state,sizeof(state)));CHECK(captures==i && writes==0);
    UnifiedCal_Step(&state,CAL_UP,&ops);CHECK(state.reference==before.reference+150);
    UnifiedCal_Step(&state,CAL_DOWN,&ops);CHECK(state.reference==before.reference);
    UnifiedCal_Step(&state,CAL_BIG_UP,&ops);CHECK(state.reference==before.reference+1500);
    UnifiedCal_Step(&state,CAL_BIG_DOWN,&ops);CHECK(state.reference==before.reference);
    UnifiedCal_Step(&state,CAL_CANCEL,&ops);CHECK(state.stage==CAL_CANCELLED && !writes);
    for(i=1;i<=168;++i) {
        Init(1);fail_capture=i;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.stage==CAL_ERROR && state.retry==CAL_GROUND && writes==0);
        UnifiedCal_Step(&state,CAL_CANCEL,&ops);CHECK(writes==0);
    }
    for(i=169;i<=288;++i) {
        Init(1);Ground();fail_capture=i;
        while(state.stage==CAL_REFERENCE)UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.stage==CAL_ERROR && state.retry==CAL_REFERENCE && !writes);
        UnifiedCal_Step(&state,CAL_CANCEL,&ops);CHECK(state.stage==CAL_CANCELLED && !writes);
    }
    Init(1);Ground();References();save_fail=9;
    UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(state.stage==CAL_ERROR && state.error==10 && writes==1);
    CHECK(!state.bank.range[0].valid && !state.bank.battery[0]);
    save_fail=0;UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(state.stage==CAL_SAVE && writes==1);
    UnifiedCal_Step(&state,CAL_CHARGER,&ops);CHECK(state.stage==CAL_DONE && writes==2 && charger);
    CHECK(!state.bank.range[0].valid && state.bank.high[0].valid && state.bank.battery[1]==3700);
    Init(1);clip_capture=2;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==3 && writes==0);
    Init(1);Ground();state.reference=1000;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==6 && writes==0 && state.range==0);
    Init(1);Ground();missing_b=1;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==6 && state.failed_channel==1);
    CHECK(state.delta[0]==75*1024 && state.delta[1]==0 && state.gain_result[1]==0 && !writes);
    Init(1);Ground();response_percent=25;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==6 && state.failed_channel==0);
    CHECK(state.delta[0]>=8*1024 && state.gain_result[0]>2048 && !writes);
    /* Preview is observational: it cannot accept a range or write coefficients. */
    Init(1);Ground();CHECK(UnifiedCal_Preview(&state,&ops));
    CHECK(state.stage==CAL_REFERENCE && state.complete==0 && !writes && state.measured_valid);
    CHECK(state.delta[0]==75*1024 && state.gain_result[0]==1024);
    UnifiedCal_Step(&state,CAL_UP,&ops);i=(unsigned)state.reference;
    UnifiedCal_Step(&state,CAL_RANGE_UP,&ops);CHECK(state.range==1);
    UnifiedCal_Step(&state,CAL_RANGE_DOWN,&ops);CHECK(state.range==0 && state.reference==(int32_t)i);
    /* Nonsequential range selection and revisiting must not skip missing ranges. */
    Init(1);Ground();UnifiedCal_Step(&state,CAL_RANGE_UP,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.complete==2 && state.range==2);
    UnifiedCal_Step(&state,CAL_RANGE_DOWN,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.complete==2 && state.range==2 && state.reference_used[1]==300000);
    for(r=0;r<7;++r)UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_SAVE && state.complete==255 && !writes);
    /* A wide-range nonlinear or biased first curve must never be committed. */
    Init(1);curve=1;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==13 && !writes);
    Init(1);initial_bias=4;Ground();
    CHECK(state.native[0].zero[0]==-5 && state.native[1].zero[0]==-7 && !writes);
    /* Within-tolerance, alternating means must NOT pass as stable calibration. */
    Init(1);unstable=1;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==11 && state.range==0);
    CHECK(state.offset_reason==2 && state.offset_attempt==CAL_OFFSET_ATTEMPTS);
    CHECK(captures==4+4*CAL_OFFSET_ATTEMPTS && !writes);
    CHECK(abs(state.offset_residual[state.failed_channel])<=CAL_OFFSET_TOLERANCE);
    unstable=0;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_REFERENCE && state.error==0 && !writes);
    Init(1);cancel_capture=7;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_CANCELLED && !writes && captures==7);
    Init(1);clip_capture=5;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==3 && !writes);
    /* New adapter failure codes cannot accidentally be treated as user cancel. */
    Init(1);capture_status=CAL_CAPTURE_UNSTABLE;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==12 && !writes);
    CHECK(state.capture_mean.b==101*1024 && state.capture_commands[0]==64);
    Init(1);capture_status=CAL_CAPTURE_CLIPPED;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==3 && !writes);
    Init(1);Ground();capture_status=CAL_CAPTURE_UNSTABLE;
    CHECK(!UnifiedCal_Preview(&state,&ops) && state.stage==CAL_REFERENCE && !state.measured_valid);
    /* A truly unrepresentable stable slope stays blocked, with numeric diagnostics. */
    Init(1);flat_b=1;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==5 && state.failed_channel==1 && !writes);
    CHECK(state.requested_offset[1]>127 && state.offset_low_command[1]==79 && state.offset_high_command[1]==154);
    CHECK(state.zero_low[0][1]>0 && state.zero_high[0][1]>state.zero_low[0][1]);
    CHECK(!CalNative_Gain(100*1024,100*1024,1000000,40000,&gain));
    CHECK(CalNative_Gain(100*1024,150*1024,3000000,40000,&gain));CHECK(gain==1536);
    for(i=512;i<=2048;++i) {
        CHECK(CalNative_Correct(45,40,-5,i)==40); /* every gain, no DC offset drift */
        CHECK(CalNative_Correct(-5,40,5,i)<=40); /* signed negative samples cannot wrap */
    }
    for(i=512;i<=2048;i+=37) {
        int raw;for(raw=-30;raw<=240;raw+=3) {
            int display=CalNative_Correct(raw,100,-7,i);
            int recovered=CalNative_Uncorrect(display,100,-7,i);
            CHECK(CalNative_Correct(200-raw,100,7,i)==200-display);
            CHECK(abs(recovered-raw)<=1);
            CHECK(CalNative_Threshold(display,100,-7,i,54,0)==(recovered+54<0?0:(recovered+54>255?255:recovered+54)));
            CHECK(CalNative_Threshold(200-display,100,-7,i,54,1)==CalNative_Threshold(display,100,-7,i,54,0));
        }
    }
    CHECK(CalNative_Threshold(-1000,100,0,1024,54,0)==0);
    CHECK(CalNative_Threshold(1000,100,0,1024,54,0)==255);
    CHECK(CalNative_Command(54,0,25)==79);CHECK(CalNative_Command(54,0,40)==94);
    CHECK(CalNative_Command(54,0,160)==200);CHECK(CalNative_Command(0,0,-40)==0);
    {
        const int offsets[]={0,20,40,54};unsigned oi;uint16_t points[4];
        for(oi=0;oi<4;++oi)for(r=0;r<8;++r)for(ch=0;ch<3;++ch) {
            int8_t zs[2]={-5,-7},ks[2];unsigned k;
            ks[0]=ch==0?0:ch==1?-64:64;ks[1]=-ks[0];
            CHECK(CalNative_Points(offsets[oi],zs,ks,75,points));
            CHECK(points[1]==25 && points[2]==100 && points[0]<25 && points[3]>100);
            for(k=1;k<4;++k) {
                CHECK(CalNative_Command(offsets[oi],ks[0],points[k])>=CalNative_Command(offsets[oi],ks[0],points[k-1])+2);
                CHECK(CalNative_Command(offsets[oi],ks[1],points[k])>=CalNative_Command(offsets[oi],ks[1],points[k-1])+2);
            }
            if(ch==0)printf("MAP synthetic R%u ADCoffset=%d Y=%u,25,100,%u DAC_A=%d,%d,%d,%d DAC_B=%d,%d,%d,%d\n",
                r+1,offsets[oi],points[0],points[3],CalNative_Command(offsets[oi],0,points[0]),CalNative_Command(offsets[oi],0,25),
                CalNative_Command(offsets[oi],0,100),CalNative_Command(offsets[oi],0,points[3]),CalNative_Command(offsets[oi],0,points[0]),
                CalNative_Command(offsets[oi],0,25),CalNative_Command(offsets[oi],0,100),CalNative_Command(offsets[oi],0,points[3]));
        }
        {int8_t zs[2]={-100,-100},ks[2]={0,0};CHECK(!CalNative_Points(54,zs,ks,200,points));}
        for(oi=0;oi<4;++oi) {
            Init(1);state.adc_offset=offsets[oi];Ground();References();
            for(r=0;r<8;++r)CHECK(state.pending144[r].dac_window && Scope144_Valid(&state.pending144[r]));
        }
    }
    Init(1);bad_upper=1;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==13 && state.failed_channel==1 && !writes);
    CHECK(state.capture_commands[0]==(unsigned)CalNative_Command(state.adc_offset,state.native[0].offset[0],state.working_y[0][3]));
    Init(1);Ground();bad_upper=2;upper_visits=0;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==13 && state.failed_channel==1 && !writes);
    CHECK(state.capture_mean.b==state.ref144_verify[3].b);
    printf("PASS unified calibration: %u checks; 8 ranges A+B+144, keys/release, no blink mutation, failures, signed gain/offset\n",checks);
    return 0;
}
