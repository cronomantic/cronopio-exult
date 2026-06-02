/* Slice 2a probe: bring up Exult's real 8bpp buffer (imagewin/Image_buffer8) on
 * the VM and present it to Cronopio's framebuffer. Composites a test pattern into
 * an Image_buffer8 with Exult's own fill8/draw_line8, builds a 256-colour palette,
 * and blits indices + palette to CRON_FB/CRON_PAL via vid_cron. This proves the
 * 8bpp buffer compiles + runs and the native blit works, WITHOUT the SDL3 shim
 * (Image_window8 + create_surface/UpdateRect come in slice 2b). Throwaway harness
 * (like files_probe); the engine's real present replaces it. */
#include <cronopio.h> /* extern "C" guarded */

#include "ibuf8.h" /* Exult: Image_buffer8 */
#include "vid_cron.h"

#include <cstdint>

static void logln(const char *s) {
    int n = 0;
    while (s[n]) {
        ++n;
    }
    cron_log(s, n);
}

void setup(void) {
    /* The real Exult 8bpp buffer (320x200, the U7 game resolution). */
    Image_buffer8 buf(320, 200);

    buf.fill8(16); /* background = index 16 */

    /* 16 horizontal bands of distinct indices (span the palette). */
    for (int i = 0; i < 16; ++i) {
        buf.fill8((unsigned char)(i * 16 + 8), 320, 12, 0, i * 12);
    }
    /* Two solid boxes + a diagonal line, all via Exult's own draw ops. */
    buf.fill8(200, 60, 60, 40, 40);
    buf.fill8(100, 60, 60, 220, 100);
    buf.draw_line8(255, 0, 0, 319, 199);

    /* A 256-colour palette: an R/G/B gradient so the bands map to real colours. */
    unsigned char pal[768];
    for (int i = 0; i < 256; ++i) {
        pal[i * 3 + 0] = (unsigned char)i;
        pal[i * 3 + 1] = (unsigned char)(255 - i);
        pal[i * 3 + 2] = (unsigned char)((i * 5) & 0xff);
    }

    vid_present(buf.get_bits(), (int)buf.get_width(), (int)buf.get_height(),
                (int)buf.get_line_width(), pal);

    logln("vid_probe: presented Image_buffer8 320x200 to CRON_FB\n");
    cron_exit(0);
}

void frame(void) {}

CRONOPIO_CART_INIT(setup, frame)
