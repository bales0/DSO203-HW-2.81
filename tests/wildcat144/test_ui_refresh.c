/* Actual UI scheduling: dirty meter definitions cannot wait for K3. */
#include "Frame144.h"
#define REAL_NORMAL_MARKS
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
/* BIOS raster for the unmodified production Normal marker painter. */
extern u16 Color[16];
u16 MAX_X=311;
u8 TrackBuff[397*4],RMSdata[2][700],discard,Rdiscard,FrameEndFlag;
static unsigned preview_draws;
int Draw_TraceSegment(unsigned,unsigned,unsigned,u8 *);
void Draw_Window(void)
{
 unsigned x,y;u8 bounds[2];++preview_draws;
 for(x=1;x<(unsigned)(MAX_X-11);++x){
  for(y=1;y<200;++y)screen[11+x][13+y]=0;
  if(Draw_TraceSegment(TrackBuff[(x-1)*4],TrackBuff[x*4],2,bounds))
   for(y=bounds[0];y<=bounds[1];++y)screen[11+x][13+y]=Color[0];
 }
}
u16 Color[16]={0xffe0,0x07ff,0xf81f,0x07e0};
s32 Tmp2;vu16 ConfNameTimer;static u16 pixel_x,pixel_y;
void __Row_DMA_Ready(void){}
void __LCD_Set_Block(u16 a,u16 b,u16 c,u16 d){(void)a;(void)b;(void)c;(void)d;}
void __Point_SCR(u16 x,u16 y){CHECK(x<400 && y<240);pixel_x=x;pixel_y=y;}
void __LCD_SetPixl(u16 color){CHECK(pixel_x<400 && pixel_y<240);screen[pixel_x][pixel_y++]=color;}
void PrintConfigName(void){}
u8 SpiLogic(void){return 0;}
u8 i2cLogic(void){return 0;}
s32 ScaleXposi(void){return 0;}
void DrawBase(u16 x,u16 c){(void)x;(void)c;}
static void NativeMarkPixels(void)
{
 unsigned x,y;u16 zero[7][9],trigger[7][9];
 _1_source=1;_2_source=_3_source=_4_source=0;_Tr_source=TRACK1;
 _1_posi=100;_Vt1=130;draws=formatted=0;Update_Mark();
 for(x=0;x<7;++x)for(y=0;y<9;++y){
  zero[x][y]=screen[x][109+y];trigger[x][y]=screen[x][139+y];
 }
 CHECK(memcmp(zero,trigger,sizeof zero));
 _1_posi=110;Update_Mark();
 for(x=0;x<7;++x)for(y=0;y<9;++y){
  CHECK(screen[x][119+y]==zero[x][y]);
  CHECK(screen[x][139+y]==trigger[x][y]);
  CHECK(screen[x][109+y]==0);
 }
 /* Same exact overlap as Normal: no alternative label coordinate. */
 _1_posi=130;Update_Mark();
 for(x=0;x<7;++x)for(y=0;y<9;++y)CHECK(screen[x][139+y]==trigger[x][y]);
 _1_posi=100;
}
/* Actual persistent LCD fixture: all title dirty flags have already been
 * consumed before a panel clear, as after K3 while waiting for a record. */
