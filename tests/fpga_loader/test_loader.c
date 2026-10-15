#define FPGA_LOADER_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "mocks/stm32f10x.h"
#include "mocks/ff.h"

uint8_t fpga_test_rom[135096], reference_rom[135096];
uint8_t image[135100], installed_rom[135096];
GPIO_TypeDef mock_a, mock_b, mock_c;
DMAChannel mock_channel;
DMAController mock_dma;
static unsigned starts, ends, sent[8], done_fail_budget, closes;
static uint8_t transmitted[8][135600];
static bool reset_ok, fail_preflight_read, fail_transfer_read;
static int count;

FRESULT f_open(FIL *f, const char *name, int mode)
{
    (void)mode;
    if (strcmp(name, "image") != 0) return FR_DISK_ERR;
    *f = (FIL){ image, sizeof(image), 0 };
    return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; closes++; return FR_OK; }
FRESULT f_lseek(FIL *f, DWORD pos)
{ if (pos > f->fsize) return FR_DISK_ERR; f->pos=pos; return FR_OK; }
FRESULT f_read(FIL *f, void *out, UINT want, UINT *got)
{
    *got=0;
    if (fail_preflight_read || (fail_transfer_read && starts == 1))
        return FR_DISK_ERR;
    UINT available=f->fsize-f->pos;
    *got=want < available ? want : available;
    memcpy(out,f->data+f->pos,*got);f->pos+=*got;return FR_OK;
}
bool fpga_test_begin(uint32_t *old)
{ *old=mock_b.CRH; starts++; mock_b.CRH=0x83833300; return reset_ok; }
void fpga_test_transmit(uint8_t value) { assert(starts<8 && sent[starts]<135600); transmitted[starts][sent[starts]++]=value; }
bool fpga_test_done(void)
{ if(done_fail_budget){done_fail_budget--;return false;}return true; }
void fpga_test_end(uint32_t old) { ends++;mock_b.CRH=old; }

#include "../../QuadPawn/Runtime/fpga.c"
#include "../../QuadPawn/Runtime/amx_fpga.c"

