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
#include "Clock144.h"
#include "Text144.h"
#include "CalText.h"
#include "NativeWindow.h"
#include "UiCommon.h"
#include <string.h>
s16 PPM_Comp=0;
u8 CalibrateMode=0;
typedef struct { s8 zero[10],offset[10];u16 gain[10]; } NativeBank;
typedef struct {
    CalPreview preview;unsigned preview_drawn,preview_range,preview_valid;int32_t preview_uv[2];
    CalMean preview_filtered;unsigned preview_filter_valid,preview_good;
    uint32_t preview_last_good;
    UnifiedCal state;Scope144Bank previous;NativeBank saved[3][2];u16 lo[2],hi[2],difference[2];
    s16 ppm,output[4];u8 sweep,adjust,calmode;
    uint32_t demcr,dwt,irq2,irq4;u16 ccer,ccmr;
} CalWorkspace;
/* The scope is stopped while the wizard owns the existing acquisition RAM. */
typedef char CalWorkspaceFits[(sizeof(CalWorkspace)<=2048*sizeof(DataBuf[0]))?1:-1];
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
static void VoltageCell(unsigned x,unsigned y,u16 color,unsigned mode,int32_t uv)
{
    char n[13];unsigned len;uint32_t v=uv<0?0:(uint32_t)uv;
    if(v<1000000) {
        len=Decimal(n,v/1000);n[len++]='.';n[len++]=(char)('0'+v/100%10);n[len++]='m';
    } else {
        len=Decimal(n,v/1000000);n[len++]='.';
        n[len++]=(char)('0'+v/100000%10);n[len++]=(char)('0'+v/10000%10);n[len++]=(char)('0'+v/1000%10);
    }
    n[len++]='V';n[len]=0;Cell(x,y,color,mode,n,7);
}
static void Table(void)
{
    unsigned x,y,i;const unsigned columns[]={0,44,88,132,200,244,288,332,398};
    __Clear_Screen(BLACK);GPIOC->BRR=0x30;
    for(y=0;y<=200;y+=20)for(x=0;x<399;++x){__Point_SCR(x,y);__LCD_SetPixl(WHT);}
    for(x=0;x<399;++x){__Point_SCR(x,239);__LCD_SetPixl(WHT);}
    for(i=0;i<9;++i)for(y=(i==0 || i==8)?0:20;y<=(i==0 || i==8?239:200);++y){__Point_SCR(columns[i],y);__LCD_SetPixl(WHT);}
    Cell(6,185,YEL,PRN,"CH_A",4);Cell(49,185,YEL,PRN,"ZERO",4);
    Cell(93,185,YEL,PRN,"DIFF",4);Cell(134,185,YEL,PRN,"VOLTAGE",7);
    Cell(206,185,0xFFE0,PRN,"CH_B",4);Cell(249,185,0xFFE0,PRN,"ZERO",4);
    Cell(293,185,0xFFE0,PRN,"DIFF",4);Cell(334,185,0xFFE0,PRN,"VOLTAGE",7);
}
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
        DiagnosticFrame("144 B ZERO/OFFSET");
        PairValue(156,"Raw/CMD B: ",s->capture_mean.b*1000/1024,s->capture_commands[1]);
        PairValue(136,"Expected/error (mADC): ",s->dac_diagnostic.expected[1]*1000/1024,s->dac_diagnostic.residual[1]*1000/1024);
        PairValue(116,"Attempt/limit (mADC): ",s->offset_attempt,s->dac_diagnostic.tolerance*1000/1024);
        Value(96,"Spread B (mADC): ",s->capture_stats.spread_b*1000/1024);
        Value(76,"Wanted ZERO (-255..255): ",s->requested_zero[c]);
        Value(56,"Wanted DIFF (-512..1024): ",s->requested_offset[c]);return;
    }
    DiagnosticFrame("RAW OFFSET DIAGNOSTICS");
    Value(156,"Raw low (mADC): ",s->zero_low[r][c]*1000/1024);
    Value(136,"Raw high (mADC): ",s->zero_high[r][c]*1000/1024);
    Value(116,"Low command: ",(int32_t)s->offset_low_command[c]);
    Value(96,"High command: ",(int32_t)s->offset_high_command[c]);
    Value(76,"Wanted ZERO (-128..127): ",s->requested_zero[c]);
    Value(56,"Wanted DIFF (-128..127): ",s->requested_offset[c]);
}
/* Only the voltage cells change on an idle preview. Reference and button
 * instructions remain visible, and identical numerical values are not redrawn. */
