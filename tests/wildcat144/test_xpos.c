/* Real Normal XPOS branches and 144 service; mock BIOS only. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
#include <limits.h>
u16 XposRef;
static u16 compensation;
u16 KpCompensation(void){return compensation;}
int Test_NormalXpos(int direction);
static unsigned LegacyNormal(int direction,u16 *ref,s16 *value,s16 limit)
{
    if(direction<0){
        if(*ref>150){if(SetLowVolume)*value-=Key_Mid_Speed?6:30;else --*value;}
        else if(*value>0){
            if(SetLowVolume && *ref>30){*ref-=4;return 1;}
            --*value;
        }
    }else if(*ref<151){
        if(SetLowVolume){if(*ref<30)++*value;else{*ref+=4;return 1;}}
        else if(*value<limit)++*value;
    }else{
        if(SetLowVolume){
            if(*value<limit-30)*value+=Key_Mid_Speed?6:30;
            else if(*value<limit-6)*value+=6;
            else if(*value<limit)++*value;
        }else if(*value<limit)++*value;
    }
    *ref=GetXposRef((u16)*value);return 0;
}
static void NormalCompatibility(void)
{
    static const unsigned kp[]={246,492,614,819,983,1024};
    unsigned k,stage,ref,dir;
    Current=T_BASE;Detail[T_BASE]=XPOSI;_T_base=21;_X_posi.Limit=4095;
    for(k=0;k<sizeof kp/sizeof kp[0];++k){
        compensation=(u16)(kp[k]==246?24576:kp[k]==492?4915:kp[k]==614?6144:kp[k]==819?8192:kp[k]==983?9830:1024);xattributes[21].KP=(u16)kp[k];
        for(stage=0;stage<3;++stage)for(ref=0;ref<20000;++ref)for(dir=0;dir<2;++dir){
            u16 oldref=(u16)ref;s16 oldvalue=(s16)GetXpos((u16)ref);unsigned bypass;
            if(oldvalue>4095 || (ref>150 && oldvalue<30))continue;
            SetLowVolume=(u8)(stage>0);Key_Mid_Speed=(u8)(stage==1);
            XposRef=oldref;_X_posi.Value=oldvalue;
            bypass=LegacyNormal(dir?1:-1,&oldref,&oldvalue,_X_posi.Limit);
            CHECK((unsigned)Test_NormalXpos(dir?1:-1)==bypass);
            CHECK(XposRef==oldref && _X_posi.Value==oldvalue);
        }
    }
    SetLowVolume=Key_Mid_Speed=0;
}
static void SharedStops(void)
{
 unsigned k,stage,i;static const unsigned kp[]={246,492,614,819,983,1024};
 int64_t units[]={1,65536,157286};
 for(k=0;k<3;++k){int64_t q=units[k],low=-500*q,high=700*q,p=high;
  for(i=0;i<100;++i){p=UiCommon_XposBound(p,30*q,low,high);CHECK(p==high);}
  CHECK(UiCommon_XposBound(p,-q,low,high)==high-q);
  p=low;for(i=0;i<100;++i){p=UiCommon_XposBound(p,-30*q,low,high);CHECK(p==low);}
  CHECK(UiCommon_XposBound(p,q,low,high)==low+q);
  CHECK(UiCommon_XposBound(high+500*q,-q,low,high)==high-q);
  CHECK(UiCommon_XposBound(low-500*q,q,low,high)==low+q);
 }
 CHECK(UiCommon_XposBound(INT64_MAX,1,INT64_MIN,INT64_MAX)==INT64_MAX);
 CHECK(UiCommon_XposBound(INT64_MIN,-1,INT64_MIN,INT64_MAX)==INT64_MIN);
 CHECK(UiCommon_XposBound(0,INT64_MIN,-10,20)==-10);
 CHECK(UiCommon_XposBound(0,INT64_MAX,-10,20)==20);
 CHECK(UiCommon_XposBound(5,1,5,5)==5);
 CHECK(UiCommon_XposBound(7,1,10,0)==7);
 Current=T_BASE;Detail[T_BASE]=XPOSI;_T_base=21;_X_posi.Limit=4095;
 for(k=0;k<6;++k)for(stage=0;stage<3;++stage){
  compensation=(u16)(kp[k]==246?24576:kp[k]==492?4915:kp[k]==614?6144:kp[k]==819?8192:kp[k]==983?9830:1024);
  xattributes[21].KP=(u16)kp[k];SetLowVolume=(u8)(stage>0);Key_Mid_Speed=(u8)(stage==1);
  _X_posi.Value=_X_posi.Limit;XposRef=GetXposRef((u16)_X_posi.Value);
  for(i=0;i<100;++i){CHECK(!Test_NormalXpos(1));CHECK(_X_posi.Value==_X_posi.Limit);}
  CHECK(!Test_NormalXpos(-1) && _X_posi.Value<_X_posi.Limit);
  _X_posi.Value=0;XposRef=GetXposRef(0);
  for(i=0;i<100;++i){CHECK(!Test_NormalXpos(-1));CHECK(!_X_posi.Value);}
  CHECK(!Test_NormalXpos(1) && _X_posi.Value>0);
 }
 SetLowVolume=Key_Mid_Speed=0;
}
static void Geometry(void)
{
    static const unsigned slots[]={295,293,306,304};
    static const int shifts[]={-32*65536,-1,0,12*65536,32*65536};
    Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,0,1024,0,1024,1024,54,1024,1024,0,0};
    Frame144 f;ViewWindowGeometry v,before;unsigned w,base,t,sh,phase,col;
    Frame144_Reset(&f,1);f.words=(const uint32_t *)DataBuf;f.calibration=cal;
    f.capture.fifo_words=4096;f.capture.valid_begin=2;f.capture.valid_words=4094;
    f.capture.time_samples=8188;f.capture.status=CAPTURE_WINDOW_READY;
    f.capture.sample_period_ps_q16=(1000000000000ULL*65536+72000000)/144000000;
    f.capture.flags=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
    f.capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;
    for(w=300;w<=386;w+=86)for(base=0;base<3;++base)for(t=0;t<4;++t)
    for(sh=0;sh<5;++sh)for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase){
        uint32_t step=Scope144_Step(base),trigger=slots[t]*65536u;
        int64_t requested=(int64_t)trigger-150LL*step;
        f.capture.trigger_q16=trigger;f.digital_shift_q16=shifts[sh];f.capture.sample_phase=(u8)phase;
        CHECK(Frame144_Window(&f,w,base,0,&v));CHECK(Frame144_Valid(&f));
        CHECK(v.window_start_q16==(uint32_t)(requested<0?0:requested));
        CHECK(v.window_end_q16<=8187u*65536u && v.visible_columns==w);
        col=(trigger-v.window_start_q16+step/2u)/step;
        CHECK(base==2?col<150:col==150);
        CHECK(f.capture.trigger_q16==trigger && f.digital_shift_q16==shifts[sh]);
        CHECK(Frame144_Window(&f,w,base,INT64_MIN,&v) && !v.window_start_q16);
        before=v;CHECK(Frame144_Window(&f,w,base,INT64_MIN,&v) && !memcmp(&v,&before,sizeof v));
        CHECK(Frame144_Window(&f,w,base,INT64_MAX,&v) && v.window_end_q16==8187u*65536u);
        before=v;CHECK(Frame144_Window(&f,w,base,INT64_MAX,&v) && !memcmp(&v,&before,sizeof v));
        CHECK(Frame144_Window(&f,w,base,-1,&v));
        CHECK(v.window_start_q16==(uint32_t)(requested>0?requested-1:0));
        CHECK(Frame144_Window(&f,w,base,1,&v));
        CHECK(v.window_start_q16==(uint32_t)(requested+1>0?requested+1:0));
    }
    f.capture.flags=0;f.capture.trigger_kind=CAPTURE_TRIGGER_NONE;f.capture.trigger_q16=0;
    CHECK(Frame144_Window(&f,386,0,0,&v) && v.window_start_q16==295u*65536u-150u*Scope144_Step(0));
    CHECK(Frame144_Window(&f,386,0,1,&v) && v.window_start_q16==295u*65536u-150u*Scope144_Step(0)+1);
    CHECK(!Frame144_Window(&f,0,0,0,&v));CHECK(!Frame144_Window(&f,386,3,0,&v));
    CHECK(!Frame144_Window(0,386,0,0,&v));CHECK(!Frame144_Window(&f,386,0,0,0));
    f.capture.flags=CAPTURE_WINDOW_HAS_TRIGGER;f.capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;
    for(t=0;t<3;++t){
        f.capture.trigger_q16=t==0?0:t==1?1:8187u*65536u;
        CHECK(Frame144_Window(&f,386,2,0,&v));CHECK(v.window_end_q16<=8187u*65536u);
        CHECK(Frame144_Window(&f,10,0,0,&v));
    }
    f.capture.status=CAPTURE_WINDOW_EMPTY;CHECK(!Frame144_Window(&f,386,0,0,&v));
    puts("144 requested trigger columns (rounded): base(ns/div) A-edge A-pulse C/D-edge C/D-pulse; widths 300/386");
    for(base=0;base<3;++base){
        printf("%u",base==0?100u:base==1?200u:500u);
        for(t=0;t<4;++t){col=(slots[t]*65536u+Scope144_Step(base)/2)/Scope144_Step(base);printf(" %u",col>150?150:col);}
        puts("");
    }
}
static int64_t ExpectedDelta(int64_t offset,unsigned base,unsigned phase,int direction,int64_t maximum)
{
    uint32_t step=Scope144_Step(base);int64_t quantum=(base==2?4LL:2LL)*65536;
    int ref=(int)(offset>=0?(offset+step/2)/step:-((-offset+step/2)/step));
    int amount=1;
    if(phase && ref>150)amount=phase==1?6:30;
    else if(phase && (direction<0?ref>30:ref>=30))return direction*4LL*step;
    if(direction>0 && amount>1 && maximum-offset<=30*quantum)
        amount=maximum-offset>6*quantum?6:1;
    return direction*amount*quantum;
}
static void Runtime(void)
{
    unsigned base,panel,phase,direction,i,records,reads;
    uint32_t hash,epoch;ViewWindowGeometry origin,expected;int64_t origin_q16,requested;
    for(base=0;base<3;++base)for(panel=0;panel<2;++panel){
        hw_test=1;hw_ready=0;history=0;capture_case=0;key_status=0xffff;
        _Kind=6;_Mode=SGL;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;
        _T_base=(s16)(21-base);_A_Range=0;_1_source=1;_3_source=_4_source=1;
        FlagMeter=(u8)panel;Current=TRACK1;Detail[TRACK1]=SOURCE;_X_posi.Limit=3703;
        Title[TRACK1][COUPLE].Value=SCOPE144_DC;
        CHECK(Wildcat144_Service() && hw_waiting);hw_ready=1;
        for(i=0;i<8;++i)KeyEvent(0);
        CHECK(_State.Value==HOLD && Frame144_Valid(last_frame));
        CHECK(Frame144_Window(last_frame,MAX_X-MIN_X,base,0,&origin));
        CHECK(last_view.window_start_q16==origin.window_start_q16);
        CHECK(Frame144_WindowOrigin(last_frame,MAX_X-MIN_X,base,&origin_q16));
        requested=0;records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;
        hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
        Current=T_BASE;Detail[T_BASE]=XPOSI;
        for(phase=0;phase<3;++phase)for(direction=0;direction<2;++direction){
            SetLowVolume=(u8)(phase>0);Key_Mid_Speed=(u8)(phase==1);
            for(i=0;i<(phase==2?1000u:400u);++i){
                int dir=direction?-1:1;unsigned before_draws=draws;
                int64_t minimum=-origin_q16;
                int64_t maximum=(int64_t)(last_frame->capture.time_samples-1u)*65536-
                    (int64_t)(MAX_X-MIN_X-1)*Scope144_Step(base)-origin_q16;
                int64_t offset=requested,next,start;
                if(offset<minimum)offset=minimum;
                if(offset>maximum)offset=maximum;
                next=offset+ExpectedDelta(offset,base,phase,dir,maximum);
                if(next>maximum)next=maximum;
                if(next<minimum)next=minimum;
                requested=next;start=origin_q16+next;
                CHECK(ViewWindow_Build(last_frame->capture.time_samples,MAX_X-MIN_X,Scope144_Step(base),start,&expected));
                KeyEvent(dir>0?K_INDEX_INC:K_INDEX_DEC);
                CHECK(last_view.window_start_q16==expected.window_start_q16 && draws<=before_draws+1);
                CHECK(_X_posi.Value==(s16)(requested/65536));
                CHECK(last_frame->capture.epoch==epoch && record_count==records && fifo_index==reads);
            }
        }
        /* Base and panel changes retain the requested physical displacement, clamped only
         * by the common window builder. No acquisition is allowed in HOLD. */
        SetLowVolume=Key_Mid_Speed=0;
        {unsigned original_base=base;
        for(i=0;i<6;++i){
            int64_t displacement=requested;
            unsigned target=i<3?i:5-i;
            Detail[T_BASE]=BASE;
            while((unsigned)(21-_T_base)!=target)KeyEvent((unsigned)(21-_T_base)<target?K_INDEX_DEC:K_INDEX_INC);
            base=(unsigned)(21-_T_base);
            CHECK(Frame144_Window(last_frame,MAX_X-MIN_X,base,displacement,&expected));
            CHECK(last_view.window_start_q16==expected.window_start_q16);
            CHECK(Frame144_Window(last_frame,MAX_X-MIN_X,base,0,&origin));
            CHECK(Frame144_WindowOrigin(last_frame,MAX_X-MIN_X,base,&origin_q16));
        }
        Detail[T_BASE]=BASE;
        while((unsigned)(21-_T_base)!=original_base)KeyEvent((unsigned)(21-_T_base)<original_base?K_INDEX_DEC:K_INDEX_INC);
        base=original_base;}
        for(i=0;i<3;++i){
            int64_t displacement=requested;
            KeyEvent(KEY3);
            CHECK(Frame144_Window(last_frame,MAX_X-MIN_X,base,displacement,&expected));
            CHECK(last_view.window_start_q16==expected.window_start_q16);
            CHECK(Frame144_Window(last_frame,MAX_X-MIN_X,base,0,&origin));
            CHECK(Frame144_WindowOrigin(last_frame,MAX_X-MIN_X,base,&origin_q16));
        }
        CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash && record_count==records && fifo_index==reads);
        /* Real K1 still toggles acquisition, independent of XPOS repeat flags. */
        KeyEvent(KEY1);CHECK(_State.Value==RUN && hw_waiting);KeyEvent(KEY1);
        for(i=0;i<8 && hw_waiting;++i)KeyEvent(0);
        CHECK(_State.Value==HOLD && !hw_waiting);
        Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
    }
}

