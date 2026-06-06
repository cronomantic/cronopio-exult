/* gamewin_probe — Phase-1 frame-loop bring-up.
 *
 * Constructs the FULL Exult engine core directly: a Game_window + the BG_Game
 * singleton, bypassing exult_main / GameManager / ExultMenu and the exult.flx
 * CRC/game-detect scan. This is the first time the whole engine core (gamewin/
 * gamerend/gamemap/usecode/objs/gumps/shapes/...) links and runs on the VM.
 *
 * Sequence mirrors exult.cc Init() (the EXULT_MENU_GAME bring-up at exult.cc:2833):
 *   exult_files_init()                    -> ROM-FS + <STATIC>/<DATA>/... tokens
 *   config = new Configuration            -> defaults only (no exult.cfg on disk)
 *   gwin = new Game_window(320,240,...)   -> LIGHT: allocates the subsystems
 *   Game::create_game(&bg)                -> sets the BG_Game singleton + paths
 *   gwin->init_files(false)               -> Usecode_machine + shape_man->load()
 *
 * shape_man->load() reads <STATIC>/{faces,gumps,sprites,paperdol,...}.vga +
 * <DATA>/exult.flx + <DATA>/exult_bg.flx + fonts through Exult's U7open_in -> the
 * baked ROM (build_exult.sh bakes both <STATIC> and the exult*.flx into one ROM).
 */
#include <cronopio.h>

#include "files_cron.h"

#include "Configuration.h"
#include "fnames.h"        /* CFG_BG_NAME, ENDSHAPE_FLX, PATCH_ENDSHAPE */
#include "game.h"          /* Game, BaseGameInfo via modmgr */
#include "gamewin.h"
#include "modmgr.h"        /* BaseGameInfo */
#include "exult_constants.h"
#include "iwin8.h"         /* Image_window8 / Image_buffer8 (present) */
#include "vid_cron.h"      /* vid_present -> CRON_FB */
#include "tqueue.h"        /* Time_queue::activate (per-frame world advance) */
#include "gumps/Gump_manager.h"  /* Gump_manager::update_gumps / find_gump / do_modal_gump */
#include "gumps/Gump.h"          /* Gump::on_button (gump-button hit test) */
#include "gumps/Gump_button.h"   /* Gump_button::is_checkmark */
#include "mouse.h"         /* Mouse (engine cursor) + *_speed_factor statics */
#include "keys.h"          /* KeyBinder (native action dispatch) */
#include "Audio.h"         /* Audio::Init — host-native audio bring-up */
#include "audio_cron.h"    /* cron_audio::pump — per-frame MIDI sequencer */
#include "shapevga.h"      /* Shape_manager::paint_text — draw the OSK glyphs */
#include "items.h"         /* Game::setup_text strings — get_text_msg / counts */
#include "singles.h"       /* Game_singletons::sman (reached via a derived helper) */
#include "SDL3/SDL.h"      /* synthetic input events + SDL_GetTicks (shimmed) */
#include <coro.h>          /* cron_coro_* — run the loop on a coroutine so the
                            * engine's BLOCKING code (do_modal_gump, fades, …)
                            * can yield to the host instead of hanging the cart */

extern KeyBinder* keybinder;   /* created + LoadDefaults() in Game_window::init_files */

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

extern Configuration* config;

/* Kept across setup()->frame() so frame() can present the composited buffer. */
static Game_window* g_gwin = nullptr;

/* --- Engine coroutine (see engine_loop / frame). -----------------------------
 * The Cronopio cart model is one frame() per host frame that must return fast,
 * but Exult drives BLOCKING loops (do_modal_gump, the char-naming screen, palette
 * fades) that run their OWN paint+event loop until done — impossible in a single
 * frame() call. So the engine's live loop runs on a cooperative coroutine: frame()
 * resumes it, it runs ONE iteration and yields back (one host frame). The blocking
 * engine loops yield at SDL_Delay (the shim yields the coroutine there too), so
 * they advance one host frame per iteration instead of hanging — Exult's native
 * control flow runs unmodified, no fork patch (fork-patch-policy). [[cronovm-coro-design]] */
static cron_coro_t g_host   = {};     /* host frame()'s context (zero-init "main") */
static cron_coro_t g_engine = {};     /* the engine loop's context                */
static bool        g_engine_ready = false;
static void        engine_loop(void*);    /* fwd decl (defined after run_input) */

