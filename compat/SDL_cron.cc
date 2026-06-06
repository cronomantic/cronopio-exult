/* SDL3 shim implementation for the Cronopio Exult port — render slice.
 *
 * REAL: SDL_Surface + SDL_Palette as plain memory (Exult composites the 8bpp
 * game into them) and the pixel-format helpers. INERT: the window/renderer/
 * texture/display chain (Cronopio presents straight to CRON_FB, the fork's
 * create_surface/UpdateRect take a native path under CRONOPIO, so these are
 * never on the live path — they exist only so imagewin.cc links). See
 * compat/SDL3/SDL.h and memory exult-sdl3-shim. */
#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

#include <cronopio.h> /* cron_time_ms — the host millisecond clock for SDL_GetTicks */

/* ---- pixel-format details (only the formats the render path can name) ---- */
static SDL_PixelFormatDetails g_fmt_index8 = {
        SDL_PIXELFORMAT_INDEX8, 8, 1, {0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static SDL_PixelFormatDetails g_fmt_rgb565 = {
        SDL_PIXELFORMAT_RGB565, 16, 2, {0, 0}, 0xF800, 0x07E0, 0x001F, 0, 5, 6, 5, 0, 11, 5, 0, 0};
static SDL_PixelFormatDetails g_fmt_xrgb8888 = {
        SDL_PIXELFORMAT_XRGB8888, 32, 4, {0, 0},
        0x00FF0000, 0x0000FF00, 0x000000FF, 0, 8, 8, 8, 0, 16, 8, 0, 0};

static int bytes_for_format(SDL_PixelFormat f) {
    switch (f) {
    case SDL_PIXELFORMAT_INDEX8:
        return 1;
    case SDL_PIXELFORMAT_RGB565:
        return 2;
    default:
        return 4;
    }
}

const SDL_PixelFormatDetails* SDL_GetPixelFormatDetails(SDL_PixelFormat format) {
    switch (format) {
    case SDL_PIXELFORMAT_RGB565:
        return &g_fmt_rgb565;
    case SDL_PIXELFORMAT_INDEX8:
        return &g_fmt_index8;
    default:
        return &g_fmt_xrgb8888;
    }
}

SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, Uint32 Rmask, Uint32 Gmask,
                                           Uint32 Bmask, Uint32 Amask) {
    (void)Rmask;
    (void)Gmask;
    (void)Bmask;
    (void)Amask;
    if (bpp == 8) {
        return SDL_PIXELFORMAT_INDEX8;
    }
    if (bpp == 16) {
        return SDL_PIXELFORMAT_RGB565;
    }
    return SDL_PIXELFORMAT_XRGB8888;
}

SDL_bool SDL_GetMasksForPixelFormat(SDL_PixelFormat format, int* bpp, Uint32* Rmask,
                                    Uint32* Gmask, Uint32* Bmask, Uint32* Amask) {
    const SDL_PixelFormatDetails* d = SDL_GetPixelFormatDetails(format);
    if (bpp) {
        *bpp = d->bits_per_pixel;
    }
    if (Rmask) {
        *Rmask = d->Rmask;
    }
    if (Gmask) {
        *Gmask = d->Gmask;
    }
    if (Bmask) {
        *Bmask = d->Bmask;
    }
    if (Amask) {
        *Amask = d->Amask;
    }
    return 1;
}

/* ---- surface + palette (real, heap-backed) ------------------------------ */
SDL_Surface* SDL_CreateSurface(int width, int height, SDL_PixelFormat format) {
    SDL_Surface* s = (SDL_Surface*)std::calloc(1, sizeof(SDL_Surface));
    if (!s) {
        return nullptr;
    }
    int bpp     = bytes_for_format(format);
    s->format   = format;
    s->w        = width;
    s->h        = height;
    s->pitch    = width * bpp;
    s->pixels   = std::calloc((size_t)s->pitch * height, 1);
    s->refcount = 1;
    s->palette  = nullptr;
    if (!s->pixels) {
        std::free(s);
        return nullptr;
    }
    return s;
}

void SDL_DestroySurface(SDL_Surface* surface) {
    if (!surface) {
        return;
    }
    if (surface->palette) {
        std::free(surface->palette->colors);
        std::free(surface->palette);
    }
    std::free(surface->pixels);
    std::free(surface);
}

SDL_Palette* SDL_CreateSurfacePalette(SDL_Surface* surface) {
    if (!surface) {
        return nullptr;
    }
    if (surface->palette) {
        return surface->palette;
    }
    SDL_Palette* p = (SDL_Palette*)std::calloc(1, sizeof(SDL_Palette));
    if (!p) {
        return nullptr;
    }
    p->ncolors = 256;
    p->colors  = (SDL_Color*)std::calloc(256, sizeof(SDL_Color));
    if (!p->colors) {
        std::free(p);
        return nullptr;
    }
    surface->palette = p;
    return p;
}

SDL_Palette* SDL_GetSurfacePalette(SDL_Surface* surface) {
    return surface ? surface->palette : nullptr;
}

SDL_bool SDL_SetPaletteColors(SDL_Palette* palette, const SDL_Color* colors,
                              int firstcolor, int ncolors) {
    if (!palette || !colors) {
        return 0;
    }
    for (int i = 0; i < ncolors; ++i) {
        int idx = firstcolor + i;
        if (idx >= 0 && idx < palette->ncolors) {
            palette->colors[idx] = colors[i];
        }
    }
    return 1;
}

const char* SDL_GetError(void) {
    return "";
}

/* No external browser on Cronopio; report failure (callers tolerate it). */
SDL_bool SDL_OpenURL(const char* url) {
    (void)url;
    return 0;
}

/* One fixed software renderer name (inert renderer path). */
const char* SDL_GetRendererName(SDL_Renderer* renderer) {
    (void)renderer;
    return "cronopio";
}

void SDL_free(void* mem) {
    std::free(mem);
}

/* ---- DROPPED window / renderer / texture / display: inert stubs --------- *
 * Cronopio presents straight to CRON_FB; create_surface/UpdateRect take a
 * native path under CRONOPIO, so none of these run on the live path. They are
 * defined only so the unpatched parts of imagewin.cc link. */
SDL_Window* SDL_CreateWindow(const char*, int, int, SDL_WindowFlags) { return nullptr; }
void        SDL_DestroyWindow(SDL_Window*) {}
SDL_bool    SDL_SetWindowSize(SDL_Window*, int, int) { return 1; }
SDL_bool    SDL_SetWindowFullscreen(SDL_Window*, SDL_bool) { return 1; }
SDL_bool    SDL_SetWindowFullscreenMode(SDL_Window*, const SDL_DisplayMode*) { return 1; }
SDL_bool    SDL_SetWindowPosition(SDL_Window*, int, int) { return 1; }
SDL_bool    SDL_SetWindowTitle(SDL_Window*, const char*) { return 1; }
SDL_bool    SDL_GetWindowSize(SDL_Window*, int* w, int* h) { if (w) *w = 320; if (h) *h = 240; return 1; }
SDL_bool    SDL_GetWindowSizeInPixels(SDL_Window*, int* w, int* h) { if (w) *w = 320; if (h) *h = 240; return 1; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window*) { return 1; }

SDL_Renderer* SDL_CreateRenderer(SDL_Window*, const char*) { return nullptr; }
void          SDL_DestroyRenderer(SDL_Renderer*) {}
SDL_bool      SDL_SetRenderVSync(SDL_Renderer*, int) { return 1; }
SDL_bool      SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8) { return 1; }
SDL_bool      SDL_RenderClear(SDL_Renderer*) { return 1; }
SDL_bool      SDL_RenderPresent(SDL_Renderer*) { return 1; }
SDL_bool      SDL_RenderTexture(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) { return 1; }
SDL_bool      SDL_GetCurrentRenderOutputSize(SDL_Renderer*, int* w, int* h) { if (w) *w = 320; if (h) *h = 240; return 1; }
SDL_bool      SDL_SetRenderLogicalPresentation(SDL_Renderer*, int, int, SDL_RendererLogicalPresentation) { return 1; }

