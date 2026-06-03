/* Slice (objects) probe: place REAL Ultima 7 ifix objects on top of terrain.
 *
 * Builds on slice 4 (terrain region from u7map + u7chunks) by overlaying the
 * fixed objects of the "u7ifix" files: trees, signs, furniture, walls, ... —
 * the static scenery the game logic places into each chunk. Terrain is painted
 * first (the slice-4 path), then the ifix objects are painted on top, sorted
 * back-to-front, with Exult's own Vga_file/Shape_frame painter into the engine's
 * Image_buffer8, then blitted to CRON_FB. All file access routes through
 * Exult's U7open_in -> the baked ROM (the files_cron bridge from slice 1).
 *
 * Black Gate ifix format (V1 "orig"), from Exult gamemap.cc get_ifix_*:
 *   There is one FLEX file per superchunk: "<STATIC>/u7ifix" + two hex digits
 *   (u7ifix00 .. u7ifix8f, superchunk 0..143). Each FLEX has 256 entries, one
 *   per chunk (index = cy*16 + cx within the superchunk). An entry is a packed
 *   array of 4-byte object records:
 *     tx = (b0 >> 4) & 0xf;  ty = b0 & 0xf;  tz = b1 & 0xf;   (tile + lift)
 *     shnum = b2 + 256*(b3 & 3);   frnum = b3 >> 2;           (shapes.vga)
 *   Screen placement mirrors Exult's Get_shape_location (gamewin.cc): the shape
 *   origin (hot spot) sits at the lower-right of its base tile, raised by the
 *   lift: px = tilex*8 + (8-1) - 4*tz (paint_rle takes the hot spot directly).
 *
 * FLEX header layout: 80-byte title, magic1@80, count@84 (u32 LE), then at 0x80
 * a table of count * (offset:u32, length:u32). We parse it raw from a slurped
 * buffer (same "raw data layout + Exult render pipeline" approach as map_probe).
 */
#include <cronopio.h>
#include "iwin8.h"
#include "vid_cron.h"
#include "vgafile.h"
#include "U7obj.h"          /* U7multiobject, File_spec */
#include "utils.h"          /* U7open_in */
#include "files_cron.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <istream>
#include <vector>
#include <algorithm>

static char lbuf[256];
#define LOGF(...) do { int n = snprintf(lbuf, sizeof lbuf, __VA_ARGS__); if (n>0) cron_log(lbuf, n); } while (0)

enum { TILES = 16, TILEPX = 8, CHUNKPX = TILES * TILEPX };   /* 16 tiles * 8px = 128 */