static char lbuf[256];
#define LOGF(...)                                                  \
    do {                                                           \
        int n = snprintf(lbuf, sizeof lbuf, __VA_ARGS__);          \
        if (n > 0) cron_log(lbuf, n);                              \
    } while (0)

/* Minimal concrete BaseGameInfo for Black Gate. Bypasses the GameManager scan:
 * we already KNOW the game is BG and where its files are (the baked ROM). found
 * + editing are true so a missing optional file degrades instead of aborting. */
class ProbeBG : public BaseGameInfo {
public:
    ProbeBG()
            : BaseGameInfo(
                      BLACK_GATE, Game_Language::ENGLISH, CFG_BG_NAME, /*mod*/ "",
                      /*path_prefix*/ "BLACKGATE", /*menu*/ "", /*exp*/ false,
                      /*sibeta*/ false, /*found*/ true, /*editing*/ true,
                      /*codepage*/ "ASCII") {}

    bool get_config_file(Configuration*& cfg, std::string& root) override {
        cfg  = config;
        root = "config/disk/game/blackgate";
        return false;
    }
};

void setup(void) {
    LOGF("gamewin_probe: start\n");

    /* Top-level guard. Real Exult runs Game construction inside exult_main's
     * try/catch; this probe bypasses exult_main, so a file_open_exception from a
     * genuinely-missing required file would otherwise escape uncaught (-> HALT).
     * Catch it here to name the offending file and exit cleanly. */
    try {
        int rc = exult_files_init();
        LOGF("exult_files_init rc=%d\n", rc);

        config = new Configuration();    // defaults only; Configuration::value falls back
        /* Default to digital music (the audio pack is baked) so the Audio Options
         * gump's "Digital Music" toggle reads ON and matches cron_audio's default.
         * Switching it there (set_midi_driver -> cron_audio::set_use_ogg) selects
         * MIDI synth vs the recorded Ogg soundtrack — menu-only, no pad combo. */
        config->set("config/audio/midi/use_oggs", "yes", false);

        /* 320x240 with FILL, NOT 320x200: the Cronopio host framebuffer is
         * 320x240 (--region=fb:76800; vid_cron.cc FB_W/FB_H) and vid_present
         * blits the game buffer (get_ib8()) to the FB top-left, so a 320x200
         * game area leaves the bottom 40 FB rows unpainted (an ugly border).
         * The default fillmode (AspectCorrectCentre) CAPS the game area to the
         * aspect-correct 320x200 and centres it — get_ib8() would be 320x200.
         * Image_window::Fill makes the game area the FULL 320x240 with no aspect
         * cap and no centring offset (so screen coords == game coords — the old
         * AspectCorrectCentre y-offset is gone). Exult just shows 40 more rows of
         * world. The SDL3 shim reports a 320x240 window/display to match. */
        Game_window* gwin = new Game_window(
                320, 240, /*fullscreen*/ false, 320, 240, /*scale*/ 1, /*scaler*/ 0,
                Image_window::Fill);
        g_gwin = gwin;
        LOGF("Game_window constructed (win=%p)\n", (void*)gwin->get_win());

        ProbeBG bg;
        Game::create_game(&bg);
        LOGF("create_game done (game_type=%d)\n", (int)Game::get_game_type());

        gwin->init_files(false);    // Usecode + shape_man->load()
        LOGF("init_files done\n");

        /* Host-native audio bring-up (mirrors exult.cc's early Audio::Init()).
         * Creates the Audio singleton + selects the host SoundFont; the in-game
         * ambient-music state machine (run per frame from engine_tick) then drives
         * cron_audio (XMI -> cron_midi_send). See compat/audio_cron + gamewin_stubs. */
        Audio::Init();
        LOGF("Audio::Init done\n");

        /* Load the UI/message string table (exultmsg.txt from <DATA>/exult.flx +
         * the game's item/text names). Exult does this in exult.cc's Init (right
         * after create_game); that TU isn't compiled, so the cart never called it
         * and every get_text_msg() returned "Missing String" (menus, gump labels,
         * item names). Mirrors exult.cc:1076. */
        Game::setup_text();
        LOGF("setup_text done (%d msgs, %d items)\n", get_num_text_msgs(), get_num_item_names());

        /* Phase 2: bring the game STATE up (what exult.cc Init() does after
         * init_files: init_gamedat -> read_gwin -> setup_game). The probe
         * bypasses show_menu/new_game, so unpack the initial state ourselves:
         * init_gamedat(true) restores <STATIC>/initgame.dat into <GAMEDAT>
         * (RAM-FS). Then read_gwin (gamewin.dat) + setup_game (terrain + NPCs +
         * usecode + eggs). Breadcrumb each step to localise the next wall. */
        gwin->init_gamedat(true);
        LOGF("init_gamedat done\n");
        gwin->read_gwin();
        LOGF("read_gwin done\n");
        /* Start POST-intro: skip_intro -> setup_game sets the did_first_scene
         * usecode flag, which makes the avatar visible AND the game-start/intro
         * eggs inert (their usecode plays the intro cutscene + blocks, which the
         * one-frame cart model can't drive). Without this, the start egg's hatch
         * runs the intro usecode and hangs (egg i=1 at the BG start tile). */
        config->set("config/gameplay/skip_intro", "yes", false);
        gwin->setup_game(false);    // terrain + NPCs + usecode + eggs
        LOGF("setup_game done\n");

        /* Engine cursor — REQUIRED before any gump/mouse INTERACTION (not just
         * the cursor-rendering slice): the gump-click path dereferences
         * Mouse::mouse() throughout (Gump_manager::double_clicked,
         * Game_window::start_dragging via Dragging_info, set_speed_cursor). So
         * the Mouse must exist before clicks; its on-screen RENDERING (show/hide
         * around paint) is still a later slice — here it only backs input/hit
         * tests + speed cursor. Mirrors exult.cc Play()'s `Mouse mouse(gwin)`:
         * loads <STATIC>/pointers.shp (case-insensitive ROM find of POINTERS.SHP)
         * and MakeCurrent()s itself so Mouse::mouse() returns it. Heap-allocated
         * to outlive setup() into frame() (never deleted — lives for the run). */
        new Mouse(gwin);
        Mouse::mouse()->set_shape(Mouse::hand);
        /* Hide the host OS cursor now that the engine draws its own U7 pointer
         * (frame() show()/hide()) — mirrors exult.cc Play()'s SDL_HideCursor(),
         * which our SDL shim doesn't honour. cron_cursor is a no-op on headless
         * (no OS cursor) and hides the SDL cursor on the desktop host. */
        cron_cursor(0);
        LOGF("engine cursor ready (Mouse::mouse=%p)\n", (void*)Mouse::mouse());

        gwin->paint();              // composite the world into the 8bpp buffer
        LOGF("paint done\n");

        /* TEMP (audio bring-up verification): kick a known BG background track now
         * so cron_midi_send fires within the first frames (headless confirms the
         * syscalls + no trap; user confirms it is AUDIBLE on the desktop host).
         * The real ambient-music state machine (Background_noise, scheduled +5000ms
         * virtual) also drives this once enough frames elapse. Track 9 is a BG
         * background track (gamewin.cc is_background_track 9..12). STRIP once the
         * ambient path is confirmed. */
        Audio::get_ptr()->start_music(9, true);
        LOGF("TEMP start_music(9) issued\n");

        /* Spin up the engine coroutine (engine_loop): the live loop runs on its
         * own stack and frame() resumes it each host frame. 4 MB stack (held for
         * the whole run) — deep enough for paint + usecode + (later) do_modal_gump.
         * The init runs HERE on the main stack (it doesn't block); only the live
         * loop moves onto the coroutine. */
        const uint32_t CORO_STK = 4u * 1024u * 1024u;
        g_engine.fn       = engine_loop;
        g_engine.arg      = nullptr;
        g_engine.stack_lo = new unsigned char[CORO_STK];
        g_engine.stack_sz = CORO_STK;
        cron_coro_init(&g_engine);
        g_engine_ready    = true;
        LOGF("engine coroutine ready (stack=%p)\n", g_engine.stack_lo);

        /* Success: RETURN (don't cron_exit) so frame() drives the engine loop
         * (which presents the painted world to CRON_FB each host frame). */
        return;
    } catch (const std::exception& e) {
        LOGF("gamewin_probe: CAUGHT std::exception: %s\n", e.what());
        cron_exit(1);
    } catch (...) {
        LOGF("gamewin_probe: CAUGHT unknown exception\n");
        cron_exit(2);
    }

    cron_exit(0);
}

