#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Interrupt.h"
#include "Draw.h"
#include "Function.h"
#include "File.h"
#include "Scope144.h"
#include "Calibrat.h"
#include "UiCommon.h"
#include <string.h>

/* Return 1 consumed, 2 changed ADC command, 3 changed clock correction.
 * Both acquisition owners perform their own safe hardware reconciliation. */
int __attribute__((optimize("Os"))) UiCommon_ServiceKey(unsigned key)
{
    int delta;
    if(!OffsetSelect || Current<=TRACK2 || Current==FILE)return 0;
    if(key==K_INDEX_S){OffsetSelect^=3u;return 1;}
    if(key!=K_INDEX_INC && key!=K_INDEX_DEC)return 0;
    delta=key==K_INDEX_INC?1:-1;
    if(OffsetSelect==1) {
        int next=ADCoffset+delta;
        if(next<0 || next>54)return 1;
        ADCoffset=(u8)next;return 2;
    }
    if(PPM_Comp+delta < -999 || PPM_Comp+delta > 999)return 1;
    PPM_Comp+=(s16)delta;return 3;
}
int __attribute__((optimize("Os"))) UiCommon_ServiceTitle(void)
{
    char cell[21];unsigned n,value;
    if(!OffsetSelect || Current<=TRACK2 || Current==FILE)return 0;
    memset(cell,' ',20);cell[20]=0;
    if(OffsetSelect==1) {
        memcpy(cell,"ADCoffset= ",11);value=ADCoffset;
        cell[11]=(char)('0'+value/10);cell[12]=(char)('0'+value%10);
    } else {
        memcpy(cell,"Clock PPM Adj=",14);n=PPM_Comp<0;
        value=n?(unsigned)-PPM_Comp:(unsigned)PPM_Comp;
        cell[14]=n?'-':'+';cell[15]=(char)('0'+value/100);
        cell[16]=(char)('0'+value/10%10);cell[17]=(char)('0'+value%10);
    }
    Print_Str(127,0,0x0405,PRN,cell);return 1;
}

/* Only runtime input writes use this cache. Calibration keeps its direct
 * BIOS writes and invalidates before entering that hardware ownership. */
static u16 analog_values[6];
static u8 analog_valid;
void UiCommon_AnalogReset(void) { analog_valid=0; }
void UiCommon_AnalogSet(unsigned char object,uint32_t value)
{
    unsigned slot,bit;
    if(object<=CH_B_OFFSET)slot=object;
    else if(object>=CH_A_COUPLE && object<=CH_B_RANGE)slot=object-CH_A_COUPLE+2u;
    else {UiCommon_AnalogReset();__Set(object,value);return;}
    /* Packed offset commands also change the shared TIM5 period. Never
     * cache those; the next ordinary writes must restore both channels. */
    if(value>=65536u){UiCommon_AnalogReset();__Set(object,value);return;}
    bit=1u<<slot;
    if((analog_valid&bit) && analog_values[slot]==value)return;
    analog_values[slot]=(u16)value;analog_valid|=(u8)bit;
    __Set(object,value);
}

/* Pure repeat policy. Normal's interpolated visible region uses reference
 * pixels (4); outside it, the caller retains native GetXpos conversions.
 * No key timing or capture state is owned here. Bounds belong to the caller. */
int UiCommon_XposStep(int reference,int direction,unsigned accelerated,unsigned middle,
    unsigned *reference_units)
{
    unsigned amount=1;
    if(reference_units)*reference_units=0;
    if(!direction)return 0;
    if(accelerated) {
        if(reference>150)amount=middle?6:30;
        else if(direction<0?reference>30:reference>=30) {
            amount=4;if(reference_units)*reference_units=1;
        }
    }
    return direction<0?-(int)amount:(int)amount;
}

/* Shared approach to the right stop, expressed in caller units. */
int UiCommon_XposApproach(int step,int64_t room,int64_t quantum)
{
    if(step>1 && quantum>0 && room<=30*quantum)step=room>6*quantum?6:1;
    return step;
}
/* Clamp before and after an edit: inherited clipping must not turn the
 * first reverse key into a repayment of invisible movement. */
int64_t UiCommon_XposBound(int64_t current,int64_t delta,int64_t minimum,int64_t maximum)
{
    int64_t next;
    if(minimum>maximum)return current;
    if(current<minimum)current=minimum;
    if(current>maximum)current=maximum;
    if(delta>0 && current>INT64_MAX-delta)return maximum;
    if(delta<0 && current<INT64_MIN-delta)return minimum;
    next=current+delta;
    if(next<minimum)return minimum;
    if(next>maximum)return maximum;
    return next;
}
/* Normal's XposRef is a display-pixel reference, but its ordinary value
 * steps are timer periods, not pixels. 144 retains their physical time in
 * Q16: one 72MHz period equals two interlace samples (four for ARR=1). */
