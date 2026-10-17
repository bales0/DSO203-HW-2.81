#include "Scope144.h"
#include "UiCommon.h"
#include <string.h>
#include <limits.h>
static int clock_ppm;
static int32_t RoundRatio(int64_t n,int64_t d)
{ return (int32_t)(n>=0?(n+d/2)/d:-((-n+d/2)/d)); }
int Scope144_SetClock(int ppm) { clock_ppm=(ppm>=-999 && ppm<=999)?ppm:0; return clock_ppm; }
uint32_t Scope144_Crc(const void *data, uint32_t count)
{
    const uint8_t *p=data; uint32_t crc=0xffffffffu; unsigned bit;
    while(count--) { crc^=*p++; for(bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0); }
    return ~crc;
}
uint32_t Scope144_Key(unsigned range) { return 0x14412800u | range; }
int Scope144_Valid(const Scope144Cal *c)
{
    return c && c->valid==1 && c->gain_q10>=512 && c->gain_q10<=2048 &&
        c->bias_q10>=-800000 && c->bias_q10<=800000 &&
        c->zero_q10>=10*1024 && c->zero_q10<=245*1024 &&
        c->span_q10>=8*1024 && c->span_q10<=235*1024 &&
        c->zero_q10+c->span_q10<=245*1024 &&
        c->a_gain_q10>=512 && c->a_gain_q10<=2048 &&
        c->a_bias_q10>=-800000 && c->a_bias_q10<=800000 &&
        c->dac_a_q10>=512 && c->dac_a_q10<=2048 &&
        c->dac_b_q10>=512 && c->dac_b_q10<=2048 &&
        c->ref_dac_a_q10>=512 && c->ref_dac_a_q10<=2048 &&
        c->ref_dac_b_q10>=512 && c->ref_dac_b_q10<=2048 &&
        c->adc_offset>=0 && c->adc_offset<=54 &&
        c->reference_uv>=1000 && c->reference_uv<=60000000 &&
        (int64_t)c->reference_uv*1024<=(int64_t)c->span_q10*800000;
}
int Scope144_CalibrateMeans(int32_t la,int32_t lb,int32_t ha,int32_t hb,int32_t reference,Scope144Cal *c)
{
    Scope144Cal next;int32_t da=ha-la,db=hb-lb;
    memset(&next,0,sizeof(next));next.a_gain_q10=next.dac_a_q10=next.dac_b_q10=next.ref_dac_a_q10=next.ref_dac_b_q10=1024;
    if(!c || la<10*1024 || lb<10*1024 || ha>245*1024 || hb>245*1024 || da<8*1024 || db<8*1024)return 0;
    next.gain_q10=RoundRatio((int64_t)da*1024,db);
    next.bias_q10=la-RoundRatio((int64_t)lb*next.gain_q10,1024);
    next.zero_q10=la;next.span_q10=da;next.reference_uv=reference;next.valid=1;
    if(!Scope144_Valid(&next))return 0;
    *c=next;return 1;
}
int Scope144_Calibrate(const FpgaProfile *profile,unsigned range,
    const uint16_t *low,const uint16_t *high,unsigned count,int32_t reference,Scope144Cal *c)
{
    Interlace144Calibration relative; Scope144Cal next; uint32_t i, a=0,b=0,n;
    if(!c) return 0;
    memset(c,0,sizeof(*c));
    memset(&next,0,sizeof(next));next.a_gain_q10=next.dac_a_q10=next.dac_b_q10=next.ref_dac_a_q10=next.ref_dac_b_q10=1024;
    if(!profile || profile->kind!=FPGA_W12 || range>=SCOPE144_RANGES ||
       count<168 || count>INTERLACE144_MAX_PAIRS || !low || !high) return 0;
    if(Interlace144_Calibrate(profile,Scope144_Key(range),low,count,high,count,&relative)!=INTERLACE144_OK) return 0;
    n=count-INTERLACE144_DISCARD_PAIRS;
    for(i=INTERLACE144_DISCARD_PAIRS;i<count;++i) { a+=low[i]&255; b+=high[i]&255; }
    next.gain_q10=relative.gain_q10; next.bias_q10=relative.bias_q10;
    next.zero_q10=(int32_t)(((uint64_t)a*1024+n/2)/n);
    next.span_q10=(int32_t)(((uint64_t)b*1024+n/2)/n)-next.zero_q10;
    next.reference_uv=reference; next.valid=1;
    if(!Scope144_Valid(&next)) return 0;
    *c=next; return 1;
}
void Scope144_Relative(const Scope144Cal *c,unsigned range,Interlace144Calibration *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(range>=SCOPE144_RANGES || !Scope144_Valid(c)) return;
    out->gain_q10=c->gain_q10; out->bias_q10=c->bias_q10;
    out->family=0x5731; out->protocol=0x0101; out->implementation=0x0102;
    out->input_key=Scope144_Key(range); out->valid=1;
}
static void Put(uint8_t *p,uint32_t v) { unsigned i; for(i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8)); }
static uint32_t Get(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
typedef char Scope144RecordSize[(sizeof(Scope144Cal)==SCOPE144_RECORD_BYTES)?1:-1];
/* Explicit little-endian v3; 40-byte header + 16*52-byte records + padding + CRC. */
int Scope144_Encode(const Scope144Bank *bank,uint32_t serial,uint8_t out[SCOPE144_CAL_BYTES])
{
    unsigned i,j;const Scope144Cal *c;uint8_t *p;
    static const Scope144Cal empty={0};
    if(!bank || !out)return 0;
    for(i=0;i<16;++i) {
        c=i<8?&bank->range[i]:&bank->high[i-8];
        if(c->valid>1 || (!c->valid && memcmp(c,&empty,sizeof(*c))) ||
           (c->valid && (!Scope144_Valid(c) || bank->battery[i/8]<2800 || bank->battery[i/8]>5200)))return 0;
    }
    memset(out,0,SCOPE144_CAL_BYTES);memcpy(out,"I144",4);
    Put(out+4,SCOPE144_CAL_VERSION);Put(out+8,serial);Put(out+12,SCOPE144_IMAGE_CRC);
    Put(out+16,bank->generation);Put(out+20,SCOPE144_CAL_BYTES);
    Put(out+24,bank->battery[0]);Put(out+28,bank->battery[1]);Put(out+32,bank->native_crc);
    for(i=0;i<16;++i) {
        int32_t fields[13];c=i<8?&bank->range[i]:&bank->high[i-8];p=out+40+i*SCOPE144_RECORD_BYTES;
        if(!c->valid)continue;
        fields[0]=c->gain_q10;fields[1]=c->bias_q10;fields[2]=c->zero_q10;fields[3]=c->span_q10;
        fields[4]=c->reference_uv;fields[5]=1;fields[6]=c->a_gain_q10;fields[7]=c->a_bias_q10;
        fields[8]=c->dac_a_q10;fields[9]=c->dac_b_q10;fields[10]=c->adc_offset;
        fields[11]=c->ref_dac_a_q10;fields[12]=c->ref_dac_b_q10;
        for(j=0;j<13;++j)Put(p+j*4,(uint32_t)fields[j]);
    }
    Put(out+1020,Scope144_Crc(out,1020));return 1;
}
static void DecodeCal(const uint8_t *p,Scope144Cal *c)
{
    c->gain_q10=(int32_t)Get(p);c->bias_q10=(int32_t)Get(p+4);
    c->zero_q10=(int32_t)Get(p+8);c->span_q10=(int32_t)Get(p+12);
    c->reference_uv=(int32_t)Get(p+16);c->valid=Get(p+20);
    c->a_gain_q10=(int32_t)Get(p+24);c->a_bias_q10=(int32_t)Get(p+28);
    c->dac_a_q10=(int32_t)Get(p+32);c->dac_b_q10=(int32_t)Get(p+36);c->adc_offset=(int32_t)Get(p+40);
    c->ref_dac_a_q10=(int32_t)Get(p+44);c->ref_dac_b_q10=(int32_t)Get(p+48);
}
int Scope144_Decode(const uint8_t in[SCOPE144_CAL_BYTES],uint32_t serial,Scope144Bank *bank)
{
    unsigned i;Scope144Cal c;
    if(!in || !bank || memcmp(in,"I144",4) || Get(in+4)!=SCOPE144_CAL_VERSION || Get(in+8)!=serial ||
        Get(in+12)!=SCOPE144_IMAGE_CRC || Get(in+20)!=SCOPE144_CAL_BYTES ||
        Get(in+1020)!=Scope144_Crc(in,1020))return 0;
    for(i=36;i<40;++i)if(in[i])return 0;
    for(i=40+16*SCOPE144_RECORD_BYTES;i<1020;++i)if(in[i])return 0;
    for(i=0;i<16;++i) {
        const uint8_t *p=in+40+i*SCOPE144_RECORD_BYTES;unsigned j;DecodeCal(p,&c);
        if(c.valid>1 || (c.valid && (!Scope144_Valid(&c) || Get(in+24+(i/8)*4)<2800 || Get(in+24+(i/8)*4)>5200)))return 0;
        if(!c.valid)for(j=0;j<SCOPE144_RECORD_BYTES;++j)if(p[j])return 0;
    }
    memset(bank,0,sizeof(*bank));bank->generation=Get(in+16);
    bank->battery[0]=Get(in+24);bank->battery[1]=Get(in+28);bank->native_crc=Get(in+32);
    for(i=0;i<16;++i)DecodeCal(in+40+i*SCOPE144_RECORD_BYTES,i<8?&bank->range[i]:&bank->high[i-8]);
    return 1;
}
unsigned Scope144_Command(int adc_offset)
{ return adc_offset>=0 && adc_offset<=54?(unsigned)adc_offset+SCOPE144_PIVOT:0; }
static int32_t Blend(int32_t a,int32_t b,uint32_t weight)
{ return a+(int32_t)(((int64_t)b-a)*weight/65536); }
static int Rebase(const Scope144Cal *c,int offset,Scope144Cal *out)
{
    int delta=offset-c->adc_offset;*out=*c;
    if(!delta)return 1; /* Preserve the measured anchor exactly. */
    if(c->ref_dac_a_q10==c->dac_a_q10 && c->ref_dac_b_q10==c->dac_b_q10) {
        /* Preserve the previous rounding when the two responses coincide. */
        out->bias_q10+=(int32_t)((int64_t)delta*(c->dac_a_q10-(int64_t)c->dac_b_q10*c->gain_q10/1024));
        out->a_bias_q10-=(int32_t)((int64_t)delta*c->dac_a_q10*c->a_gain_q10/1024);
    } else {
        int32_t za=(int32_t)(((int64_t)c->zero_q10-c->a_bias_q10)*1024/c->a_gain_q10);
        int32_t zb=(int32_t)(((int64_t)za-c->bias_q10)*1024/c->gain_q10);
        int32_t da=(int32_t)((int64_t)c->span_q10*1024/c->a_gain_q10);
        int32_t db=(int32_t)((int64_t)da*1024/c->gain_q10);
        /* Rebase BOTH measured endpoints. Their difference changes when
         * reference and ground have distinct offset slopes. */
        za+=delta*c->dac_a_q10;zb+=delta*c->dac_b_q10;
        da+=delta*(c->ref_dac_a_q10-c->dac_a_q10);
        db+=delta*(c->ref_dac_b_q10-c->dac_b_q10);
        if(da<8*1024 || db<8*1024 || da>235*1024 || db>235*1024)return 0;
        out->gain_q10=RoundRatio((int64_t)da*1024,db);
        out->bias_q10=za-RoundRatio((int64_t)zb*out->gain_q10,1024);
        out->a_gain_q10=RoundRatio((int64_t)c->span_q10*1024,da);
        out->a_bias_q10=c->zero_q10-RoundRatio((int64_t)za*out->a_gain_q10,1024);
    }
    out->adc_offset=offset;return Scope144_Valid(out);
}
int Scope144_Select(const Scope144Bank *bank,unsigned range,int offset,uint32_t battery,Scope144Cal *out)
{
    Scope144Cal a,b;uint32_t weight;int va,vb;
    if(!bank || !out || range>=8 || !Scope144_Command(offset) || battery<2800 || battery>5200)return 0;
    va=Scope144_Valid(&bank->range[range]) && bank->battery[0]>=2800 && bank->battery[0]<=5200;
    vb=Scope144_Valid(&bank->high[range]) && bank->battery[1]>=2800 && bank->battery[1]<=5200;
    if(!va && !vb)return 0;
    if(va)va=Rebase(&bank->range[range],offset,&a);
    if(vb)vb=Rebase(&bank->high[range],offset,&b);
    if(!va && !vb)return 0;
    if(!va){*out=b;return Scope144_Valid(out)?1:0;}
    if(!vb || bank->battery[0]==bank->battery[1]){*out=a;return Scope144_Valid(out)?1:0;}
    if(bank->battery[0]>bank->battery[1]){Scope144Cal swap=a;a=b;b=swap;}
    {uint32_t low=bank->battery[0]<bank->battery[1]?bank->battery[0]:bank->battery[1];
     uint32_t high=bank->battery[0]>bank->battery[1]?bank->battery[0]:bank->battery[1];
     weight=battery<=low?0:battery>=high?65536:(battery-low)*65536/(high-low);}
    if(!weight){*out=a;return Scope144_Valid(out)?2:0;}
    if(weight==65536){*out=b;return Scope144_Valid(out)?2:0;}
    *out=a;
    /* Interpolate measured physical endpoints, not products/inverse gains.
     * Blending affine coefficients directly creates a false DC offset when
     * both gain and raw ground change with supply voltage. */
    {
        int32_t za=(int32_t)(((int64_t)a.zero_q10-a.a_bias_q10)*1024/a.a_gain_q10);
        int32_t zb=(int32_t)(((int64_t)b.zero_q10-b.a_bias_q10)*1024/b.a_gain_q10);
        int32_t ba=(int32_t)(((int64_t)za-a.bias_q10)*1024/a.gain_q10);
        int32_t bb=(int32_t)(((int64_t)zb-b.bias_q10)*1024/b.gain_q10);
        int32_t da=(int32_t)((int64_t)a.span_q10*1024/a.a_gain_q10);
        int32_t db=(int32_t)((int64_t)b.span_q10*1024/b.a_gain_q10);
        int32_t ea=(int32_t)((int64_t)da*1024/a.gain_q10);
        int32_t eb=(int32_t)((int64_t)db*1024/b.gain_q10);
        za=Blend(za,zb,weight);ba=Blend(ba,bb,weight);
        da=Blend(da,db,weight);ea=Blend(ea,eb,weight);
        if(da<8*1024 || ea<8*1024)return 0;
        out->zero_q10=Blend(a.zero_q10,b.zero_q10,weight);out->span_q10=Blend(a.span_q10,b.span_q10,weight);
        out->reference_uv=Blend(a.reference_uv,b.reference_uv,weight);
        out->gain_q10=RoundRatio((int64_t)da*1024,ea);
        out->bias_q10=za-RoundRatio((int64_t)ba*out->gain_q10,1024);
        out->a_gain_q10=RoundRatio((int64_t)out->span_q10*1024,da);
        out->a_bias_q10=out->zero_q10-RoundRatio((int64_t)za*out->a_gain_q10,1024);
    }
    out->dac_a_q10=Blend(a.dac_a_q10,b.dac_a_q10,weight);out->dac_b_q10=Blend(a.dac_b_q10,b.dac_b_q10,weight);
    out->ref_dac_a_q10=Blend(a.ref_dac_a_q10,b.ref_dac_a_q10,weight);
    out->ref_dac_b_q10=Blend(a.ref_dac_b_q10,b.ref_dac_b_q10,weight);
    return Scope144_Valid(out)?2:0;
}
int Scope144_Normalize(const Scope144Cal *cal,int32_t *q,unsigned count)
{
    unsigned i;if(!Scope144_Valid(cal) || !q || count>1024)return 0;
    for(i=0;i<count;++i) {
        int64_t v=(int64_t)q[i]*cal->a_gain_q10/1024+cal->a_bias_q10;
        if(v<INT32_MIN || v>INT32_MAX)return 0;
        q[i]=(int32_t)v;
    }
    return 1;
}
int Scope144_Newer(uint32_t a,uint32_t b) { return a!=b && (uint32_t)(a-b)<0x80000000u; }
int32_t Scope144_Voltage(const Scope144Cal *cal,int32_t q10)
{
    int64_t value;
    if(!Scope144_Valid(cal)) return 0;
    value=((int64_t)q10-cal->zero_q10)*cal->reference_uv/cal->span_q10;
    if(value>INT32_MAX) return INT32_MAX;
    if(value<INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}
static uint32_t Sqrt(uint64_t value)
{
    uint64_t root=0,bit=(uint64_t)1<<62;
    while(bit>value) bit>>=2;
    while(bit) { if(value>=root+bit) { value-=root+bit; root=(root>>1)+bit; } else root>>=1; bit>>=2; }
    return (uint32_t)root;
}
int Scope144_MeasureSource(const Scope144Cal *cal,const Scope144Source *source,unsigned clips,Scope144Measurements *out)
{
    Scope144Measurements m; unsigned i,count,edges=0; int64_t sum=0,center,d;
    uint64_t square=0,first=0,last=0,pos,high=0,rise=0; int32_t lo,hi,threshold,q,previous;
    if(!Scope144_Valid(cal) || !source || !source->read || !out || source->count<16 ||
        source->count>SCOPE144_MAX_SAMPLES || source->period_ps_q16>8000ULL*65536 ||
        (source->period_ps_q16 && source->period_ps_q16<6000ULL*65536)) return 0;
    count=source->count;
    if(!source->read(source->user,0,&q))return 0;
    lo=hi=q;
    for(i=0;i<count;++i) {
        /* Acquisition corrections have a tighter bound than arbitrary int32 input. */
        if(!source->read(source->user,i,&q) || q<-4000000 || q>4000000) return 0;
        if(q<lo)lo=q;
        if(q>hi)hi=q;
        d=(int64_t)q-cal->zero_q10; sum+=d; square+=(uint64_t)(d*d);
    }
    memset(&m,0,sizeof(m)); m.samples=count; m.clips=clips;
    m.minimum_uv=Scope144_Voltage(cal,lo); m.maximum_uv=Scope144_Voltage(cal,hi);
    center=sum/count; m.average_uv=Scope144_Voltage(cal,(int32_t)center+cal->zero_q10);
    {uint64_t rms=(uint64_t)Sqrt(square/count)*cal->reference_uv/cal->span_q10;
     m.rms_uv=rms>INT32_MAX?INT32_MAX:(int32_t)rms;}
    threshold=lo+(hi-lo)/2;
    if(!source->read(source->user,0,&previous))return 0;
    for(i=1;i<count;++i) {
        if(!source->read(source->user,i,&q))return 0;
        if(previous<threshold && q>=threshold && hi-lo>=10*1024) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
            if(!edges)first=pos;
            last=pos; ++edges;
        }
        previous=q;
    }
    if(edges>1 && !clips) {
        uint64_t period=(last-first)/(edges-1);
        if(source->period_ps_q16) {
            m.period_ps=(uint32_t)((period*source->period_ps_q16+0x80000000ULL)>>32);
            if(!m.period_ps)return 0;
            m.frequency_hz=(uint32_t)((1000000000000ULL+m.period_ps/2)/m.period_ps);
        } else {
            m.frequency_hz=(uint32_t)(((uint64_t)(144u*(1000000+clock_ppm))<<16)/period);
            m.period_ps=(uint32_t)(period*62500*1000000/((uint64_t)9*65536*(1000000+clock_ppm)));
        }
        /* Integrate high time over complete cycles only; exclude record ends. */
        rise=first;
        if(!source->read(source->user,0,&previous))return 0;
        for(i=1;i<count;++i) {
            if(!source->read(source->user,i,&q))return 0;
            if(previous<threshold && q>=threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
                if(pos>=first && pos<=last)rise=pos;
            } else if(previous>=threshold && q<threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(previous-threshold)<<16)/(previous-q);
                if(pos>first && pos<=last && pos>=rise)high+=pos-rise;
            }
            previous=q;
        }
        m.duty_ppm=(uint32_t)(high*1000000/(last-first));
        m.high_ps=(uint32_t)((uint64_t)m.period_ps*m.duty_ppm/1000000);
        m.low_ps=m.period_ps-m.high_ps;
    }
    *out=m; return 1;
}
static int ArrayRead(const void *user,unsigned i,int32_t *q)
{ *q=((const int32_t *)user)[i];return 1; }
int Scope144_Measure(const Scope144Cal *cal,const int32_t *q,unsigned count,unsigned clips,Scope144Measurements *out)
{
    Scope144Source source;
    if(!q || count>1024)return 0;
    source.user=q;source.count=count;source.read=ArrayRead;source.period_ps_q16=0;
    return Scope144_MeasureSource(cal,&source,clips,out);
}
unsigned Scope144_TimePs(unsigned sample)
{
    uint64_t denominator=9u*(1000000+clock_ppm);
    return (unsigned)(((uint64_t)sample*62500*1000000+denominator/2)/denominator);
}
uint32_t Scope144_Step(unsigned base)
{
    static const unsigned ns[3]={100,200,500};
    return base<3 ? (uint32_t)(((uint64_t)ns[base]*144*65536*(1000000+clock_ppm)+15000000000ULL)/30000000000ULL) : 0;
}
int32_t Scope144_CursorTime(unsigned base,unsigned x)
{
    return (int32_t)((uint64_t)x*Scope144_Step(base)*62500*1000000/
        ((uint64_t)9*65536*(1000000+clock_ppm)));
}
int Scope144_At(const int32_t *q,unsigned count,uint32_t pos,int32_t *value)
{
    unsigned index=pos>>16,frac=pos&65535;
    if(!q || !value || !count || index>=count || (frac && index+1>=count)) return 0;
    *value=q[index]; if(frac) *value+=(int32_t)(((int64_t)q[index+1]-q[index])*frac/65536);
    return 1;
}
int Scope144_Trigger(const Scope144Cal *cal,const int32_t *q,unsigned count,unsigned base,
    int32_t threshold,int falling,unsigned *start)
{
    unsigned i,pre,window; uint32_t step=Scope144_Step(base);
    if(!Scope144_Valid(cal) || !q || !start || !step || count>1024) return 0;
    pre=(step*30+65535)>>16; window=(step*386+65535)>>16;
    if(window>=count) return 0;
    for(i=pre;i<count && i-pre+window<count;++i) {
        int32_t a=Scope144_Voltage(cal,q[i-1]),b=Scope144_Voltage(cal,q[i]);
        if(falling ? a>threshold && b<=threshold : a<threshold && b>=threshold) { *start=i-pre; return 1; }
    }
    return 0;
}
static unsigned Decimal(char *out,unsigned n,int32_t v)
{
    char digits[12]; unsigned k=0; uint32_t a;
    if(v<0) { out[n++]='-'; a=0u-(uint32_t)v; } else a=(uint32_t)v;
    do { digits[k++]=(char)('0'+a%10); a/=10; } while(a);
    while(k)out[n++]=digits[--k];
    return n;
}
unsigned Scope144_CsvPoint(char out[80],const Scope144Cal *cal,unsigned sample,
    uint32_t time,int32_t q,unsigned clip,int factor,char adc)
{
    unsigned n=0; int64_t voltage;
    if(!out || !Scope144_Valid(cal) || sample>=SCOPE144_MAX_SAMPLES || time>INT32_MAX ||
       (adc!='A' && adc!='B') || clip>1 ||
       (factor!=1 && factor!=-1 && factor!=10 && factor!=-10)) return 0;
    voltage=(int64_t)Scope144_Voltage(cal,q)*factor;
    if(voltage>INT32_MAX || voltage<INT32_MIN) return 0;
    n=Decimal(out,n,(int32_t)sample); out[n++]=',';
    n=Decimal(out,n,(int32_t)time); out[n++]=',';
    n=Decimal(out,n,(int32_t)voltage); out[n++]=',';
    out[n++]=adc; out[n++]=','; out[n++]=(char)('0'+clip);
    out[n++]='\r'; out[n++]='\n'; out[n]=0; return n;
}
unsigned Scope144_CsvScaled(char out[80],const Scope144Cal *cal,unsigned sample,int32_t q,unsigned clip,int factor)
{
    if(sample>=1024)return 0;
    return Scope144_CsvPoint(out,cal,sample,Scope144_TimePs(sample),q,clip,factor,(sample&1)?'A':'B');
}

