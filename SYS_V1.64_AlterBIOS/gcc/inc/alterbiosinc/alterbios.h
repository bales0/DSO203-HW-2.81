#ifndef __ALTERBIOS_H
#define __ALTERBIOS_H

#include "ff.h"
#include "stm32f10x_type.h"


typedef int (*func_t)();

void __alterbios_init(void);
void Alt_Init_Data(void);

extern void alterbios_init(void);

u32 __patch_GetDev_SN(void);
u8 __patch_OpenFileWr(u8 *buffer, u8 *filename, u16 *cluster, u32 *diraddr);
u8 __patch_OpenFileRd(u8 *buffer, u8 *filename, u16 *cluster, u32 *diraddr);
u8 __patch_ReadFileSec(u8* buffer, u16* cluster);
u8 __patch_ProgFileSec(u8* buffer, u16* cluster);
u8 __patch_CloseFile(u8* buffer, u32 length, u16* cluster, u32* diraddr);

FRESULT __f_open (FIL*, const TCHAR*, BYTE);			/* Open or create a file */
FRESULT __f_read (FIL*, void*, UINT, UINT*);			/* Read data from a file */
FRESULT __f_lseek (FIL*, DWORD);						/* Move file pointer of a file object */
FRESULT __f_close (FIL*);								/* Close an open file object */
FRESULT __f_opendir (DIR*, const TCHAR*);				/* Open an existing directory */
FRESULT __f_readdir (DIR*, FILINFO*);					/* Read a directory item */
FRESULT __f_stat (const TCHAR*, FILINFO*);			/* Get file status */
FRESULT __f_write (FIL*, const void*, UINT, UINT*);	/* Write data to a file */
FRESULT __f_getfree (const TCHAR*, DWORD*, FATFS**);	/* Get number of free clusters on the drive */
FRESULT __f_truncate (FIL*);							/* Truncate file */
FRESULT __f_sync (FIL*);								/* Flush cached data of a writing file */
FRESULT __f_unlink (const TCHAR*);					/* Delete an existing file or directory */
FRESULT	__f_mkdir (const TCHAR*);						/* Create a new directory */
FRESULT __f_chmod (const TCHAR*, BYTE, BYTE);			/* Change attribute of the file/dir */
FRESULT __f_utime (const TCHAR*, const FILINFO*);		/* Change times-tamp of the file/dir */
FRESULT __f_rename (const TCHAR*, const TCHAR*);		/* Rename/Move a file or directory */

/* Hackish function to deal with DSO Quad's simultaneous filesystem access. */
void __f_flush(FATFS *fs);

#endif