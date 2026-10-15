/* Volatile iCE40HX4K configuration for DS203 HW 2.81/2.82.
 * The STM32 flash is only read. The stored W1.1 or W1.2 must match a known CRC before
 * changing FPGA configuration. Host tests replace only the hardware layer.
 */
#include "fpga.h"
#include <stddef.h>
#include <string.h>

#ifdef FPGA_LOADER_HOST_TEST
#include "fpga_loader_host.h"
#define FPGA_ROM_BYTES fpga_test_rom
#define begin_configuration fpga_test_begin
#define spi_transmit fpga_test_transmit
#define configuration_done fpga_test_done
#define end_configuration fpga_test_end
#else
#include <stm32f10x.h>
#include "buttons.h"
#define FPGA_ROM_BYTES ((const uint8_t *)FPGA_ROM_ADDR)
#define SS_PIN (1u << 10)
#define SCK_PIN (1u << 11)
#define SI_PIN (1u << 12)
#define RESET_PIN (1u << 14)
#define DONE_PIN (1u << 15)
#define PIN_DELAY asm("nop"); asm("nop"); asm("nop"); asm("nop");

static bool begin_configuration(uint32_t *original_crh)
{
    *original_crh = GPIOB->CRH;
    GPIOB->CRH = (GPIOB->CRH & 0x000000FF) | 0x83833300;
    GPIOB->BRR = RESET_PIN | SS_PIN;
    GPIOB->BSRR = SCK_PIN;
    delay_ms(2);
    /* CDONE must deassert during reset, not merely remain stuck high. */
    bool reset_ok = (GPIOB->IDR & DONE_PIN) == 0;
    GPIOB->BSRR = RESET_PIN;
    delay_ms(2);
    return reset_ok;
}

static void spi_transmit(uint8_t byte)
{
    for (unsigned bit = 0; bit < 8; bit++) {
        GPIOB->BSRR = (SCK_PIN << 16) |
            ((byte & 0x80) ? SI_PIN : (SI_PIN << 16));
        PIN_DELAY;
        GPIOB->BSRR = SCK_PIN |
            ((byte & 0x80) ? SI_PIN : (SI_PIN << 16));
        PIN_DELAY;
        byte <<= 1;
    }
}

static bool configuration_done(void)
{
    return (GPIOB->IDR & DONE_PIN) != 0;
}

static void end_configuration(uint32_t original_crh)
{
    GPIOB->BSRR = SS_PIN;
    GPIOB->CRH = original_crh;
}
#endif

static uint32_t crc_update(uint32_t crc, const uint8_t *data, unsigned count)
{
    while (count--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
    }
    return crc;
}

uint32_t fpga_rom_crc(void)
{
    uint32_t crc = ~crc_update(0xFFFFFFFFu, FPGA_ROM_BYTES, FPGA_ROM_SIZE);
    return crc == FPGA_ROM_CRC32 || crc == FPGA_ROM_W12_CRC32 ? crc : 0;
}
uint16_t fpga_rom_version(void)
{
    switch (fpga_rom_crc()) {
    case FPGA_ROM_CRC32: return 0x0101;
    case FPGA_ROM_W12_CRC32: return 0x0102;
    default: return 0;
    }
}
bool fpga_restore_available(void)
{
    return fpga_rom_crc() != 0;
}

bool fpga_validate_image(FIL *file, bool check_crc, uint32_t expected_crc)
{
    uint8_t buffer[256];
    UINT got;
    DWORD remaining = f_size(file);
    /* Original HX4K image, or IceStorm image with the four-byte prefix. */
    if (remaining != FPGA_ROM_SIZE && remaining != FPGA_ROM_SIZE + 4)
        return false;
    if (f_lseek(file, 0) != FR_OK ||
        f_read(file, buffer, 8, &got) != FR_OK || got != 8)
        return false;
    static const uint8_t sync[] = {0x7E, 0xAA, 0x99, 0x7E};
    unsigned offset = remaining == FPGA_ROM_SIZE ? 0 : 4;
    if (memcmp(buffer + offset, sync, sizeof(sync)) != 0)
        return false;
    if (f_lseek(file, 0) != FR_OK)
        return false;
    uint32_t crc = 0xFFFFFFFFu;
    while (remaining) {
        UINT want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (f_read(file, buffer, want, &got) != FR_OK || got != want)
            return false;
        crc = crc_update(crc, buffer, got);
        remaining -= got;
    }
    return (!check_crc || ~crc == expected_crc) && f_lseek(file, 0) == FR_OK;
}

static bool configure(FIL *file, bool check_crc, uint32_t expected_crc)
{
    if (!fpga_restore_available())
        return false;
    if (file) {
        if (!fpga_validate_image(file, check_crc, expected_crc))
            return false;
    } else if (check_crc && expected_crc != fpga_rom_crc()) {
        return false;
    }

    uint32_t original_crh;
    bool complete = begin_configuration(&original_crh);
    if (complete && !file) {
        for (unsigned i = 0; i < FPGA_ROM_SIZE; i++)
            spi_transmit(FPGA_ROM_BYTES[i]);
    } else if (complete) {
        uint8_t buffer[256];
        DWORD remaining = f_size(file);
        while (remaining) {
            UINT got, want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (f_read(file, buffer, want, &got) != FR_OK || got != want) {
                complete = false;
                break;
            }
            for (unsigned i = 0; i < got; i++)
                spi_transmit(buffer[i]);
            remaining -= got;
        }
    }
    if (complete) {
        for (unsigned i = 0; i < 500; i++)
            spi_transmit(0); /* Keep the original postamble clocks. */
    }
    bool status = complete && configuration_done();
    end_configuration(original_crh);
    return status;
}

bool fpga_configure(FIL *file)
{
    return configure(file, false, 0);
}

bool fpga_configure_checked(FIL *file, uint32_t expected_crc)
{
    return configure(file, true, expected_crc);
}