int64_t UiCommon_Xpos144Delta(int64_t offset,unsigned base,unsigned psc,unsigned arr,
    int64_t maximum,int direction,unsigned accelerated,unsigned middle)
{
    int64_t pixel=Scope144_Step(base),reference,remainder,quantum,room;
    unsigned reference_units;int step;
    if(!pixel || psc>65535 || arr>65535)return 0;
    reference=offset/pixel;remainder=offset%pixel;
    if(remainder>=(pixel+1)/2)++reference;
    else if(remainder<=-(pixel+1)/2)--reference;
    if(reference>2147483647LL)reference=2147483647LL;
    if(reference<(-2147483647LL-1))reference=(-2147483647LL-1);
    step=UiCommon_XposStep((int)reference,direction,accelerated,middle,&reference_units);
    if(reference_units)return (int64_t)step*pixel;
    quantum=2LL*65536*(psc+1u)*(arr+1u);
    room=maximum-offset;
    step=UiCommon_XposApproach(step,room,quantum);
    return (int64_t)step*quantum;
}

/* KIND, frequency and (where meaningful) duty or amplitude. */
static unsigned output_dirty,ui_status,directory_count,file_changed;
static int requested_kind=-1;
static unsigned native_header_shown;
extern u8 SelectiveUpdate;
/* Invalidate only mode-dependent native cells. GEN, battery and panel
 * layout are shared; their ordinary handlers retain their own dirty flags. */
void UiCommon_ModeTransition(void)
{
    unsigned group,detail;
    /* Mode callers already reconcile ADC/range/base/trigger. Discard the
     * pending Normal whole-UI refresh (including an unintended GEN restart). */
    Update=0;SelectiveUpdate=0;ClearMeterAreaFlag=0;
    for(group=TRACK1;group<=T_VERNIE;++group) {
        if(group==OUTPUT || group==RUNNING || (group==FILE && Current!=FILE))continue;
        for(detail=0;detail<4;++detail)Title[group][detail].Flag|=UPDAT;
    }
    Title[RUNNING][STATE].Flag|=UPDAT;
    for(group=0;group<9;++group)Meter[group].Flag|=UPDAT;
    Update_Mark();
    if(Current!=FILE)Update_View_Area();
}
/* Drawing only: callers retain dirty flags, selection timing and capability
 * filtering. Native coordinates, fixed labels and colors come from Title[]. */
void UiCommon_FixedTitle(unsigned group,unsigned detail,unsigned mode)
{
    menu *e=&Title[group][detail];
    Print_Str(e->XPOS,e->YPOS,e->Color[0],(u8)mode,e->Str);
}
void UiCommon_PercentTitle(unsigned group,unsigned mode)
{
    menu *e=&Title[group][1];unsigned n;
    NumStr[0]=' ';
    Int2Str(NumStr,10*(e->Value+1),P_UNIT,e->Value==9?3:2,STD,0);
    /* Native percent text is 5/6 cells. Pad to 6 in the same draw pass so
     * 100 -> 90 never leaves the previous suffix at the right edge. */
    n=(unsigned)strlen(NumStr);while(n<6)NumStr[n++]=' ';NumStr[n]=0;
    Print_Str(e->XPOS,e->YPOS,e->Color[0],(u8)mode,NumStr);
}
void UiCommon_Status(unsigned status){ui_status=status;}
void UiFile_Result(unsigned status){ui_status=status?status:205u;}
const char *UiCommon_Message(void)
{
    if(ui_status==UI_FILE_BUSY)return "UARTBUSY";
    if(ui_status==UI_ARB_INVALID)return "LOAD ARB";
    if(ui_status==UI_UART_INVALID)return "LOAD UAR";
    if(ui_status==UI_UNAVAILABLE)return "UNAVAIL";
    if(ui_status==200)return "EXISTS";
    if(ui_status==205)return "FILE OK";
    if(ui_status==EMPT)return "NONE";
    if(ui_status==RD_ERR)return "READ ERR";
    if(ui_status==WR_ERR)return "WRITEERR";
    return ui_status?"FILE ERR":"";
}
int Generator_SecBusy(void){return UartFileSize!=0;}
void Generator_UartDiscard(void)
{
    DMA1_Channel4->CCR&=~1u;StartUart=0;UartFileSize=0;
}
int Generator_Validate(void)
{
    if(_Kind<0 || _Kind>8)_Kind=6;
    if(_Kind>=5)Sweep=0; /* Native sweep ISR only implements waveform kinds 0..4. */
    if(_Frqn<0)_Frqn=0;
    if(_Frqn>GeneratorCapabilities[_Kind].frequency_max)_Frqn=GeneratorCapabilities[_Kind].frequency_max;
    if((_Kind==7 && (ArbtSampleNumber<2 || ArbtSampleNumber>720)) ||
       (_Kind==8 && (!UartFileSize || UartFileSize>4096))) {
        requested_kind=_Kind;
        UiCommon_Status(_Kind==7?UI_ARB_INVALID:UI_UART_INVALID);_Kind=6;
    }
    if(_Kind!=8)InitiateUartGen=0;
    if(GenUartMode<1 || GenUartMode>6)GenUartMode=3;
    if(GenBaudIndex<16)GenBaudIndex=16;
    if(GenBaudIndex>15871)GenBaudIndex=15871;
    return _Kind!=6;
}
void Generator_Reconcile(void)
{
    if(output_dirty){output_dirty=0;Update_Output();}
}
static void BaudStep(int delta)
{
    unsigned step=1,index=GenBaudIndex;
    if(GenBaudAdjSpeed)step=index<800?20:index<2800?50:index<12970?100:index<14970?50:20;
    if(delta>0)GenBaudIndex=(u16)(index>16+step?index-step:16);
    else GenBaudIndex=(u16)(index+step<15871?index+step:15871);
}
/* Compact labels are independent of the stable measurement IDs in CFG.
 * Only actual measurements have storage; invalid IDs never index the table. */
