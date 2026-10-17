#include "Capture144.h"
#include <string.h>
static Capture144Status Check(const Capture144Ops *ops, uint32_t start, uint32_t timeout)
{
    if (ops->cancelled(ops->user)) return CAPTURE144_CANCELLED;
    if ((uint32_t)(ops->now(ops->user) - start) >= timeout) return CAPTURE144_TIMEOUT;
    return CAPTURE144_OK;
}
Capture144Status Capture144_RecordFifo(const FpgaProfile *profile,
    const Capture144Ops *ops, uint16_t *pairs, uint32_t count, uint32_t timeout)
{
    uint32_t start, i;
    uint16_t prime;
    Capture144Status result;
    if (!profile || !ops || !pairs || !ops->configure || !ops->start ||
        !ops->status || !ops->read_pair || !ops->now || !ops->cancelled ||
        timeout == 0 || timeout > 0x7fffffffu ||
        count < INTERLACE144_DISCARD_PAIRS + INTERLACE144_MIN_PAIRS ||
        count > INTERLACE144_MAX_PAIRS) return CAPTURE144_ARGUMENT;
    if (!FpgaProfile_Has(profile,FPGA_CAP_FIFO)) return CAPTURE144_UNSUPPORTED;
    start = ops->now(ops->user);
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->configure(ops->user)) return CAPTURE144_IO;
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->start(ops->user)) return CAPTURE144_IO;
    do {
        result = Check(ops, start, timeout);
        if (result != CAPTURE144_OK) return result;
    } while ((ops->status(ops->user) & 0x28u) != 0x28u);
    result = Check(ops, start, timeout);
    if (result != CAPTURE144_OK) return result;
    if (!ops->read_pair(ops->user, &prime)) return CAPTURE144_IO;
    for (i = 0; i < count; ++i) {
        result = Check(ops, start, timeout);
        if (result != CAPTURE144_OK) return result;
        if (!ops->read_pair(ops->user, &pairs[i])) return CAPTURE144_IO;
    }
    return Check(ops, start, timeout);
}

Capture144Status Capture144_Record(const FpgaProfile *profile,
    const Capture144Ops *ops,uint16_t *pairs,uint32_t count,uint32_t timeout)
{
    if(!profile)return CAPTURE144_ARGUMENT;
    if(!Interlace144_Available(profile))return CAPTURE144_UNSUPPORTED;
    return Capture144_RecordFifo(profile,ops,pairs,count,timeout);
}

