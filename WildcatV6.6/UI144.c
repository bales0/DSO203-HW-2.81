/* Interlace uses the ordinary Title/Meter coordinates, Print_Str font and
 * Draw_Window cursor renderer. Its numbers retain the 144 MHz time scale. */
#include "BIOS.h"
#include "Menu.h"
#include "Interrupt.h"
#include "Process.h"
#include "Function.h"
#include "Scope144.h"
#include "Draw.h"
#include <string.h>
extern char x10[8][6];
static Scope144Measurements shown;
static int32_t cursor_dt,cursor_dv;
static unsigned ready,frame_rate;
static unsigned repaint,refresh_values,refresh_slow;
static u16 last_second;static u8 last_phase,last_twink;
static char meter_text[9][12];static u8 meter_item[9],meter_track[9];
extern u8 FlagMeter,UpdateMeter;
static u16 Tone(unsigned group,unsigned detail)
{
    menu *e=&Title[group][detail];
    if(group!=RUNNING && !Scope144_Available(group,detail,(unsigned)_Kind))return 0x0905;
    return e->Color?e->Color[group==RUNNING?e->Value:group==TRIGG?Title[group][0].Value:0]:0x0405;
}
static void Paint(unsigned x,unsigned y,u16 color,unsigned selected,const char *text)
{ Print_Str((u16)x,(u16)y,color,selected?Twink:PRN,(char *)(text?text:"")); }
static const char *Word(unsigned group,unsigned detail)
{
    menu *e=&Title[group][detail];int v=e->Value;
    if(!e->Str)return "";
    if(e->MARK&FIX)return e->Str;
    if(v<0 || v>e->Limit)v=0;
    return e->Str+v*(group==FILE && detail==0?11:10);
}
static void Entry(unsigned group,unsigned detail,const char *override)
{
    menu *e=&Title[group][detail];
    unsigned selected=Current==group && Detail[group]==detail && Scope144_Available(group,detail,_Kind);
    Paint(e->XPOS,e->YPOS,Tone(group,detail),selected,override?override:Word(group,detail));
}
static void Number(char out[16],int32_t v)
{
    char digits[12];unsigned n=0,k=0;uint32_t magnitude;
    if(v<0){out[n++]='-';magnitude=0u-(uint32_t)v;}else magnitude=(uint32_t)v;
    do{digits[k++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);
    while(k)out[n++]=digits[--k];
    out[n]=0;
}
static void FooterNumber(const char *label,int32_t v,const char *unit,int enabled)
{
    char row[49],digits[16];Number(digits,v);
    strcpy(row,label);strcat(row,digits);strcat(row,unit);
    Paint(0,0,enabled?0x0405:0x0905,0,row);
}
static void PaintOptions(const char *const *labels,const unsigned *allowed,unsigned count,unsigned current)
{
    unsigned i,x=0;
    for(i=0;i<count;++i){Paint(x,0,allowed[i]?0x0405:0x0905,allowed[i] && i==current,labels[i]);x+=(strlen(labels[i])+1)*8;}
}
void Menu144_NativeTitle(void)
{
    unsigned g,d=Current<13?Detail[Current]:0;char number[16];
    static const char *sources[]={"","144 A ","144-A ","144xA ","144x- "};
    static const char *modes[]={"OFF","AUTO","NORM","NorCL","PERST","SGL","XY"};
    static const unsigned mode_ok[]={0,1,1,0,0,1,0};
    static const char *gen[]={"SIN","TRI","SAW","SQR","PWM","NOISE","OFF","ARB","UART"};
    static const unsigned gen_ok[]={1,1,1,1,1,0,1,0,0};
    static const char *couple[]={"DC","AC","DT","TL","RS","i2","SP","144D","144A"};
    static const unsigned couple_ok[]={1,1,1,1,1,1,1,1,1};
    static const char *trigger[]={"TR A","TR B","TR C","TR D","A&B"};
    static const unsigned trigger_ok[]={1,0,0,0,0};
    Entry(TRACK1,SOURCE,sources[_1_source>=1 && _1_source<=4?_1_source:1]);
    Entry(TRACK1,COUPLE,Title[TRACK1][COUPLE].Value==SCOPE144_AC?"AC":"DC");
    Entry(TRACK1,RANGE,_1_source>=3?x10[_A_Range]:0);
    /* Both ADCs sample A. Keep the other source controls visible, in gray. */
    for(g=TRACK2;g<=TRACK4;++g){Entry(g,SOURCE,Title[g][SOURCE].Str?Title[g][SOURCE].Str+10:"CH");}
    Entry(TRACK2,COUPLE,0);Entry(TRACK2,RANGE,0);
    Entry(RUNNING,STATE,0);Entry(RUNNING,STATUS,0);
    Entry(OUTPUT,KIND,0);Entry(OUTPUT,FRQN,0);Entry(T_BASE,MODE,0);Entry(T_BASE,BASE,0);
    Entry(TRIGG,SOURCE,0);Entry(TRIGG,TR_KIND,0);
    Int2Str(number,(int32_t)(_Vt1-_1_posi)*Y_Attr[_A_Range].SCALE*(_1_source>=3?10:1),V_UNIT,3,SIGN,0);
    Paint(Title[TRIGG][2].XPOS,Title[TRIGG][2].YPOS,Tone(TRIGG,2),Current==TRIGG && d==2,number);
    if(FlagMeter && (FlagMeter==1 || UpdateMeter==4)) {
        Entry(BK_LIGHT,0,0);Entry(VOLUME,0,0);
        Number(number,(Title[BK_LIGHT][1].Value+1)*10);strcat(number,"%");Entry(BK_LIGHT,1,number);
        Number(number,(Title[VOLUME][1].Value+1)*10);strcat(number,"%");Entry(VOLUME,1,number);
    }
    Paint(0,0,0x0505,0,"                                                 ");
    if(Current==TRACK1 && d==COUPLE)PaintOptions(couple,couple_ok,9,Title[0][COUPLE].Value);
    else if(Current==T_BASE && d==MODE)PaintOptions(modes,mode_ok,7,_Mode);
    else if(Current==OUTPUT && d==KIND)PaintOptions(gen,gen_ok,9,_Kind);
    else if(Current==TRIGG && d==SOURCE)PaintOptions(trigger,trigger_ok,5,0);
    else if(Current==TRACK2)Paint(0,0,0x0905,0,"CH(B)   SOURCE   COUPLING   RANGE   YPOS");
    else if(Current==TRACK3)Paint(0,0,0x0905,0,"CH(C)   SOURCE   YPOS");
    else if(Current==TRACK4)Paint(0,0,0x0905,0,"CH(D) A+B A-B C&D C|D REC SPEC FFT");
    else if(Current==FILE) {
        Entry(FILE,0,"SAVE");Number(number,Title[FILE][1].Value);Entry(FILE,1,number);Entry(FILE,2,"CSV");
        Paint(0,0,0x0905,0,"LOAD BMP BUF");Paint(270,0,0x0905,0,"DAT CFG DIR");
    } else if(Current==TRACK1 && d==SOURCE)Paint(0,0,0x0405,0,"A    -A    x10 A    -x10 A    144 MSa/s");
    else if(Current==TRACK1 && d==RANGE)Entry(TRACK1,RANGE,_1_source>=3?x10[_A_Range]:0);
    else if(Current==TRACK1)FooterNumber("YPOS ",_1_posi,"",1);
    else if(Current==OUTPUT && d==FRQN)Paint(0,0,Tone(OUTPUT,d),0,Word(OUTPUT,d));
    else if(Current==OUTPUT)FooterNumber(d==2?"DUTY (0.1%) ":"AMPLITUDE (%) ",d==2?(_Duty+15)/30:Title[OUTPUT][3].Value,"",Scope144_Available(Current,d,_Kind));
    else if(Current==T_BASE)Paint(0,0,Tone(Current,d),0,d==BASE?Word(T_BASE,BASE):d==XPOSI?"XPOS":"XY VIEW");
    else if(Current==TRIGG)Paint(0,0,Tone(Current,d),0,d==1?Word(TRIGG,1):d==2?number:"HOLDOFF");
    else if(Current==V_VERNIE){FooterNumber(d==0?"V1 ":d==1?"V2 ":"SOURCE A / B ",d<2?Title[V_VERNIE][d].Value:0,"",1);if(d>=2)Paint(112,0,0x0905,0,"B");}
    else if(Current==T_VERNIE)FooterNumber(d==0?"T1 ":d==1?"T2 ":"DELTA T ",d<2?Title[T_VERNIE][d].Value:cursor_dt/1000,d<2?" px":" ns",1);
    else if(Current==BK_LIGHT || Current==VOLUME)FooterNumber(Current==BK_LIGHT?"BACKLIGHT ":"VOLUME ",(Title[Current][1].Value+1)*10," %",1);
    else if(Current>=METER_0 && Current<=METER_8){Paint(0,0,0x0405,0,"SOURCE A");Paint(80,0,0x0905,0,"B C D");}
}
static void MeterValue(unsigned item,char value[16])
{
    int32_t v=0;const char *unit=V_UNIT;unsigned sign=SIGN;
    switch(item) {
    case VBT:v=__Get(V_BATTERY)*1000;break;
    case FPS:Number(value,frame_rate);return;
    case VDC:v=shown.average_uv;break;case RMS:v=shown.rms_uv;sign=UNSIGN;break;
    case MAX:v=shown.maximum_uv;break;case MIN:v=shown.minimum_uv;break;
    case VPP:v=shown.maximum_uv-shown.minimum_uv;sign=UNSIGN;break;
    case FRQ:v=shown.frequency_hz;unit=F_UNIT;sign=UNSIGN;break;
    case CIR:v=shown.period_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case TH:v=shown.high_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case TL:v=shown.low_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case DUT:v=shown.duty_ppm/10;unit=P_UNIT;sign=UNSIGN;break;
    default:strcpy(value,"--");return;
    }
    Int2Str(value,v,unit,3,sign,0);
}
static void Readout(unsigned group,const char *label,int32_t value,const char *unit)
{
    char digits[16];Int2Str(digits,value,unit,3,SIGN,0);
    Paint(Title[group][2].XPOS,Title[group][2].YPOS,Tone(group,2),0,label);
    Paint(Title[group][3].XPOS,Title[group][3].YPOS,Tone(group,3),0,digits);
}
void Menu144_Meters(const Scope144Measurements *unused)
{
    unsigned i,first=0,last=9;char value[16];(void)unused;
    if(!FlagMeter)return;
    if(FlagMeter==1 || UpdateMeter==4) {
        Readout(V_VERNIE,Title[V_VERNIE][2].Str,cursor_dv,V_UNIT);
        if(Title[T_VERNIE][2].Value && cursor_dt) {
            int64_t hz=1000000000000LL/(cursor_dt<0?-(int64_t)cursor_dt:cursor_dt);
            Readout(T_VERNIE,Title[T_VERNIE][2].Str+4,hz>2147483647?2147483647:(int32_t)hz,F_UNIT);
        } else Readout(T_VERNIE,Title[T_VERNIE][2].Str,cursor_dt/1000,T_UNIT);
    }
    if(FlagMeter==2){first=UpdateMeter==4?0:1;last=4;}
    for(i=first;i<last;++i) {
        meter *m=&Meter[i];unsigned enabled=m->Item<=FPS || (ready && m->Track==TRACK1);
        unsigned changed=meter_item[i]!=m->Item || meter_track[i]!=m->Track;
        unsigned small=FlagMeter==1 || (FlagMeter==2 && UpdateMeter==4 && i==0);
        if(FlagMeter==2 && UpdateMeter==4 && i==1)continue;
        u16 color=enabled?(m->Item<=FPS?0x0405:0x0005):0x0905;
        if((small?refresh_slow:refresh_values && (m->Item<DUT || refresh_slow)) || changed || repaint==2) {
            memset(value,0,sizeof(value));if(enabled)MeterValue(m->Item,value);else strcpy(value,"--");
            {unsigned length=(unsigned)strlen(value);if(length>11)length=11;
             memset(meter_text[i],0,12);memcpy(meter_text[i],value,length);}
            meter_item[i]=m->Item;meter_track[i]=m->Track;
        }
        if(small) {
            Paint(m->XPOS1,m->YPOS,enabled?(m->Item<=FPS?0x0504:0x0500):0x0905,Current==METER_0+i,m->Str+m->Item*5);
            Paint(m->XPOS2,m->YPOS,color,0,enabled?meter_text[i]:"--");
        } else if(enabled) {
            memset(NumStr,' ',11);NumStr[11]=0;memcpy(NumStr,meter_text[i],strlen(meter_text[i]));
            {u8 old_blink=BTwink,old_pas=EnablePaS;
             BTwink=Current==METER_0+i?Twink:PRN;EnablePaS=1;
             DisplayBigMeter((u8)i);BTwink=old_blink;EnablePaS=old_pas;}
        } else {
            unsigned y=154-(i-1)*57;
            Print_Str2(314,399,(u16)y,0x0905,PRN,"--   ",3,3);
            Print_Str2(313,347,(u16)(y-25),0x0905,PRN,"  ",2,0);
            Print_Str2(346,399,(u16)(y-25),0x0905,PRN,m->Str+m->Item*5,12,1);
        }
    }
}
void Menu144_View(const Scope144Measurements *m,int32_t dt,int32_t dv,unsigned valid,unsigned fps,unsigned refresh)
{
    unsigned phase=(Cnt_20mS%16)<8;
    repaint=refresh;refresh_slow=refresh==2 || Sec_Cnt!=last_second;
    refresh_values=refresh_slow || (phase && phase!=last_phase);
    shown=*m;cursor_dt=dt;cursor_dv=dv;ready=valid;frame_rate=fps;
    if(refresh || Twink!=last_twink)Display_Title();
    if(refresh || Twink!=last_twink || refresh_slow || (FlagMeter==2 && refresh_values))Display_Meter();
    last_second=Sec_Cnt;last_phase=(u8)phase;last_twink=Twink;
}
