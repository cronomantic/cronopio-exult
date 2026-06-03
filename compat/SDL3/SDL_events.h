/* Minimal SDL3 <SDL3/SDL_events.h> shim for the Cronopio Exult port.
 *
 * Fakes the SDL_Event union + event types + the event/keyboard/mouse/text
 * query API that Exult's input + gump code names. Cronopio has no SDL; our
 * input layer ([[exult-sdl3-shim]]) will SYNTHESISE these events from the
 * Cronopio pad/mouse, so the struct field names match what the engine reads
 * (surveyed from the source) and the SDL_EVENT_ / SDL_SCANCODE_ values follow
 * SDL3 so they stay self-consistent. Layouts are OUR own (all code that
 * touches them compiles against THIS header); only the fields the engine uses.
 * Also folds in the SDL_Scancode constants and mouse-button masks the engine
 * names (real SDL3 splits these into SDL_scancode.h/SDL_mouse.h; Exult only
 * ever includes the <SDL3/SDL.h> umbrella, which pulls this). */
#ifndef CRONOPIO_SDL3_SDL_EVENTS_H
#define CRONOPIO_SDL3_SDL_EVENTS_H

#include <SDL3/SDL_stdinc.h>  /* base scalar types (NOT the full SDL.h — avoids the cycle) */
#include <SDL3/SDL_keycode.h> /* SDL_Keycode / SDL_Keymod */

#ifdef __cplusplus
extern "C" {
#endif

/* SDL_Window is defined by the SDL.h umbrella; forward-declare it here so this
 * header is usable on its own (identical typedef redefinition is allowed). */
typedef struct SDL_Window SDL_Window;

/* ---- id / scalar types ------------------------------------------------- */
typedef Uint32 SDL_Scancode;       /* a USB-HID scancode number in our shim */
typedef Uint32 SDL_JoystickID;
typedef Uint32 SDL_MouseID;
#define SDL_TOUCH_MOUSEID ((SDL_MouseID)-1)   /* events synthesised from touch */
typedef Uint64 SDL_TouchID;
typedef Uint64 SDL_FingerID;

typedef struct SDL_Finger {
    SDL_FingerID id;
    float        x, y;
    float        pressure;
} SDL_Finger;
typedef Uint32 SDL_MouseButtonFlags;

/* ---- scancodes the engine names (values = USB-HID, matching SDL_keycode) - */
#define SDL_SCANCODE_KP_DIVIDE   84
#define SDL_SCANCODE_KP_MULTIPLY 85
#define SDL_SCANCODE_KP_MINUS    86
#define SDL_SCANCODE_KP_PLUS     87
#define SDL_SCANCODE_KP_ENTER    88
#define SDL_SCANCODE_KP_1        89
#define SDL_SCANCODE_KP_2        90
#define SDL_SCANCODE_KP_3        91
#define SDL_SCANCODE_KP_4        92
#define SDL_SCANCODE_KP_5        93
#define SDL_SCANCODE_KP_6        94
#define SDL_SCANCODE_KP_7        95
#define SDL_SCANCODE_KP_8        96
#define SDL_SCANCODE_KP_9        97
#define SDL_SCANCODE_KP_0        98
#define SDL_SCANCODE_KP_PERIOD   99
#define SDL_SCANCODE_LCTRL       224
#define SDL_SCANCODE_RCTRL       228

/* ---- mouse buttons + masks --------------------------------------------- */
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5
#define SDL_BUTTON_MASK(X) (1u << ((X) - 1))
#define SDL_BUTTON_LMASK  SDL_BUTTON_MASK(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK  SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK  SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)

/* ---- event type ids (SDL3 values) -------------------------------------- */
typedef enum SDL_EventType {
    SDL_EVENT_FIRST                = 0,
    SDL_EVENT_QUIT                 = 0x100,
    SDL_EVENT_WINDOW_MOUSE_ENTER   = 0x20b,
    SDL_EVENT_WINDOW_FOCUS_GAINED  = 0x20d,
    SDL_EVENT_WINDOW_FOCUS_LOST    = 0x20e,
    SDL_EVENT_KEY_DOWN             = 0x300,
    SDL_EVENT_KEY_UP               = 0x301,
    SDL_EVENT_TEXT_INPUT           = 0x303,
    SDL_EVENT_MOUSE_MOTION         = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN    = 0x401,
    SDL_EVENT_MOUSE_BUTTON_UP      = 0x402,
    SDL_EVENT_MOUSE_WHEEL          = 0x403,
    SDL_EVENT_GAMEPAD_AXIS_MOTION  = 0x650,
    SDL_EVENT_GAMEPAD_ADDED        = 0x653,
    SDL_EVENT_GAMEPAD_REMOVED      = 0x654,
    SDL_EVENT_FINGER_DOWN          = 0x700,
    SDL_EVENT_FINGER_UP            = 0x701,
    SDL_EVENT_FINGER_MOTION        = 0x702,
    SDL_EVENT_DROP_FILE            = 0x1000,
    SDL_EVENT_DROP_TEXT            = 0x1001,
    SDL_EVENT_DROP_BEGIN           = 0x1002,
    SDL_EVENT_DROP_COMPLETE        = 0x1003,
    SDL_EVENT_DROP_POSITION        = 0x1004,
    SDL_EVENT_USER                 = 0x8000,
    SDL_EVENT_LAST                 = 0xffff
} SDL_EventType;

/* ---- per-kind event structs (only the fields the engine reads) --------- */
typedef struct SDL_CommonEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
} SDL_CommonEvent;

