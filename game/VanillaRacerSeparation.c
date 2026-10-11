#include <common.h>
#include <CharacterRegistry.h>
#include <RacerLoadout.h>
#include <VanillaRacerSeparation.h>
#include <platform/native_assets.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define VANILLA_MASK_CHARACTER_COUNT 16
#define VANILLA_MASK_MAX_RANGES 256

struct VanillaPrimitiveRange
{
    int first;
    int last;
};

struct VanillaPrimitiveMask
{
    struct VanillaPrimitiveRange ranges[VANILLA_MASK_MAX_RANGES];
    int rangeCount;
    int loaded;
};

static struct VanillaPrimitiveMask s_vanillaMasks[VANILLA_MASK_CHARACTER_COUNT];
static int s_vanillaMasksInitialized;

static char *VanillaMask_Trim(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = 0;
    return s;
}

static void VanillaMask_LoadCharacter(int characterID)
{
    const struct CharacterDef *character = CharacterRegistry_GetByID(characterID);
    struct VanillaPrimitiveMask *mask = &s_vanillaMasks[characterID];
    char relative[256];
    char path[1024];
    char line[256];
    FILE *f;

    memset(mask, 0, sizeof(*mask));
    if (character == NULL)
        return;

    snprintf(relative, sizeof(relative),
             "mods/vanilla_racer_masks/%s.mask", character->slug);
    if (!NativeAssets_BuildPath(relative, path, sizeof(path)))
        return;

    f = fopen(path, "rb");
    if (f == NULL)
        return;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *text = VanillaMask_Trim(line);
        int first;
        int last;

        if (!*text || *text == '#' || *text == ';')
            continue;
        if (mask->rangeCount >= VANILLA_MASK_MAX_RANGES)
            break;

        if (sscanf(text, "%d-%d", &first, &last) == 2)
        {
            if (first < 0 || last < first)
                continue;
        }
        else if (sscanf(text, "%d", &first) == 1)
        {
            if (first < 0)
                continue;
            last = first;
        }
        else
        {
            continue;
        }

        mask->ranges[mask->rangeCount].first = first;
        mask->ranges[mask->rangeCount].last = last;
        mask->rangeCount++;
    }

    fclose(f);
    mask->loaded = mask->rangeCount > 0;

    if (mask->loaded)
        Platform_Log("[VANILLA SEPARATION] %s: %d primitive mask ranges\n",
                     character->slug, mask->rangeCount);
}

void VanillaRacerSeparation_Init(void)
{
    if (s_vanillaMasksInitialized)
        return;

    s_vanillaMasksInitialized = 1;
    CharacterRegistry_Init();

    for (int i = 0; i < VANILLA_MASK_CHARACTER_COUNT; i++)
        VanillaMask_LoadCharacter(i);
}

void VanillaRacerSeparation_Reload(void)
{
    s_vanillaMasksInitialized = 0;
    memset(s_vanillaMasks, 0, sizeof(s_vanillaMasks));
    VanillaRacerSeparation_Init();
}

b32 VanillaRacerSeparation_HasMask(int characterID)
{
    VanillaRacerSeparation_Init();
    if (characterID < 0 || characterID >= VANILLA_MASK_CHARACTER_COUNT)
        return false;

    return s_vanillaMasks[characterID].loaded != 0;
}

b32 VanillaRacerSeparation_ShouldHidePrimitive(int characterID, int primitiveIndex)
{
    struct VanillaPrimitiveMask *mask;

    if (RacerLoadout_IsAdventureLocked())
        return false;

    if (!VanillaRacerSeparation_HasMask(characterID) || primitiveIndex < 0)
        return false;

    mask = &s_vanillaMasks[characterID];
    for (int i = 0; i < mask->rangeCount; i++)
    {
        if (primitiveIndex >= mask->ranges[i].first &&
            primitiveIndex <= mask->ranges[i].last)
            return true;
    }

    return false;
}

static int VanillaMask_CharacterForInstance(const struct Instance *inst)
{
    struct GameTracker *gGT = sdata->gGT;

    if (inst == NULL || gGT == NULL)
        return -1;

    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
    {
        struct Driver *driver = gGT->drivers[i];
        if (driver != NULL && driver->instSelf == inst)
            return data.characterIDs[i];
    }

    return -1;
}

b32 VanillaRacerSeparation_ShouldHidePrimitiveForInstance(const struct Instance *inst, int primitiveIndex)
{
    struct GameTracker *gGT = sdata->gGT;

    if (inst == NULL || gGT == NULL)
        return false;

    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
    {
        struct Driver *driver = gGT->drivers[i];
        if (driver == NULL || driver->instSelf != inst)
            continue;

        /* A mask by itself is only tooling data. Retail geometry is hidden
           only after this driver actually selected a non-original kart. */
        if (RacerLoadout_GetEffectiveKart(i) < 0)
            return false;

        return VanillaRacerSeparation_ShouldHidePrimitive(
            data.characterIDs[i], primitiveIndex);
    }

    return false;
}

