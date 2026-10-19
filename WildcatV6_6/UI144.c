/* Interlace uses the ordinary Title/Meter coordinates, Print_Str font and
 * Draw_Window cursor renderer. Its numbers retain the 144 MHz time scale. */
#include "BIOS.h"
#include "Menu.h"
#include "Interrupt.h"
#include "Process.h"
#include "Function.h"
#include "Scope144.h"
#include "Draw.h"
#include "UiCommon.h"
#include <string.h>
extern u8 TrgAuto;
extern char x10[8][6];
static Scope144Measurements shown;
static int32_t cursor_dt,cursor_dv;
static unsigned ready,calibration_status=2;
static unsigned timing_unavailable;
static unsigned trigger_waiting,wait_visible,presentation_available,frozen_frame,last_run_state;
static u16 wait_started;
static u16 Tick(void){return (u16)(Sec_Cnt*51u+Cnt_20mS);}
void Menu144_TriggerWait(unsigned stage)
{
    if(stage==1 && trigger_waiting!=1)wait_started=Tick();
    trigger_waiting=stage;
}
void Menu144_Presentation(unsigned available){presentation_available=available;}
void Menu144_MeasurementTime(unsigned available)
{
    unsigned i;
    if(timing_unavailable==(unsigned)!available)return;
    timing_unavailable=!available;
    for(i=0;i<9;++i)if(Meter[i].Track==TRACK1 && UiCommon_TimeItem(Meter[i].Item))Meter[i].Flag|=UPDAT;
}
void Menu144_Calibration(unsigned status){calibration_status=status;}
static unsigned repaint,refresh_values,refresh_slow;
static unsigned header_detail_shown,fft_gain_shown;
static u16 last_second;static u8 last_phase,last_twink;
static char meter_text[9][12];static u8 meter_item[9],meter_track[9],meter_enabled[9];
extern u8 FlagMeter,UpdateMeter;
static unsigned Available(unsigned group,unsigned detail)
{
    if(group==TRACK4 && detail==RANGE)return Scope144_IsFft((unsigned)_4_source);
    if(group==TRACK4 && detail==POSI && Scope144_IsFft((unsigned)_4_source))return 0;
    return Scope144_Available(group,detail,(unsigned)_Kind);
}
static u16 Tone(unsigned group,unsigned detail)
{
    menu *e=&Title[group][detail];
    if(group==TRIGG && detail==2 && _Tr_source>=TRACK3)return 0x0905;
    if(group!=RUNNING && !Available(group,detail))return 0x0905;
    if(group==OUTPUT)return detail==FRQN?Generator_ValueColor():Generator_Color();
    return e->Color?e->Color[group==RUNNING?e->Value:group==TRIGG?Title[group][0].Value:0]:0x0405;
}
static void Paint(unsigned x,unsigned y,u16 color,unsigned selected,const char *text)
{ Print_Str((u16)x,(u16)y,color,selected?Twink:PRN,(char *)(text?text:"")); }
/* A rail warning temporarily occupies the existing 48-pixel source label;
 * it never overlaps the plot, menus, FFT or buffer overview. +/- identify
 * the channel-oriented high/low rails; * means that both ADC rails were reached. */
char *Menu_ChannelSource(unsigned channel)
{
    static const char rail[2][3][7]={{"ACLIP-","ACLIP+","ACLIP*"},{"BCLIP-","BCLIP+","BCLIP*"}};
    unsigned bits=Process_NativeClip(channel);
    if(!channel && Wildcat144_Active())bits=ready?shown.clip_rails:0;
    return bits?(char *)rail[channel][bits-1]:Title[channel][SOURCE].Str+Title[channel][SOURCE].Value*10;
}
void Menu_TriggerLevel(unsigned force)
{
    static unsigned previous=~0u;
    unsigned analog=_Tr_source<TRACK3 || _Tr_source==4;
    unsigned focus=Current==TRIGG && Detail[TRIGG]==2?1u+!!TriggerLevelSelect:0;
    unsigned state=(unsigned)_Tr_source+8u*TrgAuto+32u*focus+128u*Twink;
    u16 color=analog?Title[TRIGG][2].Color[_Tr_source]:0x0905;
    if(!force && state==previous)return;
    Paint(333,216,color,analog && focus==1,"LEV!");
    ClearScreenArea(362,365,216,227);
    Paint(365,216,color,analog && focus==2,analog?TriggerMenu_LevelText(TrgAuto):"!---!");
    ClearScreenArea(398,400,216,227);
    previous=state;
}
/* Rewrite the whole right-hand field in one text pass. Shorter values and
 * disabled '--' must not leave the tail of a previous Normal measurement. */