static void PanelRows(void)
{
 Scope144Measurements m={0};unsigned panel,page,waiting,valid,i,g,d,visible;
 for(panel=0;panel<3;++panel)for(page=0;page<5;++page)
 for(waiting=1;waiting<3;++waiting)for(valid=0;valid<2;++valid){
  FlagMeter=(u8)panel;UpdateMeter=(u8)page;Current=TRACK1;Twink=PRN;
  for(i=0;i<9;++i){Meter[i].Item=VDC;Meter[i].Track=TRACK2;}
  Menu144_TriggerWait(0);Menu144_Presentation(0);Draw(TRACK1,0,valid);
  for(g=BK_LIGHT;g<=VOLUME;++g)for(d=0;d<2;++d)Title[g][d].Flag=0;
  ClearScreenArea(312,400,11,216);CHECK(!screen[315][168] && !screen[315][153]);
  draws=formatted=0;Menu144_TriggerWait(waiting);Menu144_View(&m,0,0,valid,2);
  visible=panel==1 || (panel==2 && page==4);
  for(g=BK_LIGHT;g<=VOLUME;++g){
   unsigned seen=0;int before=Title[g][1].Value;
   for(i=0;i<draws;++i)if(painted[i].x==Title[g][0].XPOS && painted[i].y==Title[g][0].YPOS &&
       !strcmp(painted[i].text,Title[g][0].Str))++seen;
   CHECK(seen==visible);
   if(visible){
    CHECK(screen[Title[g][0].XPOS+1][Title[g][0].YPOS+1]!=0);
    CHECK(screen[Title[g][1].XPOS+9][Title[g][1].YPOS+1]!=0);
    CHECK(!(Title[g][0].Flag&UPDAT) && !(Title[g][1].Flag&UPDAT));
   }
   CHECK(Title[g][1].Value==before);
  }
  /* Subsequent idle updates keep the restored rows and native refresh gates. */
  draws=formatted=0;Menu144_View(&m,0,0,valid,0);
  if(visible){CHECK(screen[315][168]!=0);CHECK(screen[315][153]!=0);}
 }
 Menu144_TriggerWait(0);
}

static void YposStatusAndBattery(void)
{
 Scope144Measurements m={0};unsigned i,batteries,markers;
 FlagMeter=0;_State.Value=RUN;_Kind=6;_4_source=0;Current=TRACK1;Detail[TRACK1]=POSI;
 Sec_Cnt=100;Cnt_20mS=0;Twink=PRN;Menu144_TriggerWait(0);Menu144_Presentation(1);
 Draw(TRACK1,POSI,1);batteries=battery_calls;
 for(i=0;i<20;++i){
  _1_posi=(s16)(100+i);Twink^=1;draws=formatted=0;markers=marker_calls;
  Menu144_View(&m,0,0,0,1);
  CHECK(battery_calls==batteries);
  CHECK(marker_calls==markers+1 && marker_y==_1_posi);
 }
 Sec_Cnt=101;draws=formatted=0;Menu144_View(&m,0,0,0,0);CHECK(battery_calls==batteries+1);
 draws=formatted=0;Menu144_View(&m,0,0,0,2);CHECK(battery_calls==batteries+1);
 /* A first complete frame restores the marks after the row renderer, even
  * without a key or timer tick. Subsequent unchanged frames do not flash it. */
 Menu144_Presentation(0);draws=formatted=0;Menu144_View(&m,0,0,0,0);markers=marker_calls;
 draws=formatted=0;Menu144_View(&m,0,0,1,0);CHECK(marker_calls==markers+1);
 draws=formatted=0;Menu144_View(&m,0,0,1,0);CHECK(marker_calls==markers+1);
 Sec_Cnt=102;Cnt_20mS=0;Menu144_TriggerWait(0);Menu144_TriggerWait(1);
 draws=formatted=0;Menu144_View(&m,0,0,0,0);
 Cnt_20mS=10;draws=formatted=0;Menu144_View(&m,0,0,0,0);Find(0,228,"WAIT");
 _State.Value=HOLD;draws=formatted=0;Menu144_View(&m,0,0,0,0);Find(0,228,"HOLD");
 _State.Value=RUN;Cnt_20mS=15;Menu144_TriggerWait(0);Menu144_TriggerWait(1);
 Cnt_20mS=16;draws=formatted=0;Menu144_View(&m,0,0,0,1);Find(0,228,"RUN");
 Cnt_20mS=25;draws=formatted=0;Menu144_View(&m,0,0,0,0);Find(0,228,"WAIT");
 Menu144_TriggerWait(0);
}
/* Execute actual Process.c renderer with a destroyed/invalid raw epoch.
 * Independent saved offsets prove every key, reversal and offscreen return. */
