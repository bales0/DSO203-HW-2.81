/* Frozen K4 rules before deduplication, for host regression only. */
#include "BIOS.h"
#include "Menu.h"
#include "Process.h"
void Test_LegacyDetailTail(void)
{
if((FlagMeter==0)&&((Current==V_VERNIE)||(Current==T_VERNIE))&&(_Det==V2)){ //skip meter select if meters off
                        _Curr[_Det].Flag |= UPDAT;
                        _Det=0;                                                                    //go to V1 or T1 = 0
                      }else{
			if(Current < METER_0){                                                     // change the Detail
			  _Curr[_Det].Flag |= UPDAT;
			  if(_Det < 3)    _Det += 1;
			  else            _Det  = 0;
                          if(Current == OUTPUT){if(Title[OUTPUT][KIND].Value==PWM){if(_Det==OUTATT)_Det=0;}else if(_Det==DUTYPWM)_Det+=1;} 
			  if(_Curr[_Det].MARK & NOT)                        _Det  = 0; 
			  if(_Curr[_Det].MARK & NUM2)                       _Det  = 0; 
			  if((_Curr[_Det].MARK & NUM3)&&(Current != FILE)&& (Current != OUTPUT) )  _Det  = 0;  

                          if((Current==FILE)&&(_Det==3)){
                            if((_Curr[2].Value==7)&&(Title[FILE][0].Value==0))_Det=0;              //do not edit ROM backup file 
                          }

 			  _Curr[_Det].Flag |= BLINK;
			} else {                                                                   // change the measurement object

                          if(Meter[Current-METER_0].Item>1){                     //don't change source for VBT AND FPS (items 0 and 1) 		
			  Meter[Current-METER_0].Flag |= UPDAT;

			  if((Meter[Current-METER_0].Item>VPP)||(Meter[Current-METER_0].Item==FRQ)){
			    if(Meter[Current-METER_0].Track <=  TRACK4) 
                            Meter[Current-METER_0].Track += 1;
			    if(Meter[Current-METER_0].Track > TRACK4) 
                            Meter[Current-METER_0].Track  = TRACK1;
                          }else{
                            if(Meter[Current-METER_0].Track==TRACK1)
                              Meter[Current-METER_0].Track=TRACK2;
                                else Meter[Current-METER_0].Track=TRACK1;
                          }   
                        } 
		      }//if current, else (if in meter section)
                    }
}
int Test_LegacyDetailPrelude(void)
{
if ((Current == TRACK3)&&(_Det == 0))_Det = 2;

            if ((Current == TRACK4)&&(_Det == 0)){
              if(_4_source>11)_Det=1;else _Det=2;                                        //in fft modes, to engage MAG modes  
              if(_4_source>10)goto bypasslongpress;                                      //in fft modes, 11=spec
            }else if ((Current == TRACK4)&&(_Det == 2)&&(_4_source>9)){                  //in map mode
              _Det = 0;
              goto bypasslongpress;
            }

           if(Current==6){if(_Mode==X_Y){if(_Det==1)_Det=2;}else if((_Det==2)&&(_Mode!=NORHLD))_Det=3;}

     	   if ((((Options&1)==0)||(_Mode==AUTO)||(_Mode==X_Y)||(OsBufferLogicFlag)||(_Mode==SGL)||
             (_Mode==SCAN)||(_T_base>9))&&(Current == TRIGG)&&(_Det==2))_Det=3;	//skip over det3 (will go to 0)


                      
return 0;
bypasslongpress: return 1;
}
