#include <common.h>
#include <CharacterRegistry.h>
#include <platform/native_assets.h>

#if defined(CTR_NATIVE)
#include <SDL3/SDL.h>
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

static struct CharacterDef *s_registry;
static int s_registryCount;
static int s_registryCapacity;
static int s_registryInitialized;

static const char *const s_retailSlugs[16] = {
    "crash", "cortex", "tiny", "coco", "ngin", "dingo", "polar", "pura",
    "pinstripe", "papu", "roo", "joe", "ntropy", "pen", "fake", "oxide"
};

static const char *const s_retailNames[16] = {
    "CRASH BANDICOOT", "DR. NEO CORTEX", "TINY TIGER", "COCO BANDICOOT",
    "N. GIN", "DINGODILE", "POLAR", "PURA", "PINSTRIPE", "PAPU PAPU",
    "RIPPER ROO", "KOMODO JOE", "N. TROPY", "PENTA PENGUIN", "FAKE CRASH",
    "NITROS OXIDE"
};

static char *Registry_Trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = 0;
    return s;
}

static int Registry_Reserve(int needed)
{
    if (needed <= s_registryCapacity) return 1;
    int capacity = s_registryCapacity ? s_registryCapacity * 2 : 32;
    while (capacity < needed) capacity *= 2;
    struct CharacterDef *next = realloc(s_registry, (size_t)capacity * sizeof(*next));
    if (!next) return 0;
    s_registry = next;
    s_registryCapacity = capacity;
    return 1;
}

static int Registry_ParseBool(const char *value, int fallback)
{
    if (!value || !*value) return fallback;
    if (!SDL_strcasecmp(value, "true") || !SDL_strcasecmp(value, "yes") || !strcmp(value, "1")) return 1;
    if (!SDL_strcasecmp(value, "false") || !SDL_strcasecmp(value, "no") || !strcmp(value, "0")) return 0;
    return fallback;
}

static int Registry_ParseRetailID(const char *value, int fallback)
{
    if (!value || !*value) return fallback;
    char *end = NULL;
    long numeric = strtol(value, &end, 10);
    if (end && *Registry_Trim(end) == 0 && numeric >= 0 && numeric < 16) return (int)numeric;
    for (int i = 0; i < 16; i++)
        if (!SDL_strcasecmp(value, s_retailSlugs[i])) return i;
    return fallback;
}

static int Registry_ParseEngine(const char *value, int fallback)
{
    if (!value || !*value) return fallback;
    if (!SDL_strcasecmp(value, "turn")) return TURN;
    if (!SDL_strcasecmp(value, "accel") || !SDL_strcasecmp(value, "acceleration")) return ACCEL;
    if (!SDL_strcasecmp(value, "speed")) return SPEED;
    if (!SDL_strcasecmp(value, "balanced") || !SDL_strcasecmp(value, "balance")) return BALANCED;
    return fallback;
}

static void Registry_AddRetail(void)
{
    for (int i = 0; i < 16; i++)
    {
        if (!Registry_Reserve(s_registryCount + 1)) return;
        struct CharacterDef *c = &s_registry[s_registryCount++];
        memset(c, 0, sizeof(*c));
        c->id = (s16)i;
        c->fallbackRetailID = (s16)i;
        snprintf(c->slug, sizeof(c->slug), "%s", s_retailSlugs[i]);
        snprintf(c->assetName, sizeof(c->assetName), "%s", s_retailSlugs[i]);
        snprintf(c->displayName, sizeof(c->displayName), "%s", s_retailNames[i]);
        c->iconReuseCharacterID = i;
        c->engineClass = data.MetaDataCharacters[i].engineID;
        c->flags = CHARACTER_REGISTRY_FLAG_RETAIL;
        if (i != NITROS_OXIDE)
            c->flags |= CHARACTER_REGISTRY_FLAG_HAS_WHEELS;
    }
}

