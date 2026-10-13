#pragma once
#include <stdint.h>
#include <stdbool.h>
extern uint8_t fpga_test_rom[135096];
bool fpga_test_begin(uint32_t *);
void fpga_test_transmit(uint8_t);
bool fpga_test_done(void);
void fpga_test_end(uint32_t);