/* --- Input: the DECIDED model (exult-input-model / exult-sdl3-shim). ---------
 * The cart IS our exult.cc, so frame() (below) runs Exult's NATIVE event-driven
 * input by MIRRORING exult.cc Handle_events/Handle_event over PUBLIC engine APIs,
 * fed by the SYNTHETIC SDL events compat/SDL_cron.cc builds from cron_pad/mouse
 * — NOT a bespoke cart shortcut (the prior start_actor handle_input is gone), and
 * NO fork patch (fork-patch-policy). This slice = MOVEMENT: the d-pad arrives as
 * the SDL_Gamepad LEFT stick (-> joy_aim, mirroring Handle_event's
 * GAMEPAD_AXIS_MOTION) and the RIGHT mouse button walks toward the cursor
 * (mirroring Handle_events' RMASK continuation). Action buttons (keybinder),
 * gump clicks and the OSK are the next slices — their events are drained here for
 * now. NOTE: the engine cursor (Mouse::mouse()) isn't created in the probe yet
 * (needs pointers.shp — its own slice), so the cursor POSITION comes from
 * SDL_GetMouseState (the shim backs it), not Mouse::mouse()->get_mousex(). */
static int   joy_aim_x = 0, joy_aim_y = 0;     /* mirrors exult.cc statics */
static float joy_speed_factor = (float)Mouse::medium_speed_factor;
static bool  g_walking = false;                /* issuing start_actor last frame? */