static void RenderPreview(CalWorkspace *w)
{
    UnifiedCal *s=&w->state;unsigned ch,r=s->range,valid=s->measured_valid && !s->diagnostic_path;
    if(r>=8)return;
    for(ch=0;ch<2;++ch) {
        int32_t uv=valid?(int32_t)((int64_t)s->delta[ch]*s->scale[r]*(s->native[ch].gain[r]?s->native[ch].gain[r]:1024)/(1024*1024)):0;
        if(!w->preview_drawn || w->preview_range!=r || w->preview_valid!=valid || w->preview_uv[ch]!=uv) {
            if(valid)VoltageCell(ch?334:134,166-r*20,ch?0xFFE0:YEL,INV,uv);
            else Cell(ch?334:134,166-r*20,ch?0xFFE0:YEL,INV,"--",7);
        }
        w->preview_uv[ch]=uv;
    }
    w->preview_range=r;w->preview_valid=valid;w->preview_drawn=1;
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
    DiagnosticFrame(s->retry==CAL_GROUND?"144 B ZERO/OFFSET":"144 B -> NORMAL A");
    PairValue(164,"Zero LOW A/B: ",s->dac144_low[r].a*1000/1024,s->dac144_low[r].b*1000/1024);
    PairValue(150,"Zero HIGH A/B: ",s->dac144_high[r].a*1000/1024,s->dac144_high[r].b*1000/1024);
    PairValue(136,"Slope A/B (mADC/CMD): ",(d->reason==CAL_DAC_SLOPE?s->gain_result[0]:s->dac144[r][0])*1000/1024,(d->reason==CAL_DAC_SLOPE?s->gain_result[1]:s->dac144[r][1])*1000/1024);
    if(s->retry==CAL_REFERENCE)PairValue(122,"Ref slope A/B: ",s->ref_dac144[0]*1000/1024,s->ref_dac144[1]*1000/1024);
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
static void Render(const UnifiedCal *s,int full)
{
    unsigned r,ch;CalStage phase=s->stage==CAL_ERROR?s->retry:s->stage;
    if(s->stage==CAL_CLOCK){ClockPanel(s,full);return;}
    if(full)Table();
    for(r=0;r<8;++r)for(ch=0;ch<2;++ch) {
        unsigned x=ch?200:0,y=166-r*20,mode=(phase==CAL_REFERENCE || s->error==11) && r==s->range?INV:PRN;
        u16 color=ch?0xFFE0:YEL;
        Cell(x+6,y,color,mode,Y_Attr[r].STR,4);
        NumberCell(x+45,y,color,PRN,s->native[ch].zero[r],5);
        NumberCell(x+89,y,color,PRN,s->native[ch].offset[r],5);
        if(s->complete&(1u<<r))VoltageCell(x+134,y,color,PRN,s->voltage_used[r][ch]);
        else Cell(x+134,y,color,PRN,"--",7);
        if(r==s->range && s->measured_valid && !s->diagnostic_path) {
            int32_t uv=(int32_t)((int64_t)s->delta[ch]*s->scale[r]*(s->native[ch].gain[r]?s->native[ch].gain[r]:1024)/(1024*1024));
            VoltageCell(x+134,y,color,mode,uv);
        }
    }
    if(s->stage==CAL_GROUND) {
        Line(222," CAL A+B: disconnect signal; ground A+B");
        Line(208,s->include144?" K2: ZERO + DIFF for A+B and 144":" K2: ZERO + DIFF for A+B");
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
            const char *prefix=s->capture_path?"144 B Offset R":c?"Offset B R":"Offset A R";
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
            Line(208,s->error==12?"DC blocks did not settle before deadline":s->error==3?(s->capture_mean.clips?"Too many raw 0/255 samples (>4 per ADC)":"ADC mean outside 10..245; see raw limits"):"FIFO/capture failed");
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
        } else {
            Value(222,"Calibration error: ",s->error);
            Line(208,s->error==10?"WPT save failed; original RAM restored":s->error==13?"144 DAC response invalid; check offset data":s->error==14?"Supply changed or outside 2800..5200 mV":"Capture/offset error; check inputs");
        }
        Line(6," K2 return to failed step; K4 cancel");
    }
}
static int Capture(void *user,unsigned range,unsigned mode,unsigned a,unsigned b,CalMean *m)
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
static uint32_t Battery(void *unused){(void)unused;return __Get(V_BATTERY);}
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
    CalOps ops={w,Capture,Generator,Battery,Save};CalKeyGate gate={1};
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
    Initialize(w);
    Generator(w,1);Key_Buffer=0;Render(&w->state,1);
    preview_time=DWT_CYCLES;
    last_stage=w->state.stage;last_range=w->state.range;last_reference=w->state.reference;
    last_ppm=w->state.ppm;last_error=w->state.error;
    while(w->state.stage!=CAL_DONE && w->state.stage!=CAL_CANCELLED) {
        unsigned pressed=(~__Get(KEY_STATUS))&(KEY1_STATUS|KEY2_STATUS|KEY3_STATUS|KEY4_STATUS);
        unsigned key=Key_Buffer;Key_Buffer=0;PD_Cnt=600;
        CalKey event=CalKey_Filter(&gate,Event(key),pressed);
        if(key || pressed)w->preview.phase=0;
        UnifiedCal_Step(&w->state,event,&ops);
        if(key || pressed || w->state.stage!=last_stage || w->state.range!=last_range) {
            w->preview.phase=0;w->preview_filter_valid=0;preview_time=DWT_CYCLES;
            if(w->state.stage!=last_stage || w->state.range!=last_range) {
                w->preview_good=0;
                if(w->state.stage==CAL_REFERENCE)w->state.measured_valid=0;
            }
        }
        if(event==CAL_NONE && !pressed && w->state.stage==CAL_REFERENCE && (uint32_t)(DWT_CYCLES-preview_time)>=18000000u) {
            CalMean m={0,0,0};unsigned r=w->state.range;
            int result=WildcatCalibration_Preview(&w->preview,r,
                CalNative_Command(ADCoffset,w->state.native[0].offset[r],25),
                CalNative_Command(ADCoffset,w->state.native[1].offset[r],25),&m);
            if(result>0 && CalMean_Valid(&m)) {
                /* Display-only low pass, never passed to the K2 capture/fit. */
                if(w->preview_filter_valid) {
                    m.a=w->preview_filtered.a+(m.a-w->preview_filtered.a)/4;
                    m.b=w->preview_filtered.b+(m.b-w->preview_filtered.b)/4;
                }
                w->preview_filtered=m;w->preview_filter_valid=1;
                w->preview_last_good=DWT_CYCLES;w->preview_good=1;
                UnifiedCal_PreviewMean(&w->state,&m);RenderPreview(w);
            }
        }
        /* Brief bad blocks retain the last good display. After 1.5 s even a
         * pending/failed FIFO must visibly expire; never keep stale DC forever. */
        if(w->state.stage==CAL_REFERENCE && w->preview_good &&
           (uint32_t)(DWT_CYCLES-w->preview_last_good)>=108000000u) {
            w->preview_good=w->preview_filter_valid=0;w->state.measured_valid=0;
            RenderPreview(w);
        }
        if(w->state.stage!=last_stage || w->state.range!=last_range || w->state.reference!=last_reference ||
           w->state.ppm!=last_ppm || w->state.error!=last_error) {
            if(w->state.stage!=CAL_DONE && w->state.stage!=CAL_CANCELLED) {
                Render(&w->state,last_stage==CAL_CLOCK || last_stage==CAL_ERROR);w->preview_drawn=0;
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
