/* Execute the actual native service against simulated registers and BIOS.
 * No DS203 is connected; analog and LCD electrical behavior are not tested. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "File.h"
#include "Menu.h"
#include "Process.h"
#include "Interrupt.h"
#include "Scope144.h"
#include "Capture144.h"
#include "Fft144.h"
#include "Frame144.h"
#include "UiCommon.h"
extern uint32_t Scope144_RealCrc(const void *,uint32_t);
#include "UnifiedCal.h"
#include "Calibrat.h"
#include "Function.h"
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
static unsigned id_read_pending,confirm_stage,cal_calls,history,fifo_index,cal_loads,panel_clears;
static unsigned digital_scans,digital_scan_mask,digital_constant;
unsigned Native_RealDigitalMeasurePair(const Frame144 *,Scope144Measurements *,Scope144Measurements *);
unsigned Frame144_DigitalMeasurePair(const Frame144 *f,Scope144Measurements *c,Scope144Measurements *d)
{
    ++digital_scans;digital_scan_mask=(c?1u:0u)|(d?2u:0u);
    return Native_RealDigitalMeasurePair(f,c,d);
}
static unsigned image_crc_calls;
static unsigned analog_writes[13];
static uint32_t image_crc=SCOPE144_IMAGE_CRC;
static unsigned live_implementation=0x0102;
static unsigned checks,selector,draws,files,outputs,adc_mode,adc_a,adc_b,screen_clears,app_clears;
static unsigned key_status=0xffff;
static char footer[64];
static unsigned watch_settle,expected_mode,wrong_settle;
static uint32_t fake_cycles,capture_started,first_record_at;
static unsigned capture_case,record_count,range_sets,adc_controls,clock_arr,clock_psc;
static uint32_t fifo_started;
static unsigned timer_running;
static unsigned hw_test,hw_ready,hw_mode,hw_raw,hw_pre,hw_waiting,hw_ticks,hw_write_phase;
static unsigned hold_on_read,analog_command[13];
static unsigned hold_at=UINT32_MAX,hold_count=1,fault_at=UINT32_MAX,timeout_at=UINT32_MAX,fifo_tag,hw_status_error;
static unsigned wide_test,cal_fixture_mode,native_inherit_trigger;
static uint32_t noise_latency;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
u8 Current,Detail[14],FlagMeter,FlagFrameMode,FrameMode,OSBuffer,OSAvg,OsBufferLogicFlag;
u8 ShowFFT,Det,TrgAuto,CurDefTime,OsChartFlag,EnableCursorDisplay,DigChLockout,DownConvertMode,Sweep,CalibrateMode;
u8 FPGAosFlag,FPGAsubVer=1;
u8 OffsetSelect,OS_RateSelect,Options,ConfigFileNumber;
u8 UpdateMeter=4,Update,SelectiveUpdate,ClearMeterAreaFlag;
u8 GenAdjustMode,PWAdjustMode,GenBaudAdjSpeed,GenUartMode=3,GenUartCont,GenUartAdj,GenUartStopBits,InitiateUartGen,StartUart;
u8 FastDimAdjust,HboldAdjust,AdjBeepLevel;
uc8 ScaleIndex[17]={1,1,1,1,1,1,1,1,1,1,1,2,4,10,20,40,80};
u16 GenBaudIndex=7500,ArbtSampleNumber,DirRange;
u32 GenBaudRate=9600,UartFileSize;
s8 GenFreqShift;
s8 Ka3[10];
char Label[15][13],Ext[4],SelectedFileName[9];
void OutputAdjust(void){}
void PWMdutyControl(u8 direction){(void)direction;}
void PWMscaleReset(u8 direction){(void)direction;}
const char F_UNIT[12]="Hz",T_UNIT[12]="ns",V_UNIT[12]="uV",P_UNIT[12]="%";
char NumStr[12];
void Int2Str(char *out,s32 value,const char *unit,u8 precision,u8 mode,s8 shift)
{(void)value;(void)unit;(void)precision;(void)mode;(void)shift;out[0]=0;}
u8 ReadDir(char *ext){(void)ext;return 0;}
u8 Load_Arbt(u8 number){(void)number;return UI_ARB_INVALID;}
u8 Load_Uart(u8 number){(void)number;return UI_UART_INVALID;}
u8 SetLowVolume;vu8 Key_Mid_Speed;
vu8 Key_Buffer;vu32 Key1PressCount;vu16 Delay_Cnt,PD_Cnt,CursorDisplayTimer;
vu8 Twink;
void Print_Str(u16 x,u16 y,u16 color,u8 mode,char *s){(void)x;(void)y;(void)color;(void)mode;(void)s;}
void PrintDir(u16 row,u8 buffer,u8 mode){(void)row;(void)buffer;(void)mode;}
void ClearScreenArea(u16 x0,u16 x1,u8 y0,u8 y1){(void)x0;(void)x1;(void)y0;(void)y1;}
u16 MAX_X;menu Title[13][4]={[0]={[3]={.Limit=200}},[1]={[3]={.Limit=200}},[2]={[3]={.Limit=200}},[3]={[3]={.Limit=200}}};meter Meter[9];trigg V_Trigg[4];s16 PPM_Comp;u8 ADCoffset=54;
u32 DataBuf[4096];
static Y_attr attributes[8];Y_attr *Y_Attr=attributes;
static X_attr xattributes[27]={[19]={.ARR=1,.KP=614},[20]={.KP=492},[21]={.KP=246}};X_attr *X_Attr=xattributes;
uint32_t native_test_demcr,native_test_dwt;
uint32_t NativeTestCycles(void){if(watch_settle && adc_mode!=expected_mode)++wrong_settle;fake_cycles+=720;return fake_cycles;}
uint32_t Scope144_Crc(const void *bytes,uint32_t count)
{++image_crc_calls;CHECK(bytes==(void *)0x0805c800u && count==135096);return image_crc;}
u32 __Get(u8 object)
{
    if(object==KEY_STATUS) {
        if(capture_case==4 && (uint32_t)(fake_cycles-capture_started)>=100u*72000u)return key_status&~KEY4_STATUS;
        if(capture_case==5 && record_count>=2)return key_status&~KEY4_STATUS;
        if(!(key_status&KEY2_STATUS) && Delay_Cnt){Delay_Cnt=Delay_Cnt>20?Delay_Cnt-20:0;if(!Delay_Cnt)key_status|=KEY2_STATUS;}
        if(confirm_stage){unsigned step=confirm_stage++;if(step==3)return key_status&~KEY1_STATUS;if(step>=4)confirm_stage=0;}
        return key_status;
    }
    if(object==0 && id_read_pending){id_read_pending=0;return selector==0?0x5731:(selector==1?0x0101:(selector==2?live_implementation:0));}
    if(object==V_BATTERY)return 3700;
    if(object==FIFO_DIGIT && hw_status_error)return 0;
    if(object==FIFO_DIGIT)return hw_test && hw_mode!=UNCONDITION?(hw_ready?0x2c:0):(capture_case==6?0:0x2c);
    return 0;
}
void __Set(u8 object,u32 value)
{
    if(object<=CH_B_OFFSET || (object>=CH_A_COUPLE && object<=CH_B_RANGE))++analog_writes[object];
    if(object<13)analog_command[object]=(unsigned)value;
    if(object==37){selector=(unsigned)value;id_read_pending=1;}
    if(object==TRIGG_MODE){hw_mode=(unsigned)value;hw_pre=150;hw_write_phase=1;}
    if(object==V_THRESHOLD)hw_raw=(unsigned)value;
    if(object==T_THRESHOLD)hw_ticks=(unsigned)value;
    if(object==32+8){CHECK(hw_write_phase==1);hw_write_phase=2;hw_pre=(hw_pre&0xff00)|(unsigned)value;}
    if(object==32+9){CHECK(hw_write_phase==2);hw_write_phase=3;hw_pre=(hw_pre&255)|((unsigned)value<<8);}
    if(object==ADC_MODE){adc_mode=(unsigned)value;if(watch_settle)wrong_settle=0;}
    if(object==ADC_CTRL)++adc_controls;
    if(object==STANDBY)timer_running=value==DN;
    if(object==T_BASE_ARR)clock_arr=(unsigned)value;
    if(object==T_BASE_PSC)clock_psc=(unsigned)value;
    if(object==FIFO_CLR && value==W_PTR){fifo_started=fake_cycles;fifo_index=0;if(!record_count)first_record_at=fake_cycles;++record_count;}
    if(object==CH_A_RANGE){adc_a=(unsigned)value;++range_sets;}
    if(object==CH_B_RANGE)adc_b=(unsigned)value;
}
u32 __Read_FIFO(void)
{
    unsigned index=fifo_index++,a=55,b=55;
    if(index==hold_at){Key1PressCount+=hold_count;Key_Buffer=KEY1;hold_at=UINT32_MAX;}
    if(index==fault_at)hw_status_error=1;
    if(index==timeout_at){fake_cycles+=1000u*72000u;timeout_at=UINT32_MAX;}
    if(_Kind==5 && !(index%16))fake_cycles+=noise_latency;
    if(hold_on_read && index==200) {
        hold_on_read=0;key_status&=~KEY1_STATUS;++Key1PressCount;Key_Buffer=KEY1;
    }
    if(wide_test)return 0x2c0000u|((analog_command[CH_A_OFFSET]+17)&255u)|(((analog_command[CH_B_OFFSET]+19)&255u)<<8);
    /* ADC rails are intentional test input, not a hidden/spike filter. */
    if(capture_case==27 || capture_case==28) {
        CHECK(index<4097 && adc_mode==INTERLACE);
        return 0x2c0000u|(capture_case==27?0xffffu:0u);
    }
    if(hw_test && clock_arr==0 && hw_mode!=UNCONDITION) {
        CHECK(hw_ready && index<4097 && hw_pre==Capture144_Pretrigger(21-_T_base,MAX_X-MIN_X) && adc_b==8 && adc_mode==INTERLACE);
        unsigned kind=hw_mode&7u,boundary=kind>=4?hw_pre-1u:hw_pre;
        unsigned falling=kind==0 || kind==2 || kind>=6;
        a=index<boundary?hw_raw-14:hw_raw+14;
        if(falling)a=index<boundary?hw_raw+14:hw_raw-14;
        a+=fifo_tag;
        if(hw_mode>=16 && hw_mode<32) {
            unsigned mask=1u<<((hw_mode&8)?17:16),high=(index>=boundary)^falling;
            return index<3?0xffffffffu:0x2c0000u|(high?mask:0)|(a<<8)|a;
        }
        return index<3?0xffffffffu:0x2c0000u|(((index-1)&3)<<16)|(a<<8)|a;
    }
    if(clock_arr==239) {
        CHECK(index<4096 && adc_mode==SEPARATE && !clock_psc && timer_running);
        CHECK((uint32_t)(fake_cycles-fifo_started)>=20u*72000u);
    }
    if(history && clock_arr!=239 && hw_mode==UNCONDITION && index<=152)return 0xffff;
    if(capture_case==1)b=record_count<=4?75-record_count*5:55;
    if(capture_case==2){a=index%2?53:57;b=index%2?55:59;}
    if(capture_case==3 || (capture_case==7 && record_count==1) || (capture_case==11 && record_count==2))return 0xffff;
    if(capture_case==10)b=record_count%2?59:55;
    if(capture_case==12 && record_count>1 && index>=3584)b=255;
    if(capture_case==9) {
        uint32_t ms=(fake_cycles-capture_started)/72000u;
        b=55+(ms<600?(600-ms)/10:0);
    }
    if(capture_case==13) {
        a=165;b=163;
        if(record_count>1 && index==200)a=0;
        if(record_count>1 && index==201)b=255;
    }
    if(capture_case==8) {
        unsigned quarters=(record_count-1)*3;
        b=55+quarters/4+(index%4<quarters%4);
    }
    if(capture_case==14){if(index==0){a=56;b=56;}if(index==4095){a=57;b=58;}}
    if(capture_case==15){if(index==0)a=0;if(index==4095)b=255;}
    if(capture_case==17){a=70;b=adc_b==8?72:95;}
    /* Synthetic record matching the user's raw means/extrema and 1/3 rails;
     * the intermediate samples are invented, not recovered device data. */
    if(capture_case==18) {
        a=166;b=165;
        if(index==0)a=255;else if(index==1)a=87;else if(index<225)a=165;
        if(index<3)b=255;else if(index==3)b=72;else if(index<1238)b=164;
    }
    if(capture_case>=19 && capture_case<=23) {
        a=100;b=120;
        if(capture_case==19){if(index<4)a=255;if(index>=4 && index<8)b=0;}
        if(capture_case==20 && index<5)a=255;
        if(capture_case==21 && index<5)b=0;
        if(capture_case==22 && index<4){a=0;b=255;}
        if(capture_case==23){a=0;b=255;}
    }
    if(capture_case==24){a=9;b=200;}
    if(capture_case==25){a=246;b=150;}
    if(capture_case==26){a=10;b=245;if(index<4){a=255;b=0;}}
    return a|(b<<8)|(clock_arr==239?0:(digital_constant?0:(((index/32)&3)<<16))|0x2c0000u);
}
void __Display_Str(u16 x,u16 y,u16 color,u8 mode,char *s)
{(void)x;(void)color;(void)mode;if(y==0){strncpy(footer,s,63);footer[63]=0;}}
void __Clear_Screen(u16 color){(void)color;++screen_clears;}
void __Point_SCR(u16 x,u16 y){(void)x;(void)y;}
void __LCD_SetPixl(u16 color){(void)color;}
u8 ChartLogic(void){return 0;}
void App_init(u8 clear){UiCommon_AnalogReset();app_clears+=clear;}
static unsigned transitions;void App_ModeTransition(void){UiCommon_AnalogReset();++transitions;}
void Update_View_Area(void){}
void Update_Mark(void){}
void Clear_Meter_Area(void){++panel_clears;}
void Menu144_Calibration(unsigned status){CHECK(status<=2);}
static unsigned wait_resets;
void Menu144_TriggerWait(unsigned waiting){if(!waiting)++wait_resets;hw_waiting=waiting;}
static unsigned presentation_available;
void Menu144_Presentation(unsigned available){presentation_available=available;}
static unsigned analog_timing_available;
void Menu144_MeasurementTime(unsigned available){analog_timing_available=available;}
void Menu144_Cursor(unsigned base){CHECK(base<3);}
void Calibrat(u8 channel){CHECK(channel==TRACK1);CHECK(!Wildcat144_Active());if(hw_test)CHECK(hw_mode==UNCONDITION);++cal_calls;key_status|=KEY2_STATUS;}
static unsigned native_releases;
void Process144_ReleaseBuffer(void){++native_releases;}
void Update_Range(void){}
void Update_Base(void){}
void Update_Output(void){Generator_Validate();++outputs;}
void Update_Trig(u8 service){(void)service;}
static unsigned ui_calls;
static int32_t shown_dt;static unsigned shown_valid;
static Scope144Measurements shown_measure;
static unsigned last_refresh;
void Menu144_View(const Scope144Measurements *m,int32_t dt,int32_t dv,unsigned valid,unsigned refresh)
{last_refresh=refresh;(void)dv;shown_measure=*m;shown_dt=dt;shown_valid=valid;++ui_calls;}

