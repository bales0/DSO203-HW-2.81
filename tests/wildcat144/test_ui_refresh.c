/* Actual UI scheduling: dirty meter definitions cannot wait for K3. */
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
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
int main(void){
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
 PanelRows();
 printf("PASS UI refresh: %u checks; gray Vdc -> valid Vpp with dirty metadata, availability changes, K3 row restoration, small/large/pages, waiting/read/HOLD, native idle gates; no device IO\n",checks);return 0;
}
