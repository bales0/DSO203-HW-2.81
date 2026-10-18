#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ds203_io.h"
#include "BIOS.h"
#include <stdio.h>
#include "mathutils.h"
#include "drawing.h"
#include "ff.h"

/* ---- LCD drawing ---- */

static char buffer[55];

// Format the message to statically allocated buffer and then call BIOS to
// print it on screen.
int lcd_printf(u16 x0, u16 y0, u16 color, u8 mode, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int rv = vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);
    
    __Display_Str(x0, y0, color, mode, (u8*)buffer);
    return rv;
}

bool f_exists(const char *name)
{
    FIL file;
    if (f_open(&file, name, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return false;
    
    f_close(&file);
    return true;
}

// Find a filename that is not in use. Format is a printf format string.
char *select_filename(const char *format)
{
    static char filename[13];
    for (int i = 0; i <= 999; i++)
    {
        int length = snprintf(filename, sizeof(filename), format, i);
        if (length <= 0 || (unsigned)length >= sizeof(filename)) return NULL;
        FILINFO info;
        FRESULT status = f_stat(filename, &info);
        if (status == FR_NO_FILE) return filename;
        if (status != FR_OK) return NULL;
    }
    return NULL;
}


/* ----------------- Bitmap writing ------------ */

// 16-color palette for bitmaps. Could be adjusted per-application to
// get better looking images.
const uint32_t bmp_default_palette[16] = {
    RGB565RGB(0,0,0), RGB565RGB(255,255,255), RGB565RGB(128,128,128),
    RGB565RGB(255,0,0), RGB565RGB(0,255,0), RGB565RGB(0,0,255),
    RGB565RGB(255,255,0), RGB565RGB(0,255,255), RGB565RGB(255,0,255),
    RGB565RGB(128,0,0), RGB565RGB(0,128,0), RGB565RGB(0,0,128),
    
    // Following 4 places are used for application-specific colors
    RGB565RGB(63, 63, 63), RGB565RGB(0, 0, 63)
};

// Find the closest color in palette, using Manhattan distance.
// Comparing in HSV space would be more accurate but slower.
static int quantize(uint16_t color, const uint32_t *palette)
{
    int min_delta = 999999;
    int closest = 0;
    for (int i = 0; i < 16; i++)
    {
        int delta = (
            abs(RGB565_R(color) - RGB565_R(palette[i])) +
            abs(RGB565_G(color) - RGB565_G(palette[i])) +
            abs(RGB565_B(color) - RGB565_B(palette[i]))
        );
        if (delta < min_delta)
        {
            min_delta = delta;
            closest = i;
        }
    }
    
    return closest;
}

// We know the LCD size in advance, so the file header can be written out
// by hand.
// The main header is 14 bytes, bitmap info header is 40 bytes and the palette
// is 64 bytes. Therefore bitmap data starts at offset 118.
const uint8_t BMP_HEADER[54] = {
    0x42, 0x4D, // BMP magic
    0xF6, 0xBB, 0x00, 0x00, // File size, 400 * 240 / 2 + 118 = 48118 bytes
    0x00, 0x00, 0x00, 0x00, // Creator
    0x76, 0x00, 0x00, 0x00, // Offset to bitmap data = 118 bytes 
    0x28, 0x00, 0x00, 0x00, // Header size = 40 bytes
    0x90, 0x01, 0x00, 0x00, // Bitmap width = 400 pixels
    0xF0, 0x00, 0x00, 0x00, // Bitmap height = 240 pixels
    0x01, 0x00,             // 1 color plane
    0x04, 0x00,             // 4 bits per pixel
    0x00, 0x00, 0x00, 0x00, // Uncompressed
    0x80, 0xBB, 0x00, 0x00, // Bitmap data size, 400 * 240 / 2 = 48000 bytes
    0x70, 0x17, 0x00, 0x00, // Horizontal pixel per meter = 6000
    0x70, 0x17, 0x00, 0x00, // Vertical pixel per meter
    0x10, 0x00, 0x00, 0x00, // Palette colors = 16
    0x00, 0x00, 0x00, 0x00  // Important colors
};

bool write_bitmap(const char *filename, const uint32_t *palette)
{
    if (!filename || !filename[0]) return false;
    FIL file;
    uint8_t row[200];
    unsigned bytes;
    bool success = false;
    
    if (f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;
    
    // Write header
    if (f_write(&file, BMP_HEADER, sizeof(BMP_HEADER), &bytes) != FR_OK ||
        bytes != sizeof(BMP_HEADER)) goto close;
    
    // Write palette
    for (int i = 0; i < 16; i++)
    {
        row[i * 4] = RGB565_B(palette[i]);
        row[i * 4 + 1] = RGB565_G(palette[i]);
        row[i * 4 + 2] = RGB565_R(palette[i]);
        row[i * 4 + 3] = 0;
    }
    if (f_write(&file, row, 64, &bytes) != FR_OK || bytes != 64) goto close;
    
    // Write bitmap data
    for (int y = 0; y < 240; y++)
    {
        for (int x = 0; x < 400; x += 2)
        {
            __Point_SCR(x, y);
            int colorH = quantize(__LCD_GetPixl(), palette);
            __Point_SCR(x + 1, y);
            int colorL = quantize(__LCD_GetPixl(), palette);
            
            row[x / 2] = (colorH << 4) | colorL;
        }
        if (f_write(&file, row, sizeof(row), &bytes) != FR_OK ||
            bytes != sizeof(row)) goto close;
    }
    success = true;
close:
    if (f_close(&file) != FR_OK) success = false;
    return success;
}



