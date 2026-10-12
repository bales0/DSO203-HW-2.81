
MEMORY
{
  rom (rx) : ORIGIN = 0x08004000, LENGTH = 32K
  ram (rwx) : ORIGIN = 0x20000000, LENGTH = 12K
}

_estack = 0x20000000 + 48K;





SECTIONS
{
  .  = 0;          /* From 0x00000000 */
 
  .isr_vectors :
    {
        . = ALIGN(4);
        _isr_vectors_offs = . ;
        KEEP(*(.isr_vectors))            /* Startup code */
        . = 0x200;
        build/ASM.o  
        . = 0x400;
        build/alterbios.o
    } >rom
 
 
  .text : {
      
    *(.text*)        /* Program code */
    *(.rodata*)      /* Read only data */
    _etext = .;
    _sidata = _etext;
  } >rom

  .  = 0x20000000;   /* From 0x20000000 */

  .data : {
    _sdata = . ;
    *(.data)         /* Data memory */
    _edata = .;
  } >ram AT > rom

.bss : {
  _sbss = .;
  *(.bss)            /* Zero-filled run time allocate data memory */
  _ebss = .;
  } >ram AT > rom
}

