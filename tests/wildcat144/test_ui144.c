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
#include "UiCommon.h"
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int Wildcat144_DigitalMeasurement(unsigned ch,Scope144Measurements *out) { (void)ch;memset(out,0,sizeof *out);out->frequency_hz=1000000;out->period_ps=1000000;out->duty_ppm=500000;return 1; }
static unsigned checks,draws,title_calls,meter_calls,formatted,battery_calls,graph_calls;
static unsigned header_clears;
static u16 screen[400][240];
static int cursor_valid=1;static int32_t cursor_origin_ps,xpos_time_ps;
static unsigned overview_visible,overview_first=91,overview_last=106;
int Wildcat144_CursorTime(unsigned column,int32_t *ps)
{if(!cursor_valid)return 0;*ps=cursor_origin_ps+Scope144_CursorTime(0,column);return 1;}
int Wildcat144_XposTime(int32_t *ps){*ps=xpos_time_ps;return 1;}
int Wildcat144_Overview(unsigned *first,unsigned *last)
{*first=overview_first;*last=overview_last;return (int)overview_visible;}
static u16 colors[5]={0x0005,0x0105,0x0205,0x0305,0x0405};
static char sources[5][10]={"OFF","CH(A)","-A","x10A","-x10A"};
static char couples[9][10]={"DC","AC","DT","TL","RS","i2","SP","144D","144A"};
static char ranges[9][10]={"50mV","0.1V","0.2V","0.5V","1V","2V","5V","10V","--"};
char x10[8][6]={"0.5V","1V","2V","5V","10V","20V","50V","100V"};
static char kinds[9][10]={"SIN","TRI","SAW","SQR","PWM","NOISE","OFF","ARB","UART"};
static char rates[23][10]={"1Hz"},mode_str[7][10]={"OFF","AUTO","NORM","NorCL","PERST","SGL","XY"};
static char edge[9][10]={":!\\!",":!^!","<Vt",">Vt","L<[","L>[","H<[","H>[","GEN"},states[3][10]={"!RUN!","HOLD","HOLD"};
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
u8 Current,Detail[14],TrgAuto,FlagMeter=1,UpdateMeter=4,BTwink,EnablePaS;vu8 Twink,Cnt_20mS;vu16 Sec_Cnt;
u8 EnableCursorDisplay,DisableCursorDisplay,CursorDisplaySelect,MinBypass;
vu16 CursorDisplayTimer;
char CursorDisplayStr[12];
char NumStr[12];static unsigned big_calls;
void DisplayBigMeter(u8 n){CHECK(n>=1 && n<=3);CHECK(strlen(NumStr)==11);++big_calls;}
void Print_Str2(u16 x0,u16 x1,u16 y,u16 color,u8 mode,char *s,u8 a,u8 b)
{
    unsigned x=x0,n,k,row,leading=a>10?a-10+2:a;(void)mode;
    CHECK(x0<x1 && x1<=399 && y+22<=240);CHECK(color==0x0905);
    for(k=0;k<leading;++k,++x)for(row=y;row<y+22u;++row)screen[x][row]=0;
    for(n=0;s[n];++n)for(k=0;k<(s[n]=='!'?8u:16u);++k,++x) {
        CHECK(x<=x1);for(row=y;row<y+22u;++row)screen[x][row]=s[n]==' '?0:(u16)s[n];
    }
    for(k=0;k<b;++k,++x){CHECK(x<=x1);for(row=y;row<y+22u;++row)screen[x][row]=0;}
}
void ClearScreenArea(u16 x1,u16 x2,u8 y1,u8 y2)
{
    unsigned x,y;CHECK(x1<=x2&&x2<=400&&y1<=y2&&y2<=240);
    if(x1==172 && x2==237 && y1==216)++header_clears;
    for(x=x1;x<x2;++x)for(y=y1;y<y2;++y)screen[x][y]=0;
}
const char F_UNIT[12]="Hz",T_UNIT[12]="ns",V_UNIT[12]="uV",P_UNIT[12]="%";
static Y_attr attrs[8];Y_attr *Y_Attr=attrs;
static struct {unsigned x,y,color,mode;char text[64];} painted[128];
static struct {int32_t value;const char *unit;} formats[32];
u32 __Get(u8 object){return object==V_BATTERY?3700:0;}
void __Set(u8 object,u32 value){(void)object;(void)value;}
#ifndef NORMAL_TRIGGER_FIXTURE
int Wildcat144_Active(void){return 1;}
#endif
void Print_Str(u16 x,u16 y,u16 color,u8 mode,char *text)
{
    unsigned width=1,i,column=x,row,k;CHECK(draws<128);CHECK(mode<=1);
    for(i=0;text[i];++i)width+=text[i]=='!'?4:8;
    if(x+width>400 || y+11>240)fprintf(stderr,"bounds x=%u y=%u width=%u text=%s\n",x,y,width,text);
    CHECK(x+width<=400 && y+11<=240);
    for(row=y;row<y+11u;++row)screen[column][row]=0;
    ++column;
    for(i=0;text[i];++i)for(k=0;k<(text[i]=='!'?4u:8u);++k,++column)
        for(row=y;row<y+11u;++row)screen[column][row]=text[i]==' '?0:(u16)text[i];
    painted[draws].x=x;painted[draws].y=y;painted[draws].color=color;
    painted[draws].mode=mode;
    strncpy(painted[draws].text,text,63);painted[draws++].text[63]=0;
}
void Int2Str(char *out,s32 value,const char *unit,u8 precision,u8 mode,s8 shift)
{CHECK(formatted<32);(void)precision;(void)mode;(void)shift;formats[formatted].value=value;formats[formatted++].unit=unit;strcpy(out,unit==V_UNIT && precision==2 && mode==UNSIGN?"1.4V ":"12.3mV");}
void Display_Title(void){++title_calls;Menu144_NativeTitle();}
void Display_Meter(void){++meter_calls;Menu144_Meters(0);}
void Update_Battery(void){++battery_calls;}
void Update_View_Area(void){unsigned x,y;++graph_calls;for(x=91;x<307;++x)for(y=0;y<12;++y)screen[x][y]=0xf81f;}
static unsigned Find(unsigned x,unsigned y,const char *text)
{unsigned i;size_t n=strlen(text);for(i=0;i<draws;++i)if(painted[i].x==x && painted[i].y==y && !strncmp(painted[i].text,text,n) && strspn(painted[i].text+n," ")==strlen(painted[i].text+n))return i;fprintf(stderr,"Missing %u/%u %s\n",x,y,text);for(i=0;i<draws;++i)if(painted[i].x==x && painted[i].y==y)fprintf(stderr,"  actual: [%s]\n",painted[i].text);exit(1);}
/* Print_Str's actual PRN background is the low byte; INV swaps it.
 * Check recorded calls independently of the production color helpers. */
