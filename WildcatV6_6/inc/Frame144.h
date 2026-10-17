#ifndef WILDCAT_FRAME144_H
#define WILDCAT_FRAME144_H
#include "Scope144.h"
#include "ViewWindowGeometry.h"
/* Small immutable view over exclusively owned DataBuf. No sample copies.
 * Calibration and sample period belong to this epoch, including in HOLD. */
typedef struct {
    const uint32_t *words;
    CaptureWindow capture;
    Scope144Cal calibration;
    unsigned range;
    int32_t digital_shift_q16; /* Frozen Normal-reference C/D alignment. */
    unsigned trigger_source; /* A=0, C=2, D=3; also identifies software edges. */
} Frame144;
#define FRAME144_DIGITAL_SHIFT_WORDS 6u
#define FRAME144_DIGITAL_SHIFT_Q16 (12*65536)
/* One 72 MHz digital value per pair, step-held on the analog Q16 axis.
 * Missing aligned boundaries return false, never a synthetic low edge. */
int Frame144_DigitalAt(const Frame144 *,int64_t analog_q16,unsigned *bits);
int Frame144_DigitalMeasure(const Frame144 *,unsigned channel,Scope144Measurements *);
int Frame144_DigitalTrigger(Frame144 *,unsigned width,unsigned base,unsigned channel,int falling,ViewWindowGeometry *);
void Frame144_Reset(Frame144 *,uint32_t epoch);
int Frame144_Valid(const Frame144 *);
int Frame144_Source(const Frame144 *,Scope144Source *);
int Frame144_Read(const Frame144 *,unsigned sample,int32_t *q,unsigned *clip);
int Frame144_At(const Frame144 *,const ViewWindowGeometry *,unsigned column,int32_t *q);
int Frame144_Measure(const Frame144 *,Scope144Measurements *);
/* cal must already be selected/rebased to the current ADCoffset. Physical
 * input voltage excludes probe scaling and display inversion. Reject levels
 * incompatible with the FPGA's fixed 12-count edge re-arm hysteresis. */
int Frame144_AdcThreshold(const Scope144Cal *,int32_t physical_uv,unsigned *raw);
/* Hardware kind 0..7: edge modes re-arm at +/-12 counts, level/pulse
 * predicates at +/-1. The caller supplies the physical (inverted) kind. */
int Frame144_AdcThresholdKind(const Scope144Cal *,int32_t physical_uv,unsigned kind,unsigned *raw);
int Frame144_HardwareView(Frame144 *,unsigned width,unsigned base,ViewWindowGeometry *);
/* Width counts the track endpoints contributing to visible segments; the
 * unused MAX_X guard is excluded. Displacement is signed temporal Q16 from
 * the clamped unshifted trigger view, or from record start without a trigger. */
int Frame144_Window(const Frame144 *,unsigned width,unsigned base,int64_t displacement,
    ViewWindowGeometry *);
int Frame144_TimeAt(const Frame144 *,const ViewWindowGeometry *,unsigned column,int32_t *ps);
int Frame144_TimeDelta(const Frame144 *,const ViewWindowGeometry *,unsigned first,unsigned second,int32_t *ps);
int Frame144_Trigger(Frame144 *,unsigned width,unsigned base,int32_t threshold,
    int falling,ViewWindowGeometry *);
unsigned Frame144_CsvRow(char out[80],const Frame144 *,unsigned sample,int factor);
void Process144_DrawFrame(const Frame144 *,const ViewWindowGeometry *,unsigned position,unsigned invert);
void Process144_ClearFrame(void);
int File144_CsvFrame(const Frame144 *,unsigned base,unsigned number,int factor);
#endif
