/* Exercise real runtime demand/cache against mock BIOS; no device IO. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
static void Begin(unsigned panel,unsigned page)
{
    unsigned i;
    CHECK(!Wildcat144_Active());
    memset(Meter,0,sizeof Meter);FlagMeter=(u8)panel;UpdateMeter=(u8)page;
    for(i=0;i<9;++i){Meter[i].Item=VDC;Meter[i].Track=TRACK1;}
    _Kind=6;_Mode=AUTO;_Tr_kind=1;_Tr_source=TRACK1;_A_Range=0;
    _1_source=_3_source=_4_source=1;_State.Value=RUN;Current=TRACK1;
    Title[TRACK1][COUPLE].Value=SCOPE144_DC;
    history=1;capture_case=2;hw_test=hw_ready=0;key_status=0xffff;
    digital_scans=digital_scan_mask=0;
}
static void Stop(void)
{
    Current=TRACK1;KeyEvent(K_ITEM_S);CHECK(!Wildcat144_Active());
}
static void MeterAt(unsigned i,unsigned channel)
{Meter[i].Item=FRQ;Meter[i].Track=(u8)(TRACK3+channel);}
static void AnalogDemandCases(void)
{
 unsigned i,records,reads;uint32_t epoch,hash;Scope144Measurements full;
 Begin(0,4);CHECK(ServiceUntilFrame());CHECK(!analog_timing_available);
 CHECK(shown_measure.samples && !shown_measure.frequency_hz && !shown_measure.period_ps);
 KeyEvent(KEY1);CHECK(_State.Value==HOLD && Frame144_Valid(last_frame));
 records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);
 CHECK(Frame144_Measure(last_frame,&full));
 FlagMeter=1;Meter[3].Item=FRQ;Meter[3].Track=TRACK1;KeyEvent(0);
 CHECK(analog_timing_available && !memcmp(&full,&shown_measure,sizeof full));
 for(i=0;i<20;++i){Meter[3].Item=(u8)(i%2?DUT:FRQ);KeyEvent(0);
  CHECK(analog_timing_available && !memcmp(&full,&shown_measure,sizeof full));
  CHECK(record_count==records && fifo_index==reads && last_frame->capture.epoch==epoch);
  CHECK(Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
 }
 FlagMeter=0;KeyEvent(KEY1);CHECK(ServiceUntilFrame());CHECK(!analog_timing_available && !shown_measure.period_ps);Stop();
 /* A newly selected time meter cannot compute from an unfinished RUN epoch. */
 Begin(1,4);_Mode=NORH;hw_test=hw_ready=1;CHECK(ServiceUntilFrame());
 for(i=0;i<8 && !shown_valid;++i)KeyEvent(0);
 CHECK(shown_valid && !analog_timing_available);hw_ready=0;KeyEvent(0);
 CHECK(hw_waiting && shown_valid);hw_ready=1;KeyEvent(0);CHECK(!shown_valid);
 records=record_count;reads=fifo_index;Meter[3].Item=FRQ;Meter[3].Track=TRACK1;
 for(i=0;i<3;++i){KeyEvent(0);CHECK(!analog_timing_available && record_count==records && fifo_index==reads+512*(i+1));}
 Stop();hw_test=0;
}
int main(void)
{
    unsigned i,mask,records,reads,scans;uint32_t epoch,hash;
    Scope144Measurements a,b;
    CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
    StartupImageCheck();
    xattributes[_100uS].PSC=0;xattributes[_100uS].ARR=239;
    for(i=0;i<8;++i){strcpy(attributes[i].STR,"1V");attributes[i].SCALE=40000;}
    WildcatFpga=FpgaProfile_Decode(0x5731,0x0101,0x0102);Wildcat144_CheckImage();
    AnalogDemandCases();
    /* Traces alone and hidden meters never scan. Both visible channels share one. */
    for(mask=0;mask<4;++mask){
        Begin(1,4);if(mask&1)MeterAt(2,0);if(mask&2)MeterAt(3,1);
        CHECK(ServiceUntilFrame());CHECK(digital_scans==(mask?1u:0u));
        CHECK(digital_scan_mask==mask);Stop();
    }
    Begin(0,4);MeterAt(2,0);MeterAt(3,1);CHECK(ServiceUntilFrame());CHECK(!digital_scans);Stop();
    for(i=0;i<9;++i){
        Begin(2,4);MeterAt(i,0);CHECK(ServiceUntilFrame());
        CHECK(digital_scans==((i==0 || i==2 || i==3)?1u:0u));Stop();
        Begin(2,0);MeterAt(i,1);CHECK(ServiceUntilFrame());
        CHECK(digital_scans==((i>=1 && i<4)?1u:0u));Stop();
    }
    Begin(1,4);Meter[2].Item=RMS;Meter[2].Track=TRACK3;
    MeterAt(3,1);_4_source=0;CHECK(ServiceUntilFrame());CHECK(!digital_scans);Stop();
    /* Lazy HOLD calculation leaves FIFO, frozen epoch and bytes untouched. */
    Begin(1,4);CHECK(ServiceUntilFrame());KeyEvent(KEY1);CHECK(_State.Value==HOLD);
    CHECK(!digital_scans);records=record_count;reads=fifo_index;epoch=last_frame->capture.epoch;
    hash=Scope144_RealCrc(DataBuf,sizeof DataBuf);MeterAt(2,0);MeterAt(3,1);
    CHECK(Wildcat144_DigitalMeasurement(0,&a));CHECK(digital_scans==1 && digital_scan_mask==3);
    CHECK(Wildcat144_DigitalMeasurement(1,&b));
    for(i=0;i<50;++i){
        Meter[2].Item=(u8)(i%2?DUT:FRQ);Twink=!Twink;KeyEvent(0);
        CHECK(Wildcat144_DigitalMeasurement(0,&b));CHECK(!memcmp(&a,&b,sizeof a));
        CHECK(digital_scans==1 && record_count==records && fifo_index==reads);
        CHECK(last_frame->capture.epoch==epoch && Scope144_RealCrc(DataBuf,sizeof DataBuf)==hash);
    }
    CHECK(!Wildcat144_DigitalMeasurement(2,&a));CHECK(!Wildcat144_DigitalMeasurement(0,0));
    KeyEvent(KEY1);CHECK(_State.Value==RUN && digital_scans==2);Stop();
    /* A period-free result is cached, including failed results for both channels. */
    digital_constant=1;Begin(1,4);MeterAt(2,0);MeterAt(3,1);
    CHECK(ServiceUntilFrame());KeyEvent(KEY1);scans=digital_scans;CHECK(scans==1);
    for(i=0;i<50;++i){CHECK(!Wildcat144_DigitalMeasurement(0,&a));CHECK(!Wildcat144_DigitalMeasurement(1,&b));}
    CHECK(digital_scans==scans);Stop();digital_constant=0;
    /* Both channels may use untouched completed RAM during stage 1. */
    Begin(1,4);MeterAt(2,0);_Mode=NORH;_T_base=21;
    capture_case=history=0;hw_test=1;hw_ready=0;
    CHECK(ServiceUntilFrame() && hw_waiting);hw_ready=1;
    for(i=0;i<8;++i)KeyEvent(0);
    CHECK(!hw_waiting && Frame144_Valid(last_frame));
    CHECK(Wildcat144_DigitalMeasurement(0,&a));scans=digital_scans;
    hw_ready=0;KeyEvent(0);CHECK(hw_waiting);
    CHECK(Wildcat144_DigitalMeasurement(0,&b) && !memcmp(&a,&b,sizeof a));
    CHECK(Wildcat144_DigitalMeasurement(1,&b));CHECK(digital_scans==scans+1);
    KeyEvent(KEY1);CHECK(Wildcat144_DigitalMeasurement(0,&b));
    CHECK(Wildcat144_DigitalMeasurement(1,&b));Stop();
    printf("PASS digital demand/cache: %u checks; visible masks, lazy HOLD, epochs, no-period cache, incomplete FIFO guard\n",checks);
    return 0;
}
