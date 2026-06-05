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
#include "SDL3/SDL.h"      /* SDL_GetTicks (host ms clock, shimmed) */

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

/* --- Input: drive the Avatar from the Cronopio mouse + d-pad. ---------------
 * Both controls map to Game_window's PUBLIC start_actor()/stop_actor(), called
 * directly cart-side — no exult.cc, no fork patch (fork-patch-policy):
 *   - MOUSE (U7-native): hold the RIGHT button to walk toward the cursor.
 *     cron_mouse's position is already in window coords (vid_present blits the
 *     framebuffer top-left, unscaled), so it feeds start_actor directly. Mirrors
 *     exult.cc Handle_events' RMASK path (re-issued each frame the button held).
 *   - D-PAD: a held direction aims a point 50px from the window centre (where
 *     the Avatar is drawn). Mirrors the SDL_EVENT_GAMEPAD_AXIS_MOTION handler.
 * The single-tile / direction-change re-issue guard matches Handle_events'
 * `!is_moving() || step_tile_delta==1`. Mouse takes priority when both fire. */
static bool g_walking  = false;            /* currently driving start_actor? */
static int  g_walk_dx  = 0, g_walk_dy = 0; /* last commanded d-pad direction */

static void handle_input(Game_window* gwin) {
    const bool can_act = gwin->get_main_actor() && gwin->main_actor_can_act_charmed();

    int mx = 0, my = 0;
    const uint32_t mb         = cron_mouse(&mx, &my);
    const bool     mouse_walk = (mb & 2u) != 0;   /* bit value 2 = right button */

    const uint32_t pad = cron_pad(0);
    int dx = 0, dy = 0;
    if (pad & CRON_BTN_LEFT)  --dx;
    if (pad & CRON_BTN_RIGHT) ++dx;
    if (pad & CRON_BTN_UP)    --dy;
    if (pad & CRON_BTN_DOWN)  ++dy;

    if (mouse_walk && can_act) {
        if (!gwin->is_moving() || gwin->get_step_tile_delta() == 1) {
            gwin->start_actor(mx, my, 125 /* medium step delay (ms) */);
        }
        g_walking = true;
    } else if ((dx || dy) && can_act) {
        const bool dir_changed = (dx != g_walk_dx || dy != g_walk_dy);
        if (dir_changed || !gwin->is_moving() || gwin->get_step_tile_delta() == 1) {
            const int aim = 50;   /* window-centre-relative aim, matches Exult */
            gwin->start_actor(gwin->get_width()  / 2 + dx * aim,
                              gwin->get_height() / 2 + dy * aim, 125);
        }
        g_walking = true;
    } else if (g_walking) {   /* both released -> stop */
        if (gwin->is_moving()) {
            gwin->stop_actor();
        }
        g_walking = false;
    }
    g_walk_dx = dx;
    g_walk_dy = dy;
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

    handle_input(gwin);                      // d-pad -> Avatar movement

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
