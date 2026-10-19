/* Actual Normal and 144 label renderers; no physical LCD. */
#define REAL_UI_FORMAT
#define NORMAL_TRIGGER_FIXTURE
#define Print_Str2 Legacy_TestPrint_Str2
#define main UI144_ExistingTests
#include "test_ui144.c"
#undef main
#undef Print_Str2
static unsigned active,big_labels;static char big_label[8];
static char big_digits[8],big_suffix[8];
u8 ChartLogic(void){return 0;}
u8 BTwinkState;
static unsigned value_calls,value_index[3],value_blink[3];
void Test_NormalBigRefresh(void);
void Test_MeterValue(u8 i)
{
 CHECK(value_calls<3);value_index[value_calls]=i;value_blink[value_calls++]=BTwink;
}
/* Independent layout oracle. Compare actual Normal/144 consumers, not just
 * the new helper, including dirty flags and native large-meter blinking. */
static unsigned Visible(unsigned layout,unsigned page,unsigned i)
{return layout==1 || (layout==2 && (page==4?(i==0 || i==2 || i==3):(i>=1 && i<=3)));}
void Test_NormalMeterLabels(void);
static void VisibilityCases(void)
{
 unsigned layout,page,selected,twink,state,i,n,expected,labels,mask;
 Scope144Measurements measurement={0};meter saved[9];
 for(layout=0;layout<3;++layout)for(page=0;page<5;++page)
 for(selected=0;selected<10;++selected)for(twink=0;twink<2;++twink)for(state=0;state<2;++state){
  FlagMeter=(u8)layout;UpdateMeter=(u8)page;Current=(u8)(METER_0+selected);
  Twink=(u8)twink;Blink=1;mask=labels=0;
  for(i=0;i<9;++i){
   if(Visible(layout,page,i))mask|=1u<<i;
   Meter[i].Item=i?VDC:VBT;Meter[i].Track=i?TRACK1:4;Meter[i].Flag=UPDAT|BLINK;
  }
  memcpy(saved,Meter,sizeof saved);CHECK(UiCommon_MeterMask()==mask);
  active=0;draws=formatted=0;Test_NormalMeterLabels();
  for(i=0;i<9;++i){
   unsigned small=Visible(layout,page,i) && (layout==1 || i==0);
   if(small){++labels;Find(Meter[i].XPOS1,Meter[i].YPOS,i?"Vdc":"Vbt");CHECK(!(Meter[i].Flag&UPDAT));}
   else CHECK(Meter[i].Flag==saved[i].Flag);
   CHECK(Meter[i].Item==saved[i].Item && Meter[i].Track==saved[i].Track);
  }
  CHECK(draws==labels+(labels && selected<9 && Visible(layout,page,selected) && (layout==1 || selected==0)));
  BTwinkState=(u8)state;BTwink=7;value_calls=0;Test_NormalBigRefresh();n=0;expected=state;
  for(i=1;i<4;++i)if(layout==2 && Visible(layout,page,i)){
   unsigned blink=1;
   if(i==selected){expected=!expected;blink=expected;}
   CHECK(value_calls>n && value_index[n]==i && value_blink[n]==blink);++n;
  }
  CHECK(value_calls==n && BTwinkState==expected);
  if(!n)CHECK(BTwink==7);
  memcpy(Meter,saved,sizeof saved);active=1;draws=formatted=big_calls=0;
  Menu144_MeasurementTime(1);Menu144_Presentation(1);
  Menu144_View(&measurement,0,0,1,2);
  for(i=0;i<9;++i){
   CHECK(Meter[i].Item==saved[i].Item && Meter[i].Track==saved[i].Track);
   CHECK((Meter[i].Flag&UPDAT)==(Visible(layout,page,i)?0:UPDAT));
   if(Visible(layout,page,i) && (layout==1 || i==0))Find(Meter[i].XPOS1,Meter[i].YPOS,i?"Vdc":"Vbt");
  }
  CHECK(big_calls==(layout==2?(page==4?2u:3u):0u));
 }
 active=0;
}

