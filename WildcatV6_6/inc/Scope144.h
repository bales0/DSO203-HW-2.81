#ifndef WILDCAT_SCOPE144_H
#define WILDCAT_SCOPE144_H
#include "Interlace144.h"
#define SCOPE144_RANGES 8u
#define SCOPE144_CAL_BYTES 1024u
#define SCOPE144_CAL_VERSION 4u
#define SCOPE144_RECORD_BYTES 60u
#define SCOPE144_SAMPLES 1024u
#define SCOPE144_MAX_SAMPLES 8192u
#define SCOPE144_IMAGE_CRC 0x66B80BD2u
#define SCOPE144_PIVOT 40u
#define SCOPE144_NOMINAL 2u /* runtime only; never encodable */
/* Shared Normal hardware pivot; capture calibration owns its DAC state. */
typedef struct {
    int32_t gain_q10, bias_q10, zero_q10, span_q10, reference_uv;
    uint32_t valid;
    int32_t a_gain_q10,a_bias_q10,dac_a_q10,dac_b_q10,adc_offset;
    /* Offset response at the reference voltage, independent of ground. */
    int32_t ref_dac_a_q10,ref_dac_b_q10;
    uint32_t dac_anchor,dac_window; /* A/B anchors + signed native Ka3/Kb3; A/B min/max. Zero means legacy. */
} Scope144Cal;
typedef struct {
    Scope144Cal range[SCOPE144_RANGES],high[SCOPE144_RANGES];
    uint32_t generation,battery[2],native_crc;
} Scope144Bank;
unsigned Scope144_Command(int adc_offset);
int CalNative_Command(int,int,int);
void Scope144_NativeBank(const Scope144Bank *);
unsigned Scope144_NativeProfiles(unsigned);
int CalNative_Interpolate(int,int,unsigned,unsigned,unsigned);
int CalNative_Position(unsigned,unsigned,int,int,int,unsigned,unsigned *);
/* 0 invalid, 1 single profile, 2 two supply profiles. */
int Scope144_Select(const Scope144Bank *,unsigned,int,uint32_t,Scope144Cal *);
int Scope144_SelectAt(const Scope144Bank *,unsigned,int,uint32_t,int,Scope144Cal *,unsigned commands[2]);
int Scope144_Position(unsigned range,int offset,int ypos,unsigned battery,unsigned commands[2]);
int Scope144_Runtime(const Scope144Bank *,unsigned range,int offset,unsigned battery,
    unsigned scale,int *ypos,Scope144Cal *,unsigned commands[2],unsigned *measured);
int Scope144_Normalize(const Scope144Cal *,int32_t *,unsigned);
typedef struct {
    int32_t minimum_uv, maximum_uv, average_uv, rms_uv;
    uint32_t frequency_hz, period_ps, duty_ppm, high_ps, low_ps;
    uint32_t clips, samples;
    uint32_t clip_rails; /* bit 0: low ADC rail; bit 1: high ADC rail */
} Scope144Measurements;
/* read must be deterministic over one call: the owner freezes data, count,
 * calibration and period until all measurement passes have completed.
 * Zero means failure; any positive result means success. The Clipped adapter
 * interprets bit 1 as one raw ADC clipping event; optional bits 2/3
 * distinguish low/high raw rails. Legacy result 2 has unknown direction. */
typedef struct {
    const void *user;
    unsigned count;
    int (*read)(const void *,unsigned,int32_t *);
    uint64_t period_ps_q16; /* zero retains the legacy live-clock calculation */
} Scope144Source;
int Scope144_MeasureSource(const Scope144Cal *,const Scope144Source *,unsigned clips,Scope144Measurements *);
/* Same calculation; clipping is collected only during the amplitude pass. */
int Scope144_MeasureSourceClipped(const Scope144Cal *,const Scope144Source *,Scope144Measurements *);
/* Zero timing omits only the edge pass; its output fields remain zero. */
int Scope144_MeasureSourceRequested(const Scope144Cal *,const Scope144Source *,unsigned timing,Scope144Measurements *);
unsigned Scope144_CsvPoint(char out[80],const Scope144Cal *,unsigned sample,
    uint32_t time_ps,int32_t q,unsigned clip,int factor,char adc);
