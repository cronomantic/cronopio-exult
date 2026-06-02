/* Slice 2b probe: bring up Exult's REAL Image_window8 (imagewin/iwin8.cc, the
 * class the engine's Game_window uses) on the VM and present it to CRON_FB with
 * ZERO source patches. The SDL3 chain is shimmed in compat/; SDL_GetDesktop-
 * DisplayMode reports INDEX8 so Image_window::create_surface composites an 8bpp
 * surface; we draw with the window's own fill8/draw_line8, then blit its 8bpp
 * buffer (get_ib8()->get_bits()) + palette (get_palette()) to CRON_FB from the
 * cart — exactly how the engine loop will present once Game_window is up. No
 * Image_window/UpdateRect patch. Throwaway harness (supersedes vid_probe). */
#include <cronopio.h> /* extern "C" guarded */

#include "iwin8.h" /* Exult: Image_window8 */
#include "vid_cron.h"

#include <cstdint>

static void logln(const char* s) {
    int n = 0;
    while (s[n]) {
        ++n;
    }
    cron_log(s, n);
}

void setup(void) {
    /* The real engine window: 320x200 game, 320x200 display, scale 1, point. */
    Image_window8 win(320, 200, 320, 200);

    /* Feed a 256-colour palette (R/G/B gradient) through Exult's own set_palette
     * (applies gamma/brightness, stores into the window's colors[]). */
    unsigned char pal[768];
    for (int i = 0; i < 256; ++i) {
        pal[i * 3 + 0] = (unsigned char)i;
        pal[i * 3 + 1] = (unsigned char)(255 - i);
        pal[i * 3 + 2] = (unsigned char)((i * 5) & 0xff);
    }
    win.set_palette(pal, 255, 100);

    /* Reset the clip rectangle to the whole window before drawing. Image_window8
     * starts its buffer 0x0 (clip 0,0,0,0) and create_surface sets the buffer
     * dimensions but NOT the clip, so every clipped draw (fill8-rect, lines)
     * would clip out — only the unclipped fill8(val) full-fill would land. The
     * real engine's Game_window does this before painting; the cart must too. */
    win.get_ib8()->clear_clip();

    /* Composite a test pattern with the window's own 8bpp draw ops. */
    win.fill8(16);
    for (int i = 0; i < 16; ++i) {
        win.fill8((unsigned char)(i * 16 + 8), 320, 12, 0, i * 12);
    }
    win.fill8(200, 60, 60, 40, 40);
    win.fill8(100, 60, 60, 220, 100);
    win.draw_line8(255, 0, 0, 319, 199);

    /* Present: blit the window's 8bpp buffer + palette to CRON_FB (cart-side,
     * no engine present patch). get_palette() returns the gamma-applied colors. */
    Image_buffer8* ib = win.get_ib8();
    vid_present(ib->get_bits(), (int)ib->get_width(), (int)ib->get_height(),
                (int)ib->get_line_width(), win.get_palette());

    logln("win_probe: presented real Image_window8 320x200 to CRON_FB\n");
    cron_exit(0);
}

void frame(void) {}

CRONOPIO_CART_INIT(setup, frame)
