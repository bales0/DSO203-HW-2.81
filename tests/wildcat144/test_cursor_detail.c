/* Shared K4 action versus the complete native cursor branch. Acquisition
 * fixtures are reused; no device, analog behavior or LCD timing is tested. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
int Test_NormalChannelDetail(void);
void Test_LegacyCursorDetail(void);
static void Fixture(unsigned group,unsigned detail,unsigned panel,unsigned page,unsigned state,unsigned flags)
{
    unsigned j;
    memset(Title,0,sizeof Title);memset(Detail,0,sizeof Detail);
    Current=(u8)group;Detail[group]=(u8)detail;FlagMeter=(u8)panel;UpdateMeter=(u8)page;
    ShowFFT=0;_Mode=AUTO;_State.Value=(s16)state;
    for(j=0;j<4;++j) {
        menu e={0,0,200,j<2?FIX:j==2?CIRC:NUM3,0,0,(s16)(70+j),(u8)flags};
        memcpy(&Title[group][j],&e,sizeof e);
    }
}
int main(void)
{
    unsigned k,p,page,state,d,f,m,n,g,interlace;menu before[13][4],expected[13][4];
    u8 details[14],expected_details[14];
    static const unsigned groups[]={V_VERNIE,T_VERNIE};
    static const u8 marks[]={0,FIX,CIRC,NOT,NUM2,NUM3,NOT|NUM3};
    for(k=0;k<2;++k)for(p=0;p<3;++p)for(page=0;page<2;++page)
    for(state=RUN;state<=HOLD;++state)for(d=0;d<4;++d)for(f=0;f<4;++f)for(m=0;m<7;++m) {
        menu e;
        g=groups[k];Fixture(g,d,p,page?4:0,state,f);
        n=(d+1)%4;
        {menu changed={0,0,200,marks[m],0,0,Title[g][n].Value,(u8)f};memcpy(&e,&changed,sizeof e);}
        memcpy(&Title[g][n],&e,sizeof e);
        memcpy(before,Title,sizeof before);memcpy(details,Detail,sizeof details);
        Test_LegacyCursorDetail();
        memcpy(expected,Title,sizeof expected);memcpy(expected_details,Detail,sizeof expected_details);
        memcpy(Title,before,sizeof before);memcpy(Detail,details,sizeof details);
        CHECK(Test_NormalChannelDetail());
        CHECK(!memcmp(expected,Title,sizeof expected));
        CHECK(!memcmp(expected_details,Detail,sizeof expected_details));
    }
    /* Exact existing 144 cycles, including time-delta mode with panel hidden.
     * Stored data and every other title/detail must remain unchanged. */
    for(k=0;k<2;++k)for(p=0;p<3;++p)for(state=RUN;state<=HOLD;++state)
    for(d=0;d<Scope144_DetailLimit(groups[k]);++d) {
        g=groups[k];Fixture(g,d,p,4,state,0);
        memcpy(before,Title,sizeof before);memcpy(details,Detail,sizeof details);
        n=(d+1)%Scope144_DetailLimit(g);
        memcpy(expected,before,sizeof expected);memcpy(expected_details,details,sizeof expected_details);
        expected[g][d].Flag|=UPDAT;expected[g][n].Flag|=BLINK;expected_details[g]=(u8)n;
        CHECK(UiCommon_NextDetail(1));
        CHECK(!memcmp(expected,Title,sizeof expected));
        CHECK(!memcmp(expected_details,Detail,sizeof expected_details));
    }
    /* FFT/XY keeps its native pre-navigation; its following detail step is shared. */
    for(k=0;k<2;++k)for(m=0;m<2;++m) {
        Fixture(groups[k],V2,1,4,HOLD,3);ShowFFT=(u8)m;_Mode=m?AUTO:X_Y;
        memcpy(before,Title,sizeof before);memcpy(details,Detail,sizeof details);
        Test_LegacyCursorDetail();
        memcpy(expected,Title,sizeof expected);memcpy(expected_details,Detail,sizeof expected_details);
        memcpy(Title,before,sizeof before);memcpy(Detail,details,sizeof details);
        CHECK(Test_NormalChannelDetail());
        CHECK(!memcmp(expected,Title,sizeof expected) && !memcmp(expected_details,Detail,sizeof expected_details));
    }
    /* Native meters are not consumed here; interlace analog B is never enabled. */
    for(interlace=0;interlace<2;++interlace)for(g=0;g<22;++g) {
        if((!interlace && g<METER_0) || (interlace && (g==TRACK1 || g==V_VERNIE || g==T_VERNIE || g==T_BASE)))continue;
        Current=(u8)g;ShowFFT=0;_Mode=AUTO;
        memcpy(before,Title,sizeof before);memcpy(details,Detail,sizeof details);
        CHECK(!UiCommon_NextDetail(interlace));
        CHECK(!memcmp(before,Title,sizeof before) && !memcmp(details,Detail,sizeof details));
    }
    for(k=0;k<2;++k)for(interlace=0;interlace<2;++interlace) {
        Fixture(groups[k],interlace?Scope144_DetailLimit(groups[k]):4,1,4,HOLD,0);
        memcpy(before,Title,sizeof before);memcpy(details,Detail,sizeof details);
        CHECK(!UiCommon_NextDetail(interlace));
        CHECK(!memcmp(before,Title,sizeof before) && !memcmp(details,Detail,sizeof details));
    }
    CHECK(!outputs && !adc_controls && !range_sets && !record_count && !files);
    printf("PASS shared cursor detail: %u checks; exact native flags/hidden panel, 144 V1/V2 and T1/T2/delta, RUN/HOLD, FFT/XY tail equivalent, no value/HW mutation\n",checks);
    return 0;
}
