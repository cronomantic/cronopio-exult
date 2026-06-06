/* Cart-side definitions for engine GLOBALS the full Game_window link references
 * but whose owning TUs aren't brought up in the Phase-1 frame-loop slice.
 * Provided in compat/ (NOT patched into the Exult fork — see fork-patch-policy).
 * Distinct from exult_stubs.cc (the render-slice stubs).
 *
 * The translator requires every referenced GLOBAL to have an initializer (an
 * extern-only global is rejected), so the data globals owned by excluded TUs
 * (exult.cc / keys.cc / the audio subsystem / touchui.cc) are defined here as
 * inert nulls/defaults. Methods invoked THROUGH these (e.g. an Audio call) remain
 * undefined function declarations — tolerated by the translator, they only trap
 * if actually called, and the Phase-1 init_files/shape-load path does not call
 * them (audio is host-native in a later slice; input/keybindings too). FUNCTION
 * symbols don't need stubbing here — only globals do. */
#include "exult.h"        /* keybinder, gamemanager, ... + Get_click/make_screenshot/
                             Wait_for_arrival/setup_video (exult.cc, excluded) */
#include "Audio.h"        /* Audio::self / bg2si_songs / bg2si_sfxs + Audio::* methods,
                             MyMidiPlayer (via Midi.h) */
#include "AudioMixer.h"   /* Pentagram::AudioMixer::the_audio_mixer + isPlaying/stopSample */
#include "touchui.h"      /* TouchUI::eventType */
#include "modmgr.h"       /* BaseGameInfo / GameManager / ModManager (zip-free) */
#include "utils.h"        /* clone_system_path / U7mkdir / ... */
#include "keys.h"         /* KeyBinder / ActionFunc / GetExultAction (keys.cc, excluded) */
#include "version.h"      /* getVersionInfo / VersionGetGitRevision (version.cc, excluded) */
#include "playscene.h"    /* play_scene / scene_available free fns (playscene.cc, excluded) */
#include "soundtest.h"    /* SoundTester::test_sound */
#include "MidiDriver.h"   /* MidiDriver driver-enumeration statics */
#include "ucinternal.h"   /* Usecode_internal::get_opcode_length (ucdisasm.cc, excluded) */
#include "msgfile.h"      /* Text_msg_file_reader / set_text_msg_translator / Write_msg_file_section */
#include "servemsg.h"     /* Exult_server::Send_data / Msg_type (server, excluded) */
#include "Flex.h"         /* Flex::is_flex (get_game_identity, modmgr.cc zip-free) */
#include "databuf.h"      /* IDataSource / IFileDataSource */
#include "exceptions.h"   /* file_read_exception */
#include <cstring>        /* strcmp */
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

/* ---- exult.cc globals (main TU, excluded) ------------------------------- */
KeyBinder*         keybinder         = nullptr;   /* input slice */
GameManager*       gamemanager       = nullptr;   /* GameManager scan bypassed */
bool               combat_trace      = false;
quitting_time_enum quitting_time     = QUIT_TIME_NO;
ShortcutBar_gump*  g_shortcutBar     = nullptr;
bool               g_waiting_for_click = false;
TouchUI*           touchui           = nullptr;   /* no touch UI on Cronopio */

/* ---- audio subsystem (host-native in a later slice) --------------------- */
Audio*             Audio::self       = nullptr;
const int*         Audio::bg2si_songs = nullptr;
const int*         Audio::bg2si_sfxs  = nullptr;
Pentagram::AudioMixer* Pentagram::AudioMixer::the_audio_mixer = nullptr;

/* ---- touchui.cc (excluded: needs SDL_PropertiesID) ---------------------- */
uint32 TouchUI::eventType = 0;
/* The three STATIC TouchUI entry points are called directly (behind a null
 * `touchui` check) from the engine; the pure-virtual members need no body. */
void TouchUI::onTextInput(const char*) {}
void TouchUI::startTextInput(SDL_Window*) {}
void TouchUI::setTextInputArea(SDL_Window*, int, int, int, int) {}