static int Overlaps(const void *a,uint32_t size_a,const void *b,uint32_t size_b)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y ? y-x<size_a : x-y<size_b;
}
static Capture144Status WordCheck(const Capture144WordOps *ops,uint32_t start,uint32_t timeout)
{
    if(ops->cancelled(ops->user))return CAPTURE144_CANCELLED;
    if((uint32_t)(ops->now(ops->user)-start)>=timeout)return CAPTURE144_TIMEOUT;
    return CAPTURE144_OK;
}
Capture144Status Capture144_RecordWindow(const FpgaProfile *profile,const Capture144WordOps *ops,
    uint32_t *words,uint32_t count,uint32_t timeout,uint32_t epoch,
    uint64_t period,CaptureWindow *window)
{
    uint32_t start,i,word,status=0;
    Capture144Status result=CAPTURE144_ARGUMENT;
    /* Reject aliases before clearing metadata or touching the destination. */
    if(!window || (words && count<=4096 && Overlaps(window,sizeof *window,words,count*4u)) ||
        (ops && Overlaps(window,sizeof *window,ops,sizeof *ops)) ||
        (profile && Overlaps(window,sizeof *window,profile,sizeof *profile)))return result;
    CaptureWindow_Reset(window,epoch,CAPTURE_WINDOW_ERROR);
    if(!profile || !ops || !words || !ops->configure || !ops->start || !ops->status ||
        !ops->read_word || !ops->now || !ops->cancelled || !period || !timeout || timeout>0x7fffffffu ||
        count<INTERLACE144_DISCARD_PAIRS+INTERLACE144_MIN_PAIRS || count>4096 ||
        Overlaps(words,count*4u,ops,sizeof *ops) || Overlaps(words,count*4u,profile,sizeof *profile))return result;
    if(!Interlace144_Available(profile))return CAPTURE144_UNSUPPORTED;
    window->status=CAPTURE_WINDOW_ARMED;
    start=ops->now(ops->user);
    result=WordCheck(ops,start,timeout);if(result!=CAPTURE144_OK)goto failed;
    if(!ops->configure(ops->user)){result=CAPTURE144_IO;goto failed;}
    result=WordCheck(ops,start,timeout);if(result!=CAPTURE144_OK)goto failed;
    if(!ops->start(ops->user)){result=CAPTURE144_IO;goto failed;}
    do {
        result=WordCheck(ops,start,timeout);if(result!=CAPTURE144_OK)goto failed;
        status=ops->status(ops->user);
    } while((status & 0x2cu)!=0x2cu);
    if(!ops->read_word(ops->user,&word)){result=CAPTURE144_IO;goto failed;}
    for(i=0;i<count;++i) {
        result=WordCheck(ops,start,timeout);if(result!=CAPTURE144_OK)goto failed;
        if(!ops->read_word(ops->user,&word)){result=CAPTURE144_IO;goto failed;}
        /* Native DataBuf uses the legacy u32 typedef, which need not be
         * uint32_t's C type even when both are four bytes. */
        word &= 0x3ffffu;
        memcpy((unsigned char *)words+i*4u,&word,sizeof word);
    }
    result=WordCheck(ops,start,timeout);if(result!=CAPTURE144_OK)goto failed;
    status=ops->status(ops->user);
    if((status & 0x2cu)!=0x2cu){result=CAPTURE144_IO;goto failed;}
    window->sample_period_ps_q16=period;window->fifo_words=count;
    window->valid_begin=INTERLACE144_DISCARD_PAIRS;
    window->valid_words=count-window->valid_begin;window->time_samples=2u*window->valid_words;
    window->fifo_status=status;window->sample_phase=CAPTURE_PHASE_B_A;
    window->flags=CAPTURE_WINDOW_TRUNCATED;window->status=CAPTURE_WINDOW_READY;
    return CAPTURE144_OK;
failed:
    CaptureWindow_Reset(window,epoch,result==CAPTURE144_TIMEOUT?CAPTURE_WINDOW_TIMEOUT:
        result==CAPTURE144_CANCELLED?CAPTURE_WINDOW_CANCELLED:CAPTURE_WINDOW_ERROR);
    return result;
}

