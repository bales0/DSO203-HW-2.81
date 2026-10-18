/* Reuse BIOS fixtures; execute dispatch extracted from actual Main.c. */
#define main NativeService_ExistingTests
#include "test_native.c"
#undef main
int Test_NormalGenerator(unsigned key);
void Test_LegacyGeneratorFinish(void);
int main(void){
 unsigned kind,key,before;
 CHECK(VirtualAlloc((void *)0x40000000,0x30000,0x3000,4)==(void *)0x40000000);
 Current=OUTPUT;Sweep=GenAdjustMode=0;ArbtSampleNumber=8;UartFileSize=16;
 for(kind=0;kind<9;++kind)for(key=0;key<2;++key){
  _Kind=0;Detail[OUTPUT]=KIND;Generator_Key(K_INDEX_INC,0);Generator_Reconcile();
  ArbtSampleNumber=8;UartFileSize=16;
  _Kind=(s16)kind;Detail[OUTPUT]=KIND;SelectiveUpdate=0;before=outputs;
  CHECK(Test_NormalGenerator(key?K_INDEX_INC:K_INDEX_DEC));CHECK(outputs==before+1);
 }
 _Kind=8;Detail[OUTPUT]=FRQN;GenUartAdj=0;before=outputs;SelectiveUpdate=0;
 CHECK(Test_NormalGenerator(KEY4));CHECK(outputs==before && GenUartAdj==1);
 GenUartAdj=0;DMA1_Channel4->CCR=0;before=outputs;SelectiveUpdate=0;
 CHECK(Test_NormalGenerator(K_INDEX_S));CHECK(outputs==before); /* baud editor */
 Detail[OUTPUT]=KIND;UartFileSize=16;
 CHECK(Test_NormalGenerator(K_INDEX_S));CHECK(outputs==before+1 && StartUart);
 /* Preserve the legacy sweep/special-editor fallback without shared handling. */
 _Kind=0;Sweep=1;Detail[OUTPUT]=KIND;Key_Buffer=K_INDEX_INC;SelectiveUpdate=0;before=outputs;
 CHECK(!Test_NormalGenerator(K_INDEX_INC));Test_LegacyGeneratorFinish();CHECK(outputs==before+1);
 printf("PASS generator dispatch: %u checks; one configuration per KIND edit, K4 navigation, K_INDEX_S UART, native sweep fallback\n",checks);
 return 0;
}