/* Left-button drag/click state — mirrors exult.cc's file-scope dragging/dragged/
 * last_b1_click/left_down_x/y. The gump-click slice: a left press starts a drag
 * (which also detects gump-button presses + gump-item grabs in Dragging_info);
 * release drops it, and a quick same-spot click-pair is a double-click (the U7
 * 'use' gesture, routed through open gumps first by Game_window::double_clicked). */
static bool     s_dragging = false, s_dragged = false;
static int      s_left_down_x = 0, s_left_down_y = 0;
static unsigned s_last_b1_click = 0;
/* Right-press landed on a closable gump -> close it on release (and suppress the
 * RMASK walk meanwhile). Mirrors exult.cc's right_on_gump. */
static bool     s_right_on_gump = false;

/* OSK active flag — recomputed each frame in exult_engine_yield = "a text-input
 * field is being edited inside a modal". The SDL shim reads exult_osk_active() to
 * suppress the pad action-key synthesis while the OSK owns the face buttons. */
static bool g_osk_active = false;
extern "C" int exult_osk_active(void) { return g_osk_active ? 1 : 0; }
/* Text-input flag from the SDL shim: set by SDL_StartTextInput (the save gump via
 * our TouchUI stub), force-clearable (Exult's gump dtor leaves it stale on close). */
extern "C" int  exult_text_input_active(void);
extern "C" void exult_text_input_set(int);

/* Mirror of the cases of exult.cc Handle_event the cart drives so far: KEY_DOWN/UP
 * (keybinder actions — the pad face buttons), GAMEPAD_AXIS_MOTION (the movement
 * stick — the d-pad), and the MOUSE_BUTTON_DOWN/UP/MOTION gump-click + drag path
 * (left = drag/use, right = walk / close-gump). Unhandled events are consumed. */
