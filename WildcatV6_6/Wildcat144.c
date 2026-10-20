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
/* Completed metadata never doubles as the pending FIFO destination.
 * There is still exactly one DataBuf. ReadWord revokes the completed view
 * before a pending transfer can overwrite its first byte. */
static Frame144 frame,pending_frame;
static unsigned raw_writing,fallback_active;
static int held; /* Requested state; stage 2 finishes before it becomes HOLD. */
static ViewWindowGeometry display_view;
static int view_valid;
static unsigned overview_valid;
/* User time displacement in Q16 interlace samples. Acquisition/clamping
 * never writes this request. User edits alone apply the shared physical stops.
 * display_view alone owns the achieved viewport. */
static int32_t requested_xpos_q16;
/* Last complete presentation anchor; geometry only, never raw-data ownership. */
static uint32_t xpos_reference_q16;
static uint32_t frame_epoch;
static Capture144Session hw_session;
static unsigned hw_threshold,hw_kind,hw_range,hw_coupling,hw_probe,hw_invert;
static uint32_t hw_trigger_mode,hw_time,hw_geometry;
static int32_t hw_level;
static int hw_offset;
static unsigned hw_position;
static unsigned hw_source;
static Scope144Measurements digital_measure[2];
static unsigned digital_ready,digital_computed;
static uint32_t digital_epoch;
/* Last complete measurements survive cooperative re-arming. Raw DataBuf and
 * view_valid still describe only the current, completely transferred epoch. */
static unsigned measurement_available;
static unsigned analog_time_ready;
static int scope_active;
static uint32_t run_seen;
/* Flash is not modified by Wildcat. Cache only the startup verdict, never the
 * CRC or the bitstream; a missing/failed check leaves 144 disabled. */
static u8 installed_image_valid;
void Wildcat144_CheckImage(void)
{
    installed_image_valid=WildcatFpga.kind==FPGA_W12 &&
        Scope144_Crc((const void *)0x0805c800u,135096)==SCOPE144_IMAGE_CRC;
}
/* Only visible time/frequency meters consume full C/D statistics. Digital
 * traces and both trigger engines read their own data independently. */
