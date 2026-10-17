#ifndef WILDCAT_UI_COMMON_H
#define WILDCAT_UI_COMMON_H
/* Shared actions over the native Title/Detail state. Acquisition is not owned
 * here. The caller reconciles dirty output once, after handling an action. */
typedef struct { unsigned char details,frequency_max; } GeneratorCapability;
extern const GeneratorCapability GeneratorCapabilities[9];
unsigned Generator_Detail(unsigned kind,unsigned detail);
unsigned Generator_NextDetail(unsigned kind,unsigned detail);
int Generator_Key(unsigned key,unsigned interlace);
int UiCommon_ValueKey(unsigned key,unsigned interlace);
void Generator_Reconcile(void);
int Generator_Validate(void);
int Generator_SecBusy(void);
void Generator_UartDiscard(void);
void UiCommon_Status(unsigned status);
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
#ifdef GENERATOR_DIAGNOSTICS
#include "Clock144.h"
typedef struct {
    unsigned irq_calls[2],irq_max_cycles[2],poll_calls,poll_max_cycles,poll_gap_cycles,poll_timeouts,last_poll;
} GeneratorRuntimeStats;
extern volatile GeneratorRuntimeStats GeneratorRuntime;
void Generator_IrqSample(unsigned irq,unsigned cycles);
void Generator_PollSample(unsigned started,unsigned cycles,unsigned timeout);
#define GEN_IRQ_BEGIN unsigned gen_irq_started=DWT_CYCLES
#define GEN_IRQ_END(n) Generator_IrqSample(n,DWT_CYCLES-gen_irq_started)
#else
#define GEN_IRQ_BEGIN ((void)0)
#define GEN_IRQ_END(n) ((void)0)
#endif
#endif
