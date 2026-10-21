#include <common.h>
#include <EngineSoundRegistry.h>
#include <VehiclePartRegistry.h>
#include <NativeSession.h>
#include <platform/native_assets.h>
#include <platform/native_mod_packages.h>
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static struct EngineSoundDef s_engineRegistry[ENGINE_SOUND_MAX];
static int s_engineRegistryCount;
static int s_engineRegistryReady;

static char *ESR_Trim(char *p)
{
    char *end;
    while (*p && isspace((unsigned char)*p)) ++p;
    end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return p;
}

static int ESR_SafeFile(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (!n || n >= 128 || *s == '/' || *s == '\\' || strstr(s, ".."))
        return 0;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '/' || c == '\\' ||
              c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

static int ESR_FileExists(const char *root, const char *file)
{
    char relative[420], host[1024];
    struct stat st;
    if (!ESR_SafeFile(file))
        return 0;
    if (snprintf(relative, sizeof(relative), "%s/%s", root, file) >= (int)sizeof(relative))
        return 0;
    return NativeAssets_BuildPath(relative, host, sizeof(host)) &&
           stat(host, &st) == 0 && S_ISREG(st.st_mode);
}

static void ESR_Register(const char *root, const char *name, int kartID,
                          const char *loop, const char *idle, const char *turbo,
                          int hasValidSupplemental)
{
    struct EngineSoundDef *entry;
    int hasPrimary = ESR_FileExists(root, loop);
    /* One ENGINE option represents the COMPLETE audio pack of its owner kart.
     * A kart with only transmission/layer sounds also deserves its own entry,
     * and falls back to the character's retail main engine. Standalone
     * engine packs still require their own primary loop. */
    if (s_engineRegistryCount >= ENGINE_SOUND_MAX ||
        (!hasPrimary && !hasValidSupplemental))
        return;
    entry = &s_engineRegistry[s_engineRegistryCount++];
    memset(entry, 0, sizeof(*entry));
    entry->kartID = kartID;
    snprintf(entry->displayName, sizeof(entry->displayName), "%s", name);
    snprintf(entry->root, sizeof(entry->root), "%s", root);
    if (hasPrimary)
        snprintf(entry->loop, sizeof(entry->loop), "%s", loop);
    if (ESR_FileExists(root, idle))
        snprintf(entry->idle, sizeof(entry->idle), "%s", idle);
    if (ESR_FileExists(root, turbo))
        snprintf(entry->turbo, sizeof(entry->turbo), "%s", turbo);
    Platform_Log("[ENGINE PACK] id=%d ownerKart=%d name=%s main=%d "
                 "idle=%d turbo=%d supplemental=%d\n",
                 s_engineRegistryCount - 1, kartID, entry->displayName,
                 entry->loop[0] != 0,
                 entry->idle[0] != 0, entry->turbo[0] != 0,
                 hasValidSupplemental != 0);
}

static void ESR_ReadStandalone(const char *slug, const char *root)
{
    char configRelative[300], host[1024], line[1024], section[64] = "";
    char name[64], loop[128] = "", idle[128] = "", turbo[128] = "";
    FILE *f;
    int enabled = 1;
    snprintf(configRelative, sizeof(configRelative), "%s/engine.ini", root);
    if (!NativeAssets_BuildPath(configRelative, host, sizeof(host)))
        return;
    f = fopen(host, "rb");
    if (!f) return;
    snprintf(name, sizeof(name), "%s", slug);
    while (fgets(line, sizeof(line), f)) {
        char *s = ESR_Trim(line), *equal, *key, *value;
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = 0;
            snprintf(section, sizeof(section), "%s", ESR_Trim(s + 1));
            continue;
        }
        equal = strchr(s, '=');
        if (!equal) continue;
        *equal = 0;
        key = ESR_Trim(s);
        value = ESR_Trim(equal + 1);
        if (!SDL_strcasecmp(section, "engine")) {
            if (!SDL_strcasecmp(key, "name"))
                snprintf(name, sizeof(name), "%s", value);
            else if (!SDL_strcasecmp(key, "enabled"))
                enabled = SDL_strcasecmp(value, "false") &&
                          SDL_strcasecmp(value, "no") && strcmp(value, "0");
        } else if (!SDL_strcasecmp(section, "audio")) {
            if (!ESR_SafeFile(value)) continue;
            if (!SDL_strcasecmp(key, "engine_loop"))
                snprintf(loop, sizeof(loop), "%s", value);
            else if (!SDL_strcasecmp(key, "engine_idle"))
                snprintf(idle, sizeof(idle), "%s", value);
            else if (!SDL_strcasecmp(key, "engine_turbo"))
                snprintf(turbo, sizeof(turbo), "%s", value);
        }
    }
    fclose(f);
    if (enabled)
        ESR_Register(root, name, -1, loop, idle, turbo, 0);
}