/* ---- gamemgr/modmgr.cc (excluded: its InstallModZip pulls files/zip + zlib,
 * a later mod-support slice). The translator rejects a DIRECT call to an
 * undefined function, so the three out-of-line modmgr methods reached from the
 * compiled set are defined here. We bypass the GameManager/ModManager scan by
 * constructing a BaseGameInfo directly (ProbeBG), so find_game/get_mod are never
 * executed (they sit behind `if (gamemanager != nullptr)`, and gamemanager is
 * null) — null returns are correct. setup_game_paths IS reached (via
 * Game::create_game) and is reimplemented faithfully; the mod_title-suffix branch
 * (needs to_uppercase from modmgr.cc) is omitted since a base game has no mod —
 * matches modmgr.cc for the empty-mod_title case. */
void BaseGameInfo::setup_game_paths() {
    clone_system_path("<STATIC>", "<" + path_prefix + "_STATIC>");
    clone_system_path("<MODS>", "<" + path_prefix + "_MODS>");

    const std::string mod_path_tag = path_prefix;    // base game: mod_title empty

    clone_system_path("<GAMEDAT>", "<" + mod_path_tag + "_GAMEDAT>");
    clone_system_path("<SAVEGAME>", "<" + mod_path_tag + "_SAVEGAME>");

    if (is_system_path_defined("<" + mod_path_tag + "_PATCH>")) {
        clone_system_path("<PATCH>", "<" + mod_path_tag + "_PATCH>");
    } else {
        clear_system_path("<PATCH>");
    }
    if (is_system_path_defined("<" + mod_path_tag + "_SOURCE>")) {
        clone_system_path("<SOURCE>", "<" + mod_path_tag + "_SOURCE>");
    } else {
        clear_system_path("<SOURCE>");
    }
    if (type != EXULT_MENU_GAME) {
        U7mkdir("<SAVEGAME>", 0755);
        U7mkdir("<GAMEDAT>", 0755);
    }
}

ModManager* GameManager::find_game(const std::string& /*name*/) { return nullptr; }
ModInfo*    ModManager::get_mod(const std::string& /*name*/, bool /*checkversion*/) { return nullptr; }

/* ======================================================================== *
 * INERT FUNCTION STUBS for direct calls into the EXCLUDED TUs.
 *
 * The translator rejects a DIRECT call to a function with no definition in
 * the module ("extern is not supported"), EVEN if the call is never reached
 * at runtime (it translates every compiled function). So every such symbol
 * needs a definition. These are the audio subsystem + input/keybindings +
 * the exult.cc(main)/version/playscene/server/disasm glue — all deliberately
 * NOT compiled in the Phase-1 frame-loop slice. Defined inert here (no fork
 * patch — see fork-patch-policy).
 *
 * AUDIO is host-native in a LATER slice (user decision 2026-06-04): inert now,
 * paint a frame first. ⚠ A few of these are real engine services that the
 * paint/init path may actually exercise at RUNTIME (msgfile Text_msg_file_reader,
 * get_game_identity, get_opcode_length): inert here only to TRANSLATE — when a
 * runtime CALLR trap shows one is hit, replace that stub with its real compiled
 * TU. (See exult-gamewin-phase1.) Return values: nothing-played / not-playing /
 * empty / failure.
 * ======================================================================== */

/* ---- Audio (audio/Audio.cc) -------------------------------------------- */
void   Audio::Init() {}
void   Audio::Destroy() {}
void   Audio::Init_sfx() {}
void   Audio::cancel_streams() {}
void   Audio::pause_audio() {}
void   Audio::resume_audio() {}
sint32 Audio::copy_and_play_speech(const uint8*, uint32, bool, int) { return -1; }
sint32 Audio::copy_and_play_sfx(const uint8*, uint32, bool, int) { return -1; }
sint32 Audio::playSpeechfile(const char*, const char*, bool, int) { return -1; }
bool   Audio::start_music(int, bool, MyMidiPlayer::ForceType, const std::string&) { return false; }
bool   Audio::start_music(const std::string&, int, bool, MyMidiPlayer::ForceType) { return false; }
void   Audio::stop_music() {}
int    Audio::play_sound_effect(int, int, int, int, int) { return -1; }
int    Audio::play_sound_effect(int, const Game_object*, int, int) { return -1; }
int    Audio::play_sound_effect(int, const Tile_coord&, int, int) { return -1; }
int    Audio::play_sound_effect(const File_spec&, int, int, int, int, int) { return -1; }
int    Audio::update_sound_effect(int, const Game_object*) { return -1; }
int    Audio::update_sound_effect(int, const Tile_coord&) { return -1; }
void   Audio::stop_sound_effect(int) {}
void   Audio::stop_sound_effects() {}
bool   Audio::start_speech(int, bool) { return false; }
void   Audio::stop_speech() {}
bool   Audio::is_speech_playing() { return false; }
bool   Audio::is_sfx_playing(int) { return false; }
bool   Audio::is_track_playing(int) const { return false; }
bool   Audio::is_voice_playing() const { return false; }
void   Audio::set_audio_enabled(bool) {}
void   Audio::set_speech_volume(int, bool) {}
void   Audio::set_sfx_volume(int, bool) {}
int    Audio::wait_for_speech(std::function<int(Uint32)>) { return 0; }
MyMidiPlayer* Audio::get_midi() const { return nullptr; }
bool   Audio::have_roland_sfx(Exult_Game, std::string*) { return false; }
bool   Audio::have_sblaster_sfx(Exult_Game, std::string*) { return false; }
bool   Audio::have_midi_sfx(std::string*) { return false; }
bool   Audio::have_config_sfx(const std::string&, std::string*) { return false; }

