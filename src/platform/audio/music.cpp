#include "platform/audio/music.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/path.h"

// Kept modest on purpose: this is a second, independent PCM stream from
// disk, double buffered so the mixer thread always has one block ready
// while the next is being read. ~4KB/buffer keeps total RAM cost for
// music streaming in the ~8-10KB range regardless of track length.
#define MUSIC_SAMPLE_COUNT 1024   // stereo frames per block
#define MUSIC_NUM_BUFS     2

#define MAX_TRACKS_PER_BIOME 6
#define NUM_BIOMES            (B_JUNGLE + 1)

struct TrackPool {
    const char* paths[MAX_TRACKS_PER_BIOME];
    int         weights[MAX_TRACKS_PER_BIOME];
    int         count;
    int         totalWeight;
};

static TrackPool    g_pools[NUM_BIOMES];
static BiomeId      g_lastBiome        = (BiomeId)-1;
static bool         g_biomeMusicActive = false;

static FILE*        g_file        = NULL;
static FILE*        g_pendingFile = NULL; // set by musicPlay(), consumed by mixer thread
static bool         g_pendingLoop = false;
static volatile int g_pendingSwap = 0;    // 1 = mixer thread should swap in g_pendingFile
static bool         g_loop        = false;
static bool         g_playing     = false;
static float        g_volume      = 1.0f;
static unsigned int g_rate        = 22050;
static int          g_channels    = 2;     // 1 = mono source, 2 = stereo source
static int          g_channel     = -1;    // PSP hw channel, separate from SFX's

static void closeTrack(void) {
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_playing  = false;
}

// Fills one stereo 16-bit block from the current stream, upmixing mono
// sources and looping/stopping as configured. Silence on underrun/EOF-nonloop.
// Runs entirely on the mixer thread; track swaps requested by musicPlay()
// (main thread) are applied here, at a safe point between blocks, so the
// mixer thread never has a file closed out from under it mid-fread.
static void musicFillBlock(short* out) {
    if (g_pendingSwap) {
        closeTrack();
        g_file    = g_pendingFile;
        g_loop    = g_pendingLoop;
        g_playing = (g_file != NULL);
        g_pendingFile = NULL;
        g_pendingSwap = 0;
    }

    if (!g_playing || !g_file) {
        memset(out, 0, MUSIC_SAMPLE_COUNT * 2 * sizeof(short));
        return;
    }

    static short raw[MUSIC_SAMPLE_COUNT * 2];
    size_t wantBytes  = (size_t)MUSIC_SAMPLE_COUNT * g_channels * sizeof(short);
    size_t got         = fread(raw, 1, wantBytes, g_file);

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

static int musicThread(SceSize, void*) {
    static short out[MUSIC_NUM_BUFS][MUSIC_SAMPLE_COUNT * 2];
    int buf = 0;
    for (;;) {
        musicFillBlock(out[buf]);
        int vol = PSP_AUDIO_VOLUME_MAX; // per-sample volume already applied above
        sceAudioOutputPannedBlocking(g_channel, vol, vol, out[buf]);
        buf ^= 1;
    }
    return 0;
}

void musicSetFormat(unsigned int sampleRate, int channels) {
    g_rate     = sampleRate;
    g_channels = (channels == 1) ? 1 : 2;
}

void musicInit(void) {
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, MUSIC_SAMPLE_COUNT,
                                   PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return;

    int thid = sceKernelCreateThread("music_thread", musicThread, 0x13, 0x10000,
                                     PSP_THREAD_ATTR_USER, 0);
    if (thid < 0) { g_channel = -1; return; }
    sceKernelStartThread(thid, 0, 0);

    memset(g_pools, 0, sizeof(g_pools));
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
    g_biomeMusicActive = false;
}

void musicSetVolume(float volume) {
    g_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

void musicRegisterTrack(BiomeId biome, const char* path, int weight) {
    if (biome < 0 || biome >= NUM_BIOMES || weight <= 0) return;
    TrackPool& p = g_pools[biome];
    if (p.count >= MAX_TRACKS_PER_BIOME) return;
    p.paths[p.count]   = path;
    p.weights[p.count] = weight;
    p.totalWeight      += weight;
    p.count++;
}

static const char* pickTrack(BiomeId biome) {
    TrackPool& p = g_pools[biome];
    if (p.count == 0) return NULL;

    int r = rand() % p.totalWeight;
    for (int i = 0; i < p.count; i++) {
        if (r < p.weights[i]) return p.paths[i];
        r -= p.weights[i];
    }
    return p.paths[p.count - 1];
}

void musicUpdate(BiomeId currentBiome) {
    if (g_channel < 0) return;

    bool biomeChanged = (currentBiome != g_lastBiome);
    g_lastBiome = currentBiome;

    bool needsNext = biomeChanged || (g_biomeMusicActive && !g_playing && !g_pendingSwap);
    if (!needsNext) return;

    const char* track = pickTrack(currentBiome);
    if (!track) {
        // No tracks registered for this biome: let whatever's already
        // playing finish naturally rather than cutting it off.
        return;
    }

    FILE* probe = fopen(assetPath(track), "rb");
    if (!probe) probe = fopen(track, "rb");
    if (!probe) {
        // Registered path doesn't exist on disk (e.g. only some of the
        // biome's tracks have been dropped in yet). Mark active so the
        // next tick tries again with a fresh weighted pick instead of
        // silently getting stuck.
        g_biomeMusicActive = true;
        return;
    }
    fclose(probe);

    musicPlay(track, false); // non-looping: musicUpdate will pick the next on finish
    g_biomeMusicActive = true;
}
