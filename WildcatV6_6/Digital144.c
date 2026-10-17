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
int Frame144_DigitalMeasure(const Frame144 *f,unsigned channel,Scope144Measurements *m)
{
    unsigned i,bits,previous=0,have=0,rise=0,fall=0,cycles=0,seen_rise=0;
    uint32_t periods=0,highs=0;uint64_t period,high;int64_t time;
    if(!m)return 0;
    memset(m,0,sizeof *m);
    if(!Frame144_Valid(f) || channel>1)return 0;
    for(i=0;i<f->capture.valid_words;++i) {
        time=(int64_t)i*131072+f->digital_shift_q16;
        if(time<0 || time>(int64_t)(f->capture.time_samples-1u)*65536)continue;
        if(!Frame144_DigitalAt(f,time,&bits))return 0;
        bits=(bits>>channel)&1u;++m->samples;
        if(have && bits!=previous) {
            if(bits) {
                if(seen_rise && fall>rise) {periods+=i-rise;highs+=fall-rise;++cycles;}
                rise=i;seen_rise=1;fall=0;
            } else if(seen_rise)fall=i;
        }
        previous=bits;have=1;
    }
    if(!cycles || !periods)return 0;
    period=((uint64_t)periods*2u*f->capture.sample_period_ps_q16+cycles*32768u)/(cycles*65536u);
    high=((uint64_t)highs*2u*f->capture.sample_period_ps_q16+cycles*32768u)/(cycles*65536u);
    if(!period || period>UINT32_MAX || high>period)return 0;
    m->period_ps=(uint32_t)period;m->high_ps=(uint32_t)high;m->low_ps=(uint32_t)(period-high);
    m->frequency_hz=(uint32_t)((1000000000000ULL+period/2)/period);
    m->duty_ppm=(uint32_t)(((uint64_t)highs*1000000+periods/2)/periods);
    return 1;
}
int Frame144_DigitalTrigger(Frame144 *f,unsigned width,unsigned base,unsigned channel,
    int falling,ViewWindowGeometry *view)
{
    unsigned i,bits,previous=0,have=0;int64_t time;ViewWindowGeometry candidate;
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
            f->capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;f->capture.trigger_q16=(uint32_t)time;
            f->capture.flags|=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
            f->capture.first_visible_q16=candidate.window_start_q16;*view=candidate;return 1;
        }
        previous=bits;have=1;
    }
    f->capture.first_visible_q16=0;return 0;
}
