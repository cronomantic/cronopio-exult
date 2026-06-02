/* Slice 3 probe: render REAL Ultima 7 shapes through Exult's own pipeline.
 *
 * Proves the content -> shape -> render chain end to end on the VM:
 *   - exult_files_init() registers the <STATIC>/<GAMEDAT>/... path tokens and the
 *     istream factory over the baked ROM-FS (src/files_cron.cc).
 *   - the 256-colour game palette is read from <STATIC>/palettes.flx (palette 0,
 *     PALETTE_DAY) via Exult's own U7multiobject (FLEX object retrieve), through
 *     U7open_in -> the ROM. U7 palettes are 6-bit (0..63) -> scaled to 8-bit.
 *   - Exult's real Vga_file loads <STATIC>/shapes.vga and get_shape() decodes an
 *     RLE Shape_frame, which paint_rle() composites into the engine's
 *     Image_buffer8 (the SAME 8bpp buffer slice 2b blits).
 *   - a grid of the first shapes is painted, then the buffer + palette go to
 *     CRON_FB via src/vid_cron.cc (no fork patches; observe-the-buffer present).
 */
#include <cronopio.h>
#include "iwin8.h"
#include "vid_cron.h"
#include "vgafile.h"
#include "U7obj.h"
#include "files_cron.h"
#include <cstdint>
#include <cstdio>
#include <memory>

static void logln(const char* s) { int n = 0; while (s[n]) ++n; cron_log(s, n); }
static char lbuf[256];
#define LOGF(...) do { int n = snprintf(lbuf, sizeof lbuf, __VA_ARGS__); if (n>0) cron_log(lbuf, n); } while (0)

void setup(void) {
    int rc = exult_files_init();
    LOGF("exult_files_init rc=%d\n", rc);

    /* --- game palette: <STATIC>/palettes.flx object 0, 6-bit -> 8-bit --- */
    unsigned char pal768[768] = {0};
    {
        U7multiobject palobj(File_spec("<STATIC>/palettes.flx"), 0);
        size_t len = 0;
        std::unique_ptr<unsigned char[]> p = palobj.retrieve(len);
        LOGF("palette len=%u\n", (unsigned)len);
        if (p && len >= 768) {
            for (int i = 0; i < 768; ++i) {
                unsigned v = p[i] & 0x3f;            /* 6-bit */
                pal768[i] = (unsigned char)((v << 2) | (v >> 4));   /* -> 8-bit */
            }
        }
    }

    /* --- faces.vga (character portraits — RLE shapes, recognizable) --- */
    Vga_file shapes;
    shapes.load("<STATIC>/faces.vga");
    LOGF("faces.vga num_shapes=%d\n", shapes.get_num_shapes());

    /* --- window + clip --- */
    Image_window8 win(320, 200, 320, 200);
    win.set_palette(pal768, 255, 100);     /* not used by vid_present, but harmless */
    Image_buffer8* ib = win.get_ib8();
    ib->clear_clip();
    int W = (int)ib->get_width(), H = (int)ib->get_height();
    ib->fill8(0);

    /* --- paint a grid of the first shapes (frame 0), centred in each cell --- */
    const int COLS = 5, ROWS = 3, CELLW = W / COLS, CELLH = H / ROWS;
    int painted = 0;
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            int sh = r * COLS + c;
            if (sh >= shapes.get_num_shapes()) continue;
            Shape_frame* f = shapes.get_shape(sh, 0);
            if (!f || f->is_empty()) continue;
            int cellx = c * CELLW, celly = r * CELLH;
            int bx = cellx + (CELLW - f->get_width()) / 2;
            int by = celly + (CELLH - f->get_height()) / 2;
            int ox = bx + f->get_xleft(), oy = by + f->get_yabove();
            if (f->is_rle()) f->paint_rle(ib, ox, oy);   /* objects/items (RLE) */
            else f->paint(ib, ox, oy);                   /* flat terrain tiles */
            ++painted;
        }
    }
    LOGF("painted %d shapes\n", painted);

    vid_present(ib->get_bits(), W, H, (int)ib->get_line_width(), pal768);
    logln("shape_probe: presented real U7 shapes to CRON_FB\n");
    cron_exit(0);
}
void frame(void) {}
CRONOPIO_CART_INIT(setup, frame)
