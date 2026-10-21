#if defined(__GNUC__)
#pragma GCC optimize ("Os")
#endif
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
/* Record both phases before deciding which one failed. Diagnostics do not
 * alter the fit, limits, capture sequence or persistence. */
static int CheckDac(UnifiedCal *s,const CalMean *actual,int32_t a,int32_t b,CalStage retry)
{
    unsigned ch;CalDacDiagnostic *d=&s->dac_diagnostic;
    d->expected[0]=a;d->expected[1]=b;
    d->residual[0]=actual->a-a;d->residual[1]=actual->b-b;
    d->tolerance=d->step==CAL_DAC_DRIFT?CAL_OFFSET_STABILITY:
        s->capture_path?CAL_144_POINT_TOLERANCE:CAL_OFFSET_TOLERANCE;
    for(ch=0;ch<2;++ch)if(Magnitude(d->residual[ch])>d->tolerance) {
        s->failed_channel=ch;d->reason=CAL_DAC_RESIDUAL;Fail(s,retry,13);return 0;
    }
    return 1;
}
int __attribute__((section(".text.calibration"))) CalNative_Points(int adc_offset,const int8_t zero[2],const int8_t correction[2],unsigned reference_steps,uint16_t ypos[4])
{
    int low=10,high=190,ch;
    if(!Scope144_Command(adc_offset) || reference_steps>235)return 0;
    for(ch=0;ch<2;++ch) {
        int y;
        while(low<25 && (adc_offset+low-zero[ch]<12 || CalNative_Command(adc_offset,correction[ch],low)<2))++low;
        y=239-adc_offset+zero[ch]-(int)reference_steps;
        if(y<high)high=y;
        while(high>100 && CalNative_Command(adc_offset,correction[ch],high)>=200)--high;
    }
    if(low>17 || high<102 || high>190)return 0;
    ypos[0]=(uint16_t)low;ypos[1]=25;ypos[2]=100;ypos[3]=(uint16_t)high;
    for(ch=0;ch<2;++ch) {
        unsigned k,previous=0;
        for(k=0;k<4;++k) {
            unsigned command=CalNative_Command(adc_offset,correction[ch],ypos[k]);
            if(command>200 || (k && command<previous+2))return 0;
            previous=command;
        }
    }
    return 1;
}
/* One line, actual commands as X; 64-bit sums and signed rounding. */
static int FitDacCurve(UnifiedCal *s,const CalMean points[4],const unsigned commands[4][2],
    int32_t slope[2],int32_t intercept[2],CalStage retry)
{
    unsigned ch,k;
    for(ch=0;ch<2;++ch) {
        int64_t sx=0,sy=0,sxx=0,sxy=0,den;
        for(k=0;k<4;++k) {
            unsigned drive=s->capture_path?0:ch;
            int32_t x=commands[k][drive],y=ch?points[k].b:points[k].a;
            if(x>200 || (k && x<(int32_t)commands[k-1][drive]+2))goto bad;
            sx+=x;sy+=y;sxx+=(int64_t)x*x;sxy+=(int64_t)x*y;
        }
        den=4*sxx-sx*sx;if(den<=0)goto bad;
        slope[ch]=Rounded(4*sxy-sx*sy,den);s->gain_result[ch]=slope[ch];
        intercept[ch]=Rounded(sy-sx*slope[ch],4);
        if(slope[ch]<512 || slope[ch]>2048)goto bad;
    }
    return 1;
bad:
    s->failed_channel=ch;s->dac_diagnostic.reason=CAL_DAC_SLOPE;Fail(s,retry,13);return 0;
}
/* In CH_A routing both ADCs see the offset applied by DAC A. DAC B
 * does not independently position B144; it must never drive a fitted DIFF. */
