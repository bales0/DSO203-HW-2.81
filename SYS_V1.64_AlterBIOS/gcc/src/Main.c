/********************* (C) COPYRIGHT 2010 e-Design Co.,Ltd. ********************
 File Name : main.c  
 Version   : DS203_SYS Ver 1.5x                                  Author : bure
*******************************************************************************/
#include <string.h>
#include "Function.h"
#include "USB_lib.h"
#include "Config.h"
#include "Ident.h"
#include "BIOS.h"
#include "LCD.h"
#include "ASM.h"
#include "USB_pwr.h"
#include "Ext_Flash.h"
#include "FAT12.h"
#include "alterbios.h"

typedef  void (*pFunc)(void);

pFunc Jump_To_App;
u32 JumpAddr;

/*******************************************************************************
  版本修改说明
SYS V1.24: 修改了144MHz交替采样模式下的插值系数(BIOS.c)
           修改了V2.6硬件电路下模拟输入量程档位(BIOS.c)
SYS V1.30: 将Licence识别函数转移到了DFU模块中(ASM.s,Ident.c)
           修改了Licence权限控制力度(Main.c, Ident.c)
           修改了DFU,SYS库函数向量及其调用方式(ASM.s, VECTOR.c)
           添加了新的SYS库函数(BIOS.c)
SYS V1.31: 将内部U盘的初始化推迟到Licence识别后进行(Main.c,Config.c)
           修改了U盘扇区读写程序(SPI_flash.c)
           修改了SYS库函数向量表存放起始地址
SYS V1.32: 修改了Set(T_BASE_ARR, Value)函数中的BUG(BIOS.c)
SYS V1.33: 增加了对LCD控制芯片ILI9327与R61509V的兼容和自动识别(LCD.c, ASM.s)
SYS V1.34: 从该版本起 可并行使用 IAR 4.42与５.０版本编译环境(ASM.s)
SYS V1.35: 修改了LCD控制芯片兼容性相关的BUG(ASM.s)         
           增加了设备硬件、SYS固件及DFU版本信息导出(ASM.s, BIOS.c, Main.c)
           增加了高精度的A,B通道垂直偏移设置功能函数(BIOS.c)
SYS V1.37: 修改了高精度的A,B通道垂直偏移设置功能函数的BUG(BIOS.c)        
           增加了FPGA2.60以上版本的版本信息导出(BIOS.c)
SYS V1.40: 增加了U盘文件系统读写相关函数(DOSFS.c)        
SYS V1.41: 修改了读BUF文件时的BUG(DOSFS.c)
SYS V1.42: 增加了Row_Fill函数(ASM.s)
SYS V1.50: 重写了新的FAT12文件系统(FAT12.c, ASM.s)
           修改了Flash读写函数避开与PC机的读写冲突(Memory.c, EXT_Flash.c)
SYS V1.51: 修改了设置 FPGA 参数的函数 (BIOS.C中的函数 Set())
SYS V1.52: 修改了Get函数中读取 SCH 及 DFU 版本号的 BUG (BIOS.c, ASM.s)
SYS V1.60  增加了8M Flash 的处理，与原 2M Flash兼容，自动识别 2M和8M Flash，
           但必须使用 版本 DFU_V312 以上版本。
SYS V1.61  修改ILI9327屏幕初始化参数，优化显示效果。
           初始化程序自适应DFU版本。（LCD.c BIOS.h）
           修改了USB Disk 的一些小BUG(USB_scsi.c, USB_scsi.h)
SYS V1.62  必须搭配APP1.62（否则开机响声不正常）
           适用DFU3.13以上的LCD初始化(LCD.c)
           DFU3.14，LCD已初始化，SYS取消初始化
           开机Beep转到App响声
SYS V1.63  修复flash存文件时，FAT表超256问题 
           存储flash时，找FAT时，要连续找两个空的簇号
SYS V1.64  SYS解锁程序                                                              
           兼容MAC OS X 系统，Disk_Inquiry_Str[]长度加到0x24，
           即是36长度,STANDARD_INQUIRY_DATA_LEN = 0x24 USB_scis.c     
*******************************************************************************/

