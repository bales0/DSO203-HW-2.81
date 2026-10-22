#if defined(__GNUC__)
#pragma GCC optimize ("Os")
#endif
/* Shared A+B+144 calibration. Numerical state and button transitions live in
 * UnifiedCal.c. Blink/Twink never change the ADC path or coefficients. */
#include "Interrupt.h"
#include "Function.h"
#include "Calibrat.h"
#include "Process.h"
#include "File.h"
#include "BIOS.h"
#include "Menu.h"
#include "UnifiedCal.h"
#include "CalStable.h"
#include "Clock144.h"
#include "Text144.h"
#include "CalText.h"
#include "NativeWindow.h"
#include "UiCommon.h"
#include <string.h>
s16 PPM_Comp=0;
u8 CalibrateMode=0;
typedef struct { s8 zero[10],offset[10];u16 gain[10]; } NativeBank;
typedef struct {uint32_t mv,sequence,stamp,ready;} CalSupplyLatch;
typedef struct {
    /* Published values survive failed candidates/retry; no persistent fields.
     * 144 Z is B-A ADC, S is (DAC-A slope B/A - 1) in tenths of percent. */
    struct { int32_t value[8][3][3],shown[8][3][3];u16 color[8][3][3];
        u8 zero[3],voltage[3],active,row; } live;
    volatile CalSupplyLatch supply_sample;
    CalPreview preview;unsigned preview_drawn,preview_range,preview_valid;int32_t preview_uv[2];
    CalMean preview_filtered;unsigned preview_filter_valid,preview_good;
    uint32_t preview_last_good[2];
    UnifiedCal state;Scope144Bank previous;NativeBank saved[3][2];u16 lo[2],hi[2],difference[2];
    s16 ppm,output[4];u8 sweep,adjust,calmode;
    uint32_t demcr,dwt,irq2,irq4;u16 ccer,ccmr;
} CalWorkspace;
/* The scope is stopped while the wizard owns the existing acquisition RAM. */
typedef char CalWorkspaceFits[(sizeof(CalWorkspace)<=CAL_HIST_WORK_WORD*sizeof(DataBuf[0]))?1:-1];
static void CopyBanks(CalWorkspace *w,int restore)
{
    s8 *z[3][2]={{Ka1,Kb1},{LKa1,LKb1},{HKa1,HKb1}};
    s8 *o[3][2]={{Ka3,Kb3},{LKa3,LKb3},{HKa3,HKb3}};
    u16 *g[3][2]={{Ka2,Kb2},{LKa2,LKb2},{HKa2,HKb2}};
    unsigned i,ch;
    for(i=0;i<3;++i)for(ch=0;ch<2;++ch) {
        if(restore){memcpy(z[i][ch],w->saved[i][ch].zero,10);memcpy(o[i][ch],w->saved[i][ch].offset,10);memcpy(g[i][ch],w->saved[i][ch].gain,20);}
        else {memcpy(w->saved[i][ch].zero,z[i][ch],10);memcpy(w->saved[i][ch].offset,o[i][ch],10);memcpy(w->saved[i][ch].gain,g[i][ch],20);}
    }
    if(restore){memcpy(LoBatLevel,w->lo,4);memcpy(HiBatLevel,w->hi,4);memcpy(VDiff,w->difference,4);PPM_Comp=w->ppm;}
    else {memcpy(w->lo,LoBatLevel,4);memcpy(w->hi,HiBatLevel,4);memcpy(w->difference,VDiff,4);w->ppm=PPM_Comp;}
}
static void Line(unsigned y,const char *s)
{
    char row[49];Text144_Row(row,s);GPIOC->BRR=0x30;
    __Display_Str(8,y,WHT,PRN,row);
}
static void Value(unsigned y,const char *label,int32_t value)
{
    char row[49],digits[12];unsigned n=0,k=0;uint32_t magnitude;
    while(*label && n<30)row[n++]=*label++;
    if(value<0){row[n++]='-';magnitude=0u-(uint32_t)value;}else magnitude=(uint32_t)value;
    do{digits[k++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);
    while(k)row[n++]=digits[--k];
    row[n]=0;Line(y,row);
}
static void Cell(unsigned x,unsigned y,u16 color,unsigned mode,const char *text,unsigned width)
{
    char row[10];unsigned i=0;
    while(i<width && text[i]){row[i]=text[i];++i;}
    while(i<width)row[i++]=' ';
    row[i]=0;GPIOC->BRR=0x30;__Display_Str(x,y,color,mode,row);
}
static unsigned Decimal(char *s,int32_t value)
{
    char d[12];unsigned n=0,k=0;uint32_t v;
    if(value<0){s[n++]='-';v=0u-(uint32_t)value;}else v=(uint32_t)value;
    do{d[k++]=(char)('0'+v%10);v/=10;}while(v);
    while(k)s[n++]=d[--k];
    s[n]=0;return n;
}
static void NumberCell(unsigned x,unsigned y,u16 color,unsigned mode,int32_t value,unsigned width)
{
    char n[13];Decimal(n,value);Cell(x,y,color,mode,n,width);
}
/* Round the display only; never clamp or alter calibration values. Six
 * characters include sign/unit. Precision decreases only when width needs it. */
static void VoltageCell(unsigned x,unsigned y,u16 color,unsigned mode,int32_t uv)
{
    char text[16];uint32_t magnitude=uv<0?0u-(uint32_t)uv:(uint32_t)uv;
    unsigned unit=magnitude<1000000u?1000u:1000000u,precision=unit==1000?2:3;
    for(;;) {
        unsigned i,n=0,power=1;uint32_t value;
        for(i=0;i<precision;++i)power*=10;
        value=(uint32_t)(((uint64_t)magnitude*power+unit/2)/unit);
        if(uv<0)text[n++]='-';
        n+=Decimal(text+n,(int32_t)(value/power));
        if(precision) {
            unsigned divisor=power/10;text[n++]='.';
            while(divisor){text[n++]=(char)('0'+value/divisor%10);divisor/=10;}
        }
        if(unit==1000)text[n++]='m';
        text[n++]='V';text[n]=0;
        if(n<=6)break;
        if(precision)--precision;else {unit=1000000;precision=3;}
    }
    Cell(x,y,color,mode,text,6);
}
static void SmallRelative(char *text,int32_t value,int32_t divisor)
{
    /* Verified ADC difference <=235*1024, slope change <=3000 tenths %. */
    int32_t scaled=value*10;
    int32_t tenth=(int32_t)((scaled+(scaled<0?-divisor/2:divisor/2))/divisor);unsigned n;
    /* At most four characters: tenths near zero, rounded ADC/% otherwise. */
    if(tenth>=-99 && tenth<=999) {
        n=Decimal(text,tenth/10);
        if(tenth<0 && tenth>-10){text[0]='-';text[1]='0';n=2;}
        text[n++]='.';text[n++]=(char)('0'+(tenth<0?-tenth:tenth)%10);text[n]=0;
    } else Decimal(text,(tenth+(tenth<0?-5:5))/10);
}
static void Table(CalWorkspace *w)
{
    unsigned x,y,i,g;const unsigned columns[]={0,35,70,105,156,191,226,277,312,347,398};
    __Clear_Screen(BLACK);GPIOC->BRR=0x30;memset(w->live.color,0,sizeof(w->live.color));
    for(y=0;y<=200;y+=20)for(x=0;x<399;++x){__Point_SCR(x,y);__LCD_SetPixl(WHT);}
    for(x=0;x<399;++x){__Point_SCR(x,239);__LCD_SetPixl(WHT);}
    for(i=0;i<11;++i)for(y=(i==0 || i==10)?0:20;y<=(i==0 || i==10?239:200);++y){__Point_SCR(columns[i],y);__LCD_SetPixl(WHT);}
    Cell(1,185,WHT,PRN,"RNG",4);
    for(g=0;g<3;++g) {
        unsigned base=36+121*g;
        Cell(base,185,WHT,PRN,g==0?"A:Z":g==1?"B:Z":"dZ",4);
        Cell(base+35,185,WHT,PRN,g==2?"dS%":"D",4);
        Cell(base+70,185,WHT,PRN,g==2?"144V":"V",6);
    }
    for(i=0;i<8;++i)Cell(1,166-i*20,WHT,PRN,Y_Attr[i].STR,4);
}
static void LiveCell(CalWorkspace *w,unsigned r,unsigned g,unsigned k)
{
    UnifiedCal *s=&w->state;unsigned bit=1u<<r,active=w->live.active;
    unsigned valid=(k==2?w->live.voltage[g]:w->live.zero[g])&bit;
    unsigned ongoing=active && w->live.row==r && ((active-1>=4)==(k==2)) &&
        (((active-1)/2%2)?g==2:g<2);
    int32_t value=w->live.value[r][g][k];u16 color=valid?WHT:ongoing?YEL:GRAY;
    char text[16];
    if(!valid && !active && s->stage==CAL_REFERENCE && r==s->range && g<2 && k==2) {
        color=YEL;
        if(w->preview_good&(1u<<g)) {
            value=(int32_t)((int64_t)((g?w->preview_filtered.b:w->preview_filtered.a)-s->zero_ref[r][g])*s->scale[r]*s->native[g].gain[r]/(1024*1024));valid=1;
        }
    }
    if(!valid)value=INT32_MIN;
    if(w->live.color[r][g][k]==color && w->live.shown[r][g][k]==value)return;
    w->live.color[r][g][k]=color;w->live.shown[r][g][k]=value;
    if(value==INT32_MIN)Cell(36+g*121+k*35,166-r*20,color,PRN,"--",k==2?6:4);
    else if(k==2)VoltageCell(106+g*121,166-r*20,color,PRN,value);
    else {
        if(g==2)SmallRelative(text,value,k?10:1024);else Decimal(text,value);
        Cell(36+g*121+k*35,166-r*20,color,PRN,text,4);
    }
}
static __attribute__((section(".text.cal_work"))) void Progress(void *user,const UnifiedCal *s,CalProgress event,int32_t a,int32_t b)
{
    CalWorkspace *w=user;unsigned r=s->range,g,k,ref=event>=CAL_UI_REF_START;
    unsigned first=(event/2%2)?2:0,last=first==2?3:2,bit=1u<<r;
    if(r>=8)return;
    if(event==CAL_UI_REF_VERIFIED) {
        a=(int32_t)((int64_t)s->delta[0]*s->scale[r]*a/(1024*1024));
        b=(int32_t)((int64_t)s->delta[1]*s->scale[r]*b/(1024*1024));
    } else if(event==CAL_UI_144_REF_VERIFIED) {
        const Scope144Cal *c=s->pending144+r;
        /* Same B->A->absolute->uV chain as Frame144/Scope144. */
        int32_t relative=(int32_t)((int64_t)a*c->gain_q10/1024)+c->bias_q10;
        int32_t absolute=(int32_t)((int64_t)relative*c->a_gain_q10/1024)+c->a_bias_q10;
        a=Scope144_Voltage(c,absolute);
    }
    if(event==CAL_UI_ZERO_START) {
        w->live.zero[2]&=(u8)~bit;
        for(g=0;g<3;++g)w->live.voltage[g]&=(u8)~bit;
    }
    if(event==CAL_UI_REF_START || event==CAL_UI_144_ZERO_START)w->live.voltage[2]&=(u8)~bit;
    w->live.row=(u8)r;w->live.active=(u8)event+1;
    for(g=first;g<last;++g) {
        u8 *mask=ref?w->live.voltage+g:w->live.zero+g;
        if(!(event&1))*mask&=(u8)~bit;
        else {
            *mask|=(u8)bit;
            if(ref)w->live.value[r][g][2]=g==1?b:a;
            else if(g<2){w->live.value[r][g][0]=s->native[g].zero[r];w->live.value[r][g][1]=s->native[g].offset[r];}
            else {
                w->live.value[r][g][0]=s->zero144[r][1]-s->zero144[r][0];
                {int32_t slope=s->dac144[r][0];int32_t delta=(s->dac144[r][1]-slope)*1000;
                 w->live.value[r][g][1]=(int32_t)((delta+(delta<0?-slope/2:slope/2))/slope);}
            }
        }
    }
    for(g=0;g<3;++g)for(k=0;k<3;++k)LiveCell(w,r,g,k);
    if(event&1)w->live.active=0;
}
#if defined(SCOPE144_HOST_TEST)
/* Test seam executes the real table/observer against host LCD mocks only. */
void Calibrat_TestLive(const UnifiedCal *s,CalProgress event,int32_t a,int32_t b,int reset)
{
    CalWorkspace *w=(CalWorkspace *)DataBuf;
    if(reset)memset(w,0,sizeof(*w));
    w->state=*s;
    if(reset)Table(w);
    Progress(w,&w->state,event,a,b);
}
#endif
static void Horizontal(unsigned x0,unsigned x1,unsigned y,u16 color)
{
    unsigned x;GPIOC->BRR=0x30;
    for(x=x0;x<=x1;++x){__Point_SCR(x,y);__LCD_SetPixl(color);}
}
static void Box(unsigned x0,unsigned y0,unsigned x1,unsigned y1,u16 color)
{
    unsigned y;Horizontal(x0,x1,y0,color);Horizontal(x0,x1,y1,color);
    for(y=y0;y<=y1;++y){__Point_SCR(x0,y);__LCD_SetPixl(color);__Point_SCR(x1,y);__LCD_SetPixl(color);}
}
static void Caption(unsigned y,u16 color,const char *text)
{
    GPIOC->BRR=0x30;__Display_Str((400-strlen(text)*8)/2,y,color,PRN,(char *)text);
}
static void PairValue(unsigned y,const char *label,int32_t a,int32_t b)
{
    char row[49];unsigned n=0;
    while(*label && n<22)row[n++]=*label++;
    n+=Decimal(row+n,a);row[n++]='/';n+=Decimal(row+n,b);row[n]=0;Line(y,row);
}
static void Counts(unsigned y,const CalCaptureStats *stats)
{
    char row[49]="Records/blocks/window: ";unsigned n=(unsigned)strlen(row);
    n+=Decimal(row+n,stats->records);row[n++]='/';n+=Decimal(row+n,stats->blocks);
    row[n++]='/';n+=Decimal(row+n,stats->window);row[n]=0;Line(y,row);
}
static void ClockPanel(const UnifiedCal *s,int full)
{
    unsigned x,y,marker=(unsigned)(200+s->ppm*140/999);
    if(full) {
        __Clear_Screen(BLACK);Box(16,40,383,231,WHT);
        Horizontal(16,383,200,WHT);Horizontal(16,383,160,WHT);Horizontal(16,383,104,WHT);
        Caption(208,CYAN,"CLOCK CALIBRATION");Caption(180,YEL,"WAVE OUT: 1.000000 MHz");
        Caption(164,WHT,"Connect external frequency counter");
        Caption(142,CYAN,"CORRECTION / PPM");
        Cell(44,54,WHT,PRN,"-999",4);Cell(196,54,WHT,PRN,"0",1);Cell(316,54,WHT,PRN,"+999",4);
        Line(22," Left +/- adjusts measured error (+ if high)");
        Line(6," K2 continue; K4 cancel without saving");
    }
    NumberCell(180,120,YEL,INV,s->ppm,5);
    for(y=75;y<=96;++y)Horizontal(45,354,y,BLACK);
    Horizontal(60,340,80,WHT);
    for(x=60;x<=340;x+=20)for(y=78;y<=83;++y){__Point_SCR(x,y);__LCD_SetPixl(WHT);}
    for(y=77;y<=90;++y){__Point_SCR(200,y);__LCD_SetPixl(CYAN);}
    for(y=78;y<=88;++y)Horizontal(marker-2,marker+2,y,YEL);
}
static void DiagnosticFrame(const char *title)
{
    unsigned y;
    for(y=35;y<199;++y)Horizontal(1,397,y,BLACK);
    Box(0,34,398,199,CYAN);Caption(180,CYAN,title);
}
static void OffsetPanel(const UnifiedCal *s)
{
    unsigned c=s->failed_channel,r=s->range;
    if(r>=8 || c>1)return;
    if(s->capture_path) {
        DiagnosticFrame("144 SHARED DAC A");
        PairValue(156,"Raw/CMD A: ",s->capture_mean.a*1000/1024,s->capture_commands[0]);
        PairValue(136,"Raw B (mADC)/attempt: ",s->capture_mean.b*1000/1024,s->offset_attempt);
        PairValue(116,"Slope A/B (Q10): ",s->dac144[r][0],s->dac144[r][1]);
        Value(96,"Spread B (mADC): ",s->capture_stats.spread_b*1000/1024);
        Line(76,"Both ADCs follow the same DAC A");
        Line(56,"Temporal stability: +/-1 ADC");return;
    }
    DiagnosticFrame("RAW OFFSET DIAGNOSTICS");
    Value(156,"Raw low (mADC): ",s->zero_low[r][c]*1000/1024);
    Value(136,"Raw high (mADC): ",s->zero_high[r][c]*1000/1024);
    Value(116,"Low command: ",(int32_t)s->offset_low_command[c]);
    Value(96,"High command: ",(int32_t)s->offset_high_command[c]);
    Value(76,"Wanted ZERO (-128..127): ",s->requested_zero[c]);
    Value(56,"Wanted DIFF (-128..127): ",s->requested_offset[c]);
}
/* Error-screen measurement only. Reuse the preview's single mean; the LCD
 * retains both columns. Original fit, all coefficients and bank stay intact. */
static __attribute__((noinline,optimize("Os"),section(".text.cal_probe"))) void DacRepeat(CalWorkspace *w)
{
    UnifiedCal *s=&w->state;unsigned k,n,a;int result=1;
    if(s->range>=8 || s->ref144_verify[3].clips>200)return;
    for(k=0;k<4;++k)if(s->ref144_verify[k].a<0 || s->ref144_verify[k].a>200 ||
        (k && s->ref144_verify[k].a<=s->ref144_verify[k-1].a))return;
    a=(unsigned)CalNative_Command(s->adc_offset,s->native[0].offset[s->range],25);
    DiagnosticFrame("144 DAC REPEAT");Value(222,"Fixed A DAC: ",a);
    Line(208,"Same DACs: UP then DOWN; values in mADC");
    Cell(8,158,CYAN,PRN,"DAC",3);Cell(96,158,YEL,PRN,"UP",8);Cell(208,158,YEL,PRN,"DOWN",8);
    for(k=0;k<4;++k) {
        NumberCell(8,138-20*k,CYAN,PRN,s->ref144_verify[k].a,3);
        Cell(96,138-20*k,WHT,PRN,"--",8);Cell(208,138-20*k,WHT,PRN,"--",8);
    }
    Line(6," Measuring - K4 cancel without saving");
    for(n=0;n<8;++n) {
        k=n<4?n:7-n;
        result=WildcatCalibration_Capture(s->range,1,a,(unsigned)s->ref144_verify[k].a,&w->preview_filtered);
        if(result<=0)break;
        if(!CalMean_Valid(&w->preview_filtered)){result=CAL_CAPTURE_CLIPPED;break;}
        NumberCell(n<4?96:208,138-20*k,WHT,PRN,w->preview_filtered.b*1000/1024,8);
    }
    __Set(CH_A_OFFSET,w->preview.commands[0]);__Set(CH_B_OFFSET,w->preview.commands[1]);
    Value(54,"Capture status (1=OK): ",result);
    Line(6," K3 repeat; K2 retry; K4 exit without saving");
    if(result==-1)s->stage=CAL_CANCELLED;
}
/* Only the voltage cells change on an idle preview. Reference and button
 * instructions remain visible, and identical numerical values are not redrawn. */
static void RenderPreview(CalWorkspace *w)
{
    unsigned ch,r=w->state.range;
    if(r<8)for(ch=0;ch<2;++ch)LiveCell(w,r,ch,2);
}
static const char *DacStep(CalDacStep step)
{
    return step==CAL_DAC_DRIFT?"DRIFT":step==CAL_DAC_LOW?"LOW":step==CAL_DAC_HIGH?"HIGH":
        step==CAL_DAC_RETURN?"RETURN":step==CAL_DAC_REFERENCE?"REF":step==CAL_DAC_FIT?"FIT":"CHECK";
}
static void DacPanel(const UnifiedCal *s)
{
    unsigned r=s->range,ch=s->failed_channel;char row[49];unsigned n;
    const CalDacDiagnostic *d=&s->dac_diagnostic;
    if(r>=8 || ch>1)return;
    strcpy(row,s->capture_path?(s->retry==CAL_GROUND?"144 ZERO R":"144 REF R"):(s->retry==CAL_GROUND?"Normal ZERO R":"Normal REF R"));n=(unsigned)strlen(row);
    n+=Decimal(row+n,r+1);strcpy(row+n,ch?"/8 ADC B: ":"/8 ADC A: ");
    strcat(row,d->reason==CAL_DAC_SLOPE?"DAC SLOPE":d->step==CAL_DAC_DRIFT?"DRIFT ERROR":d->step==CAL_DAC_RETURN?"RETURN ERROR":"POINT ERROR");Line(222,row);
    Line(208,s->capture_path?"Both ADCs sample input A; values in mADC":"Separate inputs A/B; values in mADC");
    DiagnosticFrame(s->capture_path?(s->retry==CAL_GROUND?"144 SHARED DAC A":"144 B -> NORMAL A"):
        (s->retry==CAL_GROUND?"NORMAL A+B ZERO":"NORMAL A+B REF"));
    if(s->capture_path) {
        PairValue(164,"Zero LOW A/B: ",s->dac144_low[r].a*1000/1024,s->dac144_low[r].b*1000/1024);
        PairValue(150,"Zero HIGH A/B: ",s->dac144_high[r].a*1000/1024,s->dac144_high[r].b*1000/1024);
        /* If fitting A failed, B has not been fitted in this pass. */
        if(d->reason!=CAL_DAC_SLOPE || ch)PairValue(136,"Slope A/B (mADC/CMD): ",(d->reason==CAL_DAC_SLOPE?s->gain_result[0]:s->dac144[r][0])*1000/1024,(d->reason==CAL_DAC_SLOPE?s->gain_result[1]:s->dac144[r][1])*1000/1024);
        if(s->retry==CAL_REFERENCE && (d->reason!=CAL_DAC_SLOPE || ch))PairValue(122,"Ref slope A/B: ",s->ref_dac144[0]*1000/1024,s->ref_dac144[1]*1000/1024);
    } else {
        if(d->reason!=CAL_DAC_SLOPE)Value(164,"Point (1..4): ",d->point+1);
        if(s->retry==CAL_REFERENCE)PairValue(150,"DC span A/B (mADC): ",s->delta[0]*1000/1024,s->delta[1]*1000/1024);
    }
    PairValue(108,"Point CMD A/B: ",s->capture_commands[0],s->capture_commands[1]);
    if(d->reason==CAL_DAC_SLOPE) {
        Value(94,"Failed ADC slope (mADC/CMD): ",s->gain_result[ch]*1000/1024);
        Line(80,"Allowed slope: 500..2000 mADC/CMD");
    } else {
        PairValue(94,"Raw/expected (mADC): ",(ch?s->capture_mean.b:s->capture_mean.a)*1000/1024,d->expected[ch]*1000/1024);
        PairValue(80,"Error/limit (mADC): ",d->residual[ch]*1000/1024,d->tolerance*1000/1024);
    }
    PairValue(66,"Attempt/clips: ",s->offset_attempt,s->capture_mean.clips);
    PairValue(52,"Spread A/B (mADC): ",s->capture_stats.spread_a*1000/1024,s->capture_stats.spread_b*1000/1024);
}
static const char *RejectName(unsigned reason)
{
    static const char *const names[]={"OK","rails/burst","wide MAD","trim excess",
        "mean bias","slice spread","ADC center","block instability"};
    return reason<sizeof(names)/sizeof(names[0])?names[reason]:"unknown";
}
static void Render(CalWorkspace *w,int full)
{
    UnifiedCal *s=&w->state;unsigned r,g,k;
    if(s->stage==CAL_CLOCK){ClockPanel(s,full);return;}
    if(s->stage==CAL_ERROR)w->live.active=0;
    if(full)Table(w);
    for(r=0;r<8;++r)for(g=0;g<3;++g)for(k=0;k<3;++k)LiveCell(w,r,g,k);
    if(s->stage==CAL_GROUND) {
        Line(222," CAL A+B: disconnect signal; ground A+B");
        Line(208,s->include144?" K2 ZERO A+B+144; dZ:ADC, dS:relative %":" K2: ZERO + DIFF for A+B");
        Line(6," K2 confirm ground; K4 cancel without saving");
    } else if(s->stage==CAL_REFERENCE) {
        {char row[49]="Reference: ",number[20];CalReference_Text(number,s->range,s->reference);strcat(row,number);Line(222,row);}
        Line(208,s->include144?"Apply SAME DC to A+B; K2 measures A+B+144":"Apply SAME DC to A+B; K2 measures A+B");
        Line(6," L +/- reference; R +/- range; K2 OK; K4 exit");
    } else if(s->stage==CAL_SAVE) {
        Line(222," ALL RANGES COMPLETE - NOT SAVED YET");
        Line(208," K2 save BATTERY / K3 save AC ADAPTER");
        Line(6," Common WPT/WPB; K4 exit without saving");
    } else if(s->stage==CAL_ERROR) {
        if(s->error==6 || s->error==7) {
            char row[49];unsigned n=0,c=s->failed_channel;
            const char *prefix=s->error==6?(c?"B DC: dADC=":"A DC: dADC="):"144 DC: dADC=";
            while(*prefix)row[n++]=*prefix++;
            n+=Decimal(row+n,s->delta[c]/1024);memcpy(row+n," gain=",6);n+=6;
            n+=Decimal(row+n,s->gain_result[c]);row[n]=0;Line(222,row);
            Line(208,s->error==7?"144: check ADC span and relative A/B gain":s->delta[c]<8*1024?"Response <8 ADC steps; check input/reference":"Gain must be 512..2048; check DC reference");
        } else if(s->error==13) {
            DacPanel(s);
        } else if(s->error==11) {
            char row[49];unsigned n=0,c=s->failed_channel;
            const char *prefix=s->capture_path?"144 stability R":c?"Offset B R":"Offset A R";
            while(*prefix)row[n++]=*prefix++;
            n+=Decimal(row+n,s->range+1);memcpy(row+n," err(mADC)=",11);n+=11;
            n+=Decimal(row+n,s->offset_residual[c]*1000/1024);row[n]=0;Line(222,row);
            Value(208,s->offset_reason==2?"Unstable; high err(mADC)= ":s->offset_reason==3?"Coeff limit; high err(mADC)= ":"No convergence; high err(mADC)= ",s->offset_high_residual[c]*1000/1024);
            OffsetPanel(s);
        } else if(s->error==5) {
            Value(222,s->failed_channel?"Offset B limit, range: ":"Offset A limit, range: ",s->range+1);
            Line(208,"Stable raw response cannot fit saved format");OffsetPanel(s);
        } else if(s->error==15) {
            Value(222,"Four-point DAC limits, range: ",s->range+1);
            Line(208,"No common DAC/ADC headroom for four points");
            PairValue(164,"Required Y anchors: ",25,100);
            PairValue(150,"Last commands A/B: ",s->capture_commands[0],s->capture_commands[1]);
        } else if(s->error==12 || s->error==3 || s->error==2) {
            Value(222,"Calibration error: ",s->error);
            Line(208,s->error==12?"DC blocks did not settle before deadline":s->error==3?(s->capture_mean.clips?"ADC rails/burst or center outside 10..245":"ADC mean outside 10..245; see raw limits"):"FIFO/capture failed");
            if(s->capture_stats.blocks){char row[49]="A: ";strcat(row,RejectName(s->capture_stats.reason[0]));
                strcat(row," / B: ");strcat(row,RejectName(s->capture_stats.reason[1]));Line(208,row);}
            DiagnosticFrame("RAW CAPTURE DIAGNOSTICS");
            PairValue(164,"Block A/B (mADC): ",s->capture_mean.a*1000/1024,s->capture_mean.b*1000/1024);
            PairValue(150,"Commands A/B: ",s->capture_commands[0],s->capture_commands[1]);
            PairValue(136,"Path/range: ",s->capture_path,s->range+1);
            Counts(122,&s->capture_stats);
            PairValue(108,"Time/block (ms): ",s->capture_stats.elapsed_ms,s->capture_stats.block_ms);
            PairValue(94,"Span A/B (mADC): ",s->capture_stats.spread_a*1000/1024,s->capture_stats.spread_b*1000/1024);
            PairValue(80,"Clip A/B (4096): ",s->capture_stats.clips_a,s->capture_stats.clips_b);
            if(s->capture_stats.blocks) {
                PairValue(66,"Raw min/max A: ",s->capture_stats.min_a,s->capture_stats.max_a);
                PairValue(52,"Raw min/max B: ",s->capture_stats.min_b,s->capture_stats.max_b);
            } else {Line(66,"Raw min/max A: -- (no complete block)");Line(52,"Raw min/max B: -- (no complete block)");}
        } else if(s->error==14) {
            const CalSupply *f=&s->supply;char row[49]="Supply ";
            const char *reason=f->reason==CAL_SUPPLY_ABSOLUTE?"absolute limit":
                f->reason==CAL_SUPPLY_DRIFT?"sustained drift":f->reason==CAL_SUPPLY_USB?"USB mode changed":"stale ADC3";
            strcat(row,s->retry==CAL_GROUND?"ZERO":s->retry==CAL_SAVE?"SAVE":"DC REF");Line(222,row);
            Line(208,reason);DiagnosticFrame("SUPPLY VALIDATION (mV)");
            Value(164,"Range (0=all): ",s->range<8?s->range+1:0);Value(150,"ZERO baseline: ",f->zero_mv);
            Value(136,"REF baseline: ",f->ref_mv);Value(122,"Verified/current: ",f->current_mv);
            Value(108,"Delta/active baseline: ",(int32_t)f->current_mv-(int32_t)(s->retry==CAL_GROUND?f->zero_mv:f->ref_mv));
            PairValue(94,"USB baseline / transients: ",f->usb,f->rejected);
            Line(80,"Allowed: 2800..5200 mV; drift +/-200");
        } else {
            Value(222,"Calibration error: ",s->error);
            Line(208,s->error==10?"WPT save failed; original RAM restored":s->error==13?"144 DAC response invalid; check offset data":s->error==14?"Supply changed or outside 2800..5200 mV":"Capture/offset error; check inputs");
        }
        Line(6,(s->error==11 || s->error==13) && s->capture_path && s->retry==CAL_GROUND?
            " K2 retry; K3 DAC test; K4 cancel":" K2 return to failed step; K4 cancel");
    }
}
static __attribute__((section(".text.cal_work"))) int Capture(void *user,unsigned range,unsigned mode,unsigned a,unsigned b,CalMean *m)
{
    CalWorkspace *w=user;int result;
    char row[49]="Measuring ";unsigned n=(unsigned)strlen(row);
    const char *path=mode?"144":"A+B";
    while(*path)row[n++]=*path++;
    memcpy(row+n," range ",7);n+=7;n+=Decimal(row+n,range+1);
    strcat(row,"/8 - please wait; K4 cancel");Line(6,row);
    {
        char *live=row;unsigned k;
        strcpy(live,mode?(w->state.stage==CAL_GROUND?"144 ZERO R":"144 REF R"):"Normal A+B R");
        k=(unsigned)strlen(live);k+=Decimal(live+k,range+1);strcpy(live+k,"/8 C");k+=4;
        k+=Decimal(live+k,a);live[k++]='/';k+=Decimal(live+k,b);live[k++]=' ';live[k]=0;
        if(mode)strcat(live,DacStep(w->state.dac_diagnostic.step));
        Line(222,live);
    }
    memset(m,0,sizeof(*m));
    result=WildcatCalibration_Capture(range,mode,a,b,m);
    w->preview.commands[0]=a;w->preview.commands[1]=b; /* Last physical setup, not a fit diagnostic point. */
    WildcatCalibration_Diagnostics(&w->state.capture_stats);
    {
        char *live=row;unsigned k=0;
        live[k++]='R';k+=Decimal(live+k,range+1);memcpy(live+k," C",2);k+=2;
        k+=Decimal(live+k,a);live[k++]='/';k+=Decimal(live+k,b);
        memcpy(live+k," A/B:",5);k+=5;k+=Decimal(live+k,m->a*1000/1024);
        live[k++]='/';k+=Decimal(live+k,m->b*1000/1024);
        memcpy(live+k," mADC clip",10);k+=10;k+=Decimal(live+k,w->state.capture_stats.clips_a);
        live[k++]='/';k+=Decimal(live+k,w->state.capture_stats.clips_b);live[k]=0;Line(208,live);
    }
    return result;
}
static int Generator(void *unused,int clock)
{
    (void)unused;Sweep=0;GenAdjustMode=0;_Kind=clock?PWM:6;
    if(clock){_Frqn=18;_Duty=15000;}Update_Output();TIM2->DIER=0;TIM4->DIER=0;return 1;
}
/* TIM3 latches only completed conversions. Reading DR clears EOC. Existing
 * one-second BIOS conversion triggers are preserved; no extra ADC3 starts. */
void Calibrat_SupplyTick(void)
{
    CalWorkspace *w=(void *)DataBuf;
    volatile CalSupplyLatch *v=&w->supply_sample;uint32_t now=DWT_CYCLES;
    if((*(volatile uint32_t *)0x40013c00u)&2u) {
        v->mv=__Get(V_BATTERY);v->stamp=now;++v->sequence;v->ready=1;
    } else if((uint32_t)(now-v->stamp)>108000000u)v->ready=0;
    /* Expiry is sticky until another conversion. A full DWT wrap while
     * idle must never make a stopped ADC look fresh again. */
}
static void SupplySnapshot(CalWorkspace *w,CalSupplySample *out)
{
    uint32_t sequence;
    do {
        sequence=w->supply_sample.sequence;
        out->mv=w->supply_sample.mv;out->stamp=w->supply_sample.stamp;
        out->sequence=w->supply_sample.ready?sequence:0;
    } while(sequence!=w->supply_sample.sequence);
    out->now=DWT_CYCLES;out->usb=__Get(USB_POWER);
}
#if defined(SCOPE144_HOST_TEST)
void Calibrat_TestSupplyLatched(CalSupplySample *out)
{ SupplySnapshot((void *)DataBuf,out); }
unsigned Calibrat_TestWorkspaceBytes(void){return sizeof(CalWorkspace);}
#endif
static int Battery(void *user,CalSupplySample *out)
{
#if defined(SCOPE144_HOST_TEST)
    extern int Calibrat_TestSupplySample(CalSupplySample *);
    (void)user;return Calibrat_TestSupplySample(out);
#else
    if((~__Get(KEY_STATUS))&KEY4_STATUS)return -1;
    SupplySnapshot(user,out);return 1;
#endif
}
static int Save(void *user,UnifiedCal *s,int charger)
{
    CalWorkspace *w=user;unsigned ch;int result;
    s8 *z[2]={Ka1,Kb1},*o[2]={Ka3,Kb3};u16 *g[2]={Ka2,Kb2};
    s8 *bz[2]={charger?HKa1:LKa1,charger?HKb1:LKb1};
    s8 *bo[2]={charger?HKa3:LKa3,charger?HKb3:LKb3};
    u16 *bg[2]={charger?HKa2:LKa2,charger?HKb2:LKb2};
    if(s->battery<2800 || s->battery>5200)return VER_ERR;
    for(ch=0;ch<2;++ch) {
        memcpy(z[ch],s->native[ch].zero,8);memcpy(o[ch],s->native[ch].offset,8);memcpy(g[ch],s->native[ch].gain,16);
        memcpy(bz[ch],s->native[ch].zero,8);memcpy(bo[ch],s->native[ch].offset,8);memcpy(bg[ch],s->native[ch].gain,16);
        if(charger)HiBatLevel[ch]=(u16)s->battery;else LoBatLevel[ch]=(u16)s->battery;
        VDiff[ch]=HiBatLevel[ch]>LoBatLevel[ch]?HiBatLevel[ch]-LoBatLevel[ch]:0;
    }
    PPM_Comp=(s16)s->ppm;Versions=6;
    Line(6,"Saving calibration WPT/WPB - please wait");
    result=File144_WptSave(&s->bank,1);
    if(result)CopyBanks(w,1);else Scope144_NativeBank(&s->bank);
    return result;
}
static CalKey Event(unsigned key)
{
    if(key==KEY2)return CAL_OK;
    if(key==KEY3)return CAL_CHARGER;
    if(key==KEY4)return CAL_CANCEL;
    if(key==K_INDEX_DEC)return CAL_DOWN;
    if(key==K_INDEX_INC)return CAL_UP;
    if(key==K_ITEM_DEC)return CAL_RANGE_DOWN;
    if(key==K_ITEM_INC)return CAL_RANGE_UP;
    return CAL_NONE;
}
/* Keep boot-time import scratch out of the nested save/BIOS call stack. */
static __attribute__((noinline)) void Initialize(CalWorkspace *w)
{
    uint32_t scale[8];unsigned i;
    File144_CalLoad(&w->previous);for(i=0;i<8;++i)scale[i]=Y_Attr[i].SCALE;
    UnifiedCal_Init(&w->state,ADCoffset,PPM_Comp,scale,
        WildcatFpga.kind==FPGA_W12 && Scope144_Crc((const void *)0x0805c800u,135096)==SCOPE144_IMAGE_CRC,&w->previous);
}
void Calibrat(u8 channel)
{
    CalWorkspace *w=(CalWorkspace *)DataBuf;uint32_t before;unsigned i;
    CalOps ops={w,Capture,Generator,Battery,Save,Progress};CalKeyGate gate={1};CalRefHold hold={0};
    uint32_t preview_time;
    CalStage last_stage;unsigned last_range;int32_t last_reference,last_ppm;unsigned last_error;
    volatile unsigned spin;
    (void)channel;
    if(Generator_SecBusy()){Line(160,"UART owns file buffer: select GEN OFF first");UiCommon_Status(UI_FILE_BUSY);return;}
    if(!FpgaProfile_Has(&WildcatFpga,FPGA_CAP_FIFO)) {
        __Clear_Screen(BLACK);Line(160,"Calibration blocked: unknown FPGA");return;
    }
    NativeWindow_Reset(); /* DataBuf becomes calibration workspace. */
    memset(w,0,sizeof(*w));CopyBanks(w,0);
    for(i=0;i<4;++i)w->output[i]=Title[OUTPUT][i].Value;
    w->sweep=Sweep;w->adjust=GenAdjustMode;w->calmode=CalibrateMode;
    w->demcr=DEMCR;w->dwt=DWT_CTRL;w->irq2=TIM2->DIER;w->irq4=TIM4->DIER;
    w->ccer=TIM4->CCER;w->ccmr=TIM4->CCMR1;
    CalibrateMode=1;TIM2->DIER=0;TIM4->DIER=0;DEMCR|=0x01000000u;DWT_CTRL|=1;
    before=DWT_CYCLES;for(spin=0;spin<64;++spin) { }
    if(before==DWT_CYCLES)goto cleanup;
    Initialize(w);(*(volatile uint32_t *)0x40013c00u)&=~2u;
    Generator(w,1);Key_Buffer=0;Render(w,1);
    preview_time=DWT_CYCLES;
    last_stage=w->state.stage;last_range=w->state.range;last_reference=w->state.reference;
    last_ppm=w->state.ppm;last_error=w->state.error;
    while(w->state.stage!=CAL_DONE && w->state.stage!=CAL_CANCELLED) {
        unsigned physical=~__Get(KEY_STATUS);
        unsigned pressed=physical&(KEY1_STATUS|KEY2_STATUS|KEY3_STATUS|KEY4_STATUS);
        unsigned key=Key_Buffer;Key_Buffer=0;PD_Cnt=600;
        CalKey event=CalKey_Filter(&gate,Event(key),pressed);
        event=CalRefHold_Key(&hold,event,w->state.stage,w->state.range,
            ((physical&K_INDEX_D_STATUS)?1u:0u)|((physical&K_INDEX_I_STATUS)?2u:0u),DWT_CYCLES);
        if(key || pressed)w->preview.phase=0;
        if(event==CAL_CHARGER && w->state.stage==CAL_ERROR && (w->state.error==11 || w->state.error==13) &&
           w->state.capture_path && w->state.retry==CAL_GROUND) {
            DacRepeat(w);Key_Buffer=0;
        } else UnifiedCal_Step(&w->state,event,&ops);
        if(key || pressed || w->state.stage!=last_stage || w->state.range!=last_range) {
            w->preview.phase=0;w->preview_filter_valid=0;preview_time=DWT_CYCLES;
            if(w->state.stage!=last_stage || w->state.range!=last_range) {
                w->preview_good=w->preview_filter_valid=0;
                if(w->state.stage==CAL_REFERENCE)w->state.measured_valid=0;
            }
        }
        if(event==CAL_NONE && !pressed && w->state.stage==CAL_REFERENCE && (uint32_t)(DWT_CYCLES-preview_time)>=18000000u) {
            CalMean m={0,0,0};unsigned r=w->state.range;
            int result=WildcatCalibration_PreviewDetailed(&w->preview,r,
                CalNative_Command(ADCoffset,w->state.native[0].offset[r],25),
                CalNative_Command(ADCoffset,w->state.native[1].offset[r],25),&m);
            if(result>0) {
                unsigned ch;uint32_t now=DWT_CYCLES;
                /* Each mask bit certifies only that ADC from the same record.
                 * Never feed a partly valid pair to the calibration state. */
                for(ch=0;ch<2;++ch)if(w->preview.valid_mask&(1u<<ch)) {
                    int32_t *filtered=ch?&w->preview_filtered.b:&w->preview_filtered.a;
                    int32_t value=ch?m.b:m.a;
                    if(w->preview_filter_valid&(1u<<ch))value=*filtered+(value-*filtered)/4;
                    *filtered=value;w->preview_filter_valid|=1u<<ch;w->preview_good|=1u<<ch;
                    w->preview_last_good[ch]=now;
                }
                RenderPreview(w);
            }
        }
        if(w->state.stage==CAL_REFERENCE) {
            unsigned ch,changed=0;uint32_t now=DWT_CYCLES;
            for(ch=0;ch<2;++ch)if((w->preview_good&(1u<<ch)) &&
               (uint32_t)(now-w->preview_last_good[ch])>=108000000u) {
                w->preview_good&=~(1u<<ch);w->preview_filter_valid&=~(1u<<ch);changed=1;
            }
            if(changed)RenderPreview(w);
        }
        if(w->state.stage!=last_stage || w->state.range!=last_range || w->state.reference!=last_reference ||
           w->state.ppm!=last_ppm || w->state.error!=last_error) {
            if(w->state.stage!=CAL_DONE && w->state.stage!=CAL_CANCELLED) {
                Render(w,last_stage==CAL_CLOCK || last_stage==CAL_ERROR);w->preview_drawn=0;
            }
            last_stage=w->state.stage;last_range=w->state.range;last_reference=w->state.reference;
            last_ppm=w->state.ppm;last_error=w->state.error;
        }
    }
cleanup:
    __Set(ADC_CTRL,1);__Set(ADC_MODE,SEPARATE);GPIOC->BRR=0x30;
    for(i=0;i<4;++i)Title[OUTPUT][i].Value=w->output[i];
    Sweep=w->sweep;GenAdjustMode=w->adjust;CalibrateMode=w->calmode;
    DEMCR=w->demcr;DWT_CTRL=w->dwt;TIM4->CCER=w->ccer;TIM4->CCMR1=w->ccmr;
    App_init(1);Update_Range();Update_Base();Update_Output();Update_Trig(_Tr_source==4?0:1);
    TIM2->SR=0;TIM4->SR=0;
    while((__Get(KEY_STATUS)&(KEY1_STATUS|KEY2_STATUS|KEY3_STATUS|KEY4_STATUS))!=(KEY1_STATUS|KEY2_STATUS|KEY3_STATUS|KEY4_STATUS)) { }
    Key_Buffer=0;
}