static uint16_t *Positions(UnifiedCal *s,unsigned path)
{ return path?s->working144_y[s->range]:s->working_y[s->range]; }
/* Normal keeps independent corrections; CH_A uses A for both readings. */
static void PointCommands(int adc_offset,const uint16_t y[4],const int8_t offset[2],unsigned commands[4][2])
{
    unsigned k,ch;
    for(k=0;k<4;++k)for(ch=0;ch<2;++ch)
        commands[k][ch]=CalNative_Command(adc_offset,offset[ch],y[k]);
}
static void Commands(UnifiedCal *s,unsigned path,unsigned commands[4][2])
{
    int8_t offset[2]={s->native[0].offset[s->range],s->native[path?0:1].offset[s->range]};
    PointCommands(s->adc_offset,Positions(s,path),offset,commands);
}
static __attribute__((noinline)) void DacPoint(UnifiedCal *s,const CalMean points[4],const unsigned commands[4][2],unsigned k)
{
    s->capture_mean=points[k];s->capture_stats=s->ref144_stats[k];
    s->capture_commands[0]=commands[k][0];s->capture_commands[1]=commands[k][1];
}
static int Curve(UnifiedCal *s,const CalOps *ops,unsigned path,CalStage retry,
    const unsigned commands[4][2],CalMean points[4],int32_t slope[2],int32_t intercept[2])
{
    unsigned k;
    for(k=0;k<4;++k) {
        s->dac_diagnostic.step=k==0?CAL_DAC_LOW:k==3?CAL_DAC_HIGH:CAL_DAC_FIT;
        if(!Capture(s,ops,path,commands[k][0],commands[k][1],&points[k],retry))return 0;
        s->ref144_stats[k]=s->capture_stats;
        if(path && retry==CAL_GROUND) {
            s->ref144_verify[k]=points[k];s->ref144_verify[k].a=(int32_t)commands[k][1];
            s->ref144_verify[k].clips=commands[k][0];
        }
    }
    if(path && retry==CAL_GROUND) {
        s->dac144_low[s->range]=points[0];s->dac144_high[s->range]=points[3];
    }
    if(!FitDacCurve(s,points,commands,slope,intercept,retry))return 0;
    for(k=0;k<4;++k) {
        s->dac_diagnostic.step=CAL_DAC_POINT;s->dac_diagnostic.point=k;
        DacPoint(s,points,commands,k);
        if(!CheckDac(s,&points[k],intercept[0]+commands[k][0]*slope[0],intercept[1]+commands[k][1]*slope[1],retry))return 0;
    }
    return 1;
}
static int GroundRange(UnifiedCal *s,const CalOps *ops)
{
    unsigned r=s->range,ch,k,attempt,passing=0,commands[4][2],cap;
    uint16_t *y=s->working_y[r];
    int32_t slope[2],intercept[2];CalMean points[4],previous[4];int8_t zero[2]={0,0},offset[2]={0,0};
    cap=y[3];s->offset_attempt=0;s->offset_reason=0;
    for(ch=0;ch<2;++ch){s->native[ch].zero[r]=s->native[ch].offset[r]=0;s->native[ch].gain[r]=1024;}
    if(!CalNative_Points(s->adc_offset,zero,offset,0,y)){Fail(s,CAL_GROUND,15);return 0;}
    if(cap && y[3]>cap)y[3]=(uint16_t)cap;
    PointCommands(s->adc_offset,y,offset,commands);
    if(!Curve(s,ops,0,CAL_GROUND,commands,points,slope,intercept))return 0;
    for(ch=0;ch<2;++ch) {
        s->zero_low[r][ch]=ch?points[1].b:points[1].a;s->zero_high[r][ch]=ch?points[2].b:points[2].a;
        s->offset_low_command[ch]=commands[1][ch];s->offset_high_command[ch]=commands[2][ch];
    }
    for(attempt=1;attempt<=CAL_OFFSET_ATTEMPTS;++attempt) {
        int acceptable=1,stable=1;unsigned steps=(unsigned)(s->reference_input[r]/(int32_t)s->scale[r]);s->offset_attempt=attempt;
        for(ch=0;ch<2;++ch) {
            int z=s->adc_offset+40-Rounded((int64_t)(s->adc_offset+40)*slope[ch]+intercept[ch],1024);
            int o=Rounded(1024LL*1024,slope[ch])-1024;
            s->requested_zero[ch]=z;s->requested_offset[ch]=o;
            if(z<INT8_MIN || z>INT8_MAX || o<INT8_MIN || o>INT8_MAX) {
                s->failed_channel=ch;Fail(s,CAL_GROUND,5);return 0;
            }
            zero[ch]=(int8_t)z;offset[ch]=(int8_t)o;s->native[ch].offset[r]=(int8_t)o;
        }
        if(!CalNative_Points(s->adc_offset,zero,offset,steps,y)) {
            if(attempt==1 && Curve(s,ops,0,CAL_GROUND,commands,points,slope,intercept))continue;
            if(s->stage!=CAL_ERROR)Fail(s,CAL_GROUND,15);
            return 0;
        }
        if(cap && y[3]>cap)y[3]=(uint16_t)cap;
        PointCommands(s->adc_offset,y,offset,commands);
        for(k=0;k<4;++k) {
            CalMean m;
            if(!Capture(s,ops,0,commands[k][0],commands[k][1],&m,CAL_GROUND))return 0;
            for(ch=0;ch<2;++ch) {
                int32_t raw=ch?m.b:m.a;
                int32_t residual=raw+(int32_t)zero[ch]*1024-(s->adc_offset+y[k])*1024;
                if(Magnitude(residual)>CAL_OFFSET_TOLERANCE){acceptable=0;s->failed_channel=ch;}
                if(passing && Magnitude(raw-(ch?previous[k].b:previous[k].a))>CAL_OFFSET_STABILITY){stable=0;s->failed_channel=ch;}
                if(k==1){s->offset_residual[ch]=residual;s->zero_low[r][ch]=raw;s->zero_ref[r][ch]=raw;s->offset_low_command[ch]=commands[k][ch];}
                if(k==2){s->offset_high_residual[ch]=residual;s->zero_high[r][ch]=raw;s->offset_high_command[ch]=commands[k][ch];}
            }
            points[k]=previous[k]=m;
        }
        if(acceptable && passing && stable) {
            for(ch=0;ch<2;++ch){s->native[ch].zero[r]=(int8_t)zero[ch];s->native[ch].offset[r]=(int8_t)offset[ch];}
            return 1;
        }
        passing=acceptable;s->offset_reason=acceptable?2:1;
        if(!acceptable && !FitDacCurve(s,points,commands,slope,intercept,CAL_GROUND))return 0;
    }
    Fail(s,CAL_GROUND,11);return 0;
}
/* Four shared A commands, two complete agreeing passes. A retains its
 * Normal calibration; B is fitted only as a response to the same DAC A.
 * The capture adapter still requires three stable fresh blocks per point. */