static void Field(unsigned x,unsigned y,u16 color,unsigned selected,const char *text)
{
    char padded[16];unsigned n=(399u-x)/8u,length=(unsigned)strlen(text);
    if(n>15)n=15;
    if(length>n)length=n;
    memset(padded,' ',n);memcpy(padded,text,length);padded[n]=0;
    Paint(x,y,color,selected,padded);
    ClearScreenArea((u16)(x+1u+8u*n),400,(u8)y,(u8)(y+11u));
}
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
    unsigned selected=Current==group && Detail[group]==detail && Available(group,detail);
    if(!(e->Flag&UPDAT) && Current!=group && repaint!=2)return;
    e->Flag&=~UPDAT;
    if(!override && detail==1 && (group==BK_LIGHT || group==VOLUME))
        UiCommon_PercentTitle(group,selected?Twink:PRN);
    else if(!override && (e->MARK&FIX) && group!=TRIGG && group!=RUNNING &&
        Available(group,detail))
        UiCommon_FixedTitle(group,detail,selected?Twink:PRN);
    else Paint(e->XPOS,e->YPOS,Tone(group,detail),selected,override?override:Word(group,detail));
}
static void RunStatus(void)
{
    char cell[5];const char *text=!calibration_status?"CAL?":wait_visible?"WAIT":_State.Value==RUN?"RUN":"HOLD";
    unsigned n=(unsigned)strlen(text);if(n>4)n=4;
    memset(cell,' ',4);memcpy(cell,text,n);cell[4]=0;
    unsigned state=_State.Value==RUN?RUN:(frozen_frame?2u:HOLD);
    Paint(0,228,Title[RUNNING][STATE].Color[state],0,cell);
}
/* One eight-character cell between original U/0 and generator frequency.
 * Clear its full width when changing context, never the buffer waveform. */
