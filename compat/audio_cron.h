/* Cart-side host-native audio backend for the Cronopio Exult port.
 *
 * Exult's full Audio/MyMidiPlayer/AudioMixer/LowLevelMidiDriver stack is
 * threaded (std::thread + mutex + condvar) and mixes everything to PCM through
 * an SDL audio device — neither fits the single-threaded coroutine VM. So,
 * mirroring the DOOM port (cronopio-doom/src/i_sound_cron.c) and the
 * fork-patch-policy (all adaptation in compat/, zero fork patches), we REPLACE
 * the platform/output layer and REUSE only Exult's pure, threadless XMI engine:
 *
 *   XMidiFile (parse + MT32->GM convert) -> XMidiEventList -> XMidiSequence
 *
 * A small CronoMidiDriver (an XMidiSequenceHandler, NOT a LowLevelMidiDriver)
 * drives one XMidiSequence and forwards every sequenced event to the host MIDI
 * synth via cron_midi_send (+ the host SoundFont). It is pumped once per frame
 * from engine_tick (cron_audio_pump), exactly like DOOM's I_Cron_UpdateMusic —
 * no thread, no mixer, no SDL audio.
 *
 * The Audio::* methods (compat/gamewin_stubs.cc) forward here. See the roadmap
 * "(e) host-native AUDIO" + memory audio-music-architecture / exult-sdl3-shim.
 */
#ifndef CRONOPIO_AUDIO_CRON_H
#define CRONOPIO_AUDIO_CRON_H

#include <string>

namespace cron_audio {

// Music selection (mirrors MyMidiPlayer::ForceType: None/Midi/Ogg). For now only
// the MIDI path is wired; Ogg (cron_ogg_play) lands in the digital-music slice.
enum class Force { None = 0, Midi = 1, Ogg = 2 };

// Initialise the backend (select the host SoundFont, reset the synth). Idempotent.
void init();

// Start music track `num` from flex `flex` (e.g. MAINMUS = "<STATIC>/mt32mus.dat").
// `repeat` loops the track. With digital music selected (use_ogg) and an Ogg track
// available (<MUSIC>/%02dbg.ogg), streams it via cron_ogg_play; otherwise (or on
// failure) sequences the XMI via cron_midi_send. Returns true if anything started.
bool start_music(int num, bool repeat, Force force, const std::string& flex);

// MIDI vs digital-Ogg selection. Both paths are wired; this picks which start_music
// uses. Default = Ogg (the audio pack is baked). Driven by Exult's Audio Options
// gump (via MyMidiPlayer::set_midi_driver's use_oggs flag) — menu-only, no pad combo.
// Changing it restarts the current track on the other path so the switch is heard.
void set_use_ogg(bool on);
bool use_ogg();

// Stop the current track and silence held notes.
void stop_music();

// Advance the active sequence: dispatch every event due since the last call.
// Call once per host frame from engine_tick.
void pump();

// 0..100 (Exult's scale) -> host synth master volume.
void set_music_volume(int vol0to100);

bool music_playing();
int  current_track();

}    // namespace cron_audio

#endif    // CRONOPIO_AUDIO_CRON_H
