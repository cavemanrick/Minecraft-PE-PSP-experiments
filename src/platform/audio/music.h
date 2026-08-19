#ifndef MCPSP_PLATFORM_AUDIO_MUSIC_H
#define MCPSP_PLATFORM_AUDIO_MUSIC_H

#include "world/level/levelgen/biome.h"

// Call once after soundInit(). Starts the BGM thread (idle until a track
// is queued). No-op safe if no music files are ever found on disk.
void musicInit(void);

// Immediately start streaming/looping a specific track by file path
// (relative to the assets root, resolved via assetPath()).
// Raw headerless PCM: 16-bit signed, mono or stereo, matching musicSetFormat().
void musicPlay(const char* path, bool loop);

// Stop playback and go silent (fades out over a few blocks to avoid a click).
void musicStop(void);

// 0..1 master music volume, independent of SFX volume.
void musicSetVolume(float volume);

// Configure the format of tracks being streamed. Call before musicPlay()
// if your tracks differ from the default (22050 Hz, stereo).
void musicSetFormat(unsigned int sampleRate, int channels);

// Call once per frame/tick from the main loop. Drives biome-aware track
// selection: when the player's current biome changes, or the current
// track finishes (non-looping), picks and starts the next track from
// that biome's weighted pool. Safe no-op if no pools are registered.
void musicUpdate(BiomeId currentBiome);

// Register a track into a biome's weighted pool. Call during init, before
// the first musicUpdate(). weight follows the same convention as vanilla's
// per-track weight (e.g. Nether: "Chrysopoeia" 7, others 1).
void musicRegisterTrack(BiomeId biome, const char* path, int weight);

#endif