/* ---- MyMidiPlayer (audio/midi_drivers) --------------------------------- */
void   MyMidiPlayer::destroyMidiDriver() {}
bool   MyMidiPlayer::start_music(int, bool, ForceType, std::string) { return false; }
void   MyMidiPlayer::stop_music(bool) {}
int    MyMidiPlayer::get_current_track() const { return -1; }
void   MyMidiPlayer::set_repeat(bool) {}
void   MyMidiPlayer::set_timbre_lib(TimbreLibrary) {}
void   MyMidiPlayer::set_midi_driver(const std::string&, bool) {}
void   MyMidiPlayer::SetMidiMusicVolume(int, bool) {}
int    MyMidiPlayer::GetMidiMusicVolume() { return 0; }
void   MyMidiPlayer::SetOggMusicVolume(int, bool) {}
bool   MyMidiPlayer::ogg_is_playing() const { return false; }
bool   MyMidiPlayer::is_mt32() const { return false; }
bool   MyMidiPlayer::init_device(bool) { return false; }

/* ---- MidiDriver static driver enumeration ------------------------------ */
int         MidiDriver::getDriverCount() { return 0; }
std::string MidiDriver::getDriverName(uint32) { return std::string(); }
std::vector<ConfigSetting_widget::Definition>
MidiDriver::get_midi_driver_settings(const std::string&) { return {}; }

/* ---- Pentagram::AudioMixer (audio/Mixer.cc) ---------------------------- */
bool Pentagram::AudioMixer::isPlaying(sint32) const { return false; }
void Pentagram::AudioMixer::stopSample(sint32) {}

/* ---- SoundTester ------------------------------------------------------- */
void SoundTester::test_sound() {}

/* ---- KeyBinder + keyactions.cc are now COMPILED (input slice) ----------- *
 * keys.cc (KeyBinder ctor/LoadDefaults/LoadFromFile/LoadFromPatch/ShowBrowserKeys
 * /GetExultAction) and keyactions.cc (the Action* handlers, incl. ActionFileGump)
 * are in gw.sh's TU set, so their former inert stubs are gone. The `keybinder`
 * global (exult.cc) is still defined above. */

/* Translate_keyboard (exult.cc, excluded): decode an SDL event into the (chr,
 * unicode) pair the engine's key_down handlers expect. REAL here (not the old
 * inert stub) so the OSK's synthetic events deliver characters:
 *  - TEXT_INPUT  -> chr = SDLK_UNKNOWN, unicode = the typed char. Gump key_down
 *    handlers add the char (their `chr != UNKNOWN && TextInputActive` skip — meant
 *    to drop the duplicate physical KEY_DOWN — is bypassed because chr == UNKNOWN).
 *  - KEY_DOWN/UP -> chr = unicode = the keycode (printable keycodes ARE the ASCII
 *    char; specials like BACKSPACE/RETURN are matched on chr). */
bool Translate_keyboard(const SDL_Event& e, SDL_Keycode& chr, SDL_Keycode& unicode, bool) {
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        chr     = SDLK_UNKNOWN;
        unicode = e.text.text ? (SDL_Keycode)(unsigned char)e.text.text[0] : 0;
        return unicode != 0;
    }
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        chr     = e.key.key;
        unicode = e.key.key;
        return true;
    }
    return false;
}

/* ---- imagewin save_screenshot.cc (excluded) --------------------------- */
bool SaveIMG_RW(SDL_Surface*, SDL_IOStream*, bool, int) { return false; }