unsigned Scope144_CsvRow(char out[80],const Scope144Cal *cal,unsigned sample,int32_t q,unsigned clip)
{ return Scope144_CsvScaled(out,cal,sample,q,clip,1); }
int Scope144_Cursors(const Scope144Cal *cal,const int32_t *samples,unsigned start,
    unsigned base,unsigned x1,unsigned x2,int32_t *dt,int32_t *dv)
{
    int32_t a,b; int64_t difference; uint32_t step=Scope144_Step(base);
    if(!Scope144_Valid(cal) || !step || start>=1024 || x1>386 || x2>386 || !dt || !dv ||
       !Scope144_At(samples,1024,((uint32_t)start<<16)+step*x1,&a) ||
       !Scope144_At(samples,1024,((uint32_t)start<<16)+step*x2,&b)) return 0;
    difference=(int64_t)Scope144_Voltage(cal,b)-Scope144_Voltage(cal,a);
    if(difference>INT32_MAX || difference<INT32_MIN)return 0;
    *dt=(int32_t)(((int64_t)x2-x1)*step*62500*1000000/((int64_t)9*65536*(1000000+clock_ppm)));
    *dv=(int32_t)difference; return 1;
}
int Scope144_Scale(const Scope144Measurements *in,int factor,Scope144Measurements *out)
{
    Scope144Measurements next; int64_t minimum,maximum,average,rms;
    if(!in || !out || (factor!=1 && factor!=-1 && factor!=10 && factor!=-10))return 0;
    next=*in;
    minimum=(int64_t)(factor<0?in->maximum_uv:in->minimum_uv)*factor;
    maximum=(int64_t)(factor<0?in->minimum_uv:in->maximum_uv)*factor;
    average=(int64_t)in->average_uv*factor; rms=(int64_t)in->rms_uv*(factor<0?-factor:factor);
    if(minimum<INT32_MIN || minimum>INT32_MAX || maximum<INT32_MIN || maximum>INT32_MAX ||
       maximum-minimum>INT32_MAX || average<INT32_MIN || average>INT32_MAX || rms>INT32_MAX) return 0;
    next.minimum_uv=(int32_t)minimum;next.maximum_uv=(int32_t)maximum;
    next.average_uv=(int32_t)average;next.rms_uv=(int32_t)rms;
    if(factor<0 && next.period_ps) {
        next.duty_ppm=1000000-next.duty_ppm;
        next.high_ps=in->low_ps;next.low_ps=in->high_ps;
    }
    *out=next;return 1;
}