static void reset_test(void)
{
    memcpy(fpga_test_rom,reference_rom,sizeof(fpga_test_rom));
    starts=ends=done_fail_budget=closes=0;memset(sent,0,sizeof(sent));
    reset_ok=true;fail_preflight_read=fail_transfer_read=false;have_custom_image=false;
    mock_a=(GPIO_TypeDef){0x33333333,0x33333333,0,0,0};
    mock_b=mock_a;mock_c=mock_a;
}
static void ok(const char *label) { count++;printf("PASS %s\n",label); }
static cell call_load(const char *name, uint32_t crc)
{
    cell params[]={2*sizeof(cell),(cell)(uintptr_t)name,(cell)crc};
    return amx_fpga_load_checked(NULL,params);
}
int main(int argc, char **argv)
{
    assert(argc==5);
    char *crc_end;
    uint32_t image_crc=(uint32_t)strtoul(argv[3],&crc_end,16);
    assert(*argv[3] && !*crc_end && image_crc);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(reference_rom,1,sizeof(reference_rom),f)==sizeof(reference_rom));fclose(f);
    f=fopen(argv[2],"rb");assert(f);assert(fread(image,1,sizeof(image),f)==sizeof(image));fclose(f);
    f=fopen(argv[4],"rb");assert(f);assert(fread(installed_rom,1,sizeof(installed_rom),f)==sizeof(installed_rom));fclose(f);
    reset_test();assert(fpga_restore_available());assert(fpga_rom_version()==0x0101 && fpga_rom_crc()==FPGA_ROM_CRC32);ok("W1.1 reference CRC/version");
    reset_test();fpga_test_rom[100]^=1;assert(!fpga_configure(NULL)&&starts==0);ok("bad ROM rejected before reset");
    reset_test();FIL input={image,sizeof(image),0};assert(!fpga_configure_checked(&input,0)&&starts==0);ok("bad file CRC rejected before reset");
    reset_test();input=(FIL){image,68088,0};assert(!fpga_configure(&input)&&starts==0);ok("old HW length rejected");
    reset_test();input=(FIL){image,sizeof(image),0};assert(fpga_configure_checked(&input,image_crc));assert(starts==1&&ends==1&&sent[1]==sizeof(image)+500);assert(!memcmp(transmitted[1],image,sizeof(image)));ok("verified HX4K file loaded completely");
    reset_test();assert(fpga_configure(NULL)&&sent[1]==sizeof(reference_rom)+500);assert(!memcmp(transmitted[1],reference_rom,sizeof(reference_rom)));ok("correct ROM address-length restoration");
    reset_test();fail_preflight_read=true;input=(FIL){image,sizeof(image),0};assert(!fpga_configure(&input)&&starts==0);ok("file IO preflight failure");
    reset_test();fail_transfer_read=true;input=(FIL){image,sizeof(image),0};assert(!fpga_configure(&input)&&starts==1&&ends==1);ok("short-transfer failure restores hardware pins");
    reset_test();reset_ok=false;input=(FIL){image,sizeof(image),0};assert(!fpga_configure(&input)&&sent[1]==0&&ends==1);ok("CDONE stuck high during reset rejected");
    reset_test();done_fail_budget=1;input=(FIL){image,sizeof(image),0};assert(!fpga_configure(&input)&&ends==1);ok("CDONE low after transfer rejected");
    reset_test();uint32_t before=mock_a.CRL;assert(!call_load("missing",image_crc));assert(mock_a.CRL==before&&!have_custom_image&&starts==0);ok("missing file leaves current image and pins untouched");
    reset_test();assert(call_load("image",image_crc)&&have_custom_image);assert(call_load("",FPGA_ROM_CRC32)&&!have_custom_image&&starts==2);ok("native checked load and explicit restore");
    reset_test();done_fail_budget=1;assert(!call_load("image",image_crc));assert(!have_custom_image&&starts==2&&sent[2]==sizeof(reference_rom)+500);ok("native failure rolls back to W1.1");
    reset_test();assert(call_load("image",image_crc));assert(amxcleanup_fpga()==AMX_ERR_NONE&&!have_custom_image&&starts==2);ok("normal exit restores W1.1");
    reset_test();assert(call_load("image",image_crc));done_fail_budget=1;assert(amxcleanup_fpga()==AMX_ERR_NATIVE&&have_custom_image);ok("failed restore retains pending state");
    reset_test();have_custom_image=true;fpga_test_rom[100]^=1;assert(amxinit_fpga(NULL)==AMX_ERR_NATIVE&&have_custom_image&&starts==0);ok("next application blocked when restore unavailable");
    reset_test();memcpy(fpga_test_rom,installed_rom,sizeof(installed_rom));
    assert(fpga_restore_available() && fpga_rom_version()==0x0102 && fpga_rom_crc()==FPGA_ROM_W12_CRC32);ok("installed W1.2 CRC/version accepted");
    assert(!call_load("",FPGA_ROM_CRC32) && starts==0);ok("wrong expected baseline CRC rejected before reset");
    assert(call_load("",FPGA_ROM_W12_CRC32));assert(sent[1]==sizeof(installed_rom)+500 && !memcmp(transmitted[1],installed_rom,sizeof(installed_rom)));ok("native restore loads complete installed W1.2");
    reset_test();memcpy(fpga_test_rom,installed_rom,sizeof(installed_rom));
    assert(call_load("image",image_crc));assert(amxcleanup_fpga()==AMX_ERR_NONE && !have_custom_image);
    assert(starts==2 && !memcmp(transmitted[2],installed_rom,sizeof(installed_rom)));ok("application exit restores installed W1.2");
    reset_test();memcpy(fpga_test_rom,installed_rom,sizeof(installed_rom));done_fail_budget=1;
    assert(!call_load("image",image_crc) && !have_custom_image && starts==2);ok("failed load rolls back to installed W1.2");
    reset_test();memcpy(fpga_test_rom,installed_rom,sizeof(installed_rom));fpga_test_rom[100]^=1;
    assert(!fpga_restore_available() && fpga_rom_crc()==0 && fpga_rom_version()==0 && !call_load("image",image_crc) && starts==0);ok("corrupt W1.2 baseline blocks loading");
    printf("%d host checks passed; SPI electrical behavior not tested.\n",count);
    return 0;
}