static void handle_event_cart(Game_window* gwin, SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        /* mirror exult.cc Handle_event: a gump holding keyboard focus gets first
         * refusal, otherwise the native keybinder maps the key to its action
         * (inventory / combat / target / stats / menu — the pad face buttons
         * synthesise these keycodes; see compat/SDL_cron.cc KEYMAP). */
        if (keybinder && !gwin->get_gump_man()->handle_kbd_event(&e)) {
            keybinder->HandleEvent(e);
        }
        return;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        break;     /* the movement stick — handled below */
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        /* Mirror exult.cc Handle_event's MOUSE_BUTTON_DOWN (button 1): begin a
         * drag. Dragging_info(x,y) does the gump hit-test — it find_gump's the
         * click and, if on a gump button, pushes it; if on a gump item, grabs
         * it for drag; else starts dragging the gump/world object. (Right-button
         * walk is handled by the mouse-RMASK continuation in run_input.) */
        int x, y;
        gwin->get_win()->screen_to_game(e.button.x, e.button.y, gwin->get_fastmouse(), x, y);
        if (e.button.button == SDL_BUTTON_LEFT) {
            Gump*        g;
            Gump_button* btn;
            /* Allow the drag (and thus button presses) when the avatar can act,
             * or — even when it can't — on a gump checkmark so a gump can always
             * be closed. (cheat/map-editor paths omitted — not built in the cart.) */
            if (gwin->main_actor_can_act()
                || ((g = gwin->get_gump_man()->find_gump(x, y, false)) != nullptr
                    && (btn = g->on_button(x, y)) != nullptr && btn->is_checkmark())) {
                s_dragging = gwin->start_dragging(x, y);
                s_dragged  = false;
            }
            s_left_down_x = x;
            s_left_down_y = y;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            /* Mirror exult.cc: a right-press on a closable gump ARMS a close
             * (fired on release) and suppresses the RMASK walk; off a gump the
             * mouse-RMASK continuation in run_input walks the avatar. */
            Gump_manager* gm = gwin->get_gump_man();
            if (!s_dragging && gm->can_right_click_close() && gm->gump_mode()
                && gm->find_gump(x, y, false) != nullptr) {
                s_right_on_gump = true;
            }
        }
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        /* Mirror exult.cc MOUSE_BUTTON_UP (button 1): finish the drag (drop_dragged
         * also fires a pushed gump button's action), and treat a quick same-spot
         * click-pair as a double-click -> Game_window::double_clicked (use item /
         * open gump). The cheat/touch-pathfind/show_items branches are omitted. */
        int x, y;
        gwin->get_win()->screen_to_game(e.button.x, e.button.y, gwin->get_fastmouse(), x, y);
        if (e.button.button == SDL_BUTTON_LEFT) {
            const unsigned curtime = (unsigned)SDL_GetTicks();
            if (s_dragging) {
                gwin->drop_dragged(x, y, s_dragged);
                if (Mouse::mouse()) Mouse::mouse()->set_speed_cursor();
            }
            if (curtime - s_last_b1_click < 500
                && s_left_down_x - 1 <= x && x <= s_left_down_x + 1
                && s_left_down_y - 1 <= y && y <= s_left_down_y + 1) {
                s_dragging = s_dragged = false;
                gwin->double_clicked(x, y);
                if (Mouse::mouse()) Mouse::mouse()->set_speed_cursor();
                return;
            }
            if (!s_dragging || !s_dragged) {
                s_last_b1_click = curtime;
            }
            s_dragging = s_dragged = false;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            /* Mirror exult.cc MOUSE_BUTTON_UP (button 3) in gump mode: a right
             * release on the armed gump closes it. (Out of gump mode the right
             * button is a walk, handled by the run_input RMASK continuation.) */
            Gump_manager* gm = gwin->get_gump_man();
            if (gm->gump_mode() && s_right_on_gump) {
                Gump* g = gm->find_gump(x, y, false);
                if (g != nullptr) {
                    gwin->add_dirty(g->get_dirty());
                    gm->close_gump(g);
                }
            }
            s_right_on_gump = false;
        }
        return;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        /* Mirror exult.cc MOUSE_MOTION: keep the engine cursor position in sync
         * and, while the left button is held, feed the drag (gwin->drag returns
         * true once motion passes the threshold -> mark dragged so the release
         * drops the item instead of registering a click). Right-button walk is
         * the run_input RMASK continuation, not here, to avoid a double
         * start_actor per frame. */
        int mx, my;
        gwin->get_win()->screen_to_game(e.motion.x, e.motion.y, gwin->get_fastmouse(), mx, my);
        if (Mouse::mouse()) {
            Mouse::mouse()->move(mx, my);
            if (!s_dragging) Mouse::mouse()->set_speed_cursor();
        }
        if (s_dragging && (e.motion.state & SDL_BUTTON_LMASK)) {
            s_dragged = gwin->drag(mx, my);
        }
        return;
    }
    default:
        return;    /* wheel + later slices */
    }
    if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY) {
        return;
    }
    if (!gwin->get_main_actor() || !gwin->main_actor_can_act_charmed()) {
        return;
    }
    SDL_Gamepad* dev = SDL_GetGamepadFromID(e.gaxis.which);
    if (!dev) {
        return;
    }
    float ax = SDL_GetGamepadAxis(dev, SDL_GAMEPAD_AXIS_LEFTX) / (float)SDL_JOYSTICK_AXIS_MAX;
    float ay = SDL_GetGamepadAxis(dev, SDL_GAMEPAD_AXIS_LEFTY) / (float)SDL_JOYSTICK_AXIS_MAX;
    /* Same deadzone/speed-tier logic as exult.cc, but comparing SQUARED
     * magnitudes so we avoid sqrtf/fabsf — those are extern libm calls the VM
     * module doesn't define (the d-pad is always full-scale anyway). */
    constexpr float dead = 0.25f, medium = 0.60f, fast = 0.90f;
    if (ax * ax <= dead * dead) ax = 0;     /* |ax| <= dead */
    if (ay * ay <= dead * dead) ay = 0;
    const float len2 = ax * ax + ay * ay;
    joy_speed_factor = (float)Mouse::fast_speed_factor;
    if (len2 < medium * medium)    joy_speed_factor = (float)Mouse::slow_speed_factor;
    else if (len2 < fast * fast)   joy_speed_factor = (float)Mouse::medium_speed_factor;
    if (ax == 0 && ay == 0) {
        gwin->stop_actor();
        joy_aim_x = joy_aim_y = 0;
    } else {
        const float aim = 50.f;             /* ax/ay are 0 or +/-1, so exact */
        joy_aim_x = gwin->get_width()  / 2 + (int)(aim * ax);
        joy_aim_y = gwin->get_height() / 2 + (int)(aim * ay);
    }
}