static inline uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Slurp a whole file (via Exult's U7open_in -> the ROM) into a byte vector. */
static bool slurp(const char* path, std::vector<unsigned char>& out) {
    auto in = U7open_in(path);
    if (!in) return false;
    in->seekg(0, std::ios::end);
    std::streamoff n = in->tellg();
    in->seekg(0, std::ios::beg);
    if (n <= 0) return false;
    out.resize((size_t)n);
    in->read(reinterpret_cast<char*>(out.data()), n);
    return true;
}

/* One parsed FLEX file: the raw bytes + entry count. */
struct Flex {
    std::vector<unsigned char> buf;
    int count = 0;
    bool load(const char* path) {
        if (!slurp(path, buf) || buf.size() < 0x80) { count = 0; return false; }
        count = (int)le32(&buf[84]);
        if (count < 0 || (size_t)0x80 + (size_t)count * 8 > buf.size()) { count = 0; return false; }
        return true;
    }
    /* Entry i's data span; returns false (len=0) if absent. */
    bool entry(int i, const unsigned char*& p, size_t& len) const {
        len = 0; p = nullptr;
        if (i < 0 || i >= count) return false;
        size_t t = 0x80 + (size_t)i * 8;
        uint32_t off = le32(&buf[t]), l = le32(&buf[t + 4]);
        if (off == 0 || l == 0 || (size_t)off + l > buf.size()) return false;
        p = &buf[off]; len = l; return true;
    }
};

/* superchunk file name "<STATIC>/u7ifixXX" for superchunk S (0..143). */
static const char* ifix_name(int S) {
    static const char hex[] = "0123456789abcdef";
    static char name[40];
    snprintf(name, sizeof name, "<STATIC>/u7ifix%c%c", hex[(S >> 4) & 0xf], hex[S & 0xf]);
    return name;
}

/* Read chunk `cnum`'s 256 terrain tiles (shape#, frame#) from an open u7chunks stream. */
static void read_chunk(std::istream& ch, int cnum, int* shnum, int* frnum) {
    unsigned char buf[TILES * TILES * 2];
    ch.seekg((std::streamoff)cnum * sizeof(buf));
    ch.read(reinterpret_cast<char*>(buf), sizeof(buf));
    for (int i = 0; i < TILES * TILES; ++i) {
        shnum[i] = buf[i*2] + 256 * (buf[i*2+1] & 3);
        frnum[i] = (buf[i*2+1] >> 2) & 0x1f;
    }
}

/* One ifix object to draw, with a back-to-front sort key. */
struct Obj { int shnum, frnum, relx, rely, lift, key; };

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

    /* slurp u7map (144 superchunks * 512 bytes) for terrain lookup */
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

    /* Scan every superchunk's ifix file for the chunk with the most objects.
     * That chunk becomes the LEFT chunk of the rendered 2x1 region. */
    int bestX = 8, bestY = 8, bestObjs = -1;
    for (int S = 0; S < 144; ++S) {
        Flex flex;
        if (!flex.load(ifix_name(S))) continue;
        int scx = 16 * (S % 12), scy = 16 * (S / 12);
        for (int c = 0; c < 256 && c < flex.count; ++c) {
            const unsigned char* p; size_t len;
            if (!flex.entry(c, p, len)) continue;
            int n = (int)(len / 4);
            if (n > bestObjs) { bestObjs = n; bestX = scx + (c % 16); bestY = scy + (c / 16); }
        }
    }
    if (bestX > 190) bestX = 190;                          /* keep the 2x1 in-world */
    LOGF("densest ifix chunk world-(%d,%d) objs=%d\n", bestX, bestY, bestObjs);

    const int REGW = 2, REGH = 1;
    Image_window8 win(320, 200, 320, 200);
    win.set_palette(pal768, 255, 100);
    Image_buffer8* ib = win.get_ib8();
    ib->clear_clip();
    int W = (int)ib->get_width(), H = (int)ib->get_height();
    ib->fill8(0);
    int ox = (W - REGW*CHUNKPX)/2, oy = (H - REGH*CHUNKPX)/2;

    /* 1) terrain (slice-4 path) */
    auto chunks = U7open_in("<STATIC>/u7chunks");
    int sh[256], fr[256];
    int tiles = 0;
    for (int cyc = 0; cyc < REGH; ++cyc) {
        for (int cxc = 0; cxc < REGW; ++cxc) {
            int cn = chunk_at(bestX+cxc, bestY+cyc);
            read_chunk(*chunks, cn, sh, fr);
            int cox = ox + cxc*CHUNKPX, coy = oy + cyc*CHUNKPX;
            for (int ty = 0; ty < TILES; ++ty) for (int tx = 0; tx < TILES; ++tx) {
                Shape_frame* f = shapes.get_shape(sh[ty*16+tx], fr[ty*16+tx]);
                if (!f || f->is_empty()) continue;
                int px = cox + tx*TILEPX, py = coy + ty*TILEPX;
                if (f->is_rle()) f->paint_rle(ib, px + f->get_xleft(), py + f->get_yabove());
                else             f->paint(ib, px, py);
                ++tiles;
            }
        }
    }

    /* 2) ifix objects on top, collected then sorted back-to-front */
    std::vector<Obj> objs;
    int lastS = -1; Flex flex;
    for (int cyc = 0; cyc < REGH; ++cyc) {
        for (int cxc = 0; cxc < REGW; ++cxc) {
            int X = bestX+cxc, Y = bestY+cyc;
            int S = (Y/16)*12 + (X/16);
            if (S != lastS) { flex.load(ifix_name(S)); lastS = S; }
            int cidx = (Y%16)*16 + (X%16);
            const unsigned char* p; size_t len;
            if (!flex.entry(cidx, p, len)) continue;
            for (size_t i = 0; i + 4 <= len; i += 4) {
                const unsigned char* e = p + i;
                int tx = (e[0] >> 4) & 0xf, ty = e[0] & 0xf, tz = e[1] & 0xf;
                int shnum = e[2] + 256 * (e[3] & 3), frnum = e[3] >> 2;
                int relx = cxc*TILES + tx, rely = cyc*TILES + ty;
                objs.push_back({shnum, frnum, relx, rely, tz, relx + rely + tz});
            }
        }
    }
    std::sort(objs.begin(), objs.end(), [](const Obj& a, const Obj& b){ return a.key < b.key; });

    int painted = 0;
    for (const Obj& o : objs) {
        Shape_frame* f = shapes.get_shape(o.shnum, o.frnum);
        if (!f || f->is_empty()) continue;
        int lft = 4 * o.lift;
        int hx = ox + o.relx*TILEPX + (TILEPX-1) - lft;     /* hot spot (lower-right of base tile) */
        int hy = oy + o.rely*TILEPX + (TILEPX-1) - lft;
        if (f->is_rle()) f->paint_rle(ib, hx, hy);
        else             f->paint(ib, hx - f->get_xleft(), hy - f->get_yabove());
        ++painted;
    }
    LOGF("painted %d terrain tiles + %d / %d ifix objects\n", tiles, painted, (int)objs.size());

    vid_present(ib->get_bits(), W, H, (int)ib->get_line_width(), pal768);
    cron_log("object_probe: presented real U7 terrain + ifix objects to CRON_FB\n", 60);
    cron_exit(0);
}
void frame(void) {}
CRONOPIO_CART_INIT(setup, frame)
