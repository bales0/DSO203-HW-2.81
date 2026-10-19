/* Actual native tail and pre-navigation versus the removed frozen rules.
 * Covers details and meters only, not physical key timing or USB/analog HW. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
void Test_NormalDetailTail(void);
void Test_LegacyDetailTail(void);
int Test_NormalDetailPrelude(void);
int Test_LegacyDetailPrelude(void);
static void Fixture(unsigned group,unsigned detail,unsigned panel)
{
    unsigned g,d;
    memset(Title,0,sizeof Title);memset(Meter,0,sizeof Meter);memset(Detail,0,sizeof Detail);
    Current=(u8)group;FlagMeter=(u8)panel;_Mode=AUTO;ShowFFT=0;FrameMode=0;Options=0;OsBufferLogicFlag=0;
    if(group<14)Detail[group]=(u8)detail;
    for(g=0;g<13;++g)for(d=0;d<4;++d) {
        menu e={0,0,200,d==3?NUM3:0,0,0,0,1};memcpy(&Title[g][d],&e,sizeof e);
    }
}
static void Compare(unsigned prelude)
{
    menu before[13][4],expected[13][4];meter before_meter[9],expected_meter[9];
    u8 before_detail[14],expected_detail[14];int old_exit=0,new_exit=0;
    memcpy(before,Title,sizeof before);memcpy(before_detail,Detail,sizeof before_detail);
    memcpy(before_meter,Meter,sizeof before_meter);
    if(prelude)old_exit=Test_LegacyDetailPrelude();
    if(!old_exit)Test_LegacyDetailTail();
    memcpy(expected,Title,sizeof expected);memcpy(expected_detail,Detail,sizeof expected_detail);
    memcpy(expected_meter,Meter,sizeof expected_meter);
    memcpy(Title,before,sizeof before);memcpy(Detail,before_detail,sizeof before_detail);
    memcpy(Meter,before_meter,sizeof before_meter);
    if(prelude)new_exit=Test_NormalDetailPrelude();
    if(!new_exit)Test_NormalDetailTail();
    CHECK(old_exit==new_exit);
    CHECK(!memcmp(expected,Title,sizeof expected));
    CHECK(!memcmp(expected_detail,Detail,sizeof expected_detail));
    CHECK(!memcmp(expected_meter,Meter,sizeof expected_meter));
}
int main(void)
{
    unsigned g,d,m,f,k,p,a,type,mode,source,item,track;
    /* Every metadata byte, every native title group/detail and dirty flags. */
    for(g=0;g<METER_0;++g)for(d=0;d<4;++d)for(m=0;m<256;++m)for(f=0;f<4;++f) {
        unsigned next=(d+1)%4;Fixture(g,d,1);
        {menu e={0,0,200,(u8)m,0,0,0,(u8)f};memcpy(&Title[g][next],&e,sizeof e);}
        Compare(0);
    }
    /* Original sweep/special-editor GEN fallback preserves PWM duty and
     * skips amplitude/duty as before, with all relevant MARK combinations. */
    for(k=0;k<9;++k)for(d=0;d<4;++d)for(m=0;m<256;++m) {
        Fixture(OUTPUT,d,1);_Kind=(s16)k;
        {menu e={0,0,200,(u8)m,0,0,Title[OUTPUT][(d+1)%4].Value,0};memcpy(&Title[OUTPUT][(d+1)%4],&e,sizeof e);}
        Compare(0);
    }
    for(p=0;p<3;++p)for(g=V_VERNIE;g<=T_VERNIE;g+=2)for(d=0;d<4;++d){Fixture(g,d,p);Compare(0);}
    /* Legacy formats and ROM-backup restriction, including save vs load. */
    for(type=0;type<8;++type)for(a=SAVE;a<=LOAD;++a)for(d=0;d<4;++d)for(m=0;m<256;++m) {
        Fixture(FILE,d,1);Title[FILE][2].Value=(s16)type;Title[FILE][0].Value=(s16)a;
        {menu e={0,0,200,(u8)m,0,0,Title[FILE][(d+1)%4].Value,0};memcpy(&Title[FILE][(d+1)%4],&e,sizeof e);}
        Compare(0);
    }
    /* The special C/D, FFT/map, XY, timebase and holdoff pre-navigation
     * itself is still extracted from production, then feeds the same core. */
    for(g=0;g<METER_0;++g)for(d=0;d<4;++d)for(mode=0;mode<10;++mode)
    for(source=0;source<16;++source)for(p=0;p<3;++p) {
        Fixture(g,d,p);_Mode=(s16)mode;_4_source=(s16)source;
        _T_base=(s16)(source%12);Options=(u8)(source&1);OsBufferLogicFlag=(u8)(source&2);
        Compare(1);
    }
    /* Real native meter tail is unchanged: Vbat no source change;
     * voltage-only versus digital-capable items retain their source cycles. */
    for(g=METER_0;g<=METER_0+8;++g)for(item=0;item<12;++item)for(track=0;track<5;++track) {
        if(item!=VBT && item<VDC)continue;
        Fixture(g,0,1);Meter[g-METER_0].Item=(u8)item;Meter[g-METER_0].Track=(u8)track;Compare(0);
    }
    CHECK(!outputs && !adc_controls && !range_sets && !record_count && !files);
    printf("PASS deduplicated detail core: %u checks; all MARK bytes/native groups, GEN kinds, FILE modes, FFT/XY pre-navigation, cursor panels, meter sources; no HW writes\n",checks);
    return 0;
}