uc8  SYS_Ver[5]  = "GCCA0"; 
FATFS fatfs;
/*******************************************************************************
  main : Main routine.
*******************************************************************************/
int main(void)
{ 
  u8 *Prt;
  u32 Licence;
  u8  NumStr[9];
  
  Set_System();
  NVIC_Config();
  GPIO_Config();
  Timer_Config();
  FSMC_Config();
  SPI_Config();

  DMA_Config();
  ADC_Config();
  DAC_Config();
  
  LCD_RD_Type();
  //Beep(20, 500);
//============1.62_DFU_314以上版本LCD初始化在DFU中进行=============
  Prt = __DeviceVersion(DFU);
  if(memcmp(Prt,"V3.14",5) < 0)
  {
    LCD_Initial();
    Clear_Screen(BLACK);
    Display_Logo(70,150);
  }
//=================================================================  
  Display_Str( 5*8, 90, GRN, PRN,  (u8*)("Hardware Ver ") );
  Display_Str(18*8, 90, GRN, PRN,  (u8*)Get(HDWVER, 0));
  Display_Str(25*8, 90, GRN, PRN, (u8*)("Serial No: "));
  Word2Hex(NumStr, __Dev_SN());            
  Display_Str(36*8, 90, YEL, PRN, NumStr);            // 显示读取到的设备序列号

  Display_Str( 6*8, 70, GRN, PRN, (u8*)("     DS203 Mini DSO SYS Ver            "));
  Display_Str(34*8, 70, GRN, PRN, (u8*)Get(SYSVER, 0)); // 显示SYS固件版本号
//---add----------------------------------------------------------------------//
  Init_Fat_Value();
  alterbios_init();
//----------------------------------------------------------------------------//  
  if((KEY2_OFF)&&(KEY3_OFF)&&(KEY4_OFF)){       // 开机无按键按下
    Key_Buffer = 0;                             // 清除按键状态，开始等待新的按键
    Licence = Get_Lic(SYS_LIC_OFFSET);          // Get master licence
    if((__Chk_SYS(Licence)!= 1) && (memcmp(Prt,"V3.15",5) < 0)){  // No master licence 
      Display_Str(6*8,50,GRN,PRN,     (u8*)"Please input master licence: 00000000");
      Licence = Input_Lic(48+29*8, 50);         // Input Licence
      if(__Chk_SYS(Licence)!= 1){               // Licence error
        Display_Str(6*8,50,YEL,PRN,   (u8*)"        Licence input error          ");
      } else {                                  // Licence ok
        if(Add_Lic(Licence, SYS_LIC_OFFSET)== 1)// Licence writen ok
          Display_Str(6*8,50,GRN,PRN, (u8*)"         Licence writen ok           ");
        else  
          Display_Str(6*8,50,GRN,PRN, (u8*)"        Licence writen error         ");
      }
      Display_Str(6*8,30,GRN,PRN,     (u8*)"      Push any key to next step      ");
      while (Key_Buffer == 0){};
    }
    JumpAddr = *(vu32*) (APP1_BASE + 4);
    Jump_To_App = (pFunc) JumpAddr;
    __MSR_MSP(*(vu32*) APP1_BASE);
  } else if(KEY2_ON){                           // 开机按下K2键  
    JumpAddr = *(vu32*) (APP2_BASE + 4);
    Jump_To_App = (pFunc) JumpAddr;
    __MSR_MSP(*(vu32*) APP2_BASE);
  } else if(KEY3_ON){                           // 开机按下K3键 
    JumpAddr = *(vu32*) (APP3_BASE + 4);
    Jump_To_App = (pFunc) JumpAddr;
    __MSR_MSP(*(vu32*) APP3_BASE);
  } else if(KEY4_ON){                           // 开机按下K4键 
    JumpAddr = *(vu32*) (APP4_BASE + 4);
    Jump_To_App = (pFunc) JumpAddr;
    __MSR_MSP(*(vu32*) APP4_BASE);
  }
  USB_EN();
  USB_Init(); 
  Jump_To_App();
}
/*********************************  END OF FILE  ******************************/
