/* Pawn interface to fatfs */

#include "ff.h"
#include "amx.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ds203_io.h"

#define FILE_COUNT 4
typedef struct {
    FIL f;
    bool valid;
    FRESULT error;
} file_t;

static file_t files[FILE_COUNT];

static cell AMX_NATIVE_CALL amx_f_open(AMX *amx, const cell *params)
{
    file_t *file = 0;
    for (int i = 0; i < FILE_COUNT; i++)
    {
        if (!files[i].valid)
        {
            file = &files[i];
            break;
        }
    }
    
    if (!file)
        return 0; // Out of file descriptors
    
    file->error = 0;
    
    char *fname;
    amx_StrParam(amx, params[1], fname);
    file->error = f_open(&file->f, fname, params[2]);
    file->valid = (file->error == FR_OK);
    
    return file->valid ? (cell)file : 0;
}

static file_t *get_file(cell handle)
{
    for (int i = 0; i < FILE_COUNT; i++)
        if (handle == (cell)&files[i] && files[i].valid)
            return &files[i];
    return NULL;
}

#define GETPARAM() \
    file_t *file = get_file(params[1]); \
    if (!file) return 0;

#define SETERROR(cmd) do {\
    FRESULT file_status = (cmd); \
    if (file_status != FR_OK && file->error == FR_OK) file->error = file_status; \
    } while(0)

static cell AMX_NATIVE_CALL amx_f_close(AMX *amx, const cell *params)
{
    GETPARAM();
    
    SETERROR(f_close(&file->f));
    file->valid = false;
    
    return (file->error == FR_OK);
}

static cell AMX_NATIVE_CALL amx_f_read(AMX *amx, const cell *params)
{
    GETPARAM();
    
    if (params[3] < 0)
    {
        if (file->error == FR_OK) file->error = FR_INVALID_PARAMETER;
        return 0;
    }

    // Read in blocks, then pack only received bytes. A partial cell must keep
    // its unread bytes, and EOF must not touch the following Pawn cells.
    uint8_t buffer[64];
    unsigned total = 0, remaining = params[3];
    cell *dest = (cell*)params[2];
    while (remaining)
    {
        unsigned size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        unsigned bytes = 0;
        FRESULT status = f_read(&file->f, buffer, size, &bytes);
        SETERROR(status);
        for (unsigned i = 0; i < bytes; i++)
        {
            unsigned shift = 24 - (i % 4) * 8;
            dest[i / 4] = ((uint32_t)dest[i / 4] & ~(0xffu << shift)) |
                          ((uint32_t)buffer[i] << shift);
        }
        total += bytes;
        if (status != FR_OK || bytes != size) break;
        remaining -= bytes;
        dest += bytes / 4;
    }
    return total;
}

static cell AMX_NATIVE_CALL amx_f_write(AMX *amx, const cell *params)
{
    GETPARAM();
    
    cell* src = (cell*)params[2];
    int count = params[3];
    
    if (count < 0)
    {
        char *text;
        amx_StrParam(amx, src, text);
        unsigned bytes = 0;
        unsigned size = strlen(text);
        if (size == 0) return true;
        
        FRESULT status = f_write(&file->f, text, size, &bytes);
        SETERROR(status);
        
        return (status == FR_OK && bytes == size);
    }
    else
    {
        while (count > 0)
        {
            uint32_t tmp = __builtin_bswap32(*src++);
            unsigned bytes = 0;
            unsigned size = 4;
            if ((unsigned)count < size) size = count;
            
            FRESULT status = f_write(&file->f, &tmp, size, &bytes);
            SETERROR(status);
            
            if (status != FR_OK || bytes != size)
                return false;
            
            count -= 4;
        }
        return true;
    }
}

static cell AMX_NATIVE_CALL amx_f_lseek(AMX *amx, const cell *params)
{
    GETPARAM();
    SETERROR(f_lseek(&file->f, params[2]));
    return 0;
}

static cell AMX_NATIVE_CALL amx_f_tell(AMX *amx, const cell *params)
{
    GETPARAM();
    return f_tell(&file->f);
}

static cell AMX_NATIVE_CALL amx_f_size(AMX *amx, const cell *params)
{
    GETPARAM();
    return f_size(&file->f);
}

static cell AMX_NATIVE_CALL amx_f_truncate(AMX *amx, const cell *params)
{
    GETPARAM();
    FRESULT status = f_truncate(&file->f);
    SETERROR(status);
    return status == FR_OK;
}

static cell AMX_NATIVE_CALL amx_f_unlink(AMX *amx, const cell *params)
{
    char *text;
    amx_StrParam(amx, params[1], text);
    return f_unlink(text) == FR_OK;
}

static cell AMX_NATIVE_CALL amx_f_getfree(AMX *amx, const cell *params)
{
    DWORD clusters;
    FATFS *fs;
    if (f_getfree("", &clusters, &fs) != FR_OK) return 0;
    
#if _MAX_SS == 512
    return clusters * fs->csize * 512;
#else
    return clusters * fs->csize * fs->ssize;
#endif
}

static DIR root_dir;
static cell AMX_NATIVE_CALL amx_f_opendir(AMX *amx, const cell *params)
{
    return f_opendir(&root_dir, "");
}

static cell AMX_NATIVE_CALL amx_f_readdir(AMX *amx, const cell *params)
{
    FILINFO info;
    if (f_readdir(&root_dir, &info) != FR_OK || info.fname[0] == 0)
        return false;
    
    amx_SetString((cell*)params[1], info.fname, true, false, 13);
    *(cell*)params[2] = info.fsize;
    
    return true;
}

static cell AMX_NATIVE_CALL amx_f_error(AMX *amx, const cell *params)
{
    file_t *file = get_file(params[1]);
    return file ? file->error : FR_INVALID_OBJECT;
}

static cell AMX_NATIVE_CALL amx_f_exists(AMX *amx, const cell *params)
{
    char *fname;
    amx_StrParam(amx, params[1], fname);
    return f_exists(fname);
}

static cell AMX_NATIVE_CALL amx_select_filename(AMX *amx, const cell *params)
{
    char *format;
    amx_StrParam(amx, params[1], format);
    const char *name = select_filename(format);
    amx_SetString((cell*)params[1], name ? name : "", true, false, params[2]);
    return name != NULL;
}

int amxinit_file(AMX *amx)
{
    static const AMX_NATIVE_INFO funcs[] = {
        {"f_open", amx_f_open},
        {"f_close", amx_f_close},
        {"f_read", amx_f_read},
        {"f_write", amx_f_write},
        {"f_lseek", amx_f_lseek},
        {"f_tell", amx_f_tell},
        {"f_size", amx_f_size},
        {"f_truncate", amx_f_truncate},
        {"f_unlink", amx_f_unlink},
        {"f_getfree", amx_f_getfree},
        {"f_opendir", amx_f_opendir},
        {"f_readdir", amx_f_readdir},
        {"f_error", amx_f_error},
        {"f_exists", amx_f_exists},
        {"select_filename", amx_select_filename},
        {0, 0}
    };
    
    // Clear the file table
    for (int i = 0; i < FILE_COUNT; i++)
    {
        files[i].valid = false;
    }
    
    return amx_Register(amx, funcs, -1);
}

int amxcleanup_file(AMX *amx)
{
    for (int i = 0; i < FILE_COUNT; i++)
    {
        if (files[i].valid)
        {
            f_close(&files[i].f);
            files[i].valid = false;
        }
    }
    return 0;
}
