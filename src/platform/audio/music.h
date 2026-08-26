#ifndef MCPSP_PLATFORM_AUDIO_MUSIC_H
#define MCPSP_PLATFORM_AUDIO_MUSIC_H

// Call once after soundInit(). Starts the BGM thread (idle until a track
// is queued) and scans the music folder for available tracks.
// No-op safe if the music folder is empty or missing.
void musicInit(void);

// Immediately start streaming a specific track by path (relative to the
// assets root, resolved via assetPath()).
// Raw headerless PCM: 16-bit signed, mono or stereo, matching musicSetFormat().
void musicPlay(const char* path, bool loop);

// Stop playback and go silent.
void musicStop(void);

// 0..1 master music volume, independent of SFX volume.
void musicSetVolume(float volume);

// Configure the format of tracks being streamed. Call before musicPlay()
// if your tracks differ from the default (44100 Hz, stereo -- the PSP's
// audio hardware always outputs at 44100 Hz regardless of file content,
// so tracks not authored at that rate will play back pitch-shifted).
void musicSetFormat(unsigned int sampleRate, int channels);

// Call once per frame/tick from the main loop with whether the main menu
// (title screen) and gameplay are currently active. Drives:
//  - Main menu: loops menu.raw for as long as inMainMenu is true.
//  - Gameplay: on first entering gameplay, waits 60s then plays danny.raw
//    once. After it (or any subsequent track) finishes, waits a random
//    60-180s, then plays another discovered .raw track, repeating indefinitely.
//    menu.raw is reserved for the title screen; danny.raw may appear again
//    later in the gameplay rotation. Extension matching is case-insensitive.
// Passing inMainMenu=false and inGameplay=false (e.g. a sub-menu screen)
// leaves whatever's currently playing alone.
void musicUpdate(bool inMainMenu, bool inGameplay);

#endif
