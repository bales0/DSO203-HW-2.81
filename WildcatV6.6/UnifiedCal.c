#include "UnifiedCal.h"
#include <string.h>
#include <limits.h>
static int32_t Rounded(int64_t n,int64_t d) { return (int32_t)(n>=0?(n+d/2)/d:-((-n+d/2)/d)); }
CalKey CalKey_Filter(CalKeyGate *gate,CalKey key,unsigned pressed)
{
    if(key==CAL_CANCEL)return key;
    if(gate->waiting){if(!pressed)gate->waiting=0;return CAL_NONE;}
    if(key==CAL_OK || key==CAL_CHARGER)gate->waiting=1;
    return key;
}
int CalNative_Uncorrect(int display,int ypos,int zero,unsigned gain)
{ return ypos+(gain?Rounded((int64_t)(display-ypos)*1024,gain):0)-zero; }
int CalNative_Threshold(int display,int ypos,int zero,unsigned gain,int adc_offset,int inverted)
{
    int raw=adc_offset+CalNative_Uncorrect(inverted?2*ypos-display:display,ypos,zero,gain);
    if(raw<0)raw=0;
    if(raw>255)raw=255;
    return raw;
}
int CalNative_Correct(int raw_minus_offset,int ypos,int zero,unsigned gain)
{ return ypos+Rounded((int64_t)gain*(raw_minus_offset+zero-ypos),1024); }
int CalNative_Command(int adc_offset,int correction,int ypos)
{
    int command=adc_offset+40+Rounded((int64_t)(1024+correction)*(ypos-40),1024);
    if(command<0)command=0;
    if(command>200)command=200;
    return command;
}
/* Fit the inverse offset slope and the software zero at the Y=40 pivot.
 * Both the pivot and the Y=25 measurement point are inside the sampled span. */
static int FitOffset(int adc_offset,int32_t low,int32_t high,unsigned low_command,
    unsigned high_command,int8_t *zero,int8_t *offset,int32_t *wanted_zero,int32_t *wanted_offset)
{
    int z,k;int32_t delta=high-low,pivot;
    if(adc_offset<0 || adc_offset>140 || low_command>200 ||
        high_command<low_command+16 || high_command>200 || delta<16*1024)return 0;
    pivot=low+Rounded((int64_t)(adc_offset+40-(int)low_command)*delta,high_command-low_command);
    z=adc_offset+40-Rounded(pivot,1024);
    k=Rounded((int64_t)(high_command-low_command)*1024*1024,delta)-1024;
    if(wanted_zero)*wanted_zero=z;
    if(wanted_offset)*wanted_offset=k;
    if(z<INT8_MIN || z>INT8_MAX || k<INT8_MIN || k>INT8_MAX)return 0;
    *zero=(int8_t)z;*offset=(int8_t)k;return 1;
}
int CalNative_Solve(int adc_offset,int32_t low,int32_t high,unsigned high_command,int8_t *zero,int8_t *offset)
{
    if(adc_offset<0 || adc_offset>140)return 0;
    return FitOffset(adc_offset,low,high,(unsigned)adc_offset+40,high_command,zero,offset,0,0);
}
int CalNative_Gain(int32_t zero,int32_t high,int32_t reference,uint32_t scale,uint16_t *gain)
{
    int32_t delta=high-zero;int g;
    if(delta<8*1024 || !scale || reference<1000 || reference>60000000)return 0;
    g=Rounded((int64_t)reference*1024*1024,(int64_t)scale*delta);
    if(g<512 || g>2048)return 0;
    *gain=(uint16_t)g;return 1;
}
int CalMean_Valid(const CalMean *m)
{
    return m && !m->clips && m->a>=10*1024 && m->a<=245*1024 && m->b>=10*1024 && m->b<=245*1024;
}
void UnifiedCal_Init(UnifiedCal *s,int adc_offset,int ppm,const uint32_t scale[8],int include144,const Scope144Bank *bank)
{
    unsigned i;
    memset(s,0,sizeof(*s));s->stage=CAL_CLOCK;s->adc_offset=adc_offset;s->ppm=ppm>=-999 && ppm<=999?ppm:0;
    if(bank)s->bank=*bank;
    memcpy(s->scale,scale,sizeof(s->scale));s->include144=include144!=0;
    for(i=0;i<8;++i){s->reference_input[i]=(int32_t)scale[i]*75;s->native[0].gain[i]=s->native[1].gain[i]=1024;}
}
static void Fail(UnifiedCal *s,CalStage retry,unsigned error)
{ s->retry=retry;s->error=error;s->stage=error==1?CAL_CANCELLED:CAL_ERROR; }
static int Capture(UnifiedCal *s,const CalOps *ops,unsigned path,unsigned a,unsigned b,CalMean *m,CalStage retry)
{
    int result;
    memset(m,0,sizeof(*m));result=ops->capture(ops->user,s->range,path,a,b,m);
    s->capture_mean=*m;s->capture_path=path;s->capture_commands[0]=a;s->capture_commands[1]=b;
    if(result<=0){Fail(s,retry,result==-1?1:result==CAL_CAPTURE_UNSTABLE?12:result==CAL_CAPTURE_CLIPPED?3:2);return 0;}
    if(!CalMean_Valid(m)){Fail(s,retry,3);return 0;}
    return 1;
}
static int Magnitude(int32_t value) { return value<0?-value:value; }
/* Refine at the actual Y=25 / Y=100 commands. Two consecutive accepted
 * pairs must also agree within one ADC step: a transient cannot pass alone. */
