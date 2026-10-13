#pragma once
#include <stdint.h>
typedef unsigned UINT;
typedef uint32_t DWORD;
typedef int FRESULT;
typedef struct { const uint8_t *data; DWORD fsize, pos; } FIL;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FA_READ 1
#define f_size(fp) ((fp)->fsize)
FRESULT f_open(FIL *, const char *, int);
FRESULT f_close(FIL *);
FRESULT f_read(FIL *, void *, UINT, UINT *);
FRESULT f_lseek(FIL *, DWORD);
