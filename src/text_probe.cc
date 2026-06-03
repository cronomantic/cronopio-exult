/* Slice (text/fonts) probe: paint REAL Ultima 7 text through Exult's Font class.
 *
 * The last static-render building block (after shapes / terrain / objects). It
 * loads FONTS.VGA with Exult's own self-contained Font class (shapes/font.cc,
 * which depends only on Vga_file / Image_buffer8 — no gamewin/gumps/game.h) and
 * paints several lines of text into the engine's Image_buffer8, then blits to
 * CRON_FB. fonts.vga is read through Exult's U7open_in -> the baked ROM.
 *
 *   Font f(File_spec("<STATIC>/fonts.vga"), index);   // index selects a font
 *   f.paint_text(ib, "text", xoff, yoff);             // glyphs from the font's
 *                                                     // own palette indices
 *
 * This is exactly what the menus, conversations and the in-game UI need later.
 */
#include <cronopio.h>
#include "iwin8.h"
#include "vid_cron.h"
#include "font.h"
#include "vgafile.h"        /* Shape_file (Font's unique_ptr member needs the complete type) */
#include "U7obj.h"          /* U7multiobject, File_spec */
#include "utils.h"          /* U7open_in */
#include "files_cron.h"
#include <cstdint>
#include <cstdio>
#include <memory>

static char lbuf[256];
#define LOGF(...) do { int n = snprintf(lbuf, sizeof lbuf, __VA_ARGS__); if (n>0) cron_log(lbuf, n); } while (0)

void setup(void) {
    exult_files_init();

    /* game palette (6-bit -> 8-bit) */
    unsigned char pal768[768] = {0};
    {
        U7multiobject palobj(File_spec("<STATIC>/palettes.flx"), 0);
        size_t len = 0; auto p = palobj.retrieve(len);
        if (p && len >= 768) for (int i = 0; i < 768; ++i) { unsigned v = p[i] & 0x3f; pal768[i] = (unsigned char)((v<<2)|(v>>4)); }
    }

    Image_window8 win(320, 200, 320, 200);
    win.set_palette(pal768, 255, 100);
    Image_buffer8* ib = win.get_ib8();
    ib->clear_clip();
    int W = (int)ib->get_width(), H = (int)ib->get_height();

    /* Font::paint_text ignores its `win` arg and renders to Shape_frame's
     * static default window (what Game_window sets at startup) — point it at
     * our buffer so the glyph paint_rle calls land here. */
    Shape_frame::set_to_render(ib);

    /* A two-tone backdrop so glyphs of either polarity stay legible:
     * dark top band, light bottom band (U7 menu text is light, dialog dark). */
    ib->fill8(0);                                  /* black overall */
    ib->fill8(8, W, H/2, 0, H/2);                  /* lighter lower half */

    static const char* lines[] = {
        "Ultima VII: The Black Gate",
        "Exult engine on Cronopio",
        "The quick brown fox jumps",
        "over the lazy dog. 0123456789",
    };

    /* Paint each sample line in a different font index from FONTS.VGA. */
    int y = 6;
    int painted_lines = 0;
    for (int idx = 0; idx < (int)(sizeof lines / sizeof lines[0]); ++idx) {
        Font f(File_spec("<STATIC>/fonts.vga"), idx);
        int th = f.get_text_height();
        if (th <= 0) { LOGF("font %d: empty\n", idx); continue; }
        const char* s = lines[idx];
        int tw = f.get_text_width(s);
        f.paint_text(ib, s, 6, y);
        LOGF("font %d: '%s' w=%d h=%d at y=%d\n", idx, s, tw, th, y);
        y += th + 4;
        ++painted_lines;
    }

    vid_present(ib->get_bits(), W, H, (int)ib->get_line_width(), pal768);
    LOGF("text_probe: presented %d real U7 font lines to CRON_FB\n", painted_lines);
    cron_exit(0);
}
void frame(void) {}
CRONOPIO_CART_INIT(setup, frame)
