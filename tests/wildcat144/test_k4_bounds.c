/* Actual short/long K4 release prefixes, with checked Title/Detail access.
 * Simulated menu state only; no physical key timing or DS203 IO. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
u8 Current,Detail[14];
menu Title[13][4];
meter Meter[9];
static unsigned checks,title_reads,detail_reads;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s (Current=%u)\n",__LINE__,#x,Current);exit(1);}}while(0)
void Test_K4ShortStart(void);
void Test_K4LongStart(void);
menu *Test_CheckedTitle(void)
{
    ++title_reads;CHECK(Current<13);
    return Title[Current];
}
u8 Test_CheckedDetail(void)
{
    ++detail_reads;CHECK(Current<14);CHECK(Detail[Current]<4);
    return Detail[Current];
}
int main(void)
{
    unsigned g,d,f,longpress;
    menu expected[13][4];meter before_meter[9];u8 before_detail[14];
    for(g=0;g<256;++g)for(d=0;d<4;++d)for(f=0;f<8;++f)for(longpress=0;longpress<2;++longpress) {
        Current=(u8)g;memset(Title,0x55,sizeof Title);memset(Meter,0xa5,sizeof Meter);
        memset(Detail,0xe7,sizeof Detail);
        if(g<METER_0){Detail[g]=(u8)d;Title[g][d].Flag=(u8)f;}
        memcpy(expected,Title,sizeof expected);memcpy(before_meter,Meter,sizeof before_meter);
        memcpy(before_detail,Detail,sizeof before_detail);
        if(g<METER_0)expected[g][d].Flag|=UPDAT;
        title_reads=detail_reads=0;
        if(longpress)Test_K4LongStart();else Test_K4ShortStart();
        CHECK(title_reads==(g<METER_0));CHECK(detail_reads==(g<METER_0));
        CHECK(!memcmp(expected,Title,sizeof expected));
        CHECK(!memcmp(before_meter,Meter,sizeof before_meter));
        CHECK(!memcmp(before_detail,Detail,sizeof before_detail));
        CHECK(Current==g);
    }
    printf("PASS K4 bounds: %u checks; short/long release, all 9 meters and all 256 group values; title refresh preserved; no IO\n",checks);
    return 0;
}
