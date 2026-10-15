/* Low-level FPGA access routines */

#include <stm32f10x.h>
#include <amx.h>
#include <stdint.h>
#include "fpga.h"

#define PINS_COUNT 10
static const struct {
    uint32_t amxpin; // Pin mask used by PAWN code
    GPIO_TypeDef* port; // Pin port used by hardware
    int portpin; // Pin bit used by hardware
} pins[PINS_COUNT] = {
    {0x0001, GPIOB, 0}, // PB0
    {0x0002, GPIOB, 1}, // PB1
    {0x0004, GPIOB, 2}, // PB2
    
    {0x0008, GPIOA, 2}, // PA2
    {0x0010, GPIOA, 3}, // PA3
    {0x0020, GPIOA, 5}, // PA5
    {0x0040, GPIOA, 6}, // PA6
    {0x0080, GPIOA, 7}, // PA7
    
    {0x0100, GPIOC, 4}, // PC4
    {0x0200, GPIOC, 5}  // PC5
};

static const uint32_t default_pins = 0x03C2;

static void set_port_directions(uint32_t outputs)
{
    for (int i = 0; i < PINS_COUNT; i++)
    {
        int mode;
        if (outputs & pins[i].amxpin)
            mode = 3; // 50MHz output pin
        else
            mode = 8; // Input with pull-up/pull-down
        
        volatile uint32_t *conf = (pins[i].portpin < 8) ?
                        (&pins[i].port->CRL) : (&pins[i].port->CRH);
        int shift = (pins[i].portpin & 7) * 4;
        *conf = (*conf & ~(15 << shift)) | (mode << shift);
    }
}

static bool have_custom_image;

static bool restore_original(void)
{
    set_port_directions(0);
    bool status = fpga_configure(NULL);
    if (status) {
        have_custom_image = false;
        set_port_directions(default_pins);
    }
    return status;
}

static cell load_image(AMX *amx, const cell *params, bool check_crc)
{
    char *fname;
    amx_StrParam(amx, params[1], fname);
    uint32_t expected_crc = check_crc ? (uint32_t)params[2] : 0;
    if (!fname[0]) {
        if (check_crc && expected_crc != fpga_rom_crc())
            return false;
        return restore_original();
    }

    /* Opening/preflight failure must leave current configuration and pins alone. */
    FIL file;
    if (f_open(&file, fname, FA_READ) != FR_OK)
        return false;
    if (!fpga_restore_available() ||
        !fpga_validate_image(&file, check_crc, expected_crc)) {
        f_close(&file);
        return false;
    }

    set_port_directions(0);
    have_custom_image = true; /* Set before reset, including failed transfers. */
    bool status = check_crc ? fpga_configure_checked(&file, expected_crc)
                            : fpga_configure(&file);
    f_close(&file);
    if (!status)
        restore_original();
    return status;
}

static cell AMX_NATIVE_CALL amx_fpga_load(AMX *amx, const cell *params)
{
    return load_image(amx, params, false);
}

/* A new native prevents this test application from using an unpatched loader. */
static cell AMX_NATIVE_CALL amx_fpga_restore_ready(AMX *amx, const cell *params)
{
    return fpga_restore_available();
}

static cell AMX_NATIVE_CALL amx_fpga_restore_crc(AMX *amx, const cell *params)
{
    return (cell)fpga_rom_crc();
}
static cell AMX_NATIVE_CALL amx_fpga_restore_version(AMX *amx, const cell *params)
{
    return (cell)fpga_rom_version();
}

static cell AMX_NATIVE_CALL amx_fpga_load_checked(AMX *amx, const cell *params)
{
    return load_image(amx, params, true);
}

int amxcleanup_fpga(void)
{
    return !have_custom_image || restore_original() ? AMX_ERR_NONE : AMX_ERR_NATIVE;
}

static cell AMX_NATIVE_CALL amx_fpga_config_outputs(AMX *amx, const cell *params)
{
    set_port_directions(params[1]);
    return 0;
}

static cell AMX_NATIVE_CALL amx_fpga_read_pins(AMX *amx, const cell *params)
{
    uint32_t mask = params[1];
    uint32_t result = 0;
    for (int i = 0; i < PINS_COUNT; i++)
    {
        if (mask & pins[i].amxpin)
        {
            if (pins[i].port->IDR & (1 << pins[i].portpin))
                result |= pins[i].amxpin;
        }
    }
    return result;
}

static cell AMX_NATIVE_CALL amx_fpga_write_pins(AMX *amx, const cell *params)
{
    uint32_t states = params[1];
    uint32_t mask = params[2];
    for (int i = 0; i < PINS_COUNT; i++)
    {
        if (mask & pins[i].amxpin)
        {
            if (states & pins[i].amxpin)
                pins[i].port->BSRR = (1 << pins[i].portpin);
            else
                pins[i].port->BRR = (1 << pins[i].portpin);
        }
    }
    return 0;
}

static cell AMX_NATIVE_CALL amx_fpga_read(AMX *amx, const cell *params)
{
    DMA1_Channel4->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CTCIF4;
    DMA1_Channel4->CNDTR = params[2];
    DMA1_Channel4->CPAR = 0x64000000; // FPGA memory-mapped address
    DMA1_Channel4->CMAR = params[1];
    DMA1_Channel4->CCR = 0x4981; // Mem2mem, low prio, 16->32bit
    
    while (!(DMA1->ISR & DMA_ISR_TCIF4));
    DMA1_Channel4->CCR = 0;
    
    return 0;
}

static cell AMX_NATIVE_CALL amx_fpga_write(AMX *amx, const cell *params)
{
    DMA1_Channel4->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CTCIF4;
    DMA1_Channel4->CNDTR = params[2];
    DMA1_Channel4->CPAR = 0x64000000; // FPGA memory-mapped address
    DMA1_Channel4->CMAR = params[1];
    DMA1_Channel4->CCR = 0x4991; // Mem2mem, low prio, 16<-32bit
    
    while (!(DMA1->ISR & DMA_ISR_TCIF4));
    DMA1_Channel4->CCR = 0;
    
    return 0;
}

int amxinit_fpga(AMX *amx)
{
    static const AMX_NATIVE_INFO funcs[] = {
        {"fpga_load", amx_fpga_load},
        {"fpga_load_checked", amx_fpga_load_checked},
        {"fpga_restore_ready", amx_fpga_restore_ready},
        {"fpga_restore_crc", amx_fpga_restore_crc},
        {"fpga_restore_version", amx_fpga_restore_version},
        {"fpga_config_outputs", amx_fpga_config_outputs},
        {"fpga_read_pins", amx_fpga_read_pins},
        {"fpga_write_pins", amx_fpga_write_pins},
        {"fpga_read", amx_fpga_read},
        {"fpga_write", amx_fpga_write},
        {0, 0}
    };
    
    if (have_custom_image)
    {
        if (!restore_original())
            return AMX_ERR_NATIVE;
    }
    
    set_port_directions(default_pins);
    
    return amx_Register(amx, funcs, -1);
}

