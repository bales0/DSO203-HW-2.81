/* A dialog for selecting a script from the internal filesystem */

#include <stdbool.h>
#include "ff.h"
#include "menubar.h"
#include "amx.h"
#include "drawing.h"
#include "BIOS.h"
#include "buttons.h"
#include "ds203_io.h"
#include <string.h>
#include <stdlib.h>
#include "msgbox.h"
#include "metadata.h"

static const uint32_t default_icon[] = {
0b000000000000001000000000000000,
0b000000000000011110000000000000,
0b000000000001110011100000000000,
0b000000000111000000111000000000,
0b000000011100000000001100000000,
0b000000110000000000000111000000,
0b000011100000000000000001110000,
0b001110000000000000000000011000,
0b011000000000000000000000000110,
0b110000000000000000000000000011,
0b101000000000000000000000000111,
0b110110000000000000000000001111,
0b101010100000000000000000111111,
0b110101011000000000000011111111,
0b101010101110000000001111111111,
0b110101010101100000111111111111,
0b101010101010101011111111111111,
0b110101010101010111111111111111,
0b101010101010101111111111111111,
0b110101010101010111111111111111,
0b001110101010101111111111111100,
0b000011010101010111111111110000,
0b000001101010101111111111100000,
0b000000011101010111111110000000,
0b000000000110101111111000000000,
0b000000000001010111110000000000,
0b000000000000111111000000000000,
0b000000000000001100000000000000
};

#define DEFAULT_ICON_HEIGHT (sizeof(default_icon)/4)

#define SLOT_W 100
#define SLOT_H 70
#define ICONS_ON_SCREEN 12

static void draw_item(int index, const uint32_t *icon, int icon_height,
                      const char *line1, const char *line2)
{
    int x = (index % 4) * SLOT_W;
    int y = 220 - (index / 4 + 1) * SLOT_H;
    
    draw_bitmap(icon, x + 34, y + 30, RGB(255,255,255), icon_height, true);
    draw_flowtext(line1, x, y + 2, SLOT_W, 28, RGB(255,255,255), 0, true);
}

/* Names belong to the rendered page. A directory rescan must never silently
 * turn Run into execution of a different icon after USB directory changes. */
typedef struct {
    int page, index, visible_count;
    bool has_next_page, io_error;
    char names[ICONS_ON_SCREEN][13];
} ScriptPage;

static bool is_a_script(const FILINFO *file)
{
    unsigned n;
    if (file->fattrib & (AM_DIR | AM_SYS | AM_HID | AM_VOL)) return false;
    for (n = 0; n < sizeof(file->fname) && file->fname[n]; ++n) {}
    if (n < 5 || n == sizeof(file->fname)) return false;
    return file->fname[n-4] == '.' && (file->fname[n-3] | 0x20) == 'a' &&
           (file->fname[n-2] | 0x20) == 'm' && (file->fname[n-1] | 0x20) == 'x';
}

static void clamp_index(ScriptPage *s)
{
    if (!s->visible_count) s->index = -1;
    else if (s->index < 0) s->index = 0;
    else if (s->index >= s->visible_count) s->index = s->visible_count - 1;
}

/* One scan retains the last existing page if the requested page disappeared.
 * Lookahead uses the same bounded filter as rendering and selection.
 * This FatFs revision has no f_closedir and DIR owns no allocated resource. */
static void load_page(ScriptPage *s)
{
    DIR dir;
    FILINFO file;
    int target = s->page < 0 ? 0 : s->page;
    unsigned count = 0;
    s->page = 0; s->visible_count = 0; s->has_next_page = false;
    s->io_error = f_opendir(&dir, "/") != FR_OK;
    while (!s->io_error) {
        if (f_readdir(&dir, &file) != FR_OK) { s->io_error = true; break; }
        if (!file.fname[0]) break;
        if (!is_a_script(&file)) continue;
        if ((int)(count / ICONS_ON_SCREEN) > target) {
            s->has_next_page = true; break;
        }
        if (!(count % ICONS_ON_SCREEN)) {
            s->visible_count = 0; s->page = count / ICONS_ON_SCREEN;
        }
        memcpy(s->names[s->visible_count++], file.fname, sizeof(file.fname));
        ++count;
    }
    if (s->io_error) {
        s->page = 0; s->visible_count = 0; s->has_next_page = false;
    }
    clamp_index(s);
}

