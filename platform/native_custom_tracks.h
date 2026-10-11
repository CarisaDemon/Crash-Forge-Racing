#ifndef CFR_NATIVE_CUSTOM_TRACKS_H
#define CFR_NATIVE_CUSTOM_TRACKS_H

/* Native LEV/VRM community tracks. Retail level IDs remain unchanged. */
#define NATIVE_CUSTOM_TRACK_LIMIT 16
#define NATIVE_CUSTOM_TRACK_MUSIC_NONE 0
#define NATIVE_CUSTOM_TRACK_MUSIC_OGG 1
#define NATIVE_CUSTOM_TRACK_MUSIC_CSEQ 2
void NativeCustomTracks_Scan(void);
int NativeCustomTracks_Count(void);
const char *NativeCustomTracks_Name(int index);
int NativeCustomTracks_Select(int index);
void NativeCustomTracks_ClearSelection(void);
int NativeCustomTracks_IsSelected(void);
/* Only true inside a selected custom Crash Cove shell race. */
int NativeCustomTracks_IsActiveRace(void);
/* Music is local to a custom track folder; no Crash Cove soundtrack fallback. */
int NativeCustomTracks_MusicKindAt(int index);
const char *NativeCustomTracks_MusicPathAt(int index);
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
