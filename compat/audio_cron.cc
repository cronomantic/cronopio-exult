/* Host-native audio backend — music (MIDI) slice. See compat/audio_cron.h. */

#include "audio_cron.h"

#include <cassert>    /* XMidiRecyclable.h uses assert() without including it */

#include "XMidiEventList.h"
#include "XMidiFile.h"
#include "XMidiSequence.h"
#include "XMidiSequenceHandler.h"
#include "databuf.h"    /* IExultDataSource / IFileDataSource / File_spec */
#include "fnames.h"     /* MAINMUS / MAINMUS_AD (digital-music mapping) */
#include "game.h"       /* Game::get_game_type / GAME_SI (game-aware paths: BG/SI/mods) */
#include "utils.h"       /* U7exists — guard before U7open_in (which THROWS on missing) */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

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
// Read a whole ROM file (a U7 token path) into a heap buffer via Exult's file
// layer (IFileDataSource -> U7open_in -> the cart ROM-FS). Returns the size, 0 on
// a missing/empty file. The buffer is the caller's.
static size_t read_rom_file(const char* path, std::unique_ptr<uint8_t[]>& out) {
	if (!U7exists(path)) {
		return 0;    // U7open_in THROWS on a missing file — check first (mod oggs)
	}
	IFileDataSource ds{File_spec(path)};
	if (!ds.good()) {
		return 0;
	}
	const size_t len = ds.getSize();
	if (len == 0) {
		return 0;
	}
	out = std::make_unique<uint8_t[]>(len);
	ds.read(out.get(), len);
	return len;
}

static bool start_ogg(int num, bool repeat, const std::string& flex) {
	if (flex != MAINMUS && flex != MAINMUS_AD) {
		return false;    // intro/endgame use other mappings — MIDI for now
	}
	// Mod override first (<PATCH>/music/NNN.ogg), then the game's digital pack
	// (<MUSIC>/NNbg.ogg for Black Gate, NNsi.ogg for Serpent Isle) — mirrors
	// MyMidiPlayer::ogg_play_track. Game-aware via Game::get_game_type().
	char                       name[40];
	std::unique_ptr<uint8_t[]> buf;
	std::snprintf(name, sizeof(name), "<PATCH>/music/%03d.ogg", num);
	size_t len = read_rom_file(name, buf);
	if (len == 0) {
		std::snprintf(name, sizeof(name), "<MUSIC>/%02d%s.ogg", num, GAME_SI ? "si" : "bg");
		len = read_rom_file(name, buf);
	}
	if (len == 0) {
		return false;
	}
	cron_ogg_play(buf.get(), static_cast<int32_t>(len), repeat ? 1 : 0);
	cron_ogg_volume(g_music_vol * 256 / 100);
	g_ogg_playing = true;
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

//---- SFX --------------------------------------------------------------------
// Digital sound effects via the host SPU voices (cron_sample + cron_pcm): the
// host mixes/pans/resamples up to 16 voices natively (music is on the synth/ogg,
// so all voices are free for SFX — like the DOOM port). WAV member `num` from
// <DATA>/jmsfx.flx is decoded to signed-8-bit mono ONCE and cached; the host
// references that cart buffer by offset while a voice plays, so it must persist.

static constexpr int SFX_SLOT     = 0;    // scratch sample bank (reused per trigger)
static constexpr int SFX_VOICES   = 16;   // CRONOPIO_AUDIO_CHANS
static constexpr int SFX_CHANS    = 15;   // voices 0..14 for SFX (round-robin)
static constexpr int SPEECH_VOICE = 15;   // last voice reserved for dialogue speech

// The digital SFX flex, game-aware: Black Gate = jmsfx.flx, Serpent Isle =
// jmsisfx.flx (both baked under <DATA>). The engine already converts BG->SI sfx
// NUMBERS via Audio::game_sfx, so here we only pick the per-game file.
static const char* sfx_flex_path() {
	return GAME_SI ? "<DATA>/jmsisfx.flx" : "<DATA>/jmsfx.flx";
}

struct SfxPcm {
	std::unique_ptr<int8_t[]> pcm;
	uint32_t                  len  = 0;    // mono samples
	uint32_t                  rate = 0;
};
static std::map<int, SfxPcm> g_sfx_cache;

static int      g_voice_num[SFX_VOICES];   // SFX num on each voice (zero-init OK: a
static uint32_t g_voice_end[SFX_VOICES];   //   voice with end<=now counts as idle)
static int      g_voice_base[SFX_VOICES];  // base volume (pre-distance) for reposition
static int      g_voice_rr = 0;
static int      g_sfx_vol  = 100;          // 0..100 master

// Distance attenuation. `distance` is 0..256 (Audio::get_2d_position_for_tile's
// scale: 0 = at the listener, 256 = MAX_SOUND_FALLOFF away = silent). Linear.
static int sfx_falloff(int base, int distance) {
	if (distance <= 0) {
		return base;
	}
	if (distance >= 256) {
		return 0;
	}
	return base * (256 - distance) / 256;
}

static int clamp_pan(int balance) {
	int pan = balance / 2;    // -256..256 -> -128..128
	if (pan < -128) {
		pan = -128;
	}
	if (pan > 127) {
		pan = 127;
	}
	return pan;
}

static uint32_t rd_le(const uint8_t* p, int n) {
	uint32_t v = 0;
	for (int i = 0; i < n; ++i) {
		v |= (uint32_t)p[i] << (8 * i);
	}
	return v;
}

// RIFF/WAVE blob -> signed-8-bit mono PCM. 16-bit -> high byte; 8-bit (unsigned)
// -> centred to signed; stereo -> averaged. Returns false on a non-WAV layout.
static bool decode_wav(const uint8_t* d, size_t n, SfxPcm& out) {
	if (n < 44 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) {
		return false;
	}
	uint16_t channels = 0, bits = 0;
	uint32_t rate = 0, data_off = 0, data_len = 0;
	size_t   pos = 12;
	while (pos + 8 <= n) {
		const uint8_t* ch = d + pos;
		uint32_t       sz = rd_le(ch + 4, 4);
		if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16 && pos + 8 + 16 <= n) {
			channels = (uint16_t)rd_le(ch + 8 + 2, 2);
			rate     = rd_le(ch + 8 + 4, 4);
			bits     = (uint16_t)rd_le(ch + 8 + 14, 2);
		} else if (std::memcmp(ch, "data", 4) == 0) {
			data_off = (uint32_t)(pos + 8);
			data_len = sz;
			if ((uint64_t)data_off + data_len > n) {
				data_len = (uint32_t)(n - data_off);
			}
			break;
		}
		pos += 8 + sz + (sz & 1);    // chunks are word-aligned
	}
	if (rate == 0 || data_len == 0 || channels == 0 || (bits != 8 && bits != 16)) {
		return false;
	}
	const uint8_t* src   = d + data_off;
	const int      bytes = bits / 8;
	const uint32_t frame = (uint32_t)bytes * channels;
	const uint32_t nf    = data_len / frame;
	out.pcm  = std::make_unique<int8_t[]>(nf ? nf : 1);
	out.len  = nf;
	out.rate = rate;
	for (uint32_t i = 0; i < nf; ++i) {
		int acc = 0;
		for (int c = 0; c < channels; ++c) {
			const uint8_t* s = src + (size_t)i * frame + (size_t)c * bytes;
			if (bits == 16) {
				acc += (int)(int16_t)(s[0] | (s[1] << 8)) >> 8;    // high byte
			} else {
				acc += (int)s[0] - 128;                            // unsigned -> signed
			}
		}
		out.pcm[i] = (int8_t)(acc / channels);
	}
	return true;
}