static const Frame144 *last_frame;
static unsigned frame_clears,preview_calls,preview_position,preview_width,preview_invert;
void Process144_ClearFrame(void){++frame_clears;preview_width=0;}
int Process144_Preview(unsigned pos,unsigned inv)
{
 if(!preview_width || preview_width!=(unsigned)(MAX_X-MIN_X) || inv!=preview_invert || pos==preview_position)return 0;
 preview_position=pos;++preview_calls;return 1;
}
static ViewWindowGeometry last_view;
static uint32_t held_epoch,held_hash;
void Process144_DrawFrame(const Frame144 *f,const ViewWindowGeometry *view,unsigned pos,unsigned inv)
{
    CHECK(Frame144_Valid(f) && f->words==(const uint32_t *)DataBuf);
    CHECK(f->capture.fifo_words==4096);
    if(f->capture.trigger_kind>=CAPTURE_TRIGGER_HARDWARE_A) {
        CHECK(f->capture.valid_begin==2 && f->capture.time_samples==8188);
        unsigned slot=2*hw_pre-(f->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_A?5u:6u)+
            (f->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_A?0u:12u);
        if((hw_mode&7u)>=4)slot-=2;
        CHECK(f->capture.trigger_q16==slot*65536);
        CHECK(f->capture.flags&CAPTURE_WINDOW_PRETRIGGER);
    } else CHECK(f->capture.valid_begin==152 && f->capture.time_samples==7888);
    if(f->calibration.valid==SCOPE144_NOMINAL)CHECK(f->calibration.reference_uv==(int32_t)(attributes[_A_Range].SCALE*75));
    else CHECK(f->calibration.reference_uv==1234567+_A_Range*10000);
    CHECK(view->capture_count==f->capture.time_samples && view->visible_columns==(unsigned)(MAX_X-MIN_X));
    CHECK(f->capture.sample_phase==CAPTURE_PHASE_B_A);
    last_frame=f;last_view=*view;preview_position=pos;preview_invert=inv;preview_width=(unsigned)(MAX_X-MIN_X);++draws;
}
int File144_CalLoad(Scope144Bank *bank)
{
    unsigned i;++cal_loads;memset(bank,0,sizeof(*bank));
    if(cal_fixture_mode==1)return 0;
    for(i=0;i<8;++i){Scope144Cal c={1024,0,55*1024,125*1024,1234567+(int32_t)i*10000,1,1024,0,1024,1024,54,1024,1024,0,0};if(!cal_fixture_mode) {
        unsigned anchor=(unsigned)CalNative_Command(54,0,_1_posi);
        c.dac_anchor=anchor|(anchor<<8);c.dac_window=200u<<8|200u<<24;
    }
    if(cal_fixture_mode==3)c.gain_q10=0;
    if(wide_test) {
        c.zero_q10=55*1024;c.a_bias_q10=-29*1024;c.bias_q10=-1024;
        c.dac_anchor=79u|(78u<<8)|(64u<<24);c.dac_window=64u|(157u<<8)|(62u<<16)|(161u<<24);
    }bank->range[i]=c;}
    bank->battery[0]=3700;return 1;
}
static unsigned config_fails,file_messages,last_file_message,file_hold_event;
void FileMessage(u8 status){
 CHECK(Current==FILE);++file_messages;last_file_message=status;
 if(file_hold_event){file_hold_event=0;++Key1PressCount;Key_Buffer=KEY1;}
}
int File144_ConfigSave(unsigned number){
 CHECK(number<=255 && Wildcat144_Active());if(hw_test)CHECK(hw_mode==UNCONDITION);
 ++files;return config_fails?WR_ERR:0;
}
void __LCD_DMA_Ready(void){}
int File144_CalSave(Scope144Bank *bank){(void)bank;++files;return 0;}
int File144_Csv(const Scope144Cal *c,const int32_t *q,const uint16_t *pairs,unsigned range,unsigned base,unsigned num,int factor)
{(void)c;(void)q;(void)pairs;(void)range;(void)base;(void)num;(void)factor;++files;return 0;}
int File144_CsvFrame(const Frame144 *f,unsigned base,unsigned num,int factor)
{
    CHECK(f==last_frame && Frame144_Valid(f));
    CHECK(f->capture.epoch==held_epoch && Scope144_RealCrc(DataBuf,sizeof DataBuf)==held_hash);
    (void)base;(void)num;(void)factor;++files;return 0;
}
static int CalScenario(unsigned scenario,unsigned mode,CalMean *mean)
{
    int result;CalCaptureStats stats;unsigned controls_before=adc_controls;
    timer_running=0;capture_case=scenario;record_count=0;capture_started=fake_cycles;first_record_at=0;
    expected_mode=SEPARATE;watch_settle=1;wrong_settle=0;
    result=WildcatCalibration_Capture(0,mode,79,79,mean);
    watch_settle=0;CHECK(!wrong_settle && adc_controls==controls_before+1);
    CHECK(adc_mode==SEPARATE && clock_arr==239 && !clock_psc && adc_b==(mode?8:0));
    WildcatCalibration_Diagnostics(&stats);
    CHECK(stats.records<=record_count && stats.window<=3);
    CHECK(stats.elapsed_ms<=2021);
    if(result==1) {
        CHECK(stats.records>=4 && stats.blocks>=3 && stats.window==3);
        CHECK(stats.block_ms>=20 && stats.spread_a<=1024 && stats.spread_b<=1024);
    }
    CHECK((uint32_t)(fake_cycles-capture_started)<=2021u*72000u);
    if(scenario!=4)CHECK((uint32_t)(first_record_at-capture_started)>=300u*72000u);
    capture_case=0;return result;
}
/* Legacy synchronous service scenarios wait for the real cooperative HW
 * service to finish. hw_test scenarios retain one call per explicit tick. */
