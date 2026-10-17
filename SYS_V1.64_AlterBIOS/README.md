# DSO203_SYS164_GCC

Port to GCC of SYS ver 1.64 of DSO203.
tested only on DSO203 v2.72 8Mb.

This version includes AlterBIOS 0.7 as part of SYS 1.64, with FAT save/close fixes.
To use quadpawn with this release You need the latest quadpawn built for this new memory mapping.

Todo:

- check and assign a license.
- write credits at the end of the readme.

Compiled by arm-none-eabi-gcc 9.2.1 system arm compiler available in ubuntu 21.04

to compile do a
make clean all

This produces `../bin/SYS164AB/SYS164AB.HEX`, `.BIN`, and `.ADR`;
the ELF and map remain in `build/`. `SYS164AB` means SYS 1.64
with integrated AlterBIOS; the ADR/BIN pair has a common 8-character stem.
The BIN starts at `0x08004000` and can be uploaded through DFU.
