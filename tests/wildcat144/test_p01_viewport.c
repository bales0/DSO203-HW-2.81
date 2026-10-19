/* P0 regression cases, first run against the unmodified production code. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
#include "NativeWindow.h"
static void Init(void)
{
 unsigned i;CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 StartupImageCheck();WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);Wildcat144_CheckImage();
 xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
 for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
 xattributes[19].ARR=1;xattributes[19].KP=614;xattributes[20].KP=492;xattributes[21].KP=246;
}
static void Begin(unsigned mode)
{
 unsigned i;hw_test=1;hw_ready=0;history=capture_case=0;key_status=0xffff;
 _Kind=6;_Mode=(s16)mode;_State.Value=RUN;_Tr_source=TRACK1;_Tr_kind=1;
 _T_base=21;_A_Range=0;_1_source=_3_source=_4_source=1;FlagMeter=0;Current=TRACK1;
 Title[TRACK1][COUPLE].Value=SCOPE144_DC;_X_posi.Limit=3703;
 CHECK(Wildcat144_Service() && hw_waiting);hw_ready=1;
 for(i=0;i<8;++i)KeyEvent(0);
 CHECK(Frame144_Valid(last_frame));
}
static void Sparse(void)
{
 NativeWindowInput in={4096,392,1024,300,0,0,0,0,0,0};unsigned i,bad;ViewWindowGeometry v;
 for(bad=0;bad<=150;bad+=150){
  NativeWindow_Reset();for(i=0;i<4096;++i)NativeWindow_Written(i,0,i!=bad);
  NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v));
  CHECK(v.capture_count==4096 && v.window_start_q16==300u*65536u);
 }
 NativeWindow_Reset();for(i=0;i<4096;++i)NativeWindow_Written(i,0,i!=450);
 NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
}
static void Reference(void)
{
 Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024,0,0};
 Frame144 f;ViewWindowGeometry triggered,plain;unsigned base;
 Frame144_Reset(&f,1);f.words=(const uint32_t *)DataBuf;f.calibration=cal;
 f.capture.fifo_words=4096;f.capture.valid_begin=2;f.capture.valid_words=4094;f.capture.time_samples=8188;
 f.capture.status=CAPTURE_WINDOW_READY;f.capture.sample_phase=CAPTURE_PHASE_B_A;
 f.capture.sample_period_ps_q16=1000000000000ULL*65536/144000000;
 for(base=0;base<3;++base){
  f.capture.flags=CAPTURE_WINDOW_HAS_TRIGGER;f.capture.trigger_kind=CAPTURE_TRIGGER_SOFTWARE;f.capture.trigger_q16=295u*65536u;
  CHECK(Frame144_Window(&f,386,base,0,&triggered));
  f.capture.flags=0;f.capture.trigger_kind=CAPTURE_TRIGGER_NONE;f.capture.trigger_q16=0;
  CHECK(Frame144_Window(&f,386,base,0,&plain));
  CHECK(plain.window_start_q16==triggered.window_start_q16);
  CHECK(!f.capture.flags && f.capture.trigger_kind==CAPTURE_TRIGGER_NONE && !f.capture.trigger_q16);
 }
}
static void Repeat(void)
{
 uint32_t before;Begin(SGL);CHECK(_State.Value==HOLD);Current=T_BASE;Detail[T_BASE]=XPOSI;
 before=last_view.window_start_q16;KeyEvent(K_INDEX_INC);
 /* One native Normal word = one 72MHz timer period = two 144 samples. */
 CHECK(last_view.window_start_q16-before==2u*65536u);
 Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
}
static void Pending(void)
{
 unsigned first,last,a,b;Begin(NORH);CHECK(Wildcat144_Overview(&first,&last));
 hw_ready=0;KeyEvent(0);CHECK(hw_waiting);
 CHECK(Wildcat144_Overview(&a,&b) && a==first && b==last);
 Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
}

