#if defined(__GNUC__)
#pragma GCC optimize ("Os")
#endif
#include "Scope144.h"
#include "UiCommon.h"
#include <string.h>
#include <limits.h>
static int clock_ppm;
static int32_t RoundRatio(int64_t n,int64_t d)
{ return (int32_t)(n>=0?(n+d/2)/d:-((-n+d/2)/d)); }
static int NativeCorrection(int correction)
{ return correction>=-128 && correction<=127?correction:0; }
static int NativeRaw(int offset,int correction,int ypos)
{ return offset+40+RoundRatio((int64_t)(1024+correction)*(ypos-40),1024); }
int CalNative_Command(int adc_offset,int correction,int ypos)
{
    int64_t d=(int64_t)(1024+NativeCorrection(correction))*((int64_t)ypos-40);
    int64_t command=(int64_t)(adc_offset>=0 && adc_offset<=54?adc_offset:0)+40+
        (d>=0?(d+512)/1024:-((-d+512)/1024));
    return command<0?0:command>200?200:(int)command;
}
static uint32_t native_windows[2][8],native_battery[2];
static int8_t native_correction[2][8][2];
void __attribute__((section(".text.runtime"))) Scope144_NativeBank(const Scope144Bank *bank)
{
    unsigned i;
    memset(native_windows,0,sizeof(native_windows));memset(native_battery,0,sizeof(native_battery));
    if(!bank)return;
    for(i=0;i<16;++i) {
        const Scope144Cal *c=i<8?&bank->range[i]:&bank->high[i-8];
        if(c->valid==1 && Scope144_Valid(c) && bank->battery[i/8]>=2800 && bank->battery[i/8]<=5200) {
            native_windows[i/8][i%8]=c->dac_window;
            native_correction[i/8][i%8][0]=(int8_t)(c->dac_anchor>>16);
            native_correction[i/8][i%8][1]=(int8_t)(c->dac_anchor>>24);
        }
    }
    native_battery[0]=bank->battery[0];native_battery[1]=bank->battery[1];
}
unsigned Scope144_NativeProfiles(unsigned range)
{ return range<8?((!!native_windows[0][range]) | (!!native_windows[1][range]<<1)):0; }
int CalNative_Interpolate(int low,int high,unsigned lo,unsigned hi,unsigned battery)
{
    if(hi<=lo || battery<=lo)return low;
    if(battery>=hi)return high;
    return low+RoundRatio((int64_t)(high-low)*(battery-lo),hi-lo);
}
int CalNative_Position(unsigned range,unsigned ch,int offset,int correction,int ypos,unsigned battery,unsigned *command)
{
    int lo=0,hi=200;
    unsigned a=0,b=1;int target,effective;
    if(range<8 && ch<2) {
        uint32_t w;
        if(native_battery[0]>native_battery[1]){a=1;b=0;}
        if(!native_windows[a][range])a=b;
        if(!native_windows[b][range])b=a;
        if(battery<=native_battery[a])b=a;
        else if(battery>=native_battery[b])a=b;
        w=native_windows[a][range];
        if(w && native_windows[b][range]) {
            int l2=(native_windows[b][range]>>(ch*16))&255u;
            correction=CalNative_Interpolate(native_correction[a][range][ch],native_correction[b][range][ch],native_battery[a],native_battery[b],battery);
            int h2=(native_windows[b][range]>>(ch*16+8))&255u;
            lo=(w>>(ch*16))&255u;hi=(w>>(ch*16+8))&255u;
            if(l2>lo)lo=l2;
            if(h2<hi)hi=h2;
        }
    }
    /* Reference points 25/100 are not hardware stops. Use unsaturated
     * arithmetic to avoid a hidden plateau at DAC 0/200. */
    if(offset<0 || offset>54)offset=0;
    correction=NativeCorrection(correction);
    if(lo>hi){lo=0;hi=200;}
    effective=ypos<10?10:ypos>190?190:ypos;
    target=NativeRaw(offset,correction,effective);
    if(target<lo || target>hi) {
        target=target<lo?lo:hi;
        effective=40+RoundRatio((int64_t)(target-offset-40)*1024,1024+correction);
        if(effective<10)effective=10;
        if(effective>190)effective=190;
        while(effective<190 && NativeRaw(offset,correction,effective)<lo)++effective;
        while(effective>10 && NativeRaw(offset,correction,effective)>hi)--effective;
    }
    if(command)*command=(unsigned)CalNative_Command(offset,correction,effective);
    return effective;
}
/* Both ADCs sample A. Intersect their attainable logical positions before
 * applying either command, including on the first key after a stop. */
