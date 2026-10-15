#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Gen144.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    TIM_TypeDef timer4, timer7;
    DMA_Channel_TypeDef dma;
    DAC_TypeDef dac;
    GPIO_TypeDef gpioa, gpiob;
    unsigned a, b, value;
    CHECK(sizeof(u32)==4 && sizeof(u16)==2);
    for (a=0; a<16; ++a) for (b=0; b<16; ++b) {
        u32 old_a=(0x12345678ul & ~(0xful<<16)) | ((u32)a<<16);
        u32 old_b=(0x89abcdeful & ~(0xful<<24)) | ((u32)b<<24);
        memset(&timer4,0,sizeof timer4); memset(&timer7,0,sizeof timer7);
        memset(&dma,0,sizeof dma); memset(&dac,0,sizeof dac);
        memset(&gpioa,0,sizeof gpioa); memset(&gpiob,0,sizeof gpiob);
        timer4.CR1=0xa581; timer4.CCER=0x1357; timer4.DIER=0x2468;
        timer4.ARR=71; timer4.CCR1=36; timer7.CR1=0xa585; timer7.ARR=123;
        dma.CCR=0x1ab1; dma.CNDTR=720; dma.CMAR=0x20003000;
        gpioa.CRL=old_a; gpiob.CRL=old_b;
        gpioa.ODR=0x1234; gpiob.ODR=0x5678; dac.CR=0x0b6c002c;
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,878);
        CHECK(timer4.CR1==0xa580 && timer4.CCER==0x1356);
        CHECK(timer7.CR1==0xa584 && dma.CCR==0x1ab0);
        CHECK(((gpiob.CRL>>24)&15)==4); /* PWM pin released for every prior mode. */
        CHECK(((gpioa.CRL>>16)&15)==0);
        CHECK((gpiob.CRL & ~(0xful<<24))==(old_b & ~(0xful<<24)));
        CHECK((gpioa.CRL & ~(0xful<<16))==(old_a & ~(0xful<<16)));
        CHECK(dac.CR==3 && dac.DHR12R1==878);
        CHECK(gpioa.ODR==0x1234 && gpiob.ODR==0x5678);
        CHECK(timer4.DIER==0x2468 && timer4.ARR==71 && timer4.CCR1==36);
        CHECK(timer7.ARR==123 && dma.CNDTR==720 && dma.CMAR==0x20003000);
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,2633);
        CHECK(dac.DHR12R1==2633 && dac.CR==3);
        CHECK(gpiob.CRL==((old_b & ~(0xful<<24)) | (4ul<<24)));
        /* Full roundtrip: DC -> 1MHz square -> DC, from every GPIO mode. */
        timer4.CCMR1=0xa5ff; timer4.CNT=456; timer4.SR=0xffff;
        Gen144_SetSquare(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob);
        CHECK(timer4.CR1==0x81 && (timer4.CCER&3)==1);
        CHECK(timer4.CCMR1==0xa568);
        CHECK(timer4.PSC==0 && timer4.ARR==71 && timer4.CCR1==36);
        CHECK(timer4.CNT==0 && timer4.EGR==1 && timer4.SR==0);
        CHECK(dac.CR==0 && !(timer7.CR1&1) && !(dma.CCR&1));
        CHECK(((gpioa.CRL>>16)&15)==4 && ((gpiob.CRL>>24)&15)==0xb);
        CHECK((gpioa.CRL & ~(0xful<<16))==(old_a & ~(0xful<<16)));
        CHECK((gpiob.CRL & ~(0xful<<24))==(old_b & ~(0xful<<24)));
        CHECK(timer4.DIER==0x2468 && (timer4.CCER&~3u)==0x1354);
        CHECK(dma.CMAR==0x20003000 && dma.CNDTR==720 && timer7.ARR==123);
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,0);
        CHECK(!(timer4.CR1&1) && !(timer4.CCER&1) && dac.CR==3 && dac.DHR12R1==0);
        CHECK(((gpiob.CRL>>24)&15)==4 && ((gpioa.CRL>>16)&15)==0);
    }
    for (value=0; value<4096; ++value) {
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,value);
        CHECK(dac.DHR12R1==value && dac.CR==3);
        CHECK(!(timer4.CR1&1) && !(timer4.CCER&1) && !(timer7.CR1&1) && !(dma.CCR&1));
    }
    for (value=1;value<=4;value*=2) {
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,878);
        CHECK(Gen144_SetSquareRate(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,value));
        CHECK(timer4.PSC==0 && timer4.ARR==72/value-1 && timer4.CCR1==36/value);
        CHECK(timer4.CR1==0x81 && (timer4.CCER&3)==1 && (timer4.CCMR1&255)==0x68);
        CHECK(((gpioa.CRL>>16)&15)==4 && ((gpiob.CRL>>24)&15)==0xb && dac.CR==0);
        Gen144_SetDc(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,0);
        CHECK(!(timer4.CCER&1) && ((gpiob.CRL>>24)&15)==4 && dac.CR==3);
    }
    for (value=0;value<=5;++value) if(value!=1 && value!=2 && value!=4) {
        TIM_TypeDef before4=timer4,before7=timer7;
        DAC_TypeDef beforedac=dac; DMA_Channel_TypeDef beforedma=dma;
        GPIO_TypeDef beforea=gpioa,beforeb=gpiob;
        CHECK(!Gen144_SetSquareRate(&timer4,&timer7,&dma,&dac,&gpioa,&gpiob,value));
        CHECK(!memcmp(&before4,&timer4,sizeof timer4) && !memcmp(&before7,&timer7,sizeof timer7));
        CHECK(!memcmp(&beforedac,&dac,sizeof dac) && !memcmp(&beforedma,&dma,sizeof dma));
        CHECK(!memcmp(&beforea,&gpioa,sizeof gpioa) && !memcmp(&beforeb,&gpiob,sizeof gpiob));
    }
    printf("PASS Wildcat GEN DC/PWM/DC: %u register checks; no analog hardware tested\n",checks);
    return 0;
}
