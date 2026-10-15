#include "Scope144.h"
#include <string.h>
#include <limits.h>
static int clock_ppm;
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
        c->reference_uv>=1000 && c->reference_uv<=60000000 &&
        (int64_t)c->reference_uv*1024<=(int64_t)c->span_q10*800000;
}
int Scope144_CalibrateMeans(int32_t la,int32_t lb,int32_t ha,int32_t hb,int32_t reference,Scope144Cal *c)
{
    Scope144Cal next;int32_t da=ha-la,db=hb-lb;
    if(!c || la<10*1024 || lb<10*1024 || ha>245*1024 || hb>245*1024 || da<8*1024 || db<8*1024)return 0;
    next.gain_q10=(int32_t)((int64_t)da*1024/db);
    next.bias_q10=la-(int32_t)((int64_t)lb*next.gain_q10/1024);
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
int Scope144_Encode(const Scope144Bank *bank,uint32_t serial,uint8_t out[512])
{
    unsigned i; uint8_t *p;
    if(!bank || !out) return 0;
    for(i=0;i<8;++i) if(bank->range[i].valid && !Scope144_Valid(&bank->range[i])) return 0;
    memset(out,0,512); memcpy(out,"I144",4); Put(out+4,1); Put(out+8,serial);
    Put(out+12,SCOPE144_IMAGE_CRC); Put(out+16,bank->generation); Put(out+20,8);
    for(i=0;i<8;++i) {
        const Scope144Cal *c=&bank->range[i]; p=out+24+i*24;
        if(!c->valid) continue;
        Put(p,(uint32_t)c->gain_q10); Put(p+4,(uint32_t)c->bias_q10);
        Put(p+8,(uint32_t)c->zero_q10); Put(p+12,(uint32_t)c->span_q10);
        Put(p+16,(uint32_t)c->reference_uv); Put(p+20,1);
    }
    Put(out+508,Scope144_Crc(out,508)); return 1;
}
int Scope144_Decode(const uint8_t in[512],uint32_t serial,Scope144Bank *bank)
{
    unsigned i; Scope144Bank next;
    if(!in || !bank || memcmp(in,"I144",4) || Get(in+4)!=1 || Get(in+8)!=serial ||
       Get(in+12)!=SCOPE144_IMAGE_CRC || Get(in+20)!=8 || Get(in+508)!=Scope144_Crc(in,508)) return 0;
    memset(&next,0,sizeof(next)); next.generation=Get(in+16);
    for(i=0;i<8;++i) {
        const uint8_t *p=in+24+i*24; Scope144Cal *c=&next.range[i];
        c->gain_q10=(int32_t)Get(p); c->bias_q10=(int32_t)Get(p+4);
        c->zero_q10=(int32_t)Get(p+8); c->span_q10=(int32_t)Get(p+12);
        c->reference_uv=(int32_t)Get(p+16); c->valid=Get(p+20);
        if(c->valid>1 || (c->valid && !Scope144_Valid(c))) return 0;
        if(!c->valid && (c->gain_q10 || c->bias_q10 || c->zero_q10 || c->span_q10 || c->reference_uv)) return 0;
    }
    for(i=216;i<508;++i) if(in[i]) return 0;
    *bank=next; return 1;
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
int Scope144_Measure(const Scope144Cal *cal,const int32_t *q,unsigned count,unsigned clips,Scope144Measurements *out)
{
    Scope144Measurements m; unsigned i,edges=0; int64_t sum=0,center,d;
    uint64_t square=0,first=0,last=0,pos,high=0,rise=0; int32_t lo,hi,threshold;
    if(!Scope144_Valid(cal) || !q || !out || count<16 || count>1024) return 0;
    lo=hi=q[0];
    for(i=0;i<count;++i) {
        /* Acquisition corrections have a tighter bound than arbitrary int32 input. */
        if(q[i]<-800000 || q[i]>1400000) return 0;
        if(q[i]<lo)lo=q[i];
        if(q[i]>hi)hi=q[i];
        d=(int64_t)q[i]-cal->zero_q10; sum+=d; square+=(uint64_t)(d*d);
    }
    memset(&m,0,sizeof(m)); m.samples=count; m.clips=clips;
    m.minimum_uv=Scope144_Voltage(cal,lo); m.maximum_uv=Scope144_Voltage(cal,hi);
    center=sum/count; m.average_uv=(int32_t)(center*cal->reference_uv/cal->span_q10);
    m.rms_uv=(int32_t)((uint64_t)Sqrt(square/count)*cal->reference_uv/cal->span_q10);
    threshold=lo+(hi-lo)/2;
    for(i=0;i<count;++i) {
        if(i && q[i-1]<threshold && q[i]>=threshold && hi-lo>=10*1024) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-q[i-1])<<16)/(q[i]-q[i-1]);
            if(!edges)first=pos;
            last=pos; ++edges;
        }
    }
    if(edges>1 && !clips) {
        uint64_t period=(last-first)/(edges-1);
        m.frequency_hz=(uint32_t)(((uint64_t)(144u*(1000000+clock_ppm))<<16)/period);
        m.period_ps=(uint32_t)(period*62500*1000000/((uint64_t)9*65536*(1000000+clock_ppm)));
        /* Integrate high time over complete cycles only; exclude record ends. */
        rise=first;
        for(i=1;i<count;++i) {
            if(q[i-1]<threshold && q[i]>=threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-q[i-1])<<16)/(q[i]-q[i-1]);
                if(pos>=first && pos<=last)rise=pos;
            } else if(q[i-1]>=threshold && q[i]<threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(q[i-1]-threshold)<<16)/(q[i-1]-q[i]);
                if(pos>first && pos<=last && pos>=rise)high+=pos-rise;
            }
        }
        m.duty_ppm=(uint32_t)(high*1000000/(last-first));
        m.high_ps=(uint32_t)((uint64_t)m.period_ps*m.duty_ppm/1000000);
        m.low_ps=m.period_ps-m.high_ps;
    }
    *out=m; return 1;
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
unsigned Scope144_CsvScaled(char out[80],const Scope144Cal *cal,unsigned sample,int32_t q,unsigned clip,int factor)
{
    unsigned n=0; int64_t voltage;
    if(!out || !Scope144_Valid(cal) || sample>=1024 || clip>1 ||
       (factor!=1 && factor!=-1 && factor!=10 && factor!=-10)) return 0;
    voltage=(int64_t)Scope144_Voltage(cal,q)*factor;
    if(voltage>INT32_MAX || voltage<INT32_MIN) return 0;
    n=Decimal(out,n,(int32_t)sample); out[n++]=',';
    n=Decimal(out,n,(int32_t)Scope144_TimePs(sample)); out[n++]=',';
    n=Decimal(out,n,(int32_t)voltage); out[n++]=',';
    out[n++]=(sample&1)?'A':'B'; out[n++]=','; out[n++]=(char)('0'+clip);
    out[n++]='\r'; out[n++]='\n'; out[n]=0; return n;
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

unsigned Scope144_ClockLine(char out[40])
{
    unsigned n=12; if(!out)return 0;
    memcpy(out,"# clock_ppm=",n);n=Decimal(out,n,clock_ppm);
    out[n++]='\r';out[n++]='\n';out[n]=0;return n;
}

/* Accept short presses that ended between frames, ignore a held-key repeat. */
void Scope144_RunKey(int down,int event,int *was_down,int *held)
{
    if(!was_down || !held)return;
    if(!*was_down && (down || event))*held=!*held;
    *was_down=down!=0;
}

/* Native Current/Detail navigation, deliberately excluding B/C/D, XY, FFT
 * and legacy file formats while both ADCs acquire the single input A. */
int Scope144_CouplingStep(int value,int delta,int available)
{
    static const int order[]={0,1,2,SCOPE144_DC,SCOPE144_AC,3,4,5,6};
    unsigned i;
    if(!available) {
        if(value<0 || value>6)value=0;
        return (value+7+(delta<0?-1:1))%7;
    }
    for(i=0;i<9;++i)if(order[i]==value)return order[(i+9+(delta<0?-1:1))%9];
    return 0;
}
unsigned Scope144_GroupStep(unsigned group,int delta)
{
    static const unsigned groups[]={0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
    unsigned i;
    for(i=0;i<21;++i)if(groups[i]==group)return groups[(i+21+(delta<0?-1:1))%21];
    return 0;
}
unsigned Scope144_DetailLimit(unsigned group)
{
    if(group<=3 || group==5 || group==6 || group==7 || group==8 || group==9 || group==10)return 4;
    if(group==11 || group==12)return 2;
    return 1;
}
int Scope144_Available(unsigned group,unsigned detail,unsigned kind)
{
    if(detail>=Scope144_DetailLimit(group))return 0;
    if(group==1 || group==2 || group==3 || group==4)return 0;
    if(group==5 && detail==2)return kind==4;
    if(group==5 && detail==3)return kind<4;
    if(group==6 && detail>=2)return 0;
    if(group==7 && detail==3)return 0;
    if(group==9 && detail==3)return 0;
    return group<=21;
}
unsigned Scope144_Field(unsigned group,unsigned detail)
{
    static const unsigned a[]={25,26,0,6},gen[]={27,28,29,30};
    static const unsigned time[]={5,1,34,34},trig[]={31,4,3,34};
    static const unsigned ycursor[]={35,36,37,38},cursor[]={8,9,39,40},file[]={32,14,33,34};
    if(detail>=Scope144_DetailLimit(group))return 34;
    if(group==0)return a[detail];
    if(group==5)return gen[detail];
    if(group==6)return time[detail];
    if(group==7)return trig[detail];
    if(group==8)return ycursor[detail];
    if(group==10)return cursor[detail];
    if(group==9)return file[detail];
    if(group==11)return detail==1?42:43;
    if(group==12)return detail==1?44:45;
    if(group>=13 && group<=21)return 41;
    return 34;
}
