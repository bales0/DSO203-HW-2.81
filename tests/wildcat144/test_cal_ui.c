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
#include "UiCommon.h"
static unsigned uart_owner,last_ui_status;
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
static unsigned dac_fault,dac_94_count,dac_lines,dac_live;
static unsigned diagnostic_lines,progress_lines,save_lines;
static unsigned preview_calls,preview_rendering,preview_bad_rows,preview_cells,clip_reference,clip_lines;
static unsigned preview_case,preview_dashes,preview_smoothed,preview_retry,clip_progress;
static Scope144Bank stored;
static Y_attr attrs[8];Y_attr *Y_Attr=attrs;
static const uint32_t scales[8]={2000,4000,8000,20000,40000,80000,200000,400000};
u32 DataBuf[4096];u8 ADCoffset=54,GenAdjustMode,Sweep,Versions,Current;
s8 Ka1[10],Kb1[10],Ka3[10],Kb3[10],LKa1[10],LKb1[10],LKa3[10],LKb3[10],HKa1[10],HKb1[10],HKa3[10],HKb3[10];
u16 Ka2[10],Kb2[10],LKa2[10],LKb2[10],HKa2[10],HKb2[10],HiBatLevel[2],LoBatLevel[2],VDiff[2];
menu Title[13][4];vu8 Key_Buffer,Blink,Twink;vu16 PD_Cnt;
uint32_t native_test_demcr,native_test_dwt;
uint32_t NativeTestCycles(void){static uint32_t cycles;return cycles+=(preview_case==2 || preview_case==4)?144000:72000;}
uint32_t Scope144_Crc(const void *data,uint32_t count)
{CHECK(data==(void *)0x0805c800u && count==135096);return SCOPE144_IMAGE_CRC;}
u32 __Get(u8 object)
{
    if(object==V_BATTERY)return 3700;
    if(object==KEY_STATUS) {
        preview_rendering=0;++polls;CHECK(polls<10000);Twink^=1;Blink=BLINK;
        if(pos<nscript){Key_Buffer=(u8)script[pos].key;
            if(preview_case==5 && captures==121 && Key_Buffer==KEY2)preview_retry=1;
            return 0xffffu^script[pos++].pressed;}
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
    if(preview_retry && x==134 && y==166)CHECK(!strncmp(s,"--",2));
    if(preview_rendering) {
        if(y>=208 || y==6)++preview_bad_rows;
        if(x==134 || x==334) {
            ++preview_cells;
            if(!strncmp(s,"--",2))++preview_dashes;
            if(x==134 && !strncmp(s,"154.0mV",7))++preview_smoothed;
        }
    }
    if(strstr(s,"mADC clip5/0"))clip_progress=1;
    if(strstr(s,"Too many raw 0/255 samples (>4 per ADC)"))clip_lines|=1;
    if(strstr(s,"Clip A/B (4096): 5/0"))clip_lines|=2;
    if(strstr(s,"Raw min/max A: 0/166"))clip_lines|=4;
    if(strstr(s,"Raw min/max B: 162/164"))clip_lines|=8;
    if(strstr(s,"Block A/B (mADC): 164804/163156"))clip_lines|=16;
    if(strstr(s,"Records/blocks/window: 276/34/0"))clip_lines|=32;
    if(strstr(s,"Span A/B (mADC): 0/0"))clip_lines|=64;
    if(capture_status==CAL_CAPTURE_UNSTABLE) {
        if(strstr(s,"Records/blocks/window: 25/3/3"))diagnostic_lines|=1;
        if(strstr(s,"Time/block (ms): 2000/21"))diagnostic_lines|=2;
        if(strstr(s,"Span A/B (mADC): 2000/4000"))diagnostic_lines|=4;
    }
    if(strstr(s,"144 ZERO R3/8 C60/60 CHECK") || strstr(s,"144 ZERO R3/8 C60/60 FIT"))dac_live|=1;
    if(strstr(s,"R3 C60/60 A/B:"))dac_live|=2;
    if(strstr(s,"144 ZERO R3/8 ADC B: DAC SLOPE"))dac_lines|=1;
    if(strstr(s,"Slope A/B (mADC/CMD): 1000/0"))dac_lines|=2;
    if(strstr(s,"Allowed slope: 500..2000 mADC/CMD"))dac_lines|=4;
    if(strstr(s,"144 ZERO R3/8 ADC B: POINT ERROR"))dac_lines|=8;
    if(strstr(s,"Raw/expected (mADC): 53000/54195"))dac_lines|=16;
    if(strstr(s,"Error/limit (mADC): 3000/1000") || strstr(s,"Error/limit (mADC): -1195/1000") || strstr(s,"Error/limit (mADC): 2468/1000"))dac_lines|=32;
    if(strstr(s,"Point CMD A/B: 60/60") || strstr(s,"Point CMD A/B: 40/40"))dac_lines|=64;
    if(strstr(s,"Zero C40 A/B: 50000/53000"))dac_lines|=128;
    if(strstr(s,"Zero C94 A/B: 104000/107000"))dac_lines|=256;
    if(strstr(s,"144 ZERO R3/8 ADC A: POINT ERROR"))dac_lines|=512;
    if(strstr(s,"Raw/expected (mADC): 50000/51195"))dac_lines|=1024;
    if(strstr(s,"144 ZERO R3/8 ADC B: RETURN ERROR"))dac_lines|=2048;
    if(strstr(s,"Raw/expected (mADC): 110000/107000"))dac_lines|=4096;
    if(strstr(s,"144 REF R3/8 ADC B: POINT ERROR"))dac_lines|=8192;
    if(strstr(s,"Raw/expected (mADC): 155101/152632"))dac_lines|=16384;
    if(strstr(s,"CLOCK CALIBRATION"))++clock_panels;
    if(y==120 && mode==INV)++highlight_ppm;
    if(strstr(s,"CH_A") || strstr(s,"CH_B"))++headings;
    if(strstr(s,"Offset A R") || strstr(s,"Offset B R"))++offset_errors;
    if(strstr(s,"A DC: dADC=") || strstr(s,"B DC: dADC="))++dc_errors;
    if(strstr(s,"ground A+B")){positive=0;applied_reference=0;}
    if(strstr(s,"Apply SAME")){positive=1;if(clip_reference)capture_status=CAL_CAPTURE_CLIPPED;}
    if(strstr(s,"Reference: ")){char *unit;double v=strtod(strchr(s,':')+1,&unit);applied_reference=(int32_t)(v*(strstr(unit,"mV")?1000:1000000)+0.5);++reference_lines;}
    if(strstr(s,"please wait; K4 cancel"))++progress_lines;
    if(strstr(s,"Saving calibration WPT/WPB"))++save_lines;
}
void App_init(u8 clear){(void)clear;}
void Update_Range(void){}
void Update_Base(void){}
void Update_Trig(u8 service){(void)service;}
void Update_Output(void){if(Title[OUTPUT][KIND].Value==PWM && Title[OUTPUT][FRQN].Value==18)++clock_enables;
    TIM2->DIER=Sweep?1:0;TIM4->DIER=0;}
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
    if(capture_status){m->a=99*1024;m->b=101*1024;m->clips=0;
        if(capture_status==CAL_CAPTURE_CLIPPED){m->a=168760;m->b=167072;m->clips=5;}
        return capture_status;}
    if(path)CHECK(interlace);
    m->a=((int32_t)a+(path?10:5))*1024+(int32_t)signal;
    m->b=((int32_t)b+(path?13:7))*1024+(int32_t)(signal*(path?1080:1100)/1024);
    if(path && r==2) {
        if(!positive && a==94)++dac_94_count;
        if(dac_fault==1 && !positive)m->b=100*1024;
        if(dac_fault==2 && !positive && a==60)m->b+=3*1024;
        if(dac_fault==3 && !positive && a==60)m->a+=3*1024;
        if(dac_fault==4 && !positive && a==94 && dac_94_count==3)m->b+=3*1024;
        if(dac_fault==5 && positive && a==60)m->b+=3*1024;
    }
    if(unstable_offset && !path && !positive && captures>2) {
        int shift=((captures-3)/2)%2?-1536:1536;m->a+=shift;m->b+=shift;
    }
    if(flat_b && !path)m->b=(int32_t)b*820+7*1024+(int32_t)signal;
    if(fail_dc && positive)m->b=((int32_t)b+7)*1024;
    m->clips=0;return 1;
}
int WildcatCalibration_Preview(CalPreview *p,unsigned r,unsigned a,unsigned b,CalMean *m)
{
    int64_t signal=positive?(int64_t)applied_reference*1024/scales[r]:0;
    CHECK(r<8 && a<=200 && b<=200);(void)p;++preview_calls;preview_rendering=1;
    m->a=(int32_t)(a+5)*1024+(int32_t)signal;
    m->b=(int32_t)(b+7)*1024+(fail_dc?0:(int32_t)(signal*1100/1024));m->clips=0;
    if(preview_case==1 && !(preview_calls%2))m->clips=1;
    if(preview_case==2 && preview_calls>1)m->clips=1;
    if(preview_case==3 && preview_calls>1){m->a+=8*1024;m->b+=8*1024;}
    if(preview_case>=4 && preview_calls>1)return 0;
    return 1;
}
void WildcatCalibration_Diagnostics(CalCaptureStats *stats)
{
    memset(stats,0,sizeof(*stats));stats->records=25;stats->blocks=3;stats->window=3;
    stats->elapsed_ms=450;stats->block_ms=21;
    if(capture_status==CAL_CAPTURE_UNSTABLE){stats->elapsed_ms=2000;stats->spread_a=2048;stats->spread_b=4096;}
    if(capture_status==CAL_CAPTURE_CLIPPED) {
        stats->records=276;stats->blocks=34;stats->window=0;stats->elapsed_ms=2001;stats->block_ms=43;
        stats->clips_a=5;stats->clips_b=0;stats->min_a=0;stats->max_a=166;stats->min_b=162;stats->max_b=164;
    }
}
static void Push(unsigned key,unsigned pressed){CHECK(nscript<1024);script[nscript].key=key;script[nscript++].pressed=pressed;}
static void Press(unsigned key){Push(key,0);Push(0,0);}
static void Init(int with144)
{
    unsigned i;polls=nscript=pos=captures=writes=failed_save=fail_capture=reference_lines=clock_enables=0;
    table_pixels=headings=dc_errors=offset_errors=clock_panels=highlight_ppm=diagnostic_lines=progress_lines=save_lines=0;fail_dc=unstable_offset=capture_status=flat_b=0;
    dac_fault=dac_94_count=dac_lines=dac_live=0;
    preview_calls=preview_rendering=preview_bad_rows=preview_cells=clip_reference=clip_lines=0;
    preview_case=preview_dashes=preview_smoothed=preview_retry=clip_progress=0;
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
    CHECK(TIM2->DIER==1 && TIM4->DIER==0);CHECK(native_test_demcr==7 && native_test_dwt==8);
    CHECK(Title[OUTPUT][KIND].Value==3 && Title[OUTPUT][FRQN].Value==12);
    CHECK(Sweep==2 && GenAdjustMode==1 && !CalibrateMode);CHECK(!Key_Buffer);
}
int main(void)
{
    unsigned i;
    CHECK(VirtualAlloc((void *)0x40000000,0x20000,0x3000,4)==(void *)0x40000000);
    Init(1);uart_owner=1;DataBuf[0]=0x12345678;Calibrat(TRACK1);
    CHECK(DataBuf[0]==0x12345678 && !captures && !writes && last_ui_status==UI_FILE_BUSY);
    CHECK(TIM2->DIER==3 && TIM4->DIER==5 && _Kind==3);uart_owner=0;
    Init(1);Full(KEY2);Calibrat(TRACK1);
    CHECK(captures==208 && writes==1 && reference_lines==8);CHECK(progress_lines==captures && save_lines==1);CHECK(table_pixels>4000 && headings==2);CHECK(clock_enables==1 && clock_panels==1 && highlight_ppm==1);
    CHECK(LoBatLevel[0]==3700 && LoBatLevel[1]==3700 && HiBatLevel[0]==4300);
    for(i=0;i<8;++i){CHECK(LKa1[i]==-5 && LKb1[i]==-7);CHECK(HKa1[i]==21 && HKb1[i]==22);CHECK(Scope144_Valid(&stored.range[i]));}
    CleanupChecks();
    Init(1);Full(KEY3);Calibrat(TRACK2);
    CHECK(captures==208 && writes==1);CHECK(HiBatLevel[0]==3700 && LoBatLevel[0]==3200);
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
    CHECK(captures==208 && writes==0 && Ka1[0]==4 && Ka2[0]==1177);CleanupChecks();
    Init(1);Full(KEY1);Press(KEY4);Calibrat(TRACK1);
    CHECK(captures==208 && writes==0 && Ka1[0]==4);CleanupChecks();
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
    CHECK(captures==121 && !writes && dc_errors==1 && reference_lines==1);
    CHECK(preview_calls>=1 && !preview_bad_rows && preview_cells==2);CleanupChecks();
    /* Brief invalid previews retain the good value; persistent invalid data
     * expires, and a real input change is smoothed only for the display. */
    for(i=1;i<=4;++i) {
        unsigned j;Init(1);preview_case=i;Press(KEY2);Press(KEY2);
        for(j=0;j<800;++j)Push(0,0);
        Press(KEY4);Calibrat(TRACK1);
        CHECK(preview_calls>2 && !preview_bad_rows && !writes && captures==120);
        if(i==1)CHECK(!preview_dashes && preview_cells==2);
        if(i==2 || i==4)CHECK(preview_dashes==2);
        if(i==3)CHECK(preview_smoothed && !preview_dashes);
        CleanupChecks();
    }
    /* Retry after a failed K2 must not resurrect the old valid preview. */
    Init(1);preview_case=5;fail_capture=121;Press(KEY2);Press(KEY2);
    for(i=0;i<400;++i)Push(0,0);
    Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(preview_retry && reference_lines==2 && captures==121 && !writes);CleanupChecks();
    /* Original right toggle selects a range; it never confirms or saves. */
    Init(1);Press(KEY2);Press(KEY2);Press(K_ITEM_INC);Press(K_ITEM_DEC);Press(KEY4);Calibrat(TRACK1);
    CHECK(captures==120 && writes==0 && reference_lines==3);CleanupChecks();
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
    /* Typing remains in the button loop: the preview cannot overwrite the
     * reference/prompts, nor accept or save a range. */
    Init(1);Press(KEY2);Press(KEY2);
    for(i=0;i<300;++i)Push(0,0);
    Press(K_INDEX_INC);Press(K_INDEX_INC);Press(K_INDEX_DEC);Press(KEY4);Calibrat(TRACK1);
    CHECK(preview_calls>=1 && !preview_bad_rows && !writes && captures==120);CleanupChecks();
    Init(1);clip_reference=1;Press(KEY2);Press(KEY2);Press(KEY2);Press(KEY4);Calibrat(TRACK1);
    CHECK(clip_lines==127 && clip_progress && !writes && captures==121);CleanupChecks();
    /* Report the actual failing phase at the user's third range, without
     * leaving the wizard. Test B slope, both ADCs, return and reference. */
    for(i=1;i<=5;++i) {
        unsigned expected;
        Init(1);dac_fault=i;Press(KEY2);Press(KEY2);
        if(i==5){Press(KEY2);Press(KEY2);Press(KEY2);}
        Press(KEY4);Calibrat(TRACK1);
        expected=i==1?7:i==2?(8|16|32|64|128|256):i==3?(32|64|128|256|512|1024):
            i==4?(32|128|256|2048|4096):(32|64|128|256|8192|16384);
        CHECK((dac_lines&expected)==expected);
        if(i!=1)CHECK(dac_live==3);
        CHECK(!writes && Ka1[0]==4 && Ka2[0]==1177);CleanupChecks();
    }
    printf("PASS calibration UI: %u checks; actual Calibrat, held keys, all references, battery/charger, rollback, cancel, blinking\n",checks);
    return 0;
}

int Generator_SecBusy(void){return (int)uart_owner;}
void UiCommon_Status(unsigned status){last_ui_status=status;}
