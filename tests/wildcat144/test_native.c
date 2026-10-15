/* Execute the actual native service against simulated registers and BIOS.
 * No DS203 is connected; analog and LCD electrical behavior are not tested. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Interrupt.h"
#include "Scope144.h"
#include "UnifiedCal.h"
#include "Calibrat.h"
#include "Function.h"
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
static unsigned id_read_pending,confirm_stage,cal_calls,history,fifo_index;
static unsigned checks,selector,draws,files,outputs,adc_mode,adc_a,adc_b,screen_clears,app_clears;
static unsigned key_status=0xffff;
static char footer[64];
static unsigned watch_settle,expected_mode,wrong_settle;
static uint32_t fake_cycles,capture_started,first_record_at;
static unsigned capture_case,record_count;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
u8 Current,Detail[14],FlagMeter,FlagFrameMode,FrameMode,OSBuffer,OSAvg,OsBufferLogicFlag;
u8 ShowFFT,Det,TrgAuto,CurDefTime,OsChartFlag,EnableCursorDisplay,DigChLockout,DownConvertMode,Sweep,CalibrateMode;
vu8 Key_Buffer;vu16 Delay_Cnt,PD_Cnt,CursorDisplayTimer;
u16 MAX_X;menu Title[13][4];meter Meter[9];trigg V_Trigg[4];s16 PPM_Comp;
static Y_attr attributes[8];Y_attr *Y_Attr=attributes;
uint32_t native_test_demcr,native_test_dwt;
uint32_t NativeTestCycles(void){if(watch_settle && adc_mode!=expected_mode)++wrong_settle;fake_cycles+=720;return fake_cycles;}
uint32_t Scope144_Crc(const void *bytes,uint32_t count)
{CHECK(bytes==(void *)0x0805c800u && count==135096);return SCOPE144_IMAGE_CRC;}
u32 __Get(u8 object)
{
    if(object==KEY_STATUS) {
        if(capture_case==4 && (uint32_t)(fake_cycles-capture_started)>=100u*72000u)return key_status&~KEY4_STATUS;
        if(capture_case==5 && record_count>=2)return key_status&~KEY4_STATUS;
        if(!(key_status&KEY2_STATUS) && Delay_Cnt){Delay_Cnt=Delay_Cnt>20?Delay_Cnt-20:0;if(!Delay_Cnt)key_status|=KEY2_STATUS;}
        if(confirm_stage){unsigned step=confirm_stage++;if(step==3)return key_status&~KEY1_STATUS;if(step>=4)confirm_stage=0;}
        return key_status;
    }
    if(object==0 && id_read_pending){id_read_pending=0;return selector==0?0x5731:(selector==1?0x0101:(selector==2?0x0102:0));}
    if(object==FIFO_DIGIT)return capture_case==6?0:0x28;
    return 0;
}
void __Set(u8 object,u32 value)
{
    if(object==37){selector=(unsigned)value;id_read_pending=1;}
    if(object==ADC_MODE)adc_mode=(unsigned)value;
    if(object==FIFO_CLR && value==W_PTR){fifo_index=0;if(!record_count)first_record_at=fake_cycles;++record_count;}
    if(object==CH_A_RANGE)adc_a=(unsigned)value;
    if(object==CH_B_RANGE)adc_b=(unsigned)value;
}
u32 __Read_FIFO(void)
{
    unsigned index=fifo_index++,a=55,b=55;
    if(history && index<=152)return 0xffff;
    if(capture_case==1)b=record_count<=4?75-record_count*5:55;
    if(capture_case==2){a=record_count%2?53:57;b=record_count%2?55:59;}
    if(capture_case==3 || (capture_case==7 && record_count==1) || (capture_case==11 && record_count==2))return 0xffff;
    if(capture_case==10)b=((record_count>1?record_count-2:0)/8)%2?59:55;
    if(capture_case==12 && record_count>1 && (record_count-1)%8==0)b=255;
    if(capture_case==9) {
        uint32_t ms=(fake_cycles-capture_started)/72000u;
        b=55+(ms<600?(600-ms)/10:0);
    }
    if(capture_case==8) {
        unsigned quarters=(record_count-1)*3;
        b=55+quarters/4+(index%4<quarters%4);
    }
    return a|(b<<8);
}
void __Display_Str(u16 x,u16 y,u16 color,u8 mode,char *s)
{(void)x;(void)color;(void)mode;if(y==0){strncpy(footer,s,63);footer[63]=0;}}
void __Clear_Screen(u16 color){(void)color;++screen_clears;}
void __Point_SCR(u16 x,u16 y){(void)x;(void)y;}
void __LCD_SetPixl(u16 color){(void)color;}
u8 ChartLogic(void){return 0;}
void App_init(u8 clear){app_clears+=clear;}
void Calibrat(u8 channel){CHECK(channel==TRACK1);CHECK(!Wildcat144_Active());++cal_calls;key_status|=KEY2_STATUS;}
void Update_Range(void){}
void Update_Base(void){}
void Update_Output(void){++outputs;}
void Update_Trig(u8 service){(void)service;}
static unsigned ui_calls;
void Menu144_View(const Scope144Measurements *m,int32_t dt,int32_t dv,unsigned valid,unsigned fps,unsigned refresh)
{(void)refresh;(void)m;(void)dt;(void)dv;(void)valid;(void)fps;++ui_calls;}

void Process144_Draw(const Scope144Cal *c,const int32_t *q,unsigned start,unsigned base,unsigned pos,unsigned inv)
{(void)c;(void)q;(void)start;(void)base;(void)pos;(void)inv;++draws;}
int File144_CalLoad(Scope144Bank *bank)
{
    unsigned i;memset(bank,0,sizeof(*bank));
    for(i=0;i<8;++i){Scope144Cal c={1024,0,55*1024,125*1024,5000000,1};bank->range[i]=c;}
    return 1;
}
int File144_CalSave(Scope144Bank *bank){(void)bank;++files;return 0;}
int File144_Csv(const Scope144Cal *c,const int32_t *q,const uint16_t *pairs,unsigned range,unsigned base,unsigned num,int factor)
{(void)c;(void)q;(void)pairs;(void)range;(void)base;(void)num;(void)factor;++files;confirm_stage=1;return 0;}
static int CalScenario(unsigned scenario,unsigned mode,CalMean *mean)
{
    int result;CalCaptureStats stats;
    capture_case=scenario;record_count=0;capture_started=fake_cycles;first_record_at=0;
    expected_mode=mode?INTERLACE:SEPARATE;watch_settle=1;wrong_settle=0;
    result=WildcatCalibration_Capture(0,mode,79,79,mean);
    watch_settle=0;CHECK(!wrong_settle);
    WildcatCalibration_Diagnostics(&stats);
    CHECK(stats.records<=record_count && stats.window<=3);
    CHECK(stats.elapsed_ms<=2021);
    if(result==1) {
        CHECK(stats.records>=25 && stats.blocks>=3 && stats.window==3);
        CHECK(stats.block_ms>=20 && stats.spread_a<=1024 && stats.spread_b<=1024);
    }
    CHECK((uint32_t)(fake_cycles-capture_started)<=2021u*72000u);
    if(scenario!=4)CHECK((uint32_t)(first_record_at-capture_started)>=300u*72000u);
    capture_case=0;return result;
}
static void KeyEvent(unsigned key){Key_Buffer=(u8)key;CHECK(Wildcat144_Service());}
static void SelectGroup(unsigned group){unsigned count=0;while(Current!=group){CHECK(++count<22);KeyEvent(K_ITEM_INC);}}
int main(void)
{
    unsigned i,a;static char gen_names[230]={0};
    CHECK(VirtualAlloc((void *)0x40000000,0x20000,0x3000,4)==(void *)0x40000000);
    for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
    Title[OUTPUT][KIND].Str=gen_names;Title[OUTPUT][FRQN].Str=gen_names;
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);
    Title[0][COUPLE].Value=DC;Detail[0]=COUPLE;
    CHECK(!Wildcat144_Service());
    Title[0][COUPLE].Value=2;Key_Buffer=K_INDEX_INC;
    CHECK(Wildcat144_CouplingKey());CHECK(Title[0][COUPLE].Value==SCOPE144_DC);
    FlagMeter=1;Meter[1].Item=FRQ;Meter[1].Track=TRACK2;
    Title[0][RANGE].Value=4;Title[0][POSI].Value=100;Title[RUNNING][STATE].Value=HOLD;
    Title[OUTPUT][KIND].Value=6;Title[T_BASE][BASE].Value=21;
    CHECK(Wildcat144_Service());CHECK(Wildcat144_Active() && ui_calls==1 && _State.Value==HOLD && MAX_X==311 && FlagMeter==1);
    CHECK(Meter[1].Item==FRQ && Meter[1].Track==TRACK2 && !app_clears && !screen_clears);
    CHECK(adc_b==8 && adc_a==4);CHECK(Current==0 && Detail[0]==COUPLE);
    KeyEvent(K_INDEX_INC);CHECK(Title[0][COUPLE].Value==SCOPE144_AC);CHECK(_State.Value==HOLD);
    { CalMean mean;
      history=1;
      expected_mode=SEPARATE;watch_settle=1;wrong_settle=0;
      CHECK(WildcatCalibration_Capture(4,0,94,94,&mean)==1);
      watch_settle=0;CHECK(!wrong_settle);
      CHECK(adc_a==4 && adc_b==4 && adc_mode==SEPARATE);
      CHECK(mean.a==55*1024 && mean.b==55*1024 && mean.clips==0);
      expected_mode=INTERLACE;watch_settle=1;wrong_settle=0;
      CHECK(WildcatCalibration_Capture(4,1,40,40,&mean)==1);
      watch_settle=0;CHECK(!wrong_settle);
      CHECK(adc_b==8 && adc_mode==INTERLACE && mean.clips==0);
      history=0;
    }
    { CalMean mean;
      CHECK(CalScenario(7,0,&mean)==1); /* Completely clipped first full record is ignored. */
      CHECK(record_count==25 && mean.a==55*1024 && mean.b==55*1024);
      CHECK(CalScenario(1,0,&mean)==1); /* B slowly settles from the previous path. */
      CHECK(record_count>=6 && mean.a==55*1024 && mean.b==55*1024);
      CHECK(CalScenario(1,1,&mean)==1);CHECK(record_count>=6);
      CHECK(CalScenario(9,0,&mean)==1);
      CHECK((uint32_t)(fake_cycles-capture_started)>=600u*72000u && mean.b<=56*1024);
      CHECK(CalScenario(2,0,&mean)==1); /* Alternating short-burst interference is averaged. */
      CHECK(record_count==25 && mean.a==55*1024 && mean.b==57*1024);
      CHECK(CalScenario(2,1,&mean)==1 && mean.b==57*1024);
      CHECK(CalScenario(10,0,&mean)==CAL_CAPTURE_UNSTABLE); /* Entire block means keep alternating. */
      { CalCaptureStats stats;
        WildcatCalibration_Diagnostics(&stats);
        CHECK(stats.blocks>3 && stats.window==3 && stats.spread_b==4*1024 && stats.spread_a==0);
      }
      CHECK(CalScenario(11,0,&mean)==1 && record_count==33 && !mean.clips);
      CHECK(CalScenario(12,0,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==512);
      CHECK(CalScenario(8,0,&mean)==CAL_CAPTURE_UNSTABLE); /* 0.75-step drift per frame. */
      CHECK(CalScenario(3,0,&mean)==CAL_CAPTURE_CLIPPED && mean.clips==4096);
      CHECK(CalScenario(4,0,&mean)==-1 && record_count==0);
      CHECK(CalScenario(5,0,&mean)==-1 && record_count==2);
      CHECK(CalScenario(6,0,&mean)==0);
      CHECK(CalScenario(0,0,&mean)==1 && record_count==25); /* Cancellation/timeout leaves no stale stability state. */
      fake_cycles=0xffff0000u;
      CHECK(CalScenario(0,0,&mean)==1); /* Deadline and block span survive DWT wrap. */
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
    Detail[OUTPUT]=KIND;KeyEvent(K_INDEX_INC);CHECK(Title[OUTPUT][KIND].Value==0 && outputs==1);
    KeyEvent(K_ITEM_INC);CHECK(Current==T_BASE);
    Detail[T_BASE]=BASE;KeyEvent(K_INDEX_INC);CHECK(Title[T_BASE][BASE].Value==20);
    KeyEvent(KEY1);CHECK(Title[RUNNING][STATE].Value==RUN);CHECK(draws==1);CHECK(adc_mode==INTERLACE);
    KeyEvent(KEY1);CHECK(Title[RUNNING][STATE].Value==HOLD);
    /* Every tick must return: native main loop remains the owner. */
    for(i=0;i<30;++i){KeyEvent(K_ITEM_INC);CHECK(Current!=RUNNING && Current<=21);if(Current>=13)KeyEvent(K_ITEM_S);}
    CHECK(!files);
    /* Disabled controls remain focusable, but no key may mutate them. */
    { unsigned g,d,k;const unsigned keys[]={KEY2,KEY3,K_INDEX_INC,K_INDEX_DEC};
      for(g=TRACK2;g<=TRACK4;++g)for(d=0;d<4;++d)for(k=0;k<4;++k) {
        s16 before[13][4];unsigned a,b,old_outputs=outputs,old_files=files;
        Current=(u8)g;Detail[g]=(u8)d;
        for(a=0;a<13;++a)for(b=0;b<4;++b)before[a][b]=Title[a][b].Value;
        KeyEvent(keys[k]);
        for(a=0;a<13;++a)for(b=0;b<4;++b)CHECK(Title[a][b].Value==before[a][b]);
        CHECK(outputs==old_outputs && files==old_files);
      }
      Current=T_BASE;Detail[T_BASE]=2;KeyEvent(K_INDEX_INC);CHECK(_X_posi.Value==0);
      Current=TRIGG;Detail[TRIGG]=3;KeyEvent(K_INDEX_INC);CHECK(Title[TRIGG][3].Value==0);
      Current=FILE;Detail[FILE]=3;KeyEvent(KEY3);CHECK(!files);
      Current=V_VERNIE;Detail[V_VERNIE]=0;a=Title[V_VERNIE][0].Value;KeyEvent(K_INDEX_DEC);CHECK(Title[V_VERNIE][0].Value==(s16)a-1);
      Current=T_VERNIE;Detail[T_VERNIE]=0;for(a=0;a<350;++a)KeyEvent(K_INDEX_INC);CHECK(_T1==300);
      Current=METER_0;Meter[0].Item=VPP;KeyEvent(K_INDEX_INC);CHECK(Meter[0].Item==CIR && Meter[0].Track==TRACK1);
    }
    CHECK(!app_clears && !screen_clears && Meter[1].Item==FRQ && Meter[1].Track==TRACK2);
    Current=FILE;Detail[FILE]=1;KeyEvent(K_INDEX_INC);KeyEvent(KEY3);CHECK(files==1);
    Current=0;Detail[0]=COUPLE;
    KeyEvent(K_INDEX_INC);CHECK(Title[0][COUPLE].Value==3);CHECK(adc_mode==SEPARATE);
    CHECK(!Wildcat144_Service());CHECK(files==1);
    CHECK(Meter[1].Item==FRQ && Meter[1].Track==TRACK2 && FlagMeter==1);
    /* Re-enter without forcing a different panel style or page. */
    FlagMeter=2;Title[0][COUPLE].Value=SCOPE144_DC;CHECK(Wildcat144_Service());CHECK(FlagMeter==2);
    Current=TRACK1;Detail[0]=COUPLE;KeyEvent(K_INDEX_DEC);CHECK(!Wildcat144_Active() && FlagMeter==2);
    FlagMeter=0;MAX_X=397;Title[0][COUPLE].Value=SCOPE144_DC;CHECK(Wildcat144_Service());CHECK(!FlagMeter && MAX_X==397);
    Current=TRACK1;Detail[0]=COUPLE;KeyEvent(K_INDEX_DEC);CHECK(!Wildcat144_Active() && !FlagMeter);
    CHECK(Title[0][RANGE].Value==5 && Title[T_BASE][BASE].Value==20 && Title[OUTPUT][KIND].Value==0);
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0);
    Title[0][COUPLE].Value=6;Key_Buffer=K_INDEX_INC;
    CHECK(Wildcat144_CouplingKey());CHECK(Title[0][COUPLE].Value==DC);
    printf("PASS native Wildcat service: %u checks; menu entry, DC/AC, range, generator, RUN/HOLD, navigation, exit; mock HW\n",checks);
    return 0;
}
