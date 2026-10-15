/* Native Wildcat single-input interlace acquisition and calibration. */
#include "BIOS.h"
#include "Process.h"
#include "Menu.h"
#include "Interrupt.h"
#include "Function.h"
#include "Capture144.h"
#include "Gen144.h"
#include "Text144.h"
#include "Scope144.h"
#include "Calibrat.h"
#include "UnifiedCal.h"
#include "CalStable.h"
#include "Clock144.h"
#include "File.h"
#include <string.h>

#define PAIRS (INTERLACE144_DISCARD_PAIRS + 512u)
#define CYCLES_MS 72000u
/* Reuse calibration storage after coefficients have been committed. */
static union {
    struct { uint16_t low[PAIRS], high[PAIRS]; } dc;
    struct {
        uint16_t pairs[PAIRS];
        struct { int32_t output[1024]; } result;
    } frame;
} work;

static int calibrating;
static int Key(unsigned mask) { return (__Get(KEY_STATUS) & mask) == 0; }
static void Text(unsigned y, const char *s)
{
    char row[TEXT144_COLUMNS+1];
    Text144_Row(row, s);
    GPIOC->BRR = 0x30; /* Restore FIFO/LCD bus mode before every draw. */
    __Display_Str(8, y, WHT, PRN, row);
}
static unsigned AppendNumber(char *line, unsigned length, int32_t value)
{
    char digits[12];
    unsigned n = 0;
    uint32_t magnitude;
    if (value < 0) { line[length++] = '-'; magnitude = 0u - (uint32_t)value; }
    else magnitude = (uint32_t)value;
    do { digits[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    while (n) line[length++] = digits[--n];
    line[length] = 0;
    return length;
}
static void Number(unsigned y, const char *label, int32_t value)
{
    char line[48];
    unsigned length = 0;
    while (*label && length < 30) line[length++] = *label++;
    AppendNumber(line, length, value);
    Text(y, line);
}
static uint16_t ReadId(unsigned selector)
{
    uint16_t value;
    __Set(32+5, selector); __Set(32+5, selector);
    GPIOC->BSRR = 0x10;
    value = (uint16_t)__Get(0);
    GPIOC->BRR = 0x30;
    return value;
}
static int Configure(void *user)
{
    FpgaProfile live;
    unsigned mode = *(unsigned *)user;
    if (mode > 1) return 0;
    live = FpgaProfile_Decode(ReadId(0), ReadId(1), ReadId(2));
    if (!FpgaProfile_Has(&live,FPGA_CAP_FIFO) || (mode && !Interlace144_Available(&live)) || live.family != WildcatFpga.family ||
        live.protocol != WildcatFpga.protocol || live.implementation != WildcatFpga.implementation)
        return 0;
    __Set(TRIGG_MODE, UNCONDITION);
    __Set(V_THRESHOLD, 128);
    __Set(T_THRESHOLD, 10);
    __Set(ADC_CTRL, mode ? 3 : 1);
    __Set(ADC_MODE, mode ? INTERLACE : SEPARATE);
    __Set(32+0x0e, 0); __Set(32+0x0f, 0); __Set(32+0x10, 0);
    __Set(32+0x11, 0); __Set(32+0x12, 0);
    GPIOC->BRR = 0x30;
    return 1;
}
static int Start(void *unused) { (void)unused; __Set(FIFO_CLR, W_PTR); return 1; }
static uint32_t Status(void *unused) { (void)unused; GPIOC->BRR = 0x30; return __Get(FIFO_DIGIT); }
static int ReadPair(void *unused, uint16_t *pair)
{
    (void)unused;
    *pair = (uint16_t)__Read_FIFO(); /* Low half is A/B; high half status. */
    GPIOC->BRR = 0x30;
    return 1;
}
static uint32_t Now(void *unused) { (void)unused; return DWT_CYCLES; }
static int Cancel(void *unused) { (void)unused; return calibrating && Key(KEY4_STATUS); }
static int Settle(unsigned ms)
{
    uint32_t start = DWT_CYCLES;
    while ((uint32_t)(DWT_CYCLES - start) < ms * CYCLES_MS)
        if (calibrating && Key(KEY4_STATUS)) return 0;
    return 1;
}
static Capture144Status Record(const Capture144Ops *ops, uint16_t *pairs)
{
    return Capture144_Record(&WildcatFpga, ops, pairs, PAIRS, 1000u * CYCLES_MS);
}
static void CaptureError(Capture144Status status)
{
    if (status == CAPTURE144_TIMEOUT) Text(140, "FIFO timeout; K1 retry calibration");
    else if (status == CAPTURE144_IO) Text(140, "FPGA changed or FIFO error");
    else Number(140, "Capture error: ", status);
}
/* Shared native acquisition state; the main loop calls the service once per
 * frame. Modal loops are used only by the ordinary calibration wizard. */
static Scope144Bank scope_bank;
static int scope_active;
int Wildcat144_Active(void){return scope_active;}
static int ScopeConfirm(void)
{
    unsigned mask=KEY1_STATUS|KEY2_STATUS|KEY3_STATUS|KEY4_STATUS;
    while((__Get(KEY_STATUS)&mask)!=mask) if(Key(KEY4_STATUS)) return 0;
    for(;;) {
        if(Key(KEY4_STATUS))return 0;
        if(Key(KEY1_STATUS)) {
            while(Key(KEY1_STATUS))if(Key(KEY4_STATUS))return 0;
            Key_Buffer=0; return 1;
        }
    }
}
static void ScopeInputs(unsigned range,unsigned coupling)
{
    __Set(CH_A_COUPLE,coupling); __Set(CH_B_COUPLE,DC);
    __Set(CH_A_RANGE,range); __Set(CH_B_RANGE,8); /* Both ADCs sample A. */
    __Set(CH_A_OFFSET,SCOPE144_OFFSET); __Set(CH_B_OFFSET,SCOPE144_OFFSET);
    __Set(T_BASE_PSC,0); __Set(T_BASE_ARR,0);
}
/* One shared bus adapter for standard A/B and the real interlace path. */
static CalCaptureStats calibration_stats;
void WildcatCalibration_Diagnostics(CalCaptureStats *stats)
{
    if(stats)*stats=calibration_stats;
}
int WildcatCalibration_Capture(unsigned range,unsigned interlace,unsigned aoffset,unsigned boffset,CalMean *mean)
{
    unsigned mode=interlace,i,records=0,bursts=0,timer_started=0;
    uint32_t started=0,elapsed,remaining,record_started,block_started=0;
    uint32_t sum_a=0,sum_b=0,clips=0;
    CalMean last={0,0,0};CalStable stable;
    Capture144Status status;int result=0;
    Capture144Ops ops={&mode,Configure,Start,Status,ReadPair,Now,Cancel};
    memset(&calibration_stats,0,sizeof(calibration_stats));memset(&stable,0,sizeof(stable));
    if(!mean || range>=8 || interlace>1 || aoffset>200 || boffset>200)return 0;
    *mean=last;
    __Set(CH_A_COUPLE,DC);__Set(CH_B_COUPLE,DC);
    __Set(CH_A_RANGE,range);__Set(CH_B_RANGE,interlace?8:range);
    __Set(CH_A_OFFSET,aoffset);__Set(CH_B_OFFSET,boffset);
    __Set(T_BASE_PSC,0);__Set(T_BASE_ARR,0);
    calibrating=1;
    if(!Configure(&mode))goto finished;
    started=DWT_CYCLES;timer_started=1;
    if(!Settle(300)){result=-1;goto finished;}
    for(;;) {
        elapsed=DWT_CYCLES-started;
        if(elapsed>=2000u*CYCLES_MS) {
            result=calibration_stats.blocks && !CalMean_Valid(&last)?CAL_CAPTURE_CLIPPED:CAL_CAPTURE_UNSTABLE;
            break;
        }
        remaining=2000u*CYCLES_MS-elapsed;
        if(remaining>1000u*CYCLES_MS)remaining=1000u*CYCLES_MS;
        record_started=DWT_CYCLES;
        status=Capture144_RecordFifo(&WildcatFpga,&ops,work.frame.pairs,PAIRS,remaining);
        if(status!=CAPTURE144_OK) {
            if(status==CAPTURE144_CANCELLED)result=-1;
            else if(status==CAPTURE144_TIMEOUT && records>1 && (uint32_t)(DWT_CYCLES-started)>=2000u*CYCLES_MS)
                result=calibration_stats.blocks && !CalMean_Valid(&last)?CAL_CAPTURE_CLIPPED:CAL_CAPTURE_UNSTABLE;
            else result=0;
            break;
        }
        calibration_stats.records=++records;
        if(records>1) {
            if(!bursts)block_started=record_started;
            for(i=INTERLACE144_DISCARD_PAIRS;i<PAIRS;++i) {
                unsigned av=work.frame.pairs[i]&255,bv=work.frame.pairs[i]>>8;
                sum_a+=av;sum_b+=bv;if(!av || av==255 || !bv || bv==255)++clips;
            }
            if(++bursts==8) {
                /* Eight fresh 512-pair bursts, with >=3 ms between bursts:
                 * >=21 ms from first burst to last, at the actual ADC clock.
                 * Accumulate raw sums and round only after all 4096 samples. */
                last.a=(int32_t)((sum_a*1024+2048)/4096);
                last.b=(int32_t)((sum_b*1024+2048)/4096);last.clips=clips;
                *mean=last;++calibration_stats.blocks;
                calibration_stats.block_ms=(uint32_t)(record_started-block_started)/CYCLES_MS;
                bursts=0;sum_a=sum_b=clips=0;
                result=CalStable_Push(&stable,&last,mean);
                calibration_stats.window=stable.count;
                calibration_stats.spread_a=stable.spread_a;calibration_stats.spread_b=stable.spread_b;
                if(result)break;
            }
        } /* The first entire record is discarded before any block accumulation. */
        if(!Settle(3)){result=-1;break;}
    }
finished:
    if(timer_started)calibration_stats.elapsed_ms=(uint32_t)(DWT_CYCLES-started)/CYCLES_MS;
    calibrating=0;return result;
}

int Wildcat144_Service(void)
{
    static s16 saved_title[13][4],saved_trigger; static u8 saved_title_flags[13][4],saved_current;
    static u16 saved_max_x,saved_cursor_timer;
    static unsigned k2_down,k2_fired; static uint32_t k2_started;
    static uint32_t fps_started,fps_frames,fps;
    unsigned i,j,field,detail;
    static unsigned range,base,coupling,mode,falling;
    static unsigned last_range=8,last_coupling=2;
    static unsigned position=100,invert=0,probe=1,cursor1=30,cursor2=180,csvnumber=0,start=0,capture_mode=1;
    static int32_t threshold=0; static int held=0,valid=0,redraw=1,last_run_key=0;
    static u8 saved_flags[13]; static uint32_t saved_demcr,saved_dwt,saved_dier2,saved_dier4;
    static uint16_t saved_ccer,saved_ccmr;
    Interlace144Calibration relative; Interlace144Stats stats;
    static Scope144Measurements measure,display_measure;
    static Capture144Ops ops={&capture_mode,Configure,Start,Status,ReadPair,Now,Cancel};
    static unsigned exit_coupling;
    if(!scope_active && Title[TRACK1][COUPLE].Value<SCOPE144_DC)return 0;
    if(!scope_active) {
    exit_coupling=Title[TRACK1][COUPLE].Value==SCOPE144_AC?AC:DC;
    FpgaProfile detected=FpgaProfile_Decode(ReadId(0),ReadId(1),ReadId(2));
    __Set(32+5,0); __Set(32+5,0); GPIOC->BRR=0x30;
    if(detected.kind!=FPGA_W12 || Scope144_Crc((const void *)0x0805c800u,135096)!=SCOPE144_IMAGE_CRC) {
        __Clear_Screen(BLACK); Text(160,"144 scope requires installed FPGA W1.2");
        Text(140,"Install bin/FPGAW1_2 image first"); Text(100,"K1 return / K4 cancel");
        ScopeConfirm(); Title[TRACK1][COUPLE].Value=(s16)exit_coupling; App_init(1); Key_Buffer=0; return 1;
    }
    WildcatFpga=detected;
    if(Sweep || DownConvertMode || CalibrateMode || ChartLogic() || _Mode==X_Y || !(_Kind<5 || _Kind==6)) {
        __Clear_Screen(BLACK); Text(160,"144: disable sweep/noise/serial/XY first");
        ScopeConfirm(); Title[TRACK1][COUPLE].Value=(s16)exit_coupling; App_init(1); Key_Buffer=0; return 1;
    }
    for(i=0;i<13;++i)for(j=0;j<4;++j) {
        saved_title[i][j]=Title[i][j].Value; saved_title_flags[i][j]=Title[i][j].Flag;
    }
    saved_title[TRACK1][COUPLE]=(s16)exit_coupling;
    Scope144_SetClock(PPM_Comp);
    saved_trigger=_Vt1;saved_current=Current;saved_max_x=MAX_X;saved_cursor_timer=CursorDisplayTimer;
    saved_flags[0]=FlagMeter; saved_flags[1]=FlagFrameMode; saved_flags[2]=FrameMode;
    saved_flags[3]=OSBuffer; saved_flags[4]=OSAvg; saved_flags[5]=OsBufferLogicFlag;
    saved_flags[6]=ShowFFT; saved_flags[7]=Det; saved_flags[8]=TrgAuto; saved_flags[9]=CurDefTime;
    saved_flags[10]=OsChartFlag;saved_flags[11]=EnableCursorDisplay;saved_flags[12]=DigChLockout;
    range=(_A_Range>=0 && _A_Range<8)?(unsigned)_A_Range:4;
    coupling=exit_coupling; base=(_T_base==20)?1:(_T_base==19?2:0);
    mode=_Mode==SGL?2:(_Mode==NORH?1:0);falling=_Tr_kind==0;
    position=(_1_posi>=10 && _1_posi<=190)?(unsigned)_1_posi:100;
    probe=_1_source>=3?10:1;invert=_1_source==2 || _1_source==4;
    threshold=0;held=_State.Value!=RUN;valid=0;redraw=2;last_run_key=0;k2_down=k2_fired=0;
    last_range=8;last_coupling=2;start=0;
    saved_demcr=DEMCR; saved_dwt=DWT_CTRL; DEMCR|=0x01000000u; DWT_CTRL|=1;
    saved_dier2=TIM2->DIER; saved_dier4=TIM4->DIER; TIM2->DIER=0; TIM4->DIER=0;
    saved_ccer=TIM4->CCER; saved_ccmr=TIM4->CCMR1;
    { uint32_t before=DWT_CYCLES; volatile unsigned spin;
      for(spin=0;spin<64;++spin) { }
      if(before==DWT_CYCLES)goto scope_cleanup;
    }
    fps_started=DWT_CYCLES;fps_frames=fps=0;
    FlagFrameMode=0; FrameMode=0; OSBuffer=0; OSAvg=0;
    OsBufferLogicFlag=0; ShowFFT=0; Det=0; CurDefTime=0;
    OsChartFlag=0;EnableCursorDisplay=0;CursorDisplayTimer=0;DigChLockout=0;
    Current=TRACK1;MAX_X=(FlagMeter?300:386)+MIN_X;_X_posi.Flag|=HID;
    Title[V_VERNIE][V1].Flag&=~HID;Title[V_VERNIE][V2].Flag&=~HID;
    Title[T_VERNIE][T1].Flag&=~HID;Title[T_VERNIE][T2].Flag&=~HID;
    if(_V1_Vernie<1 || _V1_Vernie>199)_V1_Vernie=180;
    if(_V2_Vernie<1 || _V2_Vernie>199)_V2_Vernie=40;
    if(cursor1>(unsigned)(MAX_X-MIN_X))cursor1=30;
    if(cursor2>(unsigned)(MAX_X-MIN_X))cursor2=180;
    _1_source=1; _2_source=0; _3_source=0; _4_source=0;
    Title[TRACK2][COUPLE].Value=DC; _Mode=AUTO; _Tr_source=0; _Tr_kind=3; _X_posi.Value=0;
    memset(&measure,0,sizeof(measure)); File144_CalLoad(&scope_bank);
    Current=TRACK1;Detail[TRACK1]=COUPLE;
    App_init(0);Key_Buffer=0;scope_active=1;
    }
    {
        unsigned key=Key_Buffer; Key_Buffer=0;
        PD_Cnt=600;
        /* Latch physical press, but retain a short event between frames. */
        Scope144_RunKey(Key(KEY1_STATUS),key==KEY1,&last_run_key,&held);
        if(key==KEY1)redraw=1;
        if(key==K_ITEM_INC || key==K_ITEM_DEC) {
            Current=(u8)Scope144_GroupStep(Current,key==K_ITEM_INC?1:-1);
            redraw=1;
        }
        /* Detail has 14 entries; meter groups 13..21 have no submenu. */
        detail=Current<13?Detail[Current]:0;
        if(detail>=Scope144_DetailLimit(Current))detail=0;
        if(key==KEY4 || key==K_ITEM_S) {
            while(key==KEY4 && Key(KEY4_STATUS)) { }
            Key_Buffer=0;detail=(detail+1)%Scope144_DetailLimit(Current);redraw=1;
        }
        if(Current<13)Detail[Current]=(u8)detail;
        field=Scope144_Field(Current,detail);
        /* Match ordinary K2: <500 ms auto trigger, 500..3000 ms level,
         * 3000 ms calibration on A/B. Track the physical press across frames;
         * repeated key events must not restart the long-press deadline. */
        {
            unsigned pressed=Key(KEY2_STATUS); uint32_t now=DWT_CYCLES;
            if(!k2_down && (pressed || key==KEY2)) {
                k2_down=1;k2_fired=0;k2_started=now;
            }
            if(k2_down) {
                uint32_t elapsed=(uint32_t)(now-k2_started);
                if(pressed && !k2_fired && elapsed>=3000u*CYCLES_MS) {
                    k2_fired=1;
                    if(Current==TRACK1 || Current==TRACK2) {
                        scope_active=0;Key_Buffer=0;Calibrat(TRACK1);scope_active=1;
                        File144_CalLoad(&scope_bank);Scope144_SetClock(PPM_Comp);
                        valid=0;last_range=8;last_coupling=2;redraw=2;
                    }
                }
                if(!pressed) {
                    if(!k2_fired) {
                        if(elapsed<500u*CYCLES_MS)TrgAuto=TrgAuto==0?2:TrgAuto%3+1;
                        else {TrgAuto=0;Current=TRIGG;Detail[TRIGG]=2;}
                        redraw=1;
                    }
                    k2_down=0;
                }
            }
            if(key==KEY2)key=0;
        }
        if((key==K_INDEX_DEC || key==K_INDEX_INC) && Scope144_Available(Current,detail,_Kind)) {
            int delta=key==K_INDEX_INC?1:-1;
            switch(field) {
            case 25: {
                unsigned source=(probe==10?3:1)+invert;
                source=(source+3+delta)%4+1;
                probe=source>=3?10:1;invert=source==2 || source==4;break;
            }
            case 26: {
                int next=Scope144_CouplingStep(coupling==AC?SCOPE144_AC:SCOPE144_DC,delta,1);
                if(next<SCOPE144_DC){exit_coupling=(unsigned)next;goto scope_cleanup;}
                coupling=next==SCOPE144_AC?AC:DC;valid=0;break;
            }
            case 27: {
                int next=_Kind+delta;
                if(next==5)next+=delta;
                if(next<0)next=6;
                if(next>6)next=0;
                _Kind=(s16)next;if(_Frqn>16)_Frqn=16;Update_Output();TIM2->DIER=0;TIM4->DIER=0;break;
            }
            case 28:if((_Frqn>0 && delta<0)||(_Frqn<(_Kind==PWM?22:16) && delta>0))_Frqn+=delta;Update_Output();TIM2->DIER=0;TIM4->DIER=0;break;
            case 29:if((_Duty>1000 && delta<0)||(_Duty<29000 && delta>0))_Duty+=delta*100;Update_Output();TIM2->DIER=0;TIM4->DIER=0;break;
            case 30:if((Title[OUTPUT][OUTATT].Value>0 && delta<0)||(Title[OUTPUT][OUTATT].Value<Title[OUTPUT][OUTATT].Limit && delta>0))Title[OUTPUT][OUTATT].Value+=delta;Update_Output();TIM2->DIER=0;TIM4->DIER=0;break;
            case 0:range=(range+8+delta)%8;valid=0;memset(&measure,0,sizeof(measure));break;
            case 1:base=(base+3+delta)%3;break;
            case 2:coupling=1-coupling;valid=0;memset(&measure,0,sizeof(measure));break;
            case 3: {
                int64_t next=(int64_t)threshold+delta*(int64_t)Y_Attr[range].SCALE*probe;
                if(next>=-1000000000 && next<=1000000000)threshold=(int32_t)next;
                break;
            }
            case 4:falling=1-falling;break;case 5:mode=(mode+3+delta)%3;break;
            case 6:if((delta>0 && position<190)||(delta<0 && position>10))position+=delta;break;
            case 7:invert=1-invert;break;
            case 8:if((delta>0 && cursor1<(unsigned)(MAX_X-MIN_X))||(delta<0 && cursor1>0))cursor1+=delta;break;
            case 9:if((delta>0 && cursor2<(unsigned)(MAX_X-MIN_X))||(delta<0 && cursor2>0))cursor2+=delta;break;
            case 35:case 36: {
                s16 *v=&Title[V_VERNIE][field==35?V1:V2].Value;
                if((*v>1 && delta<0)||(*v<199 && delta>0))*v+=delta;
                break;
            }
            case 39:Title[T_VERNIE][2].Value=1-Title[T_VERNIE][2].Value;break;
            case 41: {
                meter *m=&Meter[Current-METER_0];m->Item=(m->Item+12+delta)%12;m->Track=m->Item<=FPS?4:TRACK1;break;
            }
            case 42:case 44: {
                unsigned g=field==42?BK_LIGHT:VOLUME;
                if((Title[g][1].Value>0 && delta<0)||(Title[g][1].Value<9 && delta>0))Title[g][1].Value+=delta;
                __Set(g==BK_LIGHT?BACKLIGHT:BEEP_VOLUME,(Title[g][1].Value+1)*(g==BK_LIGHT?10:5));break;
            }
            case 14:csvnumber=(csvnumber+1000+delta)%1000;break;
            case 23:probe=probe==1?10:1;break;
            default:break;
            }
            redraw=1;
        }
        if(key==KEY3 && Scope144_Available(Current,detail,_Kind)) {
            int status=0;
            if(Current==FILE)field=14;
            if(field==14) {
                status=valid?File144_Csv(&scope_bank.range[range],work.frame.result.output,work.frame.pairs,range,base,csvnumber,invert?-(int)probe:(int)probe):VER_ERR;
                __Clear_Screen(BLACK); Number(140,"CSV status (0=OK,200=exists): ",status);
                Text(100,"K1/K4 return to scope"); ScopeConfirm();
            }
            if(Current==FILE)redraw=2;else redraw=1;
        }
        field=Scope144_Field(Current,detail);
        _A_Range=(s16)range; Title[TRACK1][COUPLE].Value=(s16)(coupling==AC?SCOPE144_AC:SCOPE144_DC);
        _1_source=(probe==10?3:1)+invert;
        _1_posi=(s16)position; _T_base=base==0?21:base==1?20:19;
        _Mode=mode==2?SGL:(mode==1?NORH:AUTO);_Tr_kind=falling?0:1;
        Title[FILE][0].Value=SAVE;Title[FILE][1].Value=(s16)csvnumber;Title[FILE][2].Value=CSV;
        _T1=(s16)cursor1;_T2=(s16)cursor2;_State.Value=held?HOLD:RUN;
        { int32_t marker=(int32_t)position+(threshold/(int32_t)probe)/(int32_t)Y_Attr[range].SCALE;
          if(marker<1)marker=1;
          if(marker>199)marker=199;
          _Vt1=(s16)marker;
        }
        ScopeInputs(range,coupling);
        if(range!=last_range || coupling!=last_coupling) {
            if(!Settle(120))goto scope_cleanup;
            last_range=range;last_coupling=coupling;
        }
        if(!held && Scope144_Valid(&scope_bank.range[range])) {
            Capture144Status capture=Record(&ops,work.frame.pairs);
            if(capture==CAPTURE144_CANCELLED)goto scope_cleanup;
            if(capture!=CAPTURE144_OK) {
                valid=0; held=1; memset(&measure,0,sizeof(measure)); CaptureError(capture);
            } else {
                Scope144_Relative(&scope_bank.range[range],range,&relative);
                if(Interlace144_Process(&WildcatFpga,Scope144_Key(range),&relative,work.frame.pairs,PAIRS,
                    work.frame.result.output,1024,&stats)!=INTERLACE144_OK)goto scope_cleanup;
                if(!Scope144_Measure(&scope_bank.range[range],work.frame.result.output,1024,
                    stats.clips_a+stats.clips_b,&measure))goto scope_cleanup;
                ++fps_frames;start=0;
                if(TrgAuto) {
                    int32_t low=measure.minimum_uv,high=measure.maximum_uv;
                    threshold=TrgAuto==1?(low/2+high/2):TrgAuto==2?(low+(high-low)/4):(high-(high-low)/4);
                    threshold*=probe;if(invert)threshold=-threshold;
                }
                if(Scope144_Trigger(&scope_bank.range[range],work.frame.result.output,1024,base,(invert?-threshold:threshold)/(int32_t)probe,falling^invert,&start)) {
                    valid=1; if(mode==2)held=1;
                } else if(mode==0)valid=1;
                else valid=0;
            }
        }
        if(valid) {
            Process144_Draw(&scope_bank.range[range],work.frame.result.output,start,base,position,invert);
            /* Draw_Window renders T1/T2 and V1/V2 using the shared Title values. */
        } else if(!Scope144_Valid(&scope_bank.range[range])) {
            Text(140,"Range not calibrated: hold K2 on A");
            Text(120,"Right: menu; right press: item; left +/-");
            Text(100,"K1 RUN/HOLD; K4 next item; K3 saves CSV");
            Text(80,"Exit: A coupling -> DC/AC/DT/serial");
        }
        {
            int32_t dt=0,dv=0;
            if(valid)Scope144_Cursors(&scope_bank.range[range],work.frame.result.output,start,base,cursor1,cursor2,&dt,&dv);
            if((int64_t)dv*probe<=2147483647 && (int64_t)dv*probe>=-2147483647)dv*=probe;
            else dv=0;
            if(invert)dv=-dv;
            if(!Scope144_Scale(&measure,invert?-(int)probe:(int)probe,&display_measure))memset(&display_measure,0,sizeof(display_measure));
            uint32_t elapsed=DWT_CYCLES-fps_started;
            if(elapsed>=72000000u){fps=(uint32_t)((uint64_t)fps_frames*72000000u/elapsed);fps_frames=0;fps_started=DWT_CYCLES;}
            {int64_t delta_v=(int64_t)(_V1_Vernie-_V2_Vernie)*Y_Attr[range].SCALE*probe;
             dv=delta_v>2147483647?2147483647:delta_v<-2147483647?-2147483647:(int32_t)delta_v;}
            Menu144_View(&display_measure,dt,dv,valid,fps,(unsigned)redraw);redraw=0;
        }
    }
    return 1;
scope_cleanup:
    scope_active=0;
    saved_title[TRACK1][COUPLE]=(s16)exit_coupling;
    saved_title[TRACK1][RANGE]=(s16)range;saved_title[TRACK1][POSI]=(s16)position;
    saved_title[TRACK1][SOURCE]=(s16)((probe==10?3:1)+invert);
    saved_title[T_BASE][BASE]=(s16)(base==0?21:base==1?20:19);
    saved_title[T_BASE][MODE]=(s16)(mode==2?SGL:(mode==1?NORH:AUTO));
    saved_title[RUNNING][STATE]=(s16)(held?HOLD:RUN);
    saved_title[T_VERNIE][T1]=(s16)cursor1;saved_title[T_VERNIE][T2]=(s16)cursor2;
    saved_title[V_VERNIE][V1]=_V1_Vernie;saved_title[V_VERNIE][V2]=_V2_Vernie;
    for(i=BK_LIGHT;i<=VOLUME;++i)saved_title[i][1]=Title[i][1].Value;
    for(j=0;j<4;++j)saved_title[OUTPUT][j]=Title[OUTPUT][j].Value;
    Scope144_SetClock(0);
    __Set(ADC_CTRL,1); __Set(ADC_MODE,SEPARATE); GPIOC->BRR=0x30;
    for(i=0;i<13;++i)for(j=0;j<4;++j) {
        Title[i][j].Value=saved_title[i][j];Title[i][j].Flag=saved_title_flags[i][j];
    }
    _Vt1=saved_trigger;Current=saved_current;MAX_X=saved_max_x;CursorDisplayTimer=saved_cursor_timer;
    FlagMeter=saved_flags[0]; FlagFrameMode=saved_flags[1]; FrameMode=saved_flags[2];
    OSBuffer=saved_flags[3]; OSAvg=saved_flags[4]; OsBufferLogicFlag=saved_flags[5];
    ShowFFT=saved_flags[6]; Det=saved_flags[7]; CurDefTime=saved_flags[9];
    OsChartFlag=saved_flags[10];EnableCursorDisplay=saved_flags[11];DigChLockout=saved_flags[12];
    DWT_CTRL=saved_dwt; DEMCR=saved_demcr;
    TIM4->CCMR1=saved_ccmr; TIM4->CCER=saved_ccer;
    App_init(0); Update_Range(); Update_Base(); Update_Output();
    Update_Trig(_Tr_source==4?0:1);
    TIM2->SR=0; TIM4->SR=0; TIM2->DIER=saved_dier2; TIM4->DIER=saved_dier4;
    while(Key(KEY1_STATUS)||Key(KEY2_STATUS)||Key(KEY3_STATUS)||Key(KEY4_STATUS)) { }
    Key_Buffer=0;
    return 1;
}
int Wildcat144_CouplingKey(void)
{
    if(Current==TRACK1 && _Det==COUPLE && (Key_Buffer==K_INDEX_INC || Key_Buffer==K_INDEX_DEC)) {
        int available=WildcatFpga.kind==FPGA_W12;
        Title[TRACK1][COUPLE].Value=(s16)Scope144_CouplingStep(Title[TRACK1][COUPLE].Value,
            Key_Buffer==K_INDEX_INC?1:-1,available);
        Title[TRACK1][COUPLE].Flag|=UPDAT;Key_Buffer=0;return 1;
    }
    return 0;
}
