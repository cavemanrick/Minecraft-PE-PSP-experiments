#include "platform/audio/music.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

#include "platform/path.h"
#include "platform/time.h"

// A second, independent PCM stream from disk, decoupled from playback via
// a small ring buffer (see below) so disk read latency can't stall audio
// output. Kept modest on purpose: RING_BLOCKS * ~4KB/block keeps total RAM
// cost for music streaming in the ~25-30KB range regardless of track length.
#define MUSIC_SAMPLE_COUNT 1024   // stereo frames per block

#define MUSIC_DIR          "data/music"
#define MENU_TRACK         "data/music/menu.raw"
#define FIRST_TRACK        "data/music/danny.raw"

#define MAX_ROTATION_TRACKS 32
#define PATH_LEN             64

#define GAMEPLAY_FIRST_WAIT_SEC   60.0f
#define ROTATION_WAIT_MIN_SEC     60.0f
#define ROTATION_WAIT_MAX_SEC    180.0f

// ---- streaming/mixer state (mixer thread owns g_file/g_playing) ---------

static FILE*        g_file        = NULL;
static FILE*        g_pendingFile = NULL; // set by musicPlay(), consumed by mixer thread
static bool         g_pendingLoop = false;
static volatile int g_pendingSwap = 0;    // 1 = mixer thread should swap in g_pendingFile
static bool         g_loop        = false;
static bool         g_playing     = false;
static float        g_volume      = 1.0f;
static unsigned int g_rate        = 44100;
static int          g_channels    = 2;     // 1 = mono source, 2 = stereo source
static int          g_channel     = -1;    // PSP hw channel, separate from SFX's

// ---- ring buffer: decouples disk I/O from audio output --------------------
//
// The previous design did fread() directly inside the thread that calls
// sceAudioOutputPannedBlocking(), once per block (~23ms budget at 44100Hz).
// Memory stick read latency is not that consistent, so any slow read
// delayed the next block reaching the hardware -- audible as popping at
// the block boundary. This ring buffer separates the two concerns: a
// reader thread continuously fills blocks from disk ahead of playback,
// and the output thread only ever plays a block that's already sitting
// in RAM, never blocking on disk itself.
#define RING_BLOCKS 6   // ~6 blocks of lookahead (~140ms at 44100Hz/1024 samples)

struct RingSlot {
    short samples[MUSIC_SAMPLE_COUNT * 2];
};

static RingSlot      g_ring[RING_BLOCKS];
static volatile int  g_ringReadIdx  = 0; // next slot the output thread will play
static volatile int  g_ringWriteIdx = 0; // next slot the reader thread will fill
static volatile int  g_ringFilled   = 0; // number of slots currently holding unplayed audio

static void closeTrack(void) {
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_playing  = false;
}

// Reads and mixes one stereo 16-bit block from the current stream into
// the given slot. Runs on the reader thread. Track swaps requested by
// musicPlay() (main thread) are applied here, between blocks, so the
// reader thread never has a file closed out from under it mid-fread.
static void musicFillBlock(short* out) {
    if (g_pendingSwap) {
        closeTrack();
        g_file    = g_pendingFile;
        g_loop    = g_pendingLoop;
        g_playing = (g_file != NULL);
        g_pendingFile = NULL;
        g_pendingSwap = 0;

        // Discard any blocks from the previous track still sitting in the
        // ring, so the new track (or silence, for musicStop()) starts
        // right away instead of after however many stale blocks remain.
        g_ringWriteIdx = g_ringReadIdx;
        g_ringFilled   = 0;
    }

    if (!g_playing || !g_file) {
        memset(out, 0, MUSIC_SAMPLE_COUNT * 2 * sizeof(short));
        return;
    }

    static short raw[MUSIC_SAMPLE_COUNT * 2];
    size_t wantBytes = (size_t)MUSIC_SAMPLE_COUNT * g_channels * sizeof(short);
    size_t got        = fread(raw, 1, wantBytes, g_file);

    if (got < wantBytes) {
        if (g_loop && got == 0) {
            // Clean EOF: rewind and refill this block from the start.
            fseek(g_file, 0, SEEK_SET);
            got = fread(raw, 1, wantBytes, g_file);
        } else if (g_loop && got > 0) {
            // Partial block at EOF: fill remainder from the start so the
            // loop point doesn't leave a silent gap.
            fseek(g_file, 0, SEEK_SET);
            size_t remain = wantBytes - got;
            size_t more   = fread((char*)raw + got, 1, remain, g_file);
            got += more;
        } else {
            // Non-looping track finished.
            memset((char*)raw + got, 0, wantBytes - got);
            closeTrack();
        }
    }

    int vol256 = (int)(g_volume * 256.0f);
    if (vol256 > 256) vol256 = 256;
    if (vol256 < 0)   vol256 = 0;

    if (g_channels == 2) {
        for (int i = 0; i < MUSIC_SAMPLE_COUNT; i++) {
            out[i * 2]     = (short)((raw[i * 2]     * vol256) >> 8);
            out[i * 2 + 1] = (short)((raw[i * 2 + 1] * vol256) >> 8);
        }
    } else {
        for (int i = 0; i < MUSIC_SAMPLE_COUNT; i++) {
            short s = (short)((raw[i] * vol256) >> 8);
            out[i * 2] = out[i * 2 + 1] = s;
        }
    }
}

