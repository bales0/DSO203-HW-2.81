/* Render actual UI144 against a normal-layout Title/Meter fixture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Function.h"
#include "Interrupt.h"
#include "Scope144.h"
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks,draws,title_calls,meter_calls,formatted;
static u16 colors[5]={0x0005,0x0105,0x0205,0x0305,0x0405};
static char sources[5][10]={"OFF","CH(A)","-A","x10A","-x10A"};
static char couples[9][10]={"DC","AC","DT","TL","RS","i2","SP","144D","144A"};
static char ranges[9][10]={"50mV","0.1V","0.2V","0.5V","1V","2V","5V","10V","--"};
char x10[8][6]={"0.5V","1V","2V","5V","10V","20V","50V","100V"};
static char kinds[9][10]={"SIN","TRI","SAW","SQR","PWM","NOISE","OFF","ARB","UART"};
static char rates[23][10]={"1Hz"},mode_str[7][10]={"OFF","AUTO","NORM","NorCL","PERST","SGL","XY"};
static char edge[9][10]={":!\\!",":!^!"},states[3][10]={"RUN","HOLD","HOLD"};
static char dt_label[2][4]={"dT","1/d"};
static char labels[12][5]={"Vbat","fps","Vdc","RMS","Freq","Max","Min","Vpp","T","Duty","Th","Tl"};
#define E(s,x,y,l,v) {(char *)(s),colors,l,0,x,y,v,0}
menu Title[13][4]={
 {E(sources,35,228,4,1),E(couples,35,216,8,7),E(ranges,51,216,8,0),E("YPOS",0,0,200,100)},
 {E(sources,86,228,4,0),E(couples,86,216,4,0),E(ranges,102,216,8,1),E("YPOS",0,0,200,100)},
 {E(sources,137,228,1,0),E("",137,216,0,0),E("",153,216,0,0),E("YPOS",0,0,200,50)},
 {E(sources,188,228,15,0),E("",233,0,0,0),E("",217,0,0,0),E("YPOS",0,0,200,20)},
 {E(states,0,228,2,0),E(states,0,216,2,0),E("",0,0,0,0),E("",0,0,0,0)},
 {E(kinds,239,228,8,0),E(rates,239,216,22,0),E("DUTY",212,216,30000,15000),E("AMPL",196,216,100,50)},
 {E(mode_str,290,228,6,AUTO),E(ranges,290,216,21,0),E("XPOS",366,0,3703,0),E("VIEW",159,0,253,32)},
 {E(sources,333,228,4,0),E(edge,373,228,8,1),E("LEV",333,216,200,100),E("T",92,0,3400,0)},
 {E("V1",35,0,200,180),E("V2",62,0,200,40),E("dV",314,197,1,0),E("",342,197,240,0)},
 {E("SAVE",126,0,1,0),E("",206,0,999,0),E("CSV",230,0,7,3),E("DIR",166,0,15,0)},
 {E("T1",312,0,300,30),E("T2",339,0,300,180),E(dt_label,314,182,1,0),E("",342,182,400,0)},
 {E("LIGHT",314,167,0,0),E("",342,167,9,5),E("",0,0,0,0),E("",0,0,0,0)},
 {E("VOLUME",314,152,0,0),E("",342,152,9,5),E("",0,0,0,0),E("",0,0,0,0)}
};
meter Meter[9]={
 {labels[0],0,FRQ,314,342,137,0},{labels[0],0,CIR,314,342,122,0},{labels[0],0,VPP,314,342,107,0},
 {labels[0],0,VDC,314,342,92,0},{labels[0],0,RMS,314,342,77,0},{labels[0],0,MIN,314,342,62,0},
 {labels[0],0,MAX,314,342,47,0},{labels[0],0,DUT,314,342,32,0},{labels[0],0,TH,314,342,17,0}
};
trigg V_Trigg[4];
u8 Current,Detail[14],FlagMeter=1,UpdateMeter=4,BTwink,EnablePaS;vu8 Twink,Cnt_20mS;vu16 Sec_Cnt;
char NumStr[12];static unsigned big_calls;
void DisplayBigMeter(u8 n){CHECK(n>=1 && n<=3);CHECK(strlen(NumStr)==11);++big_calls;}
void Print_Str2(u16 x0,u16 x1,u16 y,u16 color,u8 mode,char *s,u8 a,u8 b)
{CHECK(x0<x1 && x1<=399 && y<=216);CHECK(color==0x0905);(void)mode;(void)s;(void)a;(void)b;}
const char F_UNIT[12]="Hz",T_UNIT[12]="ns",V_UNIT[12]="uV",P_UNIT[12]="%";
static Y_attr attrs[8];Y_attr *Y_Attr=attrs;
static struct {unsigned x,y,color;char text[64];} painted[128];
static struct {int32_t value;const char *unit;} formats[32];
u32 __Get(u8 object){return object==V_BATTERY?3700:0;}
void Print_Str(u16 x,u16 y,u16 color,u8 mode,char *text)
{
    unsigned width=1,i;CHECK(draws<128);CHECK(mode<=1);
    for(i=0;text[i];++i)width+=text[i]=='!'?4:8;
    if(x+width>400 || y+11>240)fprintf(stderr,"bounds x=%u y=%u width=%u text=%s\n",x,y,width,text);
    CHECK(x+width<=400 && y+11<=240);
    painted[draws].x=x;painted[draws].y=y;painted[draws].color=color;
    strncpy(painted[draws].text,text,63);painted[draws++].text[63]=0;
}
void Int2Str(char *out,s32 value,const char *unit,u8 precision,u8 mode,s8 shift)
{CHECK(formatted<32);(void)precision;(void)mode;(void)shift;formats[formatted].value=value;formats[formatted++].unit=unit;strcpy(out,"12.3mV");}
void Display_Title(void){++title_calls;Menu144_NativeTitle();}
void Display_Meter(void){++meter_calls;Menu144_Meters(0);}
static unsigned Find(unsigned x,unsigned y,const char *text)
{unsigned i;for(i=0;i<draws;++i)if(painted[i].x==x && painted[i].y==y && !strcmp(painted[i].text,text))return i;fprintf(stderr,"Missing %u/%u %s\n",x,y,text);exit(1);}
static void Draw(unsigned group,unsigned detail,unsigned valid)
{
    Scope144Measurements m={-20000,80000,30000,40000,1000000,1000000,500000,500000,500000,0,1024};
    draws=formatted=0;Current=(u8)group;if(group<13)Detail[group]=(u8)detail;
    Menu144_View(&m,1000000,200000,valid,50,2);
}
int main(void)
{
    unsigned i,j,index;for(i=0;i<8;++i)attrs[i].SCALE=2000;
    Draw(0,1,1);CHECK(title_calls==1 && meter_calls==1);
    Find(35,216,"DC");Find(51,216,"50mV");
    CHECK(painted[Find(86,228,"CH(A)")].color==0x0905); /* B source uses fixture source label, in gray. */
    for(i=0;i<9;++i){Find(Meter[i].XPOS1,Meter[i].YPOS,Meter[i].Str+Meter[i].Item*5);Find(Meter[i].XPOS2,Meter[i].YPOS,"12.3mV");}
    Title[0][SOURCE].Value=3;Draw(0,2,1);Find(51,216,"0.5V");Title[0][SOURCE].Value=1;
    Draw(T_BASE,MODE,1);CHECK(painted[Find(240,0,"XY")].color==0x0905);
    Draw(TRACK4,0,1);index=Find(0,0,"CH(D) A+B A-B C&D C|D REC SPEC FFT");CHECK(painted[index].color==0x0905);
    Draw(TRIGG,0,1);CHECK(painted[Find(40,0,"TR B")].color==0x0905);
    Draw(OUTPUT,0,1);CHECK(painted[Find(160,0,"NOISE")].color==0x0905);
    Draw(FILE,3,1);CHECK(painted[Find(270,0,"DAT CFG DIR")].color==0x0905);
    for(i=0;i<12;++i){Meter[0].Item=(u8)i;Meter[0].Track=i<=FPS?4:0;Draw(METER_0,0,1);}
    Meter[0].Item=VPP;Meter[0].Track=1;Draw(METER_0,0,1);CHECK(painted[Find(342,137,"--")].color==0x0905);
    for(i=0;i<22;++i)if(i!=RUNNING)for(j=0;j<Scope144_DetailLimit(i);++j)Draw(i,j,0);
    { Scope144Measurements m={0};unsigned calls=meter_calls;
      draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls);
      Cnt_20mS=8;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls);
      Sec_Cnt++;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+1);
      Twink=!Twink;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+2);
      FlagMeter=0;Draw(0,1,1);CHECK(!big_calls);
      FlagMeter=2;UpdateMeter=4;Meter[2].Track=0;Meter[3].Track=0;Draw(0,1,1);CHECK(big_calls==2);
      UpdateMeter=0;Meter[1].Track=0;Draw(0,1,1);CHECK(big_calls==5);
      Meter[1].Track=1;Draw(0,1,1);CHECK(big_calls==7);
      /* The large panel follows the ordinary 16x20 ms gate, not each frame. */
      {unsigned calls=meter_calls;
       draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls);
       Cnt_20mS=16;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+1);
       Cnt_20mS=17;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+1);
       Cnt_20mS=24;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+1);
       Cnt_20mS=32;draws=formatted=0;Menu144_View(&m,0,0,1,50,0);CHECK(meter_calls==calls+2);}
      FlagMeter=1;
    }
    printf("PASS shared 144 UI: %u checks; normal Title/Meter positions, all menus, gray controls, display bounds\n",checks);
    return 0;
}
