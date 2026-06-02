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
