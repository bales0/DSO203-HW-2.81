/* Execute the real key-publication block and the real press consumer.
 * The unchanged Read_Keys scanner emits zero while K1 remains held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
#include "Function.h"
#include "Interrupt.h"
#include "Scope144.h"
static unsigned checks,beeps;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
vu32 Key1PressCount;vu8 Key_Buffer;vu16 Beep_mS;
u8 SetLowVolume,LoBeepLevel;menu Title[13][4];
void __Set(u8 what,u32 value){CHECK(what==BEEP_VOLUME && value<=45);++beeps;}
void Test_KeyPublish(u8 code);
int main(void)
{
    unsigned i,n,first;int state;uint32_t seen;
    Title[VOLUME][CLASS].Value=5;
    for(first=0;first<2;++first)for(n=0;n<64;++n) {
        Key1PressCount=123;seen=123;state=(int)first;Beep_mS=0;
        for(i=0;i<n;++i)Test_KeyPublish(KEY1);
        CHECK(Scope144_RunPresses(Key1PressCount,&seen,&state)==(n!=0));
        CHECK((unsigned)state==(first^(n&1)) && seen==123+n && Beep_mS==(n?60:0));
        for(i=0;i<100;++i){Test_KeyPublish(0);CHECK(!Scope144_RunPresses(Key1PressCount,&seen,&state));}
        CHECK((unsigned)state==(first^(n&1))); /* physically held or released, no new event */
        Test_KeyPublish(KEY3);Test_KeyPublish(K_INDEX_INC);CHECK(Key_Buffer==K_INDEX_INC);
        CHECK(!Scope144_RunPresses(Key1PressCount,&seen,&state));
    }
    Key1PressCount=0xfffffffeu;seen=0xfffffffeu;state=0;
    Test_KeyPublish(KEY1);Test_KeyPublish(KEY1);
    CHECK(!Key1PressCount && Scope144_RunPresses(Key1PressCount,&seen,&state) && !state);
    Test_KeyPublish(KEY1);CHECK(Scope144_RunPresses(Key1PressCount,&seen,&state) && state==1);
    printf("PASS K1 events: %u checks; actual IRQ publication, held repeats, queued overwrite, 0..63 presses, rollover; no device IO\n",checks);
    return 0;
}