static void HeaderDetail(u16 color,unsigned selected,const char *text)
{
    UiCommon_HeaderText(color,selected,text);header_detail_shown=1;
}
void Menu144_NativeTitle(void)
{
    unsigned g,gen_dirty=Current==OUTPUT || repaint==2;
    for(g=0;g<4;++g)gen_dirty|=Title[OUTPUT][g].Flag&UPDAT;
    Entry(TRACK1,SOURCE,Menu_ChannelSource(TRACK1));
    Entry(TRACK1,COUPLE,Title[TRACK1][COUPLE].Value==SCOPE144_AC?"AC":"DC");
    Entry(TRACK1,RANGE,_1_source>=3?x10[_A_Range]:0);
    /* Both ADCs sample A; C/D remain separate 72 MHz digital inputs. */
    Entry(TRACK2,SOURCE,Title[TRACK2][SOURCE].Str?Title[TRACK2][SOURCE].Str+10:"CH");
    for(g=TRACK3;g<=TRACK4;++g)Entry(g,SOURCE,0);
    Entry(TRACK2,COUPLE,0);Entry(TRACK2,RANGE,0);
    RunStatus();
    if(gen_dirty){Generator_KindTitle();Generator_FrequencyTitle();
        for(g=0;g<4;++g)Title[OUTPUT][g].Flag&=~UPDAT;}
    Entry(T_BASE,MODE,0);Entry(T_BASE,BASE,0);
    Entry(TRIGG,SOURCE,0);Entry(TRIGG,TR_KIND,0);
    Menu_TriggerLevel(1);
    /* Voltage/duty remain visible when leaving GEN. File results use the
     * native bottom row; pulse time can use this cell without voltage/duty.
     * Manual trigger voltage uses the native Tr: graph readout instead. */
    if(Current==OUTPUT || _Kind<=PWM) {
        Generator_Draw();header_detail_shown=1;
    } else if(_Tr_kind>=4 && _Tr_kind<=7) {
        char interval[16],cell[9];unsigned length;
        uint32_t ticks=Scope144_TriggerTicks(21-_T_base,(unsigned)_T1,(unsigned)_T2);
        int32_t ns=(int32_t)((Scope144_TimePs(2u*ticks)+500u)/1000u);
        Int2Str(interval,ns,T_UNIT,3,UNSIGN,0);
        memset(cell,' ',8);cell[0]='T';length=(unsigned)strlen(interval);
        if(length>7)length=7;
        memcpy(cell+1,interval,length);cell[8]=0;
        HeaderDetail(Tone(TRIGG,TR_KIND),0,cell);
    } else if(header_detail_shown || repaint==2) {
        Paint(172,216,0x0505,0,"        ");header_detail_shown=0;
    }
    if(FlagMeter && (FlagMeter==1 || UpdateMeter==4)) {
        Entry(BK_LIGHT,0,0);Entry(VOLUME,0,0);
        Entry(BK_LIGHT,1,0);Entry(VOLUME,1,0);
    }
    /* Preserve the native cursor controls on both sides of the buffer graphic. */
    if(fft_gain_shown && !(Current==TRACK4 && Scope144_IsFft((unsigned)_4_source))) {
        Paint(0,0,0x0505,0,"    ");Title[TRACK1][POSI].Flag|=UPDAT;fft_gain_shown=0;
    }
    Entry(TRACK1,POSI,"YPOS");
    if(Current==TRACK3 || (Current==TRACK4 && !Scope144_IsFft((unsigned)_4_source)))Entry(Current,POSI,"YPOS");
    if(Current==TRACK4 && Scope144_IsFft((unsigned)_4_source)) {
        static const char gain[10][5]={"LOG ","AUTO"," 0dB"," 6dB","12dB","18dB","24dB","30dB","36dB","42dB"};
        unsigned setting=Title[TRACK4][RANGE].Value;
        Paint(0,0,Tone(TRACK4,RANGE),Detail[TRACK4]==RANGE,gain[setting<10?setting:0]);fft_gain_shown=1;
    }
    Entry(V_VERNIE,V1,0);Entry(V_VERNIE,V2,0);
    Entry(T_VERNIE,T1,0);Entry(T_VERNIE,T2,0);
    Entry(T_BASE,XPOSI,0);
    if(UiCommon_ServiceTitle())return;
    if(Current!=FILE) {
        if(repaint)Update_Buffer_Overview();
        return;
    }
    UiFile_Draw();
}

