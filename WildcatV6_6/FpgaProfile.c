#include "FpgaProfile.h"
FpgaProfile WildcatFpga;
FpgaProfile FpgaProfile_Decode(uint16_t family, uint16_t protocol, uint16_t implementation)
{
    FpgaProfile result;
    result.family = family;
    result.protocol = protocol;
    result.implementation = implementation;
    result.kind = FPGA_UNKNOWN;
    result.capabilities = 0;
    if (family != 0x5731 || implementation != 0) {
        if (family != 0x5731 || protocol != 0x0101 || implementation != 0x0102)
            return result;
        result.kind = FPGA_W12;
    } else if (protocol == 0x0101) {
        result.kind = FPGA_W11_COMPAT;
    } else if (protocol == 0) {
        result.kind = FPGA_W1_LEGACY;
    } else {
        return result;
    }
    result.capabilities = FPGA_CAP_FIFO | FPGA_CAP_TRIGGER32 | FPGA_CAP_FREERUN;
    if (result.kind != FPGA_W1_LEGACY)
        result.capabilities |= FPGA_CAP_OVERSAMPLE | FPGA_CAP_INTERLACE;
    if (result.kind == FPGA_W12)
        result.capabilities |= FPGA_CAP_VERSION_TAG;
    return result;
}
int FpgaProfile_Has(const FpgaProfile *profile, uint32_t required)
{
    return profile && profile->kind != FPGA_UNKNOWN &&
           (profile->capabilities & required) == required;
}
const char *FpgaProfile_Name(const FpgaProfile *profile)
{
    if (!profile) return "FPGA unknown";
    switch (profile->kind) {
    case FPGA_W1_LEGACY: return "FPGA W1-compatible";
    case FPGA_W11_COMPAT: return "FPGA W1.1";
    case FPGA_W12: return "FPGA W1.2";
    default: return "FPGA unknown";
    }
}