extern void * __stdcall VirtualAlloc(void *,unsigned long,unsigned long,unsigned long);
static void ContinuousYposPixels(void)
{
 static uint32_t words[4096];static Frame144 frame;
 int rel[397];u8 digital[397][2];unsigned panel,base,inv,x,n,i,width,positions[]={100,101,104,103,0,200,0,100};
 ViewWindowGeometry view;Scope144Measurements measure,before;Scope144Cal cal={1024,0,120*1024,125*1024,125000,1,0,1024,0,1024,1024,40,1024,1024,0,0};
 CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 for(panel=0;panel<2;++panel)for(base=0;base<3;++base)for(inv=0;inv<2;++inv){
  unsigned count,last=100;
  width=panel?300:386;MAX_X=(u16)(11+width);attrs[0].SCALE=1000;
  _1_source=1;_2_source=0;_3_source=_4_source=1;_Tr_source=TRACK1;_Vt1=130;
  _3_posi=50;_4_posi=80;
  Frame144_Reset(&frame,1);frame.words=words;frame.range=0;frame.calibration=cal;
  frame.capture.sample_period_ps_q16=1000000000000ULL*65536/144000000;
  frame.capture.fifo_words=4096;frame.capture.valid_begin=2;frame.capture.valid_words=4094;
  frame.capture.time_samples=8188;frame.capture.status=CAPTURE_WINDOW_READY;
  frame.capture.sample_phase=CAPTURE_PHASE_B_A;
  for(i=0;i<4096;++i)words[i]=(i*7%256)|((255-i*11%256)<<8)|((i&3)<<16);
  CHECK(Frame144_Valid(&frame) && Frame144_Window(&frame,width,base,0,&view));
  CHECK(Frame144_Measure(&frame,&measure));before=measure;
  for(x=0;x<width;++x){int32_t q,v;
   CHECK(Frame144_At(&frame,&view,x,&q));v=Scope144_Voltage(&cal,q);
   rel[x]=(inv?-v:v)/1000;
  }
  Process144_DrawFrame(&frame,&view,100,inv);count=preview_draws;
  for(x=0;x<width;++x){digital[x][0]=TrackBuff[x*4+2];digital[x][1]=TrackBuff[x*4+3];}
  /* A new DMA/FIFO epoch may already have replaced every sample. */
  frame.words=(const uint32_t *)1;frame.capture.status=CAPTURE_WINDOW_ARMED;
  memset(words,0xa5,sizeof words);
  for(n=0;n<sizeof positions/sizeof *positions+402;++n){
   unsigned p=n<sizeof positions/sizeof *positions?positions[n]:(n<209?n-8:409-n);
   CHECK(p<=200);
   CHECK(Process144_Preview(p,inv)==(p!=last));
   if(p!=last)++count;
   CHECK(preview_draws==count && _Vt1==130 && !Frame144_Valid(&frame));last=p;
   for(x=0;x<width;++x){int y=rel[x]+(int)p;
    CHECK(TrackBuff[x*4]==(y<1?0:y>199?200:y));
    CHECK(!TrackBuff[x*4+1] && TrackBuff[x*4+2]==digital[x][0] && TrackBuff[x*4+3]==digital[x][1]);
   }
   CHECK(TrackBuff[width*4]==TrackBuff[(width-1)*4]);
   CHECK(!memcmp(&measure,&before,sizeof measure));
  }
  /* Pixel at the expected horizontal trace location is restored after
   * leaving either screen edge; digital lane coordinates remain intact. */
  CHECK(Process144_Preview(100,inv)==(last!=100));
  for(x=1;x<width;++x)if(TrackBuff[x*4]>=1 && TrackBuff[x*4]<=199)
    CHECK(screen[11+x][13+TrackBuff[x*4]]==Color[0]);
  CHECK(!Process144_Preview(101,!inv));
  MAX_X=(u16)(MAX_X-1);CHECK(!Process144_Preview(101,inv));MAX_X=(u16)(MAX_X+1);
  Process144_ClearFrame();CHECK(!Process144_Preview(101,inv));
  for(x=0;x<(width+1)*4;++x)CHECK(!TrackBuff[x]);
 }
 MAX_X=311;
}

