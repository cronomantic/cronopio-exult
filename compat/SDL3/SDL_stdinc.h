/* Minimal SDL3 <SDL3/SDL_stdinc.h> shim — the scalar-type base every other
 * SDL3 header builds on (Sint8..Uint64, SDL_bool). Split out so the sub-headers
 * (SDL_keycode/SDL_events/SDL_timer) get the types WITHOUT pulling the full
 * <SDL3/SDL.h> umbrella (which would create a fragile include cycle, since
 * SDL.h re-includes them at its end). Mirrors real SDL3, where SDL_stdinc.h is
 * the leaf every header includes. */
#ifndef CRONOPIO_SDL3_SDL_STDINC_H
#define CRONOPIO_SDL3_SDL_STDINC_H

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t   Sint8;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;
typedef int      SDL_bool; /* SDL3 uses real bool; int is layout-compatible here */

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_STDINC_H */