static const SfxPcm* sfx_load(int num) {
	auto it = g_sfx_cache.find(num);
	if (it != g_sfx_cache.end()) {
		return it->second.len ? &it->second : nullptr;
	}
	SfxPcm& slot = g_sfx_cache[num];    // caches a miss too (don't retry every call)
	IExultDataSource ds(File_spec(sfx_flex_path()), num);
	if (ds.good() && ds.getSize()) {
		size_t n   = ds.getSize();
		auto   buf = std::make_unique<uint8_t[]>(n);
		ds.read(buf.get(), n);
		decode_wav(buf.get(), n, slot);
	}
	return slot.len ? &slot : nullptr;
}

int play_sfx(int num, int vol256, int balance, int repeat, int distance) {
	const SfxPcm* s = sfx_load(num);
	if (!s) {
		return -1;
	}
	int base = vol256 * 255 / 256 * g_sfx_vol / 100;    // 0..256 -> 0..255, x master
	if (base > 255) {
		base = 255;
	}
	if (base <= 0) {
		return -1;                                      // muted
	}
	const int pan = clamp_pan(balance);

	const uint32_t now = (uint32_t)cron_time_ms();
	int            v   = -1;
	for (int i = 0; i < SFX_CHANS; ++i) {     // 0..14 (voice 15 = speech)
		int c = (g_voice_rr + i) % SFX_CHANS;
		if (g_voice_num[c] < 0 || (int32_t)(g_voice_end[c] - now) <= 0) {
			v = c;
			break;
		}
	}
	if (v < 0) {
		v = g_voice_rr % SFX_CHANS;    // all busy: steal the round-robin slot
	}
	g_voice_rr = (v + 1) % SFX_CHANS;

	// Held even when the distance falloff silences it (eff 0) so a looping
	// positional SFX (e.g. a fountain) can be brought up by update_sfx as the
	// listener approaches — mirrors Exult's mixer->set2DPosition.
	cron_sample(SFX_SLOT, s->pcm.get(), (int32_t)s->len, (int32_t)s->rate);
	cron_pcm(v, SFX_SLOT, 0x10000, sfx_falloff(base, distance), pan, repeat ? 1 : 0);

	g_voice_num[v]  = num;
	g_voice_base[v] = base;
	uint32_t dur_ms = s->rate ? (uint32_t)((uint64_t)s->len * 1000 / s->rate) : 0;
	g_voice_end[v]  = repeat ? 0xFFFFFFFFu : now + dur_ms;
	return v;
}

