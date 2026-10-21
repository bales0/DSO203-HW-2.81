#ifndef WILDCAT_UNIFIED_CAL_H
#define WILDCAT_UNIFIED_CAL_H
#include "Scope144.h"
#define CAL_CAPTURE_UNSTABLE (-2)
#define CAL_CAPTURE_CLIPPED (-3)
/* Calibration only: discard at most four raw 0/255 samples per ADC/4096. */
#define CAL_CAPTURE_MAX_CLIPS 4u
#define CAL_OFFSET_ATTEMPTS 4u
#define CAL_OFFSET_TOLERANCE (2*1024)
#define CAL_OFFSET_STABILITY 1024
/* Model residual, NOT the three-block temporal stability limit. */
#define CAL_144_POINT_TOLERANCE (2*1024)
#define CAL_144_A_TOLERANCE (2*1024)
/* ADC means in Q10. Nonzero clips rejects a block; tolerated calibration
 * exclusions are counted separately in CalCaptureStats, never hidden there. */
typedef struct { int32_t a,b; unsigned clips; } CalMean;
typedef struct {
    uint32_t records,blocks,window,elapsed_ms,block_ms;
    int32_t spread_a,spread_b; /* latest stability window, Q10 ADC steps */
    uint32_t clips_a,clips_b; /* last complete 4096-pair block, each ADC separately */
    uint16_t min_a,max_a,min_b,max_b; /* same block, raw ADC codes */
} CalCaptureStats;
/* Cooperative calibration acquisition, shared by the K2 capture and preview.
 * Original 100 us/div, SEPARATE ADCs, 4096 consecutive FIFO words, no
 * interlace priming/history protocol. Path 1 only routes ADC B to input A.
 * One step reads at most 32 pairs; completed phase keeps the input setup. */
typedef struct {
    unsigned phase,range,path,commands[2],discard,index,records;
    uint32_t started,record_time,sum_a,sum_b,clips,clips_a,clips_b,block_ms;
    uint16_t min_a,max_a,min_b,max_b;
} CalPreview;

typedef struct { int8_t zero[8],offset[8];uint16_t gain[8]; } CalNative;
/* Retained workspace padding; no independent B144 coefficients are fitted. */
typedef struct { int8_t zero[8],offset[8]; } CalNative144;
typedef enum { CAL_CLOCK,CAL_GROUND,CAL_REFERENCE,CAL_SAVE,CAL_DONE,CAL_CANCELLED,CAL_ERROR } CalStage;
typedef enum { CAL_NONE,CAL_OK,CAL_CHARGER,CAL_CANCEL,CAL_DOWN,CAL_UP,CAL_BIG_DOWN,CAL_BIG_UP,CAL_RANGE_DOWN,CAL_RANGE_UP } CalKey;
typedef struct { unsigned waiting; } CalKeyGate;
typedef enum { CAL_DAC_NONE,CAL_DAC_LOW,CAL_DAC_HIGH,CAL_DAC_POINT,CAL_DAC_RETURN,CAL_DAC_REFERENCE,CAL_DAC_FIT,CAL_DAC_DRIFT } CalDacStep;
typedef enum { CAL_DAC_OK,CAL_DAC_SLOPE,CAL_DAC_RESIDUAL } CalDacReason;
typedef struct {
    CalDacStep step;CalDacReason reason;
    int32_t tolerance;unsigned point;
    int32_t expected[2],residual[2]; /* Q10 raw ADC, never display-rounded */
} CalDacDiagnostic;
typedef struct {
    CalStage stage,retry;unsigned range,include144,complete,error;
    int adc_offset,ppm;uint32_t scale[8],battery;
    int32_t reference_used[8],reference_input[8],voltage_used[8][2];
    int32_t reference,zero_ref[8][2],zero_low[8][2],zero_high[8][2],zero144[8][2];
    int32_t dac144[8][2];
    CalMean dac144_low[8],dac144_high[8];
    uint16_t working_y[8][4],working144_y[8][4];
    /* Current reference curve/verification, kept in the wizard DataBuf. */
    int32_t ref_dac144[2];
    /* ZERO error snapshot: a = actual B DAC, b = raw B Q10, clips = A DAC.
     * Reference verification overwrites all four with ordinary ADC means. */
    CalMean ref144_verify[4];
    CalCaptureStats ref144_stats[4];
    CalDacDiagnostic dac_diagnostic;
    Scope144Cal pending144[8],rollback144[8];
    CalNative native[2];CalNative144 native144_b;Scope144Bank bank;
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
int CalNative_Points(int adc_offset,const int8_t zero[2],const int8_t correction[2],unsigned reference_steps,uint16_t ypos[4]);
int CalNative_Gain(int32_t zero,int32_t high,int32_t reference,uint32_t scale,uint16_t *gain);
int CalMean_Valid(const CalMean *);
void UnifiedCal_Init(UnifiedCal *,int adc_offset,int ppm,const uint32_t scale[8],int include144,const Scope144Bank *);
int UnifiedCal_Preview(UnifiedCal *,const CalOps *);
int UnifiedCal_PreviewMean(UnifiedCal *,const CalMean *);
/* 0 pending; 1 fresh preview; -1 unavailable. Never a calibration acceptance. */
int WildcatCalibration_Preview(CalPreview *,unsigned,unsigned,unsigned,CalMean *);
void UnifiedCal_Step(UnifiedCal *,CalKey,const CalOps *);
/* Hardware adapter; calibration reuses the existing capture buffer. */
int WildcatCalibration_Capture(unsigned,unsigned,unsigned,unsigned,CalMean *);
void WildcatCalibration_Diagnostics(CalCaptureStats *);
#endif
