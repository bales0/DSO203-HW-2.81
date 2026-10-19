#include "Frame144.h"
#include <string.h>
#include <limits.h>
static int clock_ppm;
static uint32_t Sqrt(uint64_t value)
{
    uint64_t root=0,bit=(uint64_t)1<<62;
    while(bit>value) bit>>=2;
    while(bit) { if(value>=root+bit) { value-=root+bit; root=(root>>1)+bit; } else root>>=1; bit>>=2; }
    return (uint32_t)root;
}
int Legacy_MeasureSource(const Scope144Cal *cal,const Scope144Source *source,unsigned clips,Scope144Measurements *out)
{
    Scope144Measurements m; unsigned i,count,edges=0; int64_t sum=0,center,d;
    uint64_t square=0,first=0,last=0,pos,high=0,rise=0; int32_t lo,hi,threshold,q,previous;
    if(!Scope144_Valid(cal) || !source || !source->read || !out || source->count<16 ||
        source->count>SCOPE144_MAX_SAMPLES || source->period_ps_q16>8000ULL*65536 ||
        (source->period_ps_q16 && source->period_ps_q16<6000ULL*65536)) return 0;
    count=source->count;
    if(!source->read(source->user,0,&q))return 0;
    lo=hi=q;
    for(i=0;i<count;++i) {
        /* Acquisition corrections have a tighter bound than arbitrary int32 input. */
        if(!source->read(source->user,i,&q) || q<-4000000 || q>4000000) return 0;
        if(q<lo)lo=q;
        if(q>hi)hi=q;
        d=(int64_t)q-cal->zero_q10; sum+=d; square+=(uint64_t)(d*d);
    }
    memset(&m,0,sizeof(m)); m.samples=count; m.clips=clips;
    m.minimum_uv=Scope144_Voltage(cal,lo); m.maximum_uv=Scope144_Voltage(cal,hi);
    center=sum/count; m.average_uv=Scope144_Voltage(cal,(int32_t)center+cal->zero_q10);
    {uint64_t rms=(uint64_t)Sqrt(square/count)*cal->reference_uv/cal->span_q10;
     m.rms_uv=rms>INT32_MAX?INT32_MAX:(int32_t)rms;}
    threshold=lo+(hi-lo)/2;
    if(!source->read(source->user,0,&previous))return 0;
    for(i=1;i<count;++i) {
        if(!source->read(source->user,i,&q))return 0;
        if(previous<threshold && q>=threshold && hi-lo>=10*1024) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
            if(!edges)first=pos;
            last=pos; ++edges;
        }
        previous=q;
    }
    if(edges>1 && !clips) {
        uint64_t period=(last-first)/(edges-1);
        if(source->period_ps_q16) {
            m.period_ps=(uint32_t)((period*source->period_ps_q16+0x80000000ULL)>>32);
            if(!m.period_ps)return 0;
            m.frequency_hz=(uint32_t)((1000000000000ULL+m.period_ps/2)/m.period_ps);
        } else {
            m.frequency_hz=(uint32_t)(((uint64_t)(144u*(1000000+clock_ppm))<<16)/period);
            m.period_ps=(uint32_t)(period*62500*1000000/((uint64_t)9*65536*(1000000+clock_ppm)));
        }
        /* Integrate high time over complete cycles only; exclude record ends. */
        rise=first;
        if(!source->read(source->user,0,&previous))return 0;
        for(i=1;i<count;++i) {
            if(!source->read(source->user,i,&q))return 0;
            if(previous<threshold && q>=threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
                if(pos>=first && pos<=last)rise=pos;
            } else if(previous>=threshold && q<threshold) {
                pos=((uint64_t)(i-1)<<16)+((uint64_t)(previous-threshold)<<16)/(previous-q);
                if(pos>first && pos<=last && pos>=rise)high+=pos-rise;
            }
            previous=q;
        }
        m.duty_ppm=(uint32_t)(high*1000000/(last-first));
        m.high_ps=(uint32_t)((uint64_t)m.period_ps*m.duty_ppm/1000000);
        m.low_ps=m.period_ps-m.high_ps;
    }
    *out=m; return 1;
}

