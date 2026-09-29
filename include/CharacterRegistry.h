#ifndef CHARACTER_REGISTRY_H
#define CHARACTER_REGISTRY_H

enum CharacterRegistryFlags
{
    CHARACTER_REGISTRY_FLAG_RETAIL = 1 << 0,
    CHARACTER_REGISTRY_FLAG_HAS_WHEELS = 1 << 1,
};

struct CharacterDef
{
    s16 id;
    s16 fallbackRetailID;
    char slug[64];
    char assetName[64];
    char displayName[64];
    char iconPath[260];
    int iconReuseCharacterID;
    float modelScale;
    float modelOffset[3];
    u8 engineClass;
    u8 flags;
};

void CharacterRegistry_Init(void);
void CharacterRegistry_Shutdown(void);
int CharacterRegistry_GetCount(void);
int CharacterRegistry_GetModCount(void);
int CharacterRegistry_GetPageCount(int pageSize);
const struct CharacterDef *CharacterRegistry_GetByID(int characterID);
const struct CharacterDef *CharacterRegistry_GetByRosterIndex(int rosterIndex);
const char *CharacterRegistry_GetAssetName(int characterID);
const char *CharacterRegistry_GetDisplayName(int characterID);
int CharacterRegistry_GetFallbackRetailID(int characterID);
int CharacterRegistry_GetDriverPackID(int characterID);
int CharacterRegistry_GetEngineClass(int characterID);
b32 CharacterRegistry_HasWheels(int characterID);
int CharacterRegistry_GetIconReuseCharacterID(int characterID);
int CharacterRegistry_GetIconRetailID(int characterID);
const char *CharacterRegistry_GetIconPath(int characterID);

#endif
