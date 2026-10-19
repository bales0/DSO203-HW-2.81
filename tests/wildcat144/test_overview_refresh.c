/* Actual main-loop gate over the real NativeWindow model; no hardware. */
#include <stdio.h>
#include <stdlib.h>
#include "BIOS.h"
#include "Function.h"
#include "Menu.h"
#include "Interrupt.h"
#include "NativeWindow.h"
static unsigned checks,draws,full_draws,width=300,box;
u8 Current,Detail[14];menu Title[13][4];vu8 Key_Buffer;vu16 NotificationTimer;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int Process_NativeOverviewChanged(void){return NativeWindow_Changed(width);}
void Update_Buffer_Overview(void){unsigned first,last;++draws;box=(unsigned)NativeWindow_Overview(width,&first,&last);}
void Update_View_Area(void){++full_draws;}
void Test_NormalOverviewTick(void);
void Test_NormalOverviewFinish(void);
int main(void){
 NativeWindowInput input={0};unsigned i,before;
 input.capacity=4096;input.points=392;input.step_q10=1024;input.origin=150;input.discard=2;input.rdiscard=2;
 NativeWindow_Reset();Current=TRACK1;Test_NormalOverviewTick();draws=0;
 for(i=0;i<4096;++i)NativeWindow_Written(i,0,1);
 Current=FILE;NativeWindow_Publish(&input);Test_NormalOverviewTick();CHECK(!draws);
 Current=TRACK1;Test_NormalOverviewTick();CHECK(draws==1 && box);
 before=draws;NotificationTimer=40;width=386;Test_NormalOverviewTick();CHECK(draws==before);
 NotificationTimer=0;Test_NormalOverviewTick();CHECK(draws==before+1 && box);
 before=draws;for(i=0;i<100;++i)Test_NormalOverviewTick();CHECK(draws==before);
 NativeWindow_Hide();Test_NormalOverviewTick();CHECK(draws==before+1 && !box);
 NativeWindow_Publish(&input);Test_NormalOverviewTick();CHECK(box);
 Current=T_BASE;Detail[T_BASE]=XPOSI;Key_Buffer=K_INDEX_INC;before=draws;
 Test_NormalOverviewFinish();CHECK(draws==before+1 && !full_draws);
 Key_Buffer=K_INDEX_DEC;Test_NormalOverviewFinish();CHECK(draws==before+2 && !full_draws);
 Current=TRACK1;Test_NormalOverviewFinish();CHECK(full_draws==1);
 Current=T_BASE;NotificationTimer=43;Test_NormalOverviewFinish();CHECK(draws==before+2 && full_draws==1);
 printf("PASS overview refresh: %u checks; real native record and main paint gate, FILE/notification defer, width change, idle/HOLD, invalid window hidden\n",checks);return 0;
}
