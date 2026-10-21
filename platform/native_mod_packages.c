#include <platform/native_mod_packages.h>
#include <platform/native_assets.h>
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *NativeModPackage_Trim(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static int NativeModPackage_SlugSafe(const char *s)
{
    size_t count = 0;
    if (!s || !*s) return 0;
    while (*s)
    {
        unsigned char c = (unsigned char)*s++;
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '_' && c != '-') return 0;
        if (++count >= 64) return 0;
    }
    return 1;
}

static const char *NativeModPackage_IniForType(const char *type)
{
    if (!SDL_strcasecmp(type, "Character")) return "character.ini";
    if (!SDL_strcasecmp(type, "Kart")) return "kart.ini";
    if (!SDL_strcasecmp(type, "Wheels")) return "wheels.ini";
    if (!SDL_strcasecmp(type, "Music")) return "music.ini";
    if (!SDL_strcasecmp(type, "Engine")) return "engine.ini";
    if (!SDL_strcasecmp(type, "Track")) return "track.ini";
    return NULL;
}

int NativeModPackage_IsValid(const char *slug, const char *kind)
{
    const char *iniName = NativeModPackage_IniForType(kind);
    char relative[192];
    char hostPath[1024];
    char line[1024];
    char declaredID[80] = "";
    char declaredType[32] = "";
    int foundSection = 0;
    FILE *stream;

    if (!iniName || !NativeModPackage_SlugSafe(slug)) return 0;
    snprintf(relative, sizeof(relative), "mods/%s/%s", slug, iniName);
    if (!NativeAssets_BuildPath(relative, hostPath, sizeof(hostPath))) return 0;
    stream = fopen(hostPath, "rb");
    if (!stream) return 0;
    while (fgets(line, sizeof(line), stream))
    {
        char *s = NativeModPackage_Trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[')
        {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            foundSection = !SDL_strcasecmp(NativeModPackage_Trim(s+1),
                                           "forge_mod");
            continue;
        }
        if (foundSection)
        {
            char *equal = strchr(s, '=');
            if (!equal) continue;
            *equal = '\0';
            char *key = NativeModPackage_Trim(s);
            char *value = NativeModPackage_Trim(equal+1);
            if (!SDL_strcasecmp(key, "id"))
                snprintf(declaredID, sizeof(declaredID), "%s", value);
            if (!SDL_strcasecmp(key, "type"))
                snprintf(declaredType, sizeof(declaredType), "%s", value);
        }
    }
    fclose(stream);
    /* Directory is a readable mod name, not its permanent web ID.
       Validate immutable ID syntax and mod type independently. */
    if (NativeModPackage_SlugSafe(declaredID) &&
        !SDL_strcasecmp(declaredType, kind))
        return 1;
    Platform_Log("[MOD PACKAGE] ignored %s: invalid [forge_mod] id/type for %s\n",
                 slug, kind);
    return 0;
}