vu8 Blink;u8 Options;
uc16 Y_INV[5]={0x0500,0x0501,0x0502,0x0503,0x0504};
uc16 Y_COLOR[5]={0x0005,0x0105,0x0205,0x0305,0x0405};
int Wildcat144_Active(void){return (int)active;}
void Test_NormalMeterLabels(void);
void Test_NormalBigMeter(u8);
void __LCD_Set_Block(u16 x1,u16 x2,u16 y1,u16 y2)
{CHECK(x1==0 && x2==399 && y1==0 && y2==239);}
void Print_Str2(u16 x0,u16 x1,u16 y,u16 color,u8 mode,char *s,u8 a,u8 b)
{
    (void)a;(void)b;(void)color;(void)mode;
    CHECK(x0<x1 && x1<=399 && y+22<=240);
    if(x0==346){CHECK(strlen(s)<=4);strcpy(big_label,s);++big_labels;}
    if(x0==314 && y==154){CHECK(strlen(s)<=5);strcpy(big_digits,s);}
    if(x0==313 && y==129){CHECK(strlen(s)<=3);strcpy(big_suffix,s);}
}
static void NumericCases(void)
{
 static const unsigned ids[]={VBT,VDC,RMS,MAX,MIN,VPP,FRQ,CIR,DUT,TH,TL};
 static const int32_t volts[]={0,29200,99949,100000,999499,1000000,2920000,9994999,10000000,99949999,100000000,1000000000};
 unsigned item,k,negative;char expected[16],digits[8],suffix[8];
 for(item=0;item<11;++item)for(k=0;k<sizeof volts/sizeof volts[0];++k)for(negative=0;negative<2;++negative){
  Scope144Measurements m={0};int32_t v=volts[k];unsigned sign=SIGN,precision=3;const char *unit=V_UNIT;
  if(negative){if(ids[item]!=VDC && ids[item]!=MIN && ids[item]!=MAX)continue;v=-v;}
  m.average_uv=m.rms_uv=m.maximum_uv=m.minimum_uv=v;
  m.frequency_hz=(uint32_t)volts[k];m.period_ps=m.high_ps=m.low_ps=((uint32_t)volts[k]%4294968u)*1000u;
  m.duty_ppm=(uint32_t)volts[k]%1000001u;
  if(ids[item]==VPP)m.minimum_uv=0;
  if(ids[item]==VBT)v=3700000;
  if(ids[item]==FRQ){unit=F_UNIT;precision=4;sign=UNSIGN;}
  if(ids[item]==CIR || ids[item]==TH || ids[item]==TL){unit=T_UNIT;precision=4;sign=UNSIGN;v=(int32_t)(m.period_ps/1000u);}
  if(ids[item]==DUT){unit=P_UNIT;precision=4;sign=UNSIGN;v=(int32_t)(m.duty_ppm/10);}
  memset(expected,0,sizeof expected);Native_Int2Str(expected,v,unit,(u8)precision,(u8)sign,0);
  CHECK(strlen(expected)<=11);
  FlagMeter=2;UpdateMeter=0;Current=TRACK1;active=1;
  Meter[1].Item=(u8)ids[item];Meter[1].Track=ids[item]==VBT?4:TRACK1;
  Meter[2].Item=Meter[3].Item=VDC;Meter[2].Track=Meter[3].Track=TRACK2;
  memset(NumStr,' ',11);NumStr[11]=0;memcpy(NumStr,expected,strlen(expected));
  Test_NormalBigMeter(1);strcpy(digits,big_digits);strcpy(suffix,big_suffix);
  draws=formatted=0;Menu144_MeasurementTime(1);Menu144_Presentation(1);Menu144_View(&m,0,0,1,2);
  if(strcmp(big_digits,digits) || strcmp(big_suffix,suffix))fprintf(stderr,"format item=%u value=%ld expected=[%s][%s] actual=[%s][%s] raw=[%s]\n",ids[item],(long)v,digits,suffix,big_digits,big_suffix,NumStr);
  CHECK(!strcmp(big_digits,digits) && !strcmp(big_suffix,suffix));
  FlagMeter=1;draws=formatted=0;Menu144_View(&m,0,0,1,2);
  Find(Meter[1].XPOS2,Meter[1].YPOS,expected);
 }
 active=0;
}
int main(void)
{
    static const unsigned ids[]={VBT,VDC,RMS,FRQ,MAX,MIN,VPP,CIR,DUT,TH,TL};
    static const char expected[11][5]={"Vbt","Vdc","RMS","Frq","Max","Min","Vpp","Per","Dut","!TH!","!TL!"};
    unsigned i,j,twink,selected,n;
    {
      Scope144Measurements m={0};
      FlagMeter=2;UpdateMeter=0;Current=TRACK1;active=1;
      Meter[1].Item=VPP;Meter[1].Track=TRACK1;
      Meter[2].Item=Meter[3].Item=VDC;Meter[2].Track=Meter[3].Track=TRACK2;
      m.maximum_uv=2920000;draws=formatted=0;
      Menu144_MeasurementTime(1);Menu144_Presentation(1);Menu144_View(&m,0,0,1,2);
      CHECK(!strcmp(big_digits,"+2.92") && !strcmp(big_suffix,"!V!"));
    }
    for(i=0;i<11;++i) {
        CHECK(!strcmp(UiCommon_MeterLabel(ids[i]),expected[i]));
        for(twink=0;twink<2;++twink)for(selected=0;selected<2;++selected) {
            active=0;draws=formatted=0;Twink=(u8)twink;Blink=1;Current=selected?METER_1:TRACK1;FlagMeter=1;
            for(j=0;j<9;++j){Meter[j].Item=(u8)ids[i];Meter[j].Track=TRACK1;Meter[j].Flag=UPDAT;Meter[j].Str=0;}
            Test_NormalMeterLabels();
            for(j=0;j<9;++j)Find(Meter[j].XPOS1,Meter[j].YPOS,expected[i]);
            active=1;Draw(Current,0,1);
            for(j=0;j<9;++j)Find(Meter[j].XPOS1,Meter[j].YPOS,expected[i]);
            active=0;
            for(n=1;n<4;++n) {
                strcpy(NumStr," 1234mV    ");BTwink=(u8)twink;EnablePaS=1;big_labels=0;
                Test_NormalBigMeter((u8)n);CHECK(big_labels==1 && !strcmp(big_label,expected[i]));
            }
        }
    }
    /* Exact default layout after large -> small: the former B Vdc row
     * becomes A Vpp, including the actual LCD label and enabled color. */
    {
      Scope144Measurements m={0};unsigned count=0;
      FlagMeter=2;UpdateMeter=4;_3_source=_4_source=0;active=1;MeterDefaults();
      draws=formatted=0;Menu144_View(&m,0,0,1,2);CHECK(Meter[3].Item==VDC && Meter[3].Track==TRACK2);
      FlagMeter=1;MeterDefaults();draws=formatted=0;Menu144_View(&m,0,0,1,2);
      CHECK(Meter[3].Item==VPP && Meter[3].Track==TRACK1);
      CHECK(painted[Find(Meter[3].XPOS1,Meter[3].YPOS,"Vpp")].color!=0x0905);
      for(i=0;i<9;++i)count+=Meter[i].Item==VDC;
      CHECK(count==2);active=0;
    }
    NumericCases();
    VisibilityCases();
    CHECK(METER_1==METER_0+1 && sizeof Meter/sizeof Meter[0]==9);
    printf("PASS meter labels: %u checks; real Normal small/large and 144 label consumers, actual Int2Str + big renderer, 2.92V regression, signed voltages/prefixes/time/frequency/duty, both panels, layout masks; no device IO\n",checks);
    return 0;
}
