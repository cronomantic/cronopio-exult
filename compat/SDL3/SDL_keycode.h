/* Minimal SDL3 <SDL3/SDL_keycode.h> shim for the Cronopio Exult port.
 *
 * Exult's gump/input code (Gump.h key_down, keys.cc, keyactions.cc) is written
 * against SDL3 keycodes. Cronopio has no SDL; this fakes the SDL_Keycode /
 * SDL_Keymod types and the SDLK_* / SDL_KMOD_* constants the engine names.
 * Values follow SDL3's scheme so they stay self-consistent with the synthetic
 * events our input layer will inject ([[exult-sdl3-shim]]): printable keys are
 * their ASCII codepoint; non-printable keys are SDLK_SCANCODE_MASK | <USB-HID
 * scancode> (the standard SDL_Scancode numbers). Only the subset Exult uses. */
#ifndef CRONOPIO_SDL3_SDL_KEYCODE_H
#define CRONOPIO_SDL3_SDL_KEYCODE_H

#include <SDL3/SDL_stdinc.h>   /* Uint16/Uint32 — NOT the full SDL.h (avoids the cycle) */

#ifdef __cplusplus
extern "C" {
#endif

typedef Uint32 SDL_Keycode;
typedef Uint16 SDL_Keymod;

#define SDLK_SCANCODE_MASK (1u << 30)
#define CRON_SK(sc)        (SDLK_SCANCODE_MASK | (Uint32)(sc))   /* scancode -> keycode */

/* printable keys = their ASCII codepoint */
#define SDLK_UNKNOWN   0x00000000u
#define SDLK_RETURN    0x0000000du
#define SDLK_ESCAPE    0x0000001bu
#define SDLK_BACKSPACE 0x00000008u
#define SDLK_TAB       0x00000009u
#define SDLK_SPACE     0x00000020u
#define SDLK_ASTERISK  0x0000002au   /* '*' */
#define SDLK_PLUS      0x0000002bu   /* '+' */
#define SDLK_MINUS     0x0000002du   /* '-' */
#define SDLK_PERIOD    0x0000002eu   /* '.' */
#define SDLK_SLASH     0x0000002fu   /* '/' */
#define SDLK_DELETE    0x0000007fu

#define SDLK_0 0x00000030u
#define SDLK_1 0x00000031u
#define SDLK_2 0x00000032u
#define SDLK_3 0x00000033u
#define SDLK_4 0x00000034u
#define SDLK_5 0x00000035u
#define SDLK_6 0x00000036u
#define SDLK_7 0x00000037u
#define SDLK_8 0x00000038u
#define SDLK_9 0x00000039u

/* SDL3 letter keycodes are the LOWERCASE ascii value (macros are upper-named) */
#define SDLK_A 0x00000061u
#define SDLK_B 0x00000062u
#define SDLK_C 0x00000063u
#define SDLK_D 0x00000064u
#define SDLK_E 0x00000065u
#define SDLK_F 0x00000066u
#define SDLK_G 0x00000067u
#define SDLK_H 0x00000068u
#define SDLK_I 0x00000069u
#define SDLK_J 0x0000006au
#define SDLK_K 0x0000006bu
#define SDLK_L 0x0000006cu
#define SDLK_M 0x0000006du
#define SDLK_N 0x0000006eu
#define SDLK_O 0x0000006fu
#define SDLK_P 0x00000070u
#define SDLK_Q 0x00000071u
#define SDLK_R 0x00000072u
#define SDLK_S 0x00000073u
#define SDLK_T 0x00000074u
#define SDLK_U 0x00000075u
#define SDLK_V 0x00000076u
#define SDLK_W 0x00000077u
#define SDLK_X 0x00000078u
#define SDLK_Y 0x00000079u
#define SDLK_Z 0x0000007au

/* non-printable keys = SCANCODE_MASK | USB-HID scancode (standard SDL numbers) */
#define SDLK_CAPSLOCK      CRON_SK(57)
#define SDLK_F1            CRON_SK(58)
#define SDLK_F2            CRON_SK(59)
#define SDLK_F3            CRON_SK(60)
#define SDLK_F4            CRON_SK(61)
#define SDLK_F5            CRON_SK(62)
#define SDLK_F6            CRON_SK(63)
#define SDLK_F7            CRON_SK(64)
#define SDLK_F8            CRON_SK(65)
#define SDLK_F9            CRON_SK(66)
#define SDLK_F10           CRON_SK(67)
#define SDLK_F11           CRON_SK(68)
#define SDLK_F12           CRON_SK(69)
#define SDLK_F13           CRON_SK(104)
#define SDLK_F14           CRON_SK(105)
#define SDLK_F15           CRON_SK(106)
#define SDLK_SCROLLLOCK    CRON_SK(71)
#define SDLK_PAUSE         CRON_SK(72)
#define SDLK_INSERT        CRON_SK(73)
#define SDLK_HOME          CRON_SK(74)
#define SDLK_PAGEUP        CRON_SK(75)
#define SDLK_END           CRON_SK(77)
#define SDLK_PAGEDOWN      CRON_SK(78)
#define SDLK_RIGHT         CRON_SK(79)
#define SDLK_LEFT          CRON_SK(80)
#define SDLK_DOWN          CRON_SK(81)
#define SDLK_UP            CRON_SK(82)
#define SDLK_NUMLOCKCLEAR  CRON_SK(83)
#define SDLK_KP_DIVIDE     CRON_SK(84)
#define SDLK_KP_MULTIPLY   CRON_SK(85)
#define SDLK_KP_MINUS      CRON_SK(86)
#define SDLK_KP_PLUS       CRON_SK(87)
#define SDLK_KP_ENTER      CRON_SK(88)
#define SDLK_KP_1          CRON_SK(89)
#define SDLK_KP_2          CRON_SK(90)
#define SDLK_KP_3          CRON_SK(91)
#define SDLK_KP_4          CRON_SK(92)
#define SDLK_KP_5          CRON_SK(93)
#define SDLK_KP_6          CRON_SK(94)
#define SDLK_KP_7          CRON_SK(95)
#define SDLK_KP_8          CRON_SK(96)
#define SDLK_KP_9          CRON_SK(97)
#define SDLK_KP_0          CRON_SK(98)
#define SDLK_KP_PERIOD     CRON_SK(99)
#define SDLK_CLEAR         CRON_SK(156)
#define SDLK_LCTRL         CRON_SK(224)
#define SDLK_LSHIFT        CRON_SK(225)
#define SDLK_LALT          CRON_SK(226)
#define SDLK_LGUI          CRON_SK(227)
#define SDLK_RCTRL         CRON_SK(228)
#define SDLK_RSHIFT        CRON_SK(229)
#define SDLK_RALT          CRON_SK(230)
#define SDLK_RGUI          CRON_SK(231)
#define SDLK_MODE          CRON_SK(257)

/* key modifiers (SDL3 SDL_Keymod bit values) */
#define SDL_KMOD_NONE   0x0000u
#define SDL_KMOD_LSHIFT 0x0001u
#define SDL_KMOD_RSHIFT 0x0002u
#define SDL_KMOD_LCTRL  0x0040u
#define SDL_KMOD_RCTRL  0x0080u
#define SDL_KMOD_LALT   0x0100u
#define SDL_KMOD_RALT   0x0200u
#define SDL_KMOD_LGUI   0x0400u
#define SDL_KMOD_RGUI   0x0800u
#define SDL_KMOD_NUM    0x1000u
#define SDL_KMOD_CAPS   0x2000u
#define SDL_KMOD_MODE   0x4000u
#define SDL_KMOD_SHIFT  (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)
#define SDL_KMOD_CTRL   (SDL_KMOD_LCTRL  | SDL_KMOD_RCTRL)
#define SDL_KMOD_ALT    (SDL_KMOD_LALT   | SDL_KMOD_RALT)
#define SDL_KMOD_GUI    (SDL_KMOD_LGUI   | SDL_KMOD_RGUI)

#ifdef __cplusplus
}
#endif

#endif /* CRONOPIO_SDL3_SDL_KEYCODE_H */
