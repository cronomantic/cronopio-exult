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
SDL_bool    SDL_GetWindowSize(SDL_Window*, int* w, int* h) { if (w) *w = 320; if (h) *h = 200; return 1; }
SDL_bool    SDL_GetWindowSizeInPixels(SDL_Window*, int* w, int* h) { if (w) *w = 320; if (h) *h = 200; return 1; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window*) { return 1; }

SDL_Renderer* SDL_CreateRenderer(SDL_Window*, const char*) { return nullptr; }
void          SDL_DestroyRenderer(SDL_Renderer*) {}
SDL_bool      SDL_SetRenderVSync(SDL_Renderer*, int) { return 1; }
SDL_bool      SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8) { return 1; }
SDL_bool      SDL_RenderClear(SDL_Renderer*) { return 1; }
SDL_bool      SDL_RenderPresent(SDL_Renderer*) { return 1; }
SDL_bool      SDL_RenderTexture(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) { return 1; }
SDL_bool      SDL_GetCurrentRenderOutputSize(SDL_Renderer*, int* w, int* h) { if (w) *w = 320; if (h) *h = 200; return 1; }
SDL_bool      SDL_SetRenderLogicalPresentation(SDL_Renderer*, int, int, SDL_RendererLogicalPresentation) { return 1; }

SDL_Texture* SDL_CreateTexture(SDL_Renderer*, SDL_PixelFormat, int, int, int) { return nullptr; }
void         SDL_DestroyTexture(SDL_Texture*) {}
SDL_bool     SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 1; }
SDL_bool     SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int) { return 1; }

/* Report an INDEX8 (8bpp paletted) "monitor": this STEERS Exult down its native
 * 8bpp path (create_surface makes an 8bpp display/draw surface), with no source
 * patch — purely by what the shim returns. The cart then blits the engine's 8bpp
 * buffer to CRON_FB. See memory fork-patch-policy. */
static SDL_DisplayMode g_mode = {1, SDL_PIXELFORMAT_INDEX8, 320, 200, 1.0f, 60.0f, 60, 1, nullptr};
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
 * SDL_Delay is a NO-OP (a cart must never block — see cart-no-blocking-input).
 * SDL_AddTimer/RemoveTimer are inert (no callback scheduler yet). */
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
void   SDL_Delay(Uint32) {}
SDL_TimerID SDL_AddTimer(Uint32, SDL_TimerCallback, void*) { return 0; }
bool        SDL_RemoveTimer(SDL_TimerID) { return true; }

/* ---- events / input query ----------------------------------------------- *
 * INERT for now: no events are queued and all input-state queries report
 * "nothing pressed". The synthetic input layer (pad/mouse -> SDL events + the
 * OSK, see memory exult-input-model) plugs in HERE later; for Phase-1 frame
 * bring-up the engine simply sees an idle keyboard/mouse. */
bool   SDL_PollEvent(SDL_Event* event) { (void)event; return false; }
bool   SDL_PushEvent(SDL_Event*) { return true; }
void   SDL_PumpEvents(void) {}
int    SDL_PeepEvents(SDL_Event*, int, SDL_EventAction, Uint32, Uint32) { return 0; }
Uint32 SDL_RegisterEvents(int numevents) {
    /* Hand out a fresh user-event type range, starting at SDL_EVENT_USER. */
    static Uint32 next = SDL_EVENT_USER;
    Uint32        base = next;
    if (numevents > 0) next += (Uint32)numevents;
    return base;
}

SDL_Keymod   SDL_GetModState(void) { return 0; }
const bool*  SDL_GetKeyboardState(int* numkeys) {
    static bool keys[512] = {};   /* enough HID slots; all up */
    if (numkeys) *numkeys = 512;
    return keys;
}
SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    return 0;
}
const char*  SDL_GetKeyName(SDL_Keycode) { return ""; }
bool         SDL_StartTextInput(SDL_Window*) { return true; }
bool         SDL_StopTextInput(SDL_Window*) { return true; }
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
