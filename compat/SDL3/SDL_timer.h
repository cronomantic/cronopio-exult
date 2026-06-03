/* Minimal SDL3 <SDL3/SDL_timer.h> shim for the Cronopio Exult port.
 *
 * Exult's clock/time-queue + busy-wait Delay() use SDL's millisecond clock and
 * timers. Cronopio has its own frame clock; these map onto it (impl in
 * compat/SDL_cron.cc). SDL_AddTimer/RemoveTimer exist so the engine's optional
 * timer callbacks compile/link; the cart drives one frame per host frame so a
 * real callback scheduler isn't needed yet (the shim can no-op / track ids). */
#ifndef CRONOPIO_SDL3_SDL_TIMER_H
#define CRONOPIO_SDL3_SDL_TIMER_H

#include <SDL3/SDL_stdinc.h>   /* base types (NOT the full SDL.h — avoids the cycle) */

#ifdef __cplusplus
extern "C" {
#endif

typedef Uint32 SDL_TimerID;

/* SDL3: callback returns the next interval in ms (0 cancels). */
typedef Uint32 (*SDL_TimerCallback)(void* userdata, SDL_TimerID timerID, Uint32 interval);

extern Uint64      SDL_GetTicks(void);                 /* ms since start */
extern void        SDL_Delay(Uint32 ms);
extern SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void* userdata);
extern bool        SDL_RemoveTimer(SDL_TimerID id);

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_TIMER_H */