static uint32_t BaselineWord(const Frame144 *frame,unsigned pair)
{
    uint32_t word;
    /* Byte access respects both the legacy DataBuf u32 objects and uint32_t
     * host fixtures. The compiler reduces this fixed-size copy to a load. */
    memcpy(&word,(const unsigned char *)frame->words+pair*4u,sizeof word);
    return word;
}
static int BaselineRead(const void *user,unsigned sample,int32_t *q)
{
    const Frame144 *frame=user;
    const Scope144Cal *cal=&frame->calibration;
    uint32_t word,raw;int32_t relative;int64_t value;int is_a;
    if(!q || sample>=frame->capture.time_samples)return 0;
    word=BaselineWord(frame,frame->capture.valid_begin+sample/2u);
    is_a=(sample & 1u)==(frame->capture.sample_phase==CAPTURE_PHASE_B_A);
    raw=is_a?(word & 255u):((word>>8)&255u);
    relative=is_a?(int32_t)raw*1024:(int32_t)raw*cal->gain_q10+cal->bias_q10;
    value=(int64_t)relative*cal->a_gain_q10/1024+cal->a_bias_q10;
    if(value<INT32_MIN || value>INT32_MAX)return 0;
    *q=(int32_t)value;return 1;
}
int Baseline_Source(const Frame144 *frame,Scope144Source *source)
{
    if(source)memset(source,0,sizeof *source);
    if(!source || !Frame144_Valid(frame))return 0;
    source->user=frame;source->count=frame->capture.time_samples;source->read=BaselineRead;
    source->period_ps_q16=frame->capture.sample_period_ps_q16;return 1;
}
int Baseline_FrameRead(const Frame144 *frame,unsigned sample,int32_t *q,unsigned *clip)
{
    unsigned word,raw;int is_a;
    if(!q || !Frame144_Valid(frame) || sample>=frame->capture.time_samples)return 0;
    if(!BaselineRead(frame,sample,q))return 0;
    word=BaselineWord(frame,frame->capture.valid_begin+sample/2u);
    is_a=(sample & 1u)==(frame->capture.sample_phase==CAPTURE_PHASE_B_A);
    raw=is_a?(word & 255u):((word>>8)&255u);
    if(clip)*clip=raw==0 || raw==255;
    return 1;
}
/* P0 oracle: unchanged two-pass math and separate raw clipping scan. */
int Baseline_MeasureSource(const Scope144Cal *cal,const Scope144Source *source,unsigned clips,Scope144Measurements *out)
{
    Scope144Measurements m; unsigned i,count,edges=0; int64_t sum=0,center,d;
    uint64_t square=0,first=0,last=0,pos,high=0,rise=0,pending_high=0; int32_t lo,hi,threshold,q,previous;
    if(!Scope144_Valid(cal) || !source || !source->read || !out || source->count<16 ||
        source->count>SCOPE144_MAX_SAMPLES || source->period_ps_q16>8000ULL*65536 ||
        (source->period_ps_q16 && source->period_ps_q16<6000ULL*65536)) return 0;
    count=source->count;
    if(!source->read(source->user,0,&q))return 0;
    lo=hi=q;
    for(i=0;i<count;++i) {
        /* Acquisition corrections have a tighter bound than arbitrary int32 input. */
        if(!source->read(source->user,i,&q) || q<-4000000 || q>4000000) return 0;
        if(q<lo)lo=q;
        if(q>hi)hi=q;
        d=(int64_t)q-cal->zero_q10; sum+=d; square+=(uint64_t)(d*d);
    }
    memset(&m,0,sizeof(m)); m.samples=count; m.clips=clips;
    m.minimum_uv=Scope144_Voltage(cal,lo); m.maximum_uv=Scope144_Voltage(cal,hi);
    center=sum/count; m.average_uv=Scope144_Voltage(cal,(int32_t)center+cal->zero_q10);
    {uint64_t rms=(uint64_t)Sqrt(square/count)*cal->reference_uv/cal->span_q10;
     m.rms_uv=rms>INT32_MAX?INT32_MAX:(int32_t)rms;}
    threshold=lo+(hi-lo)/2;
    if(!source->read(source->user,0,&previous))return 0;
    for(i=1;i<count;++i) {
        if(!source->read(source->user,i,&q))return 0;
        if(previous<threshold && q>=threshold && hi-lo>=10*1024) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(threshold-previous)<<16)/(q-previous);
            if(!edges)first=pos;
            else high+=pending_high;
            pending_high=0;rise=pos;
            last=pos; ++edges;
        } else if(previous>=threshold && q<threshold && edges) {
            pos=((uint64_t)(i-1)<<16)+((uint64_t)(previous-threshold)<<16)/(previous-q);
            if(pos>=rise)pending_high+=pos-rise;
        }
        previous=q;
    }
    if(edges>1 && !clips) {
        uint64_t period=(last-first)/(edges-1);
        if(source->period_ps_q16) {
            m.period_ps=(uint32_t)((period*source->period_ps_q16+0x80000000ULL)>>32);
            if(!m.period_ps)return 0;
            m.frequency_hz=(uint32_t)((1000000000000ULL+m.period_ps/2)/m.period_ps);
        } else {
            m.frequency_hz=(uint32_t)(((uint64_t)(144u*(1000000+clock_ppm))<<16)/period);
            m.period_ps=(uint32_t)(period*62500*1000000/((uint64_t)9*65536*(1000000+clock_ppm)));
        }
        /* Commit high time only when the next rising edge closes a cycle.
         * The pending tail after the last rise is excluded exactly as before. */
        m.duty_ppm=(uint32_t)(high*1000000/(last-first));
        m.high_ps=(uint32_t)((uint64_t)m.period_ps*m.duty_ppm/1000000);
        m.low_ps=m.period_ps-m.high_ps;
    }
    *out=m; return 1;
}
int Baseline_FrameMeasure(const Frame144 *frame,Scope144Measurements *measure)
{
    Scope144Source source;unsigned i,clips=0,word,a,b;
    if(!measure || !Baseline_Source(frame,&source))return 0;
    for(i=0;i<frame->capture.valid_words;++i) {
        memcpy(&word,(const unsigned char *)frame->words+4u*(frame->capture.valid_begin+i),sizeof word);a=word&255u;b=(word>>8)&255u;
        clips+=!a || a==255;clips+=!b || b==255;
    }
    return Baseline_MeasureSource(&frame->calibration,&source,clips,measure);
}

