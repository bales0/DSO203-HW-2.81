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
