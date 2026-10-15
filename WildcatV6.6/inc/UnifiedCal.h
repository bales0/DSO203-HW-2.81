#ifndef WILDCAT_UNIFIED_CAL_H
#define WILDCAT_UNIFIED_CAL_H
#include "Scope144.h"
#define CAL_CAPTURE_UNSTABLE (-2)
#define CAL_CAPTURE_CLIPPED (-3)
#define CAL_OFFSET_ATTEMPTS 4u
#define CAL_OFFSET_TOLERANCE (2*1024)
#define CAL_OFFSET_STABILITY 1024
typedef struct { int32_t a,b; unsigned clips; } CalMean; /* raw ADC, Q10 */
typedef struct {
    uint32_t records,blocks,window,elapsed_ms,block_ms;
    int32_t spread_a,spread_b; /* latest stability window, Q10 ADC steps */
} CalCaptureStats;
typedef struct { int8_t zero[8],offset[8];uint16_t gain[8]; } CalNative;
typedef enum { CAL_CLOCK,CAL_GROUND,CAL_REFERENCE,CAL_SAVE,CAL_DONE,CAL_CANCELLED,CAL_ERROR } CalStage;
typedef enum { CAL_NONE,CAL_OK,CAL_CHARGER,CAL_CANCEL,CAL_DOWN,CAL_UP,CAL_BIG_DOWN,CAL_BIG_UP,CAL_RANGE_DOWN,CAL_RANGE_UP } CalKey;
typedef struct { unsigned waiting; } CalKeyGate;
typedef struct {
    CalStage stage,retry;unsigned range,include144,complete,error;
    int adc_offset,ppm;uint32_t scale[8],battery;
    int32_t reference_used[8],reference_input[8],voltage_used[8][2];
    int32_t reference,zero_ref[8][2],zero_low[8][2],zero_high[8][2],zero144[8][2];
    CalNative native[2];Scope144Bank bank;
    CalMean measured;int32_t delta[2],gain_result[2];
    unsigned measured_valid,failed_channel,diagnostic_path;
    int32_t offset_residual[2],offset_high_residual[2];
    unsigned offset_attempt,offset_reason;
    int32_t requested_zero[2],requested_offset[2];
    unsigned offset_low_command[2],offset_high_command[2];
    CalMean capture_mean;unsigned capture_commands[2],capture_path;CalCaptureStats capture_stats;
} UnifiedCal;
typedef struct {
    void *user;
    /* Configures the entire path, settles, then reads a FRESH record. */
    int (*capture)(void *,unsigned range,unsigned interlace,unsigned aoffset,unsigned boffset,CalMean *);
    int (*generator)(void *,int clock_on);
    uint32_t (*battery)(void *);
    int (*save)(void *,UnifiedCal *,int charger);
} CalOps;
CalKey CalKey_Filter(CalKeyGate *,CalKey,unsigned pressed);
int CalNative_Uncorrect(int display,int ypos,int zero,unsigned gain);
int CalNative_Threshold(int display,int ypos,int zero,unsigned gain,int adc_offset,int inverted);
int CalNative_Correct(int raw_minus_offset,int ypos,int zero,unsigned gain);
int CalNative_Command(int adc_offset,int correction,int ypos);
int CalNative_Solve(int adc_offset,int32_t low,int32_t high,unsigned high_command,int8_t *zero,int8_t *offset);
int CalNative_Gain(int32_t zero,int32_t high,int32_t reference,uint32_t scale,uint16_t *gain);
int CalMean_Valid(const CalMean *);
void UnifiedCal_Init(UnifiedCal *,int adc_offset,int ppm,const uint32_t scale[8],int include144,const Scope144Bank *);
int UnifiedCal_Preview(UnifiedCal *,const CalOps *);
void UnifiedCal_Step(UnifiedCal *,CalKey,const CalOps *);
/* Hardware adapter; calibration reuses the existing capture buffer. */
int WildcatCalibration_Capture(unsigned,unsigned,unsigned,unsigned,CalMean *);
void WildcatCalibration_Diagnostics(CalCaptureStats *);
#endif
