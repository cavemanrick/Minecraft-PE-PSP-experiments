#include "platform/audio/extended_sound_fx.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <cstdio>
#include <cstring>

#include "platform/path.h"

// Same block size as music.cpp's stream; no particular reason to differ,
// and keeping it identical makes the per-block CPU/latency budget already
// proven out for BGM streaming apply here unchanged.
#define SFX_SAMPLE_COUNT 1024   // stereo frames per block
#define RING_BLOCKS      6      // ~140ms lookahead at 44100Hz, matches music.cpp
#define PENDING_PATH_LEN 96

struct RingSlot {
    short samples[SFX_SAMPLE_COUNT * 2];
};

static RingSlot      g_ring[RING_BLOCKS];
static volatile unsigned int g_ringWritten = 0; // reader thread only
static volatile unsigned int g_ringRead    = 0; // output thread only

static FILE*        g_file        = NULL; // reader-owned
static volatile int g_playing     = 0;

// Path is staged by the main thread (extendedSoundFXPlay) and consumed by
// the reader thread between blocks -- same handoff pattern music.cpp uses
// for track swaps, so a play request never yanks a file out from under an
// in-flight fread().
static char           g_pendingPath[PENDING_PATH_LEN];
static volatile int   g_pendingSwap = 0;

static float         g_volume     = 1.0f;
static int           g_channel    = -1;   // own PSP hw channel, separate from SFX/BGM

static unsigned int ringFilled(void) { return g_ringWritten - g_ringRead; }

static void closeTrack(void) {
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_playing = 0;
}

// Runs on the reader thread. Applies a pending play request between
// blocks, same rationale as music.cpp's musicFillBlock().
static void sfxFillBlock(short* out) {
    if (g_pendingSwap) {
        closeTrack();
        FILE* f = fopen(assetPath(g_pendingPath), "rb");
        if (!f) f = fopen(g_pendingPath, "rb");
        g_file    = f;
        g_playing = (g_file != NULL) ? 1 : 0;
        g_pendingSwap = 0;
        // Deliberately not flushing/rewinding the ring indices here, for
        // the same cross-thread-race reason music.cpp documents at its
        // equivalent swap point: g_ringRead belongs to the output thread.
        // Up to RING_BLOCKS (~140ms) of stale silence/previous-play tail
        // may still drain first, which is inaudible for a short stinger.
    }

    if (!g_playing || !g_file) {
        memset(out, 0, SFX_SAMPLE_COUNT * 2 * sizeof(short));
        return;
    }

    size_t wantBytes = (size_t)SFX_SAMPLE_COUNT * 2 * sizeof(short);
    size_t got = fread(out, 1, wantBytes, g_file);
    if (got < wantBytes) {
        // One-shot: no looping. Zero-fill the remainder and stop.
        memset((char*)out + got, 0, wantBytes - got);
        closeTrack();
    }

    int vol256 = (int)(g_volume * 256.0f);
    if (vol256 > 256) vol256 = 256;
    if (vol256 < 0)   vol256 = 0;
    if (vol256 != 256) {
        for (int i = 0; i < SFX_SAMPLE_COUNT * 2; i++) {
            out[i] = (short)((out[i] * vol256) >> 8);
        }
    }
}

static int sfxReaderThread(SceSize, void*) {
    for (;;) {
        while (ringFilled() >= RING_BLOCKS) {
            sceKernelDelayThread(3000); // 3ms; ring full, idle until consumed or replayed
        }
        sfxFillBlock(g_ring[g_ringWritten % RING_BLOCKS].samples);
        g_ringWritten++; // published only after the slot is fully written
    }
    return 0;
}

static int sfxOutputThread(SceSize, void*) {
    static short silence[SFX_SAMPLE_COUNT * 2];
    memset(silence, 0, sizeof(silence));

    for (;;) {
        int vol = PSP_AUDIO_VOLUME_MAX; // per-sample volume already applied in sfxFillBlock
        if (ringFilled() > 0) {
            sceAudioOutputPannedBlocking(g_channel, vol, vol,
                                          g_ring[g_ringRead % RING_BLOCKS].samples);
            g_ringRead++;
        } else {
            sceAudioOutputPannedBlocking(g_channel, vol, vol, silence);
        }
    }
    return 0;
}

void extendedSoundFXInit(void) {
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SFX_SAMPLE_COUNT,
                                   PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return;

    // Priority matches music.cpp's threads (below the main thread, so
    // gameplay is never starved buffering a stinger that isn't even
    // playing most of the time).
    int readerThid = sceKernelCreateThread("extfx_reader_thread", sfxReaderThread,
                                           0x26, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (readerThid < 0) { g_channel = -1; return; }

    int outputThid = sceKernelCreateThread("extfx_output_thread", sfxOutputThread,
                                           0x25, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (outputThid < 0) { g_channel = -1; return; }

    sceKernelStartThread(readerThid, 0, 0);
    sceKernelStartThread(outputThid, 0, 0);
}

void extendedSoundFXPlay(const char* path) {
    if (g_channel < 0 || !path || !path[0]) return;
    strncpy(g_pendingPath, path, PENDING_PATH_LEN - 1);
    g_pendingPath[PENDING_PATH_LEN - 1] = '\0';
    g_pendingSwap = 1; // applied on the reader thread at the next block boundary
}

void extendedSoundFXSetVolume(float volume) {
    g_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}
