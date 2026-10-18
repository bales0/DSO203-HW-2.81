/* Real App_init/transition/plot clear with BIOS counters, no physical LCD. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Draw.h"
#include "Process.h"
#include "Interrupt.h"
#include "UiCommon.h"
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,clears,plot_draws,graphs,marks,sets,ms;
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
int main(void){
 unsigned i,j;
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
 printf("PASS transition helpers: %u checks; exact hardware order/delays, dirty cells, blank plot, legacy full reset retained\n",checks);
 return 0;
}
