/* Actual Process transfer branches and interpolator; mock FIFO/LCD only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Interrupt.h"
#include "NativeWindow.h"
static unsigned checks,reads,available=4096,empty_at=UINT32_MAX,sent,rail_at=UINT32_MAX;
static u32 rail_word;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
menu Title[13][4];u8 Current,Detail[14],FlagMeter,FlagFrameMode;
u8 FrameMode,OsBufferLogicFlag,OsChartFlag,CalibrateMode,FPGAsubVer=1,FPGAosFlag=1;
u8 InvertA,InvertB,CalFlag,LastA_Mid,LastB_Mid,FirstA_Mid,FirstB_Mid;
s8 Ka1[10],Kb1[10];u16 Ka2[10],Kb2[10];s16 A_Posi,B_Posi;
static Y_attr attrs[8];Y_attr *Y_Attr=attrs;
s32 CSVdata(u8,u8,u16,u8);s16 QParam(u8,u16,u8);
u8 OSBufferLogic(void){return OsBufferLogicFlag;}
u8 ChartMode,Options,OSAvg,ShowFFT,Det,DownConvertMode,Update,freerun,FastMode;
u8 native_buffer_pending,BufferRestore,SerialStatus,exitflag,discard,Rdiscard;
u8 RMSdata[2][700],OsFFTData[2][700];
u32 DataBuf[4096],ReverseBitMask,BitMask;u16 TempKp1=1024,JumpCnt,bag_max_buf=4096;
s16 TrigSourceEnable;
u16 get_bag_max_buf(void){return bag_max_buf;}
s16 Xtend;s32 Waste;u8 OsTransferCompleteFlag;
vu8 Tim2Factor;u16 MAX_X=397;
u8 OSBuffer,SubIndex=1,SpiChartFlag,ArrayIndex,Sweep,SweepIndex;
s16 SpiNumBits;const SubTbase TbaseSub[13]={{0}};
u8 ListLogic(void){return 0;}
u8 ChartLogic(void){return 0;}
u8 SpiLogic(void){return 0;}
int Wildcat144_Active(void){return 0;}
u32 __Get(u8 object){(void)object;return 0;}
u32 __Read_FIFO(void){unsigned i=reads++;CHECK(i<4096);return (i==rail_at?rail_word:0x00008281u)|(i>=available || i==empty_at?0x00100000u:0);}
void Send_Data(s16 a,s16 b,s16 c,s16 d,u16 n){(void)a;(void)b;(void)c;(void)d;CHECK(n<392);++sent;}
void Test_NativeProcess(int shift);
void Test_NativeBaseMetadata(void);
static void Capture(unsigned words,int shift)
{
    NativeWindow_Reset();JumpCnt=0;reads=sent=0;available=words;
    Test_NativeProcess(shift);CHECK(reads==4096 && sent==392);
}

static void SupportedLifecycle(void)
{
 static const unsigned bases[]={8,16,19,20,21},kp[]={1024,1024,614,492,246};
 static const unsigned modes[]={AUTO,NORH,SGL};
 unsigned base,mode,source,panel,j,first,last,a,b,old_reads;ViewWindowGeometry view;
 FrameMode=FlagFrameMode=OSBuffer=OsBufferLogicFlag=ShowFFT=Det=DownConvertMode=Options=CalibrateMode=0;
 native_buffer_pending=BufferRestore=0;_1_source=1;_2_source=0;_4_source=0;
 for(base=0;base<5;++base)for(mode=0;mode<3;++mode)for(source=0;source<4;++source)if(source!=1)
 for(panel=0;panel<2;++panel)for(j=0;j<3;++j){
  int shift=bases[base]>16?(int)j*2-2:0;
  _T_base=(s16)bases[base];TempKp1=(u16)kp[base];_Mode=(s16)modes[mode];
  _Tr_source=(s16)source;MAX_X=panel?311:397;FlagMeter=(u8)panel;_X_posi.Value=0;
  _State.Value=RUN;empty_at=UINT32_MAX;Capture(4096,shift);
  if(!Process_NativeOverview(&first,&last) || !NativeWindow_Geometry(MAX_X-MIN_X,&view)){
   fprintf(stderr,"Lifecycle base=%u mode=%u source=%u panel=%u shift=%d\n",bases[base],modes[mode],source,panel,shift);
   CHECK(0);
  }
  CHECK(first>=91 && last<=306 && first<=last);
  _State.Value=HOLD;old_reads=reads;Test_NativeProcess(shift);
  CHECK(reads==old_reads && Process_NativeOverview(&a,&b) && a==first && b==last);
  /* The status bit outside the renderer footprint cannot hide a good view. */
  _State.Value=RUN;empty_at=0;Capture(4096,shift);
  CHECK(Process_NativeOverview(&a,&b) && a==first && b==last);
  /* A genuine invalid endpoint inside the physical viewport must block it. */
  empty_at=(view.window_start_q16/65536u+view.window_end_q16/65536u)/2u;
  Capture(4096,shift);CHECK(!Process_NativeOverview(&a,&b));
  /* A new valid record recovers without changing keys, mode or width. */
  empty_at=UINT32_MAX;Capture(4096,shift);CHECK(Process_NativeOverview(&a,&b) && a==first && b==last);
 }
}
int main(void)
{
    unsigned i,first,last;ViewWindowGeometry v;
    _Mode=AUTO;_State.Value=RUN;_T_base=19;_1_source=1;_Tr_source=TRACK1;
    TempKp1=614;_X_posi.Value=0;Capture(4095,0);
    CHECK(NativeWindow_Count()==4095);CHECK(Process_NativeOverview(&first,&last));
    CHECK(NativeWindow_Geometry(386,&v));
    CHECK(v.capture_count==4096 && v.window_start_q16==60u*65536u);
    printf("Normal transfer: prefix=%u capacity=%u origin=60 shift=0 step=%u points=%u first=%u last=%u\n",NativeWindow_Count(),bag_max_buf,TempKp1,sent,first,last);
    /* All cells / one empty at the tail / a damaged viewport / damage beyond
     * it. No invalid cell or old suffix is counted as new valid history. */
    Capture(4096,0);CHECK(Process_NativeOverview(&first,&last));
    empty_at=1000;Capture(4096,0);CHECK(NativeWindow_Count()==1000 && Process_NativeOverview(&first,&last));
    CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==4096);
    empty_at=100;Capture(4096,0);CHECK(NativeWindow_Count()==100 && !Process_NativeOverview(&first,&last));
    empty_at=UINT32_MAX;Capture(200,0);CHECK(!Process_NativeOverview(&first,&last));
    /* A partial prefix can cover the narrow panel but not a wider viewport. */
    TempKp1=1024;_T_base=16;Capture(320,0);MAX_X=311;
    CHECK(Process_NativeOverview(&first,&last));MAX_X=397;CHECK(!Process_NativeOverview(&first,&last));
    /* Old metadata is revoked synchronously at the first actual write. */
    Capture(4096,0);CHECK(Process_NativeOverview(&first,&last));
    reads=0;TransferFIFO(0,0);CHECK(NativeWindow_Count()==1 && !Process_NativeOverview(&first,&last));
    /* HOLD republishes the same complete record, without touching FIFO. */
    Capture(4096,-2);_State.Value=HOLD;reads=0;Test_NativeProcess(-2);
    CHECK(!reads && Process_NativeOverview(&first,&last));
    CHECK(NativeWindow_Geometry(386,&v) && v.window_start_q16==2u*65536u);
    for(i=0;i<8;++i){_X_posi.Value=(s16)(i*300);Test_NativeProcess(-2);CHECK(!reads && Process_NativeOverview(&first,&last));}
    _X_posi.Value=3900;Test_NativeProcess(0);CHECK(!Process_NativeOverview(&first,&last));
    _X_posi.Value=0;Test_NativeProcess(0);CHECK(Process_NativeOverview(&first,&last));
    /* Existing policy restrictions remain in the real Process predicates. */
    _Mode=SCAN;CHECK(!Process_NativeOverview(&first,&last));_Mode=X_Y;CHECK(!Process_NativeOverview(&first,&last));_Mode=AUTO;
    ShowFFT=1;CHECK(!Process_NativeOverview(&first,&last));ShowFFT=0;
    OSBuffer=OsBufferLogicFlag=1;CHECK(!Process_NativeOverview(&first,&last));OSBuffer=OsBufferLogicFlag=0;
    FlagFrameMode=1;CHECK(!Process_NativeOverview(&first,&last));FlagFrameMode=0;
    native_buffer_pending=1;CHECK(!Process_NativeOverview(&first,&last));BufferRestore=1;CHECK(Process_NativeOverview(&first,&last));
    /* A real RUN timebase reconfiguration cannot reinterpret an old capture.
     * HOLD and successful BUF restoration retain their immutable raw data. */
    _State.Value=HOLD;Test_NativeBaseMetadata();CHECK(Process_NativeOverview(&first,&last));
    _State.Value=2;Test_NativeBaseMetadata();CHECK(Process_NativeOverview(&first,&last));
    i=reads;_State.Value=RUN;Test_NativeBaseMetadata();
    CHECK(!Process_NativeOverview(&first,&last) && !NativeWindow_Count() && reads==i);
    _State.Value=HOLD;Test_NativeProcess(0);CHECK(!Process_NativeOverview(&first,&last));
    _State.Value=RUN;Capture(4096,0);CHECK(Process_NativeOverview(&first,&last));
    SupportedLifecycle();
    /* Publish raw rail warnings through the actual transfer + Process tail,
     * not a direct test-only call to the private publication helper. */
    _Mode=AUTO;_T_base=16;_Tr_source=0;FrameMode=FlagFrameMode=0;
    OsBufferLogicFlag=OSBuffer=0;_1_source=_2_source=1;
    Title[0][COUPLE].Value=Title[1][COUPLE].Value=0;
    native_buffer_pending=BufferRestore=0;_X_posi.Value=0;TempKp1=1024;
    _State.Value=RUN;rail_at=100;rail_word=0x0000ff00u;empty_at=UINT32_MAX;
    Capture(4096,0);CHECK(Process_NativeClip(0)==1 && Process_NativeClip(1)==2);
    _State.Value=HOLD;reads=0;Test_NativeProcess(0);
    CHECK(!reads && Process_NativeClip(0)==1 && Process_NativeClip(1)==2);
    InvertA=InvertB=1;CHECK(Process_NativeClip(0)==2 && Process_NativeClip(1)==1);
    InvertA=InvertB=0;_State.Value=RUN;empty_at=0;rail_at=100;
    Capture(4096,0);CHECK(Process_NativeClip(0)==1 && Process_NativeClip(1)==2);
    CHECK(NativeWindow_Count()==0); /* Warning does not declare the frame valid. */
    rail_at=empty_at=100;Capture(4096,0);
    CHECK(!Process_NativeClip(0) && !Process_NativeClip(1)); /* Invalid rail alone. */
    empty_at=UINT32_MAX;
    InvertA=InvertB=0;_State.Value=RUN;rail_at=UINT32_MAX;Capture(4096,0);
    CHECK(!Process_NativeClip(0) && !Process_NativeClip(1));

    ADCoffset=54;_State.Value=RUN;Capture(4096,0);
    CHECK(Process_NativeOffset(0)==54 && Process_NativeOffset(1)==54);
    _State.Value=HOLD;CalFlag=1;_A_Range=_B_Range=0;_1_source=_2_source=1;
    Ka2[0]=Kb2[0]=1024;attrs[0].SCALE=2000;LastA_Mid=LastB_Mid=80;
    {
        s32 csv_a=CSVdata(0,0,10,129),csv_b=CSVdata(1,0,10,130);
        s16 qa=QParam(0,10,1),qb=QParam(1,10,1);
        ADCoffset=0;
        CHECK(CSVdata(0,0,10,129)==csv_a && CSVdata(1,0,10,130)==csv_b);
        CHECK(QParam(0,10,1)==qa && QParam(1,10,1)==qb);
    }
    reads=0;Test_NativeProcess(0);
    CHECK(Process_NativeOffset(0)==54 && Process_NativeOffset(1)==54 && !reads);
    _State.Value=2;CHECK(Process_NativeOffset(0)==54);
    _State.Value=RUN;CHECK(Process_NativeOffset(0)==0);Capture(4096,0);
    _State.Value=HOLD;ADCoffset=54;CHECK(Process_NativeOffset(0)==0 && Process_NativeOffset(1)==0);
    printf("PASS Native overview transfer: %u checks; real transfer/selection/interpolation, prefix/viewport, HOLD/XPOS, policy; no hardware\n",checks);
    return 0;
}
