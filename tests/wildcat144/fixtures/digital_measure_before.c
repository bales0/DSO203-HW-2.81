#include "Frame144.h"
#include <string.h>
int Legacy_DigitalMeasure(const Frame144 *f,unsigned channel,Scope144Measurements *m)
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