/* Mirror of exult.cc Handle_events' per-frame body: pump the synthetic events,
 * dispatch each, then apply the movement continuation (RMASK mouse-walk +
 * joy_aim). */
static void run_input(Game_window* gwin) {
    SDL_PumpEvents();                        /* build this frame's events */
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        handle_event_cart(gwin, e);
    }

    const bool can_act = gwin->get_main_actor() && gwin->main_actor_can_act_charmed();
    bool       walking = false;
    if (can_act) {
        float  fx = 0, fy = 0;
        Uint32 ms = SDL_GetMouseState(&fx, &fy);
        if ((ms & SDL_BUTTON_RMASK) && !s_right_on_gump) {  /* right-button walk toward the cursor */
            if (!gwin->is_moving() || gwin->get_step_tile_delta() == 1) {
                const int spd = Mouse::mouse() ? Mouse::mouse()->avatar_speed : 125;
                gwin->start_actor((int)fx, (int)fy, spd);
            }
            walking = true;
        }
        if (joy_aim_x != 0 || joy_aim_y != 0) {
            const int speed = 200 * gwin->get_std_delay() / (int)joy_speed_factor;
            gwin->start_actor(joy_aim_x, joy_aim_y, speed);
            walking = true;
        }
    }
    if (!walking && g_walking && gwin->is_moving()) {
        gwin->stop_actor();
    }
    g_walking = walking;
}

/* ONE iteration of the live loop. Mirrors the core of exult.cc Handle_events()
 * (minus lerp): un-draw the cursor, read input, advance the clock, run the time
 * queue (terrain animation, NPC schedules, usecode), update gumps, repaint the
 * dirty regions, cycle the palette, re-draw the cursor, present. The host ms clock
 * (SDL_GetTicks) advances ~16 ms per cart frame, so the world advances in real-ish
 * time. The composited 8bpp buffer is presented to CRON_FB cart-side (no engine
 * show()/present patch — fork-patch-policy). */
static void engine_tick(Game_window* gwin) {
    Mouse* cursor = Mouse::mouse();

    const unsigned int ticks = (unsigned int)SDL_GetTicks();
    Game::set_ticks(ticks);

    /* Mirror exult.cc: HIDE the cursor at the top — restore the pixels it saved
     * last frame so this frame's paint starts clean (first call no-ops via the
     * `onscreen` guard). Position tracks the MOUSE_MOTION events in run_input. */
    if (cursor) {
        cursor->hide();
    }

    run_input(gwin);                         // native event dispatch (pad+mouse)

    gwin->get_tqueue()->activate(ticks);     // advance animation / NPC schedules
    gwin->get_gump_man()->update_gumps();    // auto-repeat for held gump arrows
    if (gwin->is_dirty()) {
        gwin->paint_dirty();                 // repaint only the changed regions
    }

    /* Re-DRAW the engine cursor (U7 pointer) on top of the painted world, mirroring
     * exult.cc's Mouse::show() before the blit. No blit_dirty() — vid_present blits
     * the whole buffer (cursor included) each frame. */
    if (cursor) {
        cursor->show();
    }

    gwin->rotatecolours();                   // water/lava palette cycling

    /* Advance the host-native music sequencer: dispatch every XMI event that has
     * fallen due this frame to the host MIDI synth (cron_midi_send). (Ogg streams in
     * the host; nothing to pump.) Mirrors the DOOM port's I_Cron_UpdateMusic. */
    cron_audio::pump();
}

