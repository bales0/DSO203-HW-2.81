/* Pixel-exact comparison: incremental outline vs complete actual painter. */
#define main Holdoff_ExistingTests
#include "test_holdoff_view.c"
#undef main
static u16 incremental[400][16];
static void Compare(unsigned allow_full)
{
 unsigned x,y,writes;
 _X_View.Flag|=UPDAT;Title[TRIGG][3].Flag|=UPDAT;
 pixel_writes=0;Update_Buffer_Overview();CHECK(!(_X_View.Flag&UPDAT) && !(Title[TRIGG][3].Flag&UPDAT));writes=pixel_writes;CHECK(writes<(allow_full?3500u:1000u));
 memcpy(incremental,pixels,sizeof pixels);Test_PaintOverview();
 for(x=0;x<400;++x)for(y=0;y<16;++y)CHECK(incremental[x][y]==pixels[x][y]);
 pixel_writes=0;Update_Buffer_Overview();CHECK(!pixel_writes);
}
int main(void)
{
 unsigned mode,state,i,notes,file;menu e={"T",colors,3400,FIX,91,0,0,UPDAT};
 /* Same trace helper serves Normal and 144. Complete offscreen segments
  * must not become horizontal LCD-edge lines; a real narrow pulse survives. */
 {unsigned bold,a,b;u8 y[2];
  for(bold=0;bold<3;++bold)for(a=0;a<=200;++a)for(b=0;b<=200;++b){
   int visible=Draw_TraceSegment(a,b,bold,y);
   CHECK(visible==!((a<1 && b<1)||(a>199 && b>199)));
   if(visible){CHECK(y[0]>=1 && y[1]<=199 && y[0]<=y[1]);
    if(a==b && a>2 && a<198)CHECK((unsigned)(y[1]-y[0]+1)==3-bold);}
  }
  CHECK(Draw_TraceSegment(50,200,2,y) && y[0]==50 && y[1]==199);
  CHECK(Draw_TraceSegment(200,50,2,y) && y[0]==50 && y[1]==199);
  CHECK(!Draw_TraceSegment(200,200,0,y));
 }
 memcpy(&Title[TRIGG][3],&e,sizeof e);Current=TRACK1;_Mode=AUTO;
 for(mode=0;mode<2;++mode)for(state=0;state<2;++state){
  interlace=mode;_State.Value=(s16)state;FrameMode=FlagFrameMode=OSBuffer=OsBufferLogicFlag=0;
  native_visible=interlace_visible=1;native_first=interlace_first=91;native_last=interlace_last=105;
  Test_PaintOverview();
  for(i=0;i<120;++i){
   unsigned first=91+(i*7)%180,last=first+1+i%30;if(last>306)last=306;
   native_first=interlace_first=first;native_last=interlace_last=last;Compare(0);
  }
  for(OffsetSelect=1;OffsetSelect<=2;++OffsetSelect) {
   memset(pixels,0x4c,sizeof pixels);memcpy(incremental,pixels,sizeof pixels);
   pixel_writes=0;Update_Buffer_Overview();CHECK(!pixel_writes && !memcmp(incremental,pixels,sizeof pixels));
  }
  OffsetSelect=0;Compare(1);
  for(notes=0;notes<2;++notes)for(file=0;file<2;++file)if(notes || file){
   Current=file?FILE:TRACK1;NotificationTimer=(u16)notes;
   native_first=interlace_first=150;native_last=interlace_last=160;
   pixel_writes=0;Update_Buffer_Overview();CHECK(!pixel_writes);
   Current=TRACK1;NotificationTimer=0;Compare(1);
  }
  native_visible=interlace_visible=0;Compare(0);
  native_visible=interlace_visible=1;native_first=interlace_first=91;native_last=interlace_last=306;Compare(0);
  native_last=interlace_last=91;Compare(0);
 }
 interlace=!interlace;Compare(1);
 interlace=0;FlagFrameMode=1;Update_Buffer_Overview();memcpy(incremental,pixels,sizeof pixels);Test_PaintOverview();CHECK(!memcmp(incremental,pixels,sizeof pixels));
 printf("PASS overview pixels: %u checks; Normal/144 RUN/HOLD, exact background restoration, bounds, deferred overlays, no unchanged redraw\n",checks);return 0;
}
