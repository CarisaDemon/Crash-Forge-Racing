#ifndef CFR_NATIVE_CUSTOM_TRACKS_H
#define CFR_NATIVE_CUSTOM_TRACKS_H

/* Native LEV/VRM community tracks. Retail level IDs remain unchanged. */
/* Native music reserves custom slots 100..191, so the scanner can host
 * up to 92 drop-in tracks without recompiling or changing the menu code. */
#define NATIVE_CUSTOM_TRACK_LIMIT 92
#define NATIVE_CUSTOM_TRACK_MUSIC_NONE 0
#define NATIVE_CUSTOM_TRACK_MUSIC_OGG 1
#define NATIVE_CUSTOM_TRACK_MUSIC_CSEQ 2
void NativeCustomTracks_Scan(void);
/* Cached unless refreshed intentionally; no LEV/VRM disk reads in menus. */
void NativeCustomTracks_EnsureScanned(void);
int NativeCustomTracks_Count(void);
const char *NativeCustomTracks_Name(int index);
/* Stable ID (web id when present, folder slug for legacy packs). */
const char *NativeCustomTracks_IdAt(int index);
int NativeCustomTracks_FindId(const char *id);
int NativeCustomTracks_SelectedIndex(void);
int NativeCustomTracks_Select(int index);
void NativeCustomTracks_ClearSelection(void);
int NativeCustomTracks_IsSelected(void);
/* Only true inside a selected custom Crash Cove shell race. */
int NativeCustomTracks_IsActiveRace(void);
/* Music is local to a custom track folder; no Crash Cove soundtrack fallback. */
int NativeCustomTracks_MusicKindAt(int index);
/* Retail slot replaced by the original mod, used for TT/ghost compatibility.
 * Returns -1 when a community track does not declare one. */
int NativeCustomTracks_OriginalLevelIDAt(int index);
int NativeCustomTracks_SelectedOriginalLevelID(void);
const char *NativeCustomTracks_MusicPathAt(int index);
int NativeCustomTracks_MusicSamplesAt(int index);
const char *NativeCustomTracks_DirectoryAt(int index);
/* Current custom race title, or NULL outside an active custom race. */
const char *NativeCustomTracks_ActiveName(void);
int NativeCustomTracks_SelectedMusicKind(void);
int NativeCustomTracks_SelectedMusicSamples(void);
int NativeCustomTracks_SelectedMusicKey(void);
const char *NativeCustomTracks_SelectedMusicPath(void);
const char *NativeCustomTracks_SelectedFinalMusicPath(void);
const char *NativeCustomTracks_SelectedDirectory(void);
/* 1 = LEV, 2 = VRM, 0 = normal retail file. */
int NativeCustomTracks_OverrideKind(int loadType, int bigfileIndex);
void *NativeCustomTracks_ReadOverride(int kind, void *destination,
                                      unsigned int *outSize);
#endif