static int ESR_Directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ESR_SafeSlug(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (!n || n >= 64) return 0;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (!(islower(c) || isdigit(c) || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static void ESR_ScanStandalone(const char *relative, int unified)
{
    char dirPath[1024], root[256];
    DIR *dir;
    struct dirent *it;
    if (!NativeAssets_BuildPath(relative, dirPath, sizeof(dirPath)) ||
        !ESR_Directory(dirPath) || !(dir = opendir(dirPath)))
        return;
    while ((it = readdir(dir)) != NULL) {
        char child[1200];
        if (!ESR_SafeSlug(it->d_name)) continue;
        snprintf(child, sizeof(child), "%s/%s", dirPath, it->d_name);
        if (!ESR_Directory(child)) continue;
        if (unified && !NativeModPackage_IsValid(it->d_name, "Engine"))
            continue;
        snprintf(root, sizeof(root), "%s/%s", relative, it->d_name);
        ESR_ReadStandalone(it->d_name, root);
    }
    closedir(dir);
}

void EngineSoundRegistry_Init(void)
{
    int i;
    if (s_engineRegistryReady) return;
    s_engineRegistryReady = 1;
    if (NativeSession_NoModsActive()) return;
    for (i = 0; i < VehiclePartRegistry_GetCount(VEHICLE_PART_KART); ++i) {
        const struct VehiclePartDef *kart =
            VehiclePartRegistry_GetByIndex(VEHICLE_PART_KART, i);
        char root[256];
        int hasValidSupplemental;
        if (!kart) continue;
        /* Karts from the Forge Hub use unified mods/<slug>/kart.ini.
         * Legacy loose karts still live under mods/karts/<slug>/.
         * Resolve the same directory as VehiclePart_LoadOne. */
        if (kart->unifiedMod)
            snprintf(root, sizeof(root), "mods/%s", kart->slug);
        else
            snprintf(root, sizeof(root), "mods/karts/%s", kart->slug);
        /* Validate actual audio files before exposing an ENGINE choice.
         * An empty primary loop is fine if the kart has transmission or an
         * independent audio layer: it will play with a retail base engine. */
        hasValidSupplemental = ESR_FileExists(root, kart->secondaryEngineLoop);
        for (int layer = 0; layer < VEHICLE_PART_AUDIO_LAYER_COUNT; ++layer)
            if (ESR_FileExists(root, kart->audioLayerLoop[layer]))
                hasValidSupplemental = 1;
        ESR_Register(root, kart->displayName, kart->id,
                     kart->engineLoop, kart->engineIdle, kart->engineTurbo,
                     hasValidSupplemental);
    }
    ESR_ScanStandalone("mods/engines", 0);
    /* Root mod packages require [forge_mod] type Engine and a valid web ID. */
    ESR_ScanStandalone("mods", 1);
    Platform_Log("[ENGINE PACK] total=%d\n", s_engineRegistryCount);
}

void EngineSoundRegistry_Shutdown(void)
{
    s_engineRegistryCount = 0;
    s_engineRegistryReady = 0;
    memset(s_engineRegistry, 0, sizeof(s_engineRegistry));
}

int EngineSoundRegistry_GetCount(void)
{
    EngineSoundRegistry_Init();
    return s_engineRegistryCount;
}

const struct EngineSoundDef *EngineSoundRegistry_Get(int engineID)
{
    EngineSoundRegistry_Init();
    return engineID >= 0 && engineID < s_engineRegistryCount ?
           &s_engineRegistry[engineID] : NULL;
}

const char *EngineSoundRegistry_GetName(int engineID)
{
    const struct EngineSoundDef *e = EngineSoundRegistry_Get(engineID);
    return e ? e->displayName : "RETAIL";
}

int EngineSoundRegistry_FindForKart(int kartID)
{
    int i;
    EngineSoundRegistry_Init();
    for (i = 0; i < s_engineRegistryCount; i++)
        if (kartID >= 0 && s_engineRegistry[i].kartID == kartID)
            return i;
    return -1;
}

int EngineSoundRegistry_BuildClipPath(int engineID, int kind, char *out, size_t outSize)
{
    const struct EngineSoundDef *e = EngineSoundRegistry_Get(engineID);
    const char *file;
    char relative[420];
    if (!out || !outSize) return 0;
    *out = 0;
    if (!e) return 0;
    file = kind == ENGINE_SOUND_CLIP_LOOP ? e->loop :
           kind == ENGINE_SOUND_CLIP_IDLE ? e->idle :
           kind == ENGINE_SOUND_CLIP_TURBO ? e->turbo : "";
    if (!*file || snprintf(relative, sizeof(relative), "%s/%s", e->root, file) >= (int)sizeof(relative))
        return 0;
    return NativeAssets_BuildPath(relative, out, outSize);
}
