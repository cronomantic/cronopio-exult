/* Host-native audio backend — music (MIDI) slice. See compat/audio_cron.h. */

#include "audio_cron.h"

#include <cassert>    /* XMidiRecyclable.h uses assert() without including it */

#include "XMidiEventList.h"
#include "XMidiFile.h"
#include "XMidiSequence.h"
#include "XMidiSequenceHandler.h"
#include "databuf.h"    /* IExultDataSource / IFileDataSource / File_spec */
#include "fnames.h"     /* MAINMUS / MAINMUS_AD (digital-music mapping) */

#include <cstdint>
#include <cstdio>
#include <memory>

#include <cronopio.h>    /* cron_midi_* / cron_time_ms / cron_log */

namespace cron_audio {

// SEQ id is cosmetic here (one music sequence). Matches Exult's SEQ_NUM_MUSIC.
static constexpr uint16_t SEQ_MUSIC = 0;

/* The host SoundFont. 0 selects the host's embedded BIOS default
 * (GeneralUser-GS, a GM bank). A U7-tuned / MT-32-emulating SF2 can later be
 * baked into the ROM and loaded with cron_sf2_load — see the roadmap. */
static constexpr int32_t SF_BIOS_DEFAULT = 0;

/* MT32MUS.DAT is scored for the Roland MT-32; the default SoundFont is General
 * MIDI, so remap MT-32 patch/bank assignments to GM. (Exult's MyMidiPlayer uses
 * the same conversion for a GM device — XMIDIFILE_CONVERT_MT32_TO_GM.) */
static constexpr int MUSIC_CONVERSION = XMIDIFILE_CONVERT_MT32_TO_GM;

//---- CronoMidiDriver --------------------------------------------------------
// A threadless XMidiSequenceHandler: it owns one XMidiSequence and forwards its
// sequenced events to the host synth. The XMidi clock runs at 6000 Hz; we drive
// it from the host millisecond clock (cron_time_ms), so music advances with the
// engine's virtual time exactly like the DOOM port.

class CronoMidiDriver : public XMidiSequenceHandler {
public:
	void sequenceSendEvent(uint16 /*seq*/, uint32 message) override {
		// Packed MIDI dword: status=byte0, data1=byte1, data2=byte2.
		cron_midi_send(
				static_cast<int32_t>(message & 0xFF), static_cast<int32_t>((message >> 8) & 0x7F),
				static_cast<int32_t>((message >> 16) & 0x7F));
	}

	void sequenceSendSysEx(uint16 /*seq*/, uint8 /*status*/, const uint8* /*msg*/, uint16 /*length*/) override {
		// The host TinySoundFont synth ignores SysEx (no MT-32 reverb/timbre
		// upload); on a GM bank the MT32->GM conversion already handles patches.
	}

	uint32 getTickCount(uint16 /*seq*/) override {
		// 6000ths of a second from the host clock. 6000/1000 = 6 ticks per ms.
		return static_cast<uint32>(cron_time_ms()) * 6u;
	}

	void handleCallbackTrigger(uint16 /*seq*/, uint8 /*data*/) override {}

	int getGlobalVolume() override {
		return global_volume;
	}