void update_sfx(int channel, int distance, int balance) {
	if (channel < 0 || channel >= SFX_VOICES) {
		return;
	}
	cron_pcm_params(channel, sfx_falloff(g_voice_base[channel], distance), clamp_pan(balance));
}

void stop_sfx(int channel) {
	if (channel < 0 || channel >= SFX_VOICES) {
		return;
	}
	cron_snd_stop(channel);
	g_voice_num[channel] = -1;
	g_voice_end[channel] = 0;
}

void stop_all_sfx() {
	for (int i = 0; i < SFX_VOICES; ++i) {
		stop_sfx(i);
	}
}

bool sfx_playing(int num) {
	const uint32_t now = (uint32_t)cron_time_ms();
	for (int i = 0; i < SFX_CHANS; ++i) {
		if (g_voice_num[i] == num && (int32_t)(g_voice_end[i] - now) > 0) {
			return true;
		}
	}
	return false;
}

//---- Speech (digital voice) -------------------------------------------------

static std::unique_ptr<uint8_t[]> g_speech_buf;     // current line's PCM (alive while playing)
static uint32_t                   g_speech_end = 0;
static int                        g_speech_vol = 100;

// Minimal Creative Voice (VOC) decoder -> 8-bit UNSIGNED mono PCM + sample rate.
// U7 speech is old-style VOC: a 0x1A header then type-1 sound-data blocks (a
// time-constant byte + 8-bit unsigned PCM), type-2 continuations, and occasional
// type-9 (extended) blocks. Other block types (silence/repeat/marker) are skipped.
static bool decode_voc(const uint8_t* d, size_t n, std::unique_ptr<uint8_t[]>& out,
		uint32_t& outlen, uint32_t& rate) {
	if (n < 0x1a || std::memcmp(d, "Creative Voice File\x1a", 20) != 0) {
		return false;
	}
	// The header's data-offset field (0x14) is unreliable in the U7 speech VOCs
	// (it reads e.g. 0x101A, but the first block is at 0x1A). Exult's
	// VocAudioSample hardcodes 0x1A too — so do we.
	size_t pos = 0x1a;
	std::vector<uint8_t> pcm;
	rate = 0;
	while (pos + 4 <= n) {
		const uint8_t type = d[pos];
		if (type == 0) {
			break;    // terminator
		}
		uint32_t       blen = (uint32_t)(d[pos + 1] | (d[pos + 2] << 8) | (d[pos + 3] << 16));
		const uint8_t* body = d + pos + 4;
		if ((uint64_t)pos + 4 + blen > n) {
			blen = (uint32_t)(n - pos - 4);
		}
		if (type == 1 && blen >= 2) {                   // sound data
			if (!rate) {
				rate = 1000000u / (256u - body[0]);
			}
			pcm.insert(pcm.end(), body + 2, body + blen);
		} else if (type == 2) {                         // continuation
			pcm.insert(pcm.end(), body, body + blen);
		} else if (type == 9 && blen >= 12) {           // extended sound data
			if (!rate) {
				rate = (uint32_t)(body[0] | (body[1] << 8) | (body[2] << 16) | (body[3] << 24));
			}
			pcm.insert(pcm.end(), body + 12, body + blen);
		}
		pos += 4 + blen;
	}
	if (pcm.empty() || rate == 0) {
		return false;
	}
	outlen = (uint32_t)pcm.size();
	out    = std::make_unique<uint8_t[]>(outlen);
	std::memcpy(out.get(), pcm.data(), outlen);
	return true;
}

int start_speech(int num, bool /*wait*/) {
	const char* file = GAME_SI ? SISPEECH : U7SPEECH;   // game-aware speech flex
	IExultDataSource ds(File_spec(file), num);
	if (!ds.good() || !ds.getSize()) {
		return -1;
	}
	const size_t vn  = ds.getSize();
	auto         voc = std::make_unique<uint8_t[]>(vn);
	ds.read(voc.get(), vn);

	std::unique_ptr<uint8_t[]> pcm;
	uint32_t                   plen = 0, rate = 0;
	if (!decode_voc(voc.get(), vn, pcm, plen, rate)) {
		return -1;
	}
	cron_snd_stop(SPEECH_VOICE);          // replace any current line
	g_speech_buf = std::move(pcm);        // host references this buffer while playing
	int vol = g_speech_vol * 255 / 100;
	if (vol > 255) {
		vol = 255;
	}
	cron_sample_u8(SFX_SLOT, g_speech_buf.get(), (int32_t)plen, (int32_t)rate);
	cron_pcm(SPEECH_VOICE, SFX_SLOT, 0x10000, vol, 0, 0);
	g_speech_end = (uint32_t)cron_time_ms() + (uint32_t)((uint64_t)plen * 1000 / rate);
	return SPEECH_VOICE;
}

void stop_speech() {
	cron_snd_stop(SPEECH_VOICE);
	g_speech_end = 0;
	g_speech_buf.reset();
}

bool speech_playing() {
	return (int32_t)(g_speech_end - (uint32_t)cron_time_ms()) > 0;
}

int speech_id() {
	return speech_playing() ? SPEECH_VOICE : -1;
}

}    // namespace cron_audio