int Scope144_Position(unsigned range,int offset,int ypos,unsigned battery,unsigned commands[2])
{
    int low=10,high=190,p;unsigned ch;
    if(range>=8 || offset<0 || offset>54)return -1;
    for(ch=0;ch<2;++ch) {
        p=CalNative_Position(range,ch,offset,0,10,battery,0);if(p>low)low=p;
        p=CalNative_Position(range,ch,offset,0,190,battery,0);if(p<high)high=p;
    }
    if(low>high) {
        p=ypos<10?10:ypos>190?190:ypos;
        if(p>200-offset)p=200-offset;
        if(commands)commands[0]=commands[1]=(unsigned)(offset+p);
        return p;
    }
    p=ypos<low?low:ypos>high?high:ypos;
    for(ch=0;ch<2;++ch)CalNative_Position(range,ch,offset,0,p,battery,commands?commands+ch:0);
    return p;
}

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
    return c && (c->valid==1 || c->valid==SCOPE144_NOMINAL) && c->gain_q10>=512 && c->gain_q10<=2048 &&
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
        ((!c->dac_window && !c->dac_anchor) || ((c->dac_window&255u)<((c->dac_window>>8)&255u) &&
         ((c->dac_window>>16)&255u)<(c->dac_window>>24) && ((c->dac_window>>8)&255u)<=200 &&
         (c->dac_window>>24)<=200 && (c->dac_anchor&255u)>=(c->dac_window&255u) &&
         (c->dac_anchor&255u)<=((c->dac_window>>8)&255u) && ((c->dac_anchor>>8)&255u)>=((c->dac_window>>16)&255u) &&
         ((c->dac_anchor>>8)&255u)<=(c->dac_window>>24))) &&
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
/* Explicit little-endian v4; 40-byte header + 16*60-byte records + padding + CRC. */
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
        int32_t fields[15];c=i<8?&bank->range[i]:&bank->high[i-8];p=out+40+i*SCOPE144_RECORD_BYTES;
        if(!c->valid)continue;
        fields[0]=c->gain_q10;fields[1]=c->bias_q10;fields[2]=c->zero_q10;fields[3]=c->span_q10;
        fields[4]=c->reference_uv;fields[5]=1;fields[6]=c->a_gain_q10;fields[7]=c->a_bias_q10;
        fields[8]=c->dac_a_q10;fields[9]=c->dac_b_q10;fields[10]=c->adc_offset;
        fields[11]=c->ref_dac_a_q10;fields[12]=c->ref_dac_b_q10;
        fields[13]=(int32_t)c->dac_anchor;fields[14]=(int32_t)c->dac_window;
        for(j=0;j<15;++j)Put(p+j*4,(uint32_t)fields[j]);
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
    unsigned i,stride;Scope144Cal c;
    if(!in || !bank || memcmp(in,"I144",4) || Get(in+4)!=SCOPE144_CAL_VERSION || Get(in+8)!=serial ||
        Get(in+12)!=SCOPE144_IMAGE_CRC || Get(in+20)!=SCOPE144_CAL_BYTES ||
        Get(in+1020)!=Scope144_Crc(in,1020))return 0;
    stride=SCOPE144_RECORD_BYTES;
    for(i=36;i<40;++i)if(in[i])return 0;
    for(i=40+16*stride;i<1020;++i)if(in[i])return 0;
    for(i=0;i<16;++i) {
        const uint8_t *p=in+40+i*stride;unsigned j;DecodeCal(p,&c);
        c.dac_anchor=Get(p+52);c.dac_window=Get(p+56);
        if(c.valid>1 || (c.valid && (!Scope144_Valid(&c) || Get(in+24+(i/8)*4)<2800 || Get(in+24+(i/8)*4)>5200)))return 0;
        if(!c.valid)for(j=0;j<stride;++j)if(p[j])return 0;
    }
    memset(bank,0,sizeof(*bank));bank->generation=Get(in+16);
    bank->battery[0]=Get(in+24);bank->battery[1]=Get(in+28);bank->native_crc=Get(in+32);
    for(i=0;i<16;++i) {
        Scope144Cal *out=i<8?&bank->range[i]:&bank->high[i-8];
        const uint8_t *p=in+40+i*stride;DecodeCal(p,out);
        out->dac_anchor=Get(p+52);out->dac_window=Get(p+56);
    }
    return 1;
}
unsigned Scope144_Command(int adc_offset)
{ return adc_offset>=0 && adc_offset<=54?(unsigned)adc_offset+SCOPE144_PIVOT:0; }
static int32_t Blend(int32_t a,int32_t b,uint32_t weight)
{ return a+(int32_t)(((int64_t)b-a)*weight/65536); }
static int Rebase(const Scope144Cal *c,int offset,int ypos,Scope144Cal *out,unsigned commands[2],const unsigned fixed[2])
{
    int delta=offset-c->adc_offset,db=delta;unsigned ch,anchors=0;*out=*c;
    for(ch=0;ch<2;++ch) {
        int command=offset+40,anchor=c->adc_offset+40;
        if(c->dac_window) {
            int k=(int8_t)(c->dac_anchor>>(16+ch*8));
            int64_t d=(int64_t)(1024+k)*(ypos-40);
            command+=RoundRatio(d,1024);anchor=(c->dac_anchor>>(ch*8))&255u;
        } else if(command<40 || command>94)return 0;
        if(fixed)command=(int)fixed[ch];
        if(!c->dac_window && (command<40 || command>94))return 0;
        anchors|=(unsigned)command<<(ch*8);
        if(c->dac_window && (command<(int)((c->dac_window>>(ch*16))&255u) || command>(int)((c->dac_window>>(ch*16+8))&255u)))return 0;
        if(commands)commands[ch]=(unsigned)command;
        if(ch)db=command-anchor;else delta=command-anchor;
    }
    if(c->dac_window)out->dac_anchor=(c->dac_anchor&0xffff0000u)|anchors;
    if(!delta && !db)return 1; /* Preserve the measured anchor exactly. */
    if(c->ref_dac_a_q10==c->dac_a_q10 && c->ref_dac_b_q10==c->dac_b_q10) {
        /* Preserve the previous rounding when the two responses coincide. */
        out->bias_q10+=(int32_t)((int64_t)delta*c->dac_a_q10-(int64_t)db*c->dac_b_q10*c->gain_q10/1024);
        out->a_bias_q10-=(int32_t)((int64_t)delta*c->dac_a_q10*c->a_gain_q10/1024);
    } else {
        int32_t za=(int32_t)(((int64_t)c->zero_q10-c->a_bias_q10)*1024/c->a_gain_q10);
        int32_t zb=(int32_t)(((int64_t)za-c->bias_q10)*1024/c->gain_q10);
        int32_t da=(int32_t)((int64_t)c->span_q10*1024/c->a_gain_q10);
        int32_t span_b=(int32_t)((int64_t)da*1024/c->gain_q10);
        /* Rebase BOTH measured endpoints. Their difference changes when
         * reference and ground have distinct offset slopes. */
        za+=delta*c->dac_a_q10;zb+=db*c->dac_b_q10;
        da+=delta*(c->ref_dac_a_q10-c->dac_a_q10);
        span_b+=db*(c->ref_dac_b_q10-c->dac_b_q10);
        if(da<8*1024 || span_b<8*1024 || da>235*1024 || span_b>235*1024)return 0;
        out->gain_q10=RoundRatio((int64_t)da*1024,span_b);
        out->bias_q10=za-RoundRatio((int64_t)zb*out->gain_q10,1024);
        out->a_gain_q10=RoundRatio((int64_t)c->span_q10*1024,da);
        out->a_bias_q10=c->zero_q10-RoundRatio((int64_t)za*out->a_gain_q10,1024);
    }
    out->adc_offset=offset;return Scope144_Valid(out);
}
static int Select(const Scope144Bank *bank,unsigned range,int offset,uint32_t battery,int ypos,Scope144Cal *out,unsigned commands[2])
{
    Scope144Cal a,b;uint32_t weight;int va,vb;unsigned ca[2],cb[2],fixed[2],ch;
    if(!bank || !out || range>=8 || !Scope144_Command(offset) || battery<2800 || battery>5200)return 0;
    /* Physical positioned selection requires measured windows. Pivot-only
     * legacy Select remains available, but cannot dilute a measured profile. */
    va=bank->range[range].valid==1 && (!commands || bank->range[range].dac_window) && Scope144_Valid(&bank->range[range]) && bank->battery[0]>=2800 && bank->battery[0]<=5200;
    vb=bank->high[range].valid==1 && (!commands || bank->high[range].dac_window) && Scope144_Valid(&bank->high[range]) && bank->battery[1]>=2800 && bank->battery[1]<=5200;
    if(!va && !vb)return 0;
    if(commands) {
        uint32_t lo=bank->battery[0],hi=bank->battery[1];
        weight=!va?65536:!vb || lo==hi?0:
            lo<hi?(battery<=lo?0:battery>=hi?65536:(battery-lo)*65536/(hi-lo)):
            (battery<=hi?65536:battery>=lo?0:(lo-battery)*65536/(lo-hi));
        for(ch=0;ch<2;++ch) {
            int ka=(int8_t)(bank->range[range].dac_anchor>>(16+ch*8));
            int kb=(int8_t)(bank->high[range].dac_anchor>>(16+ch*8));
            int k=!va?kb:!vb?ka:lo<=hi?CalNative_Interpolate(ka,kb,lo,hi,battery):CalNative_Interpolate(kb,ka,hi,lo,battery),command=offset+40;
            if((va && bank->range[range].dac_window) || (vb && bank->high[range].dac_window))
                command+=RoundRatio((int64_t)(1024+k)*(ypos-40),1024);
            if(command<0 || command>200)return 0;
            fixed[ch]=(unsigned)command;
        }
    }
    if(va)va=Rebase(&bank->range[range],offset,ypos,&a,ca,commands?fixed:0);
    if(vb)vb=Rebase(&bank->high[range],offset,ypos,&b,cb,commands?fixed:0);
    if(!va && !vb)return 0;
    if(commands){commands[0]=va?ca[0]:cb[0];commands[1]=va?ca[1]:cb[1];}
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
    if(out->dac_window) {
        int ka=(int8_t)(a.dac_anchor>>16)+RoundRatio((int64_t)((int8_t)(b.dac_anchor>>16)-(int8_t)(a.dac_anchor>>16))*weight,65536);
        int kb=(int8_t)(a.dac_anchor>>24)+RoundRatio((int64_t)((int8_t)(b.dac_anchor>>24)-(int8_t)(a.dac_anchor>>24))*weight,65536);
        out->dac_anchor=(out->dac_anchor&65535u)|((uint32_t)(uint8_t)ka<<16)|((uint32_t)(uint8_t)kb<<24);
    }
    out->dac_a_q10=Blend(a.dac_a_q10,b.dac_a_q10,weight);out->dac_b_q10=Blend(a.dac_b_q10,b.dac_b_q10,weight);
    out->ref_dac_a_q10=Blend(a.ref_dac_a_q10,b.ref_dac_a_q10,weight);
    out->ref_dac_b_q10=Blend(a.ref_dac_b_q10,b.ref_dac_b_q10,weight);
    return Scope144_Valid(out)?2:0;
}
int Scope144_Select(const Scope144Bank *bank,unsigned range,int offset,uint32_t battery,Scope144Cal *out)
{ return Select(bank,range,offset,battery,40,out,0); }
int Scope144_SelectAt(const Scope144Bank *bank,unsigned range,int offset,uint32_t battery,int ypos,Scope144Cal *out,unsigned commands[2])
{ return Select(bank,range,offset,battery,ypos,out,commands); }
/* The temporary runtime profile never enters the saved bank. Its validity
 * tag is accepted by arithmetic but explicitly rejected by Encode/Decode.
 * SCALE is microvolts per normalized ADC code, exactly as in Normal. */