static __attribute__((noinline)) int Ground144(UnifiedCal *s,const CalOps *ops)
{
    unsigned r=s->range,k,attempt,commands[4][2];
    CalMean points[4],current[4];int32_t intercept[2];
    memcpy(s->working144_y[r],s->working_y[r],sizeof(s->working_y[r]));
    Commands(s,1,commands);s->offset_attempt=0;s->offset_reason=0;
    memset(&s->dac_diagnostic,0,sizeof(s->dac_diagnostic));
    if(!Curve(s,ops,1,CAL_GROUND,commands,points,s->dac144[r],intercept))return 0;
    for(attempt=1;attempt<=CAL_OFFSET_ATTEMPTS;++attempt) {
        unsigned stable=1;s->offset_attempt=attempt;
        for(k=0;k<4;++k) {
            int32_t expected=(s->adc_offset+s->working144_y[r][k]-s->native[0].zero[r])*1024;
            if(!Capture(s,ops,1,commands[k][0],commands[k][1],&current[k],CAL_GROUND))return 0;
            s->ref144_stats[k]=s->capture_stats;
            s->dac_diagnostic.step=CAL_DAC_POINT;s->dac_diagnostic.point=k;
            if(!CheckDac(s,&current[k],intercept[0]+commands[k][0]*s->dac144[r][0],
                intercept[1]+commands[k][0]*s->dac144[r][1],CAL_GROUND))return 0;
            if(Magnitude(current[k].a-expected)>CAL_144_A_TOLERANCE) {
                s->dac_diagnostic.reason=CAL_DAC_RESIDUAL;
                s->dac_diagnostic.expected[0]=expected;s->dac_diagnostic.residual[0]=current[k].a-expected;
                s->failed_channel=0;Fail(s,CAL_GROUND,13);return 0;
            }
            {
                unsigned ch;int32_t drift[2]={current[k].a-points[k].a,current[k].b-points[k].b};
                for(ch=0;ch<2;++ch) {
                    if(k==1)s->offset_residual[ch]=drift[ch];
                    if(k==2)s->offset_high_residual[ch]=drift[ch];
                    if(Magnitude(drift[ch])>CAL_OFFSET_STABILITY){stable=0;s->failed_channel=ch;}
                }
            }
        }
        if(stable) {
            s->dac144_low[r]=current[0];s->dac144_high[r]=current[3];
            s->zero144[r][0]=current[1].a;s->zero144[r][1]=current[1].b;
            return 1;
        }
        memcpy(points,current,sizeof points);s->offset_reason=2;
    }
    Fail(s,CAL_GROUND,11);return 0;
}
static void Progress(const CalOps *ops,const UnifiedCal *s,CalProgress event,int32_t a,int32_t b)
{ if(ops->progress)ops->progress(ops->user,s,event,a,b); }
static int Ground(UnifiedCal *s,const CalOps *ops)
{
    unsigned r;
    s->complete=0;memset(s->pending144,0,sizeof(s->pending144));
    if(!Scope144_Command(s->adc_offset)){Fail(s,CAL_GROUND,4);return 0;}
    s->battery=ops->battery(ops->user);
    if(s->battery<2800 || s->battery>5200){Fail(s,CAL_GROUND,14);return 0;}
    for(r=0;r<8;++r) {
        if(Magnitude((int32_t)ops->battery(ops->user)-(int32_t)s->battery)>CAL_SUPPLY_DRIFT_MV){Fail(s,CAL_GROUND,14);return 0;}
        s->range=r;
        Progress(ops,s,CAL_UI_ZERO_START,0,0);
        if(!GroundRange(s,ops))return 0;
        Progress(ops,s,CAL_UI_ZERO_VERIFIED,0,0);
        if(s->include144) {
            Progress(ops,s,CAL_UI_144_ZERO_START,0,0);
            if(!Ground144(s,ops))return 0;
            Progress(ops,s,CAL_UI_144_ZERO_VERIFIED,0,0);
        }
    }
    if(Magnitude((int32_t)ops->battery(ops->user)-(int32_t)s->battery)>CAL_SUPPLY_DRIFT_MV){Fail(s,CAL_GROUND,14);return 0;}
    s->range=0;s->reference=s->reference_input[0];
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
int __attribute__((section(".text.cal_preview_ui"))) UnifiedCal_Preview(UnifiedCal *s,const CalOps *ops)
{
    CalMean m={0,0,0};unsigned r=s->range;int result;
    if(s->stage!=CAL_REFERENCE || r>=8)return 0;
    result=ops->capture(ops->user,r,0,CalNative_Command(s->adc_offset,s->native[0].offset[r],25),
        CalNative_Command(s->adc_offset,s->native[1].offset[r],25),&m);
    if(result==-1){s->stage=CAL_CANCELLED;return 0;}
    if(result<=0){s->measured_valid=0;return 0;}
    return UnifiedCal_PreviewMean(s,&m);
}
int UnifiedCal_PreviewMean(UnifiedCal *s,const CalMean *m)
{
    if(s->stage!=CAL_REFERENCE)return 0;
    if(!CalMean_Valid(m)){s->measured_valid=0;return 0;}
    Diagnose(s,m,0);return 1;
}
/* Collect a full independent reference curve and a second full pass before
 * testing any point. A final anchor distinguishes drift from offset response.
 * Verification stats live in the wizard's existing DataBuf, not the stack. */
static __attribute__((noinline,section(".text.calibration"))) int Reference144(UnifiedCal *s,const CalOps *ops,CalMean *result)
{
    unsigned commands[4][2],k;CalMean anchor,points[4];int32_t intercept[2];
    Commands(s,1,commands);memset(&s->dac_diagnostic,0,sizeof(s->dac_diagnostic));
    if(!Capture(s,ops,1,commands[1][0],commands[1][1],&anchor,CAL_REFERENCE))return 0;
    if(!Curve(s,ops,1,CAL_REFERENCE,commands,points,s->ref_dac144,intercept))return 0;
    for(k=0;k<4;++k) {
        s->dac_diagnostic.step=CAL_DAC_POINT;
        if(!Capture(s,ops,1,commands[k][0],commands[k][1],&s->ref144_verify[k],CAL_REFERENCE))return 0;
        s->ref144_stats[k]=s->capture_stats;
    }
    s->dac_diagnostic.step=CAL_DAC_RETURN;
    if(!Capture(s,ops,1,commands[1][0],commands[1][1],result,CAL_REFERENCE))return 0;
    if(Magnitude((int32_t)ops->battery(ops->user)-(int32_t)s->battery)>CAL_SUPPLY_DRIFT_MV){Fail(s,CAL_REFERENCE,14);return 0;}
    s->dac_diagnostic.step=CAL_DAC_DRIFT;s->dac_diagnostic.point=1;
    if(!CheckDac(s,result,anchor.a,anchor.b,CAL_REFERENCE))return 0;
    s->dac_diagnostic.step=CAL_DAC_RETURN;
    if(!CheckDac(s,result,intercept[0]+commands[1][0]*s->ref_dac144[0],intercept[1]+commands[1][1]*s->ref_dac144[1],CAL_REFERENCE))return 0;
    for(k=0;k<4;++k) {
        s->dac_diagnostic.step=CAL_DAC_POINT;s->dac_diagnostic.point=k;
        if(!CheckDac(s,&s->ref144_verify[k],intercept[0]+commands[k][0]*s->ref_dac144[0],intercept[1]+commands[k][1]*s->ref_dac144[1],CAL_REFERENCE)) {
            DacPoint(s,s->ref144_verify,commands,k);return 0;
        }
    }
    return 1;
}
static int Reference(UnifiedCal *s,const CalOps *ops)
{
    CalMean m;unsigned ch,r=s->range;Scope144Cal candidate;uint16_t gains[2];int32_t native_delta[2];
    s->pending144[r].valid=0;s->complete&=~(1u<<r);
    Progress(ops,s,CAL_UI_REF_START,0,0);
    if(Magnitude((int32_t)ops->battery(ops->user)-(int32_t)s->battery)>CAL_SUPPLY_DRIFT_MV){Fail(s,CAL_REFERENCE,14);return 0;}
    if(!Capture(s,ops,0,CalNative_Command(s->adc_offset,s->native[0].offset[r],25),
        CalNative_Command(s->adc_offset,s->native[1].offset[r],25),&m,CAL_REFERENCE))return 0;
    Diagnose(s,&m,0);native_delta[0]=s->delta[0];native_delta[1]=s->delta[1];
    for(ch=0;ch<2;++ch)if(!CalNative_Gain(s->zero_ref[r][ch],ch?m.b:m.a,s->reference,s->scale[r],&gains[ch])) {
        s->failed_channel=ch;Fail(s,CAL_REFERENCE,6);return 0;
    }
    {
        unsigned commands[4][2],k;CalMean p;Commands(s,0,commands);
        for(k=0;k<4;++k) {
            if(!Capture(s,ops,0,commands[k][0],commands[k][1],&p,CAL_REFERENCE))return 0;
            /* Both model values belong to this same freshly captured block.
             * Never leave the previous range/path's diagnostics on error 13. */
            s->dac_diagnostic.step=CAL_DAC_POINT;s->dac_diagnostic.reason=CAL_DAC_OK;
            s->dac_diagnostic.point=k;s->dac_diagnostic.tolerance=CAL_OFFSET_TOLERANCE;
            for(ch=0;ch<2;++ch) {
                int32_t raw=ch?p.b:p.a;
                int32_t ground=(s->adc_offset+s->working_y[r][k]-s->native[ch].zero[r])*1024;
                s->dac_diagnostic.expected[ch]=ground+native_delta[ch];
                s->dac_diagnostic.residual[ch]=raw-s->dac_diagnostic.expected[ch];
            }
            for(ch=0;ch<2;++ch)if(Magnitude(s->dac_diagnostic.residual[ch])>CAL_OFFSET_TOLERANCE) {
                s->dac_diagnostic.reason=CAL_DAC_RESIDUAL;
                s->failed_channel=ch;Fail(s,CAL_REFERENCE,13);return 0;
            }
        }
    }
    Progress(ops,s,CAL_UI_REF_VERIFIED,gains[0],gains[1]);
    if(s->include144) {
        Progress(ops,s,CAL_UI_144_REF_START,0,0);
        if(!Reference144(s,ops,&m))return 0;
        Diagnose(s,&m,1);
        s->dac_diagnostic.step=CAL_DAC_REFERENCE;s->dac_diagnostic.point=1;
        if(!CheckDac(s,&m,s->zero_ref[r][0]+native_delta[0],m.b,CAL_REFERENCE))return 0;
        if(!Scope144_CalibrateMeans(s->zero_ref[r][0],s->zero144[r][1],
            s->zero_ref[r][0]+native_delta[0],m.b,s->reference,&candidate)) {
            Fail(s,CAL_REFERENCE,7);return 0;
        }
        {
            int32_t target_span;unsigned commands[4][2];
            target_span=Rounded((int64_t)native_delta[0]*gains[0],1024);
            candidate.a_gain_q10=gains[0];
            candidate.a_bias_q10=40*1024-Rounded((int64_t)s->zero_ref[r][0]*gains[0],1024);
            candidate.zero_q10=40*1024;candidate.span_q10=target_span;
            candidate.reference_uv=Rounded((int64_t)target_span*s->scale[r],1024);
            candidate.dac_a_q10=s->dac144[r][0];candidate.dac_b_q10=s->dac144[r][1];candidate.adc_offset=s->adc_offset;
            candidate.ref_dac_a_q10=s->ref_dac144[0];candidate.ref_dac_b_q10=s->ref_dac144[1];
            Commands(s,1,commands);
            candidate.dac_anchor=commands[1][0] | (commands[1][1]<<8) |
                ((uint32_t)(uint8_t)s->native[0].offset[r]<<16) |
                ((uint32_t)(uint8_t)s->native[0].offset[r]<<24);
            candidate.dac_window=commands[0][0] | (commands[3][0]<<8) | (commands[0][1]<<16) | (commands[3][1]<<24);
            if(!Scope144_Valid(&candidate)){Fail(s,CAL_REFERENCE,7);return 0;}
            s->pending144[r]=candidate;
            Progress(ops,s,CAL_UI_144_REF_VERIFIED,m.b,0);
        }
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
        int64_t next=s->reference,small=(int64_t)s->scale[s->range]*75/1000;
        if(small<1)small=1;
        if(key==CAL_DOWN)next-=small;
        if(key==CAL_UP)next+=small;
        if(key==CAL_BIG_DOWN)next-=small*10;
        if(key==CAL_BIG_UP)next+=small*10;
        if(key==CAL_FAST_DOWN)next-=small*100;
        if(key==CAL_FAST_UP)next+=small*100;
        if(next<1000)next=1000;
        if(next>60000000)next=60000000;
        s->reference=(int32_t)next;s->reference_input[s->range]=s->reference;
        if(key==CAL_OK)Reference(s,ops);
        return;
    }
    if(s->stage==CAL_SAVE && (key==CAL_OK || key==CAL_CHARGER)) {
        if(s->complete!=255){Fail(s,CAL_GROUND,9);return;}
        unsigned profile=key==CAL_CHARGER;Scope144Cal *dest=profile?s->bank.high:s->bank.range;
        uint32_t battery=ops->battery(ops->user),previous_battery=s->bank.battery[profile];
        if(battery<2800 || battery>5200 || Magnitude((int32_t)battery-(int32_t)s->battery)>CAL_SUPPLY_DRIFT_MV){Fail(s,CAL_SAVE,14);return;}
        s->battery=battery;
        memcpy(s->rollback144,dest,sizeof(s->rollback144));
        if(s->include144)memcpy(dest,s->pending144,sizeof(s->pending144));else memset(dest,0,sizeof(s->pending144));
        s->bank.battery[profile]=s->battery;
        if(ops->save(ops->user,s,profile)==0)s->stage=CAL_DONE;
        else {
            memcpy(dest,s->rollback144,sizeof(s->rollback144));s->bank.battery[profile]=previous_battery;
            Fail(s,CAL_SAVE,10);
        }
    }
}
