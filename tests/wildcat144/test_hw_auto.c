/* Runtime HW AUTO and requested/effective XPOS regressions. Host only. */
#define main NativeService_PreviousTests
#include "test_native.c"
#undef main
static void Init(void)
{
 unsigned i;CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 StartupImageCheck();Wildcat144_CheckImage();
 xattributes[_100uS].ARR=239;
 for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
}
static void BeginAt(unsigned base,unsigned mode,unsigned kind,unsigned source,unsigned invert,unsigned panel)
{
 hw_test=1;hw_ready=0;capture_case=history=0;key_status=0xffff;TrgAuto=0;
 _Mode=(s16)mode;_State.Value=RUN;_Tr_source=(s16)source;_Tr_kind=(s16)kind;
 _T_base=(s16)(21-base);_A_Range=0;_1_source=(s16)(1+invert);_3_source=_4_source=1;FlagMeter=(u8)panel;
 Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;_X_posi.Limit=3703;
 CHECK(Wildcat144_Service());
}
static void Begin(unsigned base,unsigned mode,unsigned kind){BeginAt(base,mode,kind,0,0,0);}
static void AutoEdge(void)
{
 unsigned i;Begin(0,AUTO,1);CHECK(hw_waiting && !fifo_index && !shown_valid);
 hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
 CHECK(shown_valid && last_frame->capture.trigger_kind==CAPTURE_TRIGGER_HARDWARE_A);
 Current=TRACK1;KeyEvent(K_ITEM_S);
}
static void Requested(void)
{
 unsigned i;int32_t ps;Begin(2,SGL,1);hw_ready=1;
 for(i=0;i<8;++i)KeyEvent(0);
 CHECK(_State.Value==HOLD && Frame144_Valid(last_frame));
 CHECK(_X_posi.Value==0 && Wildcat144_XposTime(&ps) && ps==0);
 Current=TRACK1;KeyEvent(K_ITEM_S);
}