	int global_volume = 100;    // 0..100
};

//---- backend state ----------------------------------------------------------

static CronoMidiDriver g_driver;
static XMidiSequence*  g_seq          = nullptr;    // active MIDI sequence (null if Ogg)
static int             g_track        = -1;
static bool            g_repeat       = false;
static int             g_music_vol    = 100;        // 0..100
static bool            g_inited       = false;

// Digital-Ogg selection + the last start_music request (so a toggle can restart
// the current track on the other path). Default to Ogg — the audio pack is baked.
static bool            g_use_ogg      = true;
static bool            g_ogg_playing  = false;
static int             g_cur_num      = -1;
static bool            g_cur_repeat   = false;
static std::string     g_cur_flex;

static void log(const char* s) {
	cron_log(s, static_cast<int32_t>(__builtin_strlen(s)));
}

void init() {
	if (g_inited) {
		return;
	}
	g_inited = true;
	cron_midi_soundfont(SF_BIOS_DEFAULT);
	cron_midi_reset();
	cron_midi_volume(g_music_vol * 255 / 100);
}

// Try the digital-Ogg path for an in-game (MAINMUS) track. Mirrors
// MyMidiPlayer::ogg_play_track's BG MAINMUS mapping: <MUSIC>/%02dbg.ogg. The host
// decodes (stb_vorbis) and COPIES the bytes (cron_ogg.c), so the temp buffer is
// freed immediately. Returns false (→ caller falls back to MIDI) for non-MAINMUS
// flexes or a missing/unreadable file. The whole ogg is read from the ROM via
// Exult's file layer (IFileDataSource → U7open_in → the cart ROM-FS).
static bool start_ogg(int num, bool repeat, const std::string& flex) {
	if (flex != MAINMUS && flex != MAINMUS_AD) {
		return false;    // intro/endgame use other mappings — MIDI for now
	}
	char name[24];
	std::snprintf(name, sizeof(name), "<MUSIC>/%02dbg.ogg", num);

	IFileDataSource ds{File_spec(name)};
	if (!ds.good()) {
		return false;
	}
	const size_t len = ds.getSize();
	if (len == 0) {
		return false;
	}
	auto buf = std::make_unique<uint8_t[]>(len);
	ds.read(buf.get(), len);
	cron_ogg_play(buf.get(), static_cast<int32_t>(len), repeat ? 1 : 0);
	cron_ogg_volume(g_music_vol * 256 / 100);
	g_ogg_playing = true;

	char dbg[64];
	int  n = std::snprintf(dbg, sizeof(dbg), "[audio] start_ogg %02dbg.ogg size=%u\n", num, (unsigned)len);
	if (n > 0) {
		cron_log(dbg, n);
	}
	return true;
}

void stop_music() {
	if (g_seq) {
		delete g_seq;    // dtor -> evntlist->decrementCounter() -> frees the list
		g_seq = nullptr;
	}
	if (g_ogg_playing) {
		cron_ogg_stop();
		g_ogg_playing = false;
	}
	g_track = -1;
	cron_midi_reset();    // all-notes-off
}

bool start_music(int num, bool repeat, Force force, const std::string& flex) {
	init();

	if (num == -1 || num == 255) {
		stop_music();
		return true;
	}

	g_cur_num    = num;
	g_cur_repeat = repeat;
	g_cur_flex   = flex;

	stop_music();

	// Digital-Ogg first (if selected and not forced to MIDI); fall back to MIDI on
	// a missing track or a non-MAINMUS flex. Mirrors MyMidiPlayer::start_music.
	if (g_use_ogg && force != Force::Midi && start_ogg(num, repeat, flex)) {
		g_track  = num;
		g_repeat = repeat;
		return true;
	}

	// MIDI: mirror open_music_flex — read XMI member `num` from the flex (e.g.
	// <STATIC>/mt32mus.dat). IExultDataSource resolves the U7 path through the
	// cart ROM-FS bridge (files_cron). Single-file form: no <PATCH> music here.
	IExultDataSource ds(File_spec(flex), num);
	if (!ds.good() || !ds.getSize()) {
		log("[audio] start_music: no data\n");
		return false;
	}

	// Parse + convert MT-32 -> GM. The driver name is informational only.
	XMidiFile      midfile(&ds, MUSIC_CONVERSION, "Cronopio");
	XMidiEventList* eventlist = midfile.GetEventList(0);
	if (!eventlist) {
		log("[audio] start_music: no event list\n");
		return false;
	}

	// Take ownership BEFORE midfile's dtor releases its lists (see
	// XMidiEventList::decrementCounter — counter 0 survives, -1 frees).
	eventlist->incrementCounter();
	g_seq    = new XMidiSequence(&g_driver, SEQ_MUSIC, eventlist, repeat, g_music_vol * 255 / 100, -1);
	g_track  = num;
	g_repeat = repeat;

	{
		char buf[64];
		int  n = std::snprintf(buf, sizeof(buf), "[audio] start_music track=%d size=%u\n", num, (unsigned)ds.getSize());
		if (n > 0) {
			cron_log(buf, n);
		}
	}
	return true;
}

void pump() {
	if (!g_seq) {
		return;
	}
	// Dispatch every event that has fallen due. playEvent(): 0 = more events
	// playable now (keep going), <0 = sequence ended, >0 = next event is future.
	for (;;) {
		int r = g_seq->playEvent();
		if (r == 0) {
			continue;
		}
		if (r < 0) {
			// Ended (only happens when not repeating; a repeating sequence
			// rewinds internally and never returns <0).
			stop_music();
		}
		break;
	}
}

void set_music_volume(int vol0to100) {
	if (vol0to100 < 0) {
		vol0to100 = 0;
	}
	if (vol0to100 > 100) {
		vol0to100 = 100;
	}
	g_music_vol = vol0to100;
	cron_midi_volume(g_music_vol * 255 / 100);     // synth: 0..255
	cron_ogg_volume(g_music_vol * 256 / 100);      // ogg: 0..256 (Q8)
	if (g_seq) {
		g_seq->setVolume(g_music_vol * 255 / 100);
	}
}

bool music_playing() {
	return g_seq != nullptr || g_ogg_playing;
}

int current_track() {
	return g_track;
}

//---- MIDI vs digital-Ogg selection ------------------------------------------

// Set by Exult's Audio Options gump ("Digital Music" toggle → MyMidiPlayer::
// set_midi_driver's use_oggs flag). Restart the current track on the chosen path so
// the switch is heard immediately (menu-driven; no pad combo).
void set_use_ogg(bool on) {
	if (on == g_use_ogg) {
		return;
	}
	g_use_ogg = on;
	if (g_cur_num >= 0) {
		start_music(g_cur_num, g_cur_repeat, Force::None, g_cur_flex);
	}
	const char* m = g_use_ogg ? "[audio] music -> DIGITAL (Ogg)\n" : "[audio] music -> MIDI (synth)\n";
	cron_log(m, (int32_t)__builtin_strlen(m));
}

bool use_ogg() {
	return g_use_ogg;
}

}    // namespace cron_audio