/* Frozen Normal edge logic from the approved baseline. */
void Legacy_NormalEdges(const uint8_t *values,unsigned count,unsigned native_time_demand,unsigned CurDefTime,unsigned begin,unsigned end,unsigned *out)
{
    int i,bag_max_buf=(int)count,finish=(int)end,start=(int)begin;
    int8_t Sa=2,Sb=2,Sc=2,Sd=2;
    uint16_t Ta=0,Tb=0,Tc=0,Td=0,Pa=0,Pb=0,Pc=0,Pd=0;
    uint16_t TaS=0,TbS=0,TcS=0,TdS=0,PaS=0,PbS=0,PcS=0,PdS=0,TaN=0,TbN=0,TcN=0,TdN=0,QStart[2]={0};
    uint8_t Ch[2],C_D,FirstA_Mid=0,LastA_Mid=0,FirstB_Mid=0,LastB_Mid=0;
    int a_Mid_H=130,a_Mid_L=126,b_Mid_H=140,b_Mid_L=136;
    (void)native_time_demand;
    for(i=0;i<(int)count;++i){Ch[0]=values[3*i];Ch[1]=values[3*i+1];C_D=values[3*i+2];
    if((CurDefTime==0)||((i>start-1)&&(i<(finish+1)))){	//for time related measurements for both dig and analog channels

      if((i>3)&&(i<(bag_max_buf-3))){ 

       if((CurDefTime==0)||(i<finish)){

        if(Ch[0] > a_Mid_H){
          if (Sa<2){					//don't initiate if value is above trigger point when starting out
            if(Sa == 0){
		  TaS = i;                    					
                  LastA_Mid=a_Mid_H;			//save relevant time meter trigger level for quantization compensation
              if(Ta==0){				//use Ta to initiate
                  Ta=i;					//first + crossing point saved 
                  QStart[0]=i;				//start position saved for quantization calcs 
                  FirstA_Mid=a_Mid_H;
		  }else{				//after initialized, count + crossings, add + wave sampling count to PaS
		    TaN++;                              //increment TaN only after initial crossing point saved
		    PaS += Pa;				//only add positive part if whole wave is considered	
              }
		}
            Sa = 1;  
          } 
        } else { 
          if(Ch[0] < a_Mid_L){
            if(Sa == 2){
		  Sa=0; 				//initiate only after going below trigger point so partial wave does not get counted
		  PaS=0;				//initiate PaS
            }
	      if(Sa == 1){
                  Sa = 0;
		  Pa = i-TaS;				//save for PaS
            }
          }
        }

       if(Ch[1] > b_Mid_H){
          if (Sb<2){					
            if(Sb == 0){
		  TbS = i;                    					
                  LastB_Mid=b_Mid_H;			//save relevant time meter trigger level for quantization compensation
              if(Tb==0){				
                  Tb=i;					 
                  QStart[1]=i;				//start position saved for quantization calcs 
                  FirstB_Mid=b_Mid_H;
		  }else{
		    TbN++;                         
		    PbS += Pb;					
              }
		}
            Sb = 1;  
          } 
        } else { 
          if(Ch[1] < b_Mid_L){
            if(Sb == 2){
		  Sb=0; 					
		  PbS=0;					
            }
	      if(Sb == 1){
              Sb = 0;
		  Pb = i-TbS;				
            }
          }
        }
       }  

    if((i>10)&&((CurDefTime==0)||(i>start))){                        //eliminate noise at start of wave causing errors in dig channels

       if(C_D & 1){
          if (Sc<2){					
            if(Sc == 0){
		  TcS = i;                    					
              if(Tc==0){				
                Tc=i;					 
		  }else{
		    TcN++;                         
		    PcS += Pc;					
              }
		}
            Sc = 1;  
          } 
        } else { 
            if(Sc == 2){
		  Sc=0; 					
		  PcS=0;					
            }
	      if(Sc == 1){
              Sc = 0;
		  Pc = i-TcS;				
            }
	  } 

       if(C_D & 2){
          if (Sd<2){					
            if(Sd == 0){
		  TdS = i;                    					
              if(Td==0){				
                Td=i;					 
		  }else{
		    TdN++;                         
		    PdS += Pd;					
              }
		}
            Sd = 1;  
          } 
        } else { 
            if(Sd == 2){
		  Sd=0; 					
		  PdS=0;					
            }
	      if(Sd == 1){
              Sd = 0;
		  Pd = i-TdS;				
            }
        }

      }	//if i>8
    } //if i>3
  }  //if curdeftime
//}    //if flagmeter>0
   

    }
    out[0]=TaS-Ta;out[1]=PaS;out[2]=TaN;out[3]=QStart[0];out[4]=TaS;out[5]=FirstA_Mid;out[6]=LastA_Mid;
    out[7]=TbS-Tb;out[8]=PbS;out[9]=TbN;out[10]=QStart[1];out[11]=TbS;out[12]=FirstB_Mid;out[13]=LastB_Mid;
    out[14]=TcS-Tc;out[15]=PcS;out[16]=TcN;
    out[17]=TdS-Td;out[18]=PdS;out[19]=TdN;
}