typedef struct {
 Capture144Session session;CaptureWindow window;unsigned reads,configs,starts,stops;
 unsigned fail_read,cancel;int config_ok,start_ok,full;uint32_t now;
} SessionMock;
static int SCfg(void *u){SessionMock *m=u;++m->configs;CHECK(m->session.pretrigger>=20);return m->config_ok;}
static int SStart(void *u){SessionMock *m=u;++m->starts;return m->start_ok;}
static int SStop(void *u){SessionMock *m=u;++m->stops;return 1;}
static uint32_t SStatus(void *u){return ((SessionMock *)u)->full?0x2c:0;}
static uint32_t SNow(void *u){return ((SessionMock *)u)->now++;}
static int SCancel(void *u){return ((SessionMock *)u)->cancel;}
static int SRead(void *u,uint32_t *v){SessionMock *m=u;*v=m->reads++;return !m->fail_read || m->reads!=m->fail_read;}
static void SessionReset(SessionMock *m){memset(m,0,sizeof *m);m->config_ok=m->start_ok=m->full=1;}
static void Positions(void)
{
 const unsigned pres[]={150,183,235,256};const int shifts[]={-32*65536,-1,0,12*65536,32*65536};
 SessionMock m;Capture144AsyncOps ops={{&m,SCfg,SStart,SStatus,SRead,SNow,SCancel},SStop};
 unsigned p,source,kind,sh,i;uint32_t expected;Capture144Status result;
 for(p=0;p<4;++p)for(source=0;source<4;++source)if(source!=1)
 for(kind=0;kind<9;++kind)for(sh=0;sh<5;++sh){
  SessionReset(&m);expected=(2u*pres[p]-(source?6u:5u)-(kind>=4 && kind<=7?2u:0u))*65536u;
  if(source)expected+=shifts[sh];
  CHECK(Capture144_TriggerPosition(pres[p],source,kind,shifts[sh])==expected);
  CHECK(Frame144_HardwareReference(pres[p],source,kind,shifts[sh])==expected);
  CHECK(Capture144_Begin(&m.session,&WildcatFpga,&ops,(uint32_t *)DataBuf,72,
      Scope144_TimePs(65536),100000,100000,pres[p],&m.window)==CAPTURE144_PENDING);
  if(source)CHECK(Capture144_DigitalTrigger(&m.session,source-2,shifts[sh]));
  CHECK(Capture144_TriggerKind(&m.session,kind));CHECK(m.window.trigger_q16==expected);
  CHECK(!(m.window.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  result=CAPTURE144_PENDING;for(i=0;i<8;++i)result=Capture144_Poll(&m.session,512);
  CHECK(result==CAPTURE144_OK && !m.session.stage && m.reads==4097 && m.configs==1 && m.starts==1);
  CHECK(m.window.valid_begin==2 && m.window.valid_words==4094 && m.window.time_samples==8188);
  CHECK(m.window.sample_phase==CAPTURE_PHASE_B_A && m.window.trigger_q16==expected);
  CHECK(m.window.flags&CAPTURE_WINDOW_HAS_TRIGGER);
  for(i=0;i<4096;++i)CHECK(DataBuf[i]==i+1);
 }
 for(p=0;p<3;++p)for(i=300;i<=386;i+=86){
  unsigned pre=Capture144_Pretrigger(p,i);Frame144 f;ViewWindowGeometry view;Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,0,1024,0,1024,1024,54,1024,1024,0,0};
  CHECK(pre==(p==2?256u:150u));Frame144_Reset(&f,1);f.words=(uint32_t *)DataBuf;f.calibration=cal;
  f.capture=m.window;f.capture.trigger_kind=CAPTURE_TRIGGER_HARDWARE_A;
  for(source=0;source<4;++source)if(source!=1)for(kind=0;kind<8;++kind)for(sh=0;sh<5;++sh){
   int64_t origin;unsigned center=i/2;f.trigger_source=source;f.digital_shift_q16=shifts[sh];
   f.capture.trigger_q16=Capture144_TriggerPosition(pre,source,kind,shifts[sh]);
   f.presentation_q16=f.capture.trigger_q16;
   CHECK(Frame144_Window(&f,i,p,0,&view));
   CHECK(f.capture.trigger_q16-view.window_start_q16==150u*Scope144_Step(p));
   CHECK(Frame144_WindowOrigin(&f,i,p,&origin));
   CHECK(Frame144_Window(&f,i,p,-(int64_t)(center-150)*Scope144_Step(p),&view));
   CHECK(f.capture.trigger_q16-view.window_start_q16==center*Scope144_Step(p));
   {int32_t q;CHECK(Frame144_At(&f,&view,0,&q));CHECK(Frame144_At(&f,&view,i-1,&q));}
  }
 }
 CHECK(Capture144_TriggerPosition(19,0,0,0)==UINT32_MAX);
 CHECK(Capture144_TriggerPosition(1025,0,0,0)==UINT32_MAX);
 CHECK(Capture144_TriggerPosition(150,1,0,0)==UINT32_MAX);
 CHECK(Capture144_TriggerPosition(150,2,0,32*65536+1)==UINT32_MAX);
 CHECK(!Capture144_Pretrigger(3,386) && !Capture144_Pretrigger(2,0));
 /* Failure exits invalidate metadata and never produce a fallback read. */
 for(p=0;p<6;++p){
  SessionReset(&m);if(p==0)m.config_ok=0;if(p==1)m.start_ok=0;
  if(p==2)m.cancel=1;
  result=Capture144_Begin(&m.session,&WildcatFpga,&ops,(uint32_t *)DataBuf,73,
      Scope144_TimePs(65536),1000,10000,256,&m.window);
  if(p>=3){CHECK(result==CAPTURE144_PENDING);if(p==3)m.fail_read=27;
   if(p==4){m.full=0;m.now+=1001;}
   if(p==5){CHECK(Capture144_Poll(&m.session,512)==CAPTURE144_PENDING);m.now+=10001;}
   result=Capture144_Poll(&m.session,512);
  }
  CHECK(result==(p==2?CAPTURE144_CANCELLED:p>=4?CAPTURE144_TIMEOUT:CAPTURE144_IO));
  CHECK(!m.session.stage && m.stops==1 && !m.window.time_samples && !(m.window.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  CHECK(m.reads==(p==3?27u:p==5?513u:0u));
 }
}

static void RuntimeEpochs(void)
{
 unsigned base,panel,source,kind,invert,i,a,b,c,d,records;int32_t requested;uint32_t start;
 for(base=0;base<3;++base)for(panel=0;panel<2;++panel)for(source=0;source<4;++source)if(source!=1)
 for(kind=0;kind<2;++kind)for(invert=0;invert<(source?1u:2u);++invert){
  SetLowVolume=Key_Mid_Speed=0;BeginAt(base,AUTO,kind,source,invert,panel);
  CHECK(hw_waiting && !fifo_index && hw_pre==(base==2?256u:150u));
  CHECK(hw_mode==Scope144_TriggerMode(source,kind,invert));
  hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
  CHECK(Frame144_Valid(last_frame) && (last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  CHECK(_X_posi.Value==0 && Wildcat144_XposTime(&requested) && !requested);
  /* Pan the immutable record; RUN/pending checks follow separately. */
  KeyEvent(KEY1);CHECK(_State.Value==HOLD);
  Current=T_BASE;Detail[T_BASE]=XPOSI;for(i=0;i<27;++i)KeyEvent(K_INDEX_DEC);
  CHECK(Wildcat144_XposTime(&requested) && requested<0);
  if(base==2 && !panel)CHECK(Wildcat144_TriggerColumn()>=MIN_X+193);
  start=last_view.window_start_q16;CHECK(Wildcat144_Overview(&a,&b));
  hw_ready=0;KeyEvent(KEY1);CHECK(hw_waiting && shown_valid);
  {int32_t value;CHECK(Wildcat144_XposTime(&value) && value==requested);}
  CHECK(Wildcat144_Overview(&c,&d) && a==c && b==d);
  fake_cycles+=101u*72000u;KeyEvent(0);
  CHECK(_State.Value==RUN && !hw_waiting && Frame144_Valid(last_frame));
  CHECK(last_frame->capture.trigger_kind==CAPTURE_TRIGGER_NONE && !last_frame->capture.trigger_q16);
  CHECK(!(last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  CHECK(last_view.window_start_q16==start && Wildcat144_Overview(&c,&d) && a==c && b==d);
  {int32_t value;CHECK(Wildcat144_XposTime(&value) && value==requested);}
  hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
  CHECK(last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER);
  CHECK(last_view.window_start_q16==start && Wildcat144_Overview(&c,&d) && a==c && b==d);
  KeyEvent(KEY1);CHECK(_State.Value==HOLD);
  records=record_count;for(i=0;i<6;++i)KeyEvent(KEY3);
  CHECK(_State.Value==HOLD && record_count==records);
  {int32_t value;CHECK(Wildcat144_XposTime(&value) && value==requested);}
  Current=TRACK1;KeyEvent(K_ITEM_S);
 }
}
static void RearmAndFaults(void)
{
 unsigned i,records;int32_t requested;uint32_t epoch;
 Begin(0,AUTO,1);records=record_count;
 Current=T_BASE;Detail[T_BASE]=BASE;KeyEvent(K_INDEX_DEC);
 CHECK(_T_base==20 && hw_waiting && !fifo_index && record_count==records+2);
 records=record_count;KeyEvent(K_INDEX_DEC);
 CHECK(_T_base==19 && hw_pre==256 && record_count==records+2);
 hw_ready=1;KeyEvent(0);CHECK(hw_waiting==2 && fifo_index==513);
 records=record_count;hw_ready=0;KeyEvent(KEY3);
 CHECK(hw_waiting==1 && !shown_valid && record_count==records+2 && MAX_X-MIN_X==300);
 records=record_count;KeyEvent(KEY3);
 CHECK(hw_waiting && record_count==records && MAX_X-MIN_X==300);
 KeyEvent(KEY3);CHECK(hw_waiting && record_count==records+2 && MAX_X-MIN_X==386);
 hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);CHECK(shown_valid);
 /* Keep an out-of-range request intact across changes and a new capture. */
 KeyEvent(KEY1);CHECK(_State.Value==HOLD);Current=T_BASE;Detail[T_BASE]=XPOSI;
 SetLowVolume=1;Key_Mid_Speed=0;for(i=0;i<100;++i)KeyEvent(K_INDEX_DEC);
 CHECK(!last_view.window_start_q16 && Wildcat144_XposTime(&requested));
 epoch=last_frame->capture.epoch;Detail[T_BASE]=BASE;
 for(i=0;i<3;++i)KeyEvent(K_INDEX_DEC);
 CHECK(last_frame->capture.epoch==epoch && !last_view.window_start_q16);
 {int32_t value;CHECK(Wildcat144_XposTime(&value) && value==requested);}
 hw_ready=0;KeyEvent(KEY1);CHECK(hw_waiting);fake_cycles+=101u*72000u;KeyEvent(0);
 {int32_t value;CHECK(Wildcat144_XposTime(&value) && value==requested);}
 CHECK(shown_valid && last_frame->capture.trigger_kind==CAPTURE_TRIGGER_NONE);
 /* K1 inside an AUTO transfer consumes exactly one press; no fallback. */
 hold_on_read=1;hw_ready=1;KeyEvent(0);CHECK(_State.Value==RUN && hw_waiting==2 && !shown_valid);
 for(i=0;i<7;++i)KeyEvent(0);CHECK(_State.Value==HOLD && shown_valid && !hw_waiting);
 records=record_count;KeyEvent(0);CHECK(_State.Value==HOLD && record_count==records);
 key_status|=KEY1_STATUS;KeyEvent(KEY1);CHECK(_State.Value==RUN && hw_waiting);
 records=record_count;fake_cycles+=1000u*72000u;KeyEvent(0);
 CHECK(_State.Value==HOLD && !shown_valid && !hw_waiting && record_count==records+1);
 Current=TRACK1;KeyEvent(K_ITEM_S);SetLowVolume=Key_Mid_Speed=0;
 /* Configure IO failure is not a no-event timeout and cannot read fallback. */
 Begin(0,AUTO,1);live_implementation=0x0103;
 Current=TRACK1;Detail[TRACK1]=RANGE;KeyEvent(K_INDEX_INC);
 CHECK(_State.Value==HOLD && !shown_valid && !hw_waiting && !fifo_index);
 live_implementation=0x0102;KeyEvent(K_ITEM_S);
}

static void TimebaseK4(void)
{
 unsigned mode,base,panel,held,i,g,records,reads;uint32_t epoch;int32_t before,after;
 for(mode=0;mode<3;++mode)for(base=0;base<3;++base)for(panel=0;panel<3;++panel)for(held=0;held<2;++held){
  BeginAt(base,mode==0?AUTO:mode==1?NORH:SGL,1,0,0,panel);
  if(held){hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);if(_State.Value!=HOLD)KeyEvent(KEY1);}
  Current=T_BASE;Detail[T_BASE]=MODE;KeyEvent(0);
  records=record_count;reads=fifo_index;epoch=last_frame?last_frame->capture.epoch:0;
  CHECK(Wildcat144_XposTime(&before));
  for(i=0;i<18;++i){
   unsigned want=(i+1)%3;
   KeyEvent(KEY4);
   CHECK(Current==T_BASE && Detail[T_BASE]==want);
   CHECK(Scope144_Available(T_BASE,want,_Kind) && Scope144_Field(T_BASE,want)!=34);
   if(want==MODE)CHECK(Title[T_BASE][want].Flag&BLINK);
  }
  CHECK(Detail[T_BASE]==MODE && record_count==records && fifo_index==reads);
  CHECK(Wildcat144_XposTime(&after) && before==after);
  CHECK(!last_frame || last_frame->capture.epoch==epoch);
  for(g=0;g<3;++g)CHECK(Title[T_BASE][g].Value==(s16)(g==MODE?mode==0?AUTO:mode==1?NORH:SGL:g==BASE?21-base:0));
  Current=TRACK1;KeyEvent(K_ITEM_S);
 }
}
/* Same physical direction as Normal: right is shorter, left is longer. */
static void TimebaseStops(void)
{
 unsigned mode,base,panel,held,i,records,reads;uint32_t epoch,hash;
 for(mode=0;mode<3;++mode)for(base=0;base<3;++base)for(panel=0;panel<3;++panel)for(held=0;held<2;++held){
  BeginAt(base,mode==0?AUTO:mode==1?NORH:SGL,1,0,0,panel);
  if(held){hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);if(_State.Value!=HOLD)KeyEvent(KEY1);}
  Current=T_BASE;Detail[T_BASE]=BASE;
  for(i=0;i<4;++i)KeyEvent(K_INDEX_INC);
  CHECK(_T_base==21);records=record_count;reads=fifo_index;epoch=last_frame?last_frame->capture.epoch:0;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  for(i=0;i<50;++i){KeyEvent(K_INDEX_INC);CHECK(_T_base==21);}
  CHECK(record_count==records && fifo_index==reads && (!last_frame || last_frame->capture.epoch==epoch));
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  KeyEvent(K_INDEX_DEC);CHECK(_T_base==20);KeyEvent(K_INDEX_DEC);CHECK(_T_base==19);
  records=record_count;reads=fifo_index;epoch=last_frame?last_frame->capture.epoch:0;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  for(i=0;i<50;++i){KeyEvent(K_INDEX_DEC);CHECK(_T_base==19);}
  CHECK(record_count==records && fifo_index==reads && (!last_frame || last_frame->capture.epoch==epoch));
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  KeyEvent(K_INDEX_INC);CHECK(_T_base==20);KeyEvent(K_INDEX_INC);CHECK(_T_base==21);
  Current=TRACK1;KeyEvent(K_ITEM_S);
 }
}
static void PanelDefaults(void)
{
 unsigned digital,page,i,k,n,records,reads;uint32_t epoch,hash;u8 items[9],tracks[9];
 for(digital=0;digital<2;++digital){
  BeginAt(0,SGL,1,0,0,1);hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);CHECK(_State.Value==HOLD);
  _3_source=_4_source=(s16)digital;UpdateMeter=4;MeterDefaults();
  for(i=0;i<9;++i){items[i]=Meter[i].Item;tracks[i]=Meter[i].Track;}
  CHECK(Meter[3].Item==(digital?FRQ:VPP));
  records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  for(k=0;k<30;++k){
   KeyEvent(KEY3);CHECK(FlagMeter==2 && Meter[2].Item==VDC && Meter[3].Item==VDC);
   KeyEvent(KEY3);CHECK(!FlagMeter);KeyEvent(KEY3);CHECK(FlagMeter==1);
   n=0;for(i=0;i<9;++i){CHECK(Meter[i].Item==items[i] && Meter[i].Track==tracks[i]);n+=Meter[i].Item==VDC;}
   CHECK(n==2 && Meter[3].Item==(digital?FRQ:VPP));
  }
  /* Existing valid custom single-channel pages never become defaults. */
  for(page=0;page<4;++page){
   UpdateMeter=(u8)page;
   for(i=0;i<9;++i){Meter[i].Item=items[i]=(u8)(i%2?VPP:FRQ);Meter[i].Track=tracks[i]=(u8)page;}
   for(k=0;k<3;++k){KeyEvent(KEY3);for(i=0;i<9;++i)CHECK(Meter[i].Item==items[i] && Meter[i].Track==tracks[i]);}
  }
  CHECK(record_count==records && fifo_index==reads && last_frame->capture.epoch==epoch);
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  Current=TRACK1;KeyEvent(K_ITEM_S);
 }
 UpdateMeter=4;
}
/* A K3 full-panel redraw must survive subsequent frame invalidation,
 * partial FIFO reads and K1 cancellation in the same service iteration. */
static void PanelRedraw(void)
{
 unsigned mode,panel,path,i,clears;int light,volume;
 for(mode=0;mode<3;++mode)for(panel=0;panel<3;++panel)for(path=0;path<4;++path){
  UpdateMeter=4;BeginAt(0,mode==0?AUTO:mode==1?NORH:SGL,1,0,0,panel);
  hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);CHECK(shown_valid);
  if(path==0){if(_State.Value!=HOLD)KeyEvent(KEY1);CHECK(_State.Value==HOLD);}
  else {
   hw_ready=0;if(_State.Value==HOLD)KeyEvent(KEY1);else KeyEvent(0);
   CHECK(hw_waiting==1 && shown_valid);
   if(path==2){hw_ready=1;KeyEvent(0);CHECK(hw_waiting==2 && fifo_index==513);}
   if(path==3){hw_ready=1;hold_on_read=1;}
  }
  light=Title[BK_LIGHT][1].Value;volume=Title[VOLUME][1].Value;clears=panel_clears;
  KeyEvent(KEY3);
  CHECK(FlagMeter==(panel+1)%3 && panel_clears==clears+1);
  CHECK(last_refresh==2);
  CHECK(Title[BK_LIGHT][1].Value==light && Title[VOLUME][1].Value==volume);
  if(path==3){CHECK(!hold_on_read && _State.Value==RUN && hw_waiting==2);
   for(i=0;i<7;++i)KeyEvent(0);CHECK(_State.Value==HOLD && shown_valid && !hw_waiting);}
  key_status=0xffff;hold_on_read=0;Current=TRACK1;KeyEvent(K_ITEM_S);
 }
}
int main(int argc,char **argv)
{
 Init();if(argc<2 || !strcmp(argv[1],"redraw"))PanelRedraw();
 if(argc<2 || !strcmp(argv[1],"stops"))TimebaseStops();
 if(argc<2 || !strcmp(argv[1],"panels"))PanelDefaults();
 if(argc<2 || !strcmp(argv[1],"k4"))TimebaseK4();
 if(argc<2 || !strcmp(argv[1],"epochs"))RuntimeEpochs();
 if(argc<2 || !strcmp(argv[1],"faults"))RearmAndFaults();
 if(argc<2 || !strcmp(argv[1],"positions"))Positions();
 if(argc<2 || !strcmp(argv[1],"auto"))AutoEdge();
 if(argc<2 || !strcmp(argv[1],"requested"))Requested();
 printf("PASS HW AUTO/XPOS: %u checks; host only\n",checks);return 0;
}