unsigned Scope144_ClockValueLine(char out[40],int ppm)
{
    unsigned n=12; if(!out)return 0;
    memcpy(out,"# clock_ppm=",n);n=Decimal(out,n,ppm);
    out[n++]='\r';out[n++]='\n';out[n]=0;return n;
}
unsigned Scope144_ClockLine(char out[40]) { return Scope144_ClockValueLine(out,clock_ppm); }

/* Accept short presses that ended between frames, ignore a held-key repeat. */
void Scope144_RunKey(int down,int event,int *was_down,int *held)
{
    if(!was_down || !held)return;
    if(!*was_down && (down || event))*held=!*held;
    *was_down=down!=0;
}

const GeneratorCapability GeneratorCapabilities[9]={
    {11,16},{11,16},{11,16},{11,16},{7,22},{3,18},{1,16},{3,17},{3,16}
};
unsigned Generator_Detail(unsigned kind,unsigned detail)
{ return kind<9 && detail<4 && (GeneratorCapabilities[kind].details&(1u<<detail)); }
unsigned Generator_NextDetail(unsigned kind,unsigned detail)
{
    unsigned n;for(n=0;n<4;++n) {detail=(detail+1)%4;if(Generator_Detail(kind,detail))return detail;}
    return 0;
}

unsigned TriggerLevelSelect;
unsigned TriggerMenu_Next(unsigned detail,unsigned analog,unsigned holdoff)
{
    if(detail==2 && !TriggerLevelSelect && analog){TriggerLevelSelect=1;return 2;}
    TriggerLevelSelect=0;
    if(detail==0)return 1;
    if(detail==1 && analog)return 2;
    if(detail<3 && holdoff)return 3;
    return 0;
}
unsigned TriggerMenu_LevelStep(unsigned mode,int delta)
{ return (mode+4u+(delta<0?3u:1u))%4u; }
const char *TriggerMenu_LevelText(unsigned mode)
{
    static const char text[4][6]={"!Man!","!1/4!","!1/2!","!3/4!"};
    return text[mode<4?mode:0];
}
int32_t TriggerMenu_AutoLevel(int32_t low,int32_t high,unsigned mode)
{
    if(mode<1 || mode>3 || high<low)return low;
    return (int32_t)((int64_t)low+((int64_t)high-low)*mode/4);
}

