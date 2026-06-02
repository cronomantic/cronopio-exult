/* Cart-side video present for the Exult port: blit an 8bpp paletted surface to
 * Cronopio's framebuffer (CRON_FB, 320x240 indexed) + palette (CRON_PAL, 256
 * 0x00RRGGBB entries). This is the NATIVE 8bpp path — Exult composites the whole
 * game into an Image_buffer8, and we copy its indices + 256-colour palette
 * straight to the FB, no truecolor (matches Cronopio's 256-colour identity).
 *
 * Slice 2a takes raw buffer pointers (decoupled from Exult headers) so the
 * Image_buffer8 bring-up can present without the SDL3 shim. Slice 2b wires the
 * real present point (Image_window::UpdateRect) to this same blit. */
#ifndef VID_CRON_H
#define VID_CRON_H

/* Blit a w*h block of 8bpp palette indices (row pitch `pitch` bytes) to the top
 * of CRON_FB, and load `pal768` (256 RGB triplets, 0-255 each) into CRON_PAL.
 * Clipped to the 320x240 framebuffer. cron_resolve_video() must have run. */
void vid_present(const unsigned char *bits, int w, int h, int pitch,
                 const unsigned char *pal768);

#endif /* VID_CRON_H */