static void show_cursor(int index, int color)
{
    int x, y;
    if (index < 0 || index >= ICONS_ON_SCREEN) return;
    x = (index % 4) * SLOT_W;
    y = 219 - (index / 4 + 1) * SLOT_H;
    /* draw_rectangle includes x+w and y+h. Last column must end at 399. */
    draw_rectangle(x, y, SLOT_W - 1, SLOT_H - 2, color, 0);
}

static void render_screen(ScriptPage *s)
{
    int i;
    clamp_index(s);
    __Clear_Screen(0);
    draw_menubar("Run", "Refresh", "", "About");
    for (i = 0; i < s->visible_count; ++i) {
        uint32_t icon_buf[32];
        const uint32_t *icon = default_icon;
        int icon_size = 0;
        char name[20] = {0};
        if (get_program_metadata(s->names[i], icon_buf, &icon_size, name, sizeof(name)) &&
            icon_size > 0 && icon_size <= 32) icon = icon_buf;
        if (icon == default_icon) icon_size = DEFAULT_ICON_HEIGHT;
        name[sizeof(name)-1] = 0;
        if (!name[0]) memcpy(name, s->names[i], 13);
        draw_item(i, icon, icon_size, name, "");
    }
    if (s->io_error)
        draw_text("Directory read error. Press Refresh.", 200, 100, 0xFFFF, 0, true);
    else if (!s->visible_count)
        draw_text("Please copy some .AMX files to the USB drive :)", 200, 100, 0xFFFF, 0, true);
    show_cursor(s->index, RGB(128,128,255));
}

static bool get_name(char dest[13], ScriptPage *s)
{
    char selected[13];
    int page = s->page, index = s->index;
    if (index < 0 || index >= s->visible_count || s->io_error) return false;
    memcpy(selected, s->names[index], 13);
    load_page(s);
    if (s->io_error || s->page != page || s->index != index ||
        strcmp(selected, s->names[index])) return false;
    memcpy(dest, selected, 13);
    return true;
}

static void move_selection(ScriptPage *s, int delta)
{
    int next;
    if (!s->visible_count || s->io_error) return;
    next = s->index + delta;
    if (next < 0 && s->page > 0) {
        --s->page; s->index = next + ICONS_ON_SCREEN; load_page(s);
    } else if (next >= s->visible_count && s->has_next_page) {
        ++s->page; s->index = next - ICONS_ON_SCREEN; load_page(s);
    } else {
        s->index = next; clamp_index(s);
    }
}

void select_file(char result[13])
{
    static int page = 0, index = 0;
    ScriptPage s = {0};
    bool rerender = true;
    s.page = page; s.index = index;
    load_page(&s);
    for (;;) {
        /* Always validate and draw before accepting Run, including app return. */
        if (rerender) { render_screen(&s); rerender = false; }
        if (get_keys(BUTTON1) && s.index >= 0) {
            if (get_name(result, &s)) {
                page = s.page; index = s.index; return;
            }
            rerender = true;
        }
        if (get_keys(BUTTON2)) { load_page(&s); rerender = true; }
        if (get_keys(BUTTON4)) {
            show_msgbox("QuadPawn " QUADPAWN_VERSION,
                        "Pawn scripting language for DSO Quad\n"
                        "(C) 2012 Petteri Aimonen <jpa@kapsi.fi>\n"
                        "\n"
                        "Built " __DATE__ " at " __TIME__ "\n"
                        "Base: Turro75 f702d57\n"
                        "SYS GCCA0 / AlterBIOS merged\n"
                        "Local: FPGA ROM W1.1/W1.2 loader\n"
                        "Project git " COMMITID "\n"
                        "GCC version " __VERSION__);
            load_page(&s); rerender = true;
        }
        if (peek_keys(SCROLL1_LEFT | SCROLL1_RIGHT | SCROLL2_LEFT | SCROLL2_RIGHT)) {
            int old_page = s.page;
            bool boundary = s.index < 4 || s.index + 4 >= s.visible_count;
            show_cursor(s.index, 0);
            if (get_keys(SCROLL1_LEFT)) move_selection(&s, -4);
            if (get_keys(SCROLL1_RIGHT)) move_selection(&s, 4);
            if (get_keys(SCROLL2_LEFT)) move_selection(&s, -1);
            if (get_keys(SCROLL2_RIGHT)) move_selection(&s, 1);
            /* Interior moves do not reread metadata or repaint all icons. */
            if (boundary || s.page != old_page) rerender = true;
            else show_cursor(s.index, RGB(128,128,255));
        }
    }
}
