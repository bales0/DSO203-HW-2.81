/* Valid extent and viewport geometry are independent of paint policy. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NativeWindow.h"
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void Fill(unsigned count,unsigned format){unsigned i;NativeWindow_Reset();for(i=0;i<count;++i)NativeWindow_Written(i,format,1);}
int main(void)
{
 NativeWindowInput in={4096,392,1024,0,0,2,2,0,0,0};
 ViewWindowGeometry v;unsigned first,last,i,j,count;
 static const unsigned counts[]={0,1,2,100,300,320,4095,4096};
 for(i=0;i<8;++i)for(j=0;j<2;++j){
  unsigned width=j?386:300;count=counts[i];Fill(count,0);NativeWindow_Publish(&in);
  CHECK(NativeWindow_Geometry(width,&v)==(int)(count>=width));
  if(count>=width){CHECK(v.capture_count==4096);CHECK(v.window_start_q16==2u*65536u);CHECK(v.window_end_q16==(width-1u)*65536u);}
 }
 /* Fully known prefix is an honest record extent, never capacity padding. */
 Fill(4095,0);NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==4096);
 CHECK(NativeWindow_Overview(386,&first,&last));
 NativeWindow_Written(0,0,1);CHECK(NativeWindow_Count()==1 && !NativeWindow_Overview(386,&first,&last));
 for(i=1;i<4096;++i)NativeWindow_Written(i,0,1);
 NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==4096);
 NativeWindow_Written(1000,0,0);CHECK(!NativeWindow_Geometry(386,&v));
 NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==4096);
 NativeWindow_Written(100,0,0);NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
 /* Missing cells do not bridge a gap; format changes revoke the prefix. */
 Fill(100,0);NativeWindow_Written(101,0,1);CHECK(NativeWindow_Count()==100);
 NativeWindow_Written(102,1,1);CHECK(!NativeWindow_Count());NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
 for(i=0;i<3;++i){Fill(4096,i);in.format=(uint8_t)i;NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v));in.format=(uint8_t)((i+1)%3);NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));}
 in.format=0;NativeWindow_Loaded();in.capacity=512;NativeWindow_Publish(&in);
 CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==512);
 in.capacity=4096;NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v) && v.capture_count==4096);
 NativeWindow_Written(0,0,0);CHECK(!NativeWindow_Geometry(386,&v));
 /* Every footprint endpoint must be in the same current contiguous extent. */
 Fill(4096,0);in.capacity=4096;in.points=392;in.discard=in.rdiscard=0;
 for(i=0;i<3;++i)for(j=0;j<2;++j){
  in.step_q10=(uint16_t)(i==0?246:i==1?614:1024);in.origin=150;in.digital_trigger=(uint8_t)j;
  in.shift=-5;NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v)==!j);
  in.shift=0;NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(386,&v));
  CHECK(v.window_start_q16==150u*65536u+j*5u*in.step_q10*64u);
  in.origin=4095;NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
 }
 in.digital_trigger=0;in.origin=0;in.step_q10=1024;in.shift=0;in.points=299;
 NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(300,&v));in.points=300;
 NativeWindow_Publish(&in);CHECK(NativeWindow_Geometry(300,&v));
 in.points=392;NativeWindow_Publish(&in);(void)NativeWindow_Changed(300);CHECK(!NativeWindow_Changed(300));CHECK(NativeWindow_Changed(386));
 in.origin=500;NativeWindow_Publish(&in);CHECK(NativeWindow_Changed(386));CHECK(!NativeWindow_Changed(386));
 NativeWindow_Hide();CHECK(NativeWindow_Changed(386));CHECK(!NativeWindow_Changed(386));
 in.unsupported=1;NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));in.unsupported=0;
 in.capacity=4097;NativeWindow_Publish(&in);CHECK(!NativeWindow_Geometry(386,&v));
 NativeWindow_Publish(0);CHECK(!NativeWindow_Geometry(386,&v));
 NativeWindow_Written(4096,0,1);CHECK(!NativeWindow_Count());
 printf("PASS Native window: %u checks; contiguous extent, partial viewport, invalid cells/gaps/formats, BUF, metadata/bounds/dirty\n",checks);return 0;
}
