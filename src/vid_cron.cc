/* See vid_cron.h. */
#include "vid_cron.h"

#include <cronopio.h> /* CRON_FB / CRON_PAL (extern "C" guarded) */

#include <cstddef>
#include <cstdint>

/* Cronopio framebuffer geometry (the cart's --region=fb:76800 = 320*240). */
enum { FB_W = 320, FB_H = 240 };

void vid_present(const unsigned char *bits, int w, int h, int pitch,
                 const unsigned char *pal768) {
    if (!CRON_FB || !CRON_PAL) {
        return; /* cron_resolve_video() not run — nothing to draw into */
    }

    /* Palette: Exult RGB888 triplets -> CRON_PAL packed 0x00RRGGBB. */
    for (int i = 0; i < 256; ++i) {
        uint32_t r = pal768[i * 3 + 0];
        uint32_t g = pal768[i * 3 + 1];
        uint32_t b = pal768[i * 3 + 2];
        CRON_PAL[i] = (r << 16) | (g << 8) | b;
    }

    /* Indices: copy the (clipped) w*h block to the top-left of the FB. The FB
     * row pitch is FB_W; the source row pitch is `pitch`. A per-pixel loop here
     * was ~64% of all VM time (profiled: 64000 px/frame as bytecode). Copy whole
     * rows with __builtin_memcpy, which cvm-translate lowers to a single VM MEMCPY
     * opcode (one native host memcpy per row) — and a contiguous block (the common
     * full-width 320 case) to ONE memcpy. CRON_FB is volatile only to keep the
     * blit from being optimised away; a bulk copy into it is fine. */
    int cw = w < FB_W ? w : FB_W;
    int ch = h < FB_H ? h : FB_H;
    uint8_t *fb = (uint8_t *)CRON_FB;
    if (pitch == FB_W && cw == FB_W) {
        __builtin_memcpy(fb, bits, (size_t)cw * (size_t)ch);
    } else {
        for (int y = 0; y < ch; ++y) {
            __builtin_memcpy(fb + (int64_t)y * FB_W,
                             bits + (int64_t)y * pitch, (size_t)cw);
        }
    }
}