/* --- On-screen keyboard (OSK) ------------------------------------------------
 * Cronopio is keyboard-free, so text entry (save names, naming, conversation)
 * uses an OSK the cart draws + drives with the pad ([[exult-input-model]]). It is
 * drawn + handled in exult_engine_yield (the per-host-frame chokepoint) while
 * g_osk_active (set by the cart around a text-input modal) — crucially that runs
 * DURING do_modal_gump (where the cart's engine_tick is blocked), so the OSK works
 * inside the modal. d-pad moves the selection; A activates the selected key (types a
 * char as a synthetic SDL_EVENT_TEXT_INPUT, or Shift/Space/Del/OK on the special row);
 * pad shortcuts B = case toggle, L = backspace, R = space, START = enter (KEY_DOWN).
 * Those events go into the shim queue and the modal loop's
 * SDL_PollEvent drains them into the gump's key_down (Translate_keyboard turns a
 * TEXT_INPUT into key_down(SDLK_UNKNOWN, char) -> AddCharacter). */
namespace { struct CartGS : Game_singletons { static Shape_manager* sm() { return sman; } }; }

/* Layout: 4 char rows (case-folded by Shift) + a special row of variable-width
 * keys. Selection is (row 0-4, cell column 0-9); on the special row the column
 * snaps to a key's start cell. */
static const char* OSK_CHARS[4]   = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.,'-"};
static const int    OSK_SP_START[4] = {0, 2, 6, 8};               /* special-key start cell */
static const int    OSK_SP_W[4]     = {2, 4, 2, 2};               /* width in cells         */
static const char*  OSK_SP_LABEL[4] = {"Aa", "Space", "Del", "OK"};
enum { SP_SHIFT = 0, SP_SPACE, SP_BKSP, SP_ENTER };
static int  osk_row = 0, osk_col = 0;
static bool osk_shift = false;     /* true -> letters typed/shown lowercase */

static int osk_sp_at(int col) { return col < 2 ? 0 : col < 6 ? 1 : col < 8 ? 2 : 3; }

