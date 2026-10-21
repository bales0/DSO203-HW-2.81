#ifndef WILDCAT_CAL_STABLE_H
#define WILDCAT_CAL_STABLE_H
#include "UnifiedCal.h"
/* Span of the entire three-block window; adjacent-only comparison would
 * accept a slowly drifting zero. Each input is a time-distributed 4096-sample block. */
typedef struct { CalMean values[3];unsigned count,next;int32_t spread_a,spread_b; } CalStable;
static inline int CalStable_Push(CalStable *s,const CalMean *m,CalMean *out)
{
    unsigned i;int32_t amin,amax,bmin,bmax;int64_t a=0,b=0;
    if(!CalMean_Valid(m)){s->count=0;s->next=0;s->spread_a=s->spread_b=0;return 0;}
    s->values[s->next]=*m;s->next=(s->next+1)%3;
    if(s->count<3)++s->count;
    amin=amax=s->values[0].a;bmin=bmax=s->values[0].b;
    for(i=0;i<s->count;++i) {
        int32_t av=s->values[i].a,bv=s->values[i].b;
        if(av<amin)amin=av;
        if(av>amax)amax=av;
        if(bv<bmin)bmin=bv;
        if(bv>bmax)bmax=bv;
        a+=av;b+=bv;
    }
    s->spread_a=amax-amin;s->spread_b=bmax-bmin;
    if(s->count<3 || s->spread_a>CAL_OFFSET_STABILITY || s->spread_b>CAL_OFFSET_STABILITY)return 0;
    out->a=(int32_t)((a+1)/3);out->b=(int32_t)((b+1)/3);out->clips=0;return 1;
}

/* Workspace-owned, never a large automatic array. Four time slices retain
 * temporal information which a histogram alone would lose. */
