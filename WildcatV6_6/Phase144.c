#include "Phase144.h"
#define PHASE_MARKER 0x50483134u
static int Overlaps(const void *a, uint32_t an, const void *b, uint32_t bn)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    return x<=y ? y-x<an : x-y<bn;
}
void Phase144_Reset(Phase144State *state)
{
    unsigned i;
    if (!state) return;
    for (i=0;i<PHASE144_ERROR_COUNT;++i) state->errors[i]=0;
    state->marker=PHASE_MARKER;
    state->frames=0; state->fit_samples=0;
    state->clips_a=0; state->clips_b=0;
    state->min_a=255; state->min_b=255; state->max_a=0; state->max_b=0;
}
Interlace144Status Phase144_Add(const FpgaProfile *profile, uint32_t input_key,
    const Interlace144Calibration *cal, const uint16_t *pairs,
    uint32_t count, Phase144State *state)
{
    uint32_t i, valid_count;
    int lag;
    if (!profile || !cal || !pairs || !state) return INTERLACE144_ARGUMENT;
    if (!Interlace144_Available(profile)) return INTERLACE144_UNSUPPORTED;
    if (count<INTERLACE144_DISCARD_PAIRS+INTERLACE144_MIN_PAIRS ||
        count>INTERLACE144_MAX_PAIRS) return INTERLACE144_WINDOW;
    if (Overlaps(state,sizeof *state,pairs,count*sizeof *pairs) ||
        Overlaps(state,sizeof *state,cal,sizeof *cal) ||
        Overlaps(state,sizeof *state,profile,sizeof *profile)) return INTERLACE144_ARGUMENT;
    if (state->marker!=PHASE_MARKER || state->frames>=PHASE144_MAX_FRAMES)
        return INTERLACE144_ARGUMENT;
    if (cal->valid!=1 || cal->family!=profile->family || cal->protocol!=profile->protocol ||
        cal->implementation!=profile->implementation || cal->input_key!=input_key ||
        cal->gain_q10<512 || cal->gain_q10>2048 ||
        cal->bias_q10 < -510*1024 || cal->bias_q10 > 255*1024)
        return INTERLACE144_CALIBRATION;
    if (state->frames &&
        (state->calibration.family!=cal->family || state->calibration.protocol!=cal->protocol ||
         state->calibration.implementation!=cal->implementation ||
         state->calibration.input_key!=cal->input_key ||
         state->calibration.gain_q10!=cal->gain_q10 || state->calibration.bias_q10!=cal->bias_q10))
        return INTERLACE144_CALIBRATION;
    state->calibration=*cal;
    valid_count=count-INTERLACE144_DISCARD_PAIRS;
    for (i=INTERLACE144_DISCARD_PAIRS;i<count;++i) {
        unsigned a=pairs[i]&255u, b=pairs[i]>>8;
        if (a<state->min_a) state->min_a=(uint8_t)a;
        if (a>state->max_a) state->max_a=(uint8_t)a;
        if (b<state->min_b) state->min_b=(uint8_t)b;
        if (b>state->max_b) state->max_b=(uint8_t)b;
        state->clips_a+=a<=2 || a>=253;
        state->clips_b+=b<=2 || b>=253;
    }
    for (lag=-PHASE144_MAX_LAG;lag<=PHASE144_MAX_LAG;++lag) {
        int whole=lag/8, fraction=lag%8;
        uint64_t total=0;
        if (fraction<0) { fraction+=8; --whole; }
        for (i=2;i<valid_count-3;++i) {
            uint32_t index=(uint32_t)((int32_t)i+whole)+INTERLACE144_DISCARD_PAIRS;
            int32_t a0=pairs[index]&255u, a1=pairs[index+1]&255u;
            int32_t b=(pairs[i+INTERLACE144_DISCARD_PAIRS]>>8)*cal->gain_q10+cal->bias_q10;
            int32_t difference=b-(a0*(8-fraction)+a1*fraction)*128;
            total+=(uint32_t)(difference<0 ? -difference : difference);
        }
        state->errors[lag+PHASE144_MAX_LAG]+=total;
    }
    state->fit_samples+=valid_count-5;
    ++state->frames;
    return INTERLACE144_OK;
}
Interlace144Status Phase144_Finish(const Phase144State *state, Phase144Result *result)
{
    Phase144Result next;
    unsigned best=0, i;
    uint64_t threshold;
    if (!state || !result || Overlaps(state,sizeof *state,result,sizeof *result))
        return INTERLACE144_ARGUMENT;
    if (state->marker!=PHASE_MARKER || !state->frames ||
        state->frames>PHASE144_MAX_FRAMES || !state->fit_samples)
        return INTERLACE144_WINDOW;
    for (i=1;i<PHASE144_ERROR_COUNT;++i)
        if (state->errors[i]<state->errors[best]) best=i;
    next.lag8=(int32_t)best-PHASE144_MAX_LAG;
    threshold=state->errors[best]+state->errors[best]/100;
    next.near_best=0;
    for (i=0;i<PHASE144_ERROR_COUNT;++i)
        next.near_best+=state->errors[i]<=threshold;
    next.error_q10[0]=(uint32_t)(state->errors[PHASE144_MAX_LAG-4]/state->fit_samples);
    next.error_q10[1]=(uint32_t)(state->errors[PHASE144_MAX_LAG]/state->fit_samples);
    next.error_q10[2]=(uint32_t)(state->errors[PHASE144_MAX_LAG+4]/state->fit_samples);
    next.span_a=state->max_a-state->min_a; next.span_b=state->max_b-state->min_b;
    next.clips_a=state->clips_a; next.clips_b=state->clips_b;
    next.valid=next.span_a>=10 && next.span_b>=10 && !next.clips_a && !next.clips_b &&
        next.lag8>-PHASE144_MAX_LAG && next.lag8<PHASE144_MAX_LAG && next.near_best<=3;
    *result=next;
    return INTERLACE144_OK;
}
int Phase144_Compare(const Phase144Result *separate, const Phase144Result *interlace)
{
    int32_t difference;
    if (!separate || !interlace || !separate->valid || !interlace->valid ||
        separate->lag8 < -16 || separate->lag8 > 16 ||
        interlace->lag8 < -16 || interlace->lag8 > 16) return 0;
    difference=interlace->lag8-separate->lag8;
    if (difference>=3 && difference<=5) return 1;
    if (difference<=-3 && difference>=-5) return -1;
    return 0;
}
