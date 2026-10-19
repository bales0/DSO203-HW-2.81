#include "Frame144.h"
#include <string.h>
int Frame144_DigitalAt(const Frame144 *f,int64_t position,unsigned *bits)
{
    uint32_t word;unsigned pair;
    if(!bits || !Frame144_Valid(f) || f->digital_shift_q16<-(32*65536) ||
        f->digital_shift_q16>32*65536 || position<0 ||
        position>(int64_t)(f->capture.time_samples-1u)*65536)return 0;
    position-=f->digital_shift_q16;
    if(position<0)return 0;
    pair=(unsigned)(position/131072);
    if(pair>=f->capture.valid_words)return 0;
    memcpy(&word,(const unsigned char *)f->words+4u*(f->capture.valid_begin+pair),4);
    *bits=(word>>16)&3u;return 1;
}
typedef struct {
    unsigned previous,have,rise,fall,cycles,seen_rise;
    uint32_t periods,highs;
} DigitalMeasureState;
/* NULL selects no work for a channel. Validate the immutable frame once,
 * read each aligned FIFO word once, and keep independent edge histories. */
unsigned Frame144_DigitalMeasurePair(const Frame144 *f,Scope144Measurements *c,Scope144Measurements *d)
{
    DigitalMeasureState state[2];Scope144Measurements *out[2];
    unsigned i,ch,bits,word,ready=0,first,last;int64_t end;
    if(c)memset(c,0,sizeof *c);
    if(d)memset(d,0,sizeof *d);
    if((!c && !d) || (c && c==d) || !Frame144_Valid(f) ||
        f->digital_shift_q16<-(32*65536) || f->digital_shift_q16>32*65536)return 0;
    first=f->digital_shift_q16<0?(unsigned)((-(int64_t)f->digital_shift_q16+131071)/131072):0;
    end=(int64_t)(f->capture.time_samples-1u)*65536-f->digital_shift_q16;
    if(end<0)return 0;
    last=(unsigned)(end/131072)+1u;
    if(last>f->capture.valid_words)last=f->capture.valid_words;
    memset(state,0,sizeof state);out[0]=c;out[1]=d;
    for(i=first;i<last;++i) {
        memcpy(&word,(const unsigned char *)f->words+4u*(f->capture.valid_begin+i),4);
        for(ch=0;ch<2;++ch)if(out[ch]) {
            DigitalMeasureState *s=&state[ch];
            bits=(word>>(16u+ch))&1u;++out[ch]->samples;
            if(s->have && bits!=s->previous) {
                if(bits) {
                    if(s->seen_rise && s->fall>s->rise) {
                        s->periods+=i-s->rise;s->highs+=s->fall-s->rise;++s->cycles;
                    }
                    s->rise=i;s->seen_rise=1;s->fall=0;
                } else if(s->seen_rise)s->fall=i;
            }
            s->previous=bits;s->have=1;
        }
    }
    for(ch=0;ch<2;++ch)if(out[ch]) {
        DigitalMeasureState *s=&state[ch];Scope144Measurements *m=out[ch];uint64_t period,high;
        if(!s->cycles || !s->periods)continue;
        period=((uint64_t)s->periods*2u*f->capture.sample_period_ps_q16+s->cycles*32768u)/(s->cycles*65536u);
        high=((uint64_t)s->highs*2u*f->capture.sample_period_ps_q16+s->cycles*32768u)/(s->cycles*65536u);
        if(!period || period>UINT32_MAX || high>period)continue;
        m->period_ps=(uint32_t)period;m->high_ps=(uint32_t)high;m->low_ps=(uint32_t)(period-high);
        m->frequency_hz=(uint32_t)((1000000000000ULL+period/2)/period);
        m->duty_ppm=(uint32_t)(((uint64_t)s->highs*1000000+s->periods/2)/s->periods);
        ready|=1u<<ch;
    }
    return ready;
}
int Frame144_DigitalMeasure(const Frame144 *f,unsigned channel,Scope144Measurements *m)
{
    if(!m)return 0;
    if(channel>1){memset(m,0,sizeof *m);return 0;}
    return (Frame144_DigitalMeasurePair(f,channel?0:m,channel?m:0)&(1u<<channel))!=0;
}
int Frame144_DigitalTrigger(Frame144 *f,unsigned width,unsigned base,unsigned channel,
    int falling,ViewWindowGeometry *view)
{
    unsigned i,bits,previous=0,have=0;int64_t time,selected=-1,fallback=-1;ViewWindowGeometry candidate;
    if(!view || !Frame144_Valid(f) || channel>1 ||
        !ViewWindow_Build(f->capture.time_samples,width,Scope144_Step(base),0,view))return 0;
    f->trigger_source=channel+2;
    f->capture.trigger_kind=CAPTURE_TRIGGER_NONE;f->capture.trigger_q16=0;
    f->capture.flags &= ~(CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER);
    for(i=0;i<f->capture.valid_words;++i) {
        time=(int64_t)i*131072+f->digital_shift_q16;
        if(time<0 || time>(int64_t)(f->capture.time_samples-1u)*65536)continue;
        if(!Frame144_DigitalAt(f,time,&bits))return 0;
        bits=(bits>>channel)&1u;
        if(have && bits!=previous && bits==(unsigned)!falling &&
            time>=30*(int64_t)Scope144_Step(base) &&
            ViewWindow_Anchor(f->capture.time_samples,width,Scope144_Step(base),
                (uint32_t)time,30,0,&candidate) && !candidate.flags) {
            unsigned column=width>150?150:width-1;
            if(fallback<0)fallback=time;
            if(time>=(int64_t)column*Scope144_Step(base) &&
                ViewWindow_Anchor(f->capture.time_samples,width,Scope144_Step(base),
                    (uint32_t)time,column,0,&candidate) && !candidate.flags){selected=time;break;}
        }
        previous=bits;have=1;
    }
    if(selected<0)selected=fallback;
    if(selected>=0) {
        f->capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;f->capture.trigger_q16=(uint32_t)selected;
        f->capture.flags|=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
        if(!Frame144_Window(f,width,base,0,view))return 0;
        f->capture.first_visible_q16=view->window_start_q16;return 1;
    }
    f->capture.first_visible_q16=0;return 0;
}