#define CAL_DC_PAIRS 4096u
#define CAL_DC_MIN_RADIUS 2u
#define CAL_DC_MAX_RADIUS 48u
#define CAL_DC_MAX_MAD2 64u
#define CAL_DC_REJECT_DIVISOR 64u
#define CAL_DC_MAX_RUN 2u
#define CAL_DC_SLICE_RAILS 16u
#define CAL_DC_MEAN_SHIFT 1024
#define CAL_DC_MEDIAN_SHIFT 2048
#define CAL_DC_TIME_SPAN 2048
typedef struct {
    uint16_t bins[256],count[4],run,max_run;
    uint32_t sum[4];
} CalDcHistogram;
static inline void CalDc_Add(CalDcHistogram *h,unsigned code,unsigned index)
{
    unsigned slice=index/1024u;
    ++h->bins[code];
    if(!code || code==255) {
        if(++h->run>h->max_run)h->max_run=h->run;
    } else {h->run=0;++h->count[slice];h->sum[slice]+=code;}
}
/* Twice the central order statistic, so even populations retain half codes. */
static inline unsigned CalDc_Median2(const uint16_t *bins,unsigned n)
{
    unsigned k,total=0,lo=0,hi=0;
    for(k=1;k<255;++k) {
        total+=bins[k];
        if(!lo && total>=(n+1)/2)lo=k;
        if(total>n/2){hi=k;break;}
    }
    return lo+hi;
}
static inline int32_t CalDc_Abs(int32_t v){return v<0?-v:v;}
static inline int CalDc_Estimate(const CalDcHistogram *h,int32_t *out)
{
    unsigned k,t,n=0,median,mad=0,radius,inside=0,rails=h->bins[0]+h->bins[255];
    uint32_t sum=0,kept=0;int32_t raw,robust,lo=0,hi=0;
    *out=0;
    for(k=0;k<4;++k){n+=h->count[k];sum+=h->sum[k];}
    if(n)*out=(int32_t)((sum*1024u+n/2)/n);
    if(n+rails!=CAL_DC_PAIRS || rails>CAL_CAPTURE_MAX_CLIPS ||
       h->max_run>CAL_DC_MAX_RUN || !n)return CAL_CAPTURE_CLIPPED;
    median=CalDc_Median2(h->bins,n);
    /* MAD in half-code units, counted directly from the histogram. */
    {unsigned count=0;
    for(t=0;t<=CAL_DC_MAX_MAD2;++t) {
        unsigned previous=count;int left=(int)median-(int)t,right=(int)median+(int)t;
        if((right&1)==0) {
            if(left>=2 && left<510)count+=h->bins[left/2];
            if(t && right>=2 && right<510)count+=h->bins[right/2];
        }
        if(previous<(n+1)/2 && count>=(n+1)/2)mad=t;
        if(count>n/2){mad=(mad+t+1)/2;break;}
    }}
    if(t>CAL_DC_MAX_MAD2)return CAL_CAPTURE_UNSTABLE;
    radius=(3*mad+1)/2;
    if(radius<CAL_DC_MIN_RADIUS)radius=CAL_DC_MIN_RADIUS;
    if(radius>CAL_DC_MAX_RADIUS)radius=CAL_DC_MAX_RADIUS;
    for(k=1;k<255;++k)if((unsigned)CalDc_Abs((int32_t)(2*k)-(int32_t)median)<=2*radius) {
        inside+=h->bins[k];kept+=k*h->bins[k];
    }
    if(!inside || n-inside>n/CAL_DC_REJECT_DIVISOR)return CAL_CAPTURE_UNSTABLE;
    raw=(int32_t)((sum*1024u+n/2)/n);
    robust=(int32_t)((kept*1024u+inside/2)/inside);*out=robust;
    if(robust<10*1024 || robust>245*1024)return CAL_CAPTURE_CLIPPED;
    if(CalDc_Abs(robust-raw)>CAL_DC_MEAN_SHIFT ||
       CalDc_Abs(raw-(int32_t)median*512)>CAL_DC_MEDIAN_SHIFT)return CAL_CAPTURE_UNSTABLE;
    for(k=0;k<4;++k) {
        int32_t mean;
        if(1024u-h->count[k]>CAL_DC_SLICE_RAILS)return CAL_CAPTURE_CLIPPED;
        mean=(int32_t)((h->sum[k]*1024u+h->count[k]/2)/h->count[k]);
        if(!k || mean<lo)lo=mean;
        if(!k || mean>hi)hi=mean;
    }
    return hi-lo>CAL_DC_TIME_SPAN?CAL_CAPTURE_UNSTABLE:1;
}
/* Poll physical hold on every wizard iteration; only existing key events
 * generate changes. Releases, reverse direction and range/stage changes reset. */
typedef struct { uint32_t started,last;unsigned direction,range,emitted; } CalRefHold;
static inline CalKey CalRefHold_Key(CalRefHold *h,CalKey key,CalStage stage,
    unsigned range,unsigned direction,uint32_t now)
{
    if(stage!=CAL_REFERENCE || !direction || direction==3 || key==CAL_RANGE_DOWN || key==CAL_RANGE_UP) {
        h->direction=0;h->emitted=0;return direction==3 && (key==CAL_UP || key==CAL_DOWN)?CAL_NONE:key;
    }
    if(h->direction!=direction || h->range!=range) {
        h->direction=direction;h->range=range;h->started=now;h->emitted=0;
    }
    if((key==CAL_DOWN && direction==1) || (key==CAL_UP && direction==2)) {
        uint32_t elapsed=now-h->started;
        if(h->emitted && (uint32_t)(now-h->last)<2880000u)return CAL_NONE;
        h->last=now;h->emitted=1;
        if(elapsed>=144000000u)return direction==1?CAL_FAST_DOWN:CAL_FAST_UP;
        if(elapsed>=50400000u)return direction==1?CAL_BIG_DOWN:CAL_BIG_UP;
    }
    return key;
}
#endif
