#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "Scope144.h"
#include "CalText.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static uint16_t low[664],high[664];
static int32_t q[1024];
static unsigned char encoded[512],copy[512];
static const int32_t reference[8]={250000,500000,1000000,2500000,5000000,10000000,25000000,50000000};
int main(void)
{
    FpgaProfile profile=FpgaProfile_Decode(0x5731,0x0101,0x0102),old=FpgaProfile_Decode(0x5731,0x0101,0);
    Scope144Bank bank={{{0}},0},decoded; Scope144Cal cal; Scope144Measurements m,unchanged;
    Interlace144Calibration relative; char row[80]; unsigned i,r,start; int32_t value;
    CHECK(Scope144_Crc("123456789",9)==0xcbf43926u);
    for(i=0;i<664;++i){low[i]=i<152?0xffff:(55|(58<<8));high[i]=i<152?0xffff:(180|(183<<8));}
    for(r=0;r<8;++r){
        CHECK(Scope144_Calibrate(&profile,r,low,high,664,reference[r],&cal));
        CHECK(Scope144_Valid(&cal)); CHECK(cal.gain_q10==1024); CHECK(cal.bias_q10==-3072);
        CHECK(cal.zero_q10==55*1024);CHECK(cal.span_q10==125*1024);
        CHECK(Scope144_Voltage(&cal,180*1024)==reference[r]);CHECK(Scope144_Voltage(&cal,55*1024)==0);
        CHECK(Scope144_Voltage(&cal,-70*1024)==-reference[r]);
        Scope144_Relative(&cal,r,&relative);CHECK(relative.valid && relative.implementation==0x0102);
        CHECK(relative.input_key==Scope144_Key(r));bank.range[r]=cal;
    }
    CHECK(!Scope144_Calibrate(&old,0,low,high,664,250000,&cal));CHECK(!cal.valid);
    CHECK(!Scope144_Calibrate(&profile,8,low,high,664,250000,&cal));
    CHECK(Scope144_Encode(&bank,123,encoded));CHECK(Scope144_Decode(encoded,123,&decoded));CHECK(!memcmp(&bank,&decoded,sizeof(bank)));
    CHECK(!Scope144_Decode(encoded,124,&decoded));
    for(i=0;i<512;++i){memcpy(copy,encoded,512);copy[i]^=1;CHECK(!Scope144_Decode(copy,123,&decoded));}
    memcpy(copy,encoded,512);copy[4]=2; /* Unsupported format with a valid CRC is rejected. */
    {uint32_t crc=Scope144_Crc(copy,508);for(i=0;i<4;++i)copy[508+i]=(unsigned char)(crc>>(i*8));}
    CHECK(!Scope144_Decode(copy,123,&decoded));
    CHECK(Scope144_Newer(1,0));CHECK(Scope144_Newer(0,UINT_MAX));CHECK(!Scope144_Newer(0,0));CHECK(!Scope144_Newer(0,1));
    cal=bank.range[4];
    for(i=0;i<1024;++i)q[i]=(55+((i%144)>=72?125:0))*1024;
    CHECK(Scope144_Measure(&cal,q,1024,0,&m));
    CHECK(m.minimum_uv==0 && m.maximum_uv==5000000);CHECK(m.frequency_hz==1000000);
    CHECK(m.period_ps==1000000 && m.duty_ppm==500000 && m.high_ps==500000 && m.low_ps==500000);
    CHECK(m.average_uv==2460937);CHECK(m.rms_uv>3507000 && m.rms_uv<3509000);
    CHECK(Scope144_Measure(&cal,q,1024,1,&m));CHECK(m.frequency_hz==0 && m.period_ps==0 && m.duty_ppm==0);
    memset(&m,0x5a,sizeof(m));unchanged=m;q[10]=INT_MIN;
    CHECK(!Scope144_Measure(&cal,q,1024,0,&m));CHECK(!memcmp(&m,&unchanged,sizeof(m)));
    for(i=0;i<1024;++i)q[i]=55*1024;
    CHECK(Scope144_Measure(&cal,q,1024,0,&m));CHECK(m.frequency_hz==0 && m.rms_uv==0);
    CHECK(Scope144_TimePs(0)==0);CHECK(Scope144_TimePs(1)==6944);CHECK(Scope144_TimePs(1023)==7104167);
    CHECK(Scope144_Step(0)==31457);CHECK(Scope144_Step(1)==62915);CHECK(Scope144_Step(2)==157286);CHECK(!Scope144_Step(3));
    for(i=0;i<1024;++i)q[i]=(int32_t)i*1024;
    CHECK(Scope144_At(q,1024,(100u<<16)+32768,&value));CHECK(value==102912);
    CHECK(Scope144_At(q,1024,1023u<<16,&value));CHECK(value==1023*1024);
    CHECK(!Scope144_At(q,1024,(1023u<<16)+1,&value));CHECK(!Scope144_At(q,1024,1024u<<16,&value));
    for(r=0;r<3;++r)for(i=0;i<397;++i)CHECK(Scope144_At(q,1024,Scope144_Step(r)*i,&value));
    for(i=0;i<1024;++i)q[i]=(55+((i%144)>=72?125:0))*1024;
    for(r=0;r<3;++r){CHECK(Scope144_Trigger(&cal,q,1024,r,2500000,0,&start));CHECK(start+((Scope144_Step(r)*386+65535)>>16)<1024);}
    CHECK(Scope144_Trigger(&cal,q,1024,0,2500000,1,&start));
    CHECK(!Scope144_Trigger(&cal,q,1024,0,6000000,0,&start));
    CHECK(!Scope144_Trigger(&cal,q,1024,3,2500000,0,&start));
    CHECK(Scope144_CsvRow(row,&cal,0,55*1024,0)==strlen("0,0,0,B,0\r\n"));CHECK(!strcmp(row,"0,0,0,B,0\r\n"));
    CHECK(Scope144_CsvRow(row,&cal,1,180*1024,1));CHECK(!strcmp(row,"1,6944,5000000,A,1\r\n"));
    CHECK(!Scope144_CsvRow(row,&cal,1024,0,0));
    cal=bank.range[7];cal.reference_uv=INT_MAX;CHECK(!Scope144_Valid(&cal));
    cal=bank.range[4];
    for(i=0;i<1024;++i)q[i]=55*1024+(int32_t)i*1024;
    { int32_t dt,dv;
      CHECK(Scope144_Cursors(&cal,q,0,0,0,300,&dt,&dv));CHECK(dt>999990 && dt<1000010);CHECK(dv>5759900 && dv<5760100);
      CHECK(Scope144_Cursors(&cal,q,0,0,300,0,&dt,&dv));CHECK(dt<0 && dv<0);
      CHECK(Scope144_Cursors(&cal,q,0,0,30,30,&dt,&dv));CHECK(dt==0 && dv==0);
      CHECK(!Scope144_Cursors(&cal,q,1024,0,0,300,&dt,&dv));CHECK(!Scope144_Cursors(&cal,q,0,0,0,387,&dt,&dv));
    }
    { Scope144Measurements scaled;
      memset(&m,0,sizeof(m));m.minimum_uv=-1000000;m.maximum_uv=2000000;m.average_uv=500000;m.rms_uv=1000000;
      m.period_ps=1000000;m.duty_ppm=250000;m.high_ps=250000;m.low_ps=750000;
      CHECK(Scope144_Scale(&m,10,&scaled));CHECK(scaled.minimum_uv==-10000000 && scaled.maximum_uv==20000000 && scaled.rms_uv==10000000);
      CHECK(Scope144_Scale(&m,-10,&scaled));CHECK(scaled.minimum_uv==-20000000 && scaled.maximum_uv==10000000 && scaled.average_uv==-5000000);
      CHECK(scaled.duty_ppm==750000 && scaled.high_ps==750000 && scaled.low_ps==250000);
      CHECK(!Scope144_Scale(&m,0,&scaled));m.maximum_uv=INT_MAX;CHECK(!Scope144_Scale(&m,10,&scaled));
    }
    CHECK(Scope144_CsvScaled(row,&cal,1,180*1024,1,-10));CHECK(!strcmp(row,"1,6944,-50000000,A,1\r\n"));
    CHECK(!Scope144_CsvScaled(row,&cal,0,0,0,2));
    CHECK(Scope144_SetClock(100)==100);
    for(i=0;i<1024;++i)q[i]=(55+((i%144)>=72?125:0))*1024;
    CHECK(Scope144_Measure(&cal,q,1024,0,&m));CHECK(m.frequency_hz==1000100 && m.period_ps==999900);
    CHECK(Scope144_TimePs(144)==999900);CHECK(Scope144_Step(2)==157302);
    CHECK(Scope144_ClockLine(row));CHECK(!strcmp(row,"# clock_ppm=100\r\n"));
    CHECK(Scope144_SetClock(-100)==-100);CHECK(Scope144_TimePs(144)==1000100);
    CHECK(Scope144_SetClock(1000)==0);CHECK(Scope144_TimePs(144)==1000000);
    { int held=0,down=0;
      Scope144_RunKey(1,1,&down,&held);CHECK(held==1 && down==1);
      Scope144_RunKey(1,1,&down,&held);CHECK(held==1);
      Scope144_RunKey(0,1,&down,&held);CHECK(held==1 && down==0);
      Scope144_RunKey(0,1,&down,&held);CHECK(held==0); /* A short new press. */
      Scope144_RunKey(1,1,&down,&held);CHECK(held==1);
      held=1;Scope144_RunKey(1,1,&down,&held);CHECK(held==1); /* SGL must stay held. */
    }
    /* Traverse every native menu: no inaccessible channel or legacy file
     * handler can be reached while the interlace service owns acquisition. */
    {
        unsigned group=0,n,d;
        CHECK(Scope144_CouplingStep(2,1,1)==7);
        CHECK(Scope144_CouplingStep(7,1,1)==8);
        CHECK(Scope144_CouplingStep(8,1,1)==3);
        CHECK(Scope144_CouplingStep(7,-1,1)==2);
        CHECK(Scope144_CouplingStep(0,-1,1)==6);
        CHECK(Scope144_CouplingStep(6,1,0)==0);
        CHECK(Scope144_CouplingStep(0,-1,0)==6);
        for(n=0;n<21;++n) {
            CHECK(group!=4 && group<=21);
            for(d=0;d<Scope144_DetailLimit(group);++d)CHECK(Scope144_Field(group,d)<=45);
            CHECK(Scope144_Field(group,Scope144_DetailLimit(group))==34);
            CHECK(Scope144_GroupStep(Scope144_GroupStep(group,1),-1)==group);
            group=Scope144_GroupStep(group,1);
        }
        CHECK(group==0);CHECK(Scope144_Field(0,1)==26);CHECK(Scope144_Field(0,2)==0);
        CHECK(Scope144_Field(6,0)==5 && Scope144_Field(6,1)==1);
        CHECK(Scope144_Field(9,0)==32 && Scope144_Field(9,2)==33);
        CHECK(Scope144_Field(10,0)==8 && Scope144_Field(10,1)==9);
    }
    { char text[20]; unsigned n,group;
      const char *expected[]={"150.00 mV","300.00 mV","600.00 mV","1500.00 mV","3000.00 mV","6000.00 mV","15.000 V","30.000 V"};
      const int32_t uv[]={150000,300000,600000,1500000,3000000,6000000,15000000,30000000};
      for(n=0;n<8;++n){CalReference_Text(text,n,uv[n]);CHECK(!strcmp(text,expected[n]));}
      CalReference_Text(text,0,150005);CHECK(!strcmp(text,"150.01 mV"));
      CalReference_Text(text,7,15000500);CHECK(!strcmp(text,"15.001 V"));
      for(n=1;n<=3;++n)for(group=0;group<4;++group)CHECK(!Scope144_Available(n,group,0));
      CHECK(!Scope144_Available(6,2,0) && !Scope144_Available(6,3,0));
      CHECK(!Scope144_Available(7,3,0) && !Scope144_Available(9,3,0));
      CHECK(!Scope144_Available(5,2,0) && Scope144_Available(5,2,4));
    }
    printf("PASS Scope144: %u checks; calibration banks, eight ranges, timebase, meters, trigger, CSV\n",checks);
    return 0;
}
