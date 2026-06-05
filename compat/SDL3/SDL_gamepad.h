/* Minimal SDL3 <SDL3/SDL_gamepad.h> shim for the Cronopio Exult port — input.
 *
 * Exult's native input (exult.cc Handle_event GAMEPAD_AXIS_MOTION) reads the
 * left thumb-stick via SDL_GetGamepadAxis to drive `joy_aim`->start_actor. Our
 * cart (which IS our exult.cc) mirrors that path; the SDL_cron.cc shim
 * synthesises a left-stick axis from the Cronopio d-pad. Only the axis-query
 * surface that path needs is shimmed. Pulled by the <SDL3/SDL.h> umbrella. */
#ifndef CRONOPIO_SDL3_SDL_GAMEPAD_H
#define CRONOPIO_SDL3_SDL_GAMEPAD_H

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_events.h>   /* SDL_JoystickID */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque gamepad handle (the shim hands out one fixed non-null device). */
typedef struct SDL_Gamepad SDL_Gamepad;

/* Joystick axis full-scale (SDL3 value); the shim's d-pad maps to +/- this. */
#define SDL_JOYSTICK_AXIS_MAX 32767
#define SDL_JOYSTICK_AXIS_MIN (-32768)

typedef enum SDL_GamepadAxis {
    SDL_GAMEPAD_AXIS_INVALID = -1,
    SDL_GAMEPAD_AXIS_LEFTX = 0,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    SDL_GAMEPAD_AXIS_COUNT
} SDL_GamepadAxis;

extern SDL_Gamepad*  SDL_OpenGamepad(SDL_JoystickID instance_id);
extern void          SDL_CloseGamepad(SDL_Gamepad* gamepad);
extern SDL_Gamepad*  SDL_GetGamepadFromID(SDL_JoystickID instance_id);
extern Sint16        SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis);

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_GAMEPAD_H */
