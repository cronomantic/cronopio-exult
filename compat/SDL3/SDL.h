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

#include <SDL3/SDL_stdinc.h>   /* scalar types (Sint8..Uint64, SDL_bool) */

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct SDL_Point {
    int x, y;
} SDL_Point;

extern SDL_bool SDL_GetRectEnclosingPoints(const SDL_Point* points, int count,
                                           const SDL_Rect* clip, SDL_Rect* result);

/* hints — inert under Cronopio (SDL_SetHint is a no-op); the names the engine sets */
extern SDL_bool    SDL_SetHint(const char* name, const char* value);
extern const char* SDL_GetHint(const char* name);

/* misc — inert: no external browser (OpenURL). */
extern SDL_bool    SDL_OpenURL(const char* url);
#define SDL_HINT_RETURN_KEY_HIDES_IME          "SDL_RETURN_KEY_HIDES_IME"
#define SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES    "SDL_AUDIO_DEVICE_SAMPLE_FRAMES"
#define SDL_HINT_AUDIO_DRIVER                  "SDL_AUDIO_DRIVER"
#define SDL_HINT_IOS_HIDE_HOME_INDICATOR       "SDL_IOS_HIDE_HOME_INDICATOR"
#define SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE "SDL_MOUSE_EMULATE_WARP_WITH_RELATIVE"
#define SDL_HINT_MOUSE_RELATIVE_MODE_WARP      "SDL_MOUSE_RELATIVE_MODE_WARP"
#define SDL_HINT_ORIENTATIONS                  "SDL_ORIENTATIONS"
#define SDL_HINT_RENDER_DRIVER                 "SDL_RENDER_DRIVER"
#define SDL_HINT_VIDEO_DRIVER                  "SDL_VIDEO_DRIVER"
#define SDL_HINT_VIDEO_WAYLAND_EMULATE_MOUSE_WARP "SDL_VIDEO_WAYLAND_EMULATE_MOUSE_WARP"

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
extern SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format,
                                          void* pixels, int pitch);
extern void         SDL_DestroySurface(SDL_Surface* surface);

/* convenience zero-init macros (SDL3 spellings) */
#define SDL_zero(x)  __builtin_memset(&(x), 0, sizeof(x))
#define SDL_zerop(x) __builtin_memset((x), 0, sizeof(*(x)))
#define SDL_zeroa(x) __builtin_memset((x), 0, sizeof(x))
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
extern SDL_Renderer* SDL_GetRenderer(SDL_Window* window);
extern const char*   SDL_GetRendererName(SDL_Renderer* renderer);
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

/* The <SDL3/SDL.h> umbrella pulls the rest of the shimmed API, like real SDL3
 * (these are include-guarded and re-include SDL.h, which is a no-op here since
 * our guard is already defined). Subsystems that include a sub-header directly
 * (e.g. Gump.h -> SDL_keycode.h) still get the base types via this file. */
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_timer.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Render-coordinate mapping — declared here (after SDL_events.h) because it
 * takes an SDL_Event*. The renderer is inert under Cronopio, so the impl maps
 * 1:1 (no logical-presentation scaling); the cart presents the 8bpp buffer. */
extern SDL_bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer* renderer, SDL_Event* event);
#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_H */