const char *UiCommon_MeterLabel(unsigned item)
{
    static const char names[11][5]={"Vbt","Vdc","RMS","Frq","Max","Min","Vpp","Per","Dut","!TH!","!TL!"};
    if(item==VBT)return names[0];
    if(item<VDC || item>TL)return "--";
    return names[item-1u];
}
unsigned UiCommon_MeterStep(unsigned item,int delta)
{
    if(item!=VBT && (item<VDC || item>TL))return delta<0?TL:VBT;
    if(delta<0)return item==VBT?TL:item==VDC?VBT:item-1u;
    return item==TL?VBT:item==VBT?VDC:item+1u;
}
/* Shared default-page layout after a panel change, as in native Normal.
 * User-defined single-channel pages are left untouched. */
void MeterDefaults(void){
              unsigned i;

              if ((UpdateMeter==4)&&(FlagMeter>0)){

                Meter[0].Flag |= UPDAT;
                Meter[0].Item  =  VBT;
                Meter[0].Track = 4;  
                if(FlagMeter==1){
                  for(i=1;i<9;++i)Meter[i].Flag|=UPDAT;
                }else if(FlagMeter==2){
                  for(i=1;i<4;++i)Meter[i].Flag|=UPDAT;
                }

                if ((_Mode==X_Y) || ( (Title[TRACK3][SOURCE].Value == HIDE) && (Title[TRACK4][SOURCE].Value == HIDE)))
                {
                  if(FlagMeter==1){              
                    Meter[1].Item  =  RMS; Meter[1].Track = 0;
                    Meter[2].Item  =  VDC; Meter[2].Track = 0;
                    Meter[3].Item  =  VPP; Meter[3].Track = 0;
                    Meter[4].Item  =  FRQ; Meter[4].Track = 0;
                    Meter[5].Item  =  RMS; Meter[5].Track = 1;
                    Meter[6].Item  =  VDC; Meter[6].Track = 1;
                    Meter[7].Item  =  VPP; Meter[7].Track = 1;
                    Meter[8].Item  =  FRQ; Meter[8].Track = 1;
                  }else{
                    Meter[2].Item  =  VDC; Meter[2].Track = 0;
                    Meter[3].Item  =  VDC; Meter[3].Track = 1;
                  }
                }  
                else  
                {
                  if(FlagMeter==1){              
                    Meter[1].Item  =  FRQ; Meter[1].Track = 0;
                    Meter[2].Item  =  VDC; Meter[2].Track = 0;
                    Meter[3].Item  =  FRQ; Meter[3].Track = 1;
                    Meter[4].Item  =  VDC; Meter[4].Track = 1;
                    Meter[5].Item  =  FRQ; Meter[5].Track = 2;
                    Meter[6].Item  =  CIR; Meter[6].Track = 2;
                    Meter[7].Item  =  FRQ; Meter[7].Track = 3;
                    Meter[8].Item  =  CIR; Meter[8].Track = 3;
                  }else{ 
                    Meter[2].Item  =  VDC; Meter[2].Track = 0;
                    Meter[3].Item  =  VDC; Meter[3].Track = 1;
                  }
                }
               }
}

/* One visibility rule for Normal, interlace and both digital channels.
 * required contains consumers outside the panel (detector, autoset, ...). */
