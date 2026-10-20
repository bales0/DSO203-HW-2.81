/* Frozen legacy oracle; all read counters are host-test-only. */
#include "Frame144.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
static unsigned checks;static int32_t data[8188];static uint32_t words[4096];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int Legacy_MeasureSource(const Scope144Cal *,const Scope144Source *,unsigned,Scope144Measurements *);
int Baseline_MeasureSource(const Scope144Cal *,const Scope144Source *,unsigned,Scope144Measurements *);
int Baseline_FrameMeasure(const Frame144 *,Scope144Measurements *);
int Baseline_FrameRead(const Frame144 *,unsigned,int32_t *,unsigned *);
void Test_NormalAverage(unsigned,unsigned,unsigned,uint32_t *);
void Legacy_NormalAverage(unsigned,unsigned,unsigned,uint32_t *);
void Legacy_NormalEdges(const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned *);
void Test_NormalEdges(const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned *);
typedef struct {Scope144Source source;unsigned reads,fail;} Counted;
static int Read(const void *user,unsigned i,int32_t *q){Counted *c=(Counted *)user;++c->reads;if(i==c->fail)return 0;if(c->source.read)return c->source.read(c->source.user,i,q);if(i>=c->source.count)return 0;*q=data[i];return 1;}
static unsigned Compare(const Scope144Cal *cal,const Scope144Source *src,unsigned clips,unsigned fail){
 Counted a={*src,0,fail},b={*src,0,fail},c={*src,0,fail};Scope144Source old=*src,now=*src,baseline=*src;Scope144Measurements x,y,z;int rx,ry,rz;
 old.user=&a;old.read=Read;now.user=&b;now.read=Read;memset(&x,0xa5,sizeof x);memset(&y,0xa5,sizeof y);
 baseline.user=&c;baseline.read=Read;memset(&z,0xa5,sizeof z);
 rx=Legacy_MeasureSource(cal,&old,clips,&x);ry=Scope144_MeasureSource(cal,&now,clips,&y);
 rz=Baseline_MeasureSource(cal,&baseline,clips,&z);CHECK(ry==rz && !memcmp(&y,&z,sizeof y));
 if(ry)CHECK(c.reads-b.reads==2);
 CHECK(rx==ry);CHECK(!memcmp(&x,&y,sizeof x));CHECK(b.reads<=a.reads);if(rx && x.frequency_hz)CHECK(a.reads-b.reads==src->count+2);return rx && x.frequency_hz;
}
static int Tagged(const void *user,unsigned i,int32_t *q)
{
 const Scope144Source *source=user;
 if(i>=source->count)return 0;
 *q=data[i];return i%17==0?2:1;
}
static void TaggedCases(const Scope144Cal *cal)
{
 static const unsigned lengths[]={16,17,137,512,8188};
 unsigned n,i,fail;Scope144Source src={0},counted;Scope144Measurements expected,actual;
 src.user=&src;src.read=Tagged;src.period_ps_q16=6000ULL*65536;
 for(n=0;n<sizeof lengths/sizeof lengths[0];++n){
  src.count=lengths[n];
  for(i=0;i<src.count;++i)data[i]=(i%37<11?80:170)*1024;
  for(fail=0;fail<4;++fail){
   unsigned bad=fail==0?UINT_MAX:fail==1?0:fail==2?src.count/2:src.count-1;
   Counted old={src,0,bad},now={src,0,bad};int before,after;
   counted=src;counted.user=&old;counted.read=Read;
   memset(&expected,0xa5,sizeof expected);memset(&actual,0xa5,sizeof actual);
   before=Baseline_MeasureSource(cal,&counted,(src.count+16)/17,&expected);
   counted.user=&now;after=Scope144_MeasureSourceClipped(cal,&counted,&actual);
   CHECK(before==after && !memcmp(&expected,&actual,sizeof actual));
   CHECK(now.reads<=old.reads);if(after)CHECK(now.reads==2*src.count-1);
   now.reads=0;memset(&actual,0xa5,sizeof actual);
   if(before)expected.frequency_hz=expected.period_ps=expected.duty_ppm=expected.high_ps=expected.low_ps=0;
   CHECK(Scope144_MeasureSourceRequested(cal,&counted,0,&actual)==before);
   CHECK(!memcmp(&actual,&expected,sizeof actual));
   if(before)CHECK(now.reads==src.count);
  }
 }
 CHECK(!Scope144_MeasureSourceClipped(cal,0,&actual));
 CHECK(!Scope144_MeasureSourceClipped(0,&src,&actual));
 CHECK(!Scope144_MeasureSourceClipped(cal,&src,0));
 /* The original explicit clipping API keeps every unsigned value, including
  * UINT_MAX. No sentinel changes its public meaning. */
 Compare(cal,&src,UINT_MAX,UINT_MAX);
}
static unsigned Raw(unsigned sample,unsigned count,unsigned kind,uint32_t *random)
{
 switch(kind){
 case 0:return 32;
 case 1:return 128u+(int)(60*sin(6.283185307179586*sample/37.0));
 case 2:return sample%37<11?80:170;
 case 3:return sample==0?220:80;
 case 4:return sample==count-1?220:80;
 case 5:return sample%37<5?0:255;
 case 6:return sample&1?0:120;
 case 7:return sample&1?120:255;
 case 8:*random=*random*1664525u+1013904223u;return *random>>24;
 case 9:return 128u+(int)(60*sin(6.283185307179586*sample/count));
 case 10:return sample%7?1:254;
 default:return sample%3?130:119;
 }
}
static void FrameCompare(const Frame144 *frame)
{
 Scope144Measurements expected,actual;int before,after;
 memset(&expected,0xa5,sizeof expected);memset(&actual,0xa5,sizeof actual);
 before=Baseline_FrameMeasure(frame,&expected);after=Frame144_Measure(frame,&actual);
 if(before){unsigned i;expected.clip_rails=0;
  for(i=0;i<frame->capture.valid_words;++i){unsigned ch;
   for(ch=0;ch<2;++ch){unsigned raw=(frame->words[frame->capture.valid_begin+i]>>(ch*8))&255u;
    expected.clip_rails|=raw==0?1:raw==255?2:0;}}}
 if(before!=after || memcmp(&expected,&actual,sizeof actual)){
  fprintf(stderr,"Frame comparison: range=%u phase=%u begin=%u words=%u epoch=%u\n",frame?frame->range:99,frame?frame->capture.sample_phase:0,frame?frame->capture.valid_begin:0,frame?frame->capture.valid_words:0,frame?frame->capture.epoch:0);
  CHECK(0);
 }
 CHECK(before==after && !memcmp(&expected,&actual,sizeof actual));
 memset(&actual,0xa5,sizeof actual);
 CHECK(Frame144_MeasureRequested(frame,1,&actual)==before && !memcmp(&expected,&actual,sizeof actual));
 if(before)expected.frequency_hz=expected.period_ps=expected.duty_ppm=expected.high_ps=expected.low_ps=0;
 memset(&actual,0xa5,sizeof actual);
 CHECK(Frame144_MeasureRequested(frame,0,&actual)==before && !memcmp(&expected,&actual,sizeof actual));
}
static void PublicReadCases(Frame144 *frame)
{
 unsigned word,phase,sample,before_clip,after_clip;int32_t before_q,after_q;
 for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase){
  Scope144Source source;frame->capture.sample_phase=(uint8_t)phase;
  CHECK(Frame144_Source(frame,&source));
  for(word=0;word<65536;++word){
   words[frame->capture.valid_begin]=word|0xfffc0000u;
   for(sample=0;sample<2;++sample){
    int status;
    CHECK(Baseline_FrameRead(frame,sample,&before_q,&before_clip)==1);
    CHECK(Frame144_Read(frame,sample,&after_q,&after_clip)==1);
    CHECK(before_q==after_q && before_clip==after_clip);
    status=source.read(source.user,sample,&after_q);
    {unsigned raw=(word>>(sample==(phase==CAPTURE_PHASE_B_A)?0:8))&255u;
     CHECK(status==(raw==0?6:raw==255?10:1) && before_q==after_q);}
   }
  }
 }
 before_q=after_q=123;before_clip=after_clip=99;
 CHECK(!Baseline_FrameRead(frame,frame->capture.time_samples,&before_q,&before_clip));
 CHECK(!Frame144_Read(frame,frame->capture.time_samples,&after_q,&after_clip));
 CHECK(before_q==after_q && before_clip==after_clip);
 CHECK(!Frame144_Read(frame,UINT_MAX,&after_q,&after_clip));
}
static void FrameCases(const Scope144Cal *cal)
{
 static const unsigned lengths[]={8,9,31,256,3944,4094};
 static const int32_t reference[]={250000,500000,1000000,2500000,5000000,10000000,25000000,50000000};
 unsigned phase,range,n,kind,i,clips,epoch=1;uint32_t random=41;
 Frame144 f,bad;Scope144Source source;Scope144Measurements actual;
 for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase)for(range=0;range<8;++range)
 for(n=0;n<sizeof lengths/sizeof lengths[0];++n)for(kind=0;kind<12;++kind){
  uint32_t hash;
  Frame144_Reset(&f,epoch++);f.words=words;f.range=range;f.calibration=*cal;
  f.calibration.gain_q10=512+(int32_t)range*219;
  f.calibration.bias_q10=(int32_t)range*200000-700000;
  f.calibration.a_gain_q10=2048-(int32_t)range*219;
  f.calibration.a_bias_q10=700000-(int32_t)range*200000;
  f.calibration.reference_uv=reference[range];
  f.capture.fifo_words=4096;f.capture.valid_words=(uint16_t)lengths[n];
  f.capture.valid_begin=(uint16_t)(4096-lengths[n]>152?152:2);
  f.capture.time_samples=2*lengths[n];f.capture.status=CAPTURE_WINDOW_READY;
  f.capture.sample_phase=(uint8_t)phase;
  f.capture.sample_period_ps_q16=(kind%3==0?6000ULL:kind%3==1?8000ULL:6944ULL)*65536;
  /* Invalid history/priming is deliberately clipped; it is not measured. */
  for(i=0;i<4096;++i)words[i]=0x00ff00ffu;
  clips=0;
  for(i=0;i<lengths[n];++i){
   unsigned a=Raw(2*i+(phase==CAPTURE_PHASE_B_A),f.capture.time_samples,kind,&random);
   unsigned b=Raw(2*i+(phase==CAPTURE_PHASE_A_B),f.capture.time_samples,kind,&random);
   words[f.capture.valid_begin+i]=a|(b<<8)|((i&3u)<<16);
   clips+=a==0 || a==255;clips+=b==0 || b==255;
  }
  CHECK(Frame144_Valid(&f));hash=Scope144_Crc(words,sizeof words);
  FrameCompare(&f);CHECK(Frame144_Measure(&f,&actual) && actual.clips==clips);
  CHECK(actual.samples==f.capture.time_samples);
  CHECK(Scope144_Crc(words,sizeof words)==hash && f.capture.epoch==epoch-1);
  CHECK(Frame144_Source(&f,&source));
  for(i=0;i<2;++i){
   unsigned sample=i?f.capture.time_samples-1:0,clip;int32_t q,again;
   CHECK(Frame144_Read(&f,sample,&q,&clip));
   CHECK(!!(source.read(source.user,sample,&again)&2)==clip && q==again);
  }
 }
 FrameCompare(0);bad=f;bad.words=0;FrameCompare(&bad);
 bad=f;bad.range=8;FrameCompare(&bad);
 bad=f;bad.capture.status=CAPTURE_WINDOW_ARMED;FrameCompare(&bad);
 bad=f;bad.capture.valid_begin=4096;FrameCompare(&bad);
 bad=f;bad.capture.valid_words=0;FrameCompare(&bad);
 bad=f;bad.capture.time_samples--;FrameCompare(&bad);
 bad=f;bad.capture.sample_phase=CAPTURE_PHASE_NATIVE;FrameCompare(&bad);
 bad=f;bad.capture.sample_period_ps_q16=5999ULL*65536;FrameCompare(&bad);
 bad=f;bad.capture.sample_period_ps_q16=8001ULL*65536;FrameCompare(&bad);
 bad=f;bad.calibration.valid=0;FrameCompare(&bad);
 CHECK(!Frame144_Measure(&f,0));
 CHECK(!Frame144_Read(&f,f.capture.time_samples,0,0));
 PublicReadCases(&f);
}
static void NormalEdgeCases(void)
{
 static uint8_t values[4096*3];static const unsigned sizes[]={16,64,511,4096};
 unsigned n,kind,i,mask,cur,ch,k,expected[20],actual[20];uint32_t random=199;
 for(n=0;n<4;++n)for(kind=0;kind<12;++kind){
  for(i=0;i<sizes[n];++i){
   unsigned a=128,b=138,c=0;
   switch(kind){
   case 0:break;
   case 1:a=100;b=200;break;
   case 2:a=i%37<11?180:80;b=i%71<30?190:90;c=(i%27<6)|((i%49<21)<<1);break;
   case 3:a=i==sizes[n]/2?180:80;b=i==sizes[n]-5?190:90;c=i==17?3:0;break;
   case 4:a=128+(int)(60*sin(6.283185307179586*i/37));b=138+(int)(60*sin(6.283185307179586*i/41));c=i%23<10?3:0;break;
   case 5:random=random*1664525u+1013904223u;a=random>>24;b=(random>>16)&255;c=random&3;break;
   case 6:a=i%4==0?126:i%4==1?130:i%4==2?125:131;b=i%4==0?136:i%4==1?140:i%4==2?135:141;c=i&3;break;
   case 7:a=i<sizes[n]/2?255:0;b=255-a;c=a?3:0;break;
   default:a=i%37<kind?170:80;b=i%41<kind?180:90;c=(i%27<kind)|((i%33<kind)<<1);break;
   }
   values[3*i]=(uint8_t)a;values[3*i+1]=(uint8_t)b;values[3*i+2]=(uint8_t)c;
  }
  for(mask=0;mask<16;++mask)for(cur=0;cur<2;++cur){
   Legacy_NormalEdges(values,sizes[n],mask,cur,7,sizes[n]-8,expected);
   Test_NormalEdges(values,sizes[n],mask,cur,7,sizes[n]-8,actual);
   for(ch=0;ch<4;++ch){unsigned start=ch<2?ch*7:14+(ch-2)*3,length=ch<2?7:3;
    for(k=0;k<length;++k)CHECK(actual[start+k]==((mask&(1u<<ch))?expected[start+k]:0));
   }
  }
 }
}
static void VoltageAverageCases(void)
{
 unsigned mask,ch,mode,seed,i;uint32_t old[26],now[26];
 for(mask=0;mask<16;++mask)for(ch=0;ch<4;++ch)for(mode=0;mode<4;++mode)for(seed=0;seed<100;++seed){
  for(i=0;i<26;++i)old[i]=now[i]=i>=4 && i<8?seed: i==20 || i==21?seed:(seed+3)*100+i;
  Legacy_NormalAverage(mask,ch,mode,old);Test_NormalAverage(mask,ch,mode,now);
  for(i=20;i<26;++i)CHECK(old[i]==now[i]);
  if(mask&(1u<<ch))CHECK(!memcmp(old,now,sizeof old));
 }
}
int main(void){
 static const unsigned lengths[]={16,17,31,64,137,511,512,1024,8188};
 Scope144Cal cal={1024,0,55*1024,125*1024,1000000,1,0,1024,0,1024,1024,54,1024,1024,0,0};Scope144Source src={0};
 unsigned n,kind,i,clips,phase,range,matched=0;uint32_t random=17;src.period_ps_q16=1000000000000ULL*65536/144000000;Scope144_SetClock(0);
 for(n=0;n<sizeof lengths/sizeof lengths[0];++n)for(kind=0;kind<16;++kind){
  src.count=lengths[n];clips=0;
  for(i=0;i<src.count;++i){int32_t v=120*1024;switch(kind){
   case 0:break;
   case 1:v+=(int32_t)(60*1024*sin(6.283185307179586*i/37.0));break;
   case 2:v=(i%37<11?80:170)*1024;break;
   case 3:v=(i==src.count/2?170:80)*1024;break;
   case 4:v=(i%37==13?170:80)*1024;break;
   case 5:v=(i%37<5?0:255)*1024;++clips;break;
   case 6:v+=(int32_t)(60*1024*sin(1.5707963267948966*i/src.count));break;
   case 7:v=((int32_t)(i%37)*5+40)*1024;break;
   case 8:random=random*1664525u+1013904223u;v=(int32_t)(random>>24)*1024;break;
   case 9:v=(i%4==0?80:i%4==1?125:i%4==2?170:125)*1024;break;
   case 10:v+=(int32_t)(60*1024*sin(6.283185307179586*i/7.0));clips=1;break;
   case 11:v+=(int32_t)(60*1024*sin(6.283185307179586*i/src.count));break;
   case 12:v=(int32_t)i*10;break;
   case 13:v=(i%3==0?4000000:-4000000);break;
   case 14:v=(i%7<2?120:129)*1024;break;
   case 15:v=(i%23==0?170:i%23<13?125:80)*1024;break;
  }data[i]=v;}
  matched+=Compare(&cal,&src,clips,UINT_MAX);Compare(&cal,&src,clips,src.count/2);
 }
 for(kind=0;kind<256;++kind){src.count=16+kind*31;for(i=0;i<src.count;++i){random=random*1664525u+1013904223u;data[i]=(int32_t)(random>>24)*1024+(int32_t)(random&1023);}matched+=Compare(&cal,&src,0,UINT_MAX);}
 for(phase=CAPTURE_PHASE_B_A;phase<=CAPTURE_PHASE_A_B;++phase)for(range=0;range<8;++range){
  Frame144 f;Scope144Source actual;Frame144_Reset(&f,range+1);f.words=words;f.range=range;f.calibration=cal;
  f.calibration.gain_q10=800+(int32_t)range*64;f.calibration.bias_q10=-3000;f.calibration.a_gain_q10=700+(int32_t)range*100;f.calibration.a_bias_q10=1500;
  f.capture.fifo_words=4096;f.capture.valid_begin=2;f.capture.valid_words=4094;f.capture.time_samples=8188;f.capture.status=CAPTURE_WINDOW_READY;f.capture.sample_phase=(uint8_t)phase;f.capture.sample_period_ps_q16=src.period_ps_q16;
  for(i=0;i<4096;++i)words[i]=(uint32_t)(70+i%100)|((uint32_t)(80+i%90)<<8)|((i&3u)<<16);
  CHECK(Frame144_Source(&f,&actual));matched+=Compare(&f.calibration,&actual,0,UINT_MAX);
 }
 TaggedCases(&cal);FrameCases(&cal);NormalEdgeCases();VoltageAverageCases();
 CHECK(matched>200);printf("PASS analog passes: %u checks; exact legacy RECORD results, DC/sine/pulse/glitch/clips/duty/tails, 8188 samples, both phases/all ranges, failures; legacy and P0 equality, raw clipping fused, deterministic first-sample reuse, frame validation/epochs, requested amplitude pass, native edge equality\n",checks);return 0;
}
