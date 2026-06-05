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
 *   gwin = new Game_window(320,200,...)   -> LIGHT: allocates the subsystems
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
#include "gumps/Gump_manager.h"  /* Gump_manager::update_gumps */
#include "mouse.h"         /* Mouse::*_speed_factor (static), Mouse::mouse() */
#include "SDL3/SDL.h"      /* synthetic input events + SDL_GetTicks (shimmed) */

#include <cstdio>
#include <exception>
#include <string>

extern Configuration* config;

/* Kept across setup()->frame() so frame() can present the composited buffer. */
static Game_window* g_gwin = nullptr;

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

        Game_window* gwin = new Game_window(
                320, 200, /*fullscreen*/ false, 320, 200, /*scale*/ 1, /*scaler*/ 0);
        g_gwin = gwin;
        LOGF("Game_window constructed (win=%p)\n", (void*)gwin->get_win());

        ProbeBG bg;
        Game::create_game(&bg);
        LOGF("create_game done (game_type=%d)\n", (int)Game::get_game_type());

        gwin->init_files(false);    // Usecode + shape_man->load()
        LOGF("init_files done\n");

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

        gwin->paint();              // composite the world into the 8bpp buffer
        LOGF("paint done\n");
        /* Success: RETURN (don't cron_exit) so frame() presents the painted
         * world to CRON_FB each host frame. */
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

/* Mirror of exult.cc Handle_event's GAMEPAD_AXIS_MOTION case (the only event this
 * movement slice acts on; everything else is consumed). */
static void handle_event_cart(Game_window* gwin, const SDL_Event& e) {
    if (e.type != SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        return;    /* mouse / key events: drained now, handled in later slices */
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
        if (ms & SDL_BUTTON_RMASK) {         /* right-button walk toward the cursor */
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

/* Live per-frame game loop. Mirrors the core of exult.cc Handle_events() (minus
 * mouse / lerp, which are later slices): read input, advance the clock, run the time
 * queue (terrain animation, NPC schedules, usecode), update gumps, repaint the
 * dirty regions, and cycle the palette. The host ms clock (SDL_GetTicks) advances
 * ~16 ms per cart frame in headless / real wall-clock on desktop, so the world
 * advances in real-ish time. The composited 8bpp buffer is then presented to
 * CRON_FB cart-side (no engine show()/present patch — see fork-patch-policy). */
void frame(void) {
    if (!g_gwin) {
        return;
    }
    Game_window* gwin = g_gwin;

    const unsigned int ticks = (unsigned int)SDL_GetTicks();
    Game::set_ticks(ticks);

    run_input(gwin);                         // native event dispatch (pad+mouse)

    gwin->get_tqueue()->activate(ticks);     // advance animation / NPC schedules
    gwin->get_gump_man()->update_gumps();    // auto-repeat for held gump arrows
    if (gwin->is_dirty()) {
        gwin->paint_dirty();                 // repaint only the changed regions
    }
    gwin->rotatecolours();                   // water/lava palette cycling

    Image_window8* w  = gwin->get_win();
    Image_buffer8* ib = w->get_ib8();
    vid_present(ib->get_bits(), (int)ib->get_width(), (int)ib->get_height(),
                (int)ib->get_line_width(), w->get_palette());
}
CRONOPIO_CART_INIT(setup, frame)