static int Registry_IsDirectory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void Registry_LoadOneMod(const char *folderName)
{
    if ((folderName == NULL) || !*folderName ||
        (strlen(folderName) >= sizeof(((struct CharacterDef *)0)->slug)))
    {
        SDL_Log("CharacterRegistry: racer folder name is empty or too long; skipping mod");
        return;
    }

    char relative[512], configPath[1024];
    snprintf(relative, sizeof(relative), "mods/racers/%s/character.ini", folderName);
    if (!NativeAssets_BuildPath(relative, configPath, sizeof(configPath))) return;

    FILE *f = fopen(configPath, "rb");
    if (!f) return;

    struct CharacterDef temp;
    memset(&temp, 0, sizeof(temp));
    temp.fallbackRetailID = CRASH_BANDICOOT;
    temp.iconReuseCharacterID = CRASH_BANDICOOT;
    temp.modelScale = 1.0f;
    temp.modelOffset[0] = 0.0f;
    temp.modelOffset[1] = 0.0f;
    temp.modelOffset[2] = 0.0f;
    temp.engineClass = BALANCED;
    temp.flags = CHARACTER_REGISTRY_FLAG_HAS_WHEELS;
    snprintf(temp.slug, sizeof(temp.slug), "%s", folderName);
    snprintf(temp.assetName, sizeof(temp.assetName), "%s", folderName);
    snprintf(temp.displayName, sizeof(temp.displayName), "%s", folderName);

    int enabled = 1;
    int valid = 1;
    char section[64] = "";
    char line[1024];

    while (fgets(line, sizeof(line), f))
    {
        char *s = Registry_Trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[')
        {
            char *close = strchr(s, ']');
            if (!close) continue;
            *close = 0;
            snprintf(section, sizeof(section), "%s", Registry_Trim(s + 1));
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = Registry_Trim(s);
        char *value = Registry_Trim(eq + 1);

        if (!SDL_strcasecmp(section, "character"))
        {
            if (!SDL_strcasecmp(key, "name")) snprintf(temp.displayName, sizeof(temp.displayName), "%s", value);
            else if (!SDL_strcasecmp(key, "enabled")) enabled = Registry_ParseBool(value, enabled);
            else if (!SDL_strcasecmp(key, "fallback_retail")) temp.fallbackRetailID = (s16)Registry_ParseRetailID(value, temp.fallbackRetailID);
            else if (!SDL_strcasecmp(key, "engine")) temp.engineClass = (u8)Registry_ParseEngine(value, temp.engineClass);
            else if (!SDL_strcasecmp(key, "has_wheels"))
            {
                if (Registry_ParseBool(value, 1)) temp.flags |= CHARACTER_REGISTRY_FLAG_HAS_WHEELS;
                else temp.flags &= (u8)~CHARACTER_REGISTRY_FLAG_HAS_WHEELS;
            }
        }
        else if (!SDL_strcasecmp(section, "model"))
        {
            float parsed = 0.0f;
            if (sscanf(value, "%f", &parsed) == 1 && isfinite(parsed))
            {
                if (!SDL_strcasecmp(key, "scale") && parsed > 0.001f && parsed < 100.0f)
                    temp.modelScale = parsed;
                else if (!SDL_strcasecmp(key, "offset_x")) temp.modelOffset[0] = parsed;
                else if (!SDL_strcasecmp(key, "offset_y")) temp.modelOffset[1] = parsed;
                else if (!SDL_strcasecmp(key, "offset_z")) temp.modelOffset[2] = parsed;
            }
        }
        else if (!SDL_strcasecmp(section, "assets"))
        {
            if (!SDL_strcasecmp(key, "asset") || !SDL_strcasecmp(key, "asset_name"))
            {
                if (strlen(value) >= sizeof(temp.assetName))
                {
                    SDL_Log("CharacterRegistry: asset_name for %s is too long; skipping mod", folderName);
                    valid = 0;
                }
                else
                {
                    snprintf(temp.assetName, sizeof(temp.assetName), "%s", value);
                }
            }
            else if (!SDL_strcasecmp(key, "icon"))
            {
                if (!SDL_strncasecmp(value, "retail:", 7))
                    temp.iconReuseCharacterID = Registry_ParseRetailID(value + 7, temp.iconReuseCharacterID);
                else
                {
                    temp.iconReuseCharacterID = -1;
                    snprintf(temp.iconPath, sizeof(temp.iconPath), "%s", value);
                }
            }
        }
    }
    fclose(f);

    if (!enabled || !valid) return;

    for (int i = 0; i < s_registryCount; i++)
        if (!SDL_strcasecmp(s_registry[i].slug, temp.slug)) return;

    if (!Registry_Reserve(s_registryCount + 1)) return;
    temp.id = (s16)s_registryCount;
    s_registry[s_registryCount++] = temp;
    SDL_Log("CharacterRegistry: registered mod id=%d slug=%s name=%s", temp.id, temp.slug, temp.displayName);
}

static int Registry_CompareMods(const void *a, const void *b)
{
    const struct CharacterDef *ca = (const struct CharacterDef *)a;
    const struct CharacterDef *cb = (const struct CharacterDef *)b;
    return SDL_strcasecmp(ca->slug, cb->slug);
}

static void Registry_DiscoverMods(void)
{
    char modsDir[1024];
    if (!NativeAssets_BuildPath("mods/racers", modsDir, sizeof(modsDir))) return;
    if (!Registry_IsDirectory(modsDir)) return;

    DIR *dir = opendir(modsDir);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[1200];
        snprintf(child, sizeof(child), "%s/%s", modsDir, entry->d_name);
        if (Registry_IsDirectory(child)) Registry_LoadOneMod(entry->d_name);
    }
    closedir(dir);
}

void CharacterRegistry_Init(void)
{
    if (s_registryInitialized) return;
    s_registryInitialized = 1;
    Registry_AddRetail();
    Registry_DiscoverMods();
    if (s_registryCount > 17)
    {
        qsort(&s_registry[16], (size_t)(s_registryCount - 16), sizeof(*s_registry), Registry_CompareMods);
        for (int i = 16; i < s_registryCount; i++)
            s_registry[i].id = (s16)i;
    }
    SDL_Log("CharacterRegistry: %d characters (%d mods)", s_registryCount, s_registryCount > 16 ? s_registryCount - 16 : 0);
}

void CharacterRegistry_Shutdown(void)
{
    free(s_registry);
    s_registry = NULL;
    s_registryCount = 0;
    s_registryCapacity = 0;
    s_registryInitialized = 0;
}

int CharacterRegistry_GetCount(void)
{
    CharacterRegistry_Init();
    return s_registryCount;
}

int CharacterRegistry_GetModCount(void)
{
    int count = CharacterRegistry_GetCount();
    return count > 16 ? count - 16 : 0;
}

int CharacterRegistry_GetPageCount(int pageSize)
{
    int count = CharacterRegistry_GetCount();
    if (pageSize <= 0) return 0;
    return (count + pageSize - 1) / pageSize;
}

const struct CharacterDef *CharacterRegistry_GetByID(int characterID)
{
    CharacterRegistry_Init();
    if (characterID < 0 || characterID >= s_registryCount) return NULL;
    return &s_registry[characterID];
}

const struct CharacterDef *CharacterRegistry_GetByRosterIndex(int rosterIndex)
{
    return CharacterRegistry_GetByID(rosterIndex);
}

const char *CharacterRegistry_GetAssetName(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? c->assetName : "crash";
}

const char *CharacterRegistry_GetDisplayName(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? c->displayName : "CRASH BANDICOOT";
}

int CharacterRegistry_GetFallbackRetailID(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? c->fallbackRetailID : CRASH_BANDICOOT;
}

int CharacterRegistry_GetDriverPackID(int characterID)
{
    int packID = CharacterRegistry_GetFallbackRetailID(characterID);
    if ((packID < CRASH_BANDICOOT) || (packID > NITROS_OXIDE))
        packID = CRASH_BANDICOOT;
    return packID;
}

int CharacterRegistry_GetEngineClass(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? c->engineClass : BALANCED;
}

b32 CharacterRegistry_HasWheels(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? ((c->flags & CHARACTER_REGISTRY_FLAG_HAS_WHEELS) != 0) : true;
}

int CharacterRegistry_GetIconReuseCharacterID(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return c ? c->iconReuseCharacterID : CRASH_BANDICOOT;
}

int CharacterRegistry_GetIconRetailID(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    if (c == NULL)
        return CRASH_BANDICOOT;

    int iconID = c->iconReuseCharacterID;
    if ((iconID < CRASH_BANDICOOT) || (iconID > NITROS_OXIDE))
        iconID = c->fallbackRetailID;
    if ((iconID < CRASH_BANDICOOT) || (iconID > NITROS_OXIDE))
        iconID = CRASH_BANDICOOT;
    return iconID;
}

const char *CharacterRegistry_GetIconPath(int characterID)
{
    const struct CharacterDef *c = CharacterRegistry_GetByID(characterID);
    return (c && c->iconPath[0]) ? c->iconPath : NULL;
}
