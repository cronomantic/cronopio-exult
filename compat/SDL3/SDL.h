/* Minimal SDL3 compatibility shim for the Cronopio Exult port — render slice.
 *
 * Exult is written against SDL3; Cronopio has no SDL. This header fakes the
 * subset of <SDL3/SDL.h> the engine's render path (imagewin/) touches, over
 * Cronopio primitives. Per the decided strategy ([[exult-sdl3-shim]]): the 8bpp
 * SDL_Surface + SDL_Palette are REAL plain-memory objects (Exult composites the
 * game into them), while the window/renderer/texture/display chain is shimmed as
 * inert handles — the present is patched in the fork (Image_window::UpdateRect →
 * CRON_FB) and create_surface takes a native 8bpp path under CRONOPIO. Layouts
 * are OUR own (all code that touches them compiles against THIS header), kept
 * small: only the fields the engine reads. Grown incrementally per slice.
 *
 * This covers ONLY what imagewin.cc/iwin8.cc need. Other subsystems (events,
 * gamepad, timer, IOStream) get added to their own SDL3/SDL_*.h as brought up. */
#ifndef CRONOPIO_SDL3_SDL_H
#define CRONOPIO_SDL3_SDL_H

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- scalar types (SDL spellings) -------------------------------------- */
typedef int8_t   Sint8;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;
typedef int      SDL_bool; /* SDL3 uses real bool; int is layout-compatible here */

/* ---- pixel formats (only the ones the render path names) --------------- */
typedef enum SDL_PixelFormat {
    SDL_PIXELFORMAT_UNKNOWN = 0,
    SDL_PIXELFORMAT_INDEX8  = 0x11000001u,
    SDL_PIXELFORMAT_RGB565  = 0x15151002u,
    SDL_PIXELFORMAT_XRGB8888 = 0x16161804u,
    SDL_PIXELFORMAT_ARGB8888 = 0x16362004u
} SDL_PixelFormat;

typedef struct SDL_Color {
    Uint8 r, g, b, a;
} SDL_Color;

typedef struct SDL_Palette {
    int        ncolors;
    SDL_Color* colors;
    Uint32     version;
    int        refcount;
} SDL_Palette;

typedef struct SDL_PixelFormatDetails {
    SDL_PixelFormat format;
    Uint8           bits_per_pixel;
    Uint8           bytes_per_pixel;
    Uint8           padding[2];
    Uint32          Rmask, Gmask, Bmask, Amask;
    Uint8           Rbits, Gbits, Bbits, Abits;
    Uint8           Rshift, Gshift, Bshift, Ashift;
} SDL_PixelFormatDetails;

/* SDL_Surface — OUR layout (only the fields imagewin reads). The palette lives
 * in an internal field the SDL_*SurfacePalette helpers manage. */
typedef struct SDL_Surface {
    Uint32          flags;
    SDL_PixelFormat format;
    int             w, h;
    int             pitch;
    void*           pixels;
    int             refcount;
    void*           reserved;
    SDL_Palette*    palette; /* shim-internal (SDL3 keeps it private) */
} SDL_Surface;

typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

typedef Uint32 SDL_DisplayID;

typedef struct SDL_DisplayMode {
    SDL_DisplayID   displayID;
    SDL_PixelFormat format;
    int             w, h;
    float           pixel_density;
    float           refresh_rate;
    int             refresh_rate_numerator;
    int             refresh_rate_denominator;
    void*           internal;
} SDL_DisplayMode;

/* opaque handles for the dropped window/renderer/texture chain */
typedef struct SDL_Window   SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture  SDL_Texture;
typedef struct SDL_IOStream SDL_IOStream;

/* ---- enums / flags the render path names -------------------------------- */
typedef Uint64 SDL_WindowFlags;
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x0000000000002000ULL
#define SDL_WINDOWPOS_CENTERED_DISPLAY(X) (0x2FFF0000u | (X))

typedef enum SDL_TextureAccess {
    SDL_TEXTUREACCESS_STATIC = 0,
    SDL_TEXTUREACCESS_STREAMING,
    SDL_TEXTUREACCESS_TARGET
} SDL_TextureAccess;