static void MakeFrame(Frame144 *f)
{
 Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,1024,0,1024,1024,54,1024,1024,0,0};
 Frame144_Reset(f,1);f->words=(const uint32_t *)DataBuf;f->calibration=cal;
 f->capture.fifo_words=4096;f->capture.valid_begin=2;f->capture.valid_words=4094;
 f->capture.time_samples=8188;f->capture.status=CAPTURE_WINDOW_READY;
 f->capture.sample_phase=CAPTURE_PHASE_B_A;
 f->capture.sample_period_ps_q16=1000000000000ULL*65536/144000000;
}
static void SoftwareAnchors(void)
{
 Frame144 f;ViewWindowGeometry view;unsigned base,width,ch,fall,i,expected,pre,edge;
 for(base=0;base<3;++base)for(width=300;width<=386;width+=86)for(fall=0;fall<2;++fall){
  MakeFrame(&f);pre=150u*Scope144_Step(base);
  for(i=0;i<4094;++i){unsigned b=((2*i)/50)&1,a=((2*i+1)/50)&1;
   DataBuf[i+2]=((b?80:40)<<8)|(a?80:40)|((((i/25)&1)?3u:0u)<<16);}
  expected=0;
  for(i=1;i<8188;++i)if(((i/50)&1)!=(unsigned)(((i-1)/50)&1) &&
      ((i/50)&1)==(unsigned)!fall && (uint64_t)i*65536>=pre){expected=i;break;}
  CHECK(Frame144_Trigger(&f,width,base,0,fall,&view));
  CHECK(f.capture.trigger_q16==expected*65536u);
  CHECK(view.window_start_q16==expected*65536u-pre);
  CHECK(f.capture.trigger_kind==CAPTURE_TRIGGER_SOFTWARE && (f.capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  for(ch=0;ch<2;++ch){
   expected=0;
   for(i=1;i<4094;++i)if(((i/25)&1)!=(((i-1)/25)&1) && ((i/25)&1)==(unsigned)!fall &&
       (int64_t)i*131072+f.digital_shift_q16>=pre){expected=i;break;}
   CHECK(Frame144_DigitalTrigger(&f,width,base,ch,fall,&view));
   CHECK(f.capture.trigger_q16==expected*131072u+f.digital_shift_q16);
   CHECK(view.window_start_q16==f.capture.trigger_q16-pre);
  }
  /* A single early real edge remains usable; the physical buffer bounds win. */
  edge=(30u*Scope144_Step(base)+65535u)/65536u+2u;
  for(i=0;i<4094;++i)DataBuf[i+2]=((2*i>=edge?80u:40u)<<8)|(2*i+1>=edge?80u:40u);
  CHECK(Frame144_Trigger(&f,width,base,0,0,&view));
  CHECK(f.capture.trigger_q16==edge*65536u && !view.window_start_q16);
  memset(DataBuf,0x37,sizeof DataBuf);
  CHECK(!Frame144_Trigger(&f,width,base,0,0,&view));
  CHECK(!(f.capture.flags&CAPTURE_WINDOW_HAS_TRIGGER) && f.capture.trigger_kind==CAPTURE_TRIGGER_NONE);
 }
}
static void AutoFallback(void)
{
 unsigned base,source,kind,width,i,a,b,c,d;uint32_t start;
 for(base=0;base<3;++base)for(source=0;source<4;++source)if(source!=1)
 for(kind=2;kind<=6;kind+=4)for(width=300;width<=386;width+=86){
  hw_test=1;hw_ready=0;history=capture_case=0;key_status=0xffff;
  _Kind=6;_Mode=AUTO;_State.Value=RUN;_Tr_source=(s16)source;_Tr_kind=(s16)kind;
  _T_base=(s16)(21-base);_A_Range=0;_1_source=_3_source=_4_source=1;FlagMeter=0;
  Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;_X_posi.Limit=3703;
  MAX_X=(u16)(MIN_X+width);
  CHECK(Wildcat144_Service() && hw_waiting);hw_ready=1;
  for(i=0;i<8;++i)KeyEvent(0);
  CHECK(Frame144_Valid(last_frame) && (last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  start=last_view.window_start_q16;CHECK(Wildcat144_Overview(&a,&b));
  hw_ready=0;KeyEvent(0);CHECK(hw_waiting);
  CHECK(Wildcat144_Overview(&c,&d) && a==c && b==d);
  fake_cycles+=101u*72000u;KeyEvent(0);
  CHECK(Frame144_Valid(last_frame) && last_frame->capture.trigger_kind==CAPTURE_TRIGGER_NONE);
  CHECK(!(last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER) && !last_frame->capture.trigger_q16);
  CHECK(last_view.window_start_q16==start);
  CHECK(Wildcat144_Overview(&c,&d) && a==c && b==d);
  hw_ready=1;for(i=0;i<8;++i)KeyEvent(0);
  CHECK(Frame144_Valid(last_frame) && (last_frame->capture.flags&CAPTURE_WINDOW_HAS_TRIGGER));
  CHECK(last_view.window_start_q16==start);
  Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
 }
}
static unsigned Random(void){static unsigned seed=0x78291u;seed=seed*1664525u+1013904223u;return seed;}
static void SparseOracle(void)
{
 unsigned char good[4096];unsigned round,i,first,last,width,format,expected;
 NativeWindowInput in;ViewWindowGeometry v;int64_t begin,end;
 for(round=0;round<400;++round){
  memset(&in,0,sizeof in);in.capacity=4096;in.points=392;
  in.format=(uint8_t)(format=round%3);in.step_q10=(uint16_t)(1+Random()%1024);
  in.origin=(int16_t)(Random()%3700);in.shift=(int16_t)(Random()%11-5);
  in.discard=(uint8_t)(Random()%6);in.rdiscard=(uint8_t)(Random()%6);
  in.digital_trigger=(uint8_t)(Random()%2);width=round%2?300:386;
  NativeWindow_Reset();memset(good,0,sizeof good);
  for(i=0;i<4096;++i){good[i]=Random()%127!=0;NativeWindow_Written(i,format,good[i]);}
  first=format?0:in.discard;last=width-1;
  if(!format && last>392u-in.rdiscard)last=392u-in.rdiscard;
  expected=(int)first+in.shift+(in.digital_trigger?5:0)>=0 &&
    (int)last+in.shift+(in.digital_trigger?5:0)<in.points;
  begin=(int64_t)in.origin*65536+((int)first+in.shift+(in.digital_trigger?5:0))*in.step_q10*64;
  end=(int64_t)in.origin*65536+((int)last+in.shift+(in.digital_trigger?5:0))*in.step_q10*64;
  if(begin<0 || end>4095LL*65536)expected=0;
  if(expected)for(i=(unsigned)(begin/65536);i<=(unsigned)((end+65535)/65536);++i)if(!good[i])expected=0;
  NativeWindow_Publish(&in);CHECK((unsigned)NativeWindow_Geometry(width,&v)==expected);
  if(expected)CHECK(v.capture_count==4096 && v.window_start_q16==(uint32_t)begin);
 }
 /* Unknown holes, repair, invalidation, and a new format are independent of prefix length. */
 memset(&in,0,sizeof in);in.capacity=4096;in.points=392;in.step_q10=1024;in.origin=300;
 NativeWindow_Reset();for(i=300;i<686;++i)NativeWindow_Written(i,0,1);
 NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v) && !NativeWindow_Count());
 NativeWindow_Written(450,0,0);NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
 NativeWindow_Written(450,0,1);NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v));
 NativeWindow_Written(450,1,1);NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
}

int main(int argc,char **argv)
{
 Init();
 if(argc<2 || !strcmp(argv[1],"sparse"))Sparse();
 if(argc<2 || !strcmp(argv[1],"reference"))Reference();
 if(argc<2 || !strcmp(argv[1],"repeat"))Repeat();
 if(argc<2 || !strcmp(argv[1],"pending"))Pending();
 if(argc<2 || !strcmp(argv[1],"software"))SoftwareAnchors();
 if(argc<2 || !strcmp(argv[1],"auto"))AutoFallback();
 if(argc<2 || !strcmp(argv[1],"oracle"))SparseOracle();
 printf("PASS P0 viewport: %u checks; sparse validity, stable reference, Normal-time repeat, last-complete overlay\n",checks);
 return 0;
}