int main(void){
 NativeMarkPixels();
 Scope144Measurements m={0};unsigned count;
 FlagMeter=1;UpdateMeter=4;_Kind=6;Current=TRACK1;Twink=0;Sec_Cnt=20;Cnt_20mS=8;
 Meter[3].Item=VDC;Meter[3].Track=TRACK2;Draw(TRACK1,0,1);Find(314,92,"Vdc");Find(342,92,"--");
 count=meter_calls;Meter[3].Item=VPP;Meter[3].Track=TRACK1;Meter[3].Flag|=UPDAT;
 draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls==count+1);Find(314,92,"Vpp");CHECK(!(Meter[3].Flag&UPDAT));
 count=meter_calls;draws=formatted=0;Menu144_View(&m,0,0,1,0);CHECK(meter_calls==count);
 /* A newly valid frame re-enables its existing field without changing the
  * selected item/source or waiting for the periodic meter update. */
 Menu144_Presentation(0);draws=formatted=0;Menu144_View(&m,0,0,0,0);
 Find(342,92,"--");count=meter_calls;
 draws=formatted=0;Menu144_View(&m,0,0,1,0);CHECK(meter_calls==count+1);
 CHECK(Meter[3].Item==VPP && Meter[3].Track==TRACK1);
 /* Availability changes must repaint without a blink, panel toggle or tick. */
 Meter[3].Item=FRQ;Meter[3].Track=TRACK1;Meter[3].Flag|=UPDAT;
 Menu144_MeasurementTime(1);Menu144_View(&m,0,0,1,0);count=meter_calls;
 Menu144_MeasurementTime(0);draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls==count+1);Find(342,92,"--");count=meter_calls;
 Menu144_MeasurementTime(1);draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls==count+1 && formatted && !(Meter[3].Flag&UPDAT));
 /* A hidden default-page meter must not schedule a repaint or consume
  * its dirty flag. Showing it computes/paints on this same HOLD frame. */
 FlagMeter=2;UpdateMeter=4;Twink=0;Cnt_20mS=8;
 for(count=0;count<9;++count){Meter[count].Item=VDC;Meter[count].Track=TRACK1;Meter[count].Flag=0;}
 Draw(TRACK1,0,1);count=meter_calls;
 Meter[1].Item=FRQ;Meter[1].Track=TRACK3;Meter[1].Flag|=UPDAT;
 draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls==count && (Meter[1].Flag&UPDAT) && !UiCommon_TimeDemand(0));
 UpdateMeter=TRACK3;draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls==count+1 && !(Meter[1].Flag&UPDAT) && (UiCommon_TimeDemand(0)&4u));
 FlagMeter=0;Meter[2].Flag|=UPDAT;count=meter_calls;
 draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK((Meter[2].Flag&UPDAT) && !UiCommon_TimeDemand(0) && !formatted);
 FlagMeter=1;draws=formatted=0;Menu144_View(&m,0,0,1,0);
 CHECK(meter_calls>count && !(Meter[2].Flag&UPDAT));
 PanelRows();YposStatusAndBattery();ContinuousYposPixels();
 printf("PASS UI refresh: %u checks; gray Vdc -> valid Vpp with dirty metadata, availability changes, K3 row restoration, small/large/pages, waiting/read/HOLD, native idle gates, continuous YPOS/reversal/offscreen pixels with destroyed raw epoch, unchanged C/D; no device IO\n",checks);return 0;
}
