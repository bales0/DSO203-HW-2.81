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
#include "Frame144.h"
#include "UiCommon.h"
#include "Calibrat.h"
#include "UnifiedCal.h"
#include "CalStable.h"
#include "Clock144.h"
#include "Fft144.h"
#include "File.h"
#include <string.h>

#define CYCLES_MS 72000u
static Frame144 frame;
static ViewWindowGeometry display_view;
static int view_valid;
static int32_t xpos_q16;
static uint32_t frame_epoch;
static Capture144Session hw_session;
static unsigned hw_threshold,hw_kind,hw_range,hw_coupling,hw_probe,hw_invert;
static uint32_t hw_trigger_mode,hw_time;
static int32_t hw_level;
static int hw_offset;
static unsigned hw_source;
static Scope144Measurements digital_measure[2];
static unsigned digital_ready;
/* Last complete measurements survive cooperative re-arming. Raw DataBuf and
 * view_valid still describe only the current, completely transferred epoch. */
static unsigned measurement_available;
static int scope_active;
static uint32_t run_seen,run_cancelled,capture_work;
Scope144Performance Wildcat144Performance;
char Wildcat144Timing[40];
/* Flash is not modified by Wildcat. Cache only the startup verdict, never the
 * CRC or the bitstream; a missing/failed check leaves 144 disabled. */
static u8 installed_image_valid;
void Wildcat144_CheckImage(void)
{
    installed_image_valid=WildcatFpga.kind==FPGA_W12 &&
        Scope144_Crc((const void *)0x0805c800u,135096)==SCOPE144_IMAGE_CRC;
}
int Wildcat144_DigitalMeasurement(unsigned channel,Scope144Measurements *out)
{
    if(!out || channel>1 || !measurement_available || !(digital_ready&(1u<<channel)))return 0;
    *out=digital_measure[channel];return 1;
}

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
/* A/P/D are elapsed cycles inside acquisition calls, measurements and
 * conversion/LCD draw. They include IRQ time, not gaps between async polls. */
static void TimingLine(void)
{
    uint32_t values[3];unsigned i,n=0;char *text=Wildcat144Timing;
    values[0]=Wildcat144Performance.acquisition;values[1]=Wildcat144Performance.processing;
    values[2]=Wildcat144Performance.drawing;
    for(i=0;i<3;++i) {
        text[n++]="APD"[i];text[n++]=' ';
        n=AppendNumber(text,n,(int32_t)(values[i]/CYCLES_MS));
        text[n++]='.';text[n++]=(char)('0'+(values[i]/7200u)%10u);text[n++]=' ';
    }
    memcpy(text+n,"ms",3);
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
static int ReadWord(void *unused,uint32_t *word)
{
    (void)unused;*word=__Read_FIFO();GPIOC->BRR=0x30;return 1;
}
static uint32_t Now(void *unused) { (void)unused; return DWT_CYCLES; }
static int RuntimeCancel(void *unused)
{
    uint32_t sequence=Key1PressCount;(void)unused;
    if((sequence-run_seen)&1u){run_cancelled=sequence;return 1;}
    return 0;
}
static int Cancel(void *unused)
{ return calibrating?Key(KEY4_STATUS):(scope_active?RuntimeCancel(unused):0); }
static int RuntimeConfigure(void *unused)
{
    unsigned interlace=1;(void)unused;
    if(!Configure(&interlace))return 0;
    __Set(TRIGG_MODE,hw_trigger_mode);__Set(V_THRESHOLD,hw_threshold);
    __Set(T_THRESHOLD,hw_time);
    __Set(32+0x11,hw_time>>16);__Set(32+0x12,hw_time>>24);
    /* TRIGG_MODE resets PerCnt. Set both bytes afterwards, explicitly. */
    __Set(32+8,150);__Set(32+9,0);GPIOC->BRR=0x30;return 1;
}
static int RuntimeStop(void *unused)
{
    (void)unused;__Set(TRIGG_MODE,UNCONDITION);__Set(FIFO_CLR,W_PTR);
    GPIOC->BRR=0x30;return 1;
}
static int Settle(unsigned ms)
{
    uint32_t start = DWT_CYCLES;
    while ((uint32_t)(DWT_CYCLES - start) < ms * CYCLES_MS)
        if (calibrating && Key(KEY4_STATUS)) return 0;
    return 1;
}
static void CaptureError(Capture144Status status)
{
    if (status == CAPTURE144_TIMEOUT) Text(140, "FIFO timeout; K1 retries capture");
    else if (status == CAPTURE144_IO) Text(140, "FPGA changed or FIFO error");
    else Number(140, "Capture error: ", status);
}
/* Shared native acquisition state; the main loop calls the service once per
 * frame. Modal loops are used only by the ordinary calibration wizard. */
static Scope144Bank scope_bank;
int Wildcat144_Active(void){return scope_active;}
int Wildcat144_Overview(unsigned *first,unsigned *last)
{
    ViewWindowOverview overview;
    if(first)*first=0;
    if(last)*last=0;
    if(!first || !last || !scope_active || !view_valid || _State.Value!=HOLD ||
        !(frame.capture.flags&CAPTURE_WINDOW_FROZEN) || !Frame144_Valid(&frame) ||
        !ViewWindow_Overview(&display_view,91,216,&overview))return 0;
    *first=overview.first;*last=overview.last;return 1;
}
int Wildcat144_CursorTime(unsigned column,int32_t *ps)
{
    return scope_active && view_valid && Frame144_TimeAt(&frame,&display_view,column,ps);
}
int Wildcat144_XposTime(int32_t *ps)
{
    ViewWindowGeometry origin;int32_t a,b;int64_t value;
    if(!ps || !scope_active)return 0;
    if(view_valid && Frame144_Window(&frame,display_view.render_width,21-_T_base,0,&origin) &&
        Frame144_TimeAt(&frame,&display_view,0,&a) && Frame144_TimeAt(&frame,&origin,0,&b)) {
        *ps=a-b;return 1;
    }
    value=(int64_t)xpos_q16*Scope144_TimePs(65536)/4294967296LL;
    *ps=(int32_t)value;return 1;
}
int Wildcat144_TriggerColumn(void)
{
    uint32_t trigger,column;
    if(!scope_active || !view_valid || !Frame144_Valid(&frame) ||
        !(frame.capture.flags&CAPTURE_WINDOW_HAS_TRIGGER))return 0;
    trigger=frame.capture.trigger_q16;
    if(trigger<display_view.window_start_q16 || trigger>display_view.window_end_q16)return 0;
    column=(trigger-display_view.window_start_q16+display_view.step_q16/2u)/display_view.step_q16;
    return column<display_view.visible_columns?(int)(MIN_X+column):0;
}
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
    __Set(CH_A_OFFSET,Scope144_Command(ADCoffset)); __Set(CH_B_OFFSET,Scope144_Command(ADCoffset));
    __Set(T_BASE_PSC,0); __Set(T_BASE_ARR,0);
}
/* Calibration follows the original Wildcat 6.5 acquisition for BOTH paths:
 * slow separate ADCs and one consecutive 4096-word FIFO record. Routing B
 * to A is an analog switch setting, independent of the ADC/FIFO read mode. */