SDL_Texture* SDL_CreateTexture(SDL_Renderer*, SDL_PixelFormat, int, int, int) { return nullptr; }
void         SDL_DestroyTexture(SDL_Texture*) {}
SDL_bool     SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 1; }
SDL_bool     SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int) { return 1; }

/* Report an INDEX8 (8bpp paletted) "monitor": this STEERS Exult down its native
 * 8bpp path (create_surface makes an 8bpp display/draw surface), with no source
 * patch — purely by what the shim returns. The cart then blits the engine's 8bpp
 * buffer to CRON_FB. See memory fork-patch-policy. */
static SDL_DisplayMode g_mode = {1, SDL_PIXELFORMAT_INDEX8, 320, 240, 1.0f, 60.0f, 60, 1, nullptr};
SDL_DisplayID    SDL_GetPrimaryDisplay(void) { return 1; }
const SDL_DisplayMode* SDL_GetDesktopDisplayMode(SDL_DisplayID) { return &g_mode; }
SDL_DisplayMode** SDL_GetFullscreenDisplayModes(SDL_DisplayID, int* count) { if (count) *count = 0; return nullptr; }
SDL_bool SDL_GetClosestFullscreenDisplayMode(SDL_DisplayID, int, int, float, SDL_bool, SDL_DisplayMode* closest) {
    if (closest) *closest = g_mode;
    return 1;
}

