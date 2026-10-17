#include "CaptureWindow.h"
#include <string.h>

void CaptureWindow_Reset(CaptureWindow *window,uint32_t epoch,CaptureWindowStatus status)
{
    if(!window)return;
    memset(window,0,sizeof *window);
    window->epoch=epoch;
    window->status=((unsigned)status<=CAPTURE_WINDOW_ERROR &&
        status!=CAPTURE_WINDOW_READY) ? (uint8_t)status : CAPTURE_WINDOW_ERROR;
}

int CaptureWindow_Valid(const CaptureWindow *window)
{
    uint32_t factor,last;
    if(!window || window->status!=CAPTURE_WINDOW_READY ||
        !window->sample_period_ps_q16 || !window->valid_words ||
        window->fifo_words>CAPTURE_WINDOW_FIFO_WORDS ||
        window->valid_begin>window->fifo_words ||
        window->valid_words>window->fifo_words-window->valid_begin ||
        window->sample_phase<CAPTURE_PHASE_NATIVE || window->sample_phase>CAPTURE_PHASE_A_B ||
        window->trigger_kind>CAPTURE_TRIGGER_HARDWARE_D ||
        (window->flags & ~(CAPTURE_WINDOW_PRETRIGGER|CAPTURE_WINDOW_TRUNCATED|
                          CAPTURE_WINDOW_FROZEN|CAPTURE_WINDOW_HAS_TRIGGER)))return 0;
    factor=window->sample_phase==CAPTURE_PHASE_NATIVE ? 1u : 2u;
    if(window->time_samples!=window->valid_words*factor)return 0;
    last=(window->time_samples-1u)*CAPTURE_WINDOW_Q16_ONE;
    if(window->first_visible_q16>last)return 0;
    if(window->flags & CAPTURE_WINDOW_HAS_TRIGGER) {
        if(window->trigger_kind==CAPTURE_TRIGGER_NONE || window->trigger_q16>last)return 0;
    } else if(window->trigger_kind!=CAPTURE_TRIGGER_NONE || window->trigger_q16)return 0;
    if(window->trigger_kind>=CAPTURE_TRIGGER_HARDWARE_A &&
        (window->fifo_status & (CAPTURE_WINDOW_FIFO_READY|CAPTURE_WINDOW_FIFO_FULL|CAPTURE_WINDOW_FIFO_START))!=
        (CAPTURE_WINDOW_FIFO_READY|CAPTURE_WINDOW_FIFO_FULL|CAPTURE_WINDOW_FIFO_START))return 0;
    if((window->flags & CAPTURE_WINDOW_PRETRIGGER) &&
        (!(window->flags & CAPTURE_WINDOW_HAS_TRIGGER) || !window->trigger_q16))return 0;
    return 1;
}
