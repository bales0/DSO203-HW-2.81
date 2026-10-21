_estack = ORIGIN(ram)+LENGTH(ram)-0x4000;
/*_estack = ORIGIN(ram)+LENGTH(ram)-0x2000;*/

/* Linker script for Olimex STM32-H103 eval board.
 *
 * Version: 0.1
 *
 *  Copyright 2007 CodeSourcery.
 *
 * The authors hereby grant permission to use, copy, modify, distribute,
 * and license this software and its documentation for any purpose, provided
 * that existing copyright notices are retained in all copies and that this
 * notice is included verbatim in any distributions. No written agreement,
 * license, or royalty fee is required for any of the authorized uses.
 * Modifications to this software may be copyrighted by their authors
 * and need not follow the licensing terms described here, provided that
 * the new terms are clearly indicated on the first page of each file where
 * they apply.
 *
 * Modified by Olimex
 * 2009-04-07: Modified by Piotr Esden-Tempski <piotr@esden.net>
 */

SECTIONS
  {
    .  = 0x0;          /* From 0x00000000 */

    .bios 0x08004200 (NOLOAD) : {
      *BIOS.o
    }

     .isr_vector : {
          _vectors = .;
          *(.isr_vectors)                  /* Vector table */
          . = ALIGN(4);
       /* startup.o(.text) */
          *(.text.startup)
          *stm32f10x_nvic.o(.text)
          *cortexm3_macro.o(.text)

        /*  Main.o(.text) */
        /*  Files.o(.text) */

          *Function.o(.text)
          *UI144.o(.text)
          *Calibrat.o(.text)
          *(.text.calibration)             /* Calibration helpers; same ROM limits */
          *Interrupt.o(.text)
        /*  Draw.o(.text) */
       /* Process.o(.text)*/
          *(.rodata*)                     /* Read only data */
    } >rom

    .spare : {                           /*Undocumented area*/
      . = ALIGN(4);
          *startup.o(.text)
          *Draw.o(.text)
        /*  Interrupt.o(.text) */
          *Main.o(.text)
          *Menu.o(.text)
          *UiCommon.o(.text)
          *Scope144.o(.text)
          *UnifiedCal.o(.text)
          *(.text.cal_probe)               /* Non-saving calibration error test */
          *Files144.o(.text)
          *NativeWindow.o(.text)
          *Digital144.o(.text)
          *Frame144.o(.text)
          *Fft144.o(.text)
    } >spare


    .extra : {                           /*Undocumented area*/
      . = ALIGN(4);
      *(.text*)                          /*Program code */
    } >extra


    .text : {
     . = ALIGN(4);
      _etext = .;
      _sidata = _etext;
    } >rom

    .data :  AT ( _sidata ) {
      _sdata = . ;
	*(.data*)                       /* Data memory */
      . = ALIGN(4);
      _edata = .;
	} >ram

    .bss (NOLOAD) : {
    _sbss = .;
    *(.bss*)        /* Zero-filled run time allocate data memory */
    . = ALIGN(4);
    _ebss = .;
    } >ram AT > rom
  }

ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(rom) + LENGTH(rom),
       "Initialized data exceeds the application ROM region")
ASSERT(_ebss <= _estack, "Static RAM data overlaps the initial stack pointer")
/*========== end of file ==========*/











