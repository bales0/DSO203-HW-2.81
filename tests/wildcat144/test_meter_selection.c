/* Real Normal item stepping and CFG decoding; no device IO. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
u8 SaveCurrent[2];
u8 EnableMeterCalc,AutoSetFlag,DownConvertInitiate,SerialStatus;
unsigned Test_NormalTimeDemand(void);
void Test_NormalMeterStep(int);
void Test_LoadMeterItems(u16 *);
void Test_StoreMeterItems(u16 *);
void Test_StoreBootMeterItems(u16 *);
static void DemandCases(void)
{
 unsigned layout,i,item,track,k,forced,expected;
 for(layout=0;layout<15;++layout){
  FlagMeter=(u8)(layout/5);UpdateMeter=(u8)(layout%5);
  CHECK(UiCommon_MeterMask()==(FlagMeter==0?0u:FlagMeter==1?511u:UpdateMeter==4?13u:14u));
  for(i=0;i<9;++i)for(item=0;item<256;++item)for(track=0;track<6;++track){
   unsigned visible=FlagMeter==1 || (FlagMeter==2 && (UpdateMeter==4?(i==0 || i==2 || i==3):(i>=1 && i<4)));
   memset(Meter,0,sizeof Meter);Meter[i].Item=(u8)item;Meter[i].Track=(u8)track;
   expected=visible && track<4 && (item==FRQ || (item>=CIR && item<=TL))?1u<<track:0;
   for(forced=0;forced<16;forced+=5)CHECK(UiCommon_TimeDemand(forced)==(expected|forced));
  }
 }
 FlagMeter=0;_Mode=AUTO;_4_source=1;AutoSetFlag=EnableMeterCalc=DownConvertMode=DownConvertInitiate=ShowFFT=Det=SerialStatus=OsBufferLogicFlag=CalibrateMode=0;
 CHECK(!Test_NormalTimeDemand());
 {u8 *force[]={&AutoSetFlag,&EnableMeterCalc,&DownConvertMode,&DownConvertInitiate,&ShowFFT,&Det,&SerialStatus,&OsBufferLogicFlag,&CalibrateMode};
  for(k=0;k<sizeof force/sizeof force[0];++k){*force[k]=1;CHECK(Test_NormalTimeDemand()==15);*force[k]=0;}}
 _Mode=X_Y;CHECK(Test_NormalTimeDemand()==15);_Mode=SCAN;CHECK(Test_NormalTimeDemand()==15);_Mode=AUTO;
 _4_source=14;CHECK(Test_NormalTimeDemand()==15);_4_source=1;
 for(layout=0;layout<4;++layout){FlagMeter=1;memset(Meter,0,sizeof Meter);Meter[3].Item=FRQ;Meter[3].Track=(u8)layout;CHECK(Test_NormalTimeDemand()==(1u<<layout));}
 FlagMeter=0;memset(Meter,0,sizeof Meter);
}
int main(void)
{
    static const unsigned analog[]={VBT,VDC,RMS,FRQ,MAX,MIN,VPP,CIR,DUT,TH,TL};
    static const unsigned digital[]={VBT,FRQ,CIR,DUT,TH,TL};
    unsigned group,track,i,j,n,dir,k;u16 encoded[9],saved[9],boot[9];
    DemandCases();
    /* Both directions visit every remaining item once; channel restoration
     * through the battery entry remains exactly the native behavior. */
    for(group=METER_0;group<=METER_8;++group)for(track=TRACK1;track<=TRACK4;++track) {
        const unsigned *items=track<TRACK3?analog:digital;
        n=track<TRACK3?11u:6u;
        for(i=0;i<n;++i)for(dir=0;dir<2;++dir) {
            meter *m=&Meter[group-METER_0];unsigned expected=items[(i+(dir?1:n-1))%n];
            Current=(u8)group;SaveCurrent[0]=(u8)group;SaveCurrent[1]=(u8)track;
            m->Item=(u8)items[i];m->Track=items[i]==VBT?4:(u8)track;m->Flag=BLINK;
            Test_NormalMeterStep(dir?1:-1);
            CHECK(m->Item==expected && m->Item!=1);
            CHECK(m->Track==(expected==VBT?4u:track));CHECK(m->Flag==(UPDAT|BLINK));
            if(expected==VBT)CHECK(SaveCurrent[0]==group && SaveCurrent[1]==track);
        }
    }
    /* Common 144/Normal stepping skips exactly one retired slot and keeps
     * all other measurement IDs, both wrap directions and long sequences. */
    for(i=0;i<11;++i)for(dir=0;dir<2;++dir) {
        unsigned item=analog[i];
        for(j=0;j<110;++j) {
            item=UiCommon_MeterStep(item,dir?1:-1);
            CHECK(item==analog[(i+11u+(dir?(j+1)%11u:11u-(j+1)%11u))%11u]);
            CHECK(item!=1);
        }
    }
    /* Migration is the actual load loop. Every non-retired item/channel is
     * unchanged; the retired slot becomes the existing A DC readout. */
    for(i=0;i<256;++i)for(track=0;track<256;++track) {
        for(k=0;k<9;++k)encoded[k]=(u16)((track<<8)+i);
        memset(Meter,0,sizeof Meter);Test_LoadMeterItems(encoded);
        Test_StoreMeterItems(saved);Test_StoreBootMeterItems(boot);
        for(k=0;k<9;++k) {
            unsigned invalid=i==1 || i>TL || track>4;
            CHECK(Meter[k].Item==(invalid?VDC:i));
            CHECK(Meter[k].Track==(invalid?TRACK1:track));
            CHECK(saved[k]==(u16)((Meter[k].Track<<8)+Meter[k].Item));
            CHECK(boot[k]==saved[k] && (saved[k]&255)!=1);
        }
    }
    for(i=0;i<65536;++i) {
        unsigned valid=i==VBT || (i>=VDC && i<=TL);
        const char *label=UiCommon_MeterLabel(i);
        CHECK(label && *label && strlen(label)<=4);
        if(!valid)CHECK(!strcmp(label,"--"));
        for(dir=0;dir<2;++dir) {
            unsigned next=UiCommon_MeterStep(i,dir?1:-1);
            CHECK(next==VBT || (next>=VDC && next<=TL));
        }
    }
    CHECK(!outputs && !adc_controls && !range_sets && !record_count && !files);
    printf("PASS meter selection: %u checks; actual native stepping, retired item skipped, other CFG IDs/channels preserved; no device IO\n",checks);
    return 0;
}
