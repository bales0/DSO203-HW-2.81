#ifndef WILDCAT_SCOPE144_H
#define WILDCAT_SCOPE144_H
#include "Interlace144.h"
#define SCOPE144_RANGES 8u
#define SCOPE144_CAL_BYTES 512u
#define SCOPE144_SAMPLES 1024u
#define SCOPE144_IMAGE_CRC 0x66B80BD2u
#define SCOPE144_OFFSET 40u
/* Physical ADC offsets are fixed. Y position is a display-only operation. */
typedef struct {
    int32_t gain_q10, bias_q10, zero_q10, span_q10, reference_uv;
    uint32_t valid;
} Scope144Cal;
typedef struct { Scope144Cal range[SCOPE144_RANGES]; uint32_t generation; } Scope144Bank;
typedef struct {
    int32_t minimum_uv, maximum_uv, average_uv, rms_uv;
    uint32_t frequency_hz, period_ps, duty_ppm, high_ps, low_ps;
    uint32_t clips, samples;
} Scope144Measurements;
/* Snapshot of native saved clock correction; positive PPM means CPU clock high. */
int Scope144_SetClock(int ppm);
void Scope144_RunKey(int down,int event,int *was_down,int *held);
unsigned Scope144_ClockLine(char out[40]);
uint32_t Scope144_Crc(const void *data, uint32_t count);
uint32_t Scope144_Key(unsigned range);
int Scope144_Valid(const Scope144Cal *cal);
int Scope144_CalibrateMeans(int32_t low_a,int32_t low_b,int32_t high_a,int32_t high_b,int32_t reference,Scope144Cal *);
int Scope144_Calibrate(const FpgaProfile *profile, unsigned range,
    const uint16_t *low, const uint16_t *high, unsigned count,
    int32_t reference_uv, Scope144Cal *cal);
void Scope144_Relative(const Scope144Cal *cal, unsigned range, Interlace144Calibration *relative);
int Scope144_Encode(const Scope144Bank *bank, uint32_t serial, uint8_t out[SCOPE144_CAL_BYTES]);
int Scope144_Decode(const uint8_t in[SCOPE144_CAL_BYTES], uint32_t serial, Scope144Bank *bank);
int Scope144_Newer(uint32_t a, uint32_t b);
int32_t Scope144_Voltage(const Scope144Cal *cal, int32_t q10);
int Scope144_Measure(const Scope144Cal *cal, const int32_t *q10, unsigned count,
    unsigned clips, Scope144Measurements *result);
unsigned Scope144_TimePs(unsigned sample);
/* 30 pixels/div: 0=0.1us, 1=0.2us, 2=0.5us. Positions are sample Q16. */
uint32_t Scope144_Step(unsigned base);
int Scope144_At(const int32_t *q10, unsigned count, uint32_t position_q16, int32_t *value);
int Scope144_Trigger(const Scope144Cal *cal, const int32_t *q10, unsigned count,
    unsigned base, int32_t threshold_uv, int falling, unsigned *start);
int Scope144_Cursors(const Scope144Cal *cal,const int32_t *samples,unsigned start,
    unsigned base,unsigned x1,unsigned x2,int32_t *dt_ps,int32_t *dv_uv);
int Scope144_Scale(const Scope144Measurements *in,int factor,Scope144Measurements *out);
unsigned Scope144_CsvScaled(char out[80],const Scope144Cal *cal,unsigned sample,
    int32_t q10,unsigned raw_clip,int factor);
unsigned Scope144_CsvRow(char out[80], const Scope144Cal *cal, unsigned sample,
    int32_t q10, unsigned raw_clip);
#define SCOPE144_DC 7
#define SCOPE144_AC 8
int Scope144_CouplingStep(int value,int delta,int available);
unsigned Scope144_GroupStep(unsigned group,int delta);
int Scope144_Available(unsigned group,unsigned detail,unsigned kind);
int Wildcat144_Active(void);
unsigned Scope144_DetailLimit(unsigned group);
unsigned Scope144_Field(unsigned group,unsigned detail);
int Wildcat144_Service(void);
int Wildcat144_CouplingKey(void);
void Process144_Draw(const Scope144Cal *cal, const int32_t *samples, unsigned start,
    unsigned base, unsigned position, unsigned invert);
void Menu144_NativeTitle(void);
void Menu144_Meters(const Scope144Measurements *result);
void Menu144_View(const Scope144Measurements *,int32_t dt_ps,int32_t dv_uv,unsigned valid,unsigned fps,unsigned refresh);
int File144_CalLoad(Scope144Bank *bank);
int File144_CalSave(Scope144Bank *bank);
int File144_WptSave(Scope144Bank *bank,int native_updated);
int File144_NativeLoad(void);
int File144_Csv(const Scope144Cal *cal, const int32_t *q10, const uint16_t *pairs,
    unsigned range, unsigned base, unsigned number, int factor);
#endif