int UiCommon_TimeItem(unsigned item)
{ return item==FRQ || (item>=CIR && item<=TL); }
/* Bit i denotes a visible Meter[i], including the small battery field
 * above the two large default meters. Hidden fields retain their dirty flags. */
unsigned UiCommon_MeterMask(void)
{
    if(FlagMeter==1)return 0x1ffu;
    return FlagMeter==2?(UpdateMeter==4?0x0du:0x0eu):0;
}
unsigned UiCommon_TimeDemand(unsigned required)
{
    unsigned i,mask=UiCommon_MeterMask();
    for(i=0;mask;++i,mask>>=1)if(mask&1u) {
        const meter *m=&Meter[i];
        if(m->Track<4 && UiCommon_TimeItem(m->Item))required|=1u<<m->Track;
    }
    return required&15u;
}
int UiCommon_ValueKey(unsigned key,unsigned interlace)
{
    int next;
    if(Current!=BK_LIGHT && Current!=VOLUME)return 0;
    if(key!=K_INDEX_INC && key!=K_INDEX_DEC)return 0;
    if(!interlace && ((Current==BK_LIGHT && (FastDimAdjust || HboldAdjust)) ||
       (Current==VOLUME && AdjBeepLevel)))return 0;
    next=Title[Current][1].Value+(key==K_INDEX_INC?1:-1);
    if(next>=0 && next<=9) {
        Title[Current][1].Value=(s16)next;Title[Current][1].Flag|=UPDAT;
        __Set(Current==BK_LIGHT?BACKLIGHT:BEEP_VOLUME,(next+1)*(Current==BK_LIGHT?10:5));
    }
    return 1;
}
/* One native generic detail step after Main's special pre-navigation.
 * Interlace uses the same cursor/channel step with its capability limit. */