/* ---- timer / clock ------------------------------------------------------ *
 * SDL_GetTicks rides Cronopio's host millisecond clock (cron_time_ms), but with
 * a per-frame monotonic NUDGE so it strictly advances WITHIN a single cart
 * frame. The host clock only ticks once per cart frame (headless bumps it ~16ms
 * before each frame; desktop is real wall-clock). Some engine routines busy-wait
 * on the clock inside one call — e.g. Palette::fade_out/in do
 * `t=GetTicks()+20; while (t >= GetTicks());` — which would spin forever in a
 * single frame if GetTicks() never changed. The nudge makes every call return a
 * strictly larger value until the host clock advances (then it resets), so such
 * busy-waits drain immediately (the cosmetic fade becomes instant) without
 * blocking the frame, while real pacing across frames still follows cron_time_ms.
 * SDL_Delay YIELDS the engine coroutine to the host (a cart must never block —
 * see cart-no-blocking-input). Exult's blocking loops (do_modal_gump, fades, the
 * naming screen) call Delay() (gump_utils.h) as their per-iteration wait; routing
 * that to the cart's coroutine yield (exult_engine_yield, defined in the cart)
 * lets those loops advance one host frame per Delay() — and present their buffer —
 * instead of hanging. No-op off the engine coroutine (e.g. during setup()).
 * SDL_AddTimer/RemoveTimer are inert (no callback scheduler yet). */
extern "C" void exult_engine_yield(void);   /* defined in src/gamewin_probe.cc */
Uint64 SDL_GetTicks(void) {
    static uint32_t last_base = 0;
    static uint32_t nudge     = 0;
    uint32_t        base      = cron_time_ms();
    if (base != last_base) {
        last_base = base;
        nudge     = 0;
    } else {
        ++nudge;
    }
    return (Uint64)base + nudge;
}
void   SDL_Delay(Uint32) { exult_engine_yield(); }
SDL_TimerID SDL_AddTimer(Uint32, SDL_TimerCallback, void*) { return 0; }
bool        SDL_RemoveTimer(SDL_TimerID) { return true; }

/* ---- events / input query: synthetic SDL events from Cronopio pad+mouse --- *
 * The DECIDED input model (memory exult-input-model / exult-sdl3-shim): the cart
 * (which IS our exult.cc) drives Exult's NATIVE input by mirroring Handle_events/
 * Handle_event over PUBLIC engine APIs, fed by SDL events THIS shim synthesises
 * from `cron_pad`/`cron_mouse`. Each cart frame calls SDL_PumpEvents() once to
 * build the frame's events into a queue; SDL_PollEvent() drains it (returns false
 * when empty, ending the frame's `while (SDL_PollEvent(&e)) Handle_event(e)`).
 *
 * This slice (movement-first): MOUSE motion/button/wheel + the d-pad as the LEFT
 * thumb-stick axis (drives Exult's joy_aim->start_actor). Pad FACE buttons ->
 * synthetic key events come in the action slice (need Exult's default keycodes).
 *
 * Cronopio mouse button bits are 1=L,2=R,4=M (headless.c); SDL masks are
 * L=1<<0, M=1<<1, R=1<<2 — so L/R/M must be REMAPPED, not copied. */

