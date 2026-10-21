/* Crash Forge native text localization (UTF-8 catalog keys, ASCII-safe glyphs).
 * Country-specific translations remain editable by the community under
 * assets/localization/strings_<code>.tsv. English fallback is intentional.
 *
 * This adapter translates final UI text at the DecalFont boundary, including
 * many retail LNG strings as well as hardcoded custom UI text. Do not map
 * user-authored mod names, paths, chat text or player names unless explicitly
 * authored as exact translation keys in a locale catalog.
 */
#ifndef CTR_NATIVE_LOCALIZATION_IMPLEMENTATION
#define CTR_NATIVE_LOCALIZATION_IMPLEMENTATION

#define NATIVE_LOC_COUNT 1024
#define NATIVE_LOC_KEY_SIZE 1024
#define NATIVE_LOC_VALUE_SIZE 1024

struct NativeLocalizationEntry
{
    unsigned hash;
    unsigned used;
    char key[NATIVE_LOC_KEY_SIZE];
    char value[NATIVE_LOC_VALUE_SIZE];
};

static struct NativeLocalizationEntry s_nativeLocTable[NATIVE_LOC_COUNT];
static int s_nativeLocCurrentLanguage = 0;
static int s_nativeLocLoaded = 0;
static int s_nativeLocEntryCount = 0;

static const char *s_nativeLocLanguageCodes[6] =
    {"en", "es", "fr", "de", "it", "nl"};
static const char *s_nativeLocLanguageNames[6] =
    {"ENGLISH", "ESPANOL", "FRANCAIS", "DEUTSCH", "ITALIANO", "NEDERLANDS"};

const char *NativeLocalization_GetLanguageName(int lang)
{
    if (lang < 0 || lang >= 6) lang = 0;
    return s_nativeLocLanguageNames[lang];
}

static unsigned NativeLocalization_Hash(const char *s)
{
    unsigned h = 2166136261u;
    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h ? h : 1;
}

static void NativeLocalization_Insert(const char *source, const char *translated)
{
    if (!source || !source[0] || !translated || !translated[0]) return;
    if (strlen(source) >= NATIVE_LOC_KEY_SIZE ||
        strlen(translated) >= NATIVE_LOC_VALUE_SIZE) return;
    unsigned h = NativeLocalization_Hash(source);
    unsigned base = h & (NATIVE_LOC_COUNT - 1);
    for (unsigned probe = 0; probe < NATIVE_LOC_COUNT; ++probe)
    {
        struct NativeLocalizationEntry *entry =
            &s_nativeLocTable[(base + probe) & (NATIVE_LOC_COUNT - 1)];
        if (!entry->used)
        {
            entry->hash = h;
            entry->used = 1;
            snprintf(entry->key, sizeof(entry->key), "%s", source);
            snprintf(entry->value, sizeof(entry->value), "%s", translated);
            ++s_nativeLocEntryCount;
            return;
        }
        if (entry->hash == h && strcmp(entry->key, source) == 0)
        {
            snprintf(entry->value, sizeof(entry->value), "%s", translated);
            return;
        }
    }
}

void NativeLocalization_SetLanguage(int lang)
{
    char filename[128];
    char path[1024];
    char line[2304];
    FILE *fp = NULL;
    if (lang < 0 || lang >= 6) lang = 0;
    if (s_nativeLocLoaded && s_nativeLocCurrentLanguage == lang) return;

    s_nativeLocCurrentLanguage = lang;
    s_nativeLocLoaded = 1;
    s_nativeLocEntryCount = 0;
    memset(s_nativeLocTable, 0, sizeof(s_nativeLocTable));
    if (lang == 0) return; /* Original English, no lookup cost. */

    snprintf(filename, sizeof(filename), "localization/strings_%s.tsv",
             s_nativeLocLanguageCodes[lang]);
    /* Favor resolved game assets; retain cwd fallback for QA or mod installers. */
    if (NativeAssets_BuildPath(filename, path, sizeof(path)))
        fp = fopen(path, "rb");
    if (!fp)
    {
        snprintf(path, sizeof(path), "assets/%s", filename);
        fp = fopen(path, "rb");
    }
    if (!fp)
    {
        Platform_LogWarn("[LANGUAGE] missing catalog %s: English fallback\n",
                         filename);
        return;
    }

    while (fgets(line, sizeof(line), fp))
    {
        size_t n = strlen(line);
        if (n == 0) continue;
        if (n == sizeof(line) - 1 && line[n - 1] != '\n')
        {
            int ch;
            while ((ch = fgetc(fp)) != EOF && ch != '\n') { }
            continue;
        }
        while (n && (line[n - 1] == '\r' || line[n - 1] == '\n'))
            line[--n] = 0;
        if (!n || line[0] == '#') continue;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab++ = 0;
        if (*tab) NativeLocalization_Insert(line, tab);
    }
    fclose(fp);
    Platform_Log("[LANGUAGE] code=%s entries=%d\n",
                 s_nativeLocLanguageCodes[lang], s_nativeLocEntryCount);
}

int NativeLocalization_GetLanguage(void)
{
    return s_nativeLocCurrentLanguage;
}

char *NativeLocalization_Translate(const char *source)
{
    if (!source || !*source ||
        s_nativeLocCurrentLanguage == 0 || !s_nativeLocEntryCount)
        return (char *)source;

    unsigned h = NativeLocalization_Hash(source);
    unsigned base = h & (NATIVE_LOC_COUNT - 1);
    for (unsigned probe = 0; probe < NATIVE_LOC_COUNT; ++probe)
    {
        struct NativeLocalizationEntry *entry =
            &s_nativeLocTable[(base + probe) & (NATIVE_LOC_COUNT - 1)];
        if (!entry->used) break;
        if (entry->hash == h && strcmp(entry->key, source) == 0)
            return entry->value;
    }
    return (char *)source;
}
#endif
