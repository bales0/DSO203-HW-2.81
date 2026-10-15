#include "BIOS.h"
#include "File.h"
#include "Scope144.h"
#include <string.h>
extern u8 InitFileSystem(void);
extern u8 WriteFileSec(u8 *,u16 *,u32 *);
/* The original 512-byte WPT prefix remains byte-compatible. The second
 * 512-byte record is the serial/FPGA-bound interlace extension. WPB is a
 * verified complete backup, not a separate calibration format. */
extern void storeParameter(void);
static void WptName(char name[12],int backup)
{
    static const char digits[]="0123456789ABCDEF";
    uint32_t serial=__GetDev_SN(); unsigned i;
    for(i=0;i<8;++i)name[i]=digits[(serial>>(28-4*i))&15];
    memcpy(name+8,backup?"WPB":"WPT",4);
}
static int NativeValid(const u8 *bytes)
{
    unsigned i,sum=0;
    for(i=0;i<512;++i)sum+=bytes[i];
    return bytes[0]==6 && !(sum&255);
}
/* -1=IO failure, 0=invalid/missing, 1=legacy prefix, 2=validated extension. */
static int ReadWpt(char *name,u8 *native,Scope144Bank *bank,uint32_t *native_crc)
{
    u16 cluster[3];u32 directory[1];int result=0;
    memset(bank,0,sizeof(*bank));
    {int status=__OpenFileRd(SecBuff,name,cluster,directory);if(status!=OK)return status==NEW?0:-1;}
    memset(SecBuff,0,SectorSize);
    if(__ReadFileSec(SecBuff,cluster)!=OK)result=-1;
    else if(NativeValid(SecBuff)) {
        result=1;if(native)memcpy(native,SecBuff,512);
        if(native_crc)*native_crc=Scope144_Crc(SecBuff,512);
        if(SectorSize==512) {
            memset(SecBuff,0,512);
            if(__ReadFileSec(SecBuff,cluster)==OK && Scope144_Decode(SecBuff,__GetDev_SN(),bank))result=2;
        } else if(Scope144_Decode(SecBuff+512,__GetDev_SN(),bank))result=2;
    }
    if(__CloseFile(SecBuff,0,cluster,directory)!=OK)result=-1;
    return result;
}
static __attribute__((noinline)) int LegacyBank(Scope144Bank *bank)
{
    Scope144Bank candidate; unsigned slot;int found=0;
    char name[12]="IC144A  CAL";u16 cluster[3];u32 directory[1];
    memset(bank,0,sizeof(*bank));
    for(slot=0;slot<2;++slot) {
        int good=0;name[5]=(char)('A'+slot);
        if(__OpenFileRd(SecBuff,name,cluster,directory)!=OK)continue;
        memset(SecBuff,0,SectorSize);
        good=__ReadFileSec(SecBuff,cluster)==OK && Scope144_Decode(SecBuff,__GetDev_SN(),&candidate);
        if(__CloseFile(SecBuff,0,cluster,directory)!=OK)good=0;
        if(good && (candidate.generation&1)==slot && (!found || Scope144_Newer(candidate.generation,bank->generation))) {
            *bank=candidate;found=1;
        }
    }
    return found;
}
int File144_CalLoad(Scope144Bank *bank)
{
    Scope144Bank candidate;char name[12];int found=0,primary;unsigned slot;
    memset(bank,0,sizeof(*bank));if(InitFileSystem()!=OK)return 0;
    WptName(name,0);primary=ReadWpt(name,0,bank,0);
    if(primary==2)found=1;
    for(slot=1;slot<2;++slot) {
        WptName(name,1);
        if(ReadWpt(name,0,&candidate,0)==2 && (!found || Scope144_Newer(candidate.generation,bank->generation))) {
            *bank=candidate;found=1;
        }
    }
    /* Import only when there is no valid common-file extension. No writes. */
    if(!found)found=LegacyBank(bank);
    return found;
}
int File144_NativeLoad(void)
{
    Scope144Bank bank;u8 native[512];char name[12];int good;
    if(InitFileSystem()!=OK)return 0;
    WptName(name,0);good=ReadWpt(name,native,&bank,0);
    if(good<=0){WptName(name,1);good=ReadWpt(name,native,&bank,0);}
    if(good>0){memset(SecBuff,0,SectorSize);memcpy(SecBuff,native,512);}
    return good>0;
}
static int WriteWpt(char *name,const u8 native[512],const Scope144Bank *bank)
{
    u16 cluster[3];u32 directory[1];int status;
    if(!NativeValid(native))return SUM_ERR;
    if(__OpenFileWr(SecBuff,name,cluster,directory)!=OK)return DISK_ERR;
    memset(SecBuff,0,SectorSize);memcpy(SecBuff,native,512);
    if(SectorSize>512 && !Scope144_Encode(bank,__GetDev_SN(),SecBuff+512))return VER_ERR;
    status=WriteFileSec(SecBuff,cluster,directory);
    if(!status && SectorSize==512) {
        memset(SecBuff,0,512);
        if(!Scope144_Encode(bank,__GetDev_SN(),SecBuff))status=VER_ERR;
        else status=WriteFileSec(SecBuff,cluster,directory);
    }
    if(__CloseFile(SecBuff,status?0:1024,cluster,directory)!=OK && !status)status=WR_ERR;
    return status;
}
/* native_updated uses the current native parameters; otherwise preserve the
 * exact saved prefix, including calibration A/B, battery and clock values. */