uint32_t Scope144_TriggerMode(unsigned source,unsigned kind,unsigned invert)
{
    if((source!=0 && source!=2 && source!=3) || kind>8)return UINT32_MAX;
    if(kind==8)kind=3; /* Normal Update_Trig: GEN with sweep disabled. */
    if(source==0 && invert)kind^=kind<4?1u:2u;
    return (source<<3)+kind;
}
uint32_t Scope144_TriggerTicks(unsigned base,unsigned x1,unsigned x2)
{
    uint32_t step=Scope144_Step(base);
    unsigned delta=x1>x2?x1-x2:x2-x1;
    if(!step || x1>=400 || x2>=400)return 0;
    /* Two interlaced time samples per FPGA clock. Round only once, after
     * taking the absolute cursor separation; do not double the 72 MHz time. */
    return (uint32_t)(((uint64_t)delta*step+65536u)/131072u);
}

/* Native Current/Detail navigation, excluding separate analog B, XY, FFT
 * and legacy file formats while both ADCs acquire the single input A. */
unsigned Scope144_GroupStep(unsigned group,int delta)
{
    static const unsigned groups[]={0,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
    unsigned i;
    if(group==1)return delta<0?0:2;
    for(i=0;i<20;++i)if(groups[i]==group)return groups[(i+20+(delta<0?-1:1))%20];
    return 0;
}
unsigned Scope144_DetailLimit(unsigned group)
{
    if(group==8)return 2;
    if(group==10)return 3;
    if(group<=3 || group==5 || group==6 || group==7 || group==8 || group==9 || group==10)return 4;
    if(group==11 || group==12 || (group>=13 && group<=21))return 2;
    return 1;
}
unsigned Scope144_VisibleGroupStep(unsigned group,int delta,unsigned panel,unsigned page)
{
    do {
        group=Scope144_GroupStep(group,delta);
    } while((!panel && group>=11) ||
        (panel==2 && (group>=17 || (page==4?group==14:(group==11 || group==12 || group==13)))));
    return group;
}
int Scope144_Available(unsigned group,unsigned detail,unsigned kind)
{
    if(detail>=Scope144_DetailLimit(group))return 0;
    if(group==1 || group==4)return 0;
    if(group==2 || group==3)return detail==0 || detail==3;
    if(group==5)return Generator_Detail(kind,detail)!=0;
    if(group==6 && detail==3)return 0;
    if(group==7 && detail==3)return 0;

    return group<=21;
}
unsigned Scope144_Field(unsigned group,unsigned detail)
{
    static const unsigned a[]={25,26,0,6},gen[]={27,28,29,30};
    static const unsigned time[]={5,1,2,34},trig[]={31,4,3,34};
    static const unsigned ycursor[]={35,36,37,38},cursor[]={8,9,39,40},file[]={32,14,33,34};
    if(detail>=Scope144_DetailLimit(group))return 34;
    if(group==0)return a[detail];
    if(group==2 || group==3)return detail==0?46:detail==3?6:34;
    if(group==5)return gen[detail];
    if(group==6)return time[detail];
    if(group==7)return trig[detail];
    if(group==8)return ycursor[detail];
    if(group==10)return cursor[detail];
    if(group==9)return file[detail];
    if(group==11)return detail==1?42:43;
    if(group==12)return detail==1?44:45;
    if(group>=13 && group<=21)return detail==1?47:41;
    return 34;
}