int __attribute__((section(".text.runtime"))) Scope144_Runtime(const Scope144Bank *bank,unsigned range,int offset,unsigned battery,
    unsigned scale,int *ypos,Scope144Cal *out,unsigned commands[2],unsigned *measured)
{
    int p,status;
    if(measured)*measured=0;
    if(!ypos || !out || !commands || range>=8 || offset<0 || offset>54 ||
       battery<2800 || battery>5200 || !scale || scale>800000)return 0;
    p=Scope144_Position(range,offset,*ypos,battery,commands);
    if(p<0)return 0;
    *ypos=p;
    status=Scope144_SelectAt(bank,range,offset,battery,p,out,commands);
    if(status && out->dac_window) {if(measured)*measured=(unsigned)status;return 1;}
    /* A legacy record has no measured wide DAC interval. Keep it on disk,
     * without extrapolating its two-point response into unmeasured YPOS. */
    Scope144_Position(range,offset,p,battery,commands);
    memset(out,0,sizeof(*out));
    out->gain_q10=out->a_gain_q10=out->dac_a_q10=out->dac_b_q10=
        out->ref_dac_a_q10=out->ref_dac_b_q10=1024;
    out->bias_q10=((int)commands[0]-(int)commands[1])*1024;
    out->a_bias_q10=(40-(int)commands[0])*1024;
    out->zero_q10=40*1024;out->span_q10=75*1024;out->reference_uv=(int32_t)scale*75;
    out->adc_offset=offset;out->valid=SCOPE144_NOMINAL;
    return Scope144_Valid(out);
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
/* NULL collects raw clipping from the validated reader; the legacy API
 * supplies its explicit count unchanged. The requested timing pass is
 * independent of raw clipping. The existing extra region holds this core;
 * installation region addresses and limits remain unchanged. */
static int __attribute__((section(".text.scope144_measure"),optimize("O2"))) MeasureSource(const Scope144Cal *cal,const Scope144Source *source,const unsigned *known_clips,Scope144Measurements *out,unsigned timing)
{
    Scope144Measurements m; unsigned i,count,edges=0,rails=0,clips=known_clips?*known_clips:0; int status; int64_t sum=0,center,d;
    uint64_t square=0,first=0,last=0,pos,high=0,rise=0,pending_high=0; int32_t lo,hi,threshold,q,previous;
    if(!Scope144_Valid(cal) || !source || !source->read || !out || source->count<16 ||
        source->count>SCOPE144_MAX_SAMPLES || source->period_ps_q16>8000ULL*65536 ||
        (source->period_ps_q16 && source->period_ps_q16<6000ULL*65536)) return 0;
    count=source->count;
    status=source->read(source->user,0,&q);
    if(!status)return 0;
    lo=hi=previous=q;
    for(i=0;;) {
        /* Acquisition corrections have a tighter bound than arbitrary int32 input. */
        if(q<-4000000 || q>4000000) return 0;
        if(!known_clips)clips+=!!(status&2);
        rails|=((unsigned)status>>2)&3u;
        if(q<lo)lo=q;
        if(q>hi)hi=q;
        d=(int64_t)q-cal->zero_q10; sum+=d; square+=(uint64_t)(d*d);
        if(++i==count)break;
        status=source->read(source->user,i,&q);
        if(!status)return 0;
    }
    memset(&m,0,sizeof(m)); m.samples=count; m.clips=clips;m.clip_rails=rails;
    m.minimum_uv=Scope144_Voltage(cal,lo); m.maximum_uv=Scope144_Voltage(cal,hi);
    center=sum/count; m.average_uv=Scope144_Voltage(cal,(int32_t)center+cal->zero_q10);
    {uint64_t rms=(uint64_t)Sqrt(square/count)*cal->reference_uv/cal->span_q10;
     m.rms_uv=rms>INT32_MAX?INT32_MAX:(int32_t)rms;}
    if(!timing){*out=m;return 1;}
    threshold=lo+(hi-lo)/2;
    /* The source is immutable for this call; reuse its first sample. */
    for(i=1;i<count;++i) {
        if(!source->read(source->user,i,&q))return 0;
        if(previous<threshold && q>=threshold && hi-lo>=10*1024) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
            if(!edges)first=pos;
            else high+=pending_high;
            pending_high=0;rise=pos;
            last=pos; ++edges;
        } else if(previous>=threshold && q<threshold && edges) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(previous-threshold)<<16)/(previous-q);
            if(pos>=rise)pending_high+=pos-rise;
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
        /* Commit high time only when the next rising edge closes a cycle.
         * The pending tail after the last rise is excluded exactly as before. */
        m.duty_ppm=(uint32_t)(high*1000000/(last-first));
        m.high_ps=(uint32_t)((uint64_t)m.period_ps*m.duty_ppm/1000000);
        m.low_ps=m.period_ps-m.high_ps;
    }
    *out=m; return 1;
}
int Scope144_MeasureSource(const Scope144Cal *cal,const Scope144Source *source,unsigned clips,Scope144Measurements *out)
{
    return MeasureSource(cal,source,&clips,out,1);
}
int Scope144_MeasureSourceClipped(const Scope144Cal *cal,const Scope144Source *source,Scope144Measurements *out)
{
    return MeasureSource(cal,source,0,out,1);
}
int Scope144_MeasureSourceRequested(const Scope144Cal *cal,const Scope144Source *source,unsigned timing,Scope144Measurements *out)
{
    return MeasureSource(cal,source,0,out,timing);
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
    if(factor<0)next.clip_rails=((in->clip_rails&1u)<<1)|((in->clip_rails&2u)>>1);
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

/* A monotonic press sequence distinguishes a new click from the same
 * still-held key. Preserve two presses between frames and uint32 rollover. */
int Scope144_RunPresses(uint32_t published,uint32_t *seen,int *held)
{
    uint32_t pending;
    if(!seen || !held)return 0;
    pending=published-*seen;*seen=published;
    if(pending&1u)*held=!*held;
    return pending!=0;
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

int Scope144_IsFft(unsigned source){return source==14 || source==15;}
unsigned Scope144_DSourceStep(unsigned source,int delta)
{
    static const unsigned choices[4]={0,1,14,15};unsigned i;
    for(i=0;i<4;++i)if(choices[i]==source)return choices[(i+(delta<0?3:1))%4];
    return 0;
}
/* Native Current/Detail navigation, excluding separate analog B, XY, FFT_B
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
    if(group==6 || group==10)return 3;
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
    if(group==2)return detail==0 || detail==3;
    if(group==3)return detail==0 || detail==2 || detail==3;
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
    if(group==2 || group==3)return detail==0?46:detail==3?6:group==3 && detail==2?48:34;
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