/* per-frame event queue (ring buffer) */
enum { EVQ_CAP = 128 };
static SDL_Event g_evq[EVQ_CAP];
static int       g_evq_head = 0, g_evq_tail = 0;
static void evq_push(const SDL_Event* e) {
    int n = (g_evq_tail + 1) % EVQ_CAP;
    if (n == g_evq_head) return;          /* full: drop (frame over-busy) */
    g_evq[g_evq_tail] = *e;
    g_evq_tail = n;
}
static bool evq_pop(SDL_Event* e) {
    if (g_evq_head == g_evq_tail) return false;
    *e = g_evq[g_evq_head];
    g_evq_head = (g_evq_head + 1) % EVQ_CAP;
    return true;
}

/* live input state (updated each pump; read by the *State queries + gamepad) */
static float  g_mouse_x = 0.0f, g_mouse_y = 0.0f;
static Uint32 g_mouse_sdl_mask = 0;        /* SDL L/M/R mask */
static Uint32 g_prev_pad = 0;
/* While the cart's OSK is active (it owns the flag, set around the text modal),
 * the pad face buttons DRIVE the OSK (type/backspace/enter), so their normal
 * action-key synthesis below is suppressed. */
extern "C" int exult_osk_active(void);   /* defined in src/gamewin_probe.cc */
static Sint16 g_axis_x = 0, g_axis_y = 0;  /* synthetic LEFT stick from d-pad */
static int    g_gamepad_dev = 0;           /* dummy device handle backing */

static Uint32 cron_to_sdl_mouse_mask(uint32_t cb) {
    Uint32 m = 0;
    if (cb & 1u) m |= SDL_BUTTON_LMASK;    /* cron L (1) -> SDL L (1<<0) */
    if (cb & 2u) m |= SDL_BUTTON_RMASK;    /* cron R (2) -> SDL R (1<<2) */
    if (cb & 4u) m |= SDL_BUTTON_MMASK;    /* cron M (4) -> SDL M (1<<1) */
    return m;
}