#define CAL_RECORD_PAIRS 4096u
static CalCaptureStats calibration_stats;
void WildcatCalibration_Diagnostics(CalCaptureStats *stats)
{
    if(stats)*stats=calibration_stats;
}
static int CalibrationInputs(unsigned range,unsigned path,unsigned a,unsigned b)
{
    unsigned separate=0;
    __Set(STANDBY,DN); /* Original setup: analog power and TIM1 sampling enabled. */
    __Set(CH_A_COUPLE,DC);__Set(CH_B_COUPLE,DC);
    __Set(CH_A_RANGE,range);__Set(CH_B_RANGE,path?8:range);
    __Set(CH_A_OFFSET,a);__Set(CH_B_OFFSET,b);
    if(!Configure(&separate))return 0;
    __Set(T_BASE_PSC,X_Attr[_100uS].PSC);
    __Set(T_BASE_ARR,X_Attr[_100uS].ARR);
    return 1;
}
static void CalibrationStart(CalPreview *s,uint32_t now)
{
    s->index=s->sum_a=s->sum_b=s->clips=s->clips_a=s->clips_b=0;
    s->min_a=s->min_b=255;s->max_a=s->max_b=0;
    Start(0);s->record_time=now;s->phase=2;
}
/* 0 pending, 1 full record, -1 setup failure, -2 timeout. The caller owns
 * cancellation and the overall calibration deadline. No static record buffer. */
static int CalibrationRead(CalPreview *s,unsigned range,unsigned path,
    unsigned a,unsigned b,CalMean *mean)
{
    unsigned budget=32;uint16_t pair;uint32_t now=DWT_CYCLES;
    if(!s->phase || s->range!=range || s->path!=path || s->commands[0]!=a || s->commands[1]!=b) {
        memset(s,0,sizeof(*s));s->range=range;s->path=path;s->commands[0]=a;s->commands[1]=b;
        if(!CalibrationInputs(range,path,a,b))return -1;
        s->started=DWT_CYCLES;s->phase=1;s->discard=1;return 0;
    }
    if(s->phase==5) {
        /* Original loop waits 20 ms before clearing W_PTR on the next record. */
        s->started=s->record_time=now;s->phase=4;return 0;
    }
    if((uint32_t)(now-s->started)>=2000u*CYCLES_MS){s->phase=0;return -2;}
    if(s->phase==4) {
        if((uint32_t)(now-s->record_time)<20u*CYCLES_MS)return 0;
        CalibrationStart(s,now);return 0;
    }
    if(s->phase==1) {
        /* Extra initial settling after changing the analog route/offset.
         * Subsequent records do not reconfigure the ADC or the switches. */
        if((uint32_t)(now-s->started)<300u*CYCLES_MS)return 0;
        CalibrationStart(s,now);return 0;
    }
    if(s->phase==2) {
        if((uint32_t)(now-s->record_time)<20u*CYCLES_MS)return 0;
        if((Status(0)&0x28u)!=0x28u)return 0;
        s->phase=3;return 0;
    }
    while(budget-- && s->index<CAL_RECORD_PAIRS) {
        unsigned av,bv;
        /* Exactly the original 4096 __Read_FIFO calls: no extra prime and no
         * discarded interlace history, which would consume FIFO capacity. */
        ReadPair(0,&pair);av=pair&255;bv=pair>>8;
        if(!s->discard) {
            /* Each ADC has its own denominator: one bad A sample must not
             * discard its valid B partner. Preserve all raw diagnostic counts. */
            if(av && av!=255)s->sum_a+=av;
            if(bv && bv!=255)s->sum_b+=bv;
            s->clips+=!av || av==255 || !bv || bv==255;
            s->clips_a+=!av || av==255;s->clips_b+=!bv || bv==255;
            if(av<s->min_a)s->min_a=(uint16_t)av;
            if(av>s->max_a)s->max_a=(uint16_t)av;
            if(bv<s->min_b)s->min_b=(uint16_t)bv;
            if(bv>s->max_b)s->max_b=(uint16_t)bv;
        }
        ++s->index;
    }
    if(s->index<CAL_RECORD_PAIRS)return 0;
    ++s->records;
    if(s->discard){s->discard=0;s->record_time=now;s->phase=4;return 0;}
    s->block_ms=(uint32_t)(DWT_CYCLES-s->record_time)/CYCLES_MS;
    {
        unsigned na=CAL_RECORD_PAIRS-s->clips_a,nb=CAL_RECORD_PAIRS-s->clips_b;
        mean->a=na?(int32_t)((s->sum_a*1024+na/2)/na):0;
        mean->b=nb?(int32_t)((s->sum_b*1024+nb/2)/nb):0;
        /* Only this adapter can certify that bounded exclusions were removed
         * before averaging. CalMean_Valid remains strict for other callers. */
        mean->clips=s->clips_a<=CAL_CAPTURE_MAX_CLIPS && s->clips_b<=CAL_CAPTURE_MAX_CLIPS?0:s->clips;
    }
    s->phase=5;return 1;
}
int WildcatCalibration_Capture(unsigned range,unsigned path,unsigned a,unsigned b,CalMean *mean)
{
    CalPreview record;CalMean last={0,0,0};CalStable stable;
    uint32_t started;int status,result=0;
    memset(&calibration_stats,0,sizeof(calibration_stats));
    memset(&record,0,sizeof(record));memset(&stable,0,sizeof(stable));
    if(!mean || range>=8 || path>1 || a>200 || b>200)return 0;
    *mean=last;calibrating=1;started=DWT_CYCLES;
    for(;;) {
        if(Cancel(0)){result=-1;break;}
        if((uint32_t)(DWT_CYCLES-started)>=2000u*CYCLES_MS) {
            result=calibration_stats.blocks && !CalMean_Valid(&last)?CAL_CAPTURE_CLIPPED:
                calibration_stats.blocks?CAL_CAPTURE_UNSTABLE:0;break;
        }
        status=CalibrationRead(&record,range,path,a,b,&last);
        calibration_stats.records=record.records;
        if(status<0){result=0;break;}
        if(status==1) {
            *mean=last;++calibration_stats.blocks;
            calibration_stats.block_ms=record.block_ms;
            calibration_stats.clips_a=record.clips_a;calibration_stats.clips_b=record.clips_b;
            calibration_stats.min_a=record.min_a;calibration_stats.max_a=record.max_a;
            calibration_stats.min_b=record.min_b;calibration_stats.max_b=record.max_b;
            result=CalStable_Push(&stable,&last,mean);
            calibration_stats.window=stable.count;
            calibration_stats.spread_a=stable.spread_a;calibration_stats.spread_b=stable.spread_b;
            if(result)break;
        }
    }
    calibration_stats.elapsed_ms=(uint32_t)(DWT_CYCLES-started)/CYCLES_MS;
    calibrating=0;return result;
}
int WildcatCalibration_Preview(CalPreview *s,unsigned range,unsigned a,unsigned b,CalMean *mean)
{
    int result;
    if(!s || !mean || range>=8 || a>200 || b>200)return -1;
    result=CalibrationRead(s,range,0,a,b,mean);
    return result<0?-1:result;
}

