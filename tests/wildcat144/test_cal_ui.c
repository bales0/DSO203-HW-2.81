/* Actual Calibrat() entry, renderer, button gate, commit and cleanup against
 * fake BIOS/registers. No real ADC, disk or DS203 is accessed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "File.h"
#include "Process.h"
#include "Menu.h"
#include "Interrupt.h"
#include "Function.h"
#include "Calibrat.h"
#include "UnifiedCal.h"
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,polls,nscript,pos,captures,writes,failed_save,fail_capture,reference_lines,clock_enables;
static struct {unsigned key,pressed;} script[1024];
static int32_t applied_reference;
static int interlace,positive;
static unsigned table_pixels,headings,dc_errors;
static int fail_dc,unstable_offset;
static unsigned offset_errors,clock_panels,highlight_ppm;
static int capture_status,flat_b;
static unsigned diagnostic_lines,progress_lines,save_lines;
static Scope144Bank stored;
static Y_attr attrs[8];Y_attr *Y_Attr=attrs;
static const uint32_t scales[8]={2000,4000,8000,20000,40000,80000,200000,400000};
u32 DataBuf[4096];u8 ADCoffset=54,GenAdjustMode,Sweep,Versions,Current;
s8 Ka1[10],Kb1[10],Ka3[10],Kb3[10],LKa1[10],LKb1[10],LKa3[10],LKb3[10],HKa1[10],HKb1[10],HKa3[10],HKb3[10];
u16 Ka2[10],Kb2[10],LKa2[10],LKb2[10],HKa2[10],HKb2[10],HiBatLevel[2],LoBatLevel[2],VDiff[2];
menu Title[13][4];vu8 Key_Buffer,Blink,Twink;vu16 PD_Cnt;
uint32_t native_test_demcr,native_test_dwt;
uint32_t NativeTestCycles(void){static uint32_t cycles;return cycles+=72000;}
uint32_t Scope144_Crc(const void *data,uint32_t count)
{CHECK(data==(void *)0x0805c800u && count==135096);return SCOPE144_IMAGE_CRC;}
u32 __Get(u8 object)
{
    if(object==V_BATTERY)return 3700;
    if(object==KEY_STATUS) {
        ++polls;CHECK(polls<10000);Twink^=1;Blink=BLINK;
        if(pos<nscript){Key_Buffer=(u8)script[pos].key;return 0xffffu^script[pos++].pressed;}
        return 0xffff;
    }
    return 0;
}
void __Set(u8 object,u32 value){(void)object;(void)value;}
void __Clear_Screen(u16 color){(void)color;}
void __Point_SCR(u16 x,u16 y){CHECK(x<400 && y<240);}
void __LCD_SetPixl(u16 color){(void)color;++table_pixels;}
void __Display_Str(u16 x,u16 y,u16 color,u8 mode,char *s)
{
    CHECK(x+1+8*strlen(s)<400 && y+14<=240);(void)color;(void)mode;
    if(capture_status==CAL_CAPTURE_UNSTABLE) {
        if(strstr(s,"Records/blocks/window: 25/3/3"))diagnostic_lines|=1;
        if(strstr(s,"Time/block (ms): 2000/21"))diagnostic_lines|=2;
        if(strstr(s,"Span A/B (mADC): 2000/4000"))diagnostic_lines|=4;
    }
    if(strstr(s,"CLOCK CALIBRATION"))++clock_panels;
    if(y==120 && mode==INV)++highlight_ppm;
    if(strstr(s,"CH_A") || strstr(s,"CH_B"))++headings;
    if(strstr(s,"Offset A R") || strstr(s,"Offset B R"))++offset_errors;
    if(strstr(s,"A DC: dADC=") || strstr(s,"B DC: dADC="))++dc_errors;
    if(strstr(s,"ground A+B")){positive=0;applied_reference=0;}
    if(strstr(s,"Apply SAME"))positive=1;
    if(strstr(s,"Reference: ")){char *unit;double v=strtod(strchr(s,':')+1,&unit);applied_reference=(int32_t)(v*(strstr(unit,"mV")?1000:1000000)+0.5);++reference_lines;}
    if(strstr(s,"please wait; K4 cancel"))++progress_lines;
    if(strstr(s,"Saving calibration WPT/WPB"))++save_lines;
}
void App_init(u8 clear){(void)clear;}
void Update_Range(void){}
void Update_Base(void){}
void Update_Trig(u8 service){(void)service;}
void Update_Output(void){if(Title[OUTPUT][KIND].Value==PWM && Title[OUTPUT][FRQN].Value==18)++clock_enables;}
int File144_CalLoad(Scope144Bank *bank){*bank=stored;return 1;}
int File144_WptSave(Scope144Bank *bank,int native_updated)
{
    unsigned i;CHECK(native_updated==1);++writes;
    for(i=0;i<8;++i){CHECK(Ka1[i]==-5 && Kb1[i]==-7);CHECK(Ka2[i]==1024 && Kb2[i]>=952 && Kb2[i]<=954);}
    if(failed_save){--failed_save;return WR_ERR;}
    stored=*bank;return OK;
}
int WildcatCalibration_Capture(unsigned r,unsigned path,unsigned a,unsigned b,CalMean *m)
{
    int64_t signal=positive?(int64_t)applied_reference*1024/scales[r]:0;
    CHECK(r<8 && a<=200 && b<=200);CHECK(TIM2->DIER==0 && TIM4->DIER==0);++captures;
    if(captures==fail_capture)return 0;
    if(capture_status){m->a=99*1024;m->b=101*1024;m->clips=0;return capture_status;}
    if(path)CHECK(interlace);
    m->a=((int32_t)a+(path?100:5))*1024+(int32_t)signal;
    m->b=((int32_t)b+(path?103:7))*1024+(int32_t)(signal*(path?1080:1100)/1024);
    if(unstable_offset && !path && !positive && captures>2) {
        int shift=((captures-3)/2)%2?-1536:1536;m->a+=shift;m->b+=shift;
    }
    if(flat_b && !path)m->b=(int32_t)b*820+7*1024+(int32_t)signal;
    if(fail_dc && positive)m->b=((int32_t)b+7)*1024;
    m->clips=0;return 1;
}
void WildcatCalibration_Diagnostics(CalCaptureStats *stats)
{
    memset(stats,0,sizeof(*stats));stats->records=25;stats->blocks=3;stats->window=3;
    stats->elapsed_ms=450;stats->block_ms=21;
    if(capture_status==CAL_CAPTURE_UNSTABLE){stats->elapsed_ms=2000;stats->spread_a=2048;stats->spread_b=4096;}
}
static void Push(unsigned key,unsigned pressed){CHECK(nscript<1024);script[nscript].key=key;script[nscript++].pressed=pressed;}
static void Press(unsigned key){Push(key,0);Push(0,0);}
static void Init(int with144)
{
    unsigned i;polls=nscript=pos=captures=writes=failed_save=fail_capture=reference_lines=clock_enables=0;
    table_pixels=headings=dc_errors=offset_errors=clock_panels=highlight_ppm=diagnostic_lines=progress_lines=save_lines=0;fail_dc=unstable_offset=capture_status=flat_b=0;
    memset(&stored,0,sizeof(stored));positive=0;applied_reference=0;interlace=with144;
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,with144?0x0102:0);
    memset(DataBuf,0,sizeof(DataBuf));memset(Title,0,sizeof(Title));
    for(i=0;i<8;++i){attrs[i].SCALE=scales[i];strcpy(attrs[i].STR,"Range");}
    memset(Ka1,4,10);memset(Kb1,6,10);memset(Ka3,8,10);memset(Kb3,9,10);
    memset(LKa1,11,10);memset(LKb1,12,10);memset(HKa1,21,10);memset(HKb1,22,10);
    for(i=0;i<10;++i)Ka2[i]=Kb2[i]=1177;
    LoBatLevel[0]=LoBatLevel[1]=3200;HiBatLevel[0]=HiBatLevel[1]=4300;VDiff[0]=VDiff[1]=1100;
    PPM_Comp=17;CalibrateMode=0;GenAdjustMode=1;Sweep=2;
    Title[OUTPUT][KIND].Value=3;Title[OUTPUT][FRQN].Value=12;
    TIM2->DIER=3;TIM4->DIER=5;native_test_demcr=7;native_test_dwt=8;
    Push(0,0); /* Initial release of long K2. */
}
static void Full(unsigned savekey)
{
    unsigned i;Press(KEY2);Press(KEY2);for(i=0;i<8;++i)Press(KEY2);Press(savekey);
}
static void CleanupChecks(void)
{
    CHECK(TIM2->DIER==3 && TIM4->DIER==5);CHECK(native_test_demcr==7 && native_test_dwt==8);
    CHECK(Title[OUTPUT][KIND].Value==3 && Title[OUTPUT][FRQN].Value==12);
    CHECK(Sweep==2 && GenAdjustMode==1 && !CalibrateMode);CHECK(!Key_Buffer);
}
int main(void)
{
    unsigned i;
    CHECK(VirtualAlloc((void *)0x40000000,0x20000,0x3000,4)==(void *)0x40000000);
    Init(1);Full(KEY2);Calibrat(TRACK1);
    CHECK(captures==72 && writes==1 && reference_lines==8);CHECK(progress_lines==captures && save_lines==1);CHECK(table_pixels>4000 && headings==2);CHECK(clock_enables==1 && clock_panels==1 && highlight_ppm==1);
    CHECK(LoBatLevel[0]==3700 && LoBatLevel[1]==3700 && HiBatLevel[0]==4300);
    for(i=0;i<8;++i){CHECK(LKa1[i]==-5 && LKb1[i]==-7);CHECK(HKa1[i]==21 && HKb1[i]==22);CHECK(Scope144_Valid(&stored.range[i]));}
    CleanupChecks();
    Init(1);Full(KEY3);Calibrat(TRACK2);
    CHECK(captures==72 && writes==1);CHECK(HiBatLevel[0]==3700 && LoBatLevel[0]==3200);
    CHECK(HKa1[0]==-5 && LKa1[0]==11);CleanupChecks();
    Init(0);Full(KEY2);Calibrat(TRACK1);CHECK(captures==56 && writes==1);CleanupChecks();
    /* A held confirmation and its queued release must not confirm next stage. */
    Init(1);Push(KEY2,KEY2_STATUS);Push(KEY2,KEY2_STATUS);Push(KEY2,KEY2_STATUS);
    Push(KEY2,0);Push(KEY4,KEY4_STATUS);Push(0,0);Calibrat(TRACK1);
    CHECK(captures==0 && writes==0);CHECK(Ka1[0]==4 && Ka2[0]==1177);CleanupChecks();
    Init(1);Press(K_INDEX_INC);Press(KEY4);Calibrat(TRACK1);
    CHECK(PPM_Comp==17 && writes==0);CleanupChecks();
    /* Cancel after all measurements: no writes and no RAM coefficients applied. */
    Init(1);Full(KEY4);Calibrat(TRACK2);
    CHECK(captures==72 && writes==0 && Ka1[0]==4 && Ka2[0]==1177);CleanupChecks();
    Init(1);Full(KEY1);Press(KEY4);Calibrat(TRACK1);
    CHECK(captures==72 && writes==0 && Ka1[0]==4);CleanupChecks();
    Init(1);Full(KEY2);failed_save=1;Press(KEY4);Calibrat(TRACK1);
    CHECK(writes==1 && Ka1[0]==4 && Ka2[0]==1177 && LoBatLevel[0]==3200 && PPM_Comp==17);CleanupChecks();
    Init(1);Full(KEY2);failed_save=1;Press(KEY2);Press(KEY3);Calibrat(TRACK1);
    CHECK(writes==2 && HiBatLevel[0]==3700 && LoBatLevel[0]==3200);CleanupChecks();
    Init(1);fail_capture=5;Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(captures==5 && writes==0 && Ka1[0]==4);CleanupChecks();
    /* Live table refresh on idle, failed B signal remains readable, no write. */
    Init(1);Press(KEY2);Press(KEY2);
    for(i=0;i<300;++i)Push(0,0);
    Press(KEY2);Press(KEY4);fail_dc=1;Calibrat(TRACK1);
    CHECK(captures>=58 && !writes && dc_errors==1 && reference_lines>1);CleanupChecks();
    /* Original right toggle selects a range; it never confirms or saves. */
    Init(1);Press(KEY2);Press(KEY2);Press(K_ITEM_INC);Press(K_ITEM_DEC);Press(KEY4);Calibrat(TRACK1);
    CHECK(captures==56 && writes==0 && reference_lines==3);CleanupChecks();
    Init(1);unstable_offset=1;Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(offset_errors==1 && captures==2+2*CAL_OFFSET_ATTEMPTS && writes==0);
    CHECK(Ka1[0]==4 && Ka2[0]==1177);CleanupChecks();
    /* All previous native coefficients can be unusable; a fresh calibration
     * still uses neutral working values and raw ADC commands. */
    Init(1);memset(Ka1,0x80,10);memset(Kb1,0x7f,10);memset(Ka3,0x7f,10);memset(Kb3,0x80,10);
    memset(Ka2,0,20);memset(Kb2,0xff,20);PPM_Comp=32767;Full(KEY2);Calibrat(TRACK1);
    CHECK(writes==1 && Ka1[0]==-5 && Kb1[0]==-7 && Ka3[0]==0 && Kb3[0]==0 && PPM_Comp==0);CleanupChecks();
    Init(1);memset(Ka1,0x80,10);memset(Kb1,0x7f,10);memset(Ka3,0x7f,10);memset(Kb3,0x80,10);
    memset(Ka2,0,20);memset(Kb2,0xff,20);Full(KEY4);Calibrat(TRACK1);
    CHECK(!writes && Ka1[0]==-128 && Kb1[0]==127 && Ka3[0]==127 && Kb3[0]==-128 && !Ka2[0] && Kb2[0]==65535);CleanupChecks();
    Init(1);PPM_Comp=999;Press(K_INDEX_INC);Press(KEY4);Calibrat(TRACK1);
    CHECK(clock_panels==1 && highlight_ppm==1 && PPM_Comp==999 && !writes);CleanupChecks();
    Init(1);PPM_Comp=-999;Press(K_INDEX_DEC);Press(KEY4);Calibrat(TRACK1);
    CHECK(clock_panels==1 && highlight_ppm==1 && PPM_Comp==-999 && !writes);CleanupChecks();
    Init(1);capture_status=CAL_CAPTURE_UNSTABLE;Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(!writes && captures==1 && Ka1[0]==4 && diagnostic_lines==7);CleanupChecks();
    Init(1);flat_b=1;Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(!writes && captures==2 && Ka1[0]==4);CleanupChecks();
    printf("PASS calibration UI: %u checks; actual Calibrat, held keys, all references, battery/charger, rollback, cancel, blinking\n",checks);
    return 0;
}
