#ifndef MCPSP_PLATFORM_AUDIO_EXTENDED_SOUND_FX_H
#define MCPSP_PLATFORM_AUDIO_EXTENDED_SOUND_FX_H

// General-purpose one-shot sound effects, streamed directly from disk on
// their own PSP audio channel -- deliberately NOT loaded into sound.cpp's
// packed-PCM RAM pool (see sound.cpp's loadPack()) and NOT routed through
// music.cpp's BGM channel, so this neither grows the resident sound pack
// nor interrupts whatever track is currently playing. Modeled on
// music.cpp's reader/output ring-buffer split (see its comments for why
// the split exists), just without looping or track rotation, and with a
// single active voice: playing a new file interrupts whatever this
// module was already playing, rather than layering. Intended for
// occasional, deliberate one-off stingers (achievement unlocks and
// similar) rather than frequent overlapping SFX -- sound.cpp's
// MAX_VOICES-based mixer remains the right tool for that.
//
// All files played through here must be headerless 16-bit PCM, 44100Hz
// stereo -- matches the PSP's native output rate, so no
// resampling/pitch-shift.

// Call once after soundInit()/musicInit(). Starts the (idle until
// triggered) streaming threads on a free PSP audio channel. No-op safe if
// no channel is available or a requested file is later missing.
void extendedSoundFXInit(void);

// Fire a one-shot sound from the given path (resolved via assetPath(),
// same convention as musicPlay()). Safe to call from the main thread.
// If a sound is already playing when called again, it is interrupted and
// replaced by the new one -- this module only ever has one active voice.
void extendedSoundFXPlay(const char* path);

// 0..1 volume, independent of SFX and BGM volume. Applies to whatever
// plays next through extendedSoundFXPlay(), not retroactively.
void extendedSoundFXSetVolume(float volume);

#endif
