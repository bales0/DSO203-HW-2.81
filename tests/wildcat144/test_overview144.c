/* Complete/pending epochs in the real 144 runtime; no physical DS203. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
int main(void)
{
 unsigned base,panel,source,kind,i,first,last,records,reads;uint32_t epoch,hash;
 CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 StartupImageCheck();xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
 for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
 WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);Wildcat144_CheckImage();
 for(base=0;base<3;++base)for(panel=0;panel<2;++panel)for(source=0;source<3;++source)for(kind=0;kind<2;++kind){
  hw_test=1;hw_ready=0;history=capture_case=0;key_status=0xffff;
  _Kind=6;_Mode=NORH;_State.Value=RUN;_Tr_source=(s16)(source?source+1:TRACK1);_Tr_kind=(s16)(kind?4:1);
  _T_base=(s16)(21-base);_A_Range=0;_1_source=_3_source=_4_source=1;_T1=30;_T2=180;
  FlagMeter=(u8)panel;Current=TRACK1;Title[TRACK1][COUPLE].Value=SCOPE144_DC;
  CHECK(Wildcat144_Service() && hw_waiting);CHECK(!Wildcat144_Overview(&first,&last) && !first && !last);
  hw_ready=1;
  for(i=0;i<8;++i){KeyEvent(0);if(i<7)CHECK(!Wildcat144_Overview(&first,&last) && !first && !last);}
  CHECK(_State.Value==RUN && Wildcat144_Overview(&first,&last));CheckWindow();
  records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
  KeyEvent(KEY1);CHECK(_State.Value==HOLD && Wildcat144_Overview(&first,&last));
  /* FFT hides the outline without destroying the completed geometry.
   * This policy follows selection, even if a spectrum is not yet ready. */
  {unsigned saved_first=first,saved_last=last;
   Current=TRACK4;Detail[TRACK4]=SOURCE;
   KeyEvent(K_INDEX_INC);CHECK(_4_source==14 && !Wildcat144_Overview(&first,&last) && !first && !last);
   KeyEvent(K_INDEX_INC);CHECK(_4_source==15 && !Wildcat144_Overview(&first,&last) && !first && !last);
   KeyEvent(K_INDEX_INC);CHECK(!_4_source && Wildcat144_Overview(&first,&last));
   CHECK(first==saved_first && last==saved_last);
   KeyEvent(K_INDEX_INC);CHECK(_4_source==1);CheckWindow();
   CHECK(fifo_index==reads && record_count==records && last_frame->capture.epoch==epoch);
   CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  }
  Current=T_BASE;Detail[T_BASE]=XPOSI;KeyEvent(K_INDEX_INC);CheckWindow();
  KeyEvent(KEY3);CheckWindow();CHECK(fifo_index==reads && record_count==records && last_frame->capture.epoch==epoch);
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
  CHECK(Wildcat144_Overview(&first,&last));
  hw_ready=0;KeyEvent(KEY1);CHECK(_State.Value==RUN && hw_waiting);
  {unsigned a,b;int32_t ps;
   CHECK(Wildcat144_Overview(&a,&b) && a==first && b==last);
   CHECK(Wildcat144_CursorTime(0,&ps));
   for(i=0;i<3;++i){KeyEvent(0);CHECK(Wildcat144_Overview(&a,&b) && a==first && b==last);}
   hw_ready=1;for(i=0;i<8;++i){KeyEvent(0);if(i<7)CHECK(Wildcat144_Overview(&a,&b) && a==first && b==last);}}

  CHECK(Wildcat144_Overview(&first,&last));CheckWindow();
  Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active() && !Wildcat144_Overview(&first,&last));
 }
 printf("PASS 144 overview epochs: %u checks; all bases/panels, A/C/D edge/pulse, complete RUN/HOLD, retained metadata without pending/partial sample access, unchanged frozen buffer\n",checks);return 0;
}
