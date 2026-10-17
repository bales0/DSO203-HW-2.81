#include "BIOS.h"
#include "File.h"
#include "Scope144.h"
#include "Frame144.h"
#include "UiCommon.h"
#include <string.h>
extern u8 InitFileSystem(void);
extern u8 WriteFileSec(u8 *,u16 *,u32 *);
extern void storeParameter(void);
extern u32 DataBuf[4096];
/* File operations run with acquisition stopped. Use the final 4 KiB of its
 * existing RAM, disjoint from CalWorkspace at the beginning of DataBuf.
 * No multi-kilobyte image or bank copies on the small MCU stack. */
typedef struct {
    u8 image[1536],native[512];
    Scope144Bank candidate,latest;
} FileCalScratch;
typedef char FileScratchFits[(sizeof(FileCalScratch)<=4096)?1:-1];
#define SCRATCH ((FileCalScratch *)(DataBuf+3072))
static void WptName(char name[12],int slot)
{
    static const char digits[]="0123456789ABCDEF";
    uint32_t serial=__GetDev_SN();unsigned i;
    for(i=0;i<8;++i)name[i]=digits[(serial>>(28-4*i))&15];
    memcpy(name+8,slot?"WPB":"WPT",4);
}
static int NativeValid(const u8 *bytes)
{
    unsigned i,sum=0;for(i=0;i<512;++i)sum+=bytes[i];
    return bytes[0]==6 && !(sum&255);
}
/* 1 valid v3, 0 missing/invalid, -1 IO error. No legacy imports. */
static int ReadWpt(unsigned slot,Scope144Bank *bank,u8 native[512])
{
    FileCalScratch *w=SCRATCH;char name[12];u16 cluster[3];u32 directory[1];
    unsigned offset=0;int result=0,status;
    u8 *io=(u8 *)(DataBuf+2048); /* Existing unused middle 4KiB; UART SecBuff stays intact. */
    WptName(name,slot);status=__OpenFileRd(io,name,cluster,directory);
    if(status!=OK)return status==NEW?0:-1;
    memset(w->image,0,sizeof(w->image));
    while(offset<sizeof(w->image)) {
        unsigned length=sizeof(w->image)-offset;if(length>SectorSize)length=SectorSize;
        memset(io,0,SectorSize);
        if(__ReadFileSec(io,cluster)!=OK){result=-1;break;}
        memcpy(w->image+offset,io,length);offset+=length;
    }
    if(offset==sizeof(w->image) && NativeValid(w->image) &&
       Scope144_Decode(w->image+512,__GetDev_SN(),bank) &&
       bank->native_crc==Scope144_Crc(w->image,512)) {
        if(native)memcpy(native,w->image,512);
        result=1;
    }
    if(__CloseFile(io,0,cluster,directory)!=OK)result=-1;
    return result;
}
/* Always choose native parameters and 144 coefficients from the SAME slot. */
static int Latest(unsigned *slot,int strict)
{
    FileCalScratch *w=SCRATCH;int a,b;
    if(SectorSize!=512 && SectorSize!=4096)return -1;
    memset(&w->latest,0,sizeof(w->latest));*slot=0;
    a=ReadWpt(0,&w->latest,w->native);
    b=ReadWpt(1,&w->candidate,0);
    if(strict && (a<0 || b<0))return -1;
    if(b==1 && (a!=1 || Scope144_Newer(w->candidate.generation,w->latest.generation))) {
        /* Reread also fetches the matching native prefix. */
        if(ReadWpt(1,&w->latest,w->native)!=1)return -1;
        *slot=1;return 1;
    }
    return a==1?1:0;
}
int File144_CalLoad(Scope144Bank *bank)
{
    unsigned slot;memset(bank,0,sizeof(*bank));
    if(InitFileSystem()!=OK || Latest(&slot,0)!=1)return 0;
    *bank=SCRATCH->latest;return 1;
}
int File144_NativeLoad(void)
{
    if(Generator_SecBusy())return 0;
    unsigned slot;
    if(InitFileSystem()!=OK || Latest(&slot,0)!=1)return 0;
    memset(SecBuff,0,SectorSize);memcpy(SecBuff,SCRATCH->native,512);return 1;
}
static int WriteWpt(unsigned slot)
{
    FileCalScratch *w=SCRATCH;char name[12];u16 cluster[3];u32 directory[1];
    unsigned offset=0;int status=OK;
    WptName(name,slot);
    if(__OpenFileWr(SecBuff,name,cluster,directory)!=OK)return DISK_ERR;
    while(!status && offset<sizeof(w->image)) {
        unsigned length=sizeof(w->image)-offset;if(length>SectorSize)length=SectorSize;
        memset(SecBuff,0,SectorSize);memcpy(SecBuff,w->image+offset,length);
        status=WriteFileSec(SecBuff,cluster,directory);offset+=length;
    }
    if(__CloseFile(SecBuff,status?0:sizeof(w->image),cluster,directory)!=OK && !status)status=WR_ERR;
    return status;
}
int File144_WptSave(Scope144Bank *bank,int native_updated)
{
    if(Generator_SecBusy())return UI_FILE_BUSY;
    FileCalScratch *w=SCRATCH;unsigned slot,target;int prior,status;
    uint32_t generation=bank->generation,native_crc=bank->native_crc,expected_crc;
    if(InitFileSystem()!=OK)return DISK_ERR;
    /* Reject bad coefficient structures before touching any file. */
    if(!Scope144_Encode(bank,__GetDev_SN(),w->image+512))return VER_ERR;
    prior=Latest(&slot,1);if(prior<0)return RD_ERR;
    target=prior?1-slot:0;
    if(native_updated || !prior) {
        unsigned i,sum=0;memset(SecBuff,0,SectorSize);storeParameter();
        if(SecBuff[19]==0 && (SecBuff[18]==SCOPE144_DC || SecBuff[18]==SCOPE144_AC)) {
            SecBuff[18]=(u8)(SecBuff[18]==SCOPE144_AC?1:0);
            for(i=0;i<511;++i)sum+=SecBuff[i];
            SecBuff[511]=(u8)(0-sum);
        }
        memcpy(w->native,SecBuff,512);
    }
    if(!NativeValid(w->native))return VER_ERR;
    bank->generation=(prior && Scope144_Newer(w->latest.generation,generation)?w->latest.generation:generation)+1;
    bank->native_crc=Scope144_Crc(w->native,512);
    memcpy(w->image,w->native,512);
    if(!Scope144_Encode(bank,__GetDev_SN(),w->image+512)){status=VER_ERR;goto failed;}
    expected_crc=Scope144_Crc(w->image,sizeof(w->image));
    status=WriteWpt(target);
    if(status)goto failed;
    if(ReadWpt(target,&w->candidate,0)!=1 ||
       Scope144_Crc(w->image,sizeof(w->image))!=expected_crc || memcmp(bank,&w->candidate,sizeof(*bank))) {
        status=RD_ERR;goto failed;
    }
    return OK;
failed:
    /* The latest valid slot was never opened for writing. A completed new
     * generation after ambiguous close is recoverable on the next load. */
    bank->generation=generation;bank->native_crc=native_crc;return status;
}
int File144_CalSave(Scope144Bank *bank) { return File144_WptSave(bank,0); }
static int File144_Text(const char *text,unsigned length,u16 *cluster,u32 *directory,unsigned *used)
{
    while(length--) {
        SecBuff[(*used)++]=(u8)*text++;
        if(*used==SectorSize) {
            int error=WriteFileSec(SecBuff,cluster,directory);
            *used=0; if(error) return error;
        }
    }
    return OK;
}
static int CsvField(const char *label,int64_t value,char line[80],u16 *cluster,
    u32 *directory,unsigned *used,unsigned *total)
{
    unsigned length=0,n=0;char digits[10];
    line[length++]='#';line[length++]=' ';
    while(*label)line[length++]=*label++;
    line[length++]='=';
    if(value<0){line[length++]='-';value=-value;}
    do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    while(n)line[length++]=digits[--n];
    line[length++]='\r';line[length++]='\n';*total+=length;
    return File144_Text(line,length,cluster,directory,used);
}
static int WriteCsv(const Scope144Cal *cal,const int32_t *q10,const uint16_t *pairs,
    const Frame144 *frame,unsigned range,unsigned base,unsigned number,int factor)
{
    char name[12]="I144000 CSV",line[80]; u16 cluster[3]; u32 directory[1];
    unsigned i,used=0,total=0; u8 error;
    const char *header="# Wildcat V6.6; FPGA W1.2; nominal 144000000 Sa/s; B->A; microvolts at probe tip\r\n"
        "# t=0 is first valid sample; calibration is serial/range/FPGA bound\r\n";
    if(frame && frame->capture.sample_phase==CAPTURE_PHASE_A_B)
        header="# Wildcat V6.6; FPGA W1.2; nominal 144000000 Sa/s; A->B; microvolts at probe tip\r\n"
            "# t=0 is first valid sample; calibration is serial/range/FPGA bound\r\n";
    if(!Scope144_Valid(cal) || (frame?!Frame144_Valid(frame):(!q10 || !pairs)) || range>=8 || base>=3 || number>999 || (factor!=1 && factor!=-1 && factor!=10 && factor!=-10)) return VER_ERR;
    if(Generator_SecBusy())return UI_FILE_BUSY;
    if(InitFileSystem()!=OK) return DISK_ERR;
    name[4]=(char)('0'+number/100); name[5]=(char)('0'+number/10%10); name[6]=(char)('0'+number%10);
    error=__OpenFileRd(SecBuff,name,cluster,directory);
    if(error==OK) { __CloseFile(SecBuff,0,cluster,directory); return 200; } /* Never overwrite a capture. */
    if(error!=NEW) return RD_ERR;
    if(__OpenFileWr(SecBuff,name,cluster,directory)!=OK) return DISK_ERR;
    error=File144_Text(header,strlen(header),cluster,directory,&used); total=strlen(header);
    if(!error && frame) {
        error=File144_Text("# format=144CD-v2\r\n",19,cluster,directory,&used);total+=19;
        if(!error)error=CsvField("digital_shift_q16",frame->digital_shift_q16,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("trigger_source",frame->trigger_source,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("capture_epoch",frame->capture.epoch,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("valid_begin",frame->capture.valid_begin,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("valid_words",frame->capture.valid_words,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("time_samples",frame->capture.time_samples,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("trigger_kind",frame->capture.trigger_kind,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("trigger_q16",frame->capture.trigger_q16,line,cluster,directory,&used,&total);
        if(!error)error=CsvField("first_visible_q16",frame->capture.first_visible_q16,line,cluster,directory,&used,&total);
    }
    if(!error) {
        const char *probe=(factor==10 || factor==-10)?"# probe_x=10\r\n":"# probe_x=1\r\n";
        const char *inversion=factor<0?"# inverted=1\r\n":"# inverted=0\r\n";
        error=File144_Text(probe,strlen(probe),cluster,directory,&used);total+=strlen(probe);
        if(!error) {error=File144_Text(inversion,strlen(inversion),cluster,directory,&used);total+=strlen(inversion);}
    }
    if(!error) {
        unsigned length;
        if(frame) {
            uint64_t denominator=9u*frame->capture.sample_period_ps_q16;
            int ppm=(int)((62500ULL*1000000*65536+denominator/2)/denominator)-1000000;
            length=Scope144_ClockValueLine(line,ppm);
        } else length=Scope144_ClockLine(line);
        error=File144_Text(line,length,cluster,directory,&used);total+=length;
    }
    if(!error) {
        const char *columns=frame?"sample,time_ps,A_uV,adc,raw_clip,C,D,cd_valid\r\n":
            "sample,time_ps,A_uV,adc,raw_clip\r\n";
        error=File144_Text(columns,strlen(columns),cluster,directory,&used);total+=strlen(columns);
    }
    for(i=0;i<(frame?frame->capture.time_samples:1024u) && !error;++i) {
        unsigned length;
        if(frame)length=Frame144_CsvRow(line,frame,i,factor);
        else {
            unsigned pair=pairs[152+i/2],raw=(i&1)?(pair&255):(pair>>8);
            length=Scope144_CsvScaled(line,cal,i,q10[i],raw==0 || raw==255,factor);
        }
        if(!length) { error=VER_ERR; break; }
        error=File144_Text(line,length,cluster,directory,&used); total+=length;
    }
    if(!error && used) {
        /* Older SYS versions retain sector padding; keep it valid CSV there. */
        memset(SecBuff+used,'\n',SectorSize-used);
        error=WriteFileSec(SecBuff,cluster,directory);
    }
    if(__CloseFile(SecBuff,error?0:total,cluster,directory)!=OK && !error) error=WR_ERR;
    return error;
}
int File144_Csv(const Scope144Cal *cal,const int32_t *q10,const uint16_t *pairs,
    unsigned range,unsigned base,unsigned number,int factor)
{ return WriteCsv(cal,q10,pairs,0,range,base,number,factor); }
int File144_CsvFrame(const Frame144 *frame,unsigned base,unsigned number,int factor)
{
    if(!Frame144_Valid(frame))return VER_ERR;
    return WriteCsv(&frame->calibration,0,0,frame,frame->range,base,number,factor);
}