// Reader thread: keeps the ring topped up, sleeping briefly when full so
// it doesn't spin the CPU. This is the only thread that touches disk.
static int musicReaderThread(SceSize, void*) {
    for (;;) {
        while (g_ringFilled >= RING_BLOCKS) {
            sceKernelDelayThread(3000); // 3ms; ring is full, nothing to do yet
        }
        musicFillBlock(g_ring[g_ringWriteIdx].samples);
        g_ringWriteIdx = (g_ringWriteIdx + 1) % RING_BLOCKS;
        g_ringFilled++;
    }
    return 0;
}

// Output thread: only ever plays blocks already sitting in the ring, so
// sceAudioOutputPannedBlocking() is never delayed by disk I/O. If the
// ring underruns (reader fell behind), plays silence for that block
// rather than blocking here to wait.
static int musicOutputThread(SceSize, void*) {
    static short silence[MUSIC_SAMPLE_COUNT * 2];
    memset(silence, 0, sizeof(silence));

    for (;;) {
        int vol = PSP_AUDIO_VOLUME_MAX; // per-sample volume already applied in musicFillBlock
        if (g_ringFilled > 0) {
            sceAudioOutputPannedBlocking(g_channel, vol, vol,
                                          g_ring[g_ringReadIdx].samples);
            g_ringReadIdx = (g_ringReadIdx + 1) % RING_BLOCKS;
            g_ringFilled--;
        } else {
            // Underrun: reader hasn't kept up. Play silence for one block
            // instead of stalling the output thread on a blocking read.
            sceAudioOutputPannedBlocking(g_channel, vol, vol, silence);
        }
    }
    return 0;
}

void musicSetFormat(unsigned int sampleRate, int channels) {
    g_rate     = sampleRate;
    g_channels = (channels == 1) ? 1 : 2;
}

// ---- track discovery ------------------------------------------------------

// Rotation pool: every *.raw file directly in data/music/, EXCLUDING
// menu.raw and danny.raw (those are special-cased, not part of the
// random rotation).
static char g_rotationTracks[MAX_ROTATION_TRACKS][PATH_LEN];
static int  g_rotationCount = 0;

static bool isSpecialTrack(const char* filename) {
    return strcmp(filename, "menu.raw") == 0 || strcmp(filename, "danny.raw") == 0;
}

static void scanMusicFolder(void) {
    g_rotationCount = 0;

    DIR* d = opendir(assetPath(MUSIC_DIR));
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL && g_rotationCount < MAX_ROTATION_TRACKS) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5) continue; // need at least "X.raw"
        if (strcmp(name + len - 4, ".raw") != 0) continue;
        if (isSpecialTrack(name)) continue;

        snprintf(g_rotationTracks[g_rotationCount], PATH_LEN,
                 "%s/%s", MUSIC_DIR, name);
        g_rotationCount++;
    }
    closedir(d);
}

static const char* pickRotationTrack(void) {
    if (g_rotationCount == 0) return NULL;
    int idx = rand() % g_rotationCount;
    return g_rotationTracks[idx];
}

// ---- playback state machine -----------------------------------------------

enum GameplayPhase {
    PHASE_IDLE,           // not in gameplay
    PHASE_WAIT_FIRST,     // waiting the initial 60s before danny.raw
    PHASE_PLAYING_FIRST,  // danny.raw is playing
    PHASE_WAIT_ROTATION,  // waiting a random 60-180s before next rotation track
    PHASE_PLAYING_ROTATION
};

static GameplayPhase g_phase        = PHASE_IDLE;
static float         g_phaseUntil   = 0.0f; // nowSeconds() timestamp when wait ends
static bool          g_menuPlaying  = false;