typedef struct SDL_KeyboardEvent {
    Uint32       type;
    Uint32       reserved;
    Uint64       timestamp;
    Uint32       windowID;
    SDL_MouseID  which;
    SDL_Scancode scancode;
    SDL_Keycode  key;
    SDL_Keymod   mod;
    Uint16       raw;
    bool         down;
    bool         repeat;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    Uint32      type;
    Uint32      reserved;
    Uint64      timestamp;
    Uint32      windowID;
    const char* text;
} SDL_TextInputEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32               type;
    Uint32               reserved;
    Uint64               timestamp;
    Uint32               windowID;
    SDL_MouseID          which;
    SDL_MouseButtonFlags state;
    float                x, y;
    float                xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32      type;
    Uint32      reserved;
    Uint64      timestamp;
    Uint32      windowID;
    SDL_MouseID which;
    Uint8       button;
    bool        down;
    Uint8       clicks;
    Uint8       padding;
    float       x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32      type;
    Uint32      reserved;
    Uint64      timestamp;
    Uint32      windowID;
    SDL_MouseID which;
    float       x, y;
    Uint32      direction;
    float       mouse_x, mouse_y;
} SDL_MouseWheelEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    Uint32 windowID;
    Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct SDL_GamepadAxisEvent {
    Uint32         type;
    Uint32         reserved;
    Uint64         timestamp;
    SDL_JoystickID which;
    Uint8          axis;
    Uint8          padding1, padding2, padding3;
    Sint16         value;
    Uint16         padding4;
} SDL_GamepadAxisEvent;

typedef struct SDL_GamepadButtonEvent {
    Uint32         type;
    Uint32         reserved;
    Uint64         timestamp;
    SDL_JoystickID which;
    Uint8          button;
    bool           down;
    Uint8          padding1, padding2;
} SDL_GamepadButtonEvent;

typedef struct SDL_GamepadDeviceEvent {
    Uint32         type;
    Uint32         reserved;
    Uint64         timestamp;
    SDL_JoystickID which;
} SDL_GamepadDeviceEvent;

typedef struct SDL_TouchFingerEvent {
    Uint32       type;
    Uint32       reserved;
    Uint64       timestamp;
    SDL_TouchID  touchID;
    SDL_FingerID fingerID;
    float        x, y;
    float        dx, dy;
    float        pressure;
    Uint32       windowID;
} SDL_TouchFingerEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
} SDL_QuitEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    Uint32 windowID;
    Sint32 code;
    void*  data1;
    void*  data2;
} SDL_UserEvent;

typedef union SDL_Event {
    Uint32                 type;
    SDL_CommonEvent        common;
    SDL_KeyboardEvent      key;
    SDL_TextInputEvent     text;
    SDL_MouseMotionEvent   motion;
    SDL_MouseButtonEvent   button;
    SDL_MouseWheelEvent    wheel;
    SDL_WindowEvent        window;
    SDL_GamepadAxisEvent   gaxis;
    SDL_GamepadButtonEvent gbutton;
    SDL_GamepadDeviceEvent gdevice;
    SDL_TouchFingerEvent   tfinger;
    SDL_QuitEvent          quit;
    SDL_UserEvent          user;
    Uint8                  padding[128];
} SDL_Event;

typedef enum SDL_EventAction {
    SDL_ADDEVENT = 0,
    SDL_PEEKEVENT,
    SDL_GETEVENT
} SDL_EventAction;

/* ---- event + input query API (impl in compat/SDL_cron.cc) -------------- */
extern bool   SDL_PollEvent(SDL_Event* event);
extern bool   SDL_PushEvent(SDL_Event* event);
extern void   SDL_PumpEvents(void);
extern int    SDL_PeepEvents(SDL_Event* events, int numevents, SDL_EventAction action,
                             Uint32 minType, Uint32 maxType);
extern Uint32 SDL_RegisterEvents(int numevents);

extern SDL_Keymod  SDL_GetModState(void);
extern const bool* SDL_GetKeyboardState(int* numkeys);
extern SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y);
extern SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode, SDL_Keymod modstate, bool key_event);
extern const char* SDL_GetKeyName(SDL_Keycode key);
extern bool        SDL_StartTextInput(SDL_Window* window);
extern bool        SDL_StopTextInput(SDL_Window* window);

/* touch (inert on Cronopio — no touch device; returns nullptr/0) */
extern SDL_Finger** SDL_GetTouchFingers(SDL_TouchID touchID, int* count);

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_EVENTS_H */