static void osk_draw_and_drive(Game_window* gwin) {
    static uint32_t prev = 0;
    const uint32_t  pad  = cron_pad(0);
    const uint32_t  dn   = pad & ~prev;
    prev = pad;

    static char chbuf[2] = {0, 0};
    auto push_text = [](char c) {
        chbuf[0] = c;
        SDL_Event e; std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_TEXT_INPUT; e.text.text = chbuf;
        SDL_PushEvent(&e);
    };
    auto push_key = [](SDL_Keycode k) {
        SDL_Event e; std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.down = true;
        SDL_PushEvent(&e);
    };

    /* --- navigation (d-pad) --- */
    if (dn & CRON_BTN_UP)   osk_row = (osk_row + 4) % 5;
    if (dn & CRON_BTN_DOWN) osk_row = (osk_row + 1) % 5;
    if (osk_row == 4) {     /* special row: move + snap the column to a key start */
        if (dn & CRON_BTN_LEFT)  osk_col = OSK_SP_START[(osk_sp_at(osk_col) + 3) % 4];
        if (dn & CRON_BTN_RIGHT) osk_col = OSK_SP_START[(osk_sp_at(osk_col) + 1) % 4];
        osk_col = OSK_SP_START[osk_sp_at(osk_col)];
    } else {
        if (dn & CRON_BTN_LEFT)  osk_col = (osk_col + 9) % 10;
        if (dn & CRON_BTN_RIGHT) osk_col = (osk_col + 1) % 10;
    }

    /* --- A activates the selected key --- */
    if (dn & CRON_BTN_A) {
        if (osk_row < 4) {
            char c = OSK_CHARS[osk_row][osk_col];
            if (osk_shift && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            push_text(c);
        } else {
            switch (osk_sp_at(osk_col)) {
            case SP_SHIFT: osk_shift = !osk_shift;       break;
            case SP_SPACE: push_text(' ');               break;
            case SP_BKSP:  push_key(SDLK_BACKSPACE);     break;
            case SP_ENTER: push_key(SDLK_RETURN);        break;
            }
        }
    }
    /* --- pad shortcuts: B case, L del, R space, START enter --- */
    if (dn & CRON_BTN_B)     osk_shift = !osk_shift;
    if (dn & CRON_BTN_L)     push_key(SDLK_BACKSPACE);
    if (dn & CRON_BTN_R)     push_text(' ');
    if (dn & CRON_BTN_START) push_key(SDLK_RETURN);

    /* --- draw (font 2 is DARK -> light tan panel 141 + black outline; palette-robust) --- */
    Image_buffer8* ib = gwin->get_win()->get_ib8();
    Shape_manager* sm = CartGS::sm();
    const int cw = 20, ch = 13, cols = 10, ph = 5 * ch, pw = cols * cw;
    const int px = (gwin->get_width() - pw) / 2;
    const int py = gwin->get_height() - ph - 18;
    ib->fill8(141, pw + 4, ph + 4, px - 2, py - 2);
    ib->fill8(0, pw + 4, 1, px - 2, py - 2);           /* frame: top */
    ib->fill8(0, pw + 4, 1, px - 2, py + ph + 1);      /*        bottom */
    ib->fill8(0, 1, ph + 4, px - 2, py - 2);           /*        left */
    ib->fill8(0, 1, ph + 4, px + pw + 1, py - 2);      /*        right */
    auto outline = [&](int gx, int gy, int wc) {
        ib->fill8(0, wc * cw, 1, gx, gy);
        ib->fill8(0, wc * cw, 1, gx, gy + ch - 1);
        ib->fill8(0, 1, ch, gx, gy);
        ib->fill8(0, 1, ch, gx + wc * cw - 1, gy);
    };
    for (int r = 0; r < 4; ++r) {                      /* char rows */
        for (int c = 0; c < cols; ++c) {
            const int gx = px + c * cw, gy = py + r * ch;
            char cc = OSK_CHARS[r][c];
            if (osk_shift && cc >= 'A' && cc <= 'Z') cc = cc - 'A' + 'a';
            const char s[2] = {cc, 0};
            if (sm) sm->paint_text(2, s, gx + 7, gy + 2);
            if (osk_row == r && osk_col == c) outline(gx, gy, 1);
        }
    }
    for (int i = 0; i < 4; ++i) {                      /* special row */
        const int gx = px + OSK_SP_START[i] * cw, gy = py + 4 * ch;
        if (sm) sm->paint_text(2, OSK_SP_LABEL[i], gx + 4, gy + 2);
        if (osk_row == 4 && osk_sp_at(osk_col) == i) outline(gx, gy, OSK_SP_W[i]);
    }
    if (sm) sm->paint_text(2, "A:key B:case L:del R:spc START:ok", px - 2, py + ph + 3);
}

/* Present the engine's 8bpp buffer to CRON_FB, then yield the coroutine to the
 * host (one host frame). Called (a) by engine_loop after each tick, and (b) by the
 * SDL shim's SDL_Delay — so Exult's BLOCKING loops (do_modal_gump, fades, the
 * naming screen) that paint into the buffer but call the INERT gwin->show() still
 * reach the screen AND advance one host frame per Delay() instead of hanging.
 * No-op off the coroutine (status != RUNNING) so setup()-time waits keep using the
 * SDL_GetTicks nudge and don't try to yield a non-running coroutine. */
extern "C" void exult_engine_yield(void) {
    if (!g_engine_ready || g_engine.status != CORO_RUNNING) {
        return;
    }
    if (g_gwin) {
        /* OSK overlay: drawn here (the per-host-frame chokepoint) so it shows AND
         * is driven even while a blocking do_modal_gump owns the loop. It is active
         * only while a text field is being EDITED in a modal — Exult starts text
         * input (TouchUI stub -> SDL_StartTextInput) when an editable slot is picked;
         * the modal_gump_mode() guard scopes it and clears the flag the gump dtor
         * leaves stale once the modal closes. */
        const bool modal = g_gwin->get_gump_man()->modal_gump_mode();
        if (!modal && exult_text_input_active()) {
            exult_text_input_set(0);
        }
        g_osk_active = modal && exult_text_input_active();
        if (g_osk_active) {
            osk_draw_and_drive(g_gwin);
        }
        Image_window8* w  = g_gwin->get_win();
        Image_buffer8* ib = w->get_ib8();
        vid_present(ib->get_bits(), (int)ib->get_width(), (int)ib->get_height(),
                    (int)ib->get_line_width(), w->get_palette());
    }
    cron_coro_yield(&g_engine);              // -> host frame() (one host frame)
}

/* The engine coroutine: run the live loop forever, presenting + yielding once per
 * iteration. Runs on its own stack (set up in setup()). The yield goes through the
 * same exult_engine_yield that Exult's blocking loops reach via SDL_Delay, so the
 * loop body and a nested do_modal_gump both advance one host frame per turn. */
static void engine_loop(void*) {
    for (;;) {
        engine_tick(g_gwin);
        exult_engine_yield();                // present + yield to host
    }
}

/* Host entry each frame: resume the engine coroutine; it runs to its next yield
 * (one engine_tick, or — once SDL_Delay yields — one turn of a blocking loop). */
void frame(void) {
    if (!g_gwin || !g_engine_ready) {
        return;
    }
    cron_coro_swap(&g_host, &g_engine);
}
CRONOPIO_CART_INIT(setup, frame)
