#ifndef WILDCAT_CLOCK144_H
#define WILDCAT_CLOCK144_H
#include <stdint.h>
#ifdef SCOPE144_HOST_TEST
extern uint32_t native_test_demcr,native_test_dwt;
extern uint32_t NativeTestCycles(void);
#define DEMCR native_test_demcr
#define DWT_CTRL native_test_dwt
#define DWT_CYCLES NativeTestCycles()
#else
#define DEMCR (*(volatile uint32_t *)0xe000edfc)
#define DWT_CTRL (*(volatile uint32_t *)0xe0001000)
#define DWT_CYCLES (*(volatile uint32_t *)0xe0001004)
#endif
#endif