static unsigned DigitalDemand(void)
{
    unsigned mask=UiCommon_TimeDemand(0)>>2;
    if(_3_source!=1)mask&=~1u;
    if(_4_source!=1)mask&=~2u;
    return mask;
}
static void DigitalUpdate(unsigned mask)
{
    mask&=~digital_computed;
    /* DataBuf may already belong to the next, incomplete acquisition. Never
     * re-read it for an uncomputed metric of the previous completed frame. */
    if(!mask || !measurement_available || frame.capture.epoch!=digital_epoch || !Frame144_Valid(&frame))return;
    digital_ready|=Frame144_DigitalMeasurePair(&frame,mask&1u?&digital_measure[0]:0,
        mask&2u?&digital_measure[1]:0);
    digital_computed|=mask; /* A complete record with no period is cached too. */
}
int Wildcat144_DigitalMeasurement(unsigned channel,Scope144Measurements *out)
{
    unsigned bit;
    if(!out || channel>1 || !measurement_available)return 0;
    bit=1u<<channel;
    if(!(digital_computed&bit))DigitalUpdate(DigitalDemand()|bit);
    if(!(digital_ready&bit))return 0;
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
    (void)unused;
    if(scope_active && !calibrating && (hw_session.stage==2 || fallback_active)) {
        raw_writing=1;frame.capture.status=CAPTURE_WINDOW_ARMED;
        if(fallback_active)fallback_active=2;
    }
    *word=__Read_FIFO();GPIOC->BRR=0x30;return 1;
}
static uint32_t Now(void *unused) { (void)unused; return DWT_CYCLES; }
static int RuntimeCancel(void *unused)
{
    (void)unused;
    Scope144_RunPresses(Key1PressCount,&run_seen,&held);
    /* K1 cancels waiting, but a partly overwritten DataBuf must finish.
     * Other explicit abort paths remain independent of this callback. */
    return held && hw_session.stage!=2 && fallback_active!=2;
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
    __Set(32+8,hw_session.pretrigger & 255u);__Set(32+9,hw_session.pretrigger>>8);GPIOC->BRR=0x30;return 1;
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
    ViewWindowGeometry axis;ViewWindowOverview overview;
    if(first)*first=0;
    if(last)*last=0;
    /* The last completed geometry is safe metadata during the next transfer;
     * it never licenses reads of old or partially overwritten FIFO samples.
     * A common capacity axis avoids jumps from HW vs fallback discard counts. */
    if(!first || !last || !scope_active || !overview_valid || Scope144_IsFft((unsigned)_4_source))return 0;
    axis=display_view;axis.capture_count=FRAME144_BUFFER_SAMPLES;
    if(!ViewWindow_Overview(&axis,91,216,&overview))return 0;
    *first=overview.first;*last=overview.last;return 1;
}
int Wildcat144_CursorTime(unsigned column,int32_t *ps)
{
    return scope_active && view_valid && Frame144_TimeAt(&frame,&display_view,column,ps);
}
int Wildcat144_XposTime(int32_t *ps)
{
    int64_t value;uint64_t period;
    if(!ps || !scope_active)return 0;
    period=view_valid?frame.capture.sample_period_ps_q16:(uint64_t)Scope144_TimePs(65536);
    value=(int64_t)requested_xpos_q16*period/4294967296LL;
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
static void ScopeInputs(unsigned range,unsigned coupling,const unsigned commands[2])
{
    UiCommon_AnalogSet(CH_A_COUPLE,coupling); UiCommon_AnalogSet(CH_B_COUPLE,DC);
    UiCommon_AnalogSet(CH_A_RANGE,range); UiCommon_AnalogSet(CH_B_RANGE,8); /* Both ADCs sample A. */
    UiCommon_AnalogSet(CH_A_OFFSET,commands[0]); UiCommon_AnalogSet(CH_B_OFFSET,commands[1]);
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
    UiCommon_AnalogReset();
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

int __attribute__((optimize("Os"))) Wildcat144_Service(void)
{
    static s16 saved_title[13][4],saved_trigger; static u8 saved_title_flags[13][4],saved_current;
    static u16 saved_max_x,saved_cursor_timer;
    static u8 saved_detail[14];static unsigned saved_level_select;
    static unsigned k2_down,k2_fired; static uint32_t k2_started;
    static u8 file_return_current;
    static unsigned cycle_exit,k3_down,k3_fired;static uint32_t k3_started;
    unsigned i,j,field,detail;
    static unsigned range,base=2,coupling,mode,trigger_kind;
    static unsigned last_range=8,last_coupling=2;
    static int measurement_offset,measurement_ppm;
    static unsigned input_commands[2],last_commands[2];
    static unsigned position=100,invert=0,probe=1,cursor1=30,cursor2=180,capture_mode=1;
    static int32_t threshold=0; static int valid=0,redraw=1;
    static u8 saved_flags[16]; static uint32_t saved_demcr,saved_dwt;

    ViewWindowGeometry view;
    unsigned adjust_xpos=0,use_hw,pretrigger,geometry,fft_updated=0;
    static uint32_t plotted_epoch;static unsigned plotted_twink;
    uint32_t trigger_mode,trigger_time;
    static Scope144Measurements measure,display_measure;
    static Capture144WordOps ops={&capture_mode,Configure,Start,Status,ReadWord,Now,Cancel};
    static Capture144AsyncOps hw_ops={{0,RuntimeConfigure,Start,Status,ReadWord,Now,RuntimeCancel},RuntimeStop};
    static unsigned exit_coupling;
    static Scope144Cal active_cal;static unsigned cal_status,runtime_ready;static int buffer_owned;
    static unsigned display_had_frame,ypos_trace;
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
    position=(unsigned)UiCommon_XposBound(_1_posi,0,0,Title[TRACK1][POSI].Limit);
    probe=_1_source>=3?10:1;invert=_1_source==2 || _1_source==4;
    threshold=(int32_t)(saved_trigger-(int)position)*Y_Attr[range].SCALE*probe;held=_State.Value!=RUN;valid=0;redraw=1;run_seen=Key1PressCount;raw_writing=fallback_active=0;k2_down=k2_fired=k3_down=k3_fired=0;OffsetSelect=0;
    last_commands[0]=last_commands[1]=UINT32_MAX;ypos_trace=0;
    measurement_available=0;measurement_offset=ADCoffset;measurement_ppm=PPM_Comp;Menu144_Presentation(0);Menu144_TriggerWait(0);
    last_range=8;last_coupling=2;requested_xpos_q16=0;xpos_reference_q16=0;display_view.capture_count=0;
    view_valid=0;overview_valid=0;Frame144_Reset(&frame,++frame_epoch);
    saved_demcr=DEMCR; saved_dwt=DWT_CTRL; DEMCR|=0x01000000u; DWT_CTRL|=1;
    /* Generator keeps ownership of its IRQ/DMA while acquisition changes. */
    { uint32_t before=DWT_CYCLES; volatile unsigned spin;
      for(spin=0;spin<64;++spin) { }
      if(before==DWT_CYCLES)goto scope_cleanup;
    }
    Fft144_Reset();
    plotted_epoch=0xffffffffu;plotted_twink=Twink;
    FlagFrameMode=0; FrameMode=0; OSBuffer=0; OSAvg=0;FPGAosFlag=0;
    OsBufferLogicFlag=0; ShowFFT=0; Det=0; CurDefTime=0;
    OsChartFlag=0;EnableCursorDisplay=0;CursorDisplayTimer=0;DigChLockout=0;
    Current=saved_current==TRACK2?TRACK1:saved_current;
    file_return_current=Current==FILE?TRACK1:Current;
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
    _Tr_kind=3; _X_posi.Value=0;digital_ready=digital_computed=0;digital_epoch=0;display_had_frame=1;
    UiFile_Enter();UiCommon_Status(0);
    memset(&measure,0,sizeof(measure));buffer_owned=1;File144_CalLoad(&scope_bank);Scope144_NativeBank(&scope_bank);
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
        if(hw_session.stage==1 && held) {
            Capture144Status stopped=Capture144_Abort(&hw_session);
            valid=Frame144_Valid(&frame);
            if(!valid)measurement_available=0;
            if(stopped==CAPTURE144_IO)CaptureError(stopped);
        }
        if(key==KEY1)redraw=1;
        if(key && key!=KEY2 && key!=K_INDEX_S && key!=K_INDEX_INC && key!=K_INDEX_DEC && OffsetSelect) {
            OffsetSelect=0;redraw=2;
        }
        if(UiCommon_ServiceKey(key)){redraw=1;key=0;}
        /* In 144 the middle-duration standby action is deliberately disabled. */
        if((key==KEY3 && Current!=FILE) || k3_down) {
            unsigned pressed=Key(KEY3_STATUS);uint32_t now=DWT_CYCLES;
            if(!k3_down){k3_down=1;k3_fired=0;k3_started=now;}
            if(pressed && !k3_fired && (uint32_t)(now-k3_started)>=UI_K3_CONFIG_MS*CYCLES_MS) {
                k3_fired=1;file_return_current=Current;Current=FILE;Title[FILE][0].Value=SAVE;
                Title[FILE][1].Value=ConfigFileNumber;Title[FILE][2].Value=CFG;
                Detail[FILE]=1;UiFile_Enter();redraw=2;
            }
            key=0;
            if(!pressed) {
                if(!k3_fired && (uint32_t)(now-k3_started)<UI_KEY_SHORT_MS*CYCLES_MS)key=KEY3;
                k3_down=0;
            }
        }
        if(UiCommon_ValueKey(key,1)){redraw=1;key=0;}
        if(Current==OUTPUT && key!=K_ITEM_S && Generator_Key(key,1)) {
            Generator_Reconcile();redraw=1;key=0;
        }
        {int action=UiFile_Key(key,1);
         if(action) {
            if(action==2) {
                int status;
                UiFile_Begin();
                if(Title[FILE][0].Value==LOAD) {
                    if(hw_session.stage){Capture144_Abort(&hw_session);valid=0;Frame144_Reset(&frame,++frame_epoch);}
                    status=UiFile_Load();
                }
                else if(Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CSV)
                    {if(hw_session.stage)Capture144_Abort(&hw_session);
                     valid=Frame144_Valid(&frame);
                     status=valid?File144_CsvFrame(&frame,base,(unsigned)Title[FILE][1].Value,invert?-(int)probe:(int)probe):VER_ERR;}
                else if(Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CFG) {
                    if(Generator_SecBusy())status=UI_FILE_BUSY;
                    else {
                        if(hw_session.stage){Capture144_Abort(&hw_session);valid=Frame144_Valid(&frame);if(!valid)measurement_available=0;}
                        __LCD_DMA_Ready();
                        status=File144_ConfigSave((unsigned)Title[FILE][1].Value);
                        Fft144_Reset();ShowFFT=0;
                        if(!status)ConfigFileNumber=(u8)Title[FILE][1].Value;
                    }
                }
                else status=UI_UNAVAILABLE;
                UiFile_Result(status);
                /* Same painter, position, colors and confirmation time as
                 * Normal. Its modal file action leaves IRQs running. */
                FileMessage((u8)status);
                if(Key_Buffer==key)Key_Buffer=0;
                if(!status && Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CFG) {
                    Current=file_return_current;UiCommon_Status(0);redraw=2;
                }
            }
            if(!redraw)redraw=1;
            key=0;
         }}
        if(key==KEY3 && Current!=FILE) {
            while(Key(KEY3_STATUS)) { }
            FlagMeter=(FlagMeter+1)%3;
            MeterDefaults();
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
            unsigned next=Scope144_VisibleGroupStep(Current,key==K_ITEM_INC?1:-1,FlagMeter,UpdateMeter);
            if(next==FILE && Current!=FILE)file_return_current=Current;
            Current=(u8)next;redraw=1;
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
                if(pressed && !k2_fired && elapsed>=UI_K2_SERVICE_MS*CYCLES_MS) {
                    k2_fired=1;
                    if(Current==TRACK1 || Current==TRACK2) {
                        Capture144_Abort(&hw_session);
                        measurement_available=0;
                        valid=0;Frame144_Reset(&frame,++frame_epoch);Fft144_Reset();ShowFFT=0;
                        scope_active=0;Key_Buffer=0;Calibrat(TRACK1);scope_active=1;
                        UiCommon_AnalogReset();
                        File144_CalLoad(&scope_bank);Scope144_NativeBank(&scope_bank);Scope144_SetClock(PPM_Comp);run_seen=Key1PressCount;
                        valid=0;last_range=8;last_coupling=2;redraw=2;
                    } else if(Current!=FILE) {OffsetSelect=OffsetSelect?0:1;redraw=2;}
                }
                if(!pressed) {
                    if(!k2_fired) {
                        if(OffsetSelect){OffsetSelect=0;redraw=2;}
                        if(_Tr_source==TRACK1) {
                            if(elapsed<UI_KEY_SHORT_MS*CYCLES_MS) {
                                TrgAuto=(u8)TriggerMenu_K2(TrgAuto,0);
                                if(Current==TRIGG && Detail[TRIGG]==2)TriggerLevelSelect=1;
                            } else {TrgAuto=0;Current=TRIGG;Detail[TRIGG]=2;TriggerLevelSelect=0;}
                        }
                        if(!redraw)redraw=1;
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
            case 1:
                if((delta<0 && base==2) || (delta>0 && !base))break;
                if(Scope144_IsFft((unsigned)_4_source) && ((base==2)!=(base-delta==2))) {
                    Fft144_Reset();ShowFFT=0;
                }
                base=(unsigned)((int)base-delta);adjust_xpos=1;break;
            case 2: {
                ViewWindowGeometry edge;
                int64_t current=requested_xpos_q16,next,origin,minimum,maximum;
                unsigned index=21-base,width=(unsigned)(MAX_X-MIN_X),step=Scope144_Step(base);
                /* Retained geometry remains safe while the next FIFO epoch
                 * is pending. No sample of that epoch is inspected here. */
                if(!display_view.capture_count || !ViewWindow_Build(display_view.capture_count,width,step,INT64_MAX,&edge))break;
                origin=(int64_t)xpos_reference_q16-(int64_t)(width>150?150:width-1)*step;
                minimum=-origin;maximum=(int64_t)edge.window_start_q16-origin;
                current=UiCommon_XposBound(current,0,minimum,maximum);
                next=UiCommon_XposBound(current,
                    UiCommon_Xpos144Delta(current,base,X_Attr[index].PSC,X_Attr[index].ARR,
                        maximum,delta,SetLowVolume,Key_Mid_Speed),minimum,maximum);
                requested_xpos_q16=(int32_t)next;adjust_xpos=1;break;
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
                _Vt1=(s16)UiCommon_XposBound(_Vt1,delta*(1+UiCommon_CursorSpeed(1,saved_flags[1])),0,200);
                break;
            }
            case 4:trigger_kind=(trigger_kind+9+delta)%9;break;
            case 5:mode=(mode+3+delta)%3;break;
            case 6:
                if(Current==TRACK4 && Scope144_IsFft((unsigned)_4_source))break;
                if(Current==TRACK3 || Current==TRACK4) {
                    s16 *p=&Title[Current][POSI].Value;
                    *p=(s16)UiCommon_XposBound(*p,delta*(1+UiCommon_CursorSpeed(0,saved_flags[1])),0,Title[Current][POSI].Limit);
                } else {
                    int next=(int)UiCommon_XposBound(position,delta*(1+UiCommon_CursorSpeed(0,saved_flags[1])),0,Title[TRACK1][POSI].Limit);
                    if(!held)next=Scope144_Position(range,ADCoffset,next,__Get(V_BATTERY),0);
                    if(next>=0)position=(unsigned)next;
                }
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
                meter *m=&Meter[Current-METER_0];m->Item=(u8)UiCommon_MeterStep(m->Item,delta);
                if(m->Item==VBT)m->Track=4;
                else if(m->Track!=TRACK3 && m->Track!=TRACK4)m->Track=TRACK1;
                break;
            }
            case 47: {
                meter *m=&Meter[Current-METER_0];
                if(m->Item==VBT)break;
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

        _T1=(s16)cursor1;_T2=(s16)cursor2;_State.Value=held && hw_session.stage!=2?HOLD:RUN;
        Title[V_VERNIE][2].Value=TRACK1;
        /* Normal owns the manual trigger in screen coordinates. Only its
         * automatic level follows the newly measured signal. */
        if(TrgAuto) {
            int32_t marker=(int32_t)position+(threshold/(int32_t)probe)/(int32_t)Y_Attr[range].SCALE;
            _Vt1=(s16)UiCommon_XposBound(marker,0,0,200);
        } else threshold=(int32_t)(_Vt1-(int)position)*Y_Attr[range].SCALE*probe;
        trigger_mode=Scope144_TriggerMode((unsigned)_Tr_source,trigger_kind,invert);
        pretrigger=Capture144_Pretrigger(base,(unsigned)(MAX_X-MIN_X));
        geometry=(base<<16)|(unsigned)(MAX_X-MIN_X);
        use_hw=trigger_mode!=UINT32_MAX && pretrigger &&
            FpgaProfile_Has(&WildcatFpga,FPGA_CAP_TRIGGER32);
        trigger_time=Scope144_TriggerTicks(base,cursor1,cursor2);
        if(hw_session.stage && (!use_hw || geometry!=hw_geometry || range!=hw_range || coupling!=hw_coupling ||
            (hw_session.stage==1 && hw_session.wait_timeout!=(mode?2000u:100u)*CYCLES_MS) ||
            (unsigned)_Tr_source!=hw_source ||
            (!held && hw_position!=position) || ADCoffset!=hw_offset || PPM_Comp!=measurement_ppm || (!held && threshold!=hw_level) || probe!=hw_probe ||
            invert!=hw_invert || trigger_kind!=hw_kind || trigger_mode!=hw_trigger_mode ||
            (trigger_kind>=4 && trigger_kind<=7 && trigger_time!=hw_time))) {
            Capture144_Abort(&hw_session);valid=Frame144_Valid(&frame);
        }
        if(PPM_Comp!=measurement_ppm)Scope144_SetClock(PPM_Comp);
        if(!held && (ADCoffset!=measurement_offset || PPM_Comp!=measurement_ppm)) {
            measurement_available=0;valid=0;Fft144_Reset();ShowFFT=0;
            digital_ready=digital_computed=0;analog_time_ready=0;
            Frame144_Reset(&frame,++frame_epoch);
        }
        if(!hw_session.stage && (!held || !valid)) {
            int effective=(int)position;
            runtime_ready=Scope144_Runtime(&scope_bank,range,ADCoffset,__Get(V_BATTERY),
                Y_Attr[range].SCALE,&effective,&active_cal,input_commands,&cal_status);
            if(runtime_ready && !held){
                position=(unsigned)effective;_1_posi=(s16)position;
                if(!TrgAuto)threshold=(int32_t)(_Vt1-effective)*Y_Attr[range].SCALE*probe;
            }
        }
        if(range!=last_range || coupling!=last_coupling || ADCoffset!=measurement_offset || PPM_Comp!=measurement_ppm)ypos_trace=0;
        if(!hw_session.stage && (!held || !valid) && runtime_ready) {
            unsigned changed=input_commands[0]!=last_commands[0] || input_commands[1]!=last_commands[1];
            /* Preserve only LCD pixels, never the overwritten raw epoch. */
            if(changed && range==last_range && coupling==last_coupling && display_had_frame)ypos_trace=1;
            ScopeInputs(range,coupling,input_commands);
            if(changed || range!=last_range || coupling!=last_coupling || ADCoffset!=measurement_offset) {
                /* A DAC change cannot alter old RAM or its captured calibration.
                 * Keep that epoch usable until the next transfer actually writes. */
                if(range!=last_range || coupling!=last_coupling || ADCoffset!=measurement_offset) {
                    measurement_available=0;valid=0;Fft144_Reset();ShowFFT=0;
                    if(!Settle(120))goto scope_cleanup;
                }
                last_range=range;last_coupling=coupling;
                last_commands[0]=input_commands[0];last_commands[1]=input_commands[1];
            }
        }
        measurement_offset=ADCoffset;measurement_ppm=PPM_Comp;
        if(!runtime_ready){measurement_available=0;valid=0;Fft144_Reset();ShowFFT=0;Frame144_Reset(&frame,frame_epoch);memset(&measure,0,sizeof(measure));}
        /* A changed input cannot reinterpret a frozen record with a different
         * analog range/coupling. Wait for RUN to acquire a new epoch. */
        if(!valid && !hw_session.stage)CaptureWindow_Reset(&frame.capture,frame_epoch,CAPTURE_WINDOW_EMPTY);
        if((!held || hw_session.stage==2) && runtime_ready) {
            Capture144Status capture;
            unsigned wait_timeout=0;
            if(!hw_session.stage) {
                raw_writing=0;Frame144_Reset(&pending_frame,++frame_epoch);
                pending_frame.words=(const uint32_t *)DataBuf;pending_frame.calibration=active_cal;pending_frame.range=range;
                pending_frame.capture_dac=input_commands[0]|(input_commands[1]<<16);pending_frame.capture_ypos=(int32_t)position;
                pending_frame.trigger_source=(unsigned)_Tr_source;
                pending_frame.presentation_q16=use_hw?Frame144_HardwareReference(pretrigger,(unsigned)_Tr_source,
                    trigger_kind,pending_frame.digital_shift_q16):0;
            }
            if(use_hw) {
                if(!hw_session.stage) {
                    /* An aborted/re-armed capture starts its own WAIT delay. */
                    Menu144_TriggerWait(0);
                    if(_Tr_source==TRACK1 && !Frame144_AdcThresholdKind(&active_cal,(invert?-threshold:threshold)/(int32_t)probe,
                        trigger_mode&7u,&hw_threshold)) {
                        capture=CAPTURE144_ARGUMENT;
                        if(!mode)wait_timeout=1;
                        else {held=1;Text(140,"Trigger level outside ADC range");}
                    } else {
                        hw_source=(unsigned)_Tr_source;hw_geometry=geometry;
                        hw_kind=trigger_kind;hw_trigger_mode=trigger_mode;hw_time=trigger_time;
                        hw_range=range;hw_coupling=coupling;
                        hw_offset=ADCoffset;hw_position=position;hw_level=threshold;hw_probe=probe;hw_invert=invert;
                        capture=Capture144_Begin(&hw_session,&WildcatFpga,&hw_ops,(uint32_t *)DataBuf,
                            frame_epoch,Scope144_TimePs(65536),(mode?2000u:100u)*CYCLES_MS,1000u*CYCLES_MS,pretrigger,&pending_frame.capture);
                        if(capture==CAPTURE144_PENDING && hw_source>=TRACK3 &&
                            !Capture144_DigitalTrigger(&hw_session,hw_source-TRACK3,pending_frame.digital_shift_q16))
                            capture=Capture144_Abort(&hw_session);
                        if(capture==CAPTURE144_PENDING && !Capture144_TriggerKind(&hw_session,hw_kind))
                            capture=Capture144_Abort(&hw_session);
                    }
                } else capture=CAPTURE144_PENDING;
                if(capture==CAPTURE144_PENDING) {
                    unsigned waiting=hw_session.stage==1;
                    capture=Capture144_Poll(&hw_session,512);
                    wait_timeout=waiting && !raw_writing && capture==CAPTURE144_TIMEOUT;
                }
                /* AUTO also needs fresh data when the requested threshold is
                 * outside the ADC guard band. Reuse the no-event capture; do
                 * not clamp the threshold or invent a hardware trigger. A
                 * transfer failure never enters this path. */
                if(!mode && wait_timeout &&
                   (capture==CAPTURE144_TIMEOUT || capture==CAPTURE144_ARGUMENT)) {
                    if(RuntimeCancel(0))capture=CAPTURE144_CANCELLED;
                    else {
                        fallback_active=1;
                        capture=Capture144_RecordWindow(&WildcatFpga,&ops,(uint32_t *)DataBuf,4096,
                            1000u*CYCLES_MS,frame_epoch,Scope144_TimePs(65536),&pending_frame.capture);
                        fallback_active=0;
                        if(capture!=CAPTURE144_OK)RuntimeStop(0);
                    }
                }
            } else {held=1;capture=CAPTURE144_ARGUMENT;Text(140,"Unsupported FPGA trigger");}
            if(capture!=CAPTURE144_OK && capture!=CAPTURE144_PENDING) {
                /* Only untouched completed bytes can survive an unsuccessful
                 * wait. A partial transfer has already revoked their metadata. */
                if(raw_writing)CaptureWindow_Reset(&frame.capture,frame.capture.epoch,CAPTURE_WINDOW_ERROR);
                valid=Frame144_Valid(&frame);
                if(!valid){measurement_available=0;memset(&measure,0,sizeof(measure));}
                if(capture!=CAPTURE144_CANCELLED && !(mode==1 && wait_timeout))held=1;
                if(capture!=CAPTURE144_CANCELLED && capture!=CAPTURE144_ARGUMENT &&
                    !(mode==1 && wait_timeout))CaptureError(capture);
            } else if(capture==CAPTURE144_OK) {
                frame=pending_frame; /* Commit one complete raw epoch atomically to consumers. */
                analog_time_ready=UiCommon_TimeDemand(0)&1u;
                if(!Frame144_MeasureRequested(&frame,analog_time_ready,&measure))goto scope_cleanup;
                measurement_available=1;
                digital_ready=digital_computed=0;digital_epoch=frame.capture.epoch;
                DigitalUpdate(DigitalDemand());
                if(TrgAuto && _Tr_source==TRACK1) {
                    int32_t low=invert?-measure.maximum_uv:measure.minimum_uv;
                    int32_t high=invert?-measure.minimum_uv:measure.maximum_uv;
                    threshold=TriggerMenu_AutoLevel(low,high,TrgAuto)*probe;
                }
                if(frame.capture.trigger_kind>=CAPTURE_TRIGGER_HARDWARE_A) {
                    if(!Frame144_HardwareView(&frame,(unsigned)(MAX_X-MIN_X),base,&view))goto scope_cleanup;
                    valid=1;if(mode==2)held=1;
                } else if(mode==0)valid=1;
                else valid=0;
            } else valid=Frame144_Valid(&frame);
        }
        /* Lazy HOLD calculation can use only this still-owned complete epoch.
         * Pending RUN retains cached values but never re-reads old raw data. */
        if(!analog_time_ready && measurement_available && valid &&
           (UiCommon_TimeDemand(0)&1u) && frame.capture.epoch==digital_epoch && Frame144_Valid(&frame)) {
            if(!Frame144_Measure(&frame,&measure))goto scope_cleanup;
            analog_time_ready=1;
        }
        Menu144_MeasurementTime(measurement_available && analog_time_ready);
        _State.Value=held && hw_session.stage!=2?HOLD:RUN;
        Menu144_Calibration(cal_status);
        Menu144_TriggerWait(hw_session.stage);
        Menu144_Presentation(measurement_available);
        view_valid=0;
        if(!valid && display_had_frame && (!measurement_available || (held && hw_session.stage!=2) || adjust_xpos) &&
           (!ypos_trace || (held && hw_session.stage!=2) || adjust_xpos || !runtime_ready)) {
            Process144_ClearFrame();display_had_frame=ypos_trace=0;overview_valid=0;
            /* K3 has already cleared the panel. Do not downgrade its full
             * redraw when the pending frame is invalidated afterwards. */
            if(!redraw)redraw=1;
        }
        if(!valid && ypos_trace && !held && !adjust_xpos && runtime_ready)
            Process144_Preview(position,invert);
        if(valid) {
            int64_t origin;unsigned width=(unsigned)(MAX_X-MIN_X),step=Scope144_Step(base);
            if(!Frame144_WindowOrigin(&frame,width,base,&origin))goto scope_cleanup;
            xpos_reference_q16=(uint32_t)(origin+(int64_t)(width>150?150:width-1)*step);
            if(!Frame144_Window(&frame,width,base,requested_xpos_q16,&view))goto scope_cleanup;
            /* Only the effective viewport is clamped. The XPOS field/time
             * shows the request; cursors/CSV/overview use actual frame data. */
            display_view=view;view_valid=1;overview_valid=1;
            frame.capture.first_visible_q16=view.window_start_q16;
            _X_posi.Value=(s16)(requested_xpos_q16/65536);
            if(held)frame.capture.flags|=CAPTURE_WINDOW_FROZEN;
            else frame.capture.flags &= ~CAPTURE_WINDOW_FROZEN;
            Menu144_Cursor(base);
            if(Scope144_IsFft((unsigned)_4_source) && frame.capture.time_samples>=512) {
                /* Center the 512-point FFT over the visible time window,
                 * as Normal does; Fft144 keeps FIR support inside real data. */
                int64_t centered=(int64_t)view.window_start_q16+
                    ((int64_t)(view.visible_columns-1u)*view.step_q16-
                    (int64_t)511u*(base==2?4u:2u)*65536)/2;
                unsigned first=centered>0?(unsigned)(centered/65536):0;
                fft_updated=Fft144_Update(&frame,first,base,(unsigned)_4_source,(unsigned)Title[TRACK4][RANGE].Value,DWT_CYCLES);
                ShowFFT=(u8)Fft144.ready;Fft144_Labels((unsigned)_T1);
            } else ShowFFT=0;
            if(redraw || adjust_xpos || plotted_epoch!=frame.capture.epoch || plotted_twink!=Twink || fft_updated) {
                Process144_DrawFrame(&frame,&view,position,invert);
                plotted_epoch=frame.capture.epoch;plotted_twink=Twink;
            }
            display_had_frame=1;ypos_trace=0;
            /* Draw_Window renders T1/T2 and V1/V2 using the shared Title values. */
        }
        if(!valid)Menu144_Cursor(base);
        {
            int32_t dt=0,dv;
            if(view_valid && !Frame144_TimeDelta(&frame,&display_view,cursor1,cursor2,&dt))goto scope_cleanup;
            if(!view_valid && measurement_available)
                dt=Scope144_CursorTime(base,cursor2)-Scope144_CursorTime(base,cursor1);
            if(!Scope144_Scale(&measure,invert?-(int)probe:(int)probe,&display_measure))memset(&display_measure,0,sizeof(display_measure));
            {int64_t delta_v=(int64_t)(_V1_Vernie-_V2_Vernie)*Y_Attr[range].SCALE*probe;
             dv=delta_v>2147483647?2147483647:delta_v<-2147483647?-2147483647:(int32_t)delta_v;}
            Menu144_View(&display_measure,dt,dv,valid,(unsigned)redraw);redraw=0;
        }
    }
    return 1;
scope_cleanup:
    view_valid=0;overview_valid=0;Fft144_Reset();ShowFFT=0;
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
