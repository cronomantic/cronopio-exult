/* Slice 4 probe: compose a REAL Ultima 7 world region from the map + chunks.
 *
 * Builds on slice 3 (single shapes) by assembling actual terrain: read u7map
 * (the 12x12 superchunks of 16x16 chunk numbers) + u7chunks (each chunk = 16x16
 * tiles, 2 bytes/tile -> shape#/frame# into shapes.vga), and paint every tile
 * with Exult's own Shape_frame painter into the engine's Image_buffer8, then
 * blit to CRON_FB. File access routes through U7open_in -> the baked ROM.
 *
 * Black Gate map/chunk format (V1), taken from Exult gamemap.cc / objs/chunkter.cc:
 *   u7map:    144 superchunks in order S=0..143; superchunk S covers absolute
 *             chunk (16*(S%12) + cx, 16*(S/12) + cy); each entry is a 2-byte LE
 *             chunk number; layout within a superchunk is [cy][cx], 512 bytes.
 *   u7chunks: chunk N at byte N*512; 256 tiles row-major [ty][tx]; each tile is
 *             2 bytes: shnum = b0 + 256*(b1 & 3); frnum = (b1 >> 2) & 0x1f.
 * Each tile shape is an 8x8 image (flat terrain -> paint(); RLE -> paint_rle()).
 */
#include <cronopio.h>
#include "iwin8.h"
#include "vid_cron.h"
#include "vgafile.h"
#include "U7obj.h"
#include "utils.h"          /* U7open_in */
#include "files_cron.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <istream>

static char lbuf[256];
#define LOGF(...) do { int n = snprintf(lbuf, sizeof lbuf, __VA_ARGS__); if (n>0) cron_log(lbuf, n); } while (0)

enum { TILES = 16, TILEPX = 8, CHUNKPX = TILES * TILEPX };   /* 16 tiles * 8px = 128 */

/* Read chunk `cnum`'s 256 tiles (shape#, frame#) from an open u7chunks stream. */
static void read_chunk(std::istream& ch, int cnum, int* shnum, int* frnum) {
    unsigned char buf[TILES * TILES * 2];
    ch.seekg((std::streamoff)cnum * sizeof(buf));
    ch.read(reinterpret_cast<char*>(buf), sizeof(buf));
    for (int i = 0; i < TILES * TILES; ++i) {
        shnum[i] = buf[i*2] + 256 * (buf[i*2+1] & 3);
        frnum[i] = (buf[i*2+1] >> 2) & 0x1f;
    }
}

void setup(void) {
    exult_files_init();

    /* game palette (6-bit -> 8-bit) */
    unsigned char pal768[768] = {0};
    {
        U7multiobject palobj(File_spec("<STATIC>/palettes.flx"), 0);
        size_t len = 0; auto p = palobj.retrieve(len);
        if (p && len >= 768) for (int i = 0; i < 768; ++i) { unsigned v = p[i] & 0x3f; pal768[i] = (unsigned char)((v<<2)|(v>>4)); }
    }

    Vga_file shapes;
    shapes.load("<STATIC>/shapes.vga");

    /* slurp u7map (144 superchunks * 512 bytes) */
    static unsigned char mapbuf[144 * 512];
    {
        auto m = U7open_in("<STATIC>/u7map");
        if (m) m->read(reinterpret_cast<char*>(mapbuf), sizeof(mapbuf));
        LOGF("u7map loaded, shapes.vga n=%d\n", shapes.get_num_shapes());
    }
    auto chunk_at = [&](int X, int Y) -> int {            /* abs chunk (X,Y) -> chunk# */
        int S = (Y/16)*12 + (X/16), lx = X%16, ly = Y%16;
        int off = S*512 + (ly*16 + lx)*2;
        return mapbuf[off] | (mapbuf[off+1] << 8);
    };

    auto chunks = U7open_in("<STATIC>/u7chunks");

    Image_window8 win(320, 200, 320, 200);
    win.set_palette(pal768, 255, 100);
    Image_buffer8* ib = win.get_ib8();
    ib->clear_clip();
    int W = (int)ib->get_width(), H = (int)ib->get_height();
    ib->fill8(0);

    /* region of REGW x REGH chunks; scan the world for a varied (land) one */
    const int REGW = 2, REGH = 1;
    int sh[256], fr[256];
    int bestX = 16, bestY = 16, bestScore = -1;
    for (int Y = 16; Y < 176 && bestScore < 80; Y += REGH) {
        for (int X = 16; X < 176; X += REGW) {
            int score = 0; bool seen[64] = {false};
            for (int dx = 0; dx < REGW; ++dx) {
                int cn = chunk_at(X+dx, Y);
                read_chunk(*chunks, cn, sh, fr);
                for (int i = 0; i < 256; ++i) { int k = sh[i] & 63; if (!seen[k]) { seen[k]=true; ++score; } }
            }
            if (score > bestScore) { bestScore = score; bestX = X; bestY = Y; }
            if (bestScore >= 80) break;
        }
    }
    LOGF("region world-chunk (%d,%d) variety=%d\n", bestX, bestY, bestScore);

    int ox = (W - REGW*CHUNKPX)/2, oy = (H - REGH*CHUNKPX)/2;
    int painted = 0;
    for (int cyc = 0; cyc < REGH; ++cyc) {
        for (int cxc = 0; cxc < REGW; ++cxc) {
            int cn = chunk_at(bestX+cxc, bestY+cyc);
            read_chunk(*chunks, cn, sh, fr);
            int cox = ox + cxc*CHUNKPX, coy = oy + cyc*CHUNKPX;
            for (int ty = 0; ty < TILES; ++ty) for (int tx = 0; tx < TILES; ++tx) {
                Shape_frame* f = shapes.get_shape(sh[ty*16+tx], fr[ty*16+tx]);
                if (!f || f->is_empty()) continue;
                int px = cox + tx*TILEPX, py = coy + ty*TILEPX;
                int oxp = px + f->get_xleft(), oyp = py + f->get_yabove();
                if (f->is_rle()) f->paint_rle(ib, oxp, oyp); else f->paint(ib, oxp, oyp);
                ++painted;
            }
        }
    }
    LOGF("painted %d tiles\n", painted);

    vid_present(ib->get_bits(), W, H, (int)ib->get_line_width(), pal768);
    cron_log("map_probe: presented real U7 world region to CRON_FB\n", 46);
    cron_exit(0);
}
void frame(void) {}
CRONOPIO_CART_INIT(setup, frame)