int UiCommon_NextDetail(unsigned interlace)
{
    unsigned old,next,limit=4,cursor=Current==V_VERNIE || Current==T_VERNIE;
    if(interlace) {
        if(!cursor && Current!=TRACK1 && Current!=T_BASE)return 0;
        limit=Scope144_DetailLimit(Current);
    } else if(Current>=METER_0)return 0;
    old=Detail[Current];if(old>=limit)return 0;
    Title[Current][old].Flag|=UPDAT;
    if(!interlace && cursor && !FlagMeter && old==V2) {
        Detail[Current]=0;return 1;
    }
    next=old+1;if(next==limit)next=0;
    if(!interlace) {
        unsigned mask=NOT|NUM2;
        if(Current==OUTPUT) {
            if(_Kind==PWM){if(next==OUTATT)next=0;}
            else if(next==DUTYPWM)++next;
        }
        if(Current!=FILE && Current!=OUTPUT)mask|=NUM3;
        if(Title[Current][next].MARK&mask)next=0;
        if(Current==FILE && next==DIR && Title[FILE][2].Value==7 && Title[FILE][0].Value==SAVE)next=0;
    }
    Detail[Current]=(u8)next;Title[Current][next].Flag|=BLINK;
    return 1;
}
int Generator_Key(unsigned key,unsigned interlace)
{
    unsigned d;int delta;
    s16 old_values[4];u16 old_baud;unsigned old_uart,n;
    if(Current!=OUTPUT)return 0;
    d=Detail[OUTPUT];
    if(d>3){d=0;Detail[OUTPUT]=0;}
    /* Normal's sweep/burst and pulse-length special editor remain native.
     * Their Main-loop synchronization is deliberately unavailable in 144. */
    if(!interlace && ((Sweep && _Kind<5) || (GenAdjustMode && _Kind<5)))return 0;
    if(key==KEY4){
        Title[OUTPUT][d].Flag|=UPDAT;
        if(_Kind==8 && d==FRQN && GenUartAdj<3){++GenUartAdj;return 1;}
        GenUartAdj=0;Detail[OUTPUT]=(u8)Generator_NextDetail(_Kind,d);
        Title[OUTPUT][Detail[OUTPUT]].Flag|=UPDAT|BLINK;return 1;
    }
    if(key==K_INDEX_S) {
        if(d==KIND && _Kind==8) {
            if((DMA1_Channel4->CCR&1u) && DMA1_Channel4->CNDTR)DMA1_Channel4->CCR&=~1u;
            else if(UartFileSize){StartUart=1;output_dirty=1;}
            return 1;
        }
        if(d==FRQN && _Kind==8){GenBaudAdjSpeed=!GenBaudAdjSpeed;return 1;}
        if(d==FRQN && _Kind==7){GenAdjustMode=!GenAdjustMode;return 1;}
        if(_Kind==PWM && d==DUTYPWM){PWAdjustMode=!PWAdjustMode;return 1;}
        if(interlace && d==FRQN && _Kind<5){GenAdjustMode=!GenAdjustMode;return 1;}
        return 0;
    }
    if(key==K_ITEM_S && _Kind==8 && d==FRQN){GenUartAdj=(GenUartAdj+1)%4;return 1;}
    if(key!=K_INDEX_INC && key!=K_INDEX_DEC)return 0;
    delta=key==K_INDEX_INC?1:-1;ui_status=0;
    if(!Generator_Detail(_Kind,d))return 1;
    if(_Kind==8 && d!=KIND && (((DMA1_Channel4->CCR&1u) && DMA1_Channel4->CNDTR) ||
       ((USART1->CR1&0x2000u) && !(USART1->SR&0x40u)))) {
        UiCommon_Status(UI_FILE_BUSY);return 1;
    }
    for(n=0;n<4;++n)old_values[n]=Title[OUTPUT][n].Value;
    old_baud=GenBaudIndex;old_uart=GenUartMode+16u*GenUartStopBits+32u*GenUartCont;
    if(d==KIND) {
        unsigned from=requested_kind>=0 && _Kind==6?(unsigned)requested_kind:(unsigned)_Kind;
        _Kind=(s16)((from+9+delta)%9);requested_kind=_Kind;
        if(_Kind==6)Generator_UartDiscard();
        GenAdjustMode=PWAdjustMode=0;
    } else if(d==FRQN && _Kind==8) {
        if(GenUartAdj==0)BaudStep(delta);
        else if(GenUartAdj==1){if(GenUartMode+delta>=1 && GenUartMode+delta<=6)GenUartMode+=delta;}
        else if(GenUartAdj==2)GenUartStopBits=delta>0;
        else GenUartCont=delta>0;
    } else if(d==FRQN && (_Kind==7 || _Kind<5) && GenAdjustMode) {
        if(GenFreqShift-delta>=-100 && GenFreqShift-delta<=100)GenFreqShift-=delta;
        OutputAdjust();return 1;
    } else if(d==FRQN) {
        if(_Frqn+delta>=0 && _Frqn+delta<=GeneratorCapabilities[_Kind].frequency_max) {
            _Frqn+=delta;
            if(_Kind==PWM && PWAdjustMode)PWMscaleReset(delta>0?0:1);
        }
    } else if(d==DUTYPWM) {
        if(PWAdjustMode){PWMdutyControl(delta>0?0:1);Title[OUTPUT][DUTYPWM].Flag|=UPDAT;return 1;}
        int value=_Duty+delta*300;if(value<1)value=1;if(value>29999)value=29999;_Duty=(s16)value;
    } else if(d==OUTATT) {
        if(Title[OUTPUT][OUTATT].Value+delta>=0 && Title[OUTPUT][OUTATT].Value+delta<=100)
            Title[OUTPUT][OUTATT].Value+=delta;
    }
    for(n=0;n<4 && old_values[n]==Title[OUTPUT][n].Value;++n){}
    if(n<4 || old_baud!=GenBaudIndex || old_uart!=GenUartMode+16u*GenUartStopBits+32u*GenUartCont) {
        for(n=0;n<4;++n)Title[OUTPUT][n].Flag|=UPDAT;
        output_dirty=1;
    }
    return 1;
}
static void Decimal(char *out,unsigned n)
{
    char rev[11];unsigned k=0;do{rev[k++]=(char)('0'+n%10);n/=10;}while(n);
    while(k)*out++=rev[--k];
    *out=0;
}
void Generator_Text(char text[9])
{
    char n[12];unsigned d=Current==OUTPUT?Detail[OUTPUT]:_Kind==PWM?DUTYPWM:_Kind<4?OUTATT:KIND,v;
    text[0]=0;
    if(Current==OUTPUT && UiCommon_Message()[0]){strncpy(text,UiCommon_Message(),8);text[8]=0;return;}
    if(Current==OUTPUT && Generator_DisplayKind()!=(unsigned)_Kind) {
        strcpy(text,Generator_DisplayKind()==7?"LOAD ARB":"LOAD UAR");return;
    }
    if(!Generator_Detail(_Kind,d)){strcpy(text,"--");return;}
    if((_Kind==PWM && PWAdjustMode && d==DUTYPWM) ||
       (GenAdjustMode && d==FRQN && (_Kind<5 || _Kind==7))) {
        char formatted[16];uint64_t value;const char *unit=F_UNIT;
        if(_Kind==PWM && PWAdjustMode && d==DUTYPWM) {
            unsigned ticks=TIM4->CCR1<((TIM4->ARR+1)/2)?TIM4->CCR1:TIM4->ARR+1-TIM4->CCR1;
            value=(uint64_t)ticks*(TIM4->PSC+1)*1000u/72u;unit=T_UNIT;
        } else {
            unsigned samples=_Kind==7?ArbtSampleNumber:_Kind==PWM?1:720/ScaleIndex[_Frqn];
            uint64_t divider=_Kind==PWM?(uint64_t)(TIM4->PSC+1)*(TIM4->ARR+1):
                (uint64_t)(TIM7->PSC+1)*(TIM7->ARR+1)*samples;
            value=divider?72000000u/divider:0;
        }
        Int2Str(formatted,value>2147483647?2147483647:(s32)value,unit,3,UNSIGN,0);
        strncpy(text,formatted,8);text[8]=0;return;
    }
    if(_Kind==8) {
        if(GenUartAdj==0){Decimal(n,GenBaudRate);strncpy(text,n,8);text[8]=0;}
        else if(GenUartAdj==1){static const char mode[6][5]={"7Pe","7Po","8N","8Pe","8Po","9N"};strcpy(text,mode[GenUartMode>=1 && GenUartMode<=6?GenUartMode-1:2]);}
        else if(GenUartAdj==2)strcpy(text,GenUartStopBits?"2 STOP":"1 STOP");
        else strcpy(text,GenUartCont?"REPEAT":"ONCE");
    } else if(_Kind==7){Decimal(n,ArbtSampleNumber);strcpy(text,n);strcat(text," pts");}
    else if(_Kind==5 || _Kind==6)text[0]=0;
    else if(_Kind==PWM && d!=OUTATT) {
        v=(_Duty+15)/30;Decimal(n,v/10);strcpy(text,"D ");strcat(text,n);strcat(text,".");
        {unsigned len=(unsigned)strlen(text);text[len]=(char)('0'+v%10);text[len+1]='%';text[len+2]=0;}
    } else {
        /* The native output is 28 mV per amplitude step. Keep its voltage
         * visible outside GEN as well, without adding another formatter. */
        char voltage[16];
        Int2Str(voltage,Title[OUTPUT][OUTATT].Value*28000,V_UNIT,2,UNSIGN,0);
        v=(unsigned)strlen(voltage);if(v>5)v=5;
        memcpy(text,"Vo ",3);memcpy(text+3,voltage,v);text[3+v]=0;
    }
}
void Generator_Draw(void)
{
    char text[9],cell[9];unsigned n;
    Generator_Text(text);n=(unsigned)strlen(text);if(n>8)n=8;
    memset(cell,' ',8);memcpy(cell,text,n);cell[8]=0;
    Print_Str(172,216,Generator_AuxColor(),Generator_AuxSelected()?Twink:PRN,cell);
    native_header_shown=Current==OUTPUT?1:3;
}
unsigned short Generator_Color(void)
{
    return (SCRN<<8)+(((_Kind<5 && Sweep) || _Kind==5)?VERNIE:NOTE1);
}
unsigned short Generator_ValueColor(void)
{
    /* Print_Str PRN uses the low byte for background. Invert only through
     * Twink when the value is selected, as with native numeric fields. */
    return ((Generator_Color()&0xffu)<<8)|SCRN;
}
unsigned short Generator_AuxColor(void)
{
    if(Current==OUTPUT && (ui_status==UI_ARB_INVALID || ui_status==UI_UART_INVALID ||
       Generator_DisplayKind()!=(unsigned)_Kind))return (SCRN<<8)+NOTE1;
    return Generator_ValueColor();
}
unsigned Generator_DisplayKind(void)
{
    /* A selected file generator without valid data remains electrically OFF.
     * Keep its selected name and LOAD prompt, so the selector is intelligible. */
    if(_Kind==6 && (requested_kind==7 || requested_kind==8))return (unsigned)requested_kind;
    return _Kind>=0 && _Kind<9?(unsigned)_Kind:6;
}
void Generator_KindTitle(void)
{
    unsigned kind=Generator_DisplayKind();
    Print_Str(239,228,Generator_Color(),Current==OUTPUT && Detail[OUTPUT]==KIND?Twink:PRN,
        Title[OUTPUT][KIND].Str+kind*10);
}
void Generator_FrequencyTitle(void)
{
    menu *e=&Title[OUTPUT][FRQN];
    unsigned kind=Generator_DisplayKind(),selected=0,n=0,width=0;
    int rate=e->Value;const char *text="      ";char cell[9];
    /* One writer in Normal and 144, including an opaque blank for OFF.
     * Preserve the stored rate; disabled fields never blink an empty box. */
    if(kind==8)text="UART  ";
    else if(kind!=(unsigned)_Kind)text="  --  ";
    else if(kind!=6) {
        if(rate<0 || rate>e->Limit)rate=0;
        text=e->Str+rate*10;
    }
    if(kind!=6 && kind==(unsigned)_Kind)
        selected=Current==OUTPUT && Detail[OUTPUT]==FRQN;
    /* '!' is a half-width glyph. Pad shorter labels without first clearing
     * the LCD field, so switching UART/frequency/OFF leaves no old digits. */
    while(text[n] && n<8){cell[n]=text[n];width+=text[n]=='!'?4:8;++n;}
    while(width<48 && n<8){cell[n++]=' ';width+=8;}
    cell[n]=0;
    Print_Str(e->XPOS,e->YPOS,Generator_ValueColor(),selected?Twink:PRN,cell);
}
/* Native eight-character status cell, shared by FILE and 144. */
void UiCommon_HeaderText(unsigned short color,unsigned selected,const char *text)
{
    char cell[9];unsigned length=(unsigned)strlen(text);
    if(length>8)length=8;
    memset(cell,' ',8);memcpy(cell,text,length);cell[8]=0;
    Print_Str(172,216,color,selected?Twink:PRN,cell);
}
unsigned UiCommon_HeaderOwned(void)
{
    unsigned shared=Current!=FILE && !Sweep && !GenAdjustMode && !PWAdjustMode;
    shared|=Current==FILE && (Title[FILE][2].Value==5 || Title[FILE][2].Value==6);
    return shared;
}
void UiCommon_HeaderCleanup(void)
{
    if(native_header_shown && !UiCommon_HeaderOwned()){ClearScreenArea(172,237,216,227);native_header_shown=0;}
    if(UiCommon_HeaderOwned() && Current!=FILE && native_header_shown!=(Current==OUTPUT?1u:3u))
        Title[OUTPUT][KIND].Flag|=UPDAT;
}
unsigned Generator_AuxSelected(void)
{
    unsigned d=Detail[OUTPUT];
    return Current==OUTPUT && Generator_Detail(_Kind,d) &&
        (d==DUTYPWM || d==OUTATT || (d==FRQN && (_Kind==8 || GenAdjustMode)));
}
unsigned TriggerMenu_K2(unsigned mode,unsigned manual)
{return manual?0:mode==0?2:mode%3+1;}

