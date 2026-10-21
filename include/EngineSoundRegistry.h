#ifndef ENGINE_SOUND_REGISTRY_H
#define ENGINE_SOUND_REGISTRY_H
#include <stddef.h>

#define ENGINE_SOUND_AUTO (-1)
#define ENGINE_SOUND_RETAIL (-2) /* force selected character's retail engine */
#define ENGINE_SOUND_ACCEL (-3)
#define ENGINE_SOUND_SPEED (-4)
#define ENGINE_SOUND_TURN (-5)
#define ENGINE_SOUND_BALANCED (-6)
#define ENGINE_SOUND_MAX 128
enum EngineSoundClipKind {
    ENGINE_SOUND_CLIP_LOOP = 0,
    ENGINE_SOUND_CLIP_IDLE = 1,
    ENGINE_SOUND_CLIP_TURBO = 2
};

struct EngineSoundDef {
    int kartID; /* -1 for standalone engine pack */
    char displayName[64];
    char root[192]; /* relative to native game assets root */
    char loop[128];
    char idle[128];
    char turbo[128];
};

void EngineSoundRegistry_Init(void);
void EngineSoundRegistry_Shutdown(void);
int EngineSoundRegistry_GetCount(void);
const struct EngineSoundDef *EngineSoundRegistry_Get(int engineID);
const char *EngineSoundRegistry_GetName(int engineID);
int EngineSoundRegistry_FindForKart(int kartID);
int EngineSoundRegistry_BuildClipPath(int engineID, int clip, char *out, size_t outSize);

#endif
