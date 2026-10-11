#ifndef CHARACTER_REGISTRY_H
#define CHARACTER_REGISTRY_H

enum CharacterRegistryFlags
{
    CHARACTER_REGISTRY_FLAG_RETAIL = 1 << 0,
    CHARACTER_REGISTRY_FLAG_HAS_WHEELS = 1 << 1,
    CHARACTER_REGISTRY_FLAG_MODULAR_BODY = 1 << 2,
};

enum CharacterModelFormat
{
    CHARACTER_MODEL_AUTO = 0,
    CHARACTER_MODEL_OBJ,
    CHARACTER_MODEL_GLB,
};

struct CharacterDef
{
    s16 id;
    s16 fallbackRetailID;
    char slug[64];
    char assetName[64];
    char kartSlug[64];
    char displayName[64];
    char iconPath[260];
    char modelFile[128];
    int iconReuseCharacterID;
    float modelScale;
    float modelOffset[3];
    float idleMinDelay;
    float idleMaxDelay;
    float animationCrossfade;
    float referenceMin[3];
    float referenceExtent[3];
    u8 engineClass;
    u8 flags;
    u8 hasReferenceBounds;
    u8 modelFormat;
};

void CharacterRegistry_Init(void);
void CharacterRegistry_Shutdown(void);
int CharacterRegistry_GetCount(void);
int CharacterRegistry_GetModCount(void);
int CharacterRegistry_GetPageCount(int pageSize);
const struct CharacterDef *CharacterRegistry_GetByID(int characterID);
const struct CharacterDef *CharacterRegistry_GetByRosterIndex(int rosterIndex);
const char *CharacterRegistry_GetAssetName(int characterID);
const char *CharacterRegistry_GetKartSlug(int characterID);
const char *CharacterRegistry_GetDisplayName(int characterID);
int CharacterRegistry_GetFallbackRetailID(int characterID);
int CharacterRegistry_GetDriverPackID(int characterID);
int CharacterRegistry_GetEngineClass(int characterID);
b32 CharacterRegistry_HasWheels(int characterID);
b32 CharacterRegistry_IsModularBody(int characterID);
int CharacterRegistry_GetIconReuseCharacterID(int characterID);
int CharacterRegistry_GetIconRetailID(int characterID);
const char *CharacterRegistry_GetIconPath(int characterID);
const char *CharacterRegistry_GetModelFile(int characterID);
int CharacterRegistry_GetModelFormat(int characterID);
float CharacterRegistry_GetIdleMinDelay(int characterID);
float CharacterRegistry_GetIdleMaxDelay(int characterID);
float CharacterRegistry_GetAnimationCrossfade(int characterID);

#endif
