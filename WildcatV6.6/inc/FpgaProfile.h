#ifndef WILDCAT_FPGA_PROFILE_H
#define WILDCAT_FPGA_PROFILE_H
#include <stdint.h>
typedef enum { FPGA_UNKNOWN, FPGA_W1_LEGACY, FPGA_W11_COMPAT, FPGA_W12 } FpgaKind;
#define FPGA_CAP_FIFO 1u
#define FPGA_CAP_TRIGGER32 2u
#define FPGA_CAP_FREERUN 4u
#define FPGA_CAP_OVERSAMPLE 8u
#define FPGA_CAP_INTERLACE 16u
#define FPGA_CAP_VERSION_TAG 32u
typedef struct {
    uint16_t family, protocol, implementation;
    FpgaKind kind;
    uint32_t capabilities;
} FpgaProfile;
extern FpgaProfile WildcatFpga;
FpgaProfile FpgaProfile_Decode(uint16_t family, uint16_t protocol, uint16_t implementation);
int FpgaProfile_Has(const FpgaProfile *profile, uint32_t required);
const char *FpgaProfile_Name(const FpgaProfile *profile);
#endif
