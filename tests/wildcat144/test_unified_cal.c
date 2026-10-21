#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "UnifiedCal.h"
#include "CalStable.h"
static unsigned checks,captures,writes,generators,fail_capture,clip_capture,save_fail,charger;
static UnifiedCal state;
static int response_percent,missing_b,curve,initial_bias,unstable;
static unsigned cancel_capture,bad_upper,upper_visits;
static int capture_status,flat_b,route_slope,route_percent,route_offset,route_error,route_drift,a_route_error;
static unsigned path_calls,normal_calls;
static CalNative normal_snapshot[2];
static uint16_t normal_y[8][4];
static int protect_normal;static unsigned last_path;
static CalNative ground_snapshot[2];static uint16_t ground_y[8][4];
static uint32_t battery_value=3700;
static const uint32_t scale[8]={2000,4000,8000,20000,40000,80000,200000,400000};
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s stage=%u error=%u range=%u cmds=%u/%u raw=%ld/%ld\n",__LINE__,#x,state.stage,state.error,state.range,state.capture_commands[0],state.capture_commands[1],(long)state.capture_mean.a,(long)state.capture_mean.b);exit(1);}}while(0)
static int Capture(void *user,unsigned r,unsigned mode,unsigned ao,unsigned bo,CalMean *m)
{
    UnifiedCal *s=user;int32_t signal=s->stage==CAL_REFERENCE?s->reference/(int32_t)scale[r]:0;
    signal=signal*response_percent/100;
    CHECK(r<8);CHECK(mode<=1);CHECK(ao<=200 && bo<=200);++captures;
    if(mode) {
        if(!last_path){memcpy(ground_snapshot,s->native,sizeof ground_snapshot);memcpy(ground_y,s->working_y,sizeof ground_y);}
        CHECK(!memcmp(ground_snapshot,s->native,sizeof ground_snapshot));CHECK(!memcmp(ground_y,s->working_y,sizeof ground_y));
        ++path_calls;
        if(protect_normal){CHECK(!memcmp(normal_snapshot,s->native,sizeof normal_snapshot));CHECK(!memcmp(normal_y,s->working_y,sizeof normal_y));}
    } else ++normal_calls;
    last_path=mode;
    if(captures==fail_capture)return 0;
    if(captures==cancel_capture)return -1;
    if(capture_status){m->a=99*1024;m->b=101*1024;m->clips=0;return capture_status;}
    m->a=((int32_t)ao+5+signal)*1024;
    m->b=((int32_t)(mode?ao:bo)+(mode?8:7)+signal)*1024;
    if(!mode && curve) {
        int ca=(int)ao-94,cb=(int)bo-94;
        m->a+=curve*ca*ca*1024/500;m->b-=curve*cb*cb*1024/500;
    }
    if(!mode && captures<=2){m->a+=initial_bias*1024;m->b+=initial_bias*1024;}
    if(!mode && unstable && captures>4) {
        int shift=((captures-5)/4)%2?-1536:1536;m->a+=shift;m->b+=shift;
    }
    if(flat_b && !mode)m->b=((int32_t)bo*(flat_b>1?flat_b:820)+7*1024)+signal*1024;
    if(mode) {
        m->a+=a_route_error;
        if(route_slope)m->b=(int32_t)ao*route_slope+route_offset*1024+signal*1024*route_percent/100;
        if(r==4 && ao==79)m->b-=route_error;
        if(route_drift && path_calls>4)m->b+=((path_calls-5)/4)%2?-1536:1536;
    }
    if(missing_b)m->b=((int32_t)bo+(mode?8:7))*1024;
    if(bad_upper && mode && !r && s->working144_y[0][3] &&
       ao==(unsigned)CalNative_Command(s->adc_offset,s->native[0].offset[0],s->working144_y[0][3])) {
        ++upper_visits;
        if((bad_upper==1 && s->stage==CAL_GROUND) || (upper_visits==2 && bad_upper==2 && s->stage==CAL_REFERENCE))m->b-=8*1024;
    }
    m->clips=captures==clip_capture;return 1;
}
static int Gen(void *user,int on){(void)user;CHECK(on==0);++generators;return 1;}
static uint32_t Battery(void *user){(void)user;return battery_value;}
static int Save(void *user,UnifiedCal *s,int high)
{
    (void)user;CHECK(s->complete==255);CHECK(s->range==8);++writes;charger=high;
    return save_fail;
}
static CalOps ops={&state,Capture,Gen,Battery,Save};
static void Init(int interlace)
{
    bad_upper=upper_visits=0;captures=writes=generators=fail_capture=clip_capture=save_fail=0;response_percent=100;missing_b=curve=initial_bias=unstable=0;cancel_capture=0;capture_status=flat_b=0;route_slope=route_error=route_drift=a_route_error=0;route_percent=100;route_offset=8;path_calls=normal_calls=last_path=0;protect_normal=0;battery_value=3700;
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
/* Full-route tests: B144 has different DAC and voltage responses from Normal B. */
static void RoutedCalibration(void)
{
    unsigned r,y,ch,level,pass;Scope144Cal c;unsigned commands[2];Scope144Bank b;
    uint8_t bytes[SCOPE144_CAL_BYTES];Scope144Bank decoded;
    for(pass=0;pass<2;++pass) {
        Init(1);route_slope=960;route_percent=92;route_offset=9;battery_value=pass?4300:3200;
        Ground();memcpy(normal_snapshot,state.native,sizeof normal_snapshot);memcpy(normal_y,state.working_y,sizeof normal_y);
        protect_normal=1;References();
        CHECK(!memcmp(normal_snapshot[1].zero,state.native[1].zero,8));
        CHECK(!memcmp(normal_snapshot[1].offset,state.native[1].offset,8));
        for(r=0;r<8;++r) {
            CHECK(state.native[1].zero[r]==-7 && state.native[1].offset[r]==0);
            CHECK(state.pending144[r].a_gain_q10==state.native[0].gain[r]);
            CHECK((int8_t)(state.pending144[r].dac_anchor>>24)==state.native[0].offset[r]);
        }
        UnifiedCal_Step(&state,pass?CAL_CHARGER:CAL_OK,&ops);CHECK(state.stage==CAL_DONE);
        b=state.bank;CHECK(Scope144_Encode(&b,0x931c989b,bytes));CHECK(Scope144_Decode(bytes,0x931c989b,&decoded));
        for(r=0;r<8;++r)for(y=10;y<=190;++y) {
            if(!Scope144_SelectAt(&decoded,r,54,battery_value,(int)y,&c,commands))continue;
            for(ch=0;ch<2;++ch)for(level=0;level<=2;++level) {
                int32_t raw=ch?(int32_t)commands[0]*route_slope+route_offset*1024:(int32_t)(commands[0]+5)*1024;
                int32_t q,uv,target=(int32_t)scale[r]*75*(int)level/2;
                raw+=75*1024*(int)level*(ch?route_percent:100)/200;
                q=ch?(int32_t)((int64_t)raw*c.gain_q10/1024)+c.bias_q10:raw;
                q=(int32_t)((int64_t)q*c.a_gain_q10/1024)+c.a_bias_q10;
                uv=Scope144_Voltage(&c,q);CHECK(abs(uv-target)<(int)scale[r]/3);
            }
        }
        /* Installing a 144 bank must not change a Normal DAC command. */
        Scope144_NativeBank(&decoded);
        for(ch=0;ch<2;++ch)for(y=10;y<=190;++y) {
            unsigned cmd;int p=CalNative_Position(4,ch,54,0,(int)y,battery_value,&cmd);
            CHECK(cmd==(unsigned)CalNative_Command(54,0,p));
        }
        Scope144_NativeBank(0);
    }
    /* Real R5 reports: stable 1.068 / 1.125-code point deviations are not time drift. */
    for(pass=0;pass<2;++pass) {
        Init(1);route_error=pass?1152:1094;Ground();References();CHECK(state.complete==255 && !writes);
    }
    Init(1);route_error=6*1024;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && !writes && state.range==4);
    Init(1);route_drift=1;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==11 && state.capture_path==1 && !writes);
    Init(1);Ground();a_route_error=3*1024;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.failed_channel==0 && state.error==13 && !writes);
    /* Slopes which formerly produced DIFF 128/145/150 do not require
     * an independent signed-byte correction on the switched B route. */
    {
        static const int slopes[]={1170,911,1172,910,898,893};unsigned n;
        for(n=0;n<sizeof slopes/sizeof slopes[0];++n) {
            Init(1);state.adc_offset=40;route_slope=slopes[n];route_percent=100;
            route_offset=(int)((80LL*(1024-route_slope)+(route_slope<=1024?512:-512))/1024);
            Ground();References();
            for(r=0;r<8;++r) {
                    CHECK(state.pending144[r].dac_b_q10==slopes[n]);
                CHECK((int8_t)(state.pending144[r].dac_anchor>>24)==state.native[0].offset[r]);
            }
            CHECK(state.complete==255 && !writes);
        }
    }
    /* A cannot silently acquire a second absolute scale in the switched path. */
    Init(1);Ground();memcpy(normal_snapshot,state.native,sizeof normal_snapshot);a_route_error=3*1024;
    UnifiedCal_Step(&state,CAL_OK,&ops);CHECK(state.stage==CAL_ERROR && state.error==13 && !state.pending144[0].valid);
    CHECK(!memcmp(normal_snapshot,state.native,sizeof normal_snapshot) && !writes);
    a_route_error=0;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.complete==1 && state.pending144[0].valid==1);
    /* Failed retry invalidates only its pending candidate, never an installed bank. */
    state.range=0;state.reference=state.reference_input[0];a_route_error=3*1024;
    b=state.bank;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && !(state.complete&1) && !state.pending144[0].valid && !memcmp(&b,&state.bank,sizeof b));
    for(pass=13;pass<=20;pass+=4) {
        Init(1);cancel_capture=pass;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.stage==CAL_CANCELLED && state.capture_path==1 && !writes);
        CHECK(!memcmp(ground_snapshot,state.native,sizeof ground_snapshot));CHECK(!memcmp(ground_y,state.working_y,sizeof ground_y));
        Init(1);clip_capture=pass;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.stage==CAL_ERROR && state.error==3 && !writes && !memcmp(ground_snapshot,state.native,sizeof ground_snapshot));
    }
    {
        UnifiedCal independent,before;CalOps other=ops;
        Init(1);UnifiedCal_Init(&independent,40,17,scale,1,0);before=independent;
        Ground();CHECK(!memcmp(&before,&independent,sizeof before));
        before=state;other.user=&independent;
        UnifiedCal_Step(&independent,CAL_OK,&other);UnifiedCal_Step(&independent,CAL_OK,&other);
        CHECK(independent.stage==CAL_REFERENCE && !memcmp(&before,&state,sizeof before));
    }
    /* Stable residual and alternating blocks use independent criteria. */
    {
        CalStable stable={0};CalMean in={80*1024,80*1024-1152,0},out;unsigned k;
        CHECK(!CalStable_Push(&stable,&in,&out));CHECK(!CalStable_Push(&stable,&in,&out));CHECK(CalStable_Push(&stable,&in,&out));
        memset(&stable,0,sizeof stable);
        for(k=0;k<20;++k){in.b=80*1024+(k%2?1152:-1152);CHECK(!CalStable_Push(&stable,&in,&out));}
        CHECK(CAL_OFFSET_STABILITY==1024 && CAL_OFFSET_TOLERANCE==2048);
    }
}
/* The independent control experiment: B144 depends on A, not B. */
static void SharedSequence(void)
{
    unsigned profile,r,offset,y;Scope144Cal c;unsigned commands[2];
    static const int offsets[]={0,20,40,54};
    for(profile=0;profile<2;++profile)for(offset=0;offset<4;++offset) {
        Init(1);state.adc_offset=offsets[offset];battery_value=profile?4300:3200;
        route_slope=960;route_offset=9;route_percent=92;
        Ground();References();
        for(r=0;r<8;++r) {
            CHECK(!memcmp(state.working_y[r],state.working144_y[r],sizeof state.working_y[r]));
        }
        UnifiedCal_Step(&state,profile?CAL_CHARGER:CAL_OK,&ops);CHECK(state.stage==CAL_DONE);
        for(r=0;r<8;++r)for(y=10;y<=190;y+=5) {
            int p=(int)y;unsigned measured;int32_t raw[2];
            Scope144_NativeBank(&state.bank);
            CHECK(Scope144_Runtime(&state.bank,r,state.adc_offset,battery_value,scale[r],&p,&c,commands,&measured));
            CHECK(measured && commands[0]==commands[1]);
            raw[0]=(commands[0]+5)*1024;
            raw[1]=(int32_t)commands[0]*route_slope+route_offset*1024;
            raw[1]=(int32_t)((int64_t)raw[1]*c.gain_q10/1024)+c.bias_q10;
            CHECK(Scope144_Normalize(&c,raw,2));
            CHECK(abs(Scope144_Voltage(&c,raw[0]))<(int)scale[r]/3);
            CHECK(abs(Scope144_Voltage(&c,raw[1]))<(int)scale[r]/3);
        }
        Scope144_NativeBank(0);
    }
    /* A fake fit using B commands must fail this capture model: deliberately
     * hold A at 64 while sweeping B over the exact reported DAC sequence. */
    {
        static const unsigned b[]={47,64,146,153};CalMean first,m;unsigned k;
        Init(1);route_slope=960;route_offset=19;state.stage=CAL_GROUND;
        CHECK(Capture(&state,0,1,64,b[0],&first)==1);
        for(k=1;k<4;++k){CHECK(Capture(&state,0,1,64,b[k],&m)==1);CHECK(m.b==first.b);}
        CHECK(!writes);
    }
    /* Normal's signed-byte fit guards remain independent and unchanged. */
    {
        static const int slopes[]={1172,910,893},diffs[]={-129,128,150};unsigned n;
        for(n=0;n<3;++n) {
            Init(0);state.adc_offset=40;flat_b=slopes[n];
            UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
            CHECK(state.stage==CAL_ERROR && state.error==5 && !state.capture_path && !writes);
            CHECK(state.requested_offset[1]==diffs[n] && !state.native[1].offset[0]);
        }
    }
}
int main(void)
{
    unsigned r,ch,i,ground_count=0,total_count=0;uint16_t gain;CalKeyGate gate={0};UnifiedCal before;
    for(i=0;i<2;++i) {
        Init(i);Ground();CHECK(captures==8u*(i?20u:12u));if(i)ground_count=captures;
        References();CHECK(captures==8u*(i?35u:17u));if(i)total_count=captures;
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
    for(i=1;i<=ground_count;++i) {
        Init(1);fail_capture=i;UnifiedCal_Step(&state,CAL_OK,&ops);UnifiedCal_Step(&state,CAL_OK,&ops);
        CHECK(state.stage==CAL_ERROR && state.retry==CAL_GROUND && writes==0);
        UnifiedCal_Step(&state,CAL_CANCEL,&ops);CHECK(writes==0);
    }
    for(i=ground_count+1;i<=total_count;++i) {
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
    CHECK(state.stage==CAL_ERROR && (state.error==11 || state.error==13) && state.failed_channel==1 && !writes);
    CHECK(upper_visits==1 && abs(state.dac_diagnostic.residual[1])>CAL_144_POINT_TOLERANCE);
    CHECK(state.capture_commands[0]>=64 && state.capture_commands[0]<=156);
    Init(1);Ground();bad_upper=2;upper_visits=0;UnifiedCal_Step(&state,CAL_OK,&ops);
    CHECK(state.stage==CAL_ERROR && state.error==13 && state.failed_channel==1 && !writes);
    CHECK(state.capture_mean.b==state.ref144_verify[3].b);
    {
        static const int8_t differences[]={-128,-64,0,64,127};
        unsigned n,k;uint16_t y[4];int8_t z[2]={-5,0},c[2]={0,0};
        for(n=0;n<sizeof differences/sizeof differences[0];++n) {
            c[1]=differences[n];CHECK(CalNative_Points(40,z,c,75,y));
            for(k=0;k<4;++k){int a=CalNative_Command(40,c[0],y[k]),b=CalNative_Command(40,c[1],y[k]);
                CHECK(a>=0 && a<=200 && b>=0 && b<=200);
                if(k)CHECK(b>=CalNative_Command(40,c[1],y[k-1])+2);}
        }
    }
    RoutedCalibration();
    SharedSequence();
    printf("PASS unified calibration: %u checks; 8 ranges A+B+144, keys/release, no blink mutation, failures, signed gain/offset\n",checks);
    return 0;
}
