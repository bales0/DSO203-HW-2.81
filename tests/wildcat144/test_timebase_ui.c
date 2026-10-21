/* Actual UI144 with real Frame144 time geometry. No LCD/device access. */
#define main PreviousUiRegressionTests
#define Wildcat144_CursorTime Fixture_CursorTime
#define Wildcat144_XposTime Fixture_XposTime
#include "test_ui144.c"
#undef Wildcat144_CursorTime
#undef Wildcat144_XposTime
#undef main
#include "Frame144.h"
#include "Capture144.h"
static Frame144 time_frame;
static ViewWindowGeometry time_view;
static uint32_t time_words[4096];
static unsigned cursor_calls,request_calls,selected_column;
int Wildcat144_CursorTime(unsigned column,int32_t *ps)
{++cursor_calls;selected_column=column;return Frame144_TimeAt(&time_frame,&time_view,column,ps);}
int Wildcat144_XposTime(int32_t *ps){++request_calls;*ps=123456789;return 1;}
static void Setup(unsigned base,unsigned width,unsigned triggered)
{
 Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024,0,0};
 Frame144_Reset(&time_frame,17);time_frame.words=time_words;time_frame.calibration=cal;
 time_frame.capture.fifo_words=4096;time_frame.capture.valid_begin=triggered?2:152;
 time_frame.capture.valid_words=4096-time_frame.capture.valid_begin;
 time_frame.capture.time_samples=2*time_frame.capture.valid_words;
 time_frame.capture.sample_phase=CAPTURE_PHASE_B_A;time_frame.capture.status=CAPTURE_WINDOW_READY;
 time_frame.capture.sample_period_ps_q16=Scope144_TimePs(65536);time_frame.capture.fifo_status=0x2c;
 time_frame.presentation_q16=Capture144_TriggerPosition(Capture144_Pretrigger(base,width),0,1,0);
 if(triggered){time_frame.capture.trigger_kind=CAPTURE_TRIGGER_HARDWARE_A;
  time_frame.capture.trigger_q16=time_frame.presentation_q16;
  time_frame.capture.flags=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;}
 _T_base=(s16)(21-base);_T1=30;_T2=180;
 CHECK(Frame144_Window(&time_frame,width,base,0,&time_view));
}
static void Selection(void)
{
 unsigned i,panel,mode,detail,twink;
 for(panel=0;panel<3;++panel)for(mode=0;mode<3;++mode)for(twink=0;twink<2;++twink){
  Current=T_BASE;Detail[T_BASE]=MODE;FlagMeter=(u8)panel;_Mode=mode==0?AUTO:mode==1?NORH:SGL;
  Twink=(u8)twink;
  for(i=0;i<12;++i){
   unsigned next=(i+1)%3;CHECK(UiCommon_NextDetail(1));
   CHECK(Detail[T_BASE]==next && Scope144_Available(T_BASE,next,_Kind));
   draws=formatted=0;Menu144_NativeTitle();
   detail=Find(Title[T_BASE][next].XPOS,Title[T_BASE][next].YPOS,
        next==MODE?mode_str[_Mode]:next==BASE?ranges[_T_base]:"XPOS");
   CHECK(painted[detail].mode==Twink);
  }
  CHECK(Detail[T_BASE]==MODE);
 }
}
static void Times(void)
{
 static const unsigned columns[]={0,30,150,180,193,299,385};
 static const int64_t offsets[]={-512LL*65536,-1,0,1,500LL*65536};
 unsigned base,width,triggered,hold,chosen,i,offset;int32_t expected;Frame144 before;ViewWindowGeometry view_before;
 for(base=0;base<3;++base)for(width=300;width<=386;width+=86)for(triggered=0;triggered<2;++triggered)
 for(hold=0;hold<2;++hold)for(chosen=0;chosen<3;++chosen)
 for(offset=0;offset<5;++offset)for(i=0;i<7;++i){
  if(columns[i]>=width)continue;
  Setup(base,width,triggered);_State.Value=hold?HOLD:RUN;
  CHECK(Frame144_Window(&time_frame,width,base,offsets[offset],&time_view));
  if(hold)time_frame.capture.flags|=CAPTURE_WINDOW_FROZEN;
  /* Native selects T2 for every nonzero detail, including delta display. */
  Detail[T_VERNIE]=(u8)chosen;Title[T_VERNIE][chosen?T2:T1].Value=(s16)columns[i];
  Current=T_BASE;Detail[T_BASE]=XPOSI;DisableCursorDisplay=0;
  cursor_calls=request_calls=formatted=0;before=time_frame;view_before=time_view;
  CHECK(Frame144_TimeAt(&time_frame,&time_view,columns[i],&expected));
  Menu144_Cursor(base);
  CHECK(EnableCursorDisplay && CursorDisplaySelect==(chosen?2:0));
  CHECK(cursor_calls==1 && !request_calls && selected_column==columns[i]);
  CHECK(formatted==1 && formats[0].unit==T_UNIT && formats[0].value==expected/1000);
  CHECK(CursorDisplayTimer==150 && !memcmp(&before,&time_frame,sizeof before) && !memcmp(&view_before,&time_view,sizeof view_before));
  if(triggered && !offsets[offset] && columns[i]==150)CHECK(!expected);
  if(!triggered)CHECK(expected>=0); /* No fabricated trigger-relative time. */
  DisableCursorDisplay=1;Menu144_Cursor(base);CHECK(!EnableCursorDisplay && !CursorDisplayTimer);
  DisableCursorDisplay=0;time_frame.capture.status=CAPTURE_WINDOW_ARMED;
  formatted=0;Menu144_Cursor(base);CHECK(!formatted && !EnableCursorDisplay && !CursorDisplayTimer);
 }
}
int main(int argc,char **argv)
{
 if(argc<2 || !strcmp(argv[1],"k4"))Selection();
 if(argc<2 || !strcmp(argv[1],"time"))Times();
 printf("PASS timebase UI: %u checks; real frame T1/T2, K4 focus/blink, trigger/no-trigger, bases/panels, RUN/HOLD; no device IO\n",checks);
 return 0;
}