static Capture144Status EndSession(Capture144Session *s,Capture144Status result)
{
    uint32_t epoch=s->epoch;
    s->stage=0;
    if(!s->ops.stop(s->ops.io.user))result=CAPTURE144_IO;
    CaptureWindow_Reset(s->window,epoch,result==CAPTURE144_TIMEOUT?CAPTURE_WINDOW_TIMEOUT:
        result==CAPTURE144_CANCELLED?CAPTURE_WINDOW_CANCELLED:CAPTURE_WINDOW_ERROR);
    return result;
}
Capture144Status Capture144_Abort(Capture144Session *s)
{
    if(!s || !s->stage)return CAPTURE144_OK;
    return EndSession(s,CAPTURE144_CANCELLED);
}
Capture144Status Capture144_Begin(Capture144Session *s,const FpgaProfile *profile,
    const Capture144AsyncOps *ops,uint32_t *words,uint32_t epoch,uint64_t period,
    uint32_t wait_timeout,uint32_t read_timeout,CaptureWindow *window)
{
    const Capture144WordOps *io;
    Capture144Status check;
    if(!s || !profile || !ops || !window || !words || s->stage || !period ||
        !wait_timeout || wait_timeout>0x7fffffffu || !read_timeout || read_timeout>0x7fffffffu)
        return CAPTURE144_ARGUMENT;
    io=&ops->io;
    if(!io->configure || !io->start || !io->status || !io->read_word || !io->now ||
        !io->cancelled || !ops->stop ||
        Overlaps(s,sizeof *s,words,16384) || Overlaps(s,sizeof *s,window,sizeof *window) ||
        Overlaps(s,sizeof *s,ops,sizeof *ops) || Overlaps(s,sizeof *s,profile,sizeof *profile) ||
        Overlaps(window,sizeof *window,words,16384) || Overlaps(window,sizeof *window,ops,sizeof *ops) ||
        Overlaps(window,sizeof *window,profile,sizeof *profile) ||
        Overlaps(words,16384,ops,sizeof *ops) || Overlaps(words,16384,profile,sizeof *profile))
        return CAPTURE144_ARGUMENT;
    CaptureWindow_Reset(window,epoch,CAPTURE_WINDOW_ERROR);
    if(!Interlace144_Available(profile))return CAPTURE144_UNSUPPORTED;
    memset(s,0,sizeof *s);s->ops=*ops;s->words=words;s->window=window;s->epoch=epoch;
    s->period=period;s->wait_timeout=wait_timeout;s->read_timeout=read_timeout;s->stage=1;
    s->started=io->now(io->user);window->status=CAPTURE_WINDOW_ARMED;
    window->trigger_kind=CAPTURE_TRIGGER_HARDWARE_A;window->trigger_q16=295u*65536u;
    check=WordCheck(io,s->started,wait_timeout);
    if(check!=CAPTURE144_OK)return EndSession(s,check);
    if(!io->configure(io->user))return EndSession(s,CAPTURE144_IO);
    check=WordCheck(io,s->started,wait_timeout);
    if(check!=CAPTURE144_OK)return EndSession(s,check);
    if(!io->start(io->user))return EndSession(s,CAPTURE144_IO);
    return CAPTURE144_PENDING;
}
int Capture144_DigitalTrigger(Capture144Session *s,unsigned channel,int32_t shift)
{
    if(!s || s->stage!=1 || s->index || channel>1 || shift<-(32*65536) || shift>32*65536)return 0;
    s->window->trigger_kind=(uint8_t)(CAPTURE_TRIGGER_HARDWARE_C+channel);
    s->window->trigger_q16=(uint32_t)(294*65536+shift)-s->trigger_delay*65536u;return 1;
}
int Capture144_TriggerKind(Capture144Session *s,unsigned kind)
{
    if(!s || s->stage!=1 || s->index || kind>8)return 0;
    s->window->trigger_q16+=s->trigger_delay*65536u;
    s->trigger_delay=kind>=4 && kind<=7?2u:0u;
    s->window->trigger_q16-=s->trigger_delay*65536u;
    return 1;
}
Capture144Status Capture144_Poll(Capture144Session *s,unsigned budget)
{
    Capture144Status check;Capture144WordOps *io;uint32_t word,status;unsigned n;
    if(!s || !s->stage)return CAPTURE144_ARGUMENT;
    if(!budget || budget>512)return EndSession(s,CAPTURE144_ARGUMENT);
    io=&s->ops.io;
    if(s->window->epoch!=s->epoch || s->window->status!=CAPTURE_WINDOW_ARMED)
        return EndSession(s,CAPTURE144_IO);
    check=WordCheck(io,s->started,s->stage==1?s->wait_timeout:s->read_timeout);
    if(check!=CAPTURE144_OK)return EndSession(s,check);
    status=io->status(io->user);
    if(s->stage==1) {
        if((status&0x2cu)!=0x2cu)return CAPTURE144_PENDING;
        /* Full is not enough: require the real Start and Ready as well.
         * One separate prime loads Bptr-150. The first two data words are
         * overwritten after Full; the remaining prehistory is valid. */
        s->stage=2;s->started=io->now(io->user);
        if(!io->read_word(io->user,&word))return EndSession(s,CAPTURE144_IO);
    } else if((status&0x2cu)!=0x2cu)return EndSession(s,CAPTURE144_IO);
    for(n=0;n<budget && s->index<4096;++n) {
        check=WordCheck(io,s->started,s->read_timeout);
        if(check!=CAPTURE144_OK)return EndSession(s,check);
        if(!io->read_word(io->user,&word))return EndSession(s,CAPTURE144_IO);
        word &= 0x3ffffu;memcpy((unsigned char *)s->words+4u*s->index,&word,4);++s->index;
    }
    if(s->index!=4096)return CAPTURE144_PENDING;
    check=WordCheck(io,s->started,s->read_timeout);
    if(check!=CAPTURE144_OK)return EndSession(s,check);
    status=io->status(io->user);
    if((status&0x2cu)!=0x2cu)return EndSession(s,CAPTURE144_IO);
    s->window->sample_period_ps_q16=s->period;s->window->fifo_words=4096;
    s->window->valid_begin=2;s->window->valid_words=4094;s->window->time_samples=8188;
    s->window->fifo_status=status;s->window->sample_phase=CAPTURE_PHASE_B_A;
    /* Coordinates were frozen before the first read. Edge/level events are
     * raw word 149; pulse closing edges raw 148. A occupies its odd B->A
     * slot; C/D use their own alignment policy, with the same pulse delay. */
    s->window->flags=CAPTURE_WINDOW_TRUNCATED|CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
    s->window->status=CAPTURE_WINDOW_READY;s->stage=0;
    return CAPTURE144_OK;
}
