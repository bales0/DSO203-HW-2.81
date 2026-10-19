/* Frozen original FFT postprocessing for bounded native regression vectors. */
void Legacy_Process_Fft(void) { u16 i,LeftSkirt=0; s32 X,Y;u8 treshold=AutoFFT==0?75:6;
for(i=0; i<FFTSize;i++)	fi[i] = 0;
	   fix_fft(fr, fi, FFTSize);

           fr[1]=0; 
           fr[0]=0; fi[0]=0;                            					            
	   for (i=0; i < (FFTSize/2); i++)
	     {
		X= fr[i];      /* Real */		
		Y= fi[i];      /* Imag */    
                fi[ i ] = Int_sqrt((X*X)+(Y*Y));	//load in fi[x] instead of fr[x], so result can persist after loading input in fr[x]
                if(fi[i]<0)fi[i]=0;        

                if((_4_source==15)||(_4_source==13)){   //wide bandwidth, sums peak with adjacent bins, so need to leave space before next peak
                  if(i>3){         			//was 5    3 now works with  i>5 on leftskirt conditional and fr[1] cleared 
                    if((fi[i-2]>=fi[i-3])&&(fi[i-2]>fi[i-1])&&(fi[i-2]>treshold)){	        //detect peak (i= -2 =peak), threshold
                      fi[i-2]=Int_sqrt((fi[i-2]*fi[i-2])+(fi[i-3]*fi[i-3])+(fi[i-1]*fi[i-1]));  //sum with neighbors
                      fi[i-3]=2+(fi[i-2]>>2);					      		//suppress left skirt (was >>3 for hamming W)                      
                      LeftSkirt=i-3;						      		//save it's position	 
                    }
                    if(((i-5)==LeftSkirt)&&(i>5)){fi[i-3]=fi[i-5];LeftSkirt=0;}  //suppress right skirt of summed peak if next bin skipped
                  }  

                  if(i>7){
                    fi[i-7]=Log10Process(i-7);          //Processed bins in summing mode
                  } 

                }else{
                    fi[i]=Log10Process(i);               //In Hann only mode
                }
 	     }

             if((_4_source==15)||(_4_source==13)){
                for(i=249;i<256;i++){
                    fi[i]=Log10Process(i);                    //Rest of bins in summing mode
                }
  
}
}

/* Frozen 144 update before reusing the validated source in the second pass.
 * Host regression only; never linked into the firmware. */
int Before_Reader_Update(const Frame144 *frame,unsigned start,unsigned source,unsigned setting,uint32_t now)
{
    Scope144Source samples;int64_t sum=0,value;int32_t q,mean;
    unsigned i,clipped=0,shift=0;uint32_t peak=0;uint32_t finished;
    if(!Scope144_IsFft(source) || setting>9 || !Frame144_Source(frame,&samples) || samples.count<512 ||
        start>samples.count-512)return 0;
    if(Fft144.ready && ((Fft144.epoch==frame->capture.epoch && Fft144.start==start &&
        Fft144.source==source && Fft144.setting==setting) || (uint32_t)(now-Fft144.last_done)<7200000u))return 0;
    for(i=0;i<512;++i){if(!samples.read(samples.user,start+i,&q))return 0;sum+=q;}
    mean=(int32_t)(sum/512);
    AutoFFT=(u8)setting;
    if(setting>1)FFTGain=(u8)(setting-2);else if(!setting)FFTGain=8;
    if(FFTGain>8)FFTGain=8;
    for(i=0;i<512;++i) {
        unsigned clip=0;
        if(!Frame144_Read(frame,start+i,&q,&clip))return 0;
        clipped|=clip;
        /* Work in calibrated ADC counts (Q10) before the Hann window, with
         * DC removed over this exact window. Rescale before integer FFT. */
        value=((int64_t)q-mean)*Window[i]*(1u<<(2u+FFTGain));
        value/=1024*(source==15?100:80);
        if(value>0x3fffffffu || value< -0x3fffffff)return 0;
        if((uint32_t)(value<0?-value:value)>peak)peak=(uint32_t)(value<0?-value:value);
        fr[i]=(s32)value;
    }
    /* Shared original magnitude/log/summed-bin rules use the selected source. */
    /* Keep all twiddle products in signed 32-bit range without hard-clipping
     * the signal. Restore this common scale before magnitude/log display. */
    while(peak>65535u){peak>>=1;++shift;}
    if(shift)for(i=0;i<512;++i)fr[i]/=(s32)(1u<<shift);
    Process_Fft(source,shift);
    PeakFreq=2;imax=0;
    for(i=1;i<256;++i)if(fi[i]>PeakFreq){PeakFreq=(short)fi[i];imax=(int)i;}
    if(setting==1) {
        if(PeakFreq>=198 && FFTGain) --FFTGain;
        else if(PeakFreq<100 && FFTGain<7)++FFTGain;
    }
    finished=DWT_CYCLES;
    Fft144.rate_hz=(uint32_t)((1000000000000ULL*65536u+samples.period_ps_q16/2u)/samples.period_ps_q16);
    Fft144.epoch=frame->capture.epoch;Fft144.start=start;Fft144.source=source;Fft144.setting=setting;
    Fft144.clipped=clipped;Fft144.ready=1;Fft144.last_done=finished;
    return 1;
}
