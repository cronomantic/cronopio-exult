/* Cart-side definitions for engine symbols the render path references but whose
 * owning subsystems aren't brought up yet. Provided in compat/ (NOT patched into
 * the Exult fork — see memory fork-patch-policy: adaptation lives outside the
 * motor source). Grown as slices reference more not-yet-built subsystems.
 *
 * Mouse::current_mouse: imagewin.cc guards every use behind `if (Mouse::mouse())`,
 * so a null instance means "no cursor", which is correct until the gump/mouse
 * slice. Defined here against the real mouse.h so the type/mangling are exact. */
#include "mouse.h"

Mouse* Mouse::current_mouse = nullptr;

/* Mouse offset helpers (owner: mouse.cc — the gump/cursor subsystem, not built
 * for the render slice). screen_to_game references them, but always behind
 * `if (Mouse::mouse())`, which is null here, so they never run; provide bodies
 * so the references link. Replaced by mouse.cc with the input slice. */
void Mouse::apply_fast_offset(int& mx, int& my) {
    (void)mx;
    (void)my;
}

void Mouse::unapply_fast_offset(int& mx, int& my) {
    (void)mx;
    (void)my;
}

/* get_text_msg (owner: shapes/items.cc, the game text-message table — not built
 * for the render slice). Only reached here from get_displayname_for_scaler (a
 * scaler's display name); an empty string is harmless. Replaced by the real
 * items.cc when the text/shapes subsystem is brought up. */
const char* get_text_msg(unsigned num) {
    (void)num;
    return "";
}

/* SaveIMG_RW (owner: imagewin/save_screenshot.cc — drags PNG encoding, not built).
 * Only reached from Image_window::screenshot, which the cart never calls. Report
 * failure. Brought up properly if/when screenshots are wanted. */
bool SaveIMG_RW(SDL_Surface* saveme, SDL_IOStream* dst, bool freedst, int guardband) {
    (void)saveme;
    (void)dst;
    (void)freedst;
    (void)guardband;
    return false;
}