void SDL_PumpEvents(void) {
    /* ---- mouse ---- */
    int32_t  mx = 0, my = 0;
    uint32_t cb        = cron_mouse(&mx, &my);
    Uint32   sdl_mask  = cron_to_sdl_mouse_mask(cb);
    float    fx = (float)mx, fy = (float)my;

    if (fx != g_mouse_x || fy != g_mouse_y) {
        SDL_Event e;
        std::memset(&e, 0, sizeof e);
        e.type         = SDL_EVENT_MOUSE_MOTION;
        e.motion.state = sdl_mask;
        e.motion.x = fx;  e.motion.y = fy;
        e.motion.xrel = fx - g_mouse_x;
        e.motion.yrel = fy - g_mouse_y;
        evq_push(&e);
    }
    /* button edges (this frame vs last) */
    Uint32 down = sdl_mask & ~g_mouse_sdl_mask;
    Uint32 up   = g_mouse_sdl_mask & ~sdl_mask;
    const Uint8 btns[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT};
    for (int i = 0; i < 3; ++i) {
        Uint32 bit = SDL_BUTTON_MASK(btns[i]);
        if ((down | up) & bit) {
            SDL_Event e;
            std::memset(&e, 0, sizeof e);
            e.type         = (down & bit) ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                          : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = btns[i];
            e.button.down   = (down & bit) != 0;
            e.button.clicks = 1;
            e.button.x = fx;  e.button.y = fy;
            evq_push(&e);
        }
    }
    /* wheel impulse */
    int wheel = (int)cron_mouse_wheel();
    if (wheel != 0) {
        SDL_Event e;
        std::memset(&e, 0, sizeof e);
        e.type          = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.y       = (float)wheel;
        e.wheel.mouse_x = fx;  e.wheel.mouse_y = fy;
        evq_push(&e);
    }
    g_mouse_x = fx;  g_mouse_y = fy;  g_mouse_sdl_mask = sdl_mask;

    /* ---- d-pad -> LEFT thumb-stick axis (full deflection) ---- */
    uint32_t pad = cron_pad(0);
    Sint16   ax  = (Sint16)(((pad & CRON_BTN_RIGHT) ? SDL_JOYSTICK_AXIS_MAX : 0)
                          - ((pad & CRON_BTN_LEFT)  ? SDL_JOYSTICK_AXIS_MAX : 0));
    Sint16   ay  = (Sint16)(((pad & CRON_BTN_DOWN)  ? SDL_JOYSTICK_AXIS_MAX : 0)
                          - ((pad & CRON_BTN_UP)    ? SDL_JOYSTICK_AXIS_MAX : 0));
    if (ax != g_axis_x) {
        g_axis_x = ax;
        SDL_Event e; std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        e.gaxis.axis = (Uint8)SDL_GAMEPAD_AXIS_LEFTX;
        e.gaxis.value = ax;
        evq_push(&e);
    }
    if (ay != g_axis_y) {
        g_axis_y = ay;
        SDL_Event e; std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        e.gaxis.axis = (Uint8)SDL_GAMEPAD_AXIS_LEFTY;
        e.gaxis.value = ay;
        evq_push(&e);
    }

    /* ---- face buttons -> synthetic key events (action slice) ----
     * Map each pad face button to the KEYCODE of an Exult default action
     * binding (data/bg/defaultkeys.txt: a single-letter binding lowercases to
     * its ASCII keycode, mod=NONE). The cart dispatches these to the engine's
     * native keybinder (gump_man->handle_kbd_event then keybinder->HandleEvent),
     * so the pad reaches the full action set with no per-action cart code.
     * D-pad bits are the movement stick (above), so only the face buttons map.
     * SUPPRESSED while the OSK is active: then A/B/START drive the OSK (type/
     * backspace/enter) cart-side, so they must NOT also fire 'i'/'c'/Esc here. */
    if (exult_osk_active()) { g_prev_pad = pad; return; }
    static const struct { uint32_t btn; SDL_Keycode key; } KEYMAP[] = {
        { CRON_BTN_A,     (SDL_Keycode)'i' },   /* inventory      */
        { CRON_BTN_B,     (SDL_Keycode)'c' },   /* toggle_combat  */
        { CRON_BTN_X,     (SDL_Keycode)'t' },   /* target_mode (attack) */
        { CRON_BTN_Y,     (SDL_Keycode)'z' },   /* stats          */
        { CRON_BTN_R,     (SDL_Keycode)'r' },   /* face_stats     */
        { CRON_BTN_START, SDLK_ESCAPE       },  /* close_or_menu  */
    };
    uint32_t pdown = pad & ~g_prev_pad;   /* newly pressed this frame  */
    uint32_t pup   = g_prev_pad & ~pad;   /* newly released this frame */
    for (unsigned i = 0; i < sizeof KEYMAP / sizeof KEYMAP[0]; ++i) {
        uint32_t b = KEYMAP[i].btn;
        if (!((pdown | pup) & b)) continue;
        SDL_Event e; std::memset(&e, 0, sizeof e);
        e.type      = (pdown & b) ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key   = KEYMAP[i].key;
        e.key.mod   = 0;                  /* SDL_KMOD_NONE (unmodified binding) */
        e.key.down  = (pdown & b) != 0;
        e.key.repeat = false;
        evq_push(&e);
    }
    g_prev_pad = pad;
}

bool SDL_PollEvent(SDL_Event* event) {
    if (!event) return false;
    return evq_pop(event);
}
bool SDL_PushEvent(SDL_Event* event) {
    if (event) evq_push(event);
    return true;
}
int  SDL_PeepEvents(SDL_Event*, int, SDL_EventAction, Uint32, Uint32) { return 0; }
Uint32 SDL_RegisterEvents(int numevents) {
    /* Hand out a fresh user-event type range, starting at SDL_EVENT_USER. */
    static Uint32 next = SDL_EVENT_USER;
    Uint32        base = next;
    if (numevents > 0) next += (Uint32)numevents;
    return base;
}

