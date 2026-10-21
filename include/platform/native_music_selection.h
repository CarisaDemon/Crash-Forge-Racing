#ifndef PLATFORM_NATIVE_MUSIC_SELECTION_H
#define PLATFORM_NATIVE_MUSIC_SELECTION_H

/* Synthetic community cues start at index 100 and can occupy up to 64
 * slots; keep the resolver array large enough for the entire catalog. */
#define NATIVE_MUSIC_MAX_LEVELS 192
/* Synthetic music cues, never retail LEV slots. */
#define NATIVE_MUSIC_MASK_AKU_SLOT 96
#define NATIVE_MUSIC_MASK_UKA_SLOT 97
#define NATIVE_MUSIC_BOSS_SLOT 98
#define NATIVE_MUSIC_HANGOUT_SLOT 99
#define NATIVE_MUSIC_CUSTOM_BASE 100
#define NATIVE_MUSIC_SOURCE_EXISTING 0
#define NATIVE_MUSIC_SOURCE_RETAIL 1
#define NATIVE_MUSIC_SOURCE_OGG 2
#define NATIVE_MUSIC_SOURCE_CSEQ 3
#define NATIVE_MUSIC_SOURCE_DEFAULT -1

struct NativeMusicSelectionResolved
{
    int source;                  /* existing/retail/OGG/CSEQ */
    char path[1024];            /* relative to game assets root */
    char directory[1024];       /* owning folder, for CSEQ WAV samples */
    char pack[64];
    int customSamples;
};

void NativeMusicSelection_EnsureReady(void);
void NativeMusicSelection_Reload(void);
int NativeMusicSelection_Resolve(int levelID,
    struct NativeMusicSelectionResolved *out);
int NativeMusicSelection_GetDefaultSource(void);
void NativeMusicSelection_SetDefaultSource(int source);
const char *NativeMusicSelection_SourceName(int source);
int NativeMusicSelection_GetPackCount(int source);
const char *NativeMusicSelection_GetPackName(int source, int index);
int NativeMusicSelection_GetDefaultPackIndex(int source);
void NativeMusicSelection_CycleDefaultPack(int source, int direction);
int NativeMusicSelection_GetSongCount(void);
const char *NativeMusicSelection_GetSongName(int index);
int NativeMusicSelection_GetSongSource(int index);
int NativeMusicSelection_GetLevelChoice(int levelID);
int NativeMusicSelection_GetLevelChoiceCount(void);
/* Filtered UI index; excludes custom-track songs whose dependency is absent. */
int NativeMusicSelection_GetAvailableChoiceCount(int slot);
int NativeMusicSelection_GetAvailableChoiceValue(int slot, int position);
int NativeMusicSelection_GetAvailableChoicePosition(int slot, int choice);
/* Group music choices by their actual music.ini pack. These APIs affect UI
 * presentation only; saved per-track song choices and playback are unchanged.
 * Pack positions are indices among packs visible for this destination.
 * Song choices remain the original 3+globalSongIndex handles. */
int NativeMusicSelection_GetAvailablePackCount(int slot);
const char *NativeMusicSelection_GetAvailablePackName(int slot,int packPosition);
int NativeMusicSelection_GetAvailablePackPositionForChoice(int slot,int choice);
int NativeMusicSelection_GetAvailablePackSongCount(int slot,int packPosition);
int NativeMusicSelection_GetAvailablePackSongChoice(int slot,int packPosition,int pos);
int NativeMusicSelection_GetAvailablePackSongPosition(int slot,int packPosition,int choice);
const char *NativeMusicSelection_GetAvailablePackSongName(int choice);
const char *NativeMusicSelection_GetLevelChoiceName(int choice);
void NativeMusicSelection_SetLevelChoice(int levelID, int choice);
const char *NativeMusicSelection_GetLevelSelectionName(int levelID);

#endif
