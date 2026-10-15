#include "Capture144.h"
static Capture144Status Check(const Capture144Ops *ops, uint32_t start, uint32_t timeout)
{
    if (ops->cancelled(ops->user)) return CAPTURE144_CANCELLED;
    if ((uint32_t)(ops->now(ops->user) - start) >= timeout) return CAPTURE144_TIMEOUT;
    return CAPTURE144_OK;
}
Capture144Status Capture144_RecordFifo(const FpgaProfile *profile,
    const Capture144Ops *ops, uint16_t *pairs, uint32_t count, uint32_t timeout)
{
    uint32_t start, i;
    uint16_t prime;
    Capture144Status result;
    if (!profile || !ops || !pairs || !ops->configure || !ops->start ||
        !ops->status || !ops->read_pair || !ops->now || !ops->cancelled ||
        timeout == 0 || timeout > 0x7fffffffu ||
        count < INTERLACE144_DISCARD_PAIRS + INTERLACE144_MIN_PAIRS ||
        count > INTERLACE144_MAX_PAIRS) return CAPTURE144_ARGUMENT;
    if (!FpgaProfile_Has(profile,FPGA_CAP_FIFO)) return CAPTURE144_UNSUPPORTED;
    start = ops->now(ops->user);
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->configure(ops->user)) return CAPTURE144_IO;
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->start(ops->user)) return CAPTURE144_IO;
    do {
        result = Check(ops, start, timeout);
        if (result != CAPTURE144_OK) return result;
    } while ((ops->status(ops->user) & 0x28u) != 0x28u);
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->read_pair(ops->user, &prime)) return CAPTURE144_IO;
    for (i = 0; i < count; ++i) {
        result = Check(ops, start, timeout);
        if (result != CAPTURE144_OK) return result;
        if (!ops->read_pair(ops->user, &pairs[i])) return CAPTURE144_IO;
    }
    return Check(ops, start, timeout);
}

Capture144Status Capture144_Record(const FpgaProfile *profile,
    const Capture144Ops *ops,uint16_t *pairs,uint32_t count,uint32_t timeout)
{
    if(!profile)return CAPTURE144_ARGUMENT;
    if(!Interlace144_Available(profile))return CAPTURE144_UNSUPPORTED;
    return Capture144_RecordFifo(profile,ops,pairs,count,timeout);
}
