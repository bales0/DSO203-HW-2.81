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
#endif
