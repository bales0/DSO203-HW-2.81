/* Real App_init/transition/plot clear with BIOS counters, no physical LCD. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Function.h"
#include "Menu.h"
#include "Draw.h"
#include "Process.h"
#include "Interrupt.h"
#include "UiCommon.h"
#include "Calibrat.h"
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,clears,plot_draws,graphs,marks,sets,ms,range_updates;
u8 OffsetSelect,FPGAsubVer=1,FPGAosFlag,Detail[14];
vu8 Key_Buffer;
void Update_Range(void){++range_updates;}
int Test_NormalServiceKey(unsigned);
static unsigned objects[8],values[8];
u8 FlagMeter,UpdateMeter=4,Current,InitXY,Update,SelectiveUpdate,ClearMeterAreaFlag;
u8 discard,Rdiscard,FrameEndFlag;
vu16 PD_Cnt;
u16 MAX_X=311;
u8 TrackBuff[397*4];menu Title[13][4];meter Meter[9];
void __Set(u8 object,u32 value){CHECK(sets<8);objects[sets]=object;values[sets++]=(unsigned)value;}
void Delayms(u16 n){ms+=n;}
void __Clear_Screen(u16 color){CHECK(color==BLACK);++clears;}
void Draw_Window(void){unsigned n;for(n=0;n<(unsigned)(MAX_X-MIN_X+1)*4;++n)CHECK(!TrackBuff[n]);++plot_draws;}
void Update_View_Area(void){++graphs;}
void Update_Mark(void){++marks;}
void Process144_ClearFrame(void);
void Test_AdcLoad(u16,unsigned);
int main(void){
 unsigned i,j;
 CHECK(ADCoffset==40);
 for(i=0;i<65536;++i) {
   ADCoffset=17;Test_AdcLoad((u16)i,1);
   CHECK(ADCoffset==((i>>8)==0xab?(i&255):40));
   ADCoffset=23;Test_AdcLoad((u16)i,0);CHECK(ADCoffset==23);
 }
 Test_AdcLoad(0xab36,1);CHECK(ADCoffset==54);
 Test_AdcLoad(0xab28,1);CHECK(ADCoffset==40);
 Current=OUTPUT;Detail[OUTPUT]=KIND;Title[OUTPUT][KIND].Value=5;
 OffsetSelect=1;ADCoffset=54;
 CHECK(Test_NormalServiceKey(K_INDEX_INC) && ADCoffset==54 && !range_updates);
 CHECK(Test_NormalServiceKey(K_INDEX_DEC) && ADCoffset==53 && range_updates==1);
 CHECK(Title[OUTPUT][KIND].Value==5);
 ADCoffset=0;CHECK(Test_NormalServiceKey(K_INDEX_DEC) && !ADCoffset && range_updates==1);
 CHECK(Test_NormalServiceKey(K_INDEX_INC) && ADCoffset==1 && range_updates==2);
 CHECK(Test_NormalServiceKey(K_INDEX_S) && OffsetSelect==2);
 PPM_Comp=999;CHECK(Test_NormalServiceKey(K_INDEX_INC) && PPM_Comp==999);
 CHECK(Test_NormalServiceKey(K_INDEX_DEC) && PPM_Comp==998 && range_updates==2);
 PPM_Comp=-999;CHECK(Test_NormalServiceKey(K_INDEX_DEC) && PPM_Comp==-999);
 CHECK(Test_NormalServiceKey(K_INDEX_S) && OffsetSelect==1);
 Current=FILE;CHECK(!Test_NormalServiceKey(K_INDEX_INC));
 OffsetSelect=0;ADCoffset=40;
 CHECK(VirtualAlloc((void *)0x40000000,0x20000,0x3000,4)==(void *)0x40000000);
 for(i=0;i<13;++i)for(j=0;j<4;++j)Title[i][j].Flag=0;
 Current=TRACK1;FlagMeter=1;
 App_ModeTransition();
 CHECK(!clears && sets==4 && ms==120 && PD_Cnt==600 && InitXY);
 CHECK(objects[0]==ADC_CTRL && values[0]==EN && objects[1]==ADC_MODE && values[1]==SEPARATE);
 CHECK(objects[2]==STANDBY && values[2]==DN && objects[3]==FIFO_CLR && values[3]==W_PTR);
 Update=2;SelectiveUpdate=1;ClearMeterAreaFlag=1;UiCommon_ModeTransition();
 CHECK(graphs==1 && marks==1 && !clears && !ClearMeterAreaFlag && !Update && !SelectiveUpdate);
 for(i=0;i<4;++i)CHECK(!Title[OUTPUT][i].Flag && !Title[BK_LIGHT][i].Flag && !Title[VOLUME][i].Flag);
 CHECK(Title[TRACK2][SOURCE].Flag&UPDAT);CHECK(Title[T_BASE][BASE].Flag&UPDAT);
 CHECK(!(Title[RUNNING][STATUS].Flag&UPDAT));
 memset(TrackBuff,123,sizeof TrackBuff);Process144_ClearFrame();CHECK(plot_draws==1 && !clears && TrackBuff[(MAX_X-MIN_X+1)*4]==123);
 sets=ms=0;App_init(1);CHECK(clears==1 && ms==120 && ClearMeterAreaFlag && (Title[OUTPUT][0].Flag&UPDAT));
 /* Six real runtime commands, first in original Normal write order. */
 {static const u8 obj[6]={CH_A_COUPLE,CH_A_RANGE,CH_A_OFFSET,CH_B_COUPLE,CH_B_RANGE,CH_B_OFFSET};
  u32 val[6]={DC,4,154,DC,5,155};unsigned pass,k;
  sets=0;UiCommon_AnalogReset();
  for(pass=0;pass<128;++pass)for(k=0;k<6;++k)UiCommon_AnalogSet(obj[k],val[k]);
  CHECK(sets==6);for(k=0;k<6;++k)CHECK(objects[k]==obj[k] && values[k]==val[k]);
  for(k=0;k<6;++k){sets=0;++val[k];UiCommon_AnalogSet(obj[k],val[k]);
   CHECK(sets==1 && objects[0]==obj[k] && values[0]==val[k]);
   UiCommon_AnalogSet(obj[k],val[k]);CHECK(sets==1);}
  sets=0;App_ModeTransition();CHECK(sets==4);sets=0;
  for(k=0;k<6;++k)UiCommon_AnalogSet(obj[k],val[k]);CHECK(sets==6);
  sets=0;App_init(0);CHECK(sets==4);sets=0;
  for(k=0;k<6;++k)UiCommon_AnalogSet(obj[k],val[k]);CHECK(sets==6);
  /* External calibration ownership invalidates all six slots. */
  sets=0;UiCommon_AnalogReset();__Set(CH_B_RANGE,8);sets=0;
  for(k=0;k<6;++k)UiCommon_AnalogSet(obj[k],val[k]);CHECK(sets==6);
  /* Packed TIM5 period/offset commands may never poison either offset. */
  sets=0;UiCommon_AnalogSet(CH_A_OFFSET,0x12340056u);
  CHECK(sets==1 && values[0]==0x12340056u);sets=0;
  for(k=0;k<6;++k)UiCommon_AnalogSet(obj[k],val[k]);CHECK(sets==6);
  sets=0;UiCommon_AnalogSet(BACKLIGHT,70);CHECK(sets==1 && objects[0]==BACKLIGHT);
 }
 printf("PASS transition helpers: %u checks; exact hardware order/delays, dirty cells, blank plot, legacy full reset retained\n",checks);
 return 0;
}
