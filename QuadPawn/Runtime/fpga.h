/* FPGA configuration loading for DS203 HW 2.81/2.82. */
#pragma once
#include <ff.h>
#include <stdbool.h>
#include <stdint.h>

#define FPGA_ROM_ADDR 0x0805C800u
#define FPGA_ROM_SIZE 135096u
#define FPGA_ROM_CRC32 0x9FB87F03u

bool fpga_restore_available(void);
bool fpga_validate_image(FIL *file, bool check_crc, uint32_t expected_crc);
bool fpga_configure(FIL *file);
bool fpga_configure_checked(FIL *file, uint32_t expected_crc);