static int GroundRange(UnifiedCal *s,const CalOps *ops)
{
    unsigned r=s->range,ch,attempt,passing=0;
    unsigned low_command[2],high_command[2];CalMean low,high;
    int32_t previous[2][2]={{0,0},{0,0}};
    int8_t zero[2],offset[2];
    s->offset_attempt=0;s->offset_reason=0;
    for(ch=0;ch<2;++ch) {
        s->native[ch].zero[r]=s->native[ch].offset[r]=0;s->native[ch].gain[r]=1024;
        s->offset_low_command[ch]=s->adc_offset+25;s->offset_high_command[ch]=s->adc_offset+100;
        s->requested_zero[ch]=s->requested_offset[ch]=0;
    }
    if(!Capture(s,ops,0,s->adc_offset+25,s->adc_offset+25,&low,CAL_GROUND) ||
       !Capture(s,ops,0,s->adc_offset+100,s->adc_offset+100,&high,CAL_GROUND))return 0;
    for(ch=0;ch<2;++ch) {
        s->zero_low[r][ch]=ch?low.b:low.a;s->zero_high[r][ch]=ch?high.b:high.a;
        if(!FitOffset(s->adc_offset,s->zero_low[r][ch],s->zero_high[r][ch],s->adc_offset+25,
            s->adc_offset+100,&zero[ch],&offset[ch],&s->requested_zero[ch],&s->requested_offset[ch])){s->failed_channel=ch;Fail(s,CAL_GROUND,5);return 0;}
    }
    for(attempt=1;attempt<=CAL_OFFSET_ATTEMPTS;++attempt) {
        int acceptable=1,stable=1;
        s->offset_attempt=attempt;
        for(ch=0;ch<2;++ch) {
            low_command[ch]=CalNative_Command(s->adc_offset,offset[ch],25);
            high_command[ch]=CalNative_Command(s->adc_offset,offset[ch],100);
            s->offset_low_command[ch]=low_command[ch];s->offset_high_command[ch]=high_command[ch];
        }
        if(!Capture(s,ops,0,low_command[0],low_command[1],&low,CAL_GROUND) ||
           !Capture(s,ops,0,high_command[0],high_command[1],&high,CAL_GROUND))return 0;
        for(ch=0;ch<2;++ch) {
            int32_t l=ch?low.b:low.a,h=ch?high.b:high.a;
            s->zero_low[r][ch]=l;s->zero_high[r][ch]=h;
            s->offset_residual[ch]=l+(int32_t)zero[ch]*1024-(s->adc_offset+25)*1024;
            s->offset_high_residual[ch]=h+(int32_t)zero[ch]*1024-(s->adc_offset+100)*1024;
            if(Magnitude(s->offset_residual[ch])>CAL_OFFSET_TOLERANCE ||
               Magnitude(s->offset_high_residual[ch])>CAL_OFFSET_TOLERANCE) {
                acceptable=0;s->failed_channel=ch;
            }
            if(passing && (Magnitude(l-previous[ch][0])>CAL_OFFSET_STABILITY ||
                Magnitude(h-previous[ch][1])>CAL_OFFSET_STABILITY)) {
                stable=0;s->failed_channel=ch;
            }
            previous[ch][0]=l;previous[ch][1]=h;
        }
        if(acceptable) {
            if(passing && stable) {
                for(ch=0;ch<2;++ch) {
                    s->native[ch].zero[r]=zero[ch];s->native[ch].offset[r]=offset[ch];
                    s->zero_ref[r][ch]=ch?low.b:low.a;
                }
                return 1;
            }
            passing=1;s->offset_reason=2; /* Another stable measurement needed. */
        } else {
            passing=0;s->offset_reason=1; /* Residual not yet corrected. */
            for(ch=0;ch<2;++ch) {
                if(!FitOffset(s->adc_offset,ch?low.b:low.a,ch?high.b:high.a,
                    low_command[ch],high_command[ch],&zero[ch],&offset[ch],&s->requested_zero[ch],&s->requested_offset[ch])) {
                    s->failed_channel=ch;s->offset_reason=3;Fail(s,CAL_GROUND,11);return 0;
                }
            }
        }
    }
    Fail(s,CAL_GROUND,11);return 0;
}
static int Ground(UnifiedCal *s,const CalOps *ops)
{
    unsigned r;CalMean m;
    if(s->adc_offset<0 || s->adc_offset>100){Fail(s,CAL_GROUND,4);return 0;}
    for(r=0;r<8;++r) {
        s->range=r;
        if(!GroundRange(s,ops))return 0;
        if(s->include144) {
            if(!Capture(s,ops,1,SCOPE144_OFFSET,SCOPE144_OFFSET,&m,CAL_GROUND))return 0;
            s->zero144[r][0]=m.a;s->zero144[r][1]=m.b;
        }
    }
    s->battery=ops->battery(ops->user);s->range=0;s->reference=s->reference_input[0];
    s->stage=CAL_REFERENCE;return 1;
}
static void Diagnose(UnifiedCal *s,const CalMean *m,unsigned path)
{
    unsigned ch; s->measured=*m;s->measured_valid=1;s->diagnostic_path=path;
    for(ch=0;ch<2;++ch) {
        int32_t delta=(ch?m->b:m->a)-(path?s->zero144[s->range][ch]:s->zero_ref[s->range][ch]);
        s->delta[ch]=delta;
        s->gain_result[ch]=0;
        if(delta>0 && s->scale[s->range]) {
            int64_t numerator=(int64_t)s->reference*1024*1024;
            int64_t denominator=(int64_t)s->scale[s->range]*delta;
            s->gain_result[ch]=numerator/denominator>=INT32_MAX?INT32_MAX:Rounded(numerator,denominator);
        }
    }
    if(path) {
        s->failed_channel=s->delta[0]<8*1024?0:1;
        s->gain_result[s->failed_channel]=s->delta[1]>0?(int32_t)((int64_t)s->delta[0]*1024/s->delta[1]):0;
    }
}
int UnifiedCal_Preview(UnifiedCal *s,const CalOps *ops)
{
    CalMean m;unsigned r=s->range;int result;
    if(s->stage!=CAL_REFERENCE || r>=8)return 0;
    result=ops->capture(ops->user,r,0,CalNative_Command(s->adc_offset,s->native[0].offset[r],25),
        CalNative_Command(s->adc_offset,s->native[1].offset[r],25),&m);
    if(result==-1){s->stage=CAL_CANCELLED;return 0;}
    if(result<=0){s->measured_valid=0;return 0;}
    Diagnose(s,&m,0);return 1;
}
static int Reference(UnifiedCal *s,const CalOps *ops)
{
    CalMean m;unsigned ch,r=s->range;Scope144Cal candidate;uint16_t gains[2];int32_t native_delta[2];
    if(!Capture(s,ops,0,CalNative_Command(s->adc_offset,s->native[0].offset[r],25),
        CalNative_Command(s->adc_offset,s->native[1].offset[r],25),&m,CAL_REFERENCE))return 0;
    Diagnose(s,&m,0);native_delta[0]=s->delta[0];native_delta[1]=s->delta[1];
    for(ch=0;ch<2;++ch)if(!CalNative_Gain(s->zero_ref[r][ch],ch?m.b:m.a,s->reference,s->scale[r],&gains[ch])) {
        s->failed_channel=ch;Fail(s,CAL_REFERENCE,6);return 0;
    }
    if(s->include144) {
        if(!Capture(s,ops,1,SCOPE144_OFFSET,SCOPE144_OFFSET,&m,CAL_REFERENCE))return 0;
        Diagnose(s,&m,1);
        if(!Scope144_CalibrateMeans(s->zero144[r][0],s->zero144[r][1],m.a,m.b,s->reference,&candidate)) {
            Fail(s,CAL_REFERENCE,7);return 0;
        }
        s->bank.range[r]=candidate;
    }
    for(ch=0;ch<2;++ch) {
        s->native[ch].gain[r]=gains[ch];
        s->voltage_used[r][ch]=(int32_t)((int64_t)native_delta[ch]*s->scale[r]*gains[ch]/(1024*1024));
    }
    s->reference_used[r]=s->reference;s->complete|=1u<<r;
    s->measured_valid=0;
    if(s->complete==255){s->range=8;s->stage=CAL_SAVE;}
    else {
        do {s->range=(s->range+1)%8;} while(s->complete&(1u<<s->range));
        s->reference=s->reference_input[s->range];
    }
    return 1;
}
void UnifiedCal_Step(UnifiedCal *s,CalKey key,const CalOps *ops)
{
    if(s->stage==CAL_DONE || s->stage==CAL_CANCELLED)return;
    if(key==CAL_CANCEL){s->stage=CAL_CANCELLED;return;}
    if(s->stage==CAL_ERROR) {if(key==CAL_OK){s->stage=s->retry;s->error=0;}return;}
    if(s->stage==CAL_CLOCK) {
        if(key==CAL_UP && s->ppm<999)++s->ppm;
        if(key==CAL_DOWN && s->ppm>-999)--s->ppm;
        if(key==CAL_OK){if(!ops->generator(ops->user,0))Fail(s,CAL_CLOCK,8);else s->stage=CAL_GROUND;}
        return;
    }
    if(s->stage==CAL_GROUND){if(key==CAL_OK)Ground(s,ops);return;}
    if(s->stage==CAL_REFERENCE) {
        if(key==CAL_RANGE_DOWN || key==CAL_RANGE_UP) {
            s->range=(s->range+(key==CAL_RANGE_UP?1:7))%8;
            s->reference=s->reference_input[s->range];s->measured_valid=0;return;
        }
        int64_t next=s->reference;int32_t small=(int32_t)s->scale[s->range]*75/1000;
        if(small<1)small=1;
        if(key==CAL_DOWN)next-=small;
        if(key==CAL_UP)next+=small;
        if(key==CAL_BIG_DOWN)next-=small*10;
        if(key==CAL_BIG_UP)next+=small*10;
        if(next>=1000 && next<=60000000){s->reference=(int32_t)next;s->reference_input[s->range]=s->reference;}
        if(key==CAL_OK)Reference(s,ops);
        return;
    }
    if(s->stage==CAL_SAVE && (key==CAL_OK || key==CAL_CHARGER)) {
        if(s->complete!=255){Fail(s,CAL_GROUND,9);return;}
        if(ops->save(ops->user,s,key==CAL_CHARGER)==0)s->stage=CAL_DONE;
        else Fail(s,CAL_SAVE,10);
    }
}
