#include "Frame144.h"
#include <string.h>

void Frame144_Reset(Frame144 *frame,uint32_t epoch)
{
    if(!frame)return;
    memset(frame,0,sizeof *frame);
    frame->digital_shift_q16=FRAME144_DIGITAL_SHIFT_Q16;
    CaptureWindow_Reset(&frame->capture,epoch,CAPTURE_WINDOW_EMPTY);
}
int Frame144_Valid(const Frame144 *frame)
{
    return frame && frame->words && frame->range<8 && CaptureWindow_Valid(&frame->capture) &&
        (frame->capture.sample_phase==CAPTURE_PHASE_B_A || frame->capture.sample_phase==CAPTURE_PHASE_A_B) &&
        frame->capture.sample_period_ps_q16>=6000ULL*65536 &&
        frame->capture.sample_period_ps_q16<=8000ULL*65536 && Scope144_Valid(&frame->calibration);
}
/* Fast reader behind a validated source. No LCD clipping, no voltage rounding
 * before the relative B->A correction followed by absolute A normalization. */
static uint32_t Word(const Frame144 *frame,unsigned pair)
{
    uint32_t word;
    /* Byte access respects both the legacy DataBuf u32 objects and uint32_t
     * host fixtures. The compiler reduces this fixed-size copy to a load. */
    memcpy(&word,(const unsigned char *)frame->words+pair*4u,sizeof word);
    return word;
}
static int Read(const void *user,unsigned sample,int32_t *q)
{
    const Frame144 *frame=user;
    const Scope144Cal *cal=&frame->calibration;
    uint32_t word,raw;int32_t relative;int64_t value;int is_a;
    if(!q || sample>=frame->capture.time_samples)return 0;
    word=Word(frame,frame->capture.valid_begin+sample/2u);
    is_a=(sample & 1u)==(frame->capture.sample_phase==CAPTURE_PHASE_B_A);
    raw=is_a?(word & 255u):((word>>8)&255u);
    relative=is_a?(int32_t)raw*1024:(int32_t)raw*cal->gain_q10+cal->bias_q10;
    value=(int64_t)relative*cal->a_gain_q10/1024+cal->a_bias_q10;
    if(value<INT32_MIN || value>INT32_MAX)return 0;
    *q=(int32_t)value;return 1;
}
int Frame144_Source(const Frame144 *frame,Scope144Source *source)
{
    if(source)memset(source,0,sizeof *source);
    if(!source || !Frame144_Valid(frame))return 0;
    source->user=frame;source->count=frame->capture.time_samples;source->read=Read;
    source->period_ps_q16=frame->capture.sample_period_ps_q16;return 1;
}
int Frame144_Read(const Frame144 *frame,unsigned sample,int32_t *q,unsigned *clip)
{
    unsigned word,raw;int is_a;
    if(!q || !Frame144_Valid(frame) || sample>=frame->capture.time_samples)return 0;
    if(!Read(frame,sample,q))return 0;
    word=Word(frame,frame->capture.valid_begin+sample/2u);
    is_a=(sample & 1u)==(frame->capture.sample_phase==CAPTURE_PHASE_B_A);
    raw=is_a?(word & 255u):((word>>8)&255u);
    if(clip)*clip=raw==0 || raw==255;
    return 1;
}
int Frame144_At(const Frame144 *frame,const ViewWindowGeometry *view,unsigned column,int32_t *q)
{
    ViewWindowSample point;int32_t a,b;
    if(!q || !Frame144_Valid(frame) || !view || view->capture_count!=frame->capture.time_samples ||
        !ViewWindow_At(view,column,&point) || !Read(frame,point.index,&a))return 0;
    if(point.fraction_q16) {
        if(!Read(frame,point.next_index,&b))return 0;
        a+=(int32_t)(((int64_t)b-a)*point.fraction_q16/65536);
    }
    *q=a;return 1;
}
int Frame144_Measure(const Frame144 *frame,Scope144Measurements *measure)
{
    Scope144Source source;unsigned i,clips=0,word,a,b;
    if(!measure || !Frame144_Source(frame,&source))return 0;
    for(i=0;i<frame->capture.valid_words;++i) {
        word=Word(frame,frame->capture.valid_begin+i);a=word&255u;b=(word>>8)&255u;
        clips+=!a || a==255;clips+=!b || b==255;
    }
    return Scope144_MeasureSource(&frame->calibration,&source,clips,measure);
}
int Frame144_AdcThresholdKind(const Scope144Cal *cal,int32_t voltage,unsigned kind,unsigned *raw)
{
    int64_t numerator,denominator,value;
    unsigned guard=kind<2?13u:2u;
    if(!raw || kind>7 || !Scope144_Valid(cal))return 0;
    numerator=((int64_t)cal->zero_q10-cal->a_bias_q10)*cal->reference_uv+
        (int64_t)voltage*cal->span_q10;
    denominator=(int64_t)cal->a_gain_q10*cal->reference_uv;
    if(denominator<=0)return 0;
    value=numerator>=0?(numerator+denominator/2)/denominator:
        -((-numerator+denominator/2)/denominator);
    if(value<guard || value>255u-guard)return 0;
    *raw=(unsigned)value;return 1;
}
int Frame144_AdcThreshold(const Scope144Cal *cal,int32_t voltage,unsigned *raw)
{ return Frame144_AdcThresholdKind(cal,voltage,0,raw); }
int Frame144_HardwareView(Frame144 *frame,unsigned width,unsigned base,ViewWindowGeometry *view)
{
    if(!Frame144_Valid(frame) || frame->capture.trigger_kind<CAPTURE_TRIGGER_HARDWARE_A ||
        !ViewWindow_Anchor(frame->capture.time_samples,width,Scope144_Step(base),
            frame->capture.trigger_q16,30,0,view))return 0;
    frame->capture.first_visible_q16=view->window_start_q16;return 1;
}
int Frame144_Trigger(Frame144 *frame,unsigned width,unsigned base,int32_t threshold,
    int falling,ViewWindowGeometry *view)
{
    uint32_t step=Scope144_Step(base);unsigned i;int32_t previous,current;
    ViewWindowGeometry candidate;
    if(!view || !Frame144_Valid(frame) || !step || !width ||
        !ViewWindow_Build(frame->capture.time_samples,width,step,0,view))return 0;
    frame->trigger_source=0;
    frame->capture.trigger_kind=CAPTURE_TRIGGER_NONE;frame->capture.trigger_q16=0;
    frame->capture.flags &= ~(CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER);
    if(!Read(frame,0,&previous))return 0;
    previous=Scope144_Voltage(&frame->calibration,previous);
    for(i=1;i<frame->capture.time_samples;++i) {
        if(!Read(frame,i,&current))return 0;
        current=Scope144_Voltage(&frame->calibration,current);
        if(falling ? previous>threshold && current<=threshold : previous<threshold && current>=threshold) {
            uint32_t pre=30u*step;
            if((uint64_t)i*65536>=pre &&
                ViewWindow_Anchor(frame->capture.time_samples,width,step,i*65536u,30,0,&candidate) &&
                !candidate.flags) {
                frame->capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;frame->capture.trigger_q16=i*65536u;
                frame->capture.flags|=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
                frame->capture.first_visible_q16=candidate.window_start_q16;*view=candidate;return 1;
            }
        }
        previous=current;
    }
    frame->capture.first_visible_q16=0;return 0;
}
unsigned Frame144_CsvRow(char out[80],const Frame144 *frame,unsigned sample,int factor)
{
    int32_t q;unsigned clip,n,bits=0,valid;uint64_t time;char adc;
    if(!out || !Frame144_Read(frame,sample,&q,&clip))return 0;
    time=((uint64_t)sample*frame->capture.sample_period_ps_q16+32768u)/65536u;
    if(time>INT32_MAX)return 0;
    adc=((sample & 1u)==(frame->capture.sample_phase==CAPTURE_PHASE_B_A))?'A':'B';
    n=Scope144_CsvPoint(out,&frame->calibration,sample,(uint32_t)time,q,clip,factor,adc);
    if(n<2 || n>69)return 0;
    n-=2;valid=Frame144_DigitalAt(frame,(int64_t)sample*65536,&bits);
    out[n++]=',';if(valid)out[n++]=(char)('0'+(bits&1));
    out[n++]=',';if(valid)out[n++]=(char)('0'+((bits>>1)&1));
    out[n++]=',';out[n++]=(char)('0'+valid);
    out[n++]='\r';out[n++]='\n';out[n]=0;return n;
}
int Frame144_Window(const Frame144 *frame,unsigned width,unsigned base,int64_t displacement,
    ViewWindowGeometry *view)
{
    ViewWindowGeometry origin;uint32_t step=Scope144_Step(base);int64_t start;
    if(!Frame144_Valid(frame) || !view)return 0;
    if(frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER) {
        if(!ViewWindow_Anchor(frame->capture.time_samples,width,step,
            frame->capture.trigger_q16,30,0,&origin))return 0;
    } else if(!ViewWindow_Build(frame->capture.time_samples,width,step,0,&origin))return 0;
    start=origin.window_start_q16;
    if(displacement>INT64_MAX-start)start=INT64_MAX;
    else start+=displacement;
    return ViewWindow_Build(frame->capture.time_samples,width,step,start,view);
}
static int Time(const Frame144 *frame,int64_t delta,int32_t *ps)
{
    int64_t value;
    if(!ps)return 0;
    /* Valid frame/view bounds keep this signed product below 2^63. */
    value=delta*(int64_t)frame->capture.sample_period_ps_q16;
    value=value>=0?(value+2147483648LL)/4294967296LL:
        -((-value+2147483648LL)/4294967296LL);
    if(value<INT32_MIN || value>INT32_MAX)return 0;
    *ps=(int32_t)value;return 1;
}
int Frame144_TimeAt(const Frame144 *frame,const ViewWindowGeometry *view,unsigned column,int32_t *ps)
{
    ViewWindowSample point;int64_t position;
    if(!Frame144_Valid(frame) || !view || view->capture_count!=frame->capture.time_samples ||
        !ViewWindow_At(view,column,&point))return 0;
    position=point.position_q16;
    if(frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER)position-=frame->capture.trigger_q16;
    return Time(frame,position,ps);
}
int Frame144_TimeDelta(const Frame144 *frame,const ViewWindowGeometry *view,unsigned first,unsigned second,int32_t *ps)
{
    int64_t delta;
    if(!Frame144_Valid(frame) || !view || view->capture_count!=frame->capture.time_samples ||
        !ViewWindow_Delta(view,first,second,&delta))return 0;
    return Time(frame,delta,ps);
}
