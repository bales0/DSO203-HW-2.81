#ifndef WILDCAT_UI_COMMON_H
#define WILDCAT_UI_COMMON_H
#include <stdint.h>
#define UI_KEY_SHORT_MS 500u
#define UI_K2_SERVICE_MS 3000u
#define UI_K3_CONFIG_MS 1500u
int UiCommon_ServiceKey(unsigned key);
int UiCommon_ServiceTitle(void);
/* Runtime analog commands; reset after any external owner of input settings. */
void UiCommon_AnalogReset(void);
void UiCommon_AnalogSet(unsigned char object,uint32_t value);
/* Shared actions over the native Title/Detail state. Acquisition is not owned
 * here. The caller reconciles dirty output once, after handling an action. */
/* Signed step; reference_units distinguishes Normal reference pixels from
 * its native interpolated position. 144 maps either decision onto Q16 time. */
int UiCommon_XposStep(int reference,int direction,unsigned accelerated,unsigned middle,
    unsigned *reference_units);
/* Stops and approach use the caller's physical coordinate units. */
int UiCommon_XposApproach(int step,int64_t room,int64_t quantum);
int64_t UiCommon_XposBound(int64_t current,int64_t delta,int64_t minimum,int64_t maximum);
/* offset is temporal Q16 from the theoretical Normal reference (150 px).
 * Native value steps use PSC/ARR timer periods; reference steps use pixels. */
int64_t UiCommon_Xpos144Delta(int64_t offset,unsigned base,unsigned psc,unsigned arr,
    int64_t maximum,int direction,unsigned accelerated,unsigned middle);
typedef struct { unsigned char details,frequency_max; } GeneratorCapability;
extern const GeneratorCapability GeneratorCapabilities[9];
unsigned Generator_Detail(unsigned kind,unsigned detail);
unsigned Generator_NextDetail(unsigned kind,unsigned detail);
int Generator_Key(unsigned key,unsigned interlace);
int UiCommon_ValueKey(unsigned key,unsigned interlace);
unsigned UiCommon_MeterStep(unsigned item,int delta);
unsigned UiCommon_MeterMask(void);
int UiCommon_TimeItem(unsigned item);
unsigned UiCommon_TimeDemand(unsigned required_channels);
const char *UiCommon_MeterLabel(unsigned item);
void UiCommon_HeaderText(unsigned short color,unsigned selected,const char *text);
int UiCommon_NextDetail(unsigned interlace);
void Generator_Reconcile(void);
int Generator_Validate(void);
int Generator_SecBusy(void);
void Generator_UartDiscard(void);
void UiCommon_Status(unsigned status);
void UiCommon_ModeTransition(void);
void UiCommon_FixedTitle(unsigned group,unsigned detail,unsigned mode);
void UiCommon_PercentTitle(unsigned group,unsigned mode);
void UiFile_Begin(void);
void UiFile_Result(unsigned status);
const char *UiCommon_Message(void);
void Generator_Text(char text[9]);
void Generator_Draw(void);
void Generator_FrequencyTitle(void);
void Generator_KindTitle(void);
unsigned Generator_DisplayKind(void);
unsigned short Generator_Color(void);
unsigned short Generator_ValueColor(void);
unsigned short Generator_AuxColor(void);
void UiCommon_HeaderCleanup(void);
unsigned UiCommon_HeaderOwned(void);
unsigned Generator_AuxSelected(void);
void PWMdutyControl(unsigned char direction);
void PWMscaleReset(unsigned char direction);
/* 0 legacy operation, 1 handled navigation, 2 execute selected operation. */
int UiFile_Key(unsigned key,unsigned interlace);
int UiFile_Load(void);
void UiFile_Enter(void);
void UiFile_Draw(void);
unsigned UiFile_Directory(void);
unsigned UiFile_Changed(void);
unsigned TriggerMenu_K2(unsigned mode,unsigned manual);
#define UI_FILE_BUSY 201u
#define UI_ARB_INVALID 202u
#define UI_UART_INVALID 203u
#define UI_UNAVAILABLE 204u
#endif
