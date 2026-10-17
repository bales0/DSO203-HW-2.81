#include "NativeWindow.h"
#include <string.h>
static NativeWindowInput input;
static uint16_t prefix,format;
static unsigned loaded,published;
static unsigned previous_visible,previous_first,previous_last;
void NativeWindow_Reset(void){prefix=0;loaded=0;published=0;}
void NativeWindow_Hide(void){published=0;}
void NativeWindow_Loaded(void){NativeWindow_Reset();loaded=1;}
unsigned NativeWindow_Count(void){return prefix;}
void NativeWindow_Written(unsigned index,unsigned kind,int good)
{
    if(!index || kind!=format){prefix=0;format=(uint16_t)kind;loaded=0;}
    if(!good && index<prefix)prefix=(uint16_t)index;
    else if(good && index==prefix && prefix<4096)++prefix;
}
void NativeWindow_Publish(const NativeWindowInput *next)
{
    published=0;
    if(!next || next->capacity>4096)return;
    input=*next;
    if(loaded){prefix=input.capacity;format=input.format;loaded=0;}
    /* A partial prefix is not a shorter capture. Keep the requested record
     * length fixed and publish its overview only once every cell is known. */
    published=prefix>=input.capacity && format==input.format && !input.unsupported;
}
int NativeWindow_Geometry(unsigned width,ViewWindowGeometry *view)
{
    unsigned first,last;int64_t begin,end;uint32_t step;
    if(view)memset(view,0,sizeof *view);
    if(!view || !published || !input.capacity || input.capacity>4096 ||
        width<2 || width>386 || !input.step_q10 || input.step_q10>1024 ||
        input.points>392 || input.format>2)return 0;
    first=input.format?0:input.discard;last=width-1u;
    if(!input.format && last>392u-input.rdiscard)last=392u-input.rdiscard;
    if(first>last || (int)first+input.shift+(input.digital_trigger?5:0)<0 ||
        (int)last+input.shift+(input.digital_trigger?5:0)>=(int)input.points)return 0;
    step=(uint32_t)input.step_q10*64u;
    begin=(int64_t)input.origin*65536+
        ((int64_t)first+input.shift+(input.digital_trigger?5:0))*step;
    end=(int64_t)input.origin*65536+
        ((int64_t)last+input.shift+(input.digital_trigger?5:0))*step;
    if(begin<0 || end>((int64_t)input.capacity-1)*65536)return 0;
    /* Preserve the actual renderer footprint. Never silently clamp it. */
    return ViewWindow_Build(input.capacity,last-first+1u,step,begin,view) && !view->flags;
}
int NativeWindow_Overview(unsigned width,unsigned *first,unsigned *last)
{
    ViewWindowGeometry view;ViewWindowOverview overview;
    if(first)*first=0;
    if(last)*last=0;
    if(!first || !last || !NativeWindow_Geometry(width,&view) ||
        !ViewWindow_Overview(&view,91,216,&overview))return 0;
    *first=overview.first;*last=overview.last;return 1;
}
int NativeWindow_Changed(unsigned width)
{
    unsigned first=0,last=0,visible=(unsigned)NativeWindow_Overview(width,&first,&last);
    int changed=visible!=previous_visible || (visible && (first!=previous_first || last!=previous_last));
    previous_visible=visible;previous_first=first;previous_last=last;return changed;
}