static int ServiceUntilFrame(void)
{
 unsigned i;int active;
 /* Older scenarios explicitly ask for a zero-volt level. New inheritance
  * tests below retain distinct Normal coordinates instead. */
 if(!Wildcat144_Active() && !native_inherit_trigger)_Vt1=_1_posi;
 active=Wildcat144_Service();
 if(!hw_test && active && Wildcat144_Active() && _State.Value==RUN)
  for(i=0;i<8 && hw_waiting;++i){Key_Buffer=0;CHECK(Wildcat144_Service());}
 return active;
}
static void KeyEvent(unsigned key){if(key==KEY1)++Key1PressCount;Key_Buffer=(u8)key;CHECK(ServiceUntilFrame());}
static void SelectGroup(unsigned group){unsigned count=0;while(Current!=group){CHECK(++count<22);KeyEvent(K_ITEM_INC);}}
static void CheckWindow(void)
{
    unsigned first,last;ViewWindowOverview o;int32_t a,b;
    CHECK(shown_valid && Frame144_Valid(last_frame));
    CHECK(last_frame->capture.first_visible_q16==last_view.window_start_q16);
    {ViewWindowGeometry axis=last_view;axis.capture_count=FRAME144_BUFFER_SAMPLES;CHECK(ViewWindow_Overview(&axis,91,216,&o));}
    CHECK(Wildcat144_Overview(&first,&last));
    CHECK(first==o.first && last==o.last && first>=91 && last<=306);
    CHECK(Wildcat144_CursorTime((unsigned)_T1,&a) && Frame144_TimeAt(last_frame,&last_view,(unsigned)_T1,&b) && a==b);
    CHECK(Frame144_TimeDelta(last_frame,&last_view,(unsigned)_T1,(unsigned)_T2,&a) && a==shown_dt);
}
static void BrowseHeld(void)
{
    unsigned i,reads=fifo_index,records=record_count,epoch=last_frame->capture.epoch;
    unsigned hash=Scope144_RealCrc(DataBuf,sizeof DataBuf),group=Current,detail=Detail[Current];
    int32_t before,after,dt;
    Current=T_BASE;Detail[T_BASE]=BASE;
    while(_T_base!=19)KeyEvent(K_INDEX_DEC);
    Detail[T_BASE]=XPOSI;CheckWindow();dt=shown_dt;
    CHECK(Wildcat144_CursorTime(30,&before));KeyEvent(K_INDEX_INC);
    CHECK(Wildcat144_CursorTime(30,&after) && after>before && shown_dt==dt);
    CHECK(Wildcat144_XposTime(&after) && after>0);
    for(i=0;i<4000;++i)KeyEvent(K_INDEX_INC);
    CheckWindow();CHECK(last_view.window_end_q16==(last_frame->capture.time_samples-1u)*65536);
    CHECK(Wildcat144_CursorTime(last_view.visible_columns-1,&after));
    CHECK(Frame144_TimeAt(last_frame,&last_view,last_view.visible_columns-1,&before) && after==before);
    {uint32_t start=last_view.window_start_q16;KeyEvent(K_INDEX_INC);CHECK(last_view.window_start_q16==start);}
    KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3);CheckWindow();
    Detail[T_BASE]=BASE;KeyEvent(K_INDEX_INC);CheckWindow();
    while(_T_base!=19)KeyEvent(K_INDEX_DEC);
    Detail[T_BASE]=XPOSI;for(i=0;i<4000;++i)KeyEvent(K_INDEX_DEC);
    CHECK(!last_view.window_start_q16 && Wildcat144_XposTime(&after));
    /* XPOS reports the retained user request, not the clamped raw start. */
    CHECK(after<0 && _X_posi.Value<0);CheckWindow();
    CHECK(fifo_index==reads && record_count==records && last_frame->capture.epoch==epoch);
    CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash && _State.Value==HOLD);
    Current=(u8)group;Detail[group]=(u8)detail;
}
static void HardwareTriggers(void)
{
    unsigned i,records,painted,detail;
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    hw_test=1;hw_ready=0;history=0;TrgAuto=0;capture_case=0;
    _Mode=NORH;_State.Value=RUN;_A_Range=0;_1_source=1;_T_base=21;_Tr_kind=1;
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=RANGE;
    CHECK(ServiceUntilFrame());
    CHECK(hw_mode==1 && hw_raw==55 && hw_pre==150);
    CHECK(hw_waiting && !fifo_index && _State.Value==RUN);
    records=record_count;painted=draws;
    for(i=0;i<20;++i)KeyEvent(0);
    KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3);
    CHECK(record_count==records+4 && !fifo_index && draws==painted && hw_waiting);records=record_count;
    /* Geometry changes disarm/re-arm an unfinished capture epoch. */
    Current=T_BASE;Detail[T_BASE]=BASE;KeyEvent(K_INDEX_DEC);
    CHECK(record_count==records+2 && _T_base==20);records=record_count;
    Current=TRIGG;Detail[TRIGG]=2;KeyEvent(K_INDEX_INC);
    CHECK(record_count==records+2 && hw_raw==59 && hw_waiting);records=record_count;
    Detail[TRIGG]=1;KeyEvent(K_INDEX_DEC);
    CHECK(record_count==records+2 && hw_mode==0);records=record_count;
    Current=TRACK1;Detail[TRACK1]=SOURCE;KeyEvent(K_INDEX_INC);
    CHECK(record_count==records+2 && hw_mode==1 && hw_raw==51);records=record_count;
    KeyEvent(K_INDEX_INC);CHECK(hw_raw==59 && hw_mode==0); /* Same screen level with x10 probe. */
    KeyEvent(K_INDEX_INC);KeyEvent(K_INDEX_INC); /* Restore A, no inversion. */
    CHECK(_1_source==1 && hw_mode==0 && hw_raw==59);
    records=record_count;ADCoffset=53;KeyEvent(0);CHECK(record_count==records+2 && hw_raw==58);
    ADCoffset=54;KeyEvent(0);CHECK(hw_raw==59);
    Current=TRACK1;Detail[TRACK1]=RANGE;KeyEvent(K_INDEX_INC);CHECK(hw_waiting && adc_a==1);
    /* NORM publishes only after all chunks, then re-arms a distinct epoch. */
    hw_ready=1;painted=draws;
    for(i=0;i<7;++i){KeyEvent(0);CHECK(hw_waiting && draws==painted && fifo_index==1+(i+1)*512);}
    KeyEvent(0);CHECK(!hw_waiting && draws==painted+1 && _State.Value==RUN && fifo_index==4097);
    /* An actual consumer requests C while this completed buffer is owned.
     * Its cached result then survives preparation of the next record. */
    {Scope144Measurements digital;CHECK(Wildcat144_DigitalMeasurement(0,&digital));}
    held_epoch=last_frame->capture.epoch;hw_ready=0;detail=frame_clears;KeyEvent(0);
    CHECK(hw_waiting && last_frame->capture.epoch==held_epoch && Frame144_Valid(last_frame));
    CHECK(frame_clears==detail && shown_valid && presentation_available);
    {
        Scope144Measurements analog=shown_measure,digital;
        CHECK(Wildcat144_DigitalMeasurement(0,&digital));
        hw_ready=1;KeyEvent(0);
        CHECK(hw_waiting==2 && fifo_index==513 && !shown_valid && presentation_available);
        CHECK(!memcmp(&analog,&shown_measure,sizeof(analog)) && frame_clears==detail);
        CHECK(Wildcat144_DigitalMeasurement(0,&digital));
    }
    /* K4 navigates while reading; HOLD waits for the remaining FIFO chunks. */
    detail=Detail[TRACK1];KeyEvent(KEY4);CHECK(hw_waiting && _State.Value==RUN && Detail[TRACK1]==(detail+1)%4);
    KeyEvent(KEY1);CHECK(hw_waiting==2 && _State.Value==RUN);
    for(i=0;i<8 && hw_waiting;++i)KeyEvent(0);
    CHECK(!hw_waiting && _State.Value==HOLD && presentation_available && shown_valid);
    hw_ready=0;
    records=record_count;KeyEvent(0);CHECK(record_count==records && fifo_index==4097);
    KeyEvent(KEY1);CHECK(hw_waiting && _State.Value==RUN);
    painted=draws;fake_cycles+=2000u*72000u;KeyEvent(0);
    CHECK(!hw_waiting && _State.Value==RUN && draws==painted);KeyEvent(0);CHECK(hw_waiting);
    /* Abort hardware before the calibration wizard borrows DataBuf. */
    Current=TRACK1;records=record_count;detail=cal_calls;
    key_status&=~KEY2_STATUS;KeyEvent(KEY2);fake_cycles+=3000u*72000u;KeyEvent(0);
    CHECK(cal_calls==detail+1 && hw_waiting && record_count==records+2);
    key_status|=KEY2_STATUS;KeyEvent(0);
    /* Switching to SGL preserves the armed event; timeout stops until K1. */
    Current=T_BASE;Detail[T_BASE]=MODE;KeyEvent(K_INDEX_INC);CHECK(_Mode==SGL);
    fake_cycles+=2000u*72000u;KeyEvent(0);CHECK(!hw_waiting && _State.Value==HOLD);
    KeyEvent(KEY1);CHECK(hw_waiting);hw_ready=1;
    for(i=0;i<8;++i)KeyEvent(0);
    CHECK(_State.Value==HOLD && !hw_waiting && Frame144_Valid(last_frame));
    CHECK(last_frame->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_A);
    CheckWindow();CHECK(Wildcat144_TriggerColumn()==MIN_X+150);
    {int32_t a,b,dt=shown_dt;uint32_t epoch=last_frame->capture.epoch;
     Current=T_BASE;Detail[T_BASE]=XPOSI;CHECK(Wildcat144_CursorTime(150,&a) && !a);
     KeyEvent(K_INDEX_INC);CHECK(Wildcat144_CursorTime(150,&b) && b>a && shown_dt==dt);
     {unsigned step=Scope144_Step(21-_T_base),column=(last_frame->capture.trigger_q16-last_view.window_start_q16+step/2)/step;
      CHECK(Wildcat144_TriggerColumn()==(int)(MIN_X+column) && last_frame->capture.epoch==epoch);}
     KeyEvent(K_INDEX_DEC);CHECK(Wildcat144_TriggerColumn()==MIN_X+150);CheckWindow();}
    held_epoch=last_frame->capture.epoch;held_hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
    records=record_count;KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3);
    CHECK(record_count==records && last_frame->capture.epoch==held_epoch && fifo_index==4097);
    CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==held_hash);
    Current=FILE;Detail[FILE]=1;KeyEvent(KEY3);CHECK(record_count==records);
    /* HOLD during a partial transfer freezes only its complete result. */
    Current=TRACK1;KeyEvent(KEY1);CHECK(hw_waiting && fifo_index==513);
    KeyEvent(KEY1);CHECK(hw_waiting==2 && _State.Value==RUN);
    for(i=0;i<8 && hw_waiting;++i)KeyEvent(0);
    CHECK(!hw_waiting && _State.Value==HOLD && Frame144_Valid(last_frame));
    /* AUTO switch re-arms the same HW edge with its shorter wait deadline. */
    hw_ready=0;KeyEvent(KEY1);CHECK(hw_waiting);
    Current=T_BASE;Detail[T_BASE]=MODE;KeyEvent(K_INDEX_INC);
    CHECK(_Mode==AUTO && hw_waiting && hw_mode==(unsigned)_Tr_kind && !fifo_index);
    fake_cycles+=101u*72000u;KeyEvent(0);
    CHECK(!hw_waiting && hw_mode==UNCONDITION && last_frame->capture.time_samples==7888);
    KeyEvent(KEY1);CHECK(_State.Value==HOLD);
    /* Exit while waiting also aborts and restores the normal acquisition. */
    KeyEvent(K_INDEX_INC);CHECK(_Mode==NORH);KeyEvent(KEY1);CHECK(hw_waiting);
    Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && hw_mode==UNCONDITION && adc_mode==SEPARATE);
    hw_test=0;hw_ready=0;ADCoffset=54;
}
static void DigitalUI(void)
{
    unsigned source,falling,i,reads,records;int32_t ps;Scope144Measurements m;
    hw_test=1;hw_ready=0;capture_case=0;history=0;TrgAuto=0;
    for(source=TRACK3;source<=TRACK4;++source)for(falling=0;falling<2;++falling) {
        _Mode=SGL;_State.Value=RUN;_Tr_source=TRACK2;_Tr_kind=falling?0:1;
        _3_source=1;_4_source=1;_3_posi=60;_4_posi=20;_T_base=21;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK2;hw_ready=0;
        CHECK(ServiceUntilFrame() && Current==TRACK1 && _Tr_source==TRACK1 && _3_source==1 && _4_source==1);
        Current=TRIGG;Detail[TRIGG]=SOURCE;
        for(i=TRACK1;i<source;i+=(i==TRACK1?2:1))KeyEvent(K_INDEX_INC);
        CHECK(_Tr_source==(s16)source && hw_mode==source*8+!falling && hw_waiting);
        records=record_count;Current=TRACK3;Detail[TRACK3]=SOURCE;
        KeyEvent(K_INDEX_DEC);CHECK(_3_source==0 && record_count==records);
        KeyEvent(K_INDEX_INC);CHECK(_3_source==1 && record_count==records);
        Current=TRACK4;Detail[TRACK4]=POSI;KeyEvent(K_INDEX_INC);CHECK(_4_posi==21);
        hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
        CHECK(_State.Value==HOLD && !hw_waiting);
        CHECK(last_frame->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_C+source-TRACK3);
        CHECK(last_frame->trigger_source==source && last_frame->capture.trigger_q16==306u*65536);
        CHECK(Wildcat144_CursorTime(150,&ps) && !ps);CheckWindow();
        records=record_count;reads=fifo_index;
        Current=TRACK3;Detail[TRACK3]=POSI;KeyEvent(K_INDEX_INC);CHECK(_3_posi==61);
        Current=TRACK4;Detail[TRACK4]=SOURCE;KeyEvent(K_INDEX_DEC);CHECK(!_4_source);
        KeyEvent(K_INDEX_INC);CHECK(_4_source==1);
        Current=METER_0;Detail[13]=0;Meter[0].Item=FRQ;Meter[0].Track=TRACK1;
        KeyEvent(KEY4);CHECK(Detail[13]==1);KeyEvent(K_INDEX_INC);CHECK(Meter[0].Track==TRACK3);
        KeyEvent(K_INDEX_INC);CHECK(Meter[0].Track==TRACK4);
        CHECK(!Wildcat144_DigitalMeasurement(2,&m));
        Current=T_BASE;Detail[T_BASE]=XPOSI;KeyEvent(K_INDEX_INC);KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3);
        CHECK(record_count==records && fifo_index==reads);CheckWindow();
        Current=TRACK1;KeyEvent(K_ITEM_S);
        CHECK(!Wildcat144_Active() && Current==TRACK2 && _Tr_source==TRACK2 && _3_source==1 && _4_source==1);
        CHECK(_3_posi==60 && _4_posi==20 && adc_mode==SEPARATE);
    }
    _Tr_source=TRACK1;hw_test=0;hw_ready=0;
}
static void TriggerNavigation(void)
{
    unsigned i,source;
    hw_test=1;hw_ready=0;capture_case=0;TrgAuto=0;TriggerLevelSelect=0;
    _Mode=NORH;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;
    _A_Range=0;_1_source=1;Current=T_BASE;Detail[T_BASE]=BASE;
    /* Native contexts and transient edit cursors must not leak across modes. */
    Detail[OUTPUT]=FRQN;OffsetSelect=1;OS_RateSelect=1;
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;CHECK(ServiceUntilFrame());
    for(source=TRACK1;source<=TRACK4;++source)if(source!=TRACK2) {
        Current=TRIGG;Detail[TRIGG]=SOURCE;TriggerLevelSelect=0;
        _Tr_source=(s16)source;KeyEvent(0);
        KeyEvent(KEY4);CHECK(Detail[TRIGG]==TR_KIND && hw_waiting && _State.Value==RUN);
        if(source==TRACK1) {
            KeyEvent(KEY4);CHECK(Detail[TRIGG]==2 && !TriggerLevelSelect);
            KeyEvent(KEY4);CHECK(Detail[TRIGG]==2 && TriggerLevelSelect);
            for(i=0;i<8;++i) {KeyEvent(K_INDEX_INC);CHECK(TrgAuto==(i+1)%4 && Detail[TRIGG]==2 && hw_waiting);}
            for(i=0;i<8;++i) {KeyEvent(K_INDEX_DEC);CHECK(TrgAuto==(7-i)%4 && Detail[TRIGG]==2 && hw_waiting);}
        }
        KeyEvent(KEY4);CHECK(Detail[TRIGG]==SOURCE && !TriggerLevelSelect);
    }
    Current=T_BASE;Detail[T_BASE]=XPOSI;Detail[OUTPUT]=OUTATT;TriggerLevelSelect=1;
    KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
    CHECK(Current==T_BASE && Detail[T_BASE]==BASE && Detail[OUTPUT]==FRQN && !TriggerLevelSelect);
    CHECK(!OffsetSelect && !OS_RateSelect && !presentation_available);
    hw_test=0;hw_ready=0;_Tr_source=TRACK1;TrgAuto=0;
}
static void HoldDuringPoll(void)
{
    hw_test=1;hw_ready=0;capture_case=0;TrgAuto=0;
    _Mode=NORH;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;
    _A_Range=0;_1_source=1;Current=TRACK1;
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;CHECK(ServiceUntilFrame());
    hold_on_read=1;hw_ready=1;KeyEvent(0);
    CHECK(!hold_on_read && hw_waiting==2 && _State.Value==RUN && Key_Buffer==KEY1);
    {unsigned n;for(n=0;n<7;++n)CHECK(ServiceUntilFrame());}
    CHECK(_State.Value==HOLD && shown_valid && !hw_waiting);
    CHECK(ServiceUntilFrame() && _State.Value==HOLD);
    KeyEvent(0);CHECK(_State.Value==HOLD);
    key_status|=KEY1_STATUS;KeyEvent(0);CHECK(_State.Value==HOLD);
    KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());hw_test=hw_ready=0;
}
static void AllTriggerKinds(void)
{
    unsigned source,kind,invert,base,mode,i,records,reads;
    const unsigned inverted[8]={1,0,3,2,6,7,4,5};
    hw_test=1;history=0;TrgAuto=0;capture_case=0;ADCoffset=54;
    for(source=TRACK1;source<=TRACK4;++source)if(source!=TRACK2)
    for(kind=0;kind<9;++kind)for(invert=0;invert<(source==TRACK1?2u:1u);++invert)
    for(base=0;base<3;++base)for(mode=0;mode<3;++mode) {
        unsigned physical=kind==8?3:kind,expected,slot;
        /* AUTO rising/falling are part of this HW matrix too. */
        if(source==TRACK1 && invert)physical=inverted[physical];
        expected=source*8+physical;
        _Mode=mode==0?AUTO:mode==1?NORH:SGL;_State.Value=RUN;
        _Tr_source=(s16)source;_Tr_kind=(s16)kind;_T_base=(s16)(21-base);
        _A_Range=0;_1_source=(s16)(1+invert);_T1=30;_T2=180;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;hw_ready=0;
        CHECK(ServiceUntilFrame());
        CHECK(_Tr_kind==(s16)kind && hw_mode==expected && hw_waiting && !fifo_index);
        CHECK(hw_ticks==(base==0?36u:base==1?72u:180u));
        records=record_count;hw_ready=1;
        for(i=0;i<8;++i)KeyEvent(0);
        CHECK(shown_valid && !hw_waiting && fifo_index==4097 && record_count==records);
        CHECK(_State.Value==(mode==2?HOLD:RUN));
        slot=2*hw_pre-(source==TRACK1?5:6)+(source==TRACK1?0:12);if(kind>=4 && kind<=7)slot-=2;
        CHECK(last_frame->trigger_source==source && last_frame->capture.trigger_q16==slot*65536);
        CHECK(last_frame->capture.trigger_kind==(source==TRACK1?CAPTURE_TRIGGER_HARDWARE_A:
            CAPTURE_TRIGGER_HARDWARE_C+source-TRACK3));
        {unsigned step=Scope144_Step(base),column=(slot*65536u+step/2u)/step;
         if(column>150)column=150;
         CHECK(Wildcat144_TriggerColumn()==(int)(MIN_X+column));}CheckWindow();
        if(mode!=2)KeyEvent(KEY1);
        reads=fifo_index;records=record_count;
        /* All nine choices cycle in both directions in HOLD without reads. */
        Current=TRIGG;Detail[TRIGG]=TR_KIND;
        for(i=0;i<9;++i){KeyEvent(K_INDEX_INC);CHECK(_Tr_kind==(s16)((kind+i+1)%9));}
        for(i=0;i<9;++i){KeyEvent(K_INDEX_DEC);CHECK(_Tr_kind==(s16)((kind+17-i)%9));}
        CHECK(record_count==records && fifo_index==reads);
        Current=TRACK1;KeyEvent(K_ITEM_S);
        CHECK(!Wildcat144_Active() && _Tr_source==(s16)source && _Tr_kind==(s16)kind);
    }
    /* AUTO without a qualifying pulse: timeout publishes an untriggered,
     * fresh record, without inventing an event or stopping RUN. */
    _Mode=AUTO;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=4;_T_base=21;
    _A_Range=0;_1_source=1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;hw_ready=0;
    CHECK(ServiceUntilFrame() && hw_waiting && !fifo_index);
    records=record_count;fake_cycles+=100u*72000u;KeyEvent(0);
    CHECK(_State.Value==RUN && !hw_waiting && hw_mode==UNCONDITION && record_count==records+2);
    CHECK(shown_valid && last_frame->capture.time_samples==7888 &&
        !(last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER) && !Wildcat144_TriggerColumn());
    /* AUTO and NORM must not inherit each other's armed wait deadline. */
    KeyEvent(0);CHECK(hw_waiting);records=record_count;
    Current=T_BASE;Detail[T_BASE]=MODE;KeyEvent(K_INDEX_INC);
    CHECK(_Mode==NORH && hw_waiting && record_count==records+2);records=record_count;
    fake_cycles+=100u*72000u;KeyEvent(0);
    CHECK(hw_waiting && record_count==records && _State.Value==RUN);
    KeyEvent(K_INDEX_DEC);CHECK(_Mode==AUTO && hw_waiting && record_count==records+2);
    fake_cycles+=100u*72000u;KeyEvent(0);CHECK(!hw_waiting && shown_valid && _State.Value==RUN);
    /* Width, timebase and source edits re-arm an unfinished pulse wait. */
    KeyEvent(0);CHECK(hw_waiting);records=record_count;
    Current=T_VERNIE;Detail[T_VERNIE]=T2;
    for(i=0;i<5;++i)KeyEvent(K_INDEX_INC);
    CHECK(hw_waiting && record_count>records && hw_ticks==37);
    records=record_count;Current=T_BASE;Detail[T_BASE]=BASE;KeyEvent(K_INDEX_DEC);
    CHECK(hw_ticks==74 && record_count==records+2 && hw_waiting);
    records=record_count;Current=TRIGG;Detail[TRIGG]=SOURCE;KeyEvent(K_INDEX_INC);
    CHECK(_Tr_source==TRACK3 && hw_mode==20 && record_count==records+2);
    /* A transfer timeout is a failure, not AUTO's no-event fallback. */
    hw_ready=1;KeyEvent(0);CHECK(hw_waiting && fifo_index==513);
    records=record_count;fake_cycles+=1000u*72000u;KeyEvent(0);
    CHECK(!hw_waiting && _State.Value==HOLD && !shown_valid && !presentation_available && record_count==records+1);
    Current=TRACK1;KeyEvent(K_ITEM_S);hw_test=hw_ready=0;_Tr_source=TRACK1;
}
static void AutoLevels(void)
{
    unsigned fraction,invert,probe;Scope144Measurements physical;
    hw_test=0;history=0;capture_case=2;attributes[0].SCALE=1000;
    for(fraction=1;fraction<4;++fraction)for(invert=0;invert<2;++invert)for(probe=0;probe<2;++probe) {
        int32_t low,high,expected;
        _Mode=AUTO;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;
        _A_Range=0;_1_source=(s16)(1+invert+2*probe);TrgAuto=(u8)fraction;
        Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
        CHECK(ServiceUntilFrame() && shown_valid);
        CHECK(Frame144_Measure(last_frame,&physical));
        low=invert?-physical.maximum_uv:physical.minimum_uv;
        high=invert?-physical.minimum_uv:physical.maximum_uv;
        expected=low+(high-low)*(int)fraction/4;
        KeyEvent(KEY1);CHECK(_State.Value==HOLD);
        CHECK(_Vt1==_1_posi+expected/1000 && TrgAuto==fraction);
        Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
    }
    capture_case=0;TrgAuto=0;attributes[0].SCALE=40000;
}
static void NoiseCaptures(void)
{
    const unsigned rates[]={0,8,14,15,16,18};unsigned r,i;
    hw_test=1;capture_case=0;history=0;noise_latency=100000u;
    for(r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        _Kind=5;_Frqn=(s16)rates[r];Sweep=0;TrgAuto=0;
        TIM2->DIER=1;TIM4->DIER=rates[r]<16;
        _Mode=SGL;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;_A_Range=0;_1_source=1;
        Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;hw_ready=0;
        CHECK(ServiceUntilFrame() && hw_waiting && TIM2->DIER==1 && TIM4->DIER==(rates[r]<16));
        hw_ready=1;
        for(i=0;i<8;++i){unsigned before=fifo_index;KeyEvent(0);CHECK(fifo_index-before<=513);}
        CHECK(_State.Value==HOLD && shown_valid && last_frame->capture.time_samples==8188);
        CHECK(TIM2->DIER==1 && TIM4->DIER==(rates[r]<16));CheckWindow();
        KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && _Kind==5 && _Frqn==(s16)rates[r]);
        CHECK(TIM2->DIER==1 && TIM4->DIER==(rates[r]<16));
    }
    noise_latency=0;hw_ready=hw_test=0;_Kind=6;TIM2->DIER=TIM4->DIER=0;
}
static void FailedTransfer(void)
{
    unsigned i,cleared;
    _Kind=6;Sweep=TrgAuto=0;hw_test=1;hw_ready=0;capture_case=0;history=0;
    _Mode=NORH;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;_A_Range=0;_1_source=1;
    Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;CHECK(ServiceUntilFrame());
    hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
    CHECK(shown_valid && presentation_available);hw_ready=0;KeyEvent(0);
    CHECK(shown_valid && presentation_available);hw_ready=1;KeyEvent(0);
    CHECK(hw_waiting==2 && presentation_available);cleared=frame_clears;
    fake_cycles+=1000u*72000u;KeyEvent(0);
    CHECK(!shown_valid && !presentation_available && frame_clears==cleared+1 && _State.Value==HOLD);
    CHECK(!Frame144_Valid(last_frame));KeyEvent(KEY1);CHECK(_State.Value==RUN);
    KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());hw_test=hw_ready=0;
}
/* A reset initializes installed_image_valid to zero. Simulate startup checks
 * explicitly, including failed CRC and changed live versions on later entry. */