/* Snapshot of native saved clock correction; positive PPM means CPU clock high. */
int Scope144_SetClock(int ppm);
int Scope144_RunPresses(uint32_t published,uint32_t *seen,int *held);
unsigned Scope144_ClockLine(char out[40]);
unsigned Scope144_ClockValueLine(char out[40],int ppm);
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
/* Original nine trigger choices, FPGA clock stays at 72 MHz. UINT32_MAX
 * rejects B/alternate/invalid inputs. GEN follows Normal's non-sweep >Vt. */
uint32_t Scope144_TriggerMode(unsigned source,unsigned kind,unsigned invert);
uint32_t Scope144_TriggerTicks(unsigned base,unsigned x1,unsigned x2);
/* LEV and its automatic fraction are separate focus stops sharing detail=2.
 * Keep the native four-entry Title array and the optional holdoff intact. */
extern unsigned TriggerLevelSelect;
unsigned TriggerMenu_Next(unsigned detail,unsigned analog,unsigned holdoff);
unsigned TriggerMenu_LevelStep(unsigned mode,int delta);
const char *TriggerMenu_LevelText(unsigned mode);
int32_t TriggerMenu_AutoLevel(int32_t low,int32_t high,unsigned mode);
int NativeTriggerMenu(unsigned key);
void Menu_TriggerLevel(unsigned force);
int32_t Scope144_CursorTime(unsigned base,unsigned x);
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
unsigned Scope144_GroupStep(unsigned group,int delta);
unsigned Scope144_VisibleGroupStep(unsigned group,int delta,unsigned panel,unsigned page);
int Scope144_Available(unsigned group,unsigned detail,unsigned kind);
int Wildcat144_Active(void);
int Wildcat144_DigitalMeasurement(unsigned channel,Scope144Measurements *);
int Wildcat144_Overview(unsigned *first,unsigned *last);
int Wildcat144_CursorTime(unsigned column,int32_t *ps);
int Wildcat144_XposTime(int32_t *ps);
int Wildcat144_TriggerColumn(void);
/* Shared DataBuf has no surviving Normal capture after 144 releases it. */
void Process144_ReleaseBuffer(void);
int Process144_NativePending(void);
unsigned Scope144_DetailLimit(unsigned group);
int Scope144_IsFft(unsigned source);
unsigned Scope144_DSourceStep(unsigned source,int delta);
unsigned Scope144_Field(unsigned group,unsigned detail);
/* Called once at application startup; failure disables only interlace. */
void Wildcat144_CheckImage(void);
int Wildcat144_Service(void);
int Wildcat144_ModeKey(void);
void Menu144_NativeTitle(void);
void Menu144_Cursor(unsigned base);
void Menu144_Calibration(unsigned status);
void Menu144_TriggerWait(unsigned waiting);
void Menu144_Presentation(unsigned available);
void Menu144_MeasurementTime(unsigned available);
void Menu144_Meters(const Scope144Measurements *result);
void Menu144_View(const Scope144Measurements *,int32_t dt_ps,int32_t dv_uv,unsigned valid,unsigned refresh);
int File144_CalLoad(Scope144Bank *bank);
int File144_CalSave(Scope144Bank *bank);
int File144_WptSave(Scope144Bank *bank,int native_updated);
int File144_NativeLoad(void);
int File144_ConfigSave(unsigned number);
int File144_SaveParams(void);
int File144_Csv(const Scope144Cal *cal, const int32_t *q10, const uint16_t *pairs,
    unsigned range, unsigned base, unsigned number, int factor);
#endif