int Wildcat144_Service(void)
{
    static s16 saved_title[13][4],saved_trigger; static u8 saved_title_flags[13][4],saved_current;
    static u16 saved_max_x,saved_cursor_timer;
    static u8 saved_detail[14];static unsigned saved_level_select;
    static unsigned k2_down,k2_fired; static uint32_t k2_started;
    static unsigned cycle_exit;
    static uint32_t fps_started,fps_frames,fps;
    unsigned i,j,field,detail;
    static unsigned range,base=2,coupling,mode,trigger_kind;
    static unsigned last_range=8,last_coupling=2;
    static int measurement_offset;
    static unsigned position=100,invert=0,probe=1,cursor1=30,cursor2=180,capture_mode=1;
    static int32_t threshold=0; static int held=0,valid=0,redraw=1;
    static u8 saved_flags[16]; static uint32_t saved_demcr,saved_dwt;

    ViewWindowGeometry view,origin;
    unsigned adjust_xpos=0,use_hw,fft_updated=0;
    static uint32_t plotted_epoch;static unsigned plotted_twink;
    uint32_t trigger_mode,trigger_time;
    static Scope144Measurements measure,display_measure;
    static Capture144WordOps ops={&capture_mode,Configure,Start,Status,ReadWord,Now,Cancel};
    static Capture144AsyncOps hw_ops={{0,RuntimeConfigure,Start,Status,ReadWord,Now,RuntimeCancel},RuntimeStop};
    static unsigned exit_coupling;
    static Scope144Cal active_cal;static unsigned cal_status;static int buffer_owned;
    static unsigned display_had_frame;
    if(!scope_active && Title[TRACK1][COUPLE].Value<SCOPE144_DC)return 0;
    if(!scope_active) {
    buffer_owned=0;
    cycle_exit=0;
    exit_coupling=Title[TRACK1][COUPLE].Value==SCOPE144_AC?AC:DC;
    FpgaProfile detected=FpgaProfile_Decode(ReadId(0),ReadId(1),ReadId(2));
    __Set(32+5,0); __Set(32+5,0); GPIOC->BRR=0x30;
    if(detected.kind!=FPGA_W12 || !installed_image_valid) {
        __Clear_Screen(BLACK); Text(160,"144 scope requires installed FPGA W1.2");
        Text(140,"Install bin/FPGAW1_2 image first"); Text(100,"K1 return / K4 cancel");
        ScopeConfirm(); Title[TRACK1][COUPLE].Value=(s16)exit_coupling; App_init(1); Key_Buffer=0; return 1;
    }
    WildcatFpga=detected;
    if((Sweep && _Kind<5) || DownConvertMode || CalibrateMode || ChartLogic() || _Mode==X_Y) {
        __Clear_Screen(BLACK); Text(160,"144: disable sweep/decode/XY first");
        ScopeConfirm(); Title[TRACK1][COUPLE].Value=(s16)exit_coupling; App_init(1); Key_Buffer=0; return 1;
    }
    for(i=0;i<13;++i)for(j=0;j<4;++j) {
        saved_title[i][j]=Title[i][j].Value; saved_title_flags[i][j]=Title[i][j].Flag;
    }
    saved_title[TRACK1][COUPLE]=(s16)exit_coupling;
    memcpy(saved_detail,Detail,sizeof(saved_detail));saved_level_select=TriggerLevelSelect;
    Scope144_SetClock(PPM_Comp);
    saved_trigger=_Vt1;saved_current=Current;saved_max_x=MAX_X;saved_cursor_timer=CursorDisplayTimer;
    saved_flags[0]=FlagMeter; saved_flags[1]=FlagFrameMode; saved_flags[2]=FrameMode;
    saved_flags[3]=OSBuffer; saved_flags[4]=OSAvg; saved_flags[5]=OsBufferLogicFlag;
    saved_flags[6]=ShowFFT; saved_flags[7]=Det; saved_flags[8]=TrgAuto; saved_flags[9]=CurDefTime;
    saved_flags[10]=OsChartFlag;saved_flags[11]=EnableCursorDisplay;saved_flags[12]=DigChLockout;
    saved_flags[13]=FPGAosFlag;saved_flags[14]=AutoFFT;saved_flags[15]=FFTGain;
    range=(_A_Range>=0 && _A_Range<8)?(unsigned)_A_Range:4;
    coupling=exit_coupling;
    if(_T_base>=19 && _T_base<=21)base=21-_T_base;
    mode=_Mode==SGL?2:(_Mode==NORH?1:0);
    trigger_kind=_Tr_kind>=0 && _Tr_kind<=8?(unsigned)_Tr_kind:1;
    position=(_1_posi>=10 && _1_posi<=190)?(unsigned)_1_posi:100;
    probe=_1_source>=3?10:1;invert=_1_source==2 || _1_source==4;
    threshold=0;held=_State.Value!=RUN;valid=0;redraw=1;run_seen=Key1PressCount;run_cancelled=run_seen;k2_down=k2_fired=0;
    measurement_available=0;measurement_offset=ADCoffset;Menu144_Presentation(0);Menu144_TriggerWait(0);
    last_range=8;last_coupling=2;xpos_q16=0;view_valid=0;Frame144_Reset(&frame,++frame_epoch);
    saved_demcr=DEMCR; saved_dwt=DWT_CTRL; DEMCR|=0x01000000u; DWT_CTRL|=1;
    /* Generator keeps ownership of its IRQ/DMA while acquisition changes. */
    { uint32_t before=DWT_CYCLES; volatile unsigned spin;
      for(spin=0;spin<64;++spin) { }
      if(before==DWT_CYCLES)goto scope_cleanup;
    }
    fps_started=DWT_CYCLES;fps_frames=fps=0;Fft144_Reset();
    memset(&Wildcat144Performance,0,sizeof Wildcat144Performance);capture_work=0;TimingLine();
    plotted_epoch=0xffffffffu;plotted_twink=Twink;
    FlagFrameMode=0; FrameMode=0; OSBuffer=0; OSAvg=0;FPGAosFlag=0;
    OsBufferLogicFlag=0; ShowFFT=0; Det=0; CurDefTime=0;
    OsChartFlag=0;EnableCursorDisplay=0;CursorDisplayTimer=0;DigChLockout=0;
    Current=saved_current==TRACK2?TRACK1:saved_current;
    MAX_X=(FlagMeter?300:386)+MIN_X;_X_posi.Flag&=~HID;
    Title[V_VERNIE][V1].Flag&=~HID;Title[V_VERNIE][V2].Flag&=~HID;
    Title[T_VERNIE][T1].Flag&=~HID;Title[T_VERNIE][T2].Flag&=~HID;
    if(_V1_Vernie<1 || _V1_Vernie>199)_V1_Vernie=180;
    if(_V2_Vernie<1 || _V2_Vernie>199)_V2_Vernie=40;
    if(cursor1>=(unsigned)(MAX_X-MIN_X))cursor1=30;
    if(cursor2>=(unsigned)(MAX_X-MIN_X))cursor2=180;
    if(_T1>=0 && _T1<MAX_X-MIN_X)cursor1=(unsigned)_T1;
    if(_T2>=0 && _T2<MAX_X-MIN_X)cursor2=(unsigned)_T2;
    _1_source=1; _2_source=0; _3_source=_3_source==CH_C;
    if(!Scope144_IsFft((unsigned)_4_source))_4_source=_4_source==CH_D;
    Title[TRACK2][COUPLE].Value=DC; _Mode=AUTO;
    if(_Tr_source!=TRACK3 && _Tr_source!=TRACK4)_Tr_source=TRACK1;
    _Tr_kind=3; _X_posi.Value=0;digital_ready=0;display_had_frame=1;
    UiFile_Enter();UiCommon_Status(0);
    memset(&measure,0,sizeof(measure));buffer_owned=1;File144_CalLoad(&scope_bank);
    App_ModeTransition();Key_Buffer=0;scope_active=1;
    UiCommon_ModeTransition();
    }
    {
        unsigned key=Key_Buffer; Key_Buffer=0;
        PD_Cnt=600;
        if(key && Current<13)Title[Current][Detail[Current]<4?Detail[Current]:0].Flag|=UPDAT;
        /* Only newly published presses toggle. A resume press still down
         * must not cancel the acquisition it just started. */
        if(Scope144_RunPresses(Key1PressCount,&run_seen,&held))redraw=1;
        if(hw_session.stage && held) {
            Capture144_Abort(&hw_session);valid=0;held=1;redraw=2;
            measurement_available=0;
        }
        if(key==KEY1)redraw=1;
        if(UiCommon_ValueKey(key,1)){redraw=1;key=0;}
        if(Current==OUTPUT && key!=K_ITEM_S && Generator_Key(key,1)) {
            Generator_Reconcile();redraw=1;key=0;
        }
        {int action=UiFile_Key(key,1);
         if(action) {
            if(action==2) {
                int status;
                if(Title[FILE][0].Value==LOAD) {
                    if(hw_session.stage){Capture144_Abort(&hw_session);valid=0;Frame144_Reset(&frame,++frame_epoch);}
                    status=UiFile_Load();
                }
                else if(Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CSV)
                    status=valid?File144_CsvFrame(&frame,base,(unsigned)Title[FILE][1].Value,invert?-(int)probe:(int)probe):VER_ERR;
                else status=UI_UNAVAILABLE;
                UiFile_Result(status);
            }
            redraw=1;key=0;
         }}
        if(key==KEY3 && Current!=FILE) {
            while(Key(KEY3_STATUS)) { }
            FlagMeter=(FlagMeter+1)%3;
            if(FlagMeter==2 && Current>=METER_0) {
                if(Current>METER_3)Current=METER_3;
                if(UpdateMeter==4 && Current==METER_1)Current=METER_2;
                if(UpdateMeter!=4 && Current==METER_0)Current=METER_1;
            }
            MAX_X=(FlagMeter?300:386)+MIN_X;
            if(cursor1>=(unsigned)(MAX_X-MIN_X))cursor1=MAX_X-MIN_X-1;
            if(cursor2>=(unsigned)(MAX_X-MIN_X))cursor2=MAX_X-MIN_X-1;
            adjust_xpos=1;
            if(!FlagMeter && (Current>=METER_0 || Current==BK_LIGHT || Current==VOLUME))Current=TRACK1;
            Clear_Meter_Area();redraw=2;key=0;Key_Buffer=0;
        }
        if(key==K_ITEM_INC || key==K_ITEM_DEC) {
            Current=(u8)Scope144_VisibleGroupStep(Current,key==K_ITEM_INC?1:-1,FlagMeter,UpdateMeter);
            redraw=1;
        }
        /* Detail has 14 entries; meter groups 13..21 have no submenu. */
        detail=Current<13?Detail[Current]:Detail[13];
        if(detail>=Scope144_DetailLimit(Current))detail=0;
        if(key==K_ITEM_S && Current!=FILE) { cycle_exit=1;goto scope_cleanup; }
        if(key==KEY4 || key==K_ITEM_S) {
            while(key==KEY4 && Key(KEY4_STATUS)) { }
            Key_Buffer=0;
            if(Current==TRACK4)detail=detail==0?(Scope144_IsFft((unsigned)_4_source)?RANGE:POSI):0;
            else if(UiCommon_NextDetail(1))detail=Detail[Current];
            else detail=Current==TRIGG?TriggerMenu_Next(detail,_Tr_source==TRACK1,0):
                (detail+1)%Scope144_DetailLimit(Current);
            redraw=1;
        }
        if(Current<13)Detail[Current]=(u8)detail;else Detail[13]=(u8)detail;
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
                        Capture144_Abort(&hw_session);
                        measurement_available=0;
                        valid=0;Frame144_Reset(&frame,++frame_epoch);Fft144_Reset();ShowFFT=0;
                        scope_active=0;Key_Buffer=0;Calibrat(TRACK1);scope_active=1;
                        File144_CalLoad(&scope_bank);Scope144_SetClock(PPM_Comp);run_seen=Key1PressCount;
                        valid=0;last_range=8;last_coupling=2;redraw=2;
                    }
                }
                if(!pressed) {
                    if(!k2_fired) {
                        if(_Tr_source==TRACK1) {
                            if(elapsed<500u*CYCLES_MS) {
                                TrgAuto=(u8)TriggerMenu_K2(TrgAuto,0);
                                if(Current==TRIGG && Detail[TRIGG]==2)TriggerLevelSelect=1;
                            } else {TrgAuto=0;Current=TRIGG;Detail[TRIGG]=2;TriggerLevelSelect=0;}
                        }
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
                coupling=1-coupling;valid=0;break;
            }
            case 0:range=(range+8+delta)%8;valid=0;memset(&measure,0,sizeof(measure));break;
            case 1:base=(base+3+delta)%3;adjust_xpos=1;break;
            case 2: {
                int64_t current=xpos_q16,next;
                if(valid && Frame144_Window(&frame,(unsigned)(MAX_X-MIN_X),base,0,&origin) &&
                    Frame144_Window(&frame,(unsigned)(MAX_X-MIN_X),base,xpos_q16,&view))
                    current=(int64_t)view.window_start_q16-origin.window_start_q16;
                next=current+delta*(int64_t)Scope144_Step(base);
                if(next<-(8192LL*65536))next=-(8192LL*65536);
                if(next>8192LL*65536)next=8192LL*65536;
                xpos_q16=(int32_t)next;adjust_xpos=1;break;
            }
            case 3: {
                if(_Tr_source!=TRACK1)break;
                if(TriggerLevelSelect) {
                    TrgAuto=(u8)TriggerMenu_LevelStep(TrgAuto,delta);
                    if(TrgAuto && measurement_available) {
                        int32_t low=invert?-measure.maximum_uv:measure.minimum_uv;
                        int32_t high=invert?-measure.minimum_uv:measure.maximum_uv;
                        threshold=TriggerMenu_AutoLevel(low,high,TrgAuto)*probe;
                    }
                    break;
                }
                TrgAuto=0;
                int64_t next=(int64_t)threshold+delta*(int64_t)Y_Attr[range].SCALE*probe;
                if(next>=-1000000000 && next<=1000000000)threshold=(int32_t)next;
                break;
            }
            case 4:trigger_kind=(trigger_kind+9+delta)%9;break;
            case 5:mode=(mode+3+delta)%3;break;
            case 6:
                if(Current==TRACK4 && Scope144_IsFft((unsigned)_4_source))break;
                if(Current==TRACK3 || Current==TRACK4) {
                    s16 *p=&Title[Current][POSI].Value;
                    if((*p<179 && delta>0)||(*p>1 && delta<0))*p+=delta;
                } else if((delta>0 && position<190)||(delta<0 && position>10))position+=delta;
                break;
            case 46:
                if(Current==TRACK4) {
                    _4_source=(s16)Scope144_DSourceStep((unsigned)_4_source,delta);
                    Fft144_Reset();ShowFFT=0;
                } else Title[Current][SOURCE].Value=1-Title[Current][SOURCE].Value;
                break;
            case 48:
                if(Current==TRACK4 && Scope144_IsFft((unsigned)_4_source))
                    Title[TRACK4][RANGE].Value=(Title[TRACK4][RANGE].Value+10+delta)%10;
                break;
            case 31:
                if(_Tr_source==TRACK1)_Tr_source=delta>0?TRACK3:TRACK4;
                else if(_Tr_source==TRACK3)_Tr_source=delta>0?TRACK4:TRACK1;
                else _Tr_source=delta>0?TRACK1:TRACK3;
                break;
            case 8:if((delta>0 && cursor1+1u<(unsigned)(MAX_X-MIN_X))||(delta<0 && cursor1>0))cursor1+=delta;break;
            case 9:if((delta>0 && cursor2+1u<(unsigned)(MAX_X-MIN_X))||(delta<0 && cursor2>0))cursor2+=delta;break;
            case 35:case 36: {
                s16 *v=&Title[V_VERNIE][field==35?V1:V2].Value;
                if((*v>1 && delta<0)||(*v<199 && delta>0))*v+=delta;
                break;
            }
            case 39:Title[T_VERNIE][2].Value=1-Title[T_VERNIE][2].Value;break;
            case 41: {
                meter *m=&Meter[Current-METER_0];m->Item=(m->Item+12+delta)%12;
                if(m->Item<=FPS)m->Track=4;
                else if(m->Track!=TRACK3 && m->Track!=TRACK4)m->Track=TRACK1;
                break;
            }
            case 47: {
                meter *m=&Meter[Current-METER_0];
                if(m->Item<=FPS)break;
                if(m->Track==TRACK1)m->Track=delta>0?TRACK3:TRACK4;
                else if(m->Track==TRACK3)m->Track=delta>0?TRACK4:TRACK1;
                else m->Track=delta>0?TRACK1:TRACK3;
                break;
            }
            default:break;
            }
            redraw=1;
        }
        field=Scope144_Field(Current,detail);
        _A_Range=(s16)range; Title[TRACK1][COUPLE].Value=(s16)(coupling==AC?SCOPE144_AC:SCOPE144_DC);
        _1_source=(probe==10?3:1)+invert;
        _1_posi=(s16)position; _T_base=(s16)(21-base);
        _Mode=mode==2?SGL:(mode==1?NORH:AUTO);_Tr_kind=(s16)trigger_kind;

        _T1=(s16)cursor1;_T2=(s16)cursor2;_State.Value=held?HOLD:RUN;
        Title[V_VERNIE][2].Value=TRACK1;
        { int32_t marker=(int32_t)position+(threshold/(int32_t)probe)/(int32_t)Y_Attr[range].SCALE;
          if(marker<1)marker=1;
          if(marker>199)marker=199;
          _Vt1=(s16)marker;
        }
        use_hw=mode || trigger_kind>=2;
        trigger_mode=Scope144_TriggerMode((unsigned)_Tr_source,trigger_kind,invert);
        trigger_time=Scope144_TriggerTicks(base,cursor1,cursor2);
        if(hw_session.stage && (!use_hw || range!=hw_range || coupling!=hw_coupling ||
            (hw_session.stage==1 && hw_session.wait_timeout!=(mode?2000u:100u)*CYCLES_MS) ||
            (unsigned)_Tr_source!=hw_source ||
            ADCoffset!=hw_offset || threshold!=hw_level || probe!=hw_probe ||
            invert!=hw_invert || trigger_kind!=hw_kind || trigger_mode!=hw_trigger_mode ||
            (trigger_kind>=4 && trigger_kind<=7 && trigger_time!=hw_time))) {
            Capture144_Abort(&hw_session);valid=0;
        }
        if(!hw_session.stage)ScopeInputs(range,coupling);
        if(range!=last_range || coupling!=last_coupling) {
            measurement_available=0;valid=0;Fft144_Reset();ShowFFT=0;
            if(!Settle(120))goto scope_cleanup;
            last_range=range;last_coupling=coupling;
        }
        if(!hw_session.stage && (!held || !valid))
            cal_status=Scope144_Select(&scope_bank,range,ADCoffset,__Get(V_BATTERY),&active_cal);
        if(ADCoffset!=measurement_offset) {
            measurement_available=0;valid=0;measurement_offset=ADCoffset;Fft144_Reset();ShowFFT=0;
        }
        if(!cal_status){measurement_available=0;valid=0;Fft144_Reset();ShowFFT=0;Frame144_Reset(&frame,frame_epoch);memset(&measure,0,sizeof(measure));}
        /* A changed input cannot reinterpret a frozen record with a different
         * analog range/coupling. Wait for RUN to acquire a new epoch. */
        if(!valid && !hw_session.stage)CaptureWindow_Reset(&frame.capture,frame_epoch,CAPTURE_WINDOW_EMPTY);
        if(!held && cal_status) {
            Capture144Status capture;
            unsigned wait_timeout=0;uint32_t phase_started=DWT_CYCLES;
            if(!hw_session.stage)capture_work=0;
            if(!hw_session.stage) {
                valid=0;Frame144_Reset(&frame,++frame_epoch);
                frame.words=(const uint32_t *)DataBuf;frame.calibration=active_cal;frame.range=range;
                frame.trigger_source=(unsigned)_Tr_source;
            }
            if(use_hw) {
                if(!hw_session.stage) {
                    if(_Tr_source==TRACK1 && !Frame144_AdcThresholdKind(&active_cal,(invert?-threshold:threshold)/(int32_t)probe,
                        trigger_mode&7u,&hw_threshold)) {
                        held=1;capture=CAPTURE144_ARGUMENT;
                        Text(140,"Trigger level outside ADC range");
                    } else {
                        hw_source=(unsigned)_Tr_source;
                        hw_kind=trigger_kind;hw_trigger_mode=trigger_mode;hw_time=trigger_time;
                        hw_range=range;hw_coupling=coupling;
                        hw_offset=ADCoffset;hw_level=threshold;hw_probe=probe;hw_invert=invert;
                        capture=Capture144_Begin(&hw_session,&WildcatFpga,&hw_ops,(uint32_t *)DataBuf,
                            frame_epoch,Scope144_TimePs(65536),(mode?2000u:100u)*CYCLES_MS,1000u*CYCLES_MS,&frame.capture);
                        if(capture==CAPTURE144_PENDING && hw_source>=TRACK3 &&
                            !Capture144_DigitalTrigger(&hw_session,hw_source-TRACK3,frame.digital_shift_q16))
                            capture=Capture144_Abort(&hw_session);
                        if(capture==CAPTURE144_PENDING && !Capture144_TriggerKind(&hw_session,hw_kind))
                            capture=Capture144_Abort(&hw_session);
                    }
                } else capture=CAPTURE144_PENDING;
                if(capture==CAPTURE144_PENDING) {
                    unsigned waiting=hw_session.stage==1;
#ifdef GENERATOR_DIAGNOSTICS
                    uint32_t poll_started=DWT_CYCLES;
#endif
                    capture=Capture144_Poll(&hw_session,512);
                    wait_timeout=waiting && capture==CAPTURE144_TIMEOUT;
#ifdef GENERATOR_DIAGNOSTICS
                    Generator_PollSample(poll_started,DWT_CYCLES-poll_started,capture==CAPTURE144_TIMEOUT);
#endif
                    /* AUTO still shows a new record when no event matches.
                     * Only a no-trigger wait may fall back, never a failed
                     * transfer. The fallback has no invented trigger flag. */
                    if(capture==CAPTURE144_TIMEOUT && !mode && waiting)
                        capture=Capture144_RecordWindow(&WildcatFpga,&ops,(uint32_t *)DataBuf,4096,
                            1000u*CYCLES_MS,frame_epoch,Scope144_TimePs(65536),&frame.capture);
                }
            } else capture=Capture144_RecordWindow(&WildcatFpga,&ops,(uint32_t *)DataBuf,4096,
                1000u*CYCLES_MS,frame_epoch,Scope144_TimePs(65536),&frame.capture);
            capture_work+=(uint32_t)(DWT_CYCLES-phase_started);
            Wildcat144Performance.acquisition=capture_work;
            if(capture!=CAPTURE144_OK && capture!=CAPTURE144_PENDING) {
                valid=0;
                /* K1 can arrive inside Poll. Consume that press once, so its
                 * queued event cannot toggle HOLD back to RUN on the next pass. */
                if(capture==CAPTURE144_CANCELLED){run_seen=run_cancelled;redraw=1;}
                if(!(mode==1 && wait_timeout)) {
                    measurement_available=0;memset(&measure,0,sizeof(measure));
                }
                /* NORM re-arms on the next main-loop pass after a bounded
                 * no-edge wait. SGL stops and K1 explicitly retries. */
                if(!(mode==1 && capture==CAPTURE144_TIMEOUT))held=1;
                if(capture!=CAPTURE144_CANCELLED && capture!=CAPTURE144_ARGUMENT &&
                    !(mode==1 && wait_timeout))CaptureError(capture);
            } else if(capture==CAPTURE144_OK) {
                phase_started=DWT_CYCLES;
                if(!Frame144_Measure(&frame,&measure))goto scope_cleanup;
                measurement_available=1;
                digital_ready=0;
                for(i=0;i<2;++i)if(Frame144_DigitalMeasure(&frame,i,&digital_measure[i]))digital_ready|=1u<<i;
                ++fps_frames;
                if(TrgAuto && _Tr_source==TRACK1) {
                    int32_t low=invert?-measure.maximum_uv:measure.minimum_uv;
                    int32_t high=invert?-measure.minimum_uv:measure.maximum_uv;
                    threshold=TriggerMenu_AutoLevel(low,high,TrgAuto)*probe;
                }
                if(frame.capture.trigger_kind>=CAPTURE_TRIGGER_HARDWARE_A) {
                    if(!Frame144_HardwareView(&frame,(unsigned)(MAX_X-MIN_X),base,&view))goto scope_cleanup;
                    valid=1;if(mode==2)held=1;
                } else if(trigger_kind<2 && (_Tr_source>=TRACK3?
                    Frame144_DigitalTrigger(&frame,(unsigned)(MAX_X-MIN_X),base,(unsigned)_Tr_source-TRACK3,trigger_kind==0,&view):
                    Frame144_Trigger(&frame,(unsigned)(MAX_X-MIN_X),base,
                    (invert?-threshold:threshold)/(int32_t)probe,(trigger_kind==0)^invert,&view))) {
                    valid=1; if(mode==2)held=1;
                } else if(mode==0)valid=1;
                else valid=0;
                Wildcat144Performance.processing=(uint32_t)(DWT_CYCLES-phase_started);
            }
        }
        _State.Value=held?HOLD:RUN;
        Menu144_Calibration(cal_status);
        Menu144_TriggerWait(hw_session.stage);
        Menu144_Presentation(measurement_available);
        view_valid=0;
        if(!valid && display_had_frame && (!measurement_available || held || adjust_xpos)) {
            Process144_ClearFrame();display_had_frame=0;redraw=1;
        }
        if(valid) {
            if(!Frame144_Window(&frame,(unsigned)(MAX_X-MIN_X),base,xpos_q16,&view))goto scope_cleanup;
            if(held || adjust_xpos) {
                if(!Frame144_Window(&frame,(unsigned)(MAX_X-MIN_X),base,0,&origin))goto scope_cleanup;
                xpos_q16=(int32_t)((int64_t)view.window_start_q16-origin.window_start_q16);
            }
            display_view=view;view_valid=1;
            frame.capture.first_visible_q16=view.window_start_q16;
            _X_posi.Value=(s16)(xpos_q16/65536);
            if(held)frame.capture.flags|=CAPTURE_WINDOW_FROZEN;
            else frame.capture.flags &= ~CAPTURE_WINDOW_FROZEN;
            Menu144_Cursor(base);
            if(Scope144_IsFft((unsigned)_4_source) && frame.capture.time_samples>=512) {
                unsigned first=view.window_start_q16>0?(unsigned)((uint64_t)view.window_start_q16/65536u):0;
                if(first>frame.capture.time_samples-512u)first=frame.capture.time_samples-512u;
                fft_updated=Fft144_Update(&frame,first,(unsigned)_4_source,(unsigned)Title[TRACK4][RANGE].Value,DWT_CYCLES);
                ShowFFT=(u8)Fft144.ready;Fft144_Labels((unsigned)_T1);
            } else {Fft144.enabled=0;ShowFFT=0;}
            if(!held || redraw || adjust_xpos || plotted_epoch!=frame.capture.epoch || plotted_twink!=Twink || fft_updated) {
                uint32_t drawing_started=DWT_CYCLES;
                Process144_DrawFrame(&frame,&view,position,invert);
                Wildcat144Performance.drawing=(uint32_t)(DWT_CYCLES-drawing_started);
                ++Wildcat144Performance.draws;plotted_epoch=frame.capture.epoch;plotted_twink=Twink;
            }
            TimingLine();
            display_had_frame=1;
            /* Draw_Window renders T1/T2 and V1/V2 using the shared Title values. */
        } else if(!cal_status) {
            Text(140,"Range not calibrated: hold K2 on A");
            Text(120,"Right: menu; right press: item; left +/-");
            Text(100,"K1 RUN/HOLD; K4 next item; K3 saves CSV");
            Text(80,"Next scan mode: right rocker press");
        }
        if(!valid)Menu144_Cursor(base);
        {
            int32_t dt=0,dv;
            if(view_valid && !Frame144_TimeDelta(&frame,&display_view,cursor1,cursor2,&dt))goto scope_cleanup;
            if(!view_valid && measurement_available)
                dt=Scope144_CursorTime(base,cursor2)-Scope144_CursorTime(base,cursor1);
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
    view_valid=0;Fft144_Reset();ShowFFT=0;
    measurement_available=0;Menu144_Presentation(0);Menu144_TriggerWait(0);
    Capture144_Abort(&hw_session);
    Frame144_Reset(&frame,++frame_epoch);
    /* Erase only the old waveform before restoring Normal display state. */
    if(buffer_owned)Process144_ClearFrame();
    scope_active=0;
    saved_title[TRACK1][COUPLE]=(s16)coupling;
    saved_title[TRACK1][RANGE]=(s16)range;saved_title[TRACK1][POSI]=(s16)position;
    saved_title[TRACK1][SOURCE]=(s16)((probe==10?3:1)+invert);
    if(saved_title[T_BASE][MODE]!=SCAN)
        saved_title[T_BASE][MODE]=(s16)(mode==2?SGL:(mode==1?NORH:AUTO));
    saved_title[RUNNING][STATE]=(s16)(held?HOLD:RUN);
    saved_title[T_VERNIE][T1]=(s16)cursor1;saved_title[T_VERNIE][T2]=(s16)cursor2;
    saved_title[V_VERNIE][V1]=_V1_Vernie;saved_title[V_VERNIE][V2]=_V2_Vernie;
    for(i=BK_LIGHT;i<=VOLUME;++i)saved_title[i][1]=Title[i][1].Value;
    for(j=0;j<4;++j)saved_title[OUTPUT][j]=Title[OUTPUT][j].Value;
    if(UiFile_Changed()) {
        for(j=0;j<4;++j)saved_title[FILE][j]=Title[FILE][j].Value;
        saved_detail[FILE]=Detail[FILE];
    }
    saved_flags[0]=FlagMeter;saved_max_x=MAX_X;
    if(!FlagMeter && (saved_current>=METER_0 || saved_current==BK_LIGHT || saved_current==VOLUME))saved_current=TRACK1;
    Scope144_SetClock(0);
    __Set(ADC_CTRL,1); __Set(ADC_MODE,SEPARATE); GPIOC->BRR=0x30;
    for(i=0;i<13;++i)for(j=0;j<4;++j) {
        Title[i][j].Value=saved_title[i][j];Title[i][j].Flag=saved_title_flags[i][j];
    }
    _Vt1=saved_trigger;Current=saved_current;MAX_X=saved_max_x;CursorDisplayTimer=saved_cursor_timer;
    memcpy(Detail,saved_detail,sizeof(saved_detail));TriggerLevelSelect=saved_level_select;
    /* These are temporary edit cursors, reset by ordinary navigation too. */
    OffsetSelect=0;OS_RateSelect=0;
    FlagMeter=saved_flags[0]; FlagFrameMode=saved_flags[1]; FrameMode=saved_flags[2];
    OSBuffer=saved_flags[3]; OSAvg=saved_flags[4]; OsBufferLogicFlag=saved_flags[5];
    ShowFFT=saved_flags[6]; Det=saved_flags[7]; CurDefTime=saved_flags[9];
    OsChartFlag=saved_flags[10];EnableCursorDisplay=saved_flags[11];DigChLockout=saved_flags[12];
    FPGAosFlag=saved_flags[13];AutoFFT=saved_flags[14];FFTGain=saved_flags[15];
    if(cycle_exit) {
        FlagFrameMode=0;FrameMode=0;
        OSBuffer=0;OSAvg=0;OsBufferLogicFlag=0;FPGAosFlag=0;
    }
    DWT_CTRL=saved_dwt; DEMCR=saved_demcr;

    if(buffer_owned)Process144_ReleaseBuffer();
    App_ModeTransition();buffer_owned=0;Update_Range(); Update_Base();
    Update_Trig(_Tr_source==4?0:1);
    UiCommon_ModeTransition();

    while(Key(KEY1_STATUS)||Key(KEY2_STATUS)||Key(KEY3_STATUS)||Key(KEY4_STATUS)||Key(K_ITEM_S_STATUS)) { }
    Key_Buffer=0;
    return 1;
}
int Wildcat144_ModeKey(void)
{
    /* Called only at the wrap point of the ordinary buffer-mode cycle. */
    if(Key_Buffer==K_ITEM_S && FlagFrameMode && OSBuffer && !OSAvg &&
       (!FPGAsubVer || FPGAosFlag) &&
       WildcatFpga.kind==FPGA_W12 &&
       (Title[TRACK1][COUPLE].Value==DC || Title[TRACK1][COUPLE].Value==AC)) {
        Title[TRACK1][COUPLE].Value=Title[TRACK1][COUPLE].Value==AC?SCOPE144_AC:SCOPE144_DC;
        while(Key(K_ITEM_S_STATUS)) { }
        Key_Buffer=0;return 1;
    }
    return 0;
}