static void CheckGeneratorValueBackgrounds(void)
{
    unsigned i;
    for(i=0;i<draws;++i)if(painted[i].y==216 && (painted[i].x==172 || painted[i].x==239)) {
        unsigned color=painted[i].color,mode=painted[i].mode;
        unsigned load=painted[i].x==172 && !strncmp(painted[i].text,"LOAD ",5);
        if(load){CHECK(color==0x050f && mode==PRN);}
        else {
            CHECK((color&255)==5);
            CHECK((mode?color>>8:color&255)==(mode?(_Kind==5?4u:15u):5u));
        }
    }
}
static void Draw(unsigned group,unsigned detail,unsigned valid)
{
    unsigned i,width,j;
    Scope144Measurements m={-20000,80000,30000,40000,1000000,1000000,500000,500000,500000,0,1024};
    draws=formatted=0;Current=(u8)group;if(group<13)Detail[group]=(u8)detail;else Detail[13]=(u8)detail;
    Menu144_View(&m,1000000,200000,valid,50,2);
    for(i=0;i<draws;++i)if(painted[i].y==0) {
        width=0;for(j=0;painted[i].text[j];++j)width+=painted[i].text[j]=='!'?4:8;
        if(painted[i].x<89)CHECK((painted[i].x==0 && !strcmp(painted[i].text,"YPOS")) ||
            (painted[i].x==35 && !strcmp(painted[i].text,"!V1!")) ||
            (painted[i].x==62 && !strcmp(painted[i].text,"!V2!")));
        else if(painted[i].x>=310)CHECK(painted[i].x==312 || painted[i].x==339 || painted[i].x==366);
        else CHECK(painted[i].x+width<=310);
    }
}
u8 GenUartMode=3,GenUartAdj,GenUartCont,GenUartStopBits,GenBaudAdjSpeed,GenAdjustMode;
u16 ArbtSampleNumber=2,DirRange;
u32 GenBaudRate=9600;char Label[15][13];
u8 StartUart,InitiateUartGen,PWAdjustMode,Sweep;
u8 FastDimAdjust,HboldAdjust,AdjBeepLevel;
uc8 ScaleIndex[17]={1,1,1,1,1,1,1,1,1,1,1,2,4,10,20,40,80};
u16 GenBaudIndex=7500;u32 UartFileSize;s8 GenFreqShift;
char SelectedFileName[9],Ext[4];
void Update_Output(void){}
void OutputAdjust(void){}
void PWMdutyControl(u8 direction){(void)direction;}
void PWMscaleReset(u8 direction){(void)direction;}
u8 ReadDir(char *ext){(void)ext;return 0;}
u8 Load_Arbt(u8 n){(void)n;return UI_ARB_INVALID;}
u8 Load_Uart(u8 n){(void)n;return UI_UART_INVALID;}
void PrintDir(u16 row,u8 buffer,u8 mode){(void)row;(void)buffer;(void)mode;}
int main(void)
{
    unsigned i,j;for(i=0;i<8;++i)attrs[i].SCALE=2000;
    Draw(0,1,1);CHECK(title_calls==1 && meter_calls==1);
    CHECK(battery_calls==1 && graph_calls==1);
    Find(333,216,"LEV!");Find(365,216,"!Man!");
    Menu144_Calibration(1);Draw(TRACK1,0,1);Find(333,216,"LEV!");
    Menu144_Calibration(0);Draw(TRACK1,0,1);Find(0,228,"CAL?");
    Menu144_Calibration(2);Find(35,228,"CH(A)");
    Find(0,0,"YPOS");Find(35,0,"!V1!");Find(62,0,"!V2!");
    Find(312,0,"!T1!");Find(339,0,"!T2!");Find(366,0,"XPOS");
    for(i=0;i<draws;++i)CHECK(!(painted[i].x==0 && painted[i].y==216));
    Find(35,216,"DC");Find(51,216,"50mV");
    CHECK(painted[Find(86,228,"CH(A)")].color==0x0905); /* B source uses fixture source label, in gray. */
    for(i=0;i<9;++i){Find(Meter[i].XPOS1,Meter[i].YPOS,Meter[i].Str+Meter[i].Item*5);Find(Meter[i].XPOS2,Meter[i].YPOS,"12.3mV");}
    Title[0][SOURCE].Value=3;Draw(0,2,1);Find(51,216,"0.5V");Title[0][SOURCE].Value=1;
    {unsigned before=graph_calls;Draw(T_BASE,MODE,1);CHECK(graph_calls==before+1);}
    {unsigned before=graph_calls;Draw(TRACK4,0,1);CHECK(graph_calls==before+1);}
    Draw(TRIGG,0,1);Find(333,228,"OFF");
    Draw(OUTPUT,0,1);Find(239,228,"SIN");Find(172,216,"Vo 1.4V");
    Draw(FILE,3,1);Find(230,0,"CSV");
    for(i=0;i<12;++i){Meter[0].Item=(u8)i;Meter[0].Track=i<=FPS?4:0;Draw(METER_0,0,1);}
    Meter[0].Item=VPP;Meter[0].Track=1;Draw(METER_0,0,1);CHECK(painted[Find(342,137,"--")].color==0x0905);
    for(i=TRACK3;i<=TRACK4;++i) {
        Title[i][SOURCE].Value=1;Meter[0].Track=(u8)i;Meter[0].Item=FRQ;
        Draw(METER_0,1,1);
        CHECK(painted[Find(342,137,"12.3mV")].color==((i<<8)|5));
        CHECK(formats[0].value==1000000 || formatted>1);
        Meter[0].Item=VPP;Draw(METER_0,1,1);Find(342,137,"--");
        Title[i][SOURCE].Value=0;Meter[0].Item=FRQ;Draw(METER_0,1,1);Find(342,137,"--");
    }
    Meter[0].Track=TRACK1;
    for(i=0;i<22;++i)if(i!=RUNNING)for(j=0;j<Scope144_DetailLimit(i);++j)Draw(i,j,0);
    /* Readouts use actual 144 time and position scales, even with the panel off. */
    FlagMeter=0;Current=T_VERNIE;Detail[T_VERNIE]=0;_T1=300;formatted=0;
    Menu144_Cursor(0);CHECK(EnableCursorDisplay && CursorDisplaySelect==0 && CursorDisplayTimer);
    CHECK(formats[0].unit==T_UNIT && formats[0].value>=999 && formats[0].value<=1000);
    Current=V_VERNIE;Detail[V_VERNIE]=1;_V2_Vernie=40;_1_posi=100;formatted=0;
    Menu144_Cursor(0);CHECK(EnableCursorDisplay && CursorDisplaySelect==6);
    CHECK(formats[0].unit==V_UNIT && formats[0].value==-120000);
    DisableCursorDisplay=2;Menu144_Cursor(0);CHECK(!EnableCursorDisplay);DisableCursorDisplay=0;
    Current=TRACK1;Menu144_Cursor(0);CHECK(!EnableCursorDisplay);FlagMeter=1;
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
    /* Selection blinking must not erase/repaint the lower waveform. */
    Draw(T_BASE,MODE,1);
    {Scope144Measurements m={0};unsigned graphs=graph_calls;
     draws=formatted=0;Twink=!Twink;Menu144_View(&m,0,0,1,50,0);
     CHECK(graph_calls==graphs);}
    /* WAIT is delayed and confined to the RUN cell; capture stages never
     * force title, panel or footer redraws. Completed readings stay visible. */
    {
        Scope144Measurements m={0};unsigned titles,meters,graphs;
        Menu144_Presentation(1);Draw(TRIGG,2,1);
        Find(333,216,"LEV!");Find(172,216,"Vo 1.4V");
        TrgAuto=0;_Vt1=120;_1_posi=100;formatted=0;Menu144_Cursor(0);
        CHECK(EnableCursorDisplay && CursorDisplaySelect==8 && formats[0].value==40000);
        CHECK(formats[0].unit==V_UNIT);
        TrgAuto=2;Menu144_Cursor(0);CHECK(!EnableCursorDisplay);TrgAuto=0;
        titles=title_calls;meters=meter_calls;graphs=graph_calls;
        Cnt_20mS=0;Menu144_TriggerWait(1);draws=formatted=0;
        Menu144_View(&m,0,0,0,50,0);
        CHECK(title_calls==titles && meter_calls==meters && graph_calls==graphs);
        Cnt_20mS=10;draws=formatted=0;Menu144_View(&m,0,0,0,50,0);
        Find(0,228,"WAIT");CHECK(title_calls==titles && meter_calls==meters);
        Menu144_TriggerWait(2);draws=formatted=0;Menu144_View(&m,0,0,0,50,0);
        Find(0,228,"RUN");CHECK(title_calls==titles && meter_calls==meters);
        for(i=25;i<33;++i)for(j=228;j<239;++j)CHECK(!screen[i][j]);
        Menu144_TriggerWait(0);Draw(TRIGG,2,0);Find(342,137,"12.3mV");
        Menu144_Presentation(0);Draw(TRIGG,2,0);Find(342,137,"--");
        /* Focus on LEV and the fraction is independently visible. */
        for(i=0;i<4;++i)for(j=0;j<2;++j) {
            _Tr_source=TRACK1;TrgAuto=(u8)i;TriggerLevelSelect=j;Twink=INV;
            Draw(TRIGG,2,1);
            CHECK(painted[Find(333,216,"LEV!")].mode==(j?PRN:INV));
            CHECK(painted[Find(365,216,TriggerMenu_LevelText(i))].mode==(j?INV:PRN));
        }
        TrgAuto=0;TriggerLevelSelect=0;Twink=PRN;
    }
    Current=T_BASE;Detail[T_BASE]=XPOSI;xpos_time_ps=1234567;formatted=0;
    Menu144_Cursor(2);CHECK(EnableCursorDisplay && CursorDisplaySelect==10);
    CHECK(formats[0].unit==T_UNIT && formats[0].value==1234);
    Current=T_VERNIE;Detail[T_VERNIE]=T1;_T1=30;cursor_origin_ps=-1000000;formatted=0;
    Menu144_Cursor(2);CHECK(EnableCursorDisplay && formats[0].value<=-899 && formats[0].value>=-901);
    cursor_valid=0;Menu144_Cursor(2);CHECK(!EnableCursorDisplay);cursor_valid=1;
    _State.Value=HOLD;overview_visible=1;Draw(TRACK1,0,1);
    {Scope144Measurements m={0};unsigned graphs=graph_calls,titles=title_calls;
     overview_first++;overview_last++;draws=formatted=0;
     Menu144_View(&m,0,0,1,50,0);CHECK(graph_calls==graphs+1 && title_calls==titles);
     Menu144_View(&m,0,0,1,50,0);CHECK(graph_calls==graphs+1);
     overview_visible=0;Menu144_View(&m,0,0,0,50,0);CHECK(graph_calls==graphs+2);
     Draw(TRIGG,0,1);graphs=graph_calls;overview_visible=1;
     Menu144_View(&m,0,0,1,50,0);CHECK(graph_calls==graphs+1);}
    /* Regression: pre-existing Normal digits and units must be erased even
     * when the new field only contains '--'; keep the original U/0 header. */
    {
        unsigned x,y,d,state;Scope144Measurements m={0};
        FlagMeter=1;Meter[0].Track=TRACK2;Meter[0].Item=RMS;
        for(x=342;x<400;++x)for(y=137;y<148;++y)screen[x][y]=0xffff;
        for(x=137;x<170;++x)for(y=216;y<227;++y)screen[x][y]=0x1234;
        Draw(TRACK1,0,1);Find(342,137,"--");
        for(x=359;x<400;++x)for(y=137;y<148;++y)CHECK(!screen[x][y]);
        for(x=137;x<170;++x)for(y=216;y<227;++y)CHECK(screen[x][y]==0x1234);
        FlagMeter=2;UpdateMeter=0;Meter[1].Track=TRACK2;Meter[1].Item=FRQ;
        for(x=313;x<400;++x)for(y=129;y<176;++y)screen[x][y]=0xffff;
        Draw(TRACK1,0,1);
        for(x=349;x<400;++x)for(y=154;y<176;++y)CHECK(!screen[x][y]);
        for(x=313;x<346;++x)for(y=129;y<151;++y)CHECK(!screen[x][y]);
        FlagMeter=1;UpdateMeter=4;Meter[0].Track=TRACK1;
        /* Selecting each trigger detail and blinking never writes footer text. */
        for(state=RUN;state<=HOLD;++state)for(d=0;d<4;++d) {
            unsigned graphs;
            _State.Value=(s16)state;Draw(TRIGG,d,1);graphs=graph_calls;
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            draws=formatted=0;Twink=!Twink;Menu144_View(&m,0,0,1,50,0);
            CHECK(graph_calls==graphs);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            for(x=0;x<draws;++x)CHECK(!strstr(painted[x].text,"CAL1")&&!strstr(painted[x].text,"CAL2"));
        }
        _State.Value=RUN;Draw(TRACK1,0,1);
        {unsigned graphs=graph_calls;overview_first++;overview_last++;draws=formatted=0;
         Menu144_View(&m,0,0,1,50,0);CHECK(graph_calls==graphs);}
    }
    {
        unsigned kind,source,base,x,y,graphs;Scope144Measurements m={0};
        for(kind=0;kind<9;++kind)for(source=TRACK1;source<=TRACK4;++source)if(source!=TRACK2)
        for(base=0;base<3;++base) {
            _Tr_kind=(s16)kind;_Tr_source=(s16)source;_T_base=(s16)(21-base);
            _T1=30;_T2=180;Draw(TRIGG,TR_KIND,1);Find(373,228,edge[kind]);
            Find(172,216,"Vo 1.4V");
            for(x=137;x<170;++x)for(y=216;y<227;++y)CHECK(screen[x][y]==0x1234);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            graphs=graph_calls;draws=formatted=0;Twink=!Twink;Menu144_View(&m,0,0,1,50,0);
            CHECK(graph_calls==graphs);Find(373,228,edge[kind]);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            if(source!=TRACK1) {
                CHECK(painted[Find(333,216,"LEV!")].color==0x0905);
                CHECK(painted[Find(365,216,"!---!")].color==0x0905);
            }
        }
        _Tr_kind=4;Draw(TRIGG,TR_KIND,1);_Tr_kind=1;Draw(TRIGG,TR_KIND,1);
        Find(172,216,"Vo 1.4V");
    }
    /* Generator selection owns the upper native fields, never the purple
     * overview. Exercise a persistent LCD across every context transition. */
    {
        unsigned state,kind,detail,panel,pulse,x,y,graphs,titles,found;
        Scope144Measurements m={0};
        for(state=RUN;state<=HOLD;++state)for(panel=0;panel<3;++panel)
        for(pulse=0;pulse<2;++pulse)for(kind=0;kind<9;++kind)for(detail=0;detail<4;++detail) {
            _State.Value=(s16)state;FlagMeter=(u8)panel;UpdateMeter=4;
            _Tr_kind=pulse?4:1;_Kind=(s16)kind;_Duty=15000;Title[OUTPUT][OUTATT].Value=50;
            Draw(FILE,1,1); /* A different menu left real text in the footer. */
            for(x=137;x<170;++x)for(y=216;y<227;++y)screen[x][y]=0x1234;
            Draw(OUTPUT,detail,1);Find(239,228,kinds[kind]);if(kind!=8)Find(239,216,"1Hz");
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            for(x=0;x<draws;++x)if(painted[x].y==0)CHECK(painted[x].x<89 || painted[x].x>=310);
            for(x=137;x<170;++x)for(y=216;y<227;++y)CHECK(screen[x][y]==0x1234);
            if(!Generator_Detail(kind,detail))found=Find(172,216,"--");
            else if(kind==8){found=Find(172,216,"9600");Find(239,216,"UART");}
            else if(kind==7)found=Find(172,216,"2 pts");
            else if(kind==6 || kind==5)found=Find(172,216,"");
            else if(kind==PWM)found=Find(172,216,"D 50.0%");
            else found=Find(172,216,"Vo 1.4V");
            CHECK(painted[found].color==Generator_AuxColor());
            CheckGeneratorValueBackgrounds();
            CHECK(painted[Find(239,228,kinds[kind])].color==(kind==5?0x0504:0x050f));
            graphs=graph_calls;draws=formatted=0;Twink=!Twink;
            Menu144_View(&m,0,0,1,50,0);CHECK(graph_calls==graphs);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            /* HOLD overview changes while GEN is selected update the graphic
             * alone. RUN still has a waveform without a selection box. */
            if(state==HOLD) {
                overview_visible=1;overview_first=overview_first<290?overview_first+1:91;
                overview_last=overview_first+8;titles=title_calls;
                draws=formatted=0;Menu144_View(&m,0,0,1,50,0);
                CHECK(graph_calls==graphs+1 && title_calls==titles);
                for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            }
            Draw(TRIGG,TR_KIND,1);
            if(kind<PWM)Find(172,216,"Vo 1.4V");
            else if(kind==PWM)Find(172,216,"D 50.0%");
            else if(pulse)Find(172,216,"T12.3mV");
            else for(x=172;x<237;++x)for(y=216;y<227;++y)CHECK(!screen[x][y]);
        }
        _Tr_kind=1;_Kind=PWM;_Duty=30000;Draw(OUTPUT,DUTYPWM,1);Find(172,216,"D 100.0%");
        _Duty=1000;Draw(OUTPUT,DUTYPWM,1);Find(172,216,"D 3.3%");
        for(x=221;x<237;++x)for(y=216;y<227;++y)CHECK(!screen[x][y]);
        _Kind=0;Title[OUTPUT][OUTATT].Value=100;Draw(OUTPUT,OUTATT,1);Find(172,216,"Vo 1.4V");
        Title[OUTPUT][OUTATT].Value=0;Draw(OUTPUT,OUTATT,1);Find(172,216,"Vo 1.4V");
        /* Voltage stays in its fixed cell, with the native unit formatter. */
        /* A normal-mode auxiliary label must also be erased on first full
         * redraw when no generator or pulse context owns the header cell. */
        Draw(TRIGG,TR_KIND,1);
        for(x=172;x<237;++x)for(y=216;y<227;++y)screen[x][y]=0xffff;
        Draw(TRACK1,SOURCE,1);
        Find(172,216,"Vo 1.4V");
    }
    {
        unsigned group,detail,state,x,y;Scope144Measurements m={0};
        FlagMeter=1;_Kind=0;
        for(state=RUN;state<=HOLD;++state)for(group=0;group<22;++group)if(group!=FILE && group!=RUNNING)
        for(detail=0;detail<Scope144_DetailLimit(group);++detail) {
            _State.Value=(s16)state;Draw(group,detail,1);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
            draws=formatted=0;Twink=!Twink;Menu144_View(&m,0,0,1,50,0);
            for(x=91;x<307;++x)for(y=0;y<12;++y)CHECK(screen[x][y]==0xf81f);
        }
    }
    /* File generators keep their selected name while the actual engine is
     * safely OFF, including after mode entry clears an old status message. */
    for(i=7;i<=8;++i) {
        _Kind=(s16)i;ArbtSampleNumber=0;UartFileSize=0;Generator_Validate();
        CHECK(_Kind==6 && Generator_DisplayKind()==i);
        UiCommon_Status(0);Draw(OUTPUT,KIND,1);
        Find(239,228,kinds[i]);Find(172,216,i==7?"LOAD ARB":"LOAD UAR");
        CheckGeneratorValueBackgrounds();
        for(j=0;j<draws;++j)CHECK(!(painted[j].x==239 && painted[j].y==228 && strstr(painted[j].text,"OFF")));
    }
    printf("PASS shared 144 UI: %u checks; all generator kinds/details, persistent footer and upper field ownership, blink, RUN/HOLD, pulse context, meters, bounds\n",checks);
    return 0;
}
