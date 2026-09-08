
#ifndef MCPSP_PLATFORM_AUDIO_SOUND_H
#define MCPSP_PLATFORM_AUDIO_SOUND_H

void soundInit(void);

void soundPlay(const char* name, float volume, float pitch);

// Interface/notification sounds. Identical to soundPlay() except that it
// may claim the one voice slot held back from the general pool, so a
// notification can't be silently dropped because sixteen footsteps, block
// breaks and mob noises happened to be in flight at the same instant.
// Use sparingly -- this is for one-off events the player is meant to
// notice, not for anything that fires routinely.
void soundPlayUi(const char* name, float volume);

void soundSetVolume(float volume);

float soundAttenuate(float distSq, float volume);

#endif