/* Every physically retained sample remains browsable; the native menu's
 * 3703-word limit cannot truncate the faster 144 MHz record. */
static void WholeHistory(void)
{
 unsigned base,panel,fallback,i,phase,records,reads;uint32_t hash,epoch;int32_t a,b,before,after;
 for(base=0;base<3;++base)for(panel=0;panel<2;++panel)for(fallback=0;fallback<2;++fallback){
  hw_test=1;hw_ready=0;history=capture_case=0;key_status=0xffff;SetLowVolume=Key_Mid_Speed=0;
  _Kind=6;_Mode=fallback?AUTO:SGL;_State.Value=RUN;_Tr_source=(s16)(base?base+1:TRACK1);_Tr_kind=base==2?4:1;
  _T_base=(s16)(21-base);_A_Range=0;_1_source=_3_source=_4_source=1;_X_posi.Limit=3703;
  FlagMeter=(u8)panel;Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
  CHECK(Wildcat144_Service() && hw_waiting);
  if(fallback){
   fake_cycles+=101u*72000u;KeyEvent(0);
   CHECK(shown_valid && last_frame->capture.time_samples==7888);KeyEvent(KEY1);
  }else{hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);}
  CHECK(_State.Value==HOLD && Frame144_Valid(last_frame));
  records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;
  hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);Current=T_BASE;Detail[T_BASE]=XPOSI;
  for(i=0;i<4300;++i)KeyEvent(K_INDEX_INC);
  CHECK(last_view.window_end_q16==(last_frame->capture.time_samples-1u)*65536u);
  CHECK(Frame144_At(last_frame,&last_view,last_view.visible_columns-1,&a));
  CHECK(Frame144_Read(last_frame,last_frame->capture.time_samples-1,&b,0) && a==b);
  CHECK(Wildcat144_XposTime(&before));
  for(i=0;i<10;++i)KeyEvent(0);
  CHECK(Wildcat144_XposTime(&after) && before==after);
  for(phase=0;phase<3;++phase){
   uint32_t at_edge=last_view.window_start_q16;
   SetLowVolume=(u8)(phase>0);Key_Mid_Speed=(u8)(phase==1);
   CHECK(Wildcat144_XposTime(&before));
   for(i=0;i<100;++i){KeyEvent(K_INDEX_INC);CHECK(last_view.window_start_q16==at_edge);}
   CHECK(Wildcat144_XposTime(&after) && after==before);
   KeyEvent(K_INDEX_DEC);CHECK(last_view.window_start_q16<at_edge);
   for(i=0;i<100;++i)KeyEvent(K_INDEX_INC);
   CHECK(last_view.window_start_q16==at_edge);
  }
  SetLowVolume=Key_Mid_Speed=0;
  /* A wider window or slower base can clip a retained request. The first
   * user edit must rebase it before applying its movement. */
  while(FlagMeter!=1)KeyEvent(KEY3);
  for(i=0;i<4300;++i)KeyEvent(K_INDEX_INC);
  while(FlagMeter!=0)KeyEvent(KEY3);
  {uint32_t clipped=last_view.window_start_q16;
   KeyEvent(K_INDEX_DEC);CHECK(last_view.window_start_q16<clipped);}
  Detail[T_BASE]=BASE;while(_T_base!=21)KeyEvent(K_INDEX_INC);
  Detail[T_BASE]=XPOSI;for(i=0;i<4300;++i)KeyEvent(K_INDEX_INC);
  Detail[T_BASE]=BASE;KeyEvent(K_INDEX_DEC);CHECK(_T_base==20);
  Detail[T_BASE]=XPOSI;
  {uint32_t clipped=last_view.window_start_q16;
   KeyEvent(K_INDEX_DEC);CHECK(last_view.window_start_q16<clipped);}
  for(i=0;i<4600;++i)KeyEvent(K_INDEX_DEC);
  CHECK(!last_view.window_start_q16);
  for(phase=0;phase<3;++phase){
   SetLowVolume=(u8)(phase>0);Key_Mid_Speed=(u8)(phase==1);
   CHECK(Wildcat144_XposTime(&before));
   for(i=0;i<100;++i){KeyEvent(K_INDEX_DEC);CHECK(!last_view.window_start_q16);}
   CHECK(Wildcat144_XposTime(&after) && after==before);
   KeyEvent(K_INDEX_INC);CHECK(last_view.window_start_q16>0);
   KeyEvent(K_INDEX_DEC);CHECK(!last_view.window_start_q16);
  }
  SetLowVolume=Key_Mid_Speed=0;
  CHECK(Frame144_At(last_frame,&last_view,0,&a) && Frame144_Read(last_frame,0,&b,0) && a==b);
  CHECK(record_count==records && fifo_index==reads && last_frame->capture.epoch==epoch);
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
 }
}
static void PendingStops(void)
{
 unsigned base,panel,i;int32_t before,after;uint32_t hash;
 for(base=0;base<3;++base)for(panel=0;panel<3;++panel){
  hw_test=hw_ready=1;history=capture_case=0;key_status=0xffff;
  SetLowVolume=1;Key_Mid_Speed=0;_Kind=6;_Mode=NORH;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;
  _T_base=(s16)(21-base);_A_Range=0;_1_source=_3_source=_4_source=1;
  FlagMeter=(u8)panel;Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
  CHECK(Wildcat144_Service());for(i=0;i<8 && hw_waiting;++i)KeyEvent(0);
  CHECK(shown_valid && _State.Value==RUN);hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  hw_ready=0;KeyEvent(0);CHECK(hw_waiting && shown_valid);
  Current=T_BASE;Detail[T_BASE]=XPOSI;
  for(i=0;i<500;++i)KeyEvent(K_INDEX_INC);
  CHECK(Wildcat144_XposTime(&before));for(i=0;i<100;++i)KeyEvent(K_INDEX_INC);
  CHECK(Wildcat144_XposTime(&after) && after==before);
  KeyEvent(K_INDEX_DEC);CHECK(Wildcat144_XposTime(&after) && after<before);
  for(i=0;i<500;++i)KeyEvent(K_INDEX_DEC);
  CHECK(Wildcat144_XposTime(&before));for(i=0;i<100;++i)KeyEvent(K_INDEX_DEC);
  CHECK(Wildcat144_XposTime(&after) && after==before);
  KeyEvent(K_INDEX_INC);CHECK(Wildcat144_XposTime(&after) && after>before);
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  hw_ready=1;for(i=0;i<9 && hw_waiting;++i)KeyEvent(0);
  CHECK(shown_valid && last_view.window_start_q16>0);KeyEvent(KEY1);CHECK(_State.Value==HOLD);
  Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
 }
 SetLowVolume=Key_Mid_Speed=0;hw_test=0;
}
int main(int argc,char **argv)
{
    unsigned i,reference;
    CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
    StartupImageCheck();xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
    for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);Wildcat144_CheckImage();
    CHECK(!UiCommon_XposStep(100,0,1,1,&reference) && !reference);
    if(argc<2 || !strcmp(argv[1],"full"))WholeHistory();
    if(argc<2){NormalCompatibility();SharedStops();Geometry();Runtime();PendingStops();}
    printf("PASS XPOS: %u checks; real Normal compatibility, Q16 bounds, all bases/widths, repeat phases/reversal, K1/HOLD, no FIFO changes; mock HW\n",checks);
    return 0;
}