static void DirectoryRefresh(void)
{
    memcpy(Ext,Title[FILE][2].Value==5?"ARB":"UAR",4);
    directory_count=ReadDir(Ext);
    if(Title[FILE][3].Value<0)Title[FILE][3].Value=0;
    if(Title[FILE][3].Value>=(s16)directory_count)Title[FILE][3].Value=directory_count?(s16)(directory_count-1):0;
    if(directory_count && Label[Title[FILE][3].Value][0]) {
        memcpy(SelectedFileName,Label[Title[FILE][3].Value],8);SelectedFileName[8]=0;
    } else {memcpy(SelectedFileName,"None    ",9);}
}
unsigned UiFile_Directory(void)
{return Current==FILE && Detail[FILE]==DIR && Title[FILE][0].Value==LOAD && (Title[FILE][2].Value==5 || Title[FILE][2].Value==6);}
unsigned UiFile_Changed(void){return file_changed;}
static void FileChanged(void)
{
    unsigned n;file_changed=1;for(n=0;n<4;++n)Title[FILE][n].Flag|=UPDAT;
}
void __attribute__((optimize("Os"))) UiFile_Enter(void)
{
    unsigned supported=(Title[FILE][0].Value==SAVE && (Title[FILE][2].Value==CSV || Title[FILE][2].Value==CFG)) ||
        (Title[FILE][0].Value==LOAD && (Title[FILE][2].Value==5 || Title[FILE][2].Value==6));
    file_changed=0;
    if(!supported){Title[FILE][0].Value=SAVE;Title[FILE][2].Value=CSV;Detail[FILE]=0;}
    if(Title[FILE][1].Value<0 || Title[FILE][1].Value>999)Title[FILE][1].Value=0;
    if((Title[FILE][0].Value==LOAD || Title[FILE][2].Value==CFG) && Title[FILE][1].Value>255)Title[FILE][1].Value=255;
    directory_count=0;
    if(UiFile_Directory())DirectoryRefresh();
}
int __attribute__((optimize("Os"))) UiFile_Key(unsigned key,unsigned interlace)
{
    unsigned d;int delta;
    if(Current!=FILE)return 0;
    if(!interlace && Title[FILE][2].Value!=5 && Title[FILE][2].Value!=6)return 0;
    d=Detail[FILE];
    if(key==KEY3 || key==K_INDEX_S){FileChanged();return 2;}
    if(key==KEY4) {
        FileChanged();
        Detail[FILE]=(u8)((d+1)%(Title[FILE][0].Value==LOAD?4:3));
        if(UiFile_Directory()){DirRange=0;DirectoryRefresh();}
        return 1;
    }
    if(key!=K_INDEX_INC && key!=K_INDEX_DEC)return 0;
    FileChanged();ui_status=0;delta=key==K_INDEX_INC?1:-1;
    if(d==0) {
        Title[FILE][0].Value=1-Title[FILE][0].Value;
        Title[FILE][2].Value=Title[FILE][0].Value==SAVE?CSV:5;
        if(Title[FILE][0].Value==LOAD && Title[FILE][1].Value>255)Title[FILE][1].Value=255;
    } else if(d==1) {
        int limit=Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CSV && interlace?999:255;
        int next=Title[FILE][1].Value+delta;if(next<0)next=limit;if(next>limit)next=0;
        Title[FILE][1].Value=(s16)next;
    } else if(d==2 && Title[FILE][0].Value==SAVE && interlace) {
        Title[FILE][2].Value=Title[FILE][2].Value==CFG?CSV:CFG;
        if(Title[FILE][2].Value==CFG && Title[FILE][1].Value>255)Title[FILE][1].Value=255;
    } else if(d==2 && Title[FILE][0].Value==LOAD) {
        Title[FILE][2].Value=Title[FILE][2].Value==5?6:5;DirRange=0;
    } else if(d==3 && UiFile_Directory()) {
        if(delta<0){if(Title[FILE][3].Value>0)--Title[FILE][3].Value;else if(DirRange)--DirRange;}
        else if(Title[FILE][3].Value+1<(s16)directory_count)++Title[FILE][3].Value;
        else if(directory_count==15)++DirRange;
        DirectoryRefresh();
    }
    return 1;
}
int UiFile_Load(void)
{
    int status;
    if(Title[FILE][0].Value!=LOAD || (Title[FILE][2].Value!=5 && Title[FILE][2].Value!=6)) {
        UiFile_Result(UI_UNAVAILABLE);return UI_UNAVAILABLE;
    }
    if(UiFile_Directory() && (!directory_count || !Label[Title[FILE][3].Value][0])) {
        UiFile_Result(EMPT);return EMPT;
    }
    if(!UiFile_Directory() && (Title[FILE][1].Value<0 || Title[FILE][1].Value>255)) {
        UiFile_Result(UI_UNAVAILABLE);return UI_UNAVAILABLE;
    }
    status=Title[FILE][2].Value==5?Load_Arbt((u8)Title[FILE][1].Value):Load_Uart((u8)Title[FILE][1].Value);
    if(!status && _Kind==6 && requested_kind==Title[FILE][2].Value+2) {
        _Kind=(s16)requested_kind;Title[OUTPUT][KIND].Flag|=UPDAT;
    }
    if(!status && ((_Kind==7 && Title[FILE][2].Value==5) || (_Kind==8 && Title[FILE][2].Value==6))) {
        if(_Kind==8)StartUart=1;
        output_dirty=1;Generator_Reconcile();
    }
    UiFile_Result(status);return status;
}
void __attribute__((optimize("Os"))) UiFile_Draw(void)
{
    unsigned d=Detail[FILE],type=(unsigned)Title[FILE][2].Value;char num[4],cell[28];
    if(type<CSV || type>6)type=CSV;
    memset(cell,' ',27);cell[27]=0;Print_Str(92,0,0x0505,PRN,cell);
    Print_Str(126,0,0x0405,d==0?Twink:PRN,Title[FILE][0].Str+11u*(Title[FILE][0].Value==LOAD));
    num[0]=(char)('0'+Title[FILE][1].Value/100%10);num[1]=(char)('0'+Title[FILE][1].Value/10%10);num[2]=(char)('0'+Title[FILE][1].Value%10);num[3]=0;
    Print_Str(206,0,0x0405,d==1?Twink:PRN,num);
    Print_Str(230,0,0x0405,d==2?Twink:PRN,Title[FILE][2].Str+10u*type);
    if(Title[FILE][0].Value==LOAD)Print_Str(166,0,0x0405,d==DIR?Twink:PRN,"DIR");
    if(UiFile_Directory())PrintDir(0,0,1);
    if(!Wildcat144_Active()) {
        UiCommon_HeaderText(0x0405,0,UiCommon_Message());native_header_shown=2;
    }
}

void UiFile_Begin(void)
{ Print_Str(230,0,(SCRN<<8)+TEXT1,PRN," >   "); }

/* Original Normal repeat acceleration, shared with interlace. */
int UiCommon_CursorSpeed(unsigned enable,unsigned frame_mode)
{
    int speed=0;
    if(SetLowVolume && (enable ||
       (((Current==T_VERNIE || Current==V_VERNIE) && _Det<2) || (Current<4 && _Det==3)))) {
        speed=1;if(!frame_mode)++speed;
    }
    return speed;
}
u8 CursorSpeedLogic(u8 enable)
{ return (u8)UiCommon_CursorSpeed(enable,FlagFrameMode); }
