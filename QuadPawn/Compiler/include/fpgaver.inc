/** Known FPGA profiles. Capabilities are declared by verified source version,
 * not discovered from hardware. Unknown identities never enable a test.
 */
#if defined _fpgaver_included
    #endinput
#endif
#define _fpgaver_included

#define FPGA_PROFILE_UNKNOWN 0
#define FPGA_PROFILE_W11 1
#define FPGA_PROFILE_W12 2

#define FPGA_FEAT_FIFO (1 << 0)
#define FPGA_FEAT_ADC_PHASE (1 << 1)
#define FPGA_FEAT_OVERSAMPLE (1 << 2)
#define FPGA_FEAT_TRIGGER32 (1 << 3)
#define FPGA_FEAT_FREERUN (1 << 4)
#define FPGA_FEAT_VERSION_TAG (1 << 5)
#define FPGA_W1_BASE_FEATURES (FPGA_FEAT_FIFO | FPGA_FEAT_ADC_PHASE | FPGA_FEAT_OVERSAMPLE | FPGA_FEAT_TRIGGER32 | FPGA_FEAT_FREERUN)

#define FPGA_TEST_ID 0
#define FPGA_TEST_FIFO 1
#define FPGA_TEST_INTERLACE 2
#define FPGA_TEST_OVERSAMPLE 3
#define FPGA_TEST_TRIGGER32 4
#define FPGA_TEST_FREERUN 5

stock fpga_profile_decode(family, protocol, version_tag)
{
    if (family != 0x5731 || protocol != 0x0101)
        return FPGA_PROFILE_UNKNOWN;
    switch (version_tag)
    {
        case 0: return FPGA_PROFILE_W11; // Legacy W1.1-compatible untagged image.
        case 0x0102: return FPGA_PROFILE_W12;
    }
    return FPGA_PROFILE_UNKNOWN;
}

stock fpga_profile_features(profile)
{
    switch (profile)
    {
        case FPGA_PROFILE_W11: return FPGA_W1_BASE_FEATURES;
        case FPGA_PROFILE_W12: return FPGA_W1_BASE_FEATURES | FPGA_FEAT_VERSION_TAG;
    }
    return 0;
}

stock bool:fpga_profile_supports(profile, required)
{
    new available = fpga_profile_features(profile);
    return available != 0 && (available & required) == required;
}

stock bool:fpga_profile_allows_test(profile, test)
{
    switch (test)
    {
        case FPGA_TEST_ID: return fpga_profile_supports(profile, 0);
        case FPGA_TEST_FIFO: return fpga_profile_supports(profile, FPGA_FEAT_FIFO);
        case FPGA_TEST_INTERLACE: return fpga_profile_supports(profile, FPGA_FEAT_FIFO | FPGA_FEAT_ADC_PHASE);
        case FPGA_TEST_OVERSAMPLE: return fpga_profile_supports(profile, FPGA_FEAT_FIFO | FPGA_FEAT_OVERSAMPLE);
        case FPGA_TEST_TRIGGER32: return fpga_profile_supports(profile, FPGA_FEAT_FIFO | FPGA_FEAT_TRIGGER32);
        case FPGA_TEST_FREERUN: return fpga_profile_supports(profile, FPGA_FEAT_FIFO | FPGA_FEAT_FREERUN);
    }
    return false;
}