int File144_WptSave(Scope144Bank *bank,int native_updated)
{
    Scope144Bank previous,check;u8 native[512];
    uint32_t original_generation=bank->generation,generation=bank->generation;
    uint32_t expected_native_crc,read_native_crc;
    char name[12];int prior,status,from_backup=0;
    if(InitFileSystem()!=OK)return DISK_ERR;
    if(!Scope144_Encode(bank,__GetDev_SN(),SecBuff))return VER_ERR;
    WptName(name,0);prior=ReadWpt(name,native,&previous,&expected_native_crc);
    if(prior<0)return RD_ERR;
    if(!prior){WptName(name,1);prior=ReadWpt(name,native,&previous,&expected_native_crc);from_backup=1;}
    if(prior<0)return RD_ERR;
    if(prior && File144_CalLoad(&check) && Scope144_Newer(check.generation,previous.generation))previous=check;
    if(prior && !from_backup) {
        /* Do not touch primary until the complete previous image is verified. */
        WptName(name,1);status=WriteWpt(name,native,&previous);if(status)return status;
        if(ReadWpt(name,0,&check,&read_native_crc)!=2 || expected_native_crc!=read_native_crc || memcmp(&previous,&check,sizeof(check)))return RD_ERR;
        if(Scope144_Newer(previous.generation,generation))generation=previous.generation;
    }
    if(native_updated || !prior) {
        unsigned i,sum=0;
        memset(SecBuff,0,SectorSize);storeParameter();
        /* Keep the legacy prefix usable by older Wildcat: interlace lives in
         * the extension, and the saved native A coupling remains DC or AC. */
        if(SecBuff[19]==0 && (SecBuff[18]==SCOPE144_DC || SecBuff[18]==SCOPE144_AC)) {
            SecBuff[18]=(u8)(SecBuff[18]==SCOPE144_AC?1:0);
            for(i=0;i<511;++i)sum+=SecBuff[i];
            SecBuff[511]=(u8)(0-sum);
        }
        memcpy(native,SecBuff,512);
    }
    if(prior && Scope144_Newer(previous.generation,generation))generation=previous.generation;
    /* Change only the scalar generation while encoding; restore it on every
     * failure. Avoid a third full bank on the small MCU stack. */
    if(!NativeValid(native))return VER_ERR;
    bank->generation=generation+1;
    if(!Scope144_Encode(bank,__GetDev_SN(),SecBuff)) {
        bank->generation=original_generation;return VER_ERR;
    }
    expected_native_crc=Scope144_Crc(native,512);
    WptName(name,0);status=WriteWpt(name,native,bank);
    if(!status && (ReadWpt(name,0,&check,&read_native_crc)!=2 || expected_native_crc!=read_native_crc || memcmp(bank,&check,sizeof(check))))status=RD_ERR;
    if(status)bank->generation=original_generation;
    return status;
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
int File144_Csv(const Scope144Cal *cal,const int32_t *q10,const uint16_t *pairs,
    unsigned range,unsigned base,unsigned number,int factor)
{
    char name[12]="I144000 CSV",line[80]; u16 cluster[3]; u32 directory[1];
    unsigned i,used=0,total=0; u8 error;
    const char *header="# Wildcat V6.6; FPGA W1.2; nominal 144000000 Sa/s; B->A; microvolts at probe tip\r\n"
        "# t=0 is first valid sample; calibration is serial/range/FPGA bound\r\n";
    if(!Scope144_Valid(cal) || !q10 || !pairs || range>=8 || base>=3 || number>999 || (factor!=1 && factor!=-1 && factor!=10 && factor!=-10)) return VER_ERR;
    if(InitFileSystem()!=OK) return DISK_ERR;
    name[4]=(char)('0'+number/100); name[5]=(char)('0'+number/10%10); name[6]=(char)('0'+number%10);
    error=__OpenFileRd(SecBuff,name,cluster,directory);
    if(error==OK) { __CloseFile(SecBuff,0,cluster,directory); return 200; } /* Never overwrite a capture. */
    if(error!=NEW) return RD_ERR;
    if(__OpenFileWr(SecBuff,name,cluster,directory)!=OK) return DISK_ERR;
    error=File144_Text(header,strlen(header),cluster,directory,&used); total=strlen(header);
    if(!error) {
        const char *probe=(factor==10 || factor==-10)?"# probe_x=10\r\n":"# probe_x=1\r\n";
        const char *inversion=factor<0?"# inverted=1\r\n":"# inverted=0\r\n";
        error=File144_Text(probe,strlen(probe),cluster,directory,&used);total+=strlen(probe);
        if(!error) {error=File144_Text(inversion,strlen(inversion),cluster,directory,&used);total+=strlen(inversion);}
    }
    if(!error) {
        unsigned length=Scope144_ClockLine(line);
        error=File144_Text(line,length,cluster,directory,&used);total+=length;
    }
    if(!error) {
        const char *columns="sample,time_ps,A_uV,adc,raw_clip\r\n";
        error=File144_Text(columns,strlen(columns),cluster,directory,&used);total+=strlen(columns);
    }
    for(i=0;i<1024 && !error;++i) {
        unsigned pair=pairs[152+i/2],raw=(i&1)?(pair&255):(pair>>8);
        unsigned length=Scope144_CsvScaled(line,cal,i,q10[i],raw==0 || raw==255,factor);
        if(!length) { error=VER_ERR; break; }
        error=File144_Text(line,length,cluster,directory,&used); total+=length;
    }
    if(!error && used) {
        /* AlterBIOS sector API cannot truncate: newline padding remains valid CSV. */
        memset(SecBuff+used,'\n',SectorSize-used);
        error=WriteFileSec(SecBuff,cluster,directory);
    }
    if(__CloseFile(SecBuff,error?0:total,cluster,directory)!=OK && !error) error=WR_ERR;
    return error;
}