int VanillaRacerSeparation_CountPrimitives(const struct ModelHeader *mh)
{
    const u32 *cmd;
    int stripLength = 0;
    int primitiveCount = 0;

    if (mh == NULL || mh->ptrCommandList == 0)
        return 0;

    cmd = (const u32 *)(uintptr_t)mh->ptrCommandList;
    cmd++;

    while (*cmd != 0xffffffffU)
    {
        u32 command = *cmd++;
        u8 flags;

        if ((command >> 16) == 0)
            continue;

        flags = (u8)(command >> 24);
        if ((flags & 0x80) != 0)
            stripLength = 0;

        if (stripLength >= 2)
            primitiveCount++;

        stripLength++;
    }

    return primitiveCount;
}

struct VanillaDebugExportState
{
    int controlChecked;
    int requestedCharacterID;
    int completed;
    const struct Instance *activeInst;
    FILE *obj;
    int vertexCount;
    int triangleCount;
    char outputPath[1024];
};

static struct VanillaDebugExportState s_vanillaDebugExport;

static int VanillaDebug_EqualsIgnoreCase(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;

    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

static int VanillaDebug_MakeDir(const char *path)
{
    if (path == NULL || !*path)
        return 0;

#if defined(_WIN32)
    if (_mkdir(path) == 0)
#else
    if (mkdir(path, 0755) == 0)
#endif
        return 1;

    return errno == EEXIST;
}

static void VanillaDebug_CheckControl(void)
{
    char masksDir[1024];
    char controlPath[1024];
    char line[128];
    FILE *f;

    if (s_vanillaDebugExport.controlChecked)
        return;

    s_vanillaDebugExport.controlChecked = 1;
    s_vanillaDebugExport.requestedCharacterID = -1;

    if (!NativeAssets_BuildPath("mods/vanilla_racer_masks",
                                masksDir, sizeof(masksDir)))
        return;

    VanillaDebug_MakeDir(masksDir);

    if (!NativeAssets_BuildPath("mods/vanilla_racer_masks/export.txt",
                                controlPath, sizeof(controlPath)))
        return;

    f = fopen(controlPath, "rb");
    if (f == NULL)
        return;

    if (fgets(line, sizeof(line), f) != NULL)
    {
        char *value = VanillaMask_Trim(line);
        char *end = NULL;
        long numeric = strtol(value, &end, 10);

        if (end != value && *VanillaMask_Trim(end) == 0 &&
            numeric >= 0 && numeric < VANILLA_MASK_CHARACTER_COUNT)
        {
            s_vanillaDebugExport.requestedCharacterID = (int)numeric;
        }
        else
        {
            for (int i = 0; i < VANILLA_MASK_CHARACTER_COUNT; i++)
            {

                const struct CharacterDef *character = CharacterRegistry_GetByID(i);
                if (character != NULL &&
                    VanillaDebug_EqualsIgnoreCase(value, character->slug))
                {
                    s_vanillaDebugExport.requestedCharacterID = i;
                    break;
                }
            }
        }
    }

    fclose(f);

    if (s_vanillaDebugExport.requestedCharacterID >= 0)
    {
        const struct CharacterDef *character =
            CharacterRegistry_GetByID(s_vanillaDebugExport.requestedCharacterID);
        Platform_Log("[VANILLA SEPARATION] export requested: %s\n",
                     character ? character->slug : "unknown");
    }
}

static void VanillaDebug_SanitizeName(char *text)
{
    if (text == NULL)
        return;

    for (; *text; text++)
    {
        unsigned char c = (unsigned char)*text;
        if (!isalnum(c) && c != '_' && c != '-')
            *text = '_';
    }
}

void VanillaRacerSeparation_DebugBegin(const struct Instance *inst,
                                       const struct ModelHeader *mh)
{
    const struct CharacterDef *character;
    char debugDir[1024];
    char relative[512];
    char headerName[17];
    int characterID;

    VanillaDebug_CheckControl();

    if (s_vanillaDebugExport.completed ||
        s_vanillaDebugExport.obj != NULL ||
        s_vanillaDebugExport.requestedCharacterID < 0 ||
        inst == NULL || mh == NULL)
        return;

    characterID = VanillaMask_CharacterForInstance(inst);
    if (characterID != s_vanillaDebugExport.requestedCharacterID)
        return;

    character = CharacterRegistry_GetByID(characterID);
    if (character == NULL || (character->flags & CHARACTER_REGISTRY_FLAG_RETAIL) == 0)
        return;

    if (!NativeAssets_BuildPath("mods/vanilla_racer_masks/debug",
                                debugDir, sizeof(debugDir)))
        return;

    VanillaDebug_MakeDir(debugDir);

    memcpy(headerName, mh->name, 16);
    headerName[16] = 0;
    VanillaDebug_SanitizeName(headerName);

    snprintf(relative, sizeof(relative),
             "mods/vanilla_racer_masks/debug/%s_%s.obj",
             character->slug,
             headerName[0] ? headerName : "model");

    if (!NativeAssets_BuildPath(relative,
                                s_vanillaDebugExport.outputPath,
                                sizeof(s_vanillaDebugExport.outputPath)))
        return;

    s_vanillaDebugExport.obj =
        fopen(s_vanillaDebugExport.outputPath, "wb");
    if (s_vanillaDebugExport.obj == NULL)
        return;

    s_vanillaDebugExport.activeInst = inst;
    s_vanillaDebugExport.vertexCount = 0;
    s_vanillaDebugExport.triangleCount = 0;

    fprintf(s_vanillaDebugExport.obj,
            "# Crash Forge Racing vanilla racer primitive export\n");
    fprintf(s_vanillaDebugExport.obj,
            "# character=%s model_header=%s\n",
            character->slug, headerName);

    fprintf(s_vanillaDebugExport.obj,
            "# Blender polygon index equals CTR primitive index.\n");
    fprintf(s_vanillaDebugExport.obj,
            "# Select kart polygons in Blender, then export their polygon indices as .mask ranges.\n");
}

int VanillaRacerSeparation_DebugIsActiveFor(const struct Instance *inst)
{
    return (s_vanillaDebugExport.obj != NULL) &&
           (inst == s_vanillaDebugExport.activeInst);
}

void VanillaRacerSeparation_DebugTriangle(const struct Instance *inst,
                                          int primitiveIndex,
                                          int sourceA, int sourceB, int sourceC,
                                          int colorA, int colorB, int colorC,
                                          int ax, int ay, int az,
                                          int bx, int by, int bz,
                                          int cx, int cy, int cz)
{
    FILE *f = s_vanillaDebugExport.obj;
    int base;

    if (f == NULL || inst != s_vanillaDebugExport.activeInst)
        return;

    base = s_vanillaDebugExport.vertexCount + 1;

    fprintf(f, "# prim %d src %d %d %d\n",
            primitiveIndex, sourceA, sourceB, sourceC);
    fprintf(f, "v %d %d %d %.6f %.6f %.6f\n",
            ax, ay, az,
            (double)(colorA & 0xff) / 255.0,
            (double)((colorA >> 8) & 0xff) / 255.0,
            (double)((colorA >> 16) & 0xff) / 255.0);
    fprintf(f, "v %d %d %d %.6f %.6f %.6f\n",
            bx, by, bz,
            (double)(colorB & 0xff) / 255.0,
            (double)((colorB >> 8) & 0xff) / 255.0,
            (double)((colorB >> 16) & 0xff) / 255.0);
    fprintf(f, "v %d %d %d %.6f %.6f %.6f\n",
            cx, cy, cz,
            (double)(colorC & 0xff) / 255.0,
            (double)((colorC >> 8) & 0xff) / 255.0,
            (double)((colorC >> 16) & 0xff) / 255.0);
    fprintf(f, "f %d %d %d\n", base, base + 1, base + 2);

    s_vanillaDebugExport.vertexCount += 3;
    s_vanillaDebugExport.triangleCount++;
}

void VanillaRacerSeparation_DebugEnd(const struct Instance *inst)
{
    if (s_vanillaDebugExport.obj == NULL ||
        inst != s_vanillaDebugExport.activeInst)
        return;

    fprintf(s_vanillaDebugExport.obj,
            "# primitive_count=%d\n",
            s_vanillaDebugExport.triangleCount);
    fclose(s_vanillaDebugExport.obj);

    Platform_Log(
        "[VANILLA SEPARATION] exported %d primitives -> %s\n",
        s_vanillaDebugExport.triangleCount,
        s_vanillaDebugExport.outputPath);

    /* export.txt is a one-shot request. Remove it only after a successful
       export so a failed/missing racer request can be retried next run. */
    {
        char controlPath[1024];
        if (NativeAssets_BuildPath(
                "mods/vanilla_racer_masks/export.txt",
                controlPath, sizeof(controlPath)))
        {
            remove(controlPath);
        }
    }

    s_vanillaDebugExport.obj = NULL;
    s_vanillaDebugExport.activeInst = NULL;
    s_vanillaDebugExport.completed = 1;
}
