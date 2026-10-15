#ifndef WILDCAT_GEN144_H
#define WILDCAT_GEN144_H
#include "stm32f10x_map.h"
/* Shared by target adapter and host register regression. No fixed addresses.
 * Release TIM4 CH1/PB6 before enabling DAC1/PA4, as SYS ANALOG_ARR does.
 * Only the relevant timer, DMA, DAC and GPIO fields are changed.
 */
static inline void Gen144_SetDc(TIM_TypeDef *timer4, TIM_TypeDef *timer7,
    DMA_Channel_TypeDef *dma, DAC_TypeDef *dac, GPIO_TypeDef *gpioa,
    GPIO_TypeDef *gpiob, unsigned value)
{
    timer4->CR1 &= ~1u;
    timer4->CCER &= ~1u;
    timer7->CR1 &= ~1u;
    dma->CCR &= ~1u;
    dac->CR = 0;
    gpiob->CRL = (gpiob->CRL & ~(0xfu << 24)) | (4u << 24); /* PB6 floating input. */
    gpioa->CRL &= ~(0xfu << 16); /* PA4 analog input mode for DAC. */
    dac->DHR12R1 = value;
    dac->CR = 3; /* DAC1 enabled, unbuffered, no trigger or DMA. */
}
/* Fully define diagnostic 1/2/4MHz PWM state after DC disabled CH1.
 * The caller has masked generator IRQs and restores normal timer state.
 */
static inline int Gen144_SetSquareRate(TIM_TypeDef *timer4, TIM_TypeDef *timer7,
    DMA_Channel_TypeDef *dma, DAC_TypeDef *dac, GPIO_TypeDef *gpioa,
    GPIO_TypeDef *gpiob, unsigned mhz)
{
    if (mhz!=1 && mhz!=2 && mhz!=4) return 0;
    timer4->CR1 &= ~1u;
    timer4->CCER &= ~1u;
    timer7->CR1 &= ~1u;
    dma->CCR &= ~1u;
    dac->CR = 0;
    gpioa->CRL = (gpioa->CRL & ~(0xfu << 16)) | (4u << 16); /* DAC pin released. */
    gpiob->CRL = (gpiob->CRL & ~(0xfu << 24)) | (0xbu << 24); /* PB6 AF push-pull. */
    timer4->PSC = 0;
    timer4->ARR = 72u/mhz-1;
    timer4->CCR1 = 36u/mhz;
    timer4->CCMR1 = (timer4->CCMR1 & ~0xffu) | 0x68u; /* CH1 PWM1 + preload. */
    timer4->CCER = (timer4->CCER & ~3u) | 1u; /* Enable CH1, active high. */
    timer4->CNT = 0;
    timer4->EGR = 1;
    timer4->SR = 0;
    timer4->CR1 = 0x81; /* Upcounter, ARR preload, run. */
    return 1;
}
static inline void Gen144_SetSquare(TIM_TypeDef *timer4, TIM_TypeDef *timer7,
    DMA_Channel_TypeDef *dma, DAC_TypeDef *dac, GPIO_TypeDef *gpioa,
    GPIO_TypeDef *gpiob)
{
    (void)Gen144_SetSquareRate(timer4,timer7,dma,dac,gpioa,gpiob,1);
}
#endif