static void StartupImageCheck(void)
{
    unsigned n;
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;confirm_stage=1;
    CHECK(ServiceUntilFrame() && !Wildcat144_Active());
    CHECK(!image_crc_calls && !cal_loads && screen_clears==1 && app_clears==1);
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0);
    Wildcat144_CheckImage();CHECK(!image_crc_calls);
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    image_crc^=1;Wildcat144_CheckImage();CHECK(image_crc_calls==1);
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;confirm_stage=1;
    CHECK(ServiceUntilFrame() && !Wildcat144_Active());
    CHECK(image_crc_calls==1 && !cal_loads && Title[TRACK1][COUPLE].Value==DC);
    image_crc=SCOPE144_IMAGE_CRC;Wildcat144_CheckImage();CHECK(image_crc_calls==2);
    /* An accepted ROM image must not bypass the current implementation ID. */
    for(n=0;n<2;++n) {
        live_implementation=n?0x0103:0;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;confirm_stage=1;
        CHECK(ServiceUntilFrame() && !Wildcat144_Active());
        CHECK(image_crc_calls==2 && !cal_loads && Title[TRACK1][COUPLE].Value==DC);
    }
    live_implementation=0x0102;screen_clears=app_clears=0;
}
static unsigned hold_normal_mode=1;
static void HoldP0Start(unsigned mode)
{
 unsigned n;
 CHECK(!Wildcat144_Active());
 hw_test=1;hw_ready=1;history=capture_case=0;cal_fixture_mode=0;
 hold_at=fault_at=timeout_at=UINT32_MAX;hold_count=1;hw_status_error=fifo_tag=0;
 _Kind=6;TrgAuto=0;_Mode=(s16)mode;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;
 _A_Range=4;_1_posi=100;_Vt1=100;_1_source=_3_source=_4_source=1;_T_base=21;
 FlagMeter=1;FlagFrameMode=(u8)hold_normal_mode;Current=TRACK1;Detail[TRACK1]=POSI;SetLowVolume=0;
 key_status=0xffff;ADCoffset=40;
 Title[TRACK1][COUPLE].Value=SCOPE144_DC;
 CHECK(ServiceUntilFrame());for(n=0;n<7;++n)KeyEvent(0);
 CHECK(shown_valid && Frame144_Valid(last_frame));
}
static void HoldP0Stop(void)
{
 hold_at=fault_at=timeout_at=UINT32_MAX;hw_status_error=hold_on_read=fifo_tag=0;
 Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
}
static void HoldP0(void)
{
 static const unsigned modes[]={AUTO,NORH,SGL},at[]={1,512,1024,3584};
 unsigned m,j,n,records,reads,clears;uint32_t hash,epoch;Frame144 snapshot;
 /* Untouched pending RAM is still the completed frame, with its frozen
  * calibration. Stop/abort may reset FPGA pointers, not any byte of DataBuf. */
 for(m=0;m<3;++m){
  HoldP0Start(modes[m]);snapshot=*last_frame;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  hw_ready=0;if(_State.Value==HOLD)KeyEvent(KEY1);else KeyEvent(0);
  CHECK(hw_waiting==1 && shown_valid && Frame144_Valid(last_frame));
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  if(m==1){fake_cycles+=2001u*72000u;KeyEvent(0);CHECK(shown_valid && !hw_waiting);KeyEvent(0);}
  records=record_count;clears=frame_clears;KeyEvent(KEY1);
  CHECK(_State.Value==HOLD && !hw_waiting && shown_valid && frame_clears==clears);
  snapshot.capture.flags|=CAPTURE_WINDOW_FROZEN;
  CHECK(!memcmp(&snapshot,last_frame,sizeof snapshot));
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  records=record_count;for(n=0;n<4;++n)KeyEvent(0);CHECK(record_count==records);
  Current=T_BASE;Detail[T_BASE]=XPOSI;KeyEvent(K_INDEX_INC);CheckWindow();
  CHECK(record_count==records && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  Current=TRACK1;Detail[TRACK1]=POSI;reads=analog_command[CH_A_OFFSET];KeyEvent(K_INDEX_INC);
  CHECK(analog_command[CH_A_OFFSET]==reads && record_count==records);
  CHECK(!memcmp(&snapshot.calibration,&last_frame->calibration,sizeof snapshot.calibration));
  {char row[80];Scope144Measurements v;
   CHECK(Frame144_CsvRow(row,last_frame,0,1)>0 && Frame144_Measure(last_frame,&v));
   CHECK(Wildcat144_DigitalMeasurement(0,&v) && Wildcat144_DigitalMeasurement(1,&v));}
  Current=TRACK4;Detail[TRACK4]=SOURCE;KeyEvent(K_INDEX_INC);fake_cycles+=8000000;KeyEvent(0);
  CHECK(_4_source==14 && Fft144.ready && Fft144.epoch==last_frame->capture.epoch);
  KeyEvent(K_INDEX_DEC);CHECK(_4_source==1);KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3);CheckWindow();
  CHECK(record_count==records && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  HoldP0Stop();
 }
 /* K1 may arrive inside any FIFO chunk. It requests HOLD after the complete
  * next frame; the pending raw prefix is never licensed to consumers. */
 for(m=0;m<3;++m)for(j=0;j<4;++j){
  HoldP0Start(modes[m]);epoch=last_frame->capture.epoch;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  hw_ready=0;if(_State.Value==HOLD)KeyEvent(KEY1);else KeyEvent(0);
  fifo_tag=3;hold_at=at[j];hw_ready=1;KeyEvent(0);
  for(n=0;n<7;++n){
   if(hw_waiting==2){int32_t q;char row[80];
    CHECK(!shown_valid && !Frame144_Valid(last_frame));
    CHECK(!Frame144_Read(last_frame,0,&q,0) && !Frame144_CsvRow(row,last_frame,0,1));}
   KeyEvent(0);
  }
  CHECK(hold_at==UINT32_MAX && _State.Value==HOLD && !hw_waiting && shown_valid && fifo_index==4097);
  CHECK(last_frame->capture.epoch!=epoch && Frame144_Valid(last_frame));
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)!=hash);
  records=record_count;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  for(n=0;n<12;++n)KeyEvent(0);
  CHECK(_State.Value==HOLD && record_count==records && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  hw_ready=0;KeyEvent(KEY1);CHECK(_State.Value==RUN && hw_waiting==1 && shown_valid);
  Key_Buffer=KEY1;CHECK(ServiceUntilFrame() && _State.Value==RUN); /* same queued press */
  KeyEvent(KEY1);CHECK(_State.Value==HOLD && shown_valid);
  HoldP0Stop();
 }
 /* Partial errors do not become an AUTO fallback or a fake old HOLD. */
 for(m=0;m<2;++m)for(j=0;j<4;++j){
  HoldP0Start(AUTO);hw_ready=0;KeyEvent(0);epoch=last_frame->capture.epoch;
  fifo_tag=3;if(m)timeout_at=at[j];else fault_at=at[j];
  hw_ready=1;KeyEvent(0);for(n=0;n<8 && hw_waiting;++n)KeyEvent(0);
  CHECK(!hw_waiting && _State.Value==HOLD && !shown_valid && !Frame144_Valid(last_frame));
  {char row[80];int32_t q;Scope144Measurements v;
   CHECK(!Frame144_Read(last_frame,0,&q,0) && !Frame144_CsvRow(row,last_frame,0,1));
   CHECK(!Frame144_Measure(last_frame,&v));}
  hw_status_error=0;fault_at=timeout_at=UINT32_MAX;hw_ready=1;KeyEvent(KEY1);
  for(n=0;n<7;++n)KeyEvent(0);
  CHECK(shown_valid && Frame144_Valid(last_frame) && _State.Value==RUN && last_frame->capture.epoch!=epoch);
  HoldP0Stop();
 }
 /* Synchronous AUTO fallback has no recoverable old raw data once reading
  * starts. A K1 during that read finishes the no-trigger epoch and freezes it. */
 HoldP0Start(AUTO);hw_ready=0;KeyEvent(0);epoch=last_frame->capture.epoch;
 hold_at=200;fake_cycles+=101u*72000u;KeyEvent(0);
 CHECK(_State.Value==HOLD && shown_valid && !hw_waiting && fifo_index==4097);
 CHECK(last_frame->capture.epoch!=epoch && last_frame->capture.trigger_kind==CAPTURE_TRIGGER_NONE);
 CHECK(last_frame->capture.valid_begin==152 && last_frame->capture.time_samples==7888);
 records=record_count;KeyEvent(0);CHECK(_State.Value==HOLD && record_count==records);
 HoldP0Stop();
 /* All press parities, including rollover, are consumed once inside Poll. */
 for(j=1;j<=4;++j){
  Key1PressCount=0xfffffffeu;HoldP0Start(NORH);hw_ready=0;KeyEvent(0);
  hold_count=j;hold_at=512;hw_ready=1;for(n=0;n<8;++n)KeyEvent(0);
  CHECK(shown_valid && !hw_waiting && _State.Value==((j&1)?HOLD:RUN));
  CHECK((uint32_t)Key1PressCount==(uint32_t)(0xfffffffeu+j));
  Key_Buffer=KEY1;hw_ready=0;CHECK(ServiceUntilFrame());
  CHECK(_State.Value==((j&1)?HOLD:RUN));HoldP0Stop();
 }
 /* Repeat steps retain the Normal policy in both buffer modes. Physical
  * acquisition flags in 144 are not allowed to accelerate the UI again. */
 for(m=0;m<2;++m){int start;unsigned step;
  hold_normal_mode=m;HoldP0Start(NORH);KeyEvent(KEY1);CHECK(_State.Value==HOLD);
  start=_1_posi;SetLowVolume=1;step=m?2:3;reads=analog_command[CH_A_OFFSET];
  KeyEvent(K_INDEX_INC);CHECK(_1_posi==start+(int)step);
  KeyEvent(K_INDEX_DEC);CHECK(_1_posi==start && analog_command[CH_A_OFFSET]==reads);
  SetLowVolume=0;KeyEvent(K_INDEX_INC);CHECK(_1_posi==start+1);
  KeyEvent(K_INDEX_DEC);CHECK(_1_posi==start);HoldP0Stop();
 }
 hold_normal_mode=1;
 /* Before the very first complete epoch, HOLD remains honestly empty. */
 hw_test=1;hw_ready=0;_Mode=NORH;_State.Value=RUN;_T_base=21;
 Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
 CHECK(ServiceUntilFrame() && hw_waiting && !shown_valid);
 KeyEvent(KEY1);CHECK(_State.Value==HOLD && !hw_waiting && !shown_valid && !Frame144_Valid(last_frame));
 HoldP0Stop();hw_test=hw_ready=0;
 printf("PASS HOLD P0 matrix: %u cumulative checks; untouched pending snapshot, finish-on-HOLD, partial faults, AUTO fallback; mock HW\n",checks);
}

