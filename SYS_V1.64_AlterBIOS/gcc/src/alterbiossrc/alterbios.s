@ Generated code - see make_export.py
	@ This is the API symbols file for AlterBIOS.
	@ It is used from DSO Quad user applications to access AlterBIOS functions.

.syntax unified
.thumb
.thumb_func
.code 16


.globl alterbios_version_tag 
alterbios_version_tag:
	.word 0x17a0025

.globl alterbios_init
.thumb_func
alterbios_init:
    B.W  __alterbios_init

.globl patch_GetDev_SN
.thumb_func
patch_GetDev_SN:
    B.W  __patch_GetDev_SN

.globl patch_OpenFileWr
.thumb_func
patch_OpenFileWr:
    B.W  __patch_OpenFileWr

.globl patch_OpenFileRd
.thumb_func
patch_OpenFileRd:
    B.W  __patch_OpenFileRd

.globl patch_ReadFileSec
.thumb_func
patch_ReadFileSec:
    B.W  __patch_ReadFileSec

.globl patch_ProgFileSec
.thumb_func
patch_ProgFileSec:
    B.W  __patch_ProgFileSec	

.globl patch_CloseFile
.thumb_func
patch_CloseFile:
    B.W  __patch_CloseFile


.globl f_open
.thumb_func
f_open:
	B.W	__f_open

.globl f_read
.thumb_func
f_read:
	B.W __f_read

.globl f_lseek
.thumb_func
f_lseek:
	B.W __f_lseek

.globl f_close
.thumb_func
f_close:
	B.W __f_close

.globl f_opendir
.thumb_func
f_opendir:
	B.W __f_opendir

.globl f_readdir
.thumb_func
f_readdir:
	B.W __f_readdir

.globl f_stat
.thumb_func
f_stat:
	B.W __f_stat

.globl f_write
.thumb_func
f_write:
	B.W __f_write

.globl f_getfree
.thumb_func
f_getfree:
	B.W __f_getfree

.globl f_truncate
.thumb_func
f_truncate:
	B.W __f_truncate

.globl f_sync
.thumb_func
f_sync:
	B.W __f_sync

.globl f_unlink
.thumb_func
f_unlink:
	B.W __f_unlink

.globl f_mkdir
.thumb_func
f_mkdir:
	B.W __f_mkdir

.globl f_chmod
.thumb_func
f_chmod:
	B.W __f_chmod

.globl f_utime
.thumb_func
f_utime:
	B.W __f_utime

.globl f_rename
.thumb_func
f_rename:
	B.W __f_rename

.globl f_flush
.thumb_func
f_flush:
	B.W __f_flush