static float randRange(float lo, float hi) {
    float t = (float)rand() / (float)RAND_MAX;
    return lo + t * (hi - lo);
}

void musicInit(void) {
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, MUSIC_SAMPLE_COUNT,
                                   PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return;

    // Reader thread: same priority as the SFX mixer thread's disk-adjacent
    // work would be, but it's fine to run slightly lower priority than
    // the output thread since it's allowed to fall behind briefly (the
    // ring absorbs that) -- it just must never be starved indefinitely.
    int readerThid = sceKernelCreateThread("music_reader_thread", musicReaderThread,
                                           0x14, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (readerThid < 0) { g_channel = -1; return; }

    // Output thread: higher priority (lower number) than the reader, so
    // it always gets CPU time to hand the next ready block to the
    // hardware right on schedule.
    int outputThid = sceKernelCreateThread("music_output_thread", musicOutputThread,
                                           0x13, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (outputThid < 0) { g_channel = -1; return; }

    sceKernelStartThread(readerThid, 0, 0);
    sceKernelStartThread(outputThid, 0, 0);

    scanMusicFolder();
}

void musicPlay(const char* path, bool loop) {
    if (g_channel < 0 || !path || !path[0]) return;

    FILE* f = fopen(assetPath(path), "rb");
    if (!f) f = fopen(path, "rb");
    if (!f) return; // missing file: leave current playback (or silence) untouched

    // Stage the swap; the mixer thread applies it at the top of its next
    // block instead of us touching g_file directly from the main thread.
    if (g_pendingFile) fclose(g_pendingFile); // a swap was queued but never consumed
    g_pendingFile = f;
    g_pendingLoop = loop;
    g_pendingSwap = 1;
}

void musicStop(void) {
    if (g_pendingFile) { fclose(g_pendingFile); g_pendingFile = NULL; }
    g_pendingFile = NULL;
    g_pendingLoop = false;
    g_pendingSwap = 1; // swaps in a NULL file -> silence, applied on mixer thread
}

void musicSetVolume(float volume) {
    g_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

void musicUpdate(bool inMainMenu, bool inGameplay) {
    if (g_channel < 0) return;

    // --- Main menu music -----------------------------------------------
    if (inMainMenu) {
        if (!g_menuPlaying) {
            musicPlay(MENU_TRACK, true); // loop for as long as we're on the title screen
            g_menuPlaying = true;
        }
        g_phase = PHASE_IDLE; // gameplay sequence resets; re-enters at WAIT_FIRST next time
        return;
    }
    if (g_menuPlaying) {
        // Just left the title screen: stop the menu loop.
        musicStop();
        g_menuPlaying = false;
    }

    // --- Gameplay music sequence ----------------------------------------
    if (!inGameplay) {
        // Neither menu nor gameplay (e.g. a sub-menu screen): leave
        // whatever's playing alone, and don't advance the phase clock.
        return;
    }

    float now = nowSeconds();

    switch (g_phase) {
        case PHASE_IDLE:
            g_phaseUntil = now + GAMEPLAY_FIRST_WAIT_SEC;
            g_phase      = PHASE_WAIT_FIRST;
            break;

        case PHASE_WAIT_FIRST:
            if (now >= g_phaseUntil) {
                musicPlay(FIRST_TRACK, false);
                g_phase = PHASE_PLAYING_FIRST;
            }
            break;

        case PHASE_PLAYING_FIRST:
            if (!g_playing && !g_pendingSwap) {
                // danny.raw finished (or was missing and no-oped): move to
                // the random wait before rotation, same as any other track.
                g_phaseUntil = now + randRange(ROTATION_WAIT_MIN_SEC, ROTATION_WAIT_MAX_SEC);
                g_phase      = PHASE_WAIT_ROTATION;
            }
            break;

        case PHASE_WAIT_ROTATION:
            if (now >= g_phaseUntil) {
                const char* track = pickRotationTrack();
                if (track) {
                    musicPlay(track, false);
                    g_phase = PHASE_PLAYING_ROTATION;
                } else {
                    // No rotation tracks on disk: keep waiting and retry
                    // periodically rather than spinning every tick.
                    g_phaseUntil = now + 5.0f;
                }
            }
            break;

        case PHASE_PLAYING_ROTATION:
            if (!g_playing && !g_pendingSwap) {
                g_phaseUntil = now + randRange(ROTATION_WAIT_MIN_SEC, ROTATION_WAIT_MAX_SEC);
                g_phase      = PHASE_WAIT_ROTATION;
            }
            break;
    }
}