typedef enum SDL_BlendMode { SDL_BLENDMODE_NONE = 0 } SDL_BlendMode;

typedef enum SDL_RendererLogicalPresentation {
    SDL_LOGICAL_PRESENTATION_DISABLED = 0,
    SDL_LOGICAL_PRESENTATION_STRETCH,
    SDL_LOGICAL_PRESENTATION_LETTERBOX,
    SDL_LOGICAL_PRESENTATION_OVERSCAN,
    SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
} SDL_RendererLogicalPresentation;

/* ---- surface + palette (REAL, implemented in compat/SDL_cron.cc) -------- */
extern SDL_Surface* SDL_CreateSurface(int width, int height, SDL_PixelFormat format);
extern void         SDL_DestroySurface(SDL_Surface* surface);
extern SDL_Palette* SDL_CreateSurfacePalette(SDL_Surface* surface);
extern SDL_Palette* SDL_GetSurfacePalette(SDL_Surface* surface);
extern SDL_bool     SDL_SetPaletteColors(SDL_Palette* palette, const SDL_Color* colors,
                                         int firstcolor, int ncolors);
extern const SDL_PixelFormatDetails* SDL_GetPixelFormatDetails(SDL_PixelFormat format);
extern SDL_PixelFormat SDL_GetPixelFormatForMasks(int bpp, Uint32 Rmask, Uint32 Gmask,
                                                  Uint32 Bmask, Uint32 Amask);
extern SDL_bool        SDL_GetMasksForPixelFormat(SDL_PixelFormat format, int* bpp,
                                                  Uint32* Rmask, Uint32* Gmask,
                                                  Uint32* Bmask, Uint32* Amask);
extern const char* SDL_GetError(void);
extern void        SDL_free(void* mem);

/* ---- window / renderer / texture / display (DROPPED — inert stubs) ------ */
extern SDL_Window*   SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
extern void          SDL_DestroyWindow(SDL_Window* window);
extern SDL_bool      SDL_SetWindowSize(SDL_Window* window, int w, int h);
extern SDL_bool      SDL_SetWindowFullscreen(SDL_Window* window, SDL_bool fullscreen);
extern SDL_bool      SDL_SetWindowFullscreenMode(SDL_Window* window, const SDL_DisplayMode* mode);
extern SDL_bool      SDL_SetWindowPosition(SDL_Window* window, int x, int y);
extern SDL_bool      SDL_SetWindowTitle(SDL_Window* window, const char* title);
extern SDL_bool      SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
extern SDL_bool      SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h);
extern SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window);

extern SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name);
extern void          SDL_DestroyRenderer(SDL_Renderer* renderer);
extern SDL_bool      SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync);
extern SDL_bool      SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
extern SDL_bool      SDL_RenderClear(SDL_Renderer* renderer);
extern SDL_bool      SDL_RenderPresent(SDL_Renderer* renderer);
extern SDL_bool      SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture,
                                       const SDL_Rect* srcrect, const SDL_Rect* dstrect);
extern SDL_bool      SDL_GetCurrentRenderOutputSize(SDL_Renderer* renderer, int* w, int* h);
extern SDL_bool      SDL_SetRenderLogicalPresentation(SDL_Renderer* renderer, int w, int h,
                                                      SDL_RendererLogicalPresentation mode);

extern SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, SDL_PixelFormat format,
                                      int access, int w, int h);
extern void         SDL_DestroyTexture(SDL_Texture* texture);
extern SDL_bool     SDL_SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode blendMode);
extern SDL_bool     SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect,
                                      const void* pixels, int pitch);

extern SDL_DisplayID    SDL_GetPrimaryDisplay(void);
extern const SDL_DisplayMode* SDL_GetDesktopDisplayMode(SDL_DisplayID displayID);
extern SDL_DisplayMode** SDL_GetFullscreenDisplayModes(SDL_DisplayID displayID, int* count);
extern SDL_bool          SDL_GetClosestFullscreenDisplayMode(SDL_DisplayID displayID, int w, int h,
                                                             float refresh_rate, SDL_bool include_high_density,
                                                             SDL_DisplayMode* closest);

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_H */
