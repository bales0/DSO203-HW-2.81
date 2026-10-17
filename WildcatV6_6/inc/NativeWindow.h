#ifndef WILDCAT_NATIVE_WINDOW_H
#define WILDCAT_NATIVE_WINDOW_H
#include "ViewWindowGeometry.h"
/* Metadata only. Counts describe completed Native DataBuf cells; OS cells
 * are aggregates, not raw FIFO words. No acquisition or persistent writes. */
typedef struct {
    uint16_t capacity,points,step_q10;
    int16_t origin,shift;
    uint8_t discard,rdiscard,format,unsupported,digital_trigger;
} NativeWindowInput;
void NativeWindow_Reset(void);
void NativeWindow_Hide(void);
void NativeWindow_Loaded(void);
void NativeWindow_Written(unsigned index,unsigned format,int good);
unsigned NativeWindow_Count(void);
void NativeWindow_Publish(const NativeWindowInput *);
int NativeWindow_Geometry(unsigned width,ViewWindowGeometry *);
int NativeWindow_Overview(unsigned width,unsigned *first,unsigned *last);
int NativeWindow_Changed(unsigned width);
int Process_NativeOverview(unsigned *first,unsigned *last);
int Process_NativeOverviewChanged(void);
#endif
