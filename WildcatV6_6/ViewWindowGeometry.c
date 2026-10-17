#include "ViewWindowGeometry.h"
#include <string.h>

static int Clear(ViewWindowGeometry *view)
{
    if(view)memset(view,0,sizeof *view);
    return 0;
}

static int Valid(const ViewWindowGeometry *view)
{
    uint64_t end;
    if(!view || !view->capture_count || view->capture_count>VIEW_WINDOW_MAX_SAMPLES ||
        !view->render_width || view->render_width>VIEW_WINDOW_MAX_COLUMNS ||
        !view->step_q16 || !view->visible_columns ||
        view->visible_columns>view->render_width ||
        (view->flags & ~(VIEW_WINDOW_START_CLAMPED|VIEW_WINDOW_SPAN_CLIPPED)))return 0;
    end=(uint64_t)view->window_start_q16+(uint64_t)(view->visible_columns-1u)*view->step_q16;
    return end==view->window_end_q16 &&
        end<=((uint64_t)view->capture_count-1u)*CAPTURE_WINDOW_Q16_ONE;
}

int ViewWindow_Build(uint32_t count,uint32_t width,uint32_t step,int64_t requested,
    ViewWindowGeometry *view)
{
    uint64_t span,last;
    uint32_t maximum_start;
    ViewWindowGeometry next;
    if(!view || !count || count>VIEW_WINDOW_MAX_SAMPLES || !width ||
        width>VIEW_WINDOW_MAX_COLUMNS || !step)return Clear(view);
    memset(&next,0,sizeof next);
    next.capture_count=count;next.render_width=width;next.step_q16=step;
    next.visible_columns=width;
    last=((uint64_t)count-1u)*CAPTURE_WINDOW_Q16_ONE;
    span=(uint64_t)(width-1u)*step;
    if(span>last) {
        next.visible_columns=(uint32_t)(last/step)+1u;
        span=(uint64_t)(next.visible_columns-1u)*step;
        next.flags|=VIEW_WINDOW_SPAN_CLIPPED;
        maximum_start=0;
    } else maximum_start=(uint32_t)(last-span);
    if(requested<0) next.window_start_q16=0;
    else if((uint64_t)requested>maximum_start)next.window_start_q16=maximum_start;
    else next.window_start_q16=(uint32_t)requested;
    if(requested!=(int64_t)next.window_start_q16)next.flags|=VIEW_WINDOW_START_CLAMPED;
    next.window_end_q16=next.window_start_q16+(uint32_t)span;
    *view=next;
    return 1;
}

int ViewWindow_FromCapture(const CaptureWindow *capture,uint32_t width,uint32_t step,
    ViewWindowGeometry *view)
{
    if(!CaptureWindow_Valid(capture))return Clear(view);
    return ViewWindow_Build(capture->time_samples,width,step,capture->first_visible_q16,view);
}

int ViewWindow_Anchor(uint32_t count,uint32_t width,uint32_t step,uint32_t anchor,
    uint32_t column,int64_t displacement,ViewWindowGeometry *view)
{
    int64_t requested;
    if(!count || count>VIEW_WINDOW_MAX_SAMPLES || !width ||
        width>VIEW_WINDOW_MAX_COLUMNS || column>=width || !step ||
        anchor>((uint64_t)count-1u)*CAPTURE_WINDOW_Q16_ONE)return Clear(view);
    requested=(int64_t)anchor-(int64_t)((uint64_t)column*step);
    if(displacement>0 && requested>INT64_MAX-displacement)requested=INT64_MAX;
    else if(displacement<0 && requested<INT64_MIN-displacement)requested=INT64_MIN;
    else requested+=displacement;
    return ViewWindow_Build(count,width,step,requested,view);
}

int ViewWindow_At(const ViewWindowGeometry *view,uint32_t column,ViewWindowSample *sample)
{
    uint32_t position;
    if(sample)memset(sample,0,sizeof *sample);
    if(!sample || !Valid(view) || column>=view->visible_columns)return 0;
    position=(uint32_t)((uint64_t)view->window_start_q16+(uint64_t)column*view->step_q16);
    sample->position_q16=position;
    sample->index=position>>16;
    sample->fraction_q16=position & 65535u;
    sample->next_index=sample->index+(sample->fraction_q16!=0);
    return 1;
}

int ViewWindow_Delta(const ViewWindowGeometry *view,uint32_t column1,uint32_t column2,
    int64_t *delta)
{
    ViewWindowSample first,second;
    if(delta)*delta=0;
    if(!delta || !ViewWindow_At(view,column1,&first) || !ViewWindow_At(view,column2,&second))return 0;
    *delta=(int64_t)second.position_q16-first.position_q16;
    return 1;
}

int ViewWindow_Overview(const ViewWindowGeometry *view,uint32_t x,uint32_t width,
    ViewWindowOverview *overview)
{
    uint64_t count,begin,end;
    if(overview)memset(overview,0,sizeof *overview);
    if(!overview || !Valid(view) || !width || x>UINT32_MAX-(width-1u))return 0;
    count=(uint64_t)view->capture_count*CAPTURE_WINDOW_Q16_ONE;
    begin=(uint64_t)view->window_start_q16*width;
    end=((uint64_t)view->window_end_q16+CAPTURE_WINDOW_Q16_ONE)*width;
    overview->first=x+(uint32_t)(begin/count);
    overview->last=x+((uint32_t)((end+count-1u)/count)-1u);
    return 1;
}
