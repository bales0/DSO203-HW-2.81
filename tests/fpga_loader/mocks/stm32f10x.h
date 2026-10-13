#pragma once
#include <stdint.h>
typedef struct { uint32_t CRL, CRH, IDR, BSRR, BRR; } GPIO_TypeDef;
extern GPIO_TypeDef mock_a, mock_b, mock_c;
#define GPIOA (&mock_a)
#define GPIOB (&mock_b)
#define GPIOC (&mock_c)
typedef struct { uintptr_t CCR, CNDTR, CPAR, CMAR; } DMAChannel;
typedef struct { uintptr_t IFCR, ISR; } DMAController;
extern DMAChannel mock_channel;
extern DMAController mock_dma;
#define DMA1_Channel4 (&mock_channel)
#define DMA1 (&mock_dma)
#define DMA_IFCR_CTCIF4 1
#define DMA_ISR_TCIF4 1
