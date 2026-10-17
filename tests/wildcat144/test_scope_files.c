#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BIOS.h"
#include "File.h"
#include "Scope144.h"
#include "Frame144.h"
#include "UiCommon.h"
static unsigned uart_owner;
static unsigned checks,opened,write_fail,read_fail,close_fail;
static char fail_name[12],close_write_name[12];static unsigned fail_after,opened_write;
static unsigned char native_current[512];
static struct { char name[12]; unsigned size; unsigned char bytes[524288]; int exists; } files[10];
u16 SectorSize;
u8 SecBuff[LARGEST_SECTOR_SIZE];u32 DataBuf[4096];
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
    if(cursor>=files[opened].size)return OK; /* AlterBIOS FatFs EOF: zero bytes, no IO error. */
    n=files[opened].size-cursor;if(n>SectorSize)n=SectorSize;
    memcpy(buffer,files[opened].bytes+cursor,n);cursor+=n;return OK;
}
u8 __ProgFileSec(u8 *buffer,u16 *cluster)
{
    unsigned fail=write_fail || (!memcmp(files[opened].name,fail_name,11) && cursor>=fail_after);
    unsigned n=fail?128:SectorSize;(void)cluster;
    CHECK(cursor+n<=sizeof files[opened].bytes);memcpy(files[opened].bytes+cursor,buffer,n);cursor+=n;
    if(cursor>files[opened].size)files[opened].size=cursor;
    return fail?WR_ERR:OK;
}
u8 __CloseFile(u8 *buffer,u32 length,u16 *cluster,u32 *directory)
{(void)buffer;(void)length;(void)cluster;(void)directory;return close_fail || (opened_write && !memcmp(files[opened].name,close_write_name,11))?WR_ERR:OK;}
static int32_t q[1024];static uint16_t pairs[664];
int main(void)
{
    unsigned sector,i,r,fail;Scope144Bank bank,loaded;
    Scope144Cal c={1024,0,55*1024,125*1024,5000000,1,1024,0,1024,1024,54,1024,1024};
    c.ref_dac_a_q10=1100;c.ref_dac_b_q10=990;
    for(sector=512;sector<=4096;sector*=8) {
        SectorSize=(u16)sector;memset(files,0,sizeof(files));memset(&bank,0,sizeof(bank));
        bank.battery[0]=3200;bank.battery[1]=4200;
        for(r=0;r<8;++r){bank.range[r]=c;bank.high[r]=c;bank.high[r].bias_q10=100+(int32_t)r;}
        Native(35);CHECK(!File144_CalLoad(&loaded));
        CHECK(File144_WptSave(&bank,1)==OK && bank.generation==1);
        CHECK(File144_CalLoad(&loaded));CHECK(!memcmp(&bank,&loaded,sizeof(bank)));
        {
            uint32_t crc;
            memset(SecBuff,0x5a,sizeof(SecBuff));crc=Scope144_Crc(SecBuff,sizeof(SecBuff));uart_owner=1;
            CHECK(File144_CalLoad(&loaded));CHECK(!memcmp(&bank,&loaded,sizeof(bank)));
            CHECK(Scope144_Crc(SecBuff,sizeof(SecBuff))==crc);
            CHECK(!File144_NativeLoad() && File144_WptSave(&bank,1)==UI_FILE_BUSY);
            CHECK(File144_Csv(&c,q,pairs,0,0,77,1)==UI_FILE_BUSY);
            CHECK(Scope144_Crc(SecBuff,sizeof(SecBuff))==crc && Find("I144077 CSV")==10);
            uart_owner=0;
        }
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        Native(79);bank.range[4].bias_q10=1024;
        CHECK(File144_WptSave(&bank,1)==OK && bank.generation==2);
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        /* Every write boundary in the older slot may fail; newest complete pair survives. */
        bank.range[4].bias_q10=2048;
        for(fail=0;fail<1536;fail+=512) {
            if(sector==4096 && fail)break;
            memcpy(fail_name,"12345678WPT",12);fail_after=fail;
            CHECK(File144_WptSave(&bank,1)==WR_ERR && bank.generation==2);fail_name[0]=0;
            CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2 && loaded.range[4].bias_q10==1024);
            CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        }
        CHECK(File144_WptSave(&bank,1)==OK && bank.generation==3);
        read_fail=1;CHECK(!File144_CalLoad(&loaded));CHECK(File144_CalSave(&bank)==RD_ERR);read_fail=0;
        close_fail=1;CHECK(File144_CalSave(&bank)==RD_ERR);close_fail=0;
        bank.range[4].gain_q10=0;CHECK(File144_CalSave(&bank)==VER_ERR);bank.range[4].gain_q10=1024;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==3);
        /* CRC binds the native prefix and extension as one calibration transaction. */
        i=Find("12345678WPT");CHECK(i<10);files[i].bytes[7]^=1;files[i].bytes[8]^=1;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==2);
        CHECK(File144_NativeLoad());CHECK(!memcmp(SecBuff,native_current,512));
        /* A close error may leave a complete newer generation; load uses it only if valid. */
        memcpy(close_write_name,"12345678WPT",12);
        CHECK(File144_CalSave(&bank)==WR_ERR && bank.generation==3);close_write_name[0]=0;
        CHECK(File144_CalLoad(&loaded));CHECK(loaded.generation==4);
        for(r=0;r<8;++r)CHECK(Scope144_Valid(&loaded.range[r]) && Scope144_Valid(&loaded.high[r]));
        for(i=0;i<1024;++i)q[i]=55*1024;
        for(i=0;i<664;++i)pairs[i]=55|(55<<8);
        CHECK(File144_Csv(&c,q,pairs,4,0,0,1)==OK);CHECK(File144_Csv(&c,q,pairs,4,0,0,1)==200);
        i=Find("I144000 CSV");CHECK(i<10);
        CHECK(strstr((char*)files[i].bytes,"sample,time_ps,A_uV,adc,raw_clip\r\n"));
        CHECK(strstr((char*)files[i].bytes,"0,0,0,B,0\r\n1,6944,0,A,0\r\n"));
        CHECK(strstr((char*)files[i].bytes,"1023,7104167,0,A,0\r\n"));
        CHECK(memchr(files[i].bytes,0,files[i].size)==NULL);
        for(i=0;i<1024;++i)q[i]=180*1024;
        CHECK(File144_Csv(&c,q,pairs,4,0,1,-10)==OK);
        i=Find("I144001 CSV");CHECK(i<10);
        CHECK(strstr((char*)files[i].bytes,"1,6944,-50000000,A,1\r\n")==NULL);
        CHECK(strstr((char*)files[i].bytes,"1,6944,-50000000,A,0\r\n"));
        /* Old formats never activate calibration or cause writes during load. */
        memset(files,0,sizeof(files));Add("12345678WPT",native_current,512);
        CHECK(!File144_CalLoad(&loaded));CHECK(!File144_NativeLoad());CHECK(Find("12345678WPB")==10);
        CHECK(File144_WptSave(&bank,1)==OK);CHECK(File144_CalLoad(&loaded));
        /* Failed first save does not manufacture a valid calibration. */
        memset(files,0,sizeof(files));write_fail=1;CHECK(File144_WptSave(&bank,1)==WR_ERR);write_fail=0;
        CHECK(!File144_CalLoad(&loaded));
    }
    /* Full-frame CSV reads DataBuf without borrowing the calibration scratch
     * at its end; sector writes must not damage the frozen capture. */
    { Frame144 frame;unsigned j,hash,last,hw,count;char row[80];
      for(sector=512;sector<=4096;sector*=8) {
       for(hw=0;hw<2;++hw) {
        SectorSize=(u16)sector;memset(files,0,sizeof files);
        Frame144_Reset(&frame,77);frame.words=(const uint32_t *)DataBuf;frame.range=4;frame.calibration=c;
        frame.capture.status=CAPTURE_WINDOW_READY;frame.capture.fifo_words=4096;
        frame.capture.valid_begin=152;frame.capture.valid_words=3944;frame.capture.time_samples=7888;
        frame.capture.sample_phase=CAPTURE_PHASE_B_A;frame.capture.flags=CAPTURE_WINDOW_FROZEN|CAPTURE_WINDOW_TRUNCATED;
        if(hw) {
            frame.capture.valid_begin=2;frame.capture.valid_words=4094;frame.capture.time_samples=8188;
            frame.capture.trigger_kind=CAPTURE_TRIGGER_HARDWARE_A;frame.capture.trigger_q16=295u*65536;
            frame.capture.fifo_status=0x2c;frame.capture.flags|=CAPTURE_WINDOW_HAS_TRIGGER|CAPTURE_WINDOW_PRETRIGGER;
        }
        count=frame.capture.time_samples;
        frame.capture.first_visible_q16=1234567;
        Scope144_SetClock(0);frame.capture.sample_period_ps_q16=Scope144_TimePs(65536);
        for(j=0;j<4096;++j)DataBuf[j]=(55u*257u)|((j&3)<<16);
        DataBuf[4095]=(255u<<8)|180u|0x30000u;
        hash=Scope144_Crc(DataBuf,sizeof DataBuf);
        Scope144_SetClock(999);
        CHECK(File144_CsvFrame(&frame,2,8,-10)==OK);
        CHECK(File144_CsvFrame(&frame,2,8,-10)==200);
        CHECK(Scope144_Crc(DataBuf,sizeof DataBuf)==hash && frame.capture.epoch==77);
        i=Find("I144008 CSV");CHECK(i<10);
        CHECK(strstr((char*)files[i].bytes,"# capture_epoch=77\r\n"));
        CHECK(strstr((char*)files[i].bytes,"# clock_ppm=0\r\n"));
        CHECK(strstr((char*)files[i].bytes,"# first_visible_q16=1234567\r\n"));
        CHECK(strstr((char*)files[i].bytes,"# format=144CD-v2\r\n"));
        CHECK(strstr((char*)files[i].bytes,"sample,time_ps,A_uV,adc,raw_clip,C,D,cd_valid\r\n"));
        CHECK(strstr((char*)files[i].bytes,hw?"# trigger_kind=2\r\n":"# trigger_kind=0\r\n"));
        CHECK(strstr((char*)files[i].bytes,hw?"# trigger_q16=19333120\r\n":"# trigger_q16=0\r\n"));
        CHECK(strstr((char*)files[i].bytes,hw?"# valid_begin=2\r\n":"# valid_begin=152\r\n"));
        CHECK(strstr((char*)files[i].bytes,hw?"# valid_words=4094\r\n":"# valid_words=3944\r\n"));
        CHECK(Frame144_CsvRow(row,&frame,count-2,-10) && strstr((char*)files[i].bytes,row));
        CHECK(Frame144_CsvRow(row,&frame,count-1,-10) && strstr((char*)files[i].bytes,row));
        CHECK(!strstr((char*)files[i].bytes,hw?"\n8188,":"\n7888,"));
        last=0;
        for(j=0;j+2<files[i].size;++j)if(files[i].bytes[j]=='\n' &&
            files[i].bytes[j+1]>='0' && files[i].bytes[j+1]<='9')++last;
        CHECK(last==count && memchr(files[i].bytes,0,files[i].size)==NULL);
        frame.capture.status=CAPTURE_WINDOW_TIMEOUT;
        CHECK(File144_CsvFrame(&frame,2,9,1)==VER_ERR && Find("I144009 CSV")==10);
       }
      }
      Scope144_SetClock(0);
    }
    printf("PASS Scope144 v3 file IO: %u checks; two profiles, matching native prefix, alternating slots, 512/4096B faults, legacy/full CSV\n",checks);return 0;
}

int Generator_SecBusy(void){return (int)uart_owner;}
