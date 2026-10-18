/* Actual common action and Normal dispatch versus native legacy navigation.
 * Reuse fixtures only; acquisition and analog hardware are not exercised. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
int Test_NormalChannelDetail(void);
void Test_LegacyChannelDetail(void);
int main(void)
{
    unsigned channel,detail,mark,flags,n,interlace;menu expected[13][4],before[13][4];
    u8 expected_detail[14],before_detail[14];
    static const u16 marks[]={0,FIX,CIRC,NOT,NUM2,NUM3,NOT|NUM3};
    for(channel=TRACK1;channel<=TRACK2;++channel)
    for(detail=0;detail<4;++detail)for(mark=0;mark<7;++mark)for(flags=0;flags<4;++flags) {
        memset(Title,0,sizeof Title);memset(Detail,0,sizeof Detail);
        Current=(u8)channel;ShowFFT=0;_Mode=AUTO;Detail[channel]=(u8)detail;
        for(n=0;n<4;++n) {
            menu entry={0,0,0,marks[mark],0,0,(s16)(70+n),(u8)flags};
            memcpy(&Title[channel][n],&entry,sizeof entry);
        }
        memcpy(before,Title,sizeof before);memcpy(before_detail,Detail,sizeof before_detail);
        Test_LegacyChannelDetail();
        memcpy(expected,Title,sizeof expected);memcpy(expected_detail,Detail,sizeof expected_detail);
        memcpy(Title,before,sizeof before);memcpy(Detail,before_detail,sizeof before_detail);
        CHECK(Test_NormalChannelDetail());
        CHECK(!memcmp(Title,expected,sizeof expected));
        CHECK(!memcmp(Detail,expected_detail,sizeof expected_detail));
    }
    /* Native title groups now use the common tail after special navigation. */
    for(interlace=0;interlace<2;++interlace)for(n=0;n<22;++n) {
        Current=(u8)n;ShowFFT=0;_Mode=AUTO;
        if((!interlace && n<METER_0) || (interlace && (n==TRACK1 || n==V_VERNIE || n==T_VERNIE)))continue;
        memcpy(before,Title,sizeof before);memcpy(before_detail,Detail,sizeof before_detail);
        CHECK(!UiCommon_NextDetail(interlace));
        CHECK(!memcmp(Title,before,sizeof before) && !memcmp(Detail,before_detail,sizeof before_detail));
    }
    for(channel=TRACK1;channel<=TRACK2;++channel)for(n=0;n<2;++n) {
        Current=(u8)channel;_Mode=n?AUTO:X_Y;ShowFFT=(u8)n;
        memcpy(before,Title,sizeof before);memcpy(before_detail,Detail,sizeof before_detail);
        Test_LegacyChannelDetail();
        memcpy(expected,Title,sizeof expected);memcpy(expected_detail,Detail,sizeof expected_detail);
        memcpy(Title,before,sizeof before);memcpy(Detail,before_detail,sizeof before_detail);
        CHECK(Test_NormalChannelDetail());
        CHECK(!memcmp(Title,expected,sizeof expected) && !memcmp(Detail,expected_detail,sizeof expected_detail));
    }
    /* Interlace A retains all four native fields, independently of MARK; B
     * remains unavailable. Moving focus must not change any channel value. */
    ShowFFT=0;_Mode=AUTO;Current=TRACK1;
    for(detail=0;detail<4;++detail) {
        unsigned next=(detail+1)%4;
        Detail[TRACK1]=(u8)detail;
        for(n=0;n<4;++n){Title[TRACK1][n].Flag=0;}
        memcpy(before,Title,sizeof before);
        CHECK(UiCommon_NextDetail(1) && Detail[TRACK1]==next);
        CHECK(Title[TRACK1][detail].Flag==UPDAT && Title[TRACK1][next].Flag==BLINK);
        for(n=0;n<4;++n)CHECK(Title[TRACK1][n].Value==before[TRACK1][n].Value);
    }
    CHECK(!outputs && !adc_controls && !range_sets && !record_count && !files);
    printf("PASS shared channel detail: %u checks; exact native A/B flags/values, 144 A cycle, disabled B, native FFT/XY tail equivalent, unavailable interlace groups untouched\n",checks);
    return 0;
}
