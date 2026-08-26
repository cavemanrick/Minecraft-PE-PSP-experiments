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
static volatile int g_pendingLoop = 0;
static volatile int g_pendingSwap = 0;    // 1 = mixer thread should swap in g_pendingFile
static volatile int g_loop        = 0;
static volatile int g_playing     = 0;
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

static RingSlot g_ring[RING_BLOCKS];

// Two monotonically increasing counters, each written by EXACTLY ONE
// thread: the reader owns g_ringWritten, the output thread owns
// g_ringRead. Occupancy is the difference. Slot index is counter %
// RING_BLOCKS.
//
// The previous design used a single shared g_ringFilled that the reader
// incremented and the output thread decremented. Neither ++ nor -- is
// atomic on this hardware, so the two read-modify-writes interleave and
// lose updates. The count then drifts away from the ring's true
// occupancy, and once it drifts high enough the reader parks in its
// "ring is full" sleep while the output thread plays whatever stale
// slots the count claims are there -- audible as skipping, repeats and
// eventually silence, with no way back.
//
// Unsigned so the subtraction stays correct across wraparound: at 20
// blocks/second these take about seven years to wrap, but the arithmetic
// being right by construction costs nothing.
static volatile unsigned int g_ringWritten = 0; // reader thread only
static volatile unsigned int g_ringRead    = 0; // output thread only

static unsigned int ringFilled(void) { return g_ringWritten - g_ringRead; }

static void closeTrack(void) {
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_playing  = 0;
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
        g_playing = (g_file != NULL) ? 1 : 0;
        g_pendingFile = NULL;
        g_pendingSwap = 0;

        // NOTE: the ring is deliberately NOT flushed here any more. The
        // old code reset both indices from this thread, but g_ringReadIdx
        // belongs to the output thread, and rewinding the write pointer
        // on top of a slot the output thread was mid-way through handing
        // to the hardware is a data race on the sample buffer itself.
        //
        // The cost of not flushing is that up to RING_BLOCKS of the
        // previous track still play -- about 140ms at 44100Hz. That is a
        // short tail on a track change and on musicStop(), which is a far
        // better trade than a cross-thread race for the sake of a crisper
        // cut.
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
        while (ringFilled() >= RING_BLOCKS) {
            sceKernelDelayThread(3000); // 3ms; ring is full, nothing to do yet
        }
        musicFillBlock(g_ring[g_ringWritten % RING_BLOCKS].samples);
        g_ringWritten++;   // published only after the slot is fully written
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
        if (ringFilled() > 0) {
            sceAudioOutputPannedBlocking(g_channel, vol, vol,
                                          g_ring[g_ringRead % RING_BLOCKS].samples);
            g_ringRead++;  // released only after the slot has been consumed
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

// Rotation pool: every *.raw file directly in data/music/, excluding only
// menu.raw. danny.raw is used as the first gameplay track, then remains in
// the rotation pool so it can be heard again later. Extension matching is
// case-insensitive because Memory Stick filenames may not preserve the
// exact case used by the source build.
static char g_rotationTracks[MAX_ROTATION_TRACKS][PATH_LEN];
static int  g_rotationCount = 0;
static int  g_lastRotation  = -1;

static bool isSpecialTrack(const char* filename) {
    return strcmp(filename, "menu.raw") == 0 || strcmp(filename, "MENU.RAW") == 0;
}

static bool hasRawExtension(const char* filename) {
    size_t len = strlen(filename);
    if (len < 5) return false;
    const char* ext = filename + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'r' || ext[1] == 'R') &&
            (ext[2] == 'a' || ext[2] == 'A') &&
            (ext[3] == 'w' || ext[3] == 'W'));
}

static void scanMusicFolder(void) {
    g_rotationCount = 0;
    g_lastRotation  = -1;

    DIR* d = opendir(assetPath(MUSIC_DIR));
    if (!d) {
        printf("[music] unable to open %s\n", assetPath(MUSIC_DIR));
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL && g_rotationCount < MAX_ROTATION_TRACKS) {
        const char* name = ent->d_name;
        if (!hasRawExtension(name)) continue;
        if (isSpecialTrack(name)) continue;

        snprintf(g_rotationTracks[g_rotationCount], PATH_LEN,
                 "%s/%s", MUSIC_DIR, name);
        printf("[music] track[%d] = %s\n", g_rotationCount,
               g_rotationTracks[g_rotationCount]);
        g_rotationCount++;
    }
    closedir(d);

    // Danny is intentionally added to the rotation if it exists. This is
    // useful on builds with only Danny plus one or two additional tracks,
    // and prevents the playlist from becoming a one-way trip into silence.
    char dannyPath[PATH_LEN];
    snprintf(dannyPath, sizeof(dannyPath), "%s", FIRST_TRACK);
    FILE* danny = fopen(assetPath(dannyPath), "rb");
    if (!danny) danny = fopen(dannyPath, "rb");
    if (danny) {
        fclose(danny);
        if (g_rotationCount < MAX_ROTATION_TRACKS) {
            bool alreadyListed = false;
            for (int i = 0; i < g_rotationCount; ++i) {
                if (strcmp(g_rotationTracks[i], FIRST_TRACK) == 0) {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed) {
                snprintf(g_rotationTracks[g_rotationCount], PATH_LEN,
                         "%s", FIRST_TRACK);
                printf("[music] track[%d] = %s\n", g_rotationCount,
                       g_rotationTracks[g_rotationCount]);
                g_rotationCount++;
            }
        }
    }

    printf("[music] %d gameplay rotation track(s) found in %s\n",
           g_rotationCount, MUSIC_DIR);
}

static const char* nextRotationTrack(void) {
    if (g_rotationCount == 0) return NULL;

    int idx = rand() % g_rotationCount;
    if (g_rotationCount > 1 && idx == g_lastRotation) {
        idx = (idx + 1 + (rand() % (g_rotationCount - 1))) % g_rotationCount;
    }
    g_lastRotation = idx;
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

    // Priority: PSPSDK gives the main thread a default priority of 32
    // (0x20) unless PSP_MAIN_THREAD_PRIORITY overrides it, which this
    // project doesn't set. Lower priority NUMBER wins pre-emption on PSP,
    // so a music thread at 0x13/0x14 -- as this was originally set --
    // runs at *higher* priority than the game loop, meaning it can
    // pre-empt rendering/gameplay logic on every ~23ms block boundary.
    // With the output thread looping tightly on
    // sceAudioOutputPannedBlocking(), that adds up to a measurable frame
    // hit. 0x25/0x26 keep both music threads below the main thread's
    // priority (and below sound_thread's 0x12, which predates this and
    // isn't implicated), so they only run when the main thread isn't
    // ready -- gameplay is not starved for the sake of buffering ahead.
    int readerThid = sceKernelCreateThread("music_reader_thread", musicReaderThread,
                                           0x26, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (readerThid < 0) { g_channel = -1; return; }

    int outputThid = sceKernelCreateThread("music_output_thread", musicOutputThread,
                                           0x25, 0x10000, PSP_THREAD_ATTR_USER, 0);
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
    g_pendingLoop = loop ? 1 : 0;
    g_pendingSwap = 1;
}

void musicStop(void) {
    if (g_pendingFile) { fclose(g_pendingFile); g_pendingFile = NULL; }
    g_pendingFile = NULL;
    g_pendingLoop = 0;
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
            // Re-scan on entering gameplay rather than only at boot, so a
            // track added to data/music/ mid-session is picked up without
            // a restart. Cheap: one readdir per world entry.
            scanMusicFolder();
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
                const char* track = nextRotationTrack();
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