int main(void)
{
    unsigned i,a;static char gen_names[230]={0};
    CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
    StartupImageCheck();
    xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
    for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
    Title[OUTPUT][KIND].Str=gen_names;Title[OUTPUT][FRQN].Str=gen_names;
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    Title[0][COUPLE].Value=DC;Detail[0]=COUPLE;
    CHECK(!ServiceUntilFrame());
    Current=TRACK1;Key_Buffer=K_INDEX_INC;
    CHECK(!Wildcat144_ModeKey());CHECK(Title[0][COUPLE].Value==DC);
    Key_Buffer=K_ITEM_S;CHECK(!Wildcat144_ModeKey());
    FlagFrameMode=1;CHECK(!Wildcat144_ModeKey());
    OSBuffer=1;OSAvg=1;CHECK(!Wildcat144_ModeKey());
    OSAvg=0;CHECK(!Wildcat144_ModeKey());
    FPGAosFlag=1;
    CHECK(Wildcat144_ModeKey());CHECK(Title[0][COUPLE].Value==SCOPE144_DC);
    /* A user-defined page survives panel toggles; A+B defaults have their own regression. */
    UpdateMeter=0;FlagMeter=1;Meter[1].Item=FRQ;Meter[1].Track=TRACK2;
    Title[0][RANGE].Value=4;Title[0][POSI].Value=100;Title[RUNNING][STATE].Value=HOLD;
    Title[OUTPUT][KIND].Value=6;Title[T_BASE][BASE].Value=20;
    CHECK(ServiceUntilFrame());CHECK(Wildcat144_Active() && ui_calls==1 && _State.Value==HOLD && MAX_X==311 && FlagMeter==1);
    CHECK(Meter[1].Item==FRQ && Meter[1].Track==TRACK2 && !app_clears && !screen_clears);
    CHECK(adc_b==8 && adc_a==4);CHECK(Current==TRACK1 && Detail[0]==COUPLE);
    CHECK(cal_loads==1 && _T_base==20);
    /* K4 uses the common A detail cycle without leaving HOLD or acquiring. */
    {unsigned d,records=record_count,out=outputs;
     for(d=0;d<4;++d) {
         Detail[TRACK1]=(u8)d;KeyEvent(KEY4);
         CHECK(Detail[TRACK1]==(d+1)%4 && _State.Value==HOLD && Wildcat144_Active());
     }
     CHECK(record_count==records && outputs==out);Detail[TRACK1]=COUPLE;
    }
    {unsigned item=Meter[1].Item,track=Meter[1].Track;
     KeyEvent(KEY3);CHECK(FlagMeter==2 && MAX_X==311);
     KeyEvent(KEY3);CHECK(FlagMeter==0 && MAX_X==397);
     KeyEvent(KEY3);CHECK(FlagMeter==1 && MAX_X==311 && panel_clears==3);
     CHECK(Meter[1].Item==item && Meter[1].Track==track);}
    Current=TRACK1;Detail[0]=COUPLE;
    KeyEvent(K_INDEX_INC);CHECK(Title[0][COUPLE].Value==SCOPE144_AC);CHECK(_State.Value==HOLD);
    { CalMean mean;
      history=1;
      expected_mode=SEPARATE;watch_settle=1;wrong_settle=0;
      CHECK(WildcatCalibration_Capture(4,0,94,94,&mean)==1);
      watch_settle=0;CHECK(!wrong_settle);
      CHECK(adc_a==4 && adc_b==4 && adc_mode==SEPARATE);
      CHECK(mean.a==55*1024 && mean.b==55*1024 && mean.clips==0);
      expected_mode=SEPARATE;watch_settle=1;wrong_settle=0;
      CHECK(WildcatCalibration_Capture(4,1,40,40,&mean)==1);
      watch_settle=0;CHECK(!wrong_settle);
      CHECK(adc_b==8 && adc_mode==SEPARATE && clock_arr==239 && mean.clips==0);
      history=0;
    }
    { CalMean mean;
      CHECK(CalScenario(7,0,&mean)==1); /* Completely clipped first full record is ignored. */
      CHECK(record_count==4 && mean.a==55*1024 && mean.b==55*1024);
      CHECK(CalScenario(1,0,&mean)==1); /* B slowly settles from the previous path. */
      CHECK(record_count>=6 && mean.a==55*1024 && mean.b==55*1024);
      CHECK(CalScenario(1,1,&mean)==1);CHECK(record_count>=6);
      CHECK(CalScenario(9,0,&mean)==1);
      CHECK((uint32_t)(fake_cycles-capture_started)>=600u*72000u && mean.b<=56*1024);
      CHECK(CalScenario(2,0,&mean)==1); /* Whole consecutive record averages sample noise. */
      CHECK(record_count==4 && mean.a==55*1024 && mean.b==57*1024);
      CHECK(CalScenario(2,1,&mean)==1 && mean.b==57*1024);
      CHECK(CalScenario(10,0,&mean)==CAL_CAPTURE_UNSTABLE); /* Entire block means keep alternating. */
      { CalCaptureStats stats;
        WildcatCalibration_Diagnostics(&stats);
        CHECK(stats.blocks>3 && stats.window==3 && stats.spread_b==4*1024 && stats.spread_a==0);
      }
      CHECK(CalScenario(11,0,&mean)==1 && record_count==5 && !mean.clips);
      CHECK(CalScenario(12,0,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==512);
      {CalCaptureStats stats;WildcatCalibration_Diagnostics(&stats);
       CHECK(!stats.clips_a && stats.clips_b==512 && stats.min_a==55 && stats.max_a==55);
       CHECK(stats.min_b==55 && stats.max_b==255 && !stats.window && !stats.spread_a && !stats.spread_b);}
      CHECK(CalScenario(13,0,&mean)==1 && mean.a==165*1024 && mean.b==163*1024 && !mean.clips);
      {CalCaptureStats stats;WildcatCalibration_Diagnostics(&stats);
       CHECK(mean.a>160*1024 && mean.a<166*1024 && mean.b>160*1024 && mean.b<166*1024);
       CHECK(stats.clips_a==1 && stats.clips_b==1 && stats.min_a==0 && stats.max_a==165);
       CHECK(stats.min_b==163 && stats.max_b==255 && stats.window==3 && !stats.spread_a && !stats.spread_b);}
      CHECK(CalScenario(8,0,&mean)==CAL_CAPTURE_UNSTABLE); /* 0.75-step drift per frame. */
      CHECK(CalScenario(3,0,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==4096);
      CHECK(CalScenario(4,0,&mean)==-1 && record_count==0);
      CHECK(CalScenario(5,0,&mean)==-1 && record_count==2);
      CHECK(CalScenario(6,0,&mean)==0);
      CHECK(CalScenario(0,0,&mean)==1 && record_count==4); /* Cancellation/timeout leaves no stale stability state. */
      fake_cycles=0xffff0000u;
      CHECK(CalScenario(0,0,&mean)==1); /* Deadline and block span survive DWT wrap. */
      CHECK(CalScenario(14,0,&mean)==1 && mean.a==55*1024+1 && mean.b==55*1024+1);
      CHECK(fifo_index==4096); /* First and last word both count; no extra prime/history. */
      CHECK(CalScenario(15,1,&mean)==1 && mean.a==55*1024 && mean.b==55*1024 && !mean.clips);
      {CalCaptureStats st;WildcatCalibration_Diagnostics(&st);
       CHECK(st.clips_a==1 && st.clips_b==1 && st.min_a==0 && st.max_b==255);}
      CHECK(CalScenario(17,0,&mean)==1 && mean.a==70*1024 && mean.b==95*1024);
      CHECK(CalScenario(17,1,&mean)==1 && mean.a==70*1024 && mean.b==72*1024);
      CHECK(CalScenario(17,0,&mean)==1 && mean.b==95*1024);
      CHECK(CalScenario(18,0,&mean)==1 && mean.a==169908 && mean.b==168628 && !mean.clips);
      {CalCaptureStats st;WildcatCalibration_Diagnostics(&st);
       CHECK(st.clips_a==1 && st.clips_b==3 && st.min_a==87 && st.max_a==255 && st.min_b==72 && st.max_b==255);}
      CHECK(CalScenario(19,0,&mean)==1 && mean.a==100*1024 && mean.b==120*1024 && !mean.clips);
      {CalCaptureStats st;WildcatCalibration_Diagnostics(&st);CHECK(st.clips_a==4 && st.clips_b==4);}
      CHECK(CalScenario(19,1,&mean)==1 && !mean.clips); /* Up to eight disjoint pair exclusions accepted. */
      CHECK(CalScenario(20,0,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==5);
      CHECK(CalScenario(21,1,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==5);
      CHECK(CalScenario(22,1,&mean)==1 && mean.a==100*1024 && mean.b==120*1024 && !mean.clips);
      CHECK(CalScenario(23,1,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==4096 && !mean.a && !mean.b);
      CHECK(CalScenario(24,0,&mean)==CAL_CAPTURE_CLIPPED && !mean.clips);
      CHECK(CalScenario(25,1,&mean)==CAL_CAPTURE_CLIPPED && !mean.clips);
      CHECK(CalScenario(26,1,&mean)==1 && mean.a==10*1024 && mean.b==245*1024);
      CHECK(!WildcatCalibration_Capture(8,0,79,79,&mean));
      { CalCaptureStats stats;WildcatCalibration_Diagnostics(&stats);CHECK(!stats.records && !stats.blocks && !stats.elapsed_ms); }
    }
    /* Physical hold survives repeated events and DWT wrap. No mode/HOLD side effects. */
    fake_cycles=0xffff0000u;key_status&=~KEY2_STATUS;KeyEvent(KEY2);CHECK(!cal_calls);
    fake_cycles+=1000u*72000u;KeyEvent(KEY2);CHECK(!cal_calls);
    fake_cycles+=2001u*72000u;KeyEvent(KEY2);CHECK(cal_calls==1 && _State.Value==HOLD);
    KeyEvent(0); /* Release after returning from calibration. */
    {s16 mode_before=_Mode;KeyEvent(KEY2);CHECK(_Mode==mode_before && _State.Value==HOLD && TrgAuto==2);}
    key_status&=~KEY2_STATUS;KeyEvent(KEY2);fake_cycles+=600u*72000u;
    key_status|=KEY2_STATUS;KeyEvent(0);CHECK(Current==TRIGG && Detail[TRIGG]==2 && !TrgAuto);
    Current=TRACK1;Detail[0]=COUPLE;
    KeyEvent(KEY4);CHECK(Detail[0]==RANGE);
    KeyEvent(K_INDEX_INC);CHECK(adc_a==5 && Title[0][RANGE].Value==5);
    SelectGroup(OUTPUT);CHECK(Current==OUTPUT);CHECK(!files);
    Detail[OUTPUT]=KIND;KeyEvent(K_INDEX_INC);CHECK(Title[OUTPUT][KIND].Value==6 && outputs==1);
    Detail[OUTPUT]=KIND;KeyEvent(K_INDEX_DEC);CHECK(_Kind==6);
    KeyEvent(K_INDEX_DEC);CHECK(_Kind==5);
    KeyEvent(K_ITEM_INC);CHECK(Current==T_BASE);
    Detail[T_BASE]=BASE;KeyEvent(K_INDEX_DEC);CHECK(Title[T_BASE][BASE].Value==19);
    KeyEvent(KEY1);CHECK(Title[RUNNING][STATE].Value==RUN);CHECK(draws==1);CHECK(adc_mode==INTERLACE);
    CHECK(fifo_index==4097); /* One prime, 4096 data words, no sample copy. */
    CHECK((DataBuf[200]&0x30000)==(((201/32)&3)<<16) && !(DataBuf[200]&~0x3ffffu));
    held_epoch=last_frame->capture.epoch;held_hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
    { unsigned reads=fifo_index,records=record_count;
      KeyEvent(KEY1);CHECK(Title[RUNNING][STATE].Value==HOLD);
      CHECK(fifo_index==reads && record_count==records && last_frame->capture.epoch==held_epoch);
      CHECK(last_frame->capture.flags & CAPTURE_WINDOW_FROZEN);
      KeyEvent(KEY3);KeyEvent(KEY3);KeyEvent(KEY3); /* Panel changes redraw the same frame. */
      CHECK(fifo_index==reads && record_count==records && last_frame->capture.epoch==held_epoch);
      CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==held_hash); }
    BrowseHeld();
    {unsigned records=record_count;uint32_t epoch=last_frame->capture.epoch,start;
     Current=T_BASE;Detail[T_BASE]=XPOSI;KeyEvent(K_INDEX_INC);start=last_view.window_start_q16;
     KeyEvent(KEY1);CHECK(_State.Value==RUN && record_count==records+1 && last_frame->capture.epoch!=epoch);
     CHECK(last_view.window_start_q16==start);CheckWindow();
     KeyEvent(KEY1);CHECK(_State.Value==HOLD && record_count==records+1);
     KeyEvent(K_INDEX_DEC);CHECK(!last_view.window_start_q16);}
    held_epoch=last_frame->capture.epoch;held_hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
    /* Every tick must return: native main loop remains the owner. */
    for(i=0;i<30;++i){KeyEvent(K_ITEM_INC);CHECK(Current!=TRACK2 && Current!=RUNNING && Current<=21);if(Current>=13)KeyEvent(KEY4);}
    CHECK(!files);
    /* Disabled controls remain focusable, but no key may mutate them. */
    { unsigned g,d,k;const unsigned keys[]={KEY2,KEY3,K_INDEX_INC,K_INDEX_DEC};
      for(g=TRACK2;g<=TRACK4;++g)for(d=0;d<4;++d)for(k=0;k<4;++k) {
        s16 before[13][4];unsigned a,b,old_outputs=outputs,old_files=files;
        if(Scope144_Available(g,d,_Kind))continue;
        Current=(u8)g;Detail[g]=(u8)d;
        for(a=0;a<13;++a)for(b=0;b<4;++b)before[a][b]=Title[a][b].Value;
        KeyEvent(keys[k]);
        for(a=0;a<13;++a)for(b=0;b<4;++b)CHECK(Title[a][b].Value==before[a][b]);
        CHECK(outputs==old_outputs && files==old_files);
      }
      Current=TRACK1;while(FlagMeter!=1)KeyEvent(KEY3);
      Current=T_BASE;Detail[T_BASE]=2;
      {s16 before=_X_posi.Value;uint32_t start=last_view.window_start_q16;
       KeyEvent(K_INDEX_INC);CHECK(_X_posi.Value>before);
       KeyEvent(K_INDEX_DEC);CHECK(_X_posi.Value==before && last_view.window_start_q16==start);CheckWindow();}
      Current=TRIGG;Detail[TRIGG]=3;KeyEvent(K_INDEX_INC);CHECK(Title[TRIGG][3].Value==0);
      Current=FILE;Detail[FILE]=2;KeyEvent(KEY4);CHECK(Detail[FILE]==0 && !files);
      KeyEvent(K_ITEM_S);CHECK(Wildcat144_Active());
      Current=V_VERNIE;Detail[V_VERNIE]=0;a=Title[V_VERNIE][0].Value;KeyEvent(K_INDEX_DEC);CHECK(Title[V_VERNIE][0].Value==(s16)a-1);
      Current=T_VERNIE;Detail[T_VERNIE]=0;for(a=0;a<350;++a)KeyEvent(K_INDEX_INC);CHECK(_T1==299);
      Current=METER_0;Detail[13]=0;Meter[0].Item=VPP;KeyEvent(K_INDEX_INC);CHECK(Meter[0].Item==CIR && Meter[0].Track==TRACK1);
    }
    CHECK(!app_clears && !screen_clears && Meter[1].Item==FRQ && Meter[1].Track==TRACK2);
    Current=FILE;Detail[FILE]=1;KeyEvent(K_INDEX_INC);KeyEvent(KEY3);CHECK(files==1);
    Current=TRACK1;Detail[TRACK1]=RANGE;
    KeyEvent(K_ITEM_S);CHECK(Title[0][COUPLE].Value==AC);CHECK(adc_mode==SEPARATE);
    CHECK(!FlagFrameMode && !FrameMode && !OSBuffer && !OSAvg && !FPGAosFlag);
    CHECK(_T_base==20);
    CHECK(!ServiceUntilFrame());CHECK(files==1 && native_releases==1);
    CHECK(Meter[1].Item==FRQ && Meter[1].Track==TRACK2 && FlagMeter==1);
    /* Re-enter without forcing a different panel style or page. */
    FlagMeter=2;_Mode=SCAN;Title[0][COUPLE].Value=SCOPE144_DC;CHECK(ServiceUntilFrame());CHECK(FlagMeter==2);
    Current=T_BASE;Detail[T_BASE]=MODE;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && FlagMeter==2 && _Mode==SCAN);
    FlagMeter=0;MAX_X=397;Title[0][COUPLE].Value=AC;
    Title[T_BASE][BASE].Value=8;
    FlagFrameMode=1;OSBuffer=1;OSAvg=0;FPGAosFlag=1;
    Current=OUTPUT;Detail[OUTPUT]=KIND;Key_Buffer=K_ITEM_S;
    CHECK(Wildcat144_ModeKey() && Title[0][COUPLE].Value==SCOPE144_AC);
    CHECK(ServiceUntilFrame());CHECK(!FlagMeter && MAX_X==397);
    CHECK(_T_base==20); /* Last 144 base, not an automatic jump to 0.1 us. */
    Current=T_BASE;Detail[T_BASE]=BASE;KeyEvent(K_INDEX_INC);CHECK(_T_base==21);
    Current=METER_0;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && !FlagMeter);
    CHECK(Title[0][COUPLE].Value==AC);
    CHECK(Title[0][RANGE].Value==5 && Title[T_BASE][BASE].Value==8 && Title[OUTPUT][KIND].Value==5);
    /* Repeated switching remembers the last 144 base only in RAM; no save call. */
    Title[T_BASE][BASE].Value=7;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
    CHECK(ServiceUntilFrame());CHECK(_T_base==21);
    Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(_T_base==7 && files==1);
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0);
    FlagFrameMode=1;OSBuffer=1;OSAvg=0;FPGAosFlag=1;
    Title[0][COUPLE].Value=DC;Key_Buffer=K_ITEM_S;
    CHECK(!Wildcat144_ModeKey());CHECK(Title[0][COUPLE].Value==DC);
    HardwareTriggers();
    HoldDuringPoll();
    TriggerNavigation();
    DigitalUI();
    AllTriggerKinds();
    AutoLevels();
    NoiseCaptures();
    FailedTransfer();
    /* Real cooperative preview: settling/FIFO waits yield, reads are bounded,
     * first full record is discarded, all 4096 words are included, and cancellation is a phase reset. */
    {
        CalPreview p={0};CalMean mean={0,0,0};unsigned calls=0,configured;int result=0;
        WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102); /* Match live mocked ID. */
        history=1;record_count=0;capture_case=0;
        do {
            unsigned before_fifo=fifo_index,before_records=record_count;
            result=WildcatCalibration_Preview(&p,0,79,79,&mean);
            CHECK(++calls<100000 && result>=0);
            if(record_count==before_records)CHECK(fifo_index-before_fifo<=32);
        } while(!result);
        CHECK(calls>100 && record_count==2 && mean.a==55*1024 && mean.b==55*1024 && !mean.clips);
        CHECK(p.phase==5);configured=range_sets;capture_case=2;
        CHECK(WildcatCalibration_Preview(&p,0,79,79,&mean)==0 && p.phase==4);
        do{result=WildcatCalibration_Preview(&p,0,79,79,&mean);CHECK(++calls<100000);}while(!result);
        CHECK(result==1 && record_count==3 && mean.a==55*1024 && mean.b==57*1024 && !mean.clips && range_sets==configured);
        capture_case=13;
        do{result=WildcatCalibration_Preview(&p,0,79,79,&mean);CHECK(++calls<100000);}while(!result);
        CHECK(result==1 && !mean.clips && mean.a==165*1024 && mean.b==163*1024 && p.clips==2 && p.clips_a==1 && p.clips_b==1 && range_sets==configured);
        history=0;record_count=0;p.phase=0;capture_case=6;calls=0;
        do{result=WildcatCalibration_Preview(&p,0,79,79,&mean);CHECK(++calls<250000);}while(!result);
        CHECK(result==-1 && !p.phase);capture_case=0;
        CHECK(WildcatCalibration_Preview(&p,0,79,79,&mean)==0 && p.phase==1);
        p.phase=0; /* Wizard processes a key immediately, with no synchronous wait. */
        CHECK(WildcatCalibration_Preview(&p,1,78,80,&mean)==0 && p.range==1 && p.commands[1]==80);
    }
    /* Twenty ordinary transitions: preserve layout, files, generator and blank
     * the old capture in HOLD, without clearing any full screen or panel. */
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    {unsigned round,full=screen_clears,app=app_clears,panels=panel_clears;
     unsigned frames=frame_clears,trans=transitions,releases=native_releases;
     for(round=0;round<20;++round) {
        _Kind=6;FlagMeter=(u8)(round%3);MAX_X=FlagMeter?311:397;
        _Mode=AUTO;_State.Value=(round&1)?RUN:HOLD;_A_Range=(s16)(round%8);
        _3_source=round&1;_4_source=(round>>1)&1;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[0]=SOURCE;
        Update=2;SelectiveUpdate=1;
        CHECK(ServiceUntilFrame());CHECK(Wildcat144_Active() && ((round&1) || !shown_valid) && !Update && !SelectiveUpdate);
        /* Cursor navigation during repeated Normal/144 RUN/HOLD transitions
         * must retain cursor positions and mode, with every panel layout. */
        {unsigned g,d; s16 v1=_V1_Vernie,v2=_V2_Vernie,t1=_T1,t2=_T2;
         for(g=0;g<2;++g) {
             Current=g?T_VERNIE:V_VERNIE;
             for(d=0;d<Scope144_DetailLimit(Current);++d) {
                 Detail[Current]=(u8)d;KeyEvent(KEY4);
                 CHECK(Detail[Current]==(d+1)%Scope144_DetailLimit(Current));
                 CHECK(_State.Value==((round&1)?RUN:HOLD) && Wildcat144_Active());
             }
         }
         CHECK(_V1_Vernie==v1 && _V2_Vernie==v2 && _T1==t1 && _T2==t2);
        }
        Current=OUTPUT;Detail[OUTPUT]=KIND;
        {unsigned before=outputs;KeyEvent(K_INDEX_INC);CHECK(outputs<=before+1);}
        KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && _State.Value==((round&1)?RUN:HOLD));
        CHECK(_A_Range==(s16)(round%8) && FlagMeter==(u8)(round%3));
     }
     CHECK(screen_clears==full && app_clears==app && panel_clears==panels);
     CHECK(transitions==trans+40 && native_releases==releases+20 && frame_clears>=frames+30);
    }
    /* FFT144 uses ordinary D navigation; generated samples stay frozen in
     * HOLD, while source/gain edits and generator IRQ traffic remain live. */
    {unsigned r;const unsigned noise_rates[3]={0,10,18};
     for(r=0;r<3;++r) {
        unsigned records,reads,out;uint32_t hash,epoch,done;
        _Kind=5;_Frqn=(s16)noise_rates[r];_A_Range=0;_Mode=AUTO;_State.Value=RUN;
        _4_source=1;Title[TRACK4][RANGE].Value=2;ShowFFT=0;AutoFFT=7;FFTGain=4;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK4;Detail[TRACK4]=SOURCE;
        capture_case=2;history=1;CHECK(ServiceUntilFrame() && Wildcat144_Active());
        out=outputs;KeyEvent(K_INDEX_INC);CHECK(_4_source==14 && Fft144.ready && ShowFFT);
        CHECK(Fft144.source==14 && !Fft144.clipped && outputs==out && _Kind==5 && _Frqn==(s16)noise_rates[r]);
        {s16 pos=_4_posi;Detail[TRACK4]=POSI;KeyEvent(K_INDEX_INC);CHECK(_4_posi==pos && _4_source==14);Detail[TRACK4]=SOURCE;}
        KeyEvent(KEY4);CHECK(Detail[TRACK4]==RANGE);
        KeyEvent(KEY1);CHECK(_State.Value==HOLD);fake_cycles+=8000000;KeyEvent(0);
        records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;
        hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);done=Fft144.last_done;
        KeyEvent(K_INDEX_INC);CHECK(Title[TRACK4][RANGE].Value==3);
        fake_cycles+=8000000;KeyEvent(0);CHECK(Fft144.setting==3 && Fft144.last_done!=done);
        CHECK(record_count==records && fifo_index==reads && last_frame->capture.epoch==epoch);
        CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
        KeyEvent(KEY4);CHECK(Detail[TRACK4]==SOURCE);
        KeyEvent(K_INDEX_INC);CHECK(_4_source==15 && Fft144.source==15 && Fft144.ready);
        CHECK(record_count==records && fifo_index==reads && outputs==out);
        /* HOLD changes FFT sample rate/window, never the captured samples.
         * Check both axis transitions and the common XPOS-centered window. */
        {unsigned j,a,b;
         Current=T_BASE;Detail[T_BASE]=BASE;
         while(_T_base!=21)KeyEvent(K_INDEX_INC);
         for(j=0;j<3;++j) {
            unsigned d=j==2?4:2,half=16*d-1,last;
            int64_t centered;unsigned first;
            fake_cycles+=8000000;KeyEvent(0);
            CHECK(_T_base==(s16)(21-j) && Fft144.ready && ShowFFT);
            CHECK(Fft144.rate_hz==(j==2?36000000u:72000000u));
            centered=(int64_t)last_view.window_start_q16+
                ((int64_t)(last_view.visible_columns-1)*last_view.step_q16-(int64_t)511*d*65536)/2;
            first=centered>0?(unsigned)(centered/65536):0;
            last=last_frame->capture.time_samples-1-half-511*d;
            if(first<half)first=half;
            if(first>last)first=last;
            CHECK(Fft144.start==first && Fft144.epoch==epoch);
            CHECK(!Wildcat144_Overview(&a,&b) && !a && !b);
            CHECK(record_count==records && fifo_index==reads && outputs==out);
            CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
            if(j<2)KeyEvent(K_INDEX_DEC);
         }
         KeyEvent(K_INDEX_INC);fake_cycles+=8000000;KeyEvent(0);
         CHECK(_T_base==20 && Fft144.ready && Fft144.rate_hz==72000000u);
         Current=TRACK4;Detail[TRACK4]=SOURCE;
        }
        KeyEvent(KEY4);CHECK(Detail[TRACK4]==RANGE);KeyEvent(KEY4);CHECK(Detail[TRACK4]==SOURCE);
        KeyEvent(K_INDEX_INC);CHECK(!_4_source && !Fft144.ready && !ShowFFT);
        KeyEvent(K_INDEX_INC);CHECK(_4_source==1);KeyEvent(KEY4);CHECK(Detail[TRACK4]==POSI);
        KeyEvent(KEY4);CHECK(Detail[TRACK4]==SOURCE);
        /* Every digit/source direction visits only OFF/D/hFFT_A/sFFT_A. */
        KeyEvent(K_INDEX_DEC);CHECK(!_4_source);KeyEvent(K_INDEX_DEC);CHECK(_4_source==15);
        Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && !Fft144.ready);
        CHECK(_4_source==1 && AutoFFT==7 && FFTGain==4 && !ShowFFT);
        CHECK(_Kind==5 && _Frqn==(s16)noise_rates[r] && outputs==out);
     }
     capture_case=history=0;
    }
    /* HOLD idles without repainting an unchanged trace or touching capture.
     * Blink/navigation must still repaint; all service/menu calls continue. */
    {
        unsigned d,u,r,f;uint32_t hash,epoch;
        hw_test=hw_ready=0;capture_case=2;history=1;key_status=0xffff;
        _Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;_State.Value=RUN;
        _A_Range=0;_1_source=1;_4_source=0;Current=TRACK1;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;CHECK(ServiceUntilFrame());
        KeyEvent(KEY1);CHECK(_State.Value==HOLD && Frame144_Valid(last_frame));
        d=draws;u=ui_calls;r=record_count;f=fifo_index;
        hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);epoch=last_frame->capture.epoch;
        for(i=0;i<100;++i)KeyEvent(0);
        CHECK(draws==d && ui_calls==u+100);
        CHECK(record_count==r && fifo_index==f && last_frame->capture.epoch==epoch);
        CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
        Twink=!Twink;KeyEvent(0);CHECK(draws==d+1);
        Current=TRACK1;Detail[TRACK1]=POSI;KeyEvent(K_INDEX_INC);CHECK(draws==d+2);
        CHECK(record_count==r && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
        KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());capture_case=history=0;
    }
    /* A physical resume press is still down when Begin polls cancellation.
     * It must be accepted once, not immediately cancel its own acquisition. */
    {
        hw_test=1;hw_ready=0;capture_case=0;history=0;key_status=0xffff;
        _Mode=NORH;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;_A_Range=0;_1_source=1;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;
        CHECK(ServiceUntilFrame() && hw_waiting);
        KeyEvent(KEY1);CHECK(_State.Value==HOLD && !hw_waiting);
        key_status&=~KEY1_STATUS;KeyEvent(KEY1);
        CHECK(_State.Value==RUN && hw_waiting); /* Regression: used to self-cancel. */
        Key_Buffer=KEY1;CHECK(ServiceUntilFrame() && _State.Value==RUN); /* same queued event */
        key_status|=KEY1_STATUS;KeyEvent(0);CHECK(_State.Value==RUN);
        key_status&=~KEY1_STATUS;KeyEvent(KEY1);CHECK(_State.Value==HOLD && !hw_waiting);
        key_status|=KEY1_STATUS;KeyEvent(0);
        KeyEvent(KEY1);CHECK(_State.Value==RUN && hw_waiting);
        /* Two complete presses between service calls retain their parity. */
        Key1PressCount+=2;Key_Buffer=KEY1;CHECK(ServiceUntilFrame() && _State.Value==RUN && hw_waiting);
        KeyEvent(KEY1);CHECK(_State.Value==HOLD);
        Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());hw_test=hw_ready=0;
    }
    /* AUTO must keep acquiring a clipped/DC record even when its automatic
     * trigger lies beyond the hardware guard band. Never invent an edge. */
    { unsigned rail,repeat,records;
      for(rail=27;rail<=28;++rail) {
        hw_test=0;hw_ready=1;capture_case=rail;history=0;key_status=0xffff;
        _Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;_State.Value=RUN;TrgAuto=2;
        _A_Range=4;_1_source=1;_4_source=0;_T_base=21;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=RANGE;
        CHECK(ServiceUntilFrame() && _State.Value==RUN && shown_valid);
        CHECK(shown_measure.clips==8188);
        CHECK(shown_measure.clip_rails==(rail==27?2u:1u));
        for(repeat=0;repeat<3;++repeat) {
          records=record_count;KeyEvent(0);
          CHECK(_State.Value==RUN && shown_valid &&
                shown_measure.clips==2*(4096-INTERLACE144_DISCARD_PAIRS));
          CHECK(last_frame->capture.valid_begin==INTERLACE144_DISCARD_PAIRS);
          CHECK(shown_measure.clip_rails==(rail==27?2u:1u));
          CHECK(record_count==records+1 && fifo_index==4097);
          CHECK(!(last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
        }
        /* A further sensitivity change must not leave a stale voltage
         * threshold stopping AUTO; K1 remains the explicit RUN/HOLD action. */
        KeyEvent(K_INDEX_DEC);CHECK(_A_Range==3 && _State.Value==RUN);
        KeyEvent(KEY1);CHECK(_State.Value==HOLD);records=record_count;KeyEvent(0);
        CHECK(_State.Value==HOLD && record_count==records);
        KeyEvent(KEY1);CHECK(_State.Value==RUN && shown_valid);
        KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
      }
      capture_case=history=hw_test=hw_ready=TrgAuto=0;
    }
    /* Repeated captures/HOLD/menu navigation do not rewrite unchanged
     * analog inputs. A real range/coupling change writes just that command. */
    {unsigned saved[13],obj,n;
     hw_test=0;hw_ready=1;capture_case=2;history=0;key_status=0xffff;
     _Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;_State.Value=RUN;TrgAuto=0;
     _A_Range=4;_1_source=1;_4_source=0;_T_base=21;
     Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=RANGE;
     CHECK(ServiceUntilFrame());memcpy(saved,analog_writes,sizeof saved);
     for(n=0;n<20;++n)KeyEvent(0);CHECK(!memcmp(saved,analog_writes,sizeof saved));
     KeyEvent(K_INDEX_DEC);CHECK(_A_Range==3);++saved[CH_A_RANGE];
     CHECK(!memcmp(saved,analog_writes,sizeof saved));
     Detail[TRACK1]=COUPLE;KeyEvent(K_INDEX_INC);++saved[CH_A_COUPLE];
     CHECK(!memcmp(saved,analog_writes,sizeof saved));
     KeyEvent(KEY1);CHECK(_State.Value==HOLD);
     for(n=0;n<20;++n)KeyEvent(0);CHECK(!memcmp(saved,analog_writes,sizeof saved));
     KeyEvent(KEY1);CHECK(_State.Value==RUN);
     CHECK(!memcmp(saved,analog_writes,sizeof saved));
     KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
     Title[TRACK1][COUPLE].Value=SCOPE144_AC;_State.Value=RUN;
     memcpy(saved,analog_writes,sizeof saved);CHECK(ServiceUntilFrame());
     for(obj=0;obj<13;++obj)if(obj<=CH_B_OFFSET || (obj>=CH_A_COUPLE && obj<=CH_B_RANGE))++saved[obj];
     CHECK(!memcmp(saved,analog_writes,sizeof saved));
     KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());capture_case=hw_ready=0;
    }
    {unsigned n,old_panel,old_files,old_cal;int old_kind;u32 hash;
     capture_case=2;hw_test=0;hw_ready=1;history=0;key_status=0xffff;
     _Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;_State.Value=RUN;TrgAuto=0;
     _A_Range=4;_1_source=1;_4_source=0;_T_base=21;ADCoffset=54;PPM_Comp=0;
     Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=T_BASE;Detail[T_BASE]=BASE;
     CHECK(ServiceUntilFrame());old_cal=cal_calls;
     key_status&=~KEY2_STATUS;KeyEvent(KEY2);fake_cycles+=3000u*72000u;KeyEvent(0);
     CHECK(OffsetSelect==1 && cal_calls==old_cal && Current==T_BASE);
     key_status|=KEY2_STATUS;KeyEvent(0);
     for(n=0;n<60;++n)KeyEvent(K_INDEX_DEC);
     CHECK(!ADCoffset && _T_base==21 && _State.Value==RUN);
     CHECK(analog_command[CH_A_OFFSET]==(unsigned)_1_posi && analog_command[CH_B_OFFSET]==(unsigned)_1_posi);
     for(n=0;n<70;++n)KeyEvent(K_INDEX_INC);
     CHECK(ADCoffset==54 && _T_base==21 && _State.Value==RUN);
     CHECK(analog_command[CH_A_OFFSET]==54u+(unsigned)_1_posi && analog_command[CH_B_OFFSET]==54u+(unsigned)_1_posi);
     KeyEvent(KEY1);CHECK(_State.Value==HOLD);hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
     /* K1 leaves the service editor like Normal; reopen through real K2. */
     key_status&=~KEY2_STATUS;KeyEvent(KEY2);fake_cycles+=3000u*72000u;KeyEvent(0);
     key_status|=KEY2_STATUS;KeyEvent(0);CHECK(OffsetSelect==1);
     KeyEvent(K_INDEX_DEC);CHECK(ADCoffset==53 && _State.Value==HOLD && shown_valid && presentation_available);
     CHECK(analog_command[CH_A_OFFSET]==54u+(unsigned)_1_posi && analog_command[CH_B_OFFSET]==54u+(unsigned)_1_posi);
     CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
     KeyEvent(K_INDEX_S);CHECK(OffsetSelect==2 && Wildcat144_Active());
     PPM_Comp=998;KeyEvent(K_INDEX_INC);CHECK(PPM_Comp==999);
     KeyEvent(K_INDEX_INC);CHECK(PPM_Comp==999);
     PPM_Comp=-998;KeyEvent(K_INDEX_DEC);CHECK(PPM_Comp==-999);
     KeyEvent(K_INDEX_DEC);CHECK(PPM_Comp==-999);
     CHECK(_State.Value==HOLD && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
     KeyEvent(K_INDEX_S);CHECK(OffsetSelect==1);
     KeyEvent(K_ITEM_INC);CHECK(!OffsetSelect);
     /* K3 extra long selects CFG once; repeated events/release never save. */
     Current=OUTPUT;Detail[OUTPUT]=KIND;old_kind=_Kind;old_panel=FlagMeter;old_files=files;ConfigFileNumber=7;
     key_status&=~KEY3_STATUS;KeyEvent(KEY3);fake_cycles+=1400u*72000u;KeyEvent(KEY3);
     CHECK(Current==OUTPUT && FlagMeter==old_panel && files==old_files);
     fake_cycles+=101u*72000u;KeyEvent(KEY3);
     CHECK(Current==FILE && Title[FILE][0].Value==SAVE && Title[FILE][2].Value==CFG && Title[FILE][1].Value==7);
     CHECK(FlagMeter==old_panel && _Kind==old_kind && files==old_files);
     KeyEvent(KEY3);CHECK(files==old_files);key_status|=KEY3_STATUS;KeyEvent(0);CHECK(files==old_files);
     KeyEvent(KEY3);CHECK(files==old_files+1 && ConfigFileNumber==7 && _State.Value==HOLD);
     CHECK(Current==OUTPUT && file_messages && last_file_message==OK && !UiCommon_Message()[0]);
     CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
     /* Unsupported SAVE types and CFG slot bounds remain constrained. */
     Current=FILE;Detail[FILE]=2;KeyEvent(K_INDEX_INC);CHECK(Title[FILE][2].Value==CSV);
     Title[FILE][1].Value=999;KeyEvent(K_INDEX_INC);CHECK(Title[FILE][2].Value==CFG && Title[FILE][1].Value==255);
     Detail[FILE]=1;KeyEvent(K_INDEX_INC);CHECK(!Title[FILE][1].Value);
     Detail[FILE]=2;KeyEvent(K_INDEX_DEC);CHECK(Title[FILE][2].Value==CSV);
     Current=T_BASE;key_status&=~KEY3_STATUS;KeyEvent(KEY3);fake_cycles+=800u*72000u;
     key_status|=KEY3_STATUS;KeyEvent(0);CHECK(FlagMeter==old_panel && _Kind==old_kind);
     KeyEvent(KEY1);CHECK(_State.Value==RUN);
     CHECK(last_frame->capture.sample_period_ps_q16==Scope144_TimePs(65536));
     CHECK(last_frame->calibration.adc_offset==53);
     hw_test=1;hw_ready=0;_Tr_source=TRACK1;Current=FILE;Title[FILE][0].Value=SAVE;Title[FILE][2].Value=CFG;
     CHECK(Wildcat144_Service());old_files=files;KeyEvent(KEY3);
     CHECK(files==old_files+1 && _State.Value==RUN && Current==OUTPUT && !UiCommon_Message()[0] && last_file_message==OK);
     Current=FILE;config_fails=1;KeyEvent(KEY3);CHECK(_State.Value==RUN && Current==FILE && last_file_message==WR_ERR && !strcmp(UiCommon_Message(),"WRITEERR"));config_fails=0;
     UartFileSize=32;old_files=files;KeyEvent(KEY3);
     CHECK(files==old_files && _State.Value==RUN && !strcmp(UiCommon_Message(),"UARTBUSY"));UartFileSize=0;
     /* Entering FILE by the rocker also remembers its previous item.
      * Confirmation through the left rocker returns there once, without
      * swallowing K1 published by IRQ during the native modal message. */
     hw_test=0;hw_ready=1;Current=T_VERNIE;KeyEvent(K_ITEM_DEC);CHECK(Current==FILE);
     old_files=files;KeyEvent(K_INDEX_S);
     CHECK(Current==T_VERNIE && files==old_files+1 && last_file_message==OK);
     KeyEvent(K_ITEM_DEC);CHECK(Current==FILE);file_hold_event=1;KeyEvent(K_INDEX_S);KeyEvent(0);
     CHECK(Current==T_VERNIE && _State.Value==HOLD);
     Current=T_BASE;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
     ADCoffset=54;PPM_Comp=0;ConfigFileNumber=0;capture_case=0;
    }
    {
        unsigned a,b,epoch,n;Scope144Measurements frozen;uint32_t raw_crc;
        wide_test=1;capture_case=2;history=hw_test=0;hw_ready=1;key_status=0xffff;
        ADCoffset=54;PPM_Comp=0;_Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;
        _A_Range=4;_1_posi=100;_T_base=21;_1_source=1;_4_source=0;_State.Value=RUN;TrgAuto=0;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=POSI;
        CHECK(ServiceUntilFrame());CHECK(shown_valid && last_frame->capture_ypos==100);
        CHECK(analog_command[CH_A_OFFSET]==154 && analog_command[CH_B_OFFSET]==158);
        frozen=shown_measure;
        for(n=0;n<2;++n) {
            KeyEvent(K_INDEX_DEC);CHECK(shown_valid && last_frame->capture_ypos==99-(int)n);
            CHECK(abs(shown_measure.average_uv-frozen.average_uv)<1000);
        }
        KeyEvent(KEY1);CHECK(_State.Value==HOLD);epoch=last_frame->capture.epoch;
        a=analog_command[CH_A_OFFSET];b=analog_command[CH_B_OFFSET];raw_crc=Scope144_RealCrc(DataBuf,sizeof DataBuf);
        frozen=shown_measure;
        for(n=0;n<20;++n)KeyEvent(K_INDEX_INC);
        for(n=0;n<20;++n)KeyEvent(K_INDEX_DEC);
        CHECK(analog_command[CH_A_OFFSET]==a && analog_command[CH_B_OFFSET]==b);
        CHECK(last_frame->capture.epoch==epoch && last_frame->capture_ypos==98);
        CHECK(last_frame->capture_dac==(a|(b<<16)) && shown_valid);
        CHECK(!memcmp(&frozen,&shown_measure,sizeof frozen) && Scope144_RealCrc(DataBuf,sizeof DataBuf)==raw_crc);
        KeyEvent(KEY1);CHECK(_State.Value==RUN && shown_valid);
        KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());wide_test=0;capture_case=0;
    }
    /* No file, legacy without window and malformed bank all acquire with
     * nominal units; HOLD freezes both DACs and the frame's calibration. */
    {
        unsigned kind,n,records,a,b,epoch;Scope144Cal frozen;
        for(kind=1;kind<=3;++kind) {
            cal_fixture_mode=kind;hw_test=0;history=0;capture_case=0;
            _State.Value=RUN;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;TrgAuto=0;
            _A_Range=4;_1_source=1;_4_source=0;_1_posi=100;_T_base=21;ADCoffset=40;
            Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=POSI;
            records=record_count;CHECK(ServiceUntilFrame());
            CHECK(shown_valid && record_count>records && _State.Value==RUN);
            CHECK(last_frame->calibration.valid==SCOPE144_NOMINAL);
            CHECK(analog_command[CH_A_OFFSET]==140 && analog_command[CH_B_OFFSET]==140);
            KeyEvent(K_INDEX_INC);CHECK(_1_posi==101 && last_frame->capture_ypos==101);
            KeyEvent(K_INDEX_DEC);CHECK(_1_posi==100 && last_frame->capture_ypos==100);
            for(n=0;n<100;++n)KeyEvent(K_INDEX_INC);
            CHECK(_1_posi==160 && analog_command[CH_A_OFFSET]==200 && analog_command[CH_B_OFFSET]==200);
            KeyEvent(K_INDEX_DEC);CHECK(_1_posi==159 && analog_command[CH_A_OFFSET]==199);
            KeyEvent(KEY1);CHECK(_State.Value==HOLD);epoch=last_frame->capture.epoch;
            frozen=last_frame->calibration;a=analog_command[CH_A_OFFSET];b=analog_command[CH_B_OFFSET];
            KeyEvent(K_INDEX_DEC);ADCoffset=54;KeyEvent(0);
            CHECK(_State.Value==HOLD && analog_command[CH_A_OFFSET]==a && analog_command[CH_B_OFFSET]==b);
            CHECK(last_frame->capture.epoch==epoch && !memcmp(&frozen,&last_frame->calibration,sizeof frozen));
            KeyEvent(KEY1);CHECK(_State.Value==RUN && shown_valid);
            CHECK(_1_posi==146 && analog_command[CH_A_OFFSET]==200 && analog_command[CH_B_OFFSET]==200);
            if(kind==1) {
                /* Return from a successful wizard reloads a measured profile;
                 * another failed/missing reload returns to nominal live data. */
                cal_fixture_mode=0;key_status&=~KEY2_STATUS;KeyEvent(KEY2);
                fake_cycles+=3001u*72000u;KeyEvent(0);KeyEvent(0);
                CHECK(shown_valid && last_frame->calibration.valid==1);
                cal_fixture_mode=1;key_status&=~KEY2_STATUS;KeyEvent(KEY2);
                fake_cycles+=3001u*72000u;KeyEvent(0);KeyEvent(0);
                CHECK(shown_valid && last_frame->calibration.valid==SCOPE144_NOMINAL);
            }
            KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
        }
        for(kind=0;kind<2;++kind) {
            cal_fixture_mode=1;hw_test=0;hw_ready=1;capture_case=0;
            _Mode=kind?SGL:NORH;_State.Value=RUN;_A_Range=4;_1_posi=100;
            _1_source=1;_4_source=0;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;ADCoffset=40;
            Title[TRACK1][COUPLE].Value=SCOPE144_DC;Current=TRACK1;Detail[TRACK1]=POSI;
            CHECK(ServiceUntilFrame() && shown_valid);
            CHECK(last_frame->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_A);
            CHECK(last_frame->calibration.valid==SCOPE144_NOMINAL);
            CHECK(_State.Value==(kind?HOLD:RUN));
            KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
        }
        cal_fixture_mode=0;
    }

    /* YPOS restarts may invalidate the raw epoch, but must not erase the
     * completed LCD plot while AUTO/NORM/SGL are acquiring its replacement. */
    {
        unsigned kind,n,clears,paints,resets,previews;uint32_t fft_hash=0;
        for(kind=0;kind<3;++kind) {
            cal_fixture_mode=1;hw_test=1;hw_ready=1;capture_case=0;history=0;
            _Mode=kind==2?SGL:kind==1?NORH:AUTO;_State.Value=RUN;
            _A_Range=4;_1_posi=100;_1_source=1;_4_source=kind?0:14;_Tr_source=TRACK1;_Tr_kind=1;
            _T_base=21;ADCoffset=40;TrgAuto=0;Current=TRACK1;Detail[TRACK1]=POSI;
            Title[TRACK1][COUPLE].Value=SCOPE144_DC;_Vt1=_1_posi;
            CHECK(Wildcat144_Service());for(n=0;n<7;++n)KeyEvent(0);
            CHECK(shown_valid && Frame144_Valid(last_frame));
            if(!kind){CHECK(Fft144.ready && ShowFFT);fft_hash=Scope144_RealCrc(fi,512*sizeof *fi);}
            clears=frame_clears;paints=draws;previews=preview_calls;hw_ready=0;
            if(kind==2)KeyEvent(KEY1);else KeyEvent(0);
            CHECK(hw_waiting==1 && _State.Value==RUN);paints=draws;
            for(n=0;n<5;++n) {
                resets=wait_resets;KeyEvent(K_INDEX_INC);
                CHECK(_1_posi==101+(int)n && shown_valid && hw_waiting==1 && _State.Value==RUN);
                CHECK(frame_clears==clears && draws==paints+n+1 && Frame144_Valid(last_frame));
                CHECK(wait_resets>resets && analog_command[CH_A_OFFSET]==141+n);
                CHECK(preview_calls==previews && preview_position==(unsigned)_1_posi);
                if(!kind)CHECK(Fft144.ready && ShowFFT && Scope144_RealCrc(fi,512*sizeof *fi)==fft_hash);
            }
            /* First reverse repeat paints immediately, before a replacement
             * frame exists. No raw epoch becomes valid for this preview. */
            KeyEvent(K_INDEX_DEC);CHECK(_1_posi==104 && preview_position==104 && preview_calls==previews);
            CHECK(shown_valid && Frame144_Valid(last_frame) && draws==paints+6);
            paints=draws;hw_ready=1;KeyEvent(0);CHECK(hw_waiting==2);previews=preview_calls;
            /* Also interrupt an already partially transferred FIFO. */
            KeyEvent(K_INDEX_INC);CHECK(_1_posi==105 && preview_position==105 && preview_calls==previews+1);
            CHECK(!shown_valid && !Frame144_Valid(last_frame) && draws==paints);
            for(n=0;n<7;++n)KeyEvent(0);
            CHECK(shown_valid && draws==paints+1 && frame_clears==clears);
            CHECK(last_frame->capture_ypos==105 && _State.Value==(kind==2?HOLD:RUN));
            hw_ready=0;if(kind==2)KeyEvent(KEY1);else KeyEvent(0);
            CHECK(hw_waiting==1);KeyEvent(KEY1);
            CHECK(_State.Value==HOLD && shown_valid && !hw_waiting && frame_clears==clears);
            hw_ready=1;KeyEvent(KEY1);for(n=0;n<7;++n)KeyEvent(0);
            CHECK(shown_valid && Frame144_Valid(last_frame));
            KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
        }
        cal_fixture_mode=0;hw_test=0;
    }
    /* Preserve Normal's distinct screen zero/trigger coordinates, and reuse
     * its exact repeat policy without a 120-ms delay for each DAC movement. */
    {
        unsigned src,n;uint32_t before;int expected;
        native_inherit_trigger=1;cal_fixture_mode=1;hw_test=1;hw_ready=0;history=0;
        for(src=1;src<=4;++src){
            _Mode=NORH;_State.Value=RUN;_A_Range=4;_1_posi=100;_Vt1=120;
            _1_source=(s16)src;_4_source=0;_Tr_source=TRACK1;_Tr_kind=1;_T_base=21;
            ADCoffset=40;TrgAuto=0;Current=TRACK1;Detail[TRACK1]=POSI;SetLowVolume=0;FlagFrameMode=1;
            Title[TRACK1][COUPLE].Value=SCOPE144_DC;
            CHECK(ServiceUntilFrame() && _1_posi==100 && _Vt1==120);
            expected=CalNative_Threshold(_Vt1,_1_posi,0,1024,ADCoffset,src==2 || src==4);
            CHECK(hw_raw==(unsigned)expected);
            for(n=0;n<5;++n){
                before=fake_cycles;KeyEvent(K_INDEX_INC);
                CHECK(_1_posi==101+(int)n && _Vt1==120 && hw_waiting);
                CHECK((uint32_t)(fake_cycles-before)<40u*72000u);
                expected=CalNative_Threshold(_Vt1,_1_posi,0,1024,ADCoffset,src==2 || src==4);
                CHECK(hw_raw==(unsigned)expected);
            }
            SetLowVolume=1;CHECK(CursorSpeedLogic(0)==2 && UiCommon_CursorSpeed(0,1)==1);KeyEvent(K_INDEX_DEC);
            CHECK(_1_posi==103 && _Vt1==120);
            SetLowVolume=0;KeyEvent(K_INDEX_DEC);CHECK(_1_posi==102 && _Vt1==120);
            KeyEvent(KEY1);CHECK(_State.Value==HOLD);before=fifo_index;
            KeyEvent(K_INDEX_DEC);CHECK(_1_posi==101 && _Vt1==120 && fifo_index==before);
            KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && _Vt1==120);
        }
        native_inherit_trigger=0;cal_fixture_mode=0;hw_test=0;SetLowVolume=0;
    }
    HoldP0();
    CHECK(image_crc_calls==2); /* No CRC recomputation across all captures/transitions. */
    printf("PASS native Wildcat service: %u checks; menu entry, DC/AC, range, generator, RUN/HOLD, navigation, exit; mock HW\n",checks);
    return 0;
}