/* ---- exult.cc (main TU, excluded) -------------------------------------- */
bool Get_click(int&, int&, Mouse::Mouse_shapes, char*, bool, Paintable*, bool) { return false; }
void make_screenshot(bool) {}
void Wait_for_arrival(Actor*, const Tile_coord&, long) {}
void Wizard_eye(long) {}
void setup_video(bool, int, int, int, int, int, int, int, Image_window::FillMode, int) {}
/* keyactions.cc display-control actions (no-ops on Cronopio: fixed 320x200 8bpp,
 * no scaler/gamma/video reconfig — the present is fixed to CRON_FB). */
void increase_scaleval() {}
void decrease_scaleval() {}
void change_gamma(bool) {}

/* ---- version.cc (excluded — VERSION quoting) --------------------------- */
void             getVersionInfo(std::ostream&) {}
std::string_view VersionGetGitRevision(bool) { return {}; }
std::string      VersionGetGitInfo(bool) { return {}; }   /* keyactions ActionAbout */

/* ---- playscene.cc (flic intro, excluded) ------------------------------- */
bool scene_available(const std::string&) { return false; }
void play_scene(const std::string&) {}

/* ---- gamemgr/modmgr.cc get_game_identity --------------------------------- *
 * REAL now (was an inert "" stub). restore_gamedat()/init_gamedat() call this
 * to read the IDENTITY of <STATIC>/initgame.dat; an empty return makes
 * restore_gamedat take the "Wrong identity? Open anyway?" Yesno_gump modal
 * (which a cart can't drive). modmgr.cc can't be compiled (its InstallModZip
 * uses libzip unconditionally), so this is a faithful copy of modmgr.cc's
 * ZIP-DISABLED path (HAVE_ZIP_SUPPORT undefined): initgame.dat is a FLEX, so the
 * is_flex branch reads its "identity" entry. */
std::string get_game_identity(const char* savename, IDataSource* ds, const std::string& title) {
    std::unique_ptr<char[]> game_identity;
    if (!ds) {
        return title;
    }
    if (!Flex::is_flex(ds)) {
        return title;    // (ZIP path disabled — old/zip saves report the title)
    } else {
        ds->seek(0x54);    // Get to where file count sits.
        const size_t numfiles = ds->read4();
        ds->seek(0x80);    // Get to file info.
        auto finfo = std::make_unique<uint32[]>(2 * numfiles);
        for (size_t i = 0; i < numfiles; i++) {
            finfo[2 * i]     = ds->read4();    // The position, then the length.
            finfo[2 * i + 1] = ds->read4();
        }
        for (size_t i = 0; i < numfiles; i++) {    // Now read each file.
            size_t len = finfo[2 * i + 1];
            if (len <= 13) {
                continue;
            }
            len -= 13;
            ds->seek(finfo[2 * i]);    // Get to it.
            char fname[14] = {0};      // Set up name.
            ds->read(fname, 13);
            if (!strcmp("identity", fname)) {
                game_identity = std::make_unique<char[]>(len + 1);
                ds->read(game_identity.get(), len);
                game_identity[len] = 0;
                break;
            }
        }
    }
    if (!game_identity) {
        return title;
    }
    char* ptr = game_identity.get();    // Truncate identity.
    for (; (*ptr != 0x1a && *ptr != 0x0d); ptr++)
        ;
    *ptr = 0;
    return std::string(game_identity.get());
}

std::string get_game_identity(const char* savename, const std::string& title) {
    if (!U7exists(savename)) {
        return title;
    }
    IFileDataSource ds(savename);
    return get_game_identity(savename, &ds, title);
}

/* ---- msgfile (Text_msg_file_reader / translator) ----------------------- *
 * NOW REAL: the engine parses message files (shape_files / paperdoll_source /
 * shape_info / ...) with Text_msg_file_reader. The inert stubs here made
 * get_section_strings() return nothing -> the paperdoll source table defaulted
 * to <STATIC>/paperdol.vga (which BG has no static copy of) -> file_open_exception.
 * The real files/msgfile.cc is now compiled (gw.sh FILES), so these are gone. */

/* ---- usecode disasm (ucdisasm.cc, excluded) ---------------------------- */
int Usecode_internal::get_opcode_length(int) { return 0; }

/* ---- Exult_server (mapedit/server net, excluded) ----------------------- */
namespace Exult_server {
int Send_data(int, Msg_type, const unsigned char*, int) { return 0; }
}
