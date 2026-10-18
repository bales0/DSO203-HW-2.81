/* Actual native overview renderer, title guard and holdoff predicate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Draw.h"
#include "Interrupt.h"
static unsigned checks,t_prints,sample_labels,screen_x,screen_y,interlace;
static u16 pixels[400][16],colors[5]={0x0005,0x0105,0x0205,0x0305,0x0405};
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
u8 Current,Detail[14],FrameMode,FlagFrameMode,Options,OsBufferLogicFlag,OSBuffer,OSAvg,FPGAosFlag;
vu16 NotificationTimer;menu Title[13][4];
const u16 RULE_BASE[8]={0x020,0x040,0x080,0x040,0x020,0x010,0x008,0x010};
int Wildcat144_Active(void){return (int)interlace;}
int Process_NativeOverview(unsigned *first,unsigned *last){*first=91;*last=105;return 1;}
int Wildcat144_Overview(unsigned *first,unsigned *last){*first=120;*last=145;return 1;}
void __Point_SCR(u16 x,u16 y){CHECK(x<400 && y<16);screen_x=x;screen_y=y;}
void __LCD_SetPixl(u16 color){CHECK(screen_x<400 && screen_y<16);pixels[screen_x][screen_y++]=color;}
void DrawPixel(u16 x,u8 y,u16 color){CHECK(x<400 && y<16);pixels[x][y]=color;}
void Print_Str(u16 x,u16 y,u16 color,u8 mode,char *s)
{(void)color;(void)mode;CHECK(x<400 && y<16);CHECK(!strcmp(s,"T"));++t_prints;pixels[x][y]=0xdead;}
void DisplaySamples(void){++sample_labels;}
void Test_PaintOverview(void);
void Test_HoldoffTitle(void);
int main(void)
{
    unsigned frame,mode,base,option,os,expected,notes,file;
    {menu e={"T",colors,3400,FIX,91,0,0,UPDAT};memcpy(&Title[TRIGG][3],&e,sizeof e);}
    for(frame=0;frame<3;++frame)for(mode=0;mode<7;++mode)for(base=0;base<22;++base)
    for(option=0;option<4;++option)for(os=0;os<2;++os) {
        FrameMode=(u8)frame;Options=(u8)option;OsBufferLogicFlag=(u8)os;
        _Mode=(s16)mode;_T_base=(s16)base;
        expected=frame==2 && (option&1) && !os && base<10 && (mode==NORH || mode==NORHLD || mode==NORC);
        CHECK((unsigned)NativeHoldoffAvailable()==expected);
    }
    /* First orange native mode: the white buffer box survives menu UPDAT. */
    FrameMode=FlagFrameMode=OSBuffer=OsBufferLogicFlag=interlace=0;
    _Mode=AUTO;_T_base=19;Current=TRACK1;Options=1;NotificationTimer=0;
    memset(pixels,0,sizeof pixels);Test_PaintOverview();CHECK(!t_prints && pixels[91][0]==WHT);
    Title[TRIGG][3].Flag=UPDAT;Test_HoldoffTitle();CHECK(!t_prints && pixels[91][0]==WHT);
    CHECK(!(Title[TRIGG][3].Flag&UPDAT));
    /* Active T belongs only to supported holdoff; FILE/notifications keep
     * ownership, and a subsequent disabled update restores the overview. */
    FrameMode=2;FlagFrameMode=1;_Mode=NORH;_T_base=5;
    for(notes=0;notes<2;++notes)for(file=0;file<2;++file) {
        unsigned before=t_prints;NotificationTimer=(u16)notes;Current=file?FILE:TRACK1;
        Test_PaintOverview();Test_HoldoffTitle();CHECK(t_prints-before==(!notes && !file?2u:0u));
    }
    NotificationTimer=0;Current=TRACK1;FrameMode=FlagFrameMode=0;_Mode=AUTO;
    Title[TRIGG][3].Flag=UPDAT;Test_HoldoffTitle();CHECK(pixels[91][0]==WHT);
    interlace=1;_State.Value=HOLD;{unsigned before=sample_labels;Test_PaintOverview();CHECK(pixels[120][0]==WHT && sample_labels==before);}
    printf("PASS holdoff/overview: %u checks; actual predicate/native painter/title guard, orange mode buffer box, supported T, FILE/notification ownership, 144 overview\n",checks);
    return 0;
}