void Menu144_Cursor(unsigned base)
{
    unsigned d=Current<13?Detail[Current]:0;int64_t value;int32_t ps;const char *unit=V_UNIT;
    (void)base; /* The displayed frame supplies its own period and origin. */
    EnableCursorDisplay=0;CursorDisplayTimer=0;
    if(((Current==T_VERNIE && d<2) || (Current==T_BASE && d==XPOSI)) &&
        !(DisableCursorDisplay&1)) {
        /* Normal displays the selected T1/T2 time while panning XPOS too.
         * Use the completed frame's actual origin/trigger, never the request. */
        unsigned selected=Detail[T_VERNIE]?T2:T1;
        CursorDisplaySelect=selected?2:0;unit=T_UNIT;
        if(!Wildcat144_CursorTime((unsigned)Title[T_VERNIE][selected].Value,&ps))return;
        value=ps/1000;
    } else if(Current==TRIGG && d==2 && TrgAuto==0 && _Tr_source==TRACK1 && !(DisableCursorDisplay&2)) {
        CursorDisplaySelect=8;
        value=(int64_t)(_Vt1-_1_posi)*Y_Attr[_A_Range].SCALE*(_1_source>=3?10:1);
    } else if(Current==V_VERNIE && d<2 && !(DisableCursorDisplay&2)) {
        CursorDisplaySelect=d?6:4;
        value=(int64_t)(Title[V_VERNIE][d].Value-_1_posi)*Y_Attr[_A_Range].SCALE*(_1_source>=3?10:1);
    } else return;
    if(value>2147483647)value=2147483647;
    if(value<(-2147483647-1))value=(-2147483647-1);
    MinBypass=0;EnableCursorDisplay=1;CursorDisplayTimer=150;
    Int2Str(CursorDisplayStr,(int32_t)value,unit,3,SIGN,0);
}
static void MeterValue(unsigned item,const Scope144Measurements *measurement,char value[16])
{
    int32_t v=0;const char *unit=V_UNIT;unsigned sign=SIGN;
    switch(item) {
    case VBT:v=__Get(V_BATTERY)*1000;break;
    case VDC:v=measurement->average_uv;break;case RMS:v=measurement->rms_uv;break;
    case MAX:v=measurement->maximum_uv;break;case MIN:v=measurement->minimum_uv;break;
    case VPP:v=measurement->maximum_uv-measurement->minimum_uv;break;
    case FRQ:v=measurement->frequency_hz;unit=F_UNIT;sign=UNSIGN;break;
    case CIR:v=measurement->period_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case TH:v=measurement->high_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case TL:v=measurement->low_ps/1000;unit=T_UNIT;sign=UNSIGN;break;
    case DUT:v=measurement->duty_ppm/10;unit=P_UNIT;sign=UNSIGN;break;
    default:strcpy(value,"--");return;
    }
    /* Native big meters require the same fixed positions as Normal:
     * signed three-digit voltage, four-digit frequency/time/duty. */
    Int2Str(value,v,unit,sign==SIGN?3:4,sign,0);
}
static void Readout(unsigned group,const char *label,int32_t value,const char *unit)
{
    char digits[16];Int2Str(digits,value,unit,3,SIGN,0);
    Paint(Title[group][2].XPOS,Title[group][2].YPOS,Title[group][2].Color[0],0,label);
    Paint(Title[group][3].XPOS,Title[group][3].YPOS,Title[group][3].Color[0],0,digits);
}
void Menu144_Meters(const Scope144Measurements *unused)
{
    unsigned i,mask=UiCommon_MeterMask();char value[16];(void)unused;
    if(!FlagMeter)return;
    if(FlagMeter==1 || UpdateMeter==4) {
        Readout(V_VERNIE,Title[V_VERNIE][2].Str,cursor_dv,V_UNIT);
        if(Title[T_VERNIE][2].Value && cursor_dt) {
            int64_t hz=1000000000000LL/(cursor_dt<0?-(int64_t)cursor_dt:cursor_dt);
            Readout(T_VERNIE,Title[T_VERNIE][2].Str+4,hz>2147483647?2147483647:(int32_t)hz,F_UNIT);
        } else Readout(T_VERNIE,Title[T_VERNIE][2].Str,cursor_dt/1000,T_UNIT);
    }
    for(i=0;mask;++i,mask>>=1)if(mask&1u) {
        Scope144Measurements digital;const Scope144Measurements *measurement=&shown;
        meter *m=&Meter[i];unsigned enabled=m->Item==VBT || (ready && m->Track==TRACK1 && (!timing_unavailable || !UiCommon_TimeItem(m->Item)));
        if(ready && (m->Track==TRACK3 || m->Track==TRACK4) &&
            Title[m->Track][SOURCE].Value==1 &&
            (m->Item==FRQ || m->Item==CIR || m->Item==DUT || m->Item==TH || m->Item==TL) &&
            Wildcat144_DigitalMeasurement(m->Track-TRACK3,&digital)) {enabled=1;measurement=&digital;}
        unsigned changed=(m->Flag&UPDAT) || meter_item[i]!=m->Item || meter_track[i]!=m->Track || meter_enabled[i]!=enabled;
        m->Flag&=~UPDAT;
        unsigned small=FlagMeter==1 || i==0;
        u16 color=enabled?(m->Item==VBT?0x0405:(m->Track<<8)|5):0x0905;
        if((small?refresh_slow:refresh_values && (m->Item<DUT || refresh_slow)) || changed || repaint==2) {
            memset(value,0,sizeof(value));if(enabled)MeterValue(m->Item,measurement,value);else strcpy(value,"--");
            {unsigned length=(unsigned)strlen(value);if(length>11)length=11;
             memset(meter_text[i],0,12);memcpy(meter_text[i],value,length);}
            meter_item[i]=m->Item;meter_track[i]=m->Track;meter_enabled[i]=(u8)enabled;
        }
        if(small) {
            Paint(m->XPOS1,m->YPOS,enabled?(m->Item==VBT?0x0504:0x0500|m->Track):0x0905,Current==METER_0+i,UiCommon_MeterLabel(m->Item));
            Field(m->XPOS2,m->YPOS,color,0,enabled?meter_text[i]:"--");
        } else if(enabled) {
            memset(NumStr,' ',11);NumStr[11]=0;memcpy(NumStr,meter_text[i],strlen(meter_text[i]));
            {u8 old_blink=BTwink,old_pas=EnablePaS;
             BTwink=Current==METER_0+i?Twink:PRN;EnablePaS=1;
             DisplayBigMeter((u8)i);BTwink=old_blink;EnablePaS=old_pas;}
        } else {
            unsigned y=154-(i-1)*57;char label[5]={0};
            if(changed || repaint==2)ClearScreenArea(313,400,(u8)(y-25),(u8)(y+22));
            memcpy(label,UiCommon_MeterLabel(m->Item),m->Item>=TH && m->Item<=TL?4u:3u);
            Print_Str2(314,399,(u16)y,0x0905,PRN,"--   ",3,3);
            Print_Str2(313,347,(u16)(y-25),0x0905,PRN,"  ",2,0);
            Print_Str2(346,399,(u16)(y-25),0x0905,PRN,label,12,1);
        }
    }
}
void Menu144_View(const Scope144Measurements *m,int32_t dt,int32_t dv,unsigned valid,unsigned refresh)
{
    unsigned phase=(Cnt_20mS%16)<8;
    unsigned i,mask=UiCommon_MeterMask();
    unsigned meter_changed=ready!=(valid || presentation_available);
    unsigned waiting=trigger_waiting==1 && (u16)(Tick()-wait_started)>=10u;
    unsigned wait_changed=waiting!=wait_visible;
    unsigned run_changed=last_run_state!=(unsigned)_State.Value || frozen_frame!=(valid && _State.Value==HOLD);
    last_run_state=(unsigned)_State.Value;frozen_frame=valid && _State.Value==HOLD;
    wait_visible=waiting;
    repaint=refresh;refresh_slow=refresh==2 || Sec_Cnt!=last_second;
    refresh_values=refresh_slow || (phase && phase!=last_phase);
    /* Timer gates apply to changing values, never to a different field
     * definition or newly available frame. Consume dirty metadata only when
     * its visible field is actually painted. */
    for(i=0;mask;++i,mask>>=1)if(mask&1u) {
        if((Meter[i].Flag&UPDAT) || meter_item[i]!=Meter[i].Item || meter_track[i]!=Meter[i].Track)
            meter_changed=1;
    }
    if((ready?shown.clip_rails:0)!=((valid || presentation_available)?m->clip_rails:0))
        Title[TRACK1][SOURCE].Flag|=UPDAT;
    shown=*m;cursor_dt=dt;cursor_dv=dv;ready=valid || presentation_available;
    if(refresh || Twink!=last_twink || (Title[TRACK1][SOURCE].Flag&UPDAT))Display_Title();
    else if(wait_changed || run_changed)RunStatus();
    /* New captures can move the window even without a menu refresh. Update
     * only the graphic, preserving Title blink and non-waveform footers. */
    Update_Buffer_Overview();
    if(refresh || Twink!=last_twink || refresh_slow)Update_Battery();
    if(refresh || Twink!=last_twink || refresh_slow || meter_changed || (FlagMeter==2 && refresh_values))Display_Meter();
    last_second=Sec_Cnt;last_phase=(u8)phase;last_twink=Twink;
}
