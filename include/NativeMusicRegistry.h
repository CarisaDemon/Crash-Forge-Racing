#ifndef NATIVE_MUSIC_REGISTRY_H
#define NATIVE_MUSIC_REGISTRY_H

int NativeMusicRegistry_TryApplyLevel(int levelID);
int NativeMusicRegistry_TryApplyCustomTrack(void);
int NativeMusicRegistry_IsExternalCseqActive(void);
const char *NativeMusicRegistry_GetActiveSlug(void);
void NativeMusicRegistry_Shutdown(void);

#endif
