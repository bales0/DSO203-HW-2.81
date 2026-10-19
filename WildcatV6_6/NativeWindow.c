#include "NativeWindow.h"
#include <string.h>
static NativeWindowInput input;
static uint16_t prefix,format,extent;
/* One validity bit per existing DataBuf word, not another sample buffer. */
static uint32_t known[4096/32];
static unsigned loaded,published;
static unsigned previous_visible,previous_first,previous_last;
void NativeWindow_Reset(void){prefix=extent=0;loaded=0;published=0;memset(known,0,sizeof known);}
void NativeWindow_Hide(void){published=0;}
void NativeWindow_Loaded(void){NativeWindow_Reset();loaded=1;}
unsigned NativeWindow_Count(void){return prefix;}
void NativeWindow_Written(unsigned index,unsigned kind,int good)
{
    uint32_t bit;
    published=0;
    if(index>=4096 || kind>2){NativeWindow_Reset();return;}
    if(!index || kind!=format){NativeWindow_Reset();format=(uint16_t)kind;}
    if(index>=extent)extent=(uint16_t)(index+1);
    bit=1u<<(index&31u);
    if(good)known[index>>5]|=bit;else known[index>>5]&=~bit;
    if(!good && index<prefix)prefix=(uint16_t)index;
    while(prefix<extent && (known[prefix>>5]&(1u<<(prefix&31u))))++prefix;
}
/* All interpolation endpoints must be received in this epoch. A bad word
 * elsewhere in the record cannot invalidate a different, complete viewport. */
static int KnownRange(unsigned first,unsigned last)
{
    unsigned word=first>>5,end=last>>5;uint32_t mask=~(uint32_t)0<<(first&31u);
    if(last>=extent)return 0;
    while(word<end){if((known[word]&mask)!=mask)return 0;++word;mask=~(uint32_t)0;}
    mask&=~(uint32_t)0>>(31u-(last&31u));
    return (known[word]&mask)==mask;
}
void NativeWindow_Publish(const NativeWindowInput *next)
{
    published=0;
    if(!next || next->capacity>4096)return;
    input=*next;
    if(loaded){
        prefix=extent=input.capacity;format=input.format;loaded=0;
        memset(known,0,sizeof known);memset(known,0xff,(prefix/32u)*sizeof known[0]);
        if(prefix&31u)known[prefix>>5]=(1u<<(prefix&31u))-1u;
    }
    /* Capacity defines a stable overview axis; known[] separately proves
     * the actual viewport. Unreceived or invalid history is never readable. */
    published=extent>0 && format==input.format && !input.unsupported;
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
    if(begin<0 || end>((int64_t)input.capacity-1)*65536 ||
        !KnownRange((unsigned)(begin/65536),(unsigned)((end+65535)/65536)))return 0;
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