SDL_Keymod   SDL_GetModState(void) { return 0; }  /* no pad->modifier yet */
const bool*  SDL_GetKeyboardState(int* numkeys) {
    static bool keys[512] = {};   /* enough HID slots; all up (no synthetic keys yet) */
    if (numkeys) *numkeys = 512;
    return keys;
}
SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y) {
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
    return g_mouse_sdl_mask;
}

/* ---- gamepad (axis only): one fixed device, left stick = the d-pad -------- */
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID) { return (SDL_Gamepad*)&g_gamepad_dev; }
void         SDL_CloseGamepad(SDL_Gamepad*) {}
SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID) { return (SDL_Gamepad*)&g_gamepad_dev; }
Sint16       SDL_GetGamepadAxis(SDL_Gamepad*, SDL_GamepadAxis axis) {
    if (axis == SDL_GAMEPAD_AXIS_LEFTX) return g_axis_x;
    if (axis == SDL_GAMEPAD_AXIS_LEFTY) return g_axis_y;
    return 0;
}
const char*  SDL_GetKeyName(SDL_Keycode) { return ""; }
/* Cronopio has no keyboard; text entry is the cart's OSK (driven by the pad). The
 * engine's SDL text-input mode is only used by Exult's TOUCH path (touchui is null
 * here), so these are inert. The OSK's on/off is the cart-owned exult_osk_active(). */
bool         SDL_StartTextInput(SDL_Window*) { return true; }
bool         SDL_StopTextInput(SDL_Window*)  { return true; }
bool         SDL_TextInputActive(SDL_Window*) { return false; }
SDL_Finger** SDL_GetTouchFingers(SDL_TouchID, int* count) {
    if (count) *count = 0;
    return nullptr;   /* no touch device on Cronopio */
}

/* ---- rect / hint / renderer glue ---------------------------------------- */
/* Minimal enclosing rect of `points` (faithful), optionally clipped. */
SDL_bool SDL_GetRectEnclosingPoints(const SDL_Point* points, int count,
                                    const SDL_Rect* clip, SDL_Rect* result) {
    if (!points || count < 1 || !result) {
        return 0;
    }
    int minx = points[0].x, miny = points[0].y;
    int maxx = points[0].x, maxy = points[0].y;
    for (int i = 1; i < count; ++i) {
        if (points[i].x < minx) minx = points[i].x;
        if (points[i].y < miny) miny = points[i].y;
        if (points[i].x > maxx) maxx = points[i].x;
        if (points[i].y > maxy) maxy = points[i].y;
    }
    if (clip) {
        int cx2 = clip->x + clip->w - 1;
        int cy2 = clip->y + clip->h - 1;
        if (minx < clip->x) minx = clip->x;
        if (miny < clip->y) miny = clip->y;
        if (maxx > cx2)     maxx = cx2;
        if (maxy > cy2)     maxy = cy2;
        if (minx > maxx || miny > maxy) {   /* fully clipped away */
            *result = SDL_Rect{0, 0, 0, 0};
            return 0;
        }
    }
    *result = SDL_Rect{minx, miny, maxx - minx + 1, maxy - miny + 1};
    return 1;
}

/* Hints are inert under Cronopio (no SDL backends to steer). */
SDL_bool SDL_SetHint(const char*, const char*) { return 1; }

/* A surface that BORROWS the caller's pixels (SDL_CreateSurfaceFrom does not
 * own them). NOTE: SDL_DestroySurface here frees surface->pixels — only call it
 * on owned surfaces; these borrowed ones live on the inert render path (the
 * cart presents the engine's own buffer to CRON_FB) and are not destroyed. */
SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format,
                                   void* pixels, int pitch) {
    SDL_Surface* s = (SDL_Surface*)std::calloc(1, sizeof(SDL_Surface));
    if (!s) {
        return nullptr;
    }
    s->format   = format;
    s->w        = width;
    s->h        = height;
    s->pitch    = pitch;
    s->pixels   = pixels;
    s->refcount = 1;
    s->palette  = nullptr;
    return s;
}

/* Inert renderer path: Cronopio presents straight to CRON_FB. */
SDL_Renderer* SDL_GetRenderer(SDL_Window*) { return nullptr; }
/* Cronopio's framebuffer is 1:1 — render coords already equal event coords. */
SDL_bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer*, SDL_Event*) { return 1; }
