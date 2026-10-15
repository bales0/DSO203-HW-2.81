#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "File.h"
#include "Scope144.h"
static unsigned checks,opened,write_fail,read_fail,close_fail;
static char fail_name[12],close_write_name[12];static unsigned fail_after,opened_write;
static unsigned char native_current[512];
static struct { char name[12]; unsigned size; unsigned char bytes[65536]; int exists; } files[10];
u16 SectorSize;
u8 SecBuff[LARGEST_SECTOR_SIZE];
static unsigned cursor;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} }while(0)
u8 InitFileSystem(void) { return OK; }
void storeParameter(void) { memcpy(SecBuff,native_current,512); }
static void Native(unsigned value)
{
    unsigned i,sum=0;for(i=0;i<511;++i)native_current[i]=(unsigned char)(value+i);
    native_current[0]=6;
    for(i=0;i<511;++i)sum+=native_current[i];
    native_current[511]=(unsigned char)(0-sum);
}
static unsigned Find(const char *name)
{
    unsigned i;for(i=0;i<10;++i)if(files[i].exists && !memcmp(files[i].name,name,11))return i;
    return 10;
}
static void Add(const char *name,const unsigned char *bytes,unsigned length)
{
    unsigned i=Find(name);if(i==10)for(i=0;i<10 && files[i].exists;++i) { }
    CHECK(i<10);files[i].exists=1;memcpy(files[i].name,name,11);
    files[i].size=length;memcpy(files[i].bytes,bytes,length);
}
u8 WriteFileSec(u8 *buffer,u16 *cluster,u32 *directory)
{ (void)directory; return __ProgFileSec(buffer,cluster)==OK?OK:WR_ERR; }
u32 __Get(u8 object) { (void)object; abort(); }
u32 __GetDev_SN(void) { return 0x12345678; }
void u8ToDec3(char *out,u16 value,u8 service) { (void)service;out[0]='0'+value/100;out[1]='0'+value/10%10;out[2]='0'+value%10;out[3]=0; }
u8 __OpenFileRd(u8 *buffer,char *name,u16 *cluster,u32 *directory)
{
    unsigned i;(void)directory;
    for(i=0;i<10;++i)if(files[i].exists && !memcmp(name,files[i].name,11)) {
        opened=i;opened_write=0;cursor=0;*cluster=1;memset(buffer,0xa5,SectorSize);return OK;
    }
    return NEW;
}
u8 __OpenFileWr(u8 *buffer,char *name,u16 *cluster,u32 *directory)
{
    unsigned i;(void)directory;
    for(i=0;i<10;++i)if(files[i].exists && !memcmp(name,files[i].name,11))break;
    if(i==10)for(i=0;i<10;++i)if(!files[i].exists)break;
    if(i==10)return DISK_ERR;
    opened=i;opened_write=1;cursor=0;files[i].exists=1;files[i].size=0;memcpy(files[i].name,name,11);
    *cluster=1;memset(buffer,0xa5,SectorSize);return OK;
}
u8 __ReadFileSec(u8 *buffer,u16 *cluster)
{
    unsigned n;(void)cluster;if(read_fail)return RD_ERR;
    if(cursor>=files[opened].size)return RD_ERR;
    n=files[opened].size-cursor;if(n>SectorSize)n=SectorSize;
    memcpy(buffer,files[opened].bytes+cursor,n);cursor+=n;return OK;
}
u8 __ProgFileSec(u8 *buffer,u16 *cluster)
{
    unsigned fail=write_fail || (!memcmp(files[opened].name,fail_name,11) && cursor>=fail_after);
    unsigned n=fail?128:SectorSize;(void)cluster;
    CHECK(cursor+n<=65536);memcpy(files[opened].bytes+cursor,buffer,n);cursor+=n;
    if(cursor>files[opened].size)files[opened].size=cursor;
    return fail?WR_ERR:OK;
}
u8 __CloseFile(u8 *buffer,u32 length,u16 *cluster,u32 *directory)
{(void)buffer;(void)length;(void)cluster;(void)directory;return close_fail || (opened_write && !memcmp(files[opened].name,close_write_name,11))?WR_ERR:OK;}
static int32_t q[1024];static uint16_t pairs[664];
int main(void)
{
    unsigned sector,i;Scope144Bank bank,loaded;Scope144Cal c={1024,0,55*1024,125*1024,5000000,1};
    for(sector=512;sector<=4096;sector*=8) {
        SectorSize=(u16)sector;memset(files,0,sizeof(files));memset(&bank,0,sizeof(bank));bank.range[4]=c;
        Native(35);
        CHECK(!File144_CalLoad(&loaded));
        /* A real legacy WPT: native prefix only, no interlace extension. */
        Add("12345678WPT",native_current,512);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        CHECK(!File144_CalLoad(&loaded));
        CHECK(File144_CalSave(&bank)==OK && bank.generation==1);
        i=Find("12345678WPT");CHECK(i<10);CHECK(!memcmp(files[i].bytes,native_current,512));
        CHECK(files[i].size>=1024);CHECK(File144_CalLoad(&loaded));CHECK(!memcmp(&bank,&loaded,sizeof(bank)));
        bank.range[4].bias_q10=1024;CHECK(File144_CalSave(&bank)==OK && bank.generation==2);
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
        bank.range[4].bias_q10=2048;write_fail=1;
        CHECK(File144_CalSave(&bank)==WR_ERR && bank.generation==2);write_fail=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
        /* Fail primary after the backup has been completely verified. */
        memcpy(fail_name,"12345678WPT",12);fail_after=0;
        CHECK(File144_CalSave(&bank)==WR_ERR && bank.generation==2);fail_name[0]=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        /* Recovery must not rewrite the only valid backup before primary. */
        memcpy(fail_name,"12345678WPT",12);fail_after=0;
        CHECK(File144_CalSave(&bank)==WR_ERR);fail_name[0]=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2);
        CHECK(File144_CalSave(&bank)==OK && bank.generation==3);
        read_fail=1;CHECK(!File144_CalLoad(&loaded));CHECK(File144_CalSave(&bank)==RD_ERR);read_fail=0;
        close_fail=1;CHECK(File144_CalSave(&bank)==RD_ERR);close_fail=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.range[4].valid);
        /* Native calibration save keeps interlace, interlace save keeps native. */
        Native(79);CHECK(File144_WptSave(&bank,1)==OK);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.range[4].bias_q10==2048);
        Native(12);CHECK(File144_CalSave(&bank)==OK);
        i=Find("12345678WPT");CHECK(i<10);CHECK(files[i].bytes[1]==80);
        /* Native save of a new coupling must remain readable by old Wildcat. */
        Native(79);native_current[18]=SCOPE144_AC;native_current[19]=0;
        {unsigned k,sum=0;for(k=0;k<511;++k)sum+=native_current[k];native_current[511]=(unsigned char)(0-sum);}
        CHECK(File144_WptSave(&bank,1)==OK);CHECK(File144_NativeLoad());CHECK(SecBuff[18]==1 && SecBuff[19]==0);
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.range[4].bias_q10==2048);
        Native(79);CHECK(File144_WptSave(&bank,1)==OK);
        /* Corrupt primary native checksum: recover the complete backup. */
        files[i].bytes[7]^=1;CHECK(File144_NativeLoad());CHECK(SecBuff[1]==80);
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.range[4].valid);
        for(i=0;i<1024;++i)q[i]=55*1024;
        for(i=0;i<664;++i)pairs[i]=55|(55<<8);
        CHECK(File144_Csv(&c,q,pairs,4,0,0,1)==OK);
        CHECK(File144_Csv(&c,q,pairs,4,0,0,1)==200);
        for(i=0;i<10;++i)if(!memcmp(files[i].name,"I144000 CSV",11))break;
        CHECK(i<10);CHECK(strstr((char*)files[i].bytes,"sample,time_ps,A_uV,adc,raw_clip\r\n"));
        CHECK(strstr((char*)files[i].bytes,"0,0,0,B,0\r\n1,6944,0,A,0\r\n"));
        CHECK(strstr((char*)files[i].bytes,"1023,7104167,0,A,0\r\n"));
        CHECK(memchr(files[i].bytes,0,files[i].size)==NULL);
        for(i=0;i<1024;++i)q[i]=180*1024;
        CHECK(File144_Csv(&c,q,pairs,4,0,1,-10)==OK);
        for(i=0;i<10;++i)if(!memcmp(files[i].name,"I144001 CSV",11))break;
        CHECK(i<10);CHECK(strstr((char*)files[i].bytes,"# probe_x=10\r\n# inverted=1\r\n"));
        CHECK(strstr((char*)files[i].bytes,"1,6944,-50000000,A,0\r\n"));
    }
    for(sector=512;sector<=4096;sector*=8) {
        unsigned char extension[512];unsigned primary;
        SectorSize=(u16)sector;memset(files,0,sizeof(files));memset(&bank,0,sizeof(bank));
        Native(22);Add("12345678WPT",native_current,512);
        bank.range[4]=c;bank.generation=5;
        CHECK(Scope144_Encode(&bank,0x12345678,extension));Add("IC144B  CAL",extension,512);
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==5);
        CHECK(Find("12345678WPB")==10); /* Reading migration does not write. */
        CHECK(File144_CalSave(&loaded)==OK && loaded.generation==6);
        CHECK(File144_CalLoad(&bank));CHECK(bank.generation==6);
        primary=Find("12345678WPT");CHECK(primary<10);CHECK(!memcmp(files[primary].bytes,native_current,512));
        CHECK(Find("IC144B  CAL")<10); /* Do not delete user's legacy files. */
        if(sector==512) {
            bank.range[4].bias_q10=1234;memcpy(fail_name,"12345678WPT",12);fail_after=512;
            CHECK(File144_CalSave(&bank)==WR_ERR);fail_name[0]=0;
            CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==6 && loaded.range[4].bias_q10==0);
        }
        /* Corrupt only extension: native data remains readable. */
        files[primary].bytes[512]^=1;CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        /* A damaged extension must not replace a good backup with an empty bank. */
        CHECK(File144_CalLoad(&loaded));
        memcpy(fail_name,"12345678WPT",12);fail_after=0;
        CHECK(File144_CalSave(&loaded)==WR_ERR);fail_name[0]=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.range[4].valid);
        /* Invalid coefficients must cause no write/truncation. */
        bank.range[4].gain_q10=0;CHECK(File144_CalSave(&bank)==VER_ERR);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
    }
    for(sector=512;sector<=4096;sector*=8) {
        SectorSize=(u16)sector;memset(files,0,sizeof(files));memset(&bank,0,sizeof(bank));
        Native(63);bank.range[4]=c;
        CHECK(File144_CalSave(&bank)==OK && bank.generation==1);
        bank.range[4].bias_q10=1024;memcpy(close_write_name,"12345678WPT",12);
        CHECK(File144_CalSave(&bank)==WR_ERR && bank.generation==1);close_write_name[0]=0;
        /* A failed close may leave a complete new generation: validate it. */
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        /* A backup close failure must leave the primary intact. */
        bank=loaded;bank.range[4].bias_q10=2048;memcpy(close_write_name,"12345678WPB",12);
        CHECK(File144_CalSave(&bank)==WR_ERR && bank.generation==2);close_write_name[0]=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
    }
    printf("PASS Scope144 file IO: %u checks; common WPT + WPB, native preservation, migration, 512/4096B faults, CSV\n",checks);return 0;
}
