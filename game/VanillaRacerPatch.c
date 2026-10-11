#include <common.h>
#include <VanillaRacerPatch.h>
#include <CharacterRegistry.h>
#include <RacerLoadout.h>
#include <VehiclePartRegistry.h>
#include <platform/native_assets.h>
#include <platform/native_obj.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VANILLA_PATCH_MAX_VARIANTS 64
#define VANILLA_PATCH_MAX_RANGES 256
#define VANILLA_PATCH_MAX_ANCHORS 256

struct VanillaPatchRange
{
    int first;
    int last;
};

struct VanillaPatchBinding
{
    u8 valid;
    s16 anchorSlot[3];
    float weight[3];
    float offset[3];
};

struct VanillaPatchVariant
{
    int used;
    int checked;
    int ready;
    int characterID;
    char headerName[17];

    struct VanillaPatchRange ranges[VANILLA_PATCH_MAX_RANGES];
    int rangeCount;

    const struct NativeObjMesh *bodyMesh;
    struct VanillaPatchBinding *bindings;
    int bindingCount;

    int anchorCount;
    int anchorSource[VANILLA_PATCH_MAX_ANCHORS];
};

struct VanillaPatchCapture
{
    const struct Instance *inst;
    struct VanillaPatchVariant *variant;
    u8 hasSeatOffset;
    float seatOffset[3];
    u8 seen[VANILLA_PATCH_MAX_ANCHORS];
    float position[VANILLA_PATCH_MAX_ANCHORS][3];
};

static struct VanillaPatchVariant
    s_patchVariants[VANILLA_PATCH_MAX_VARIANTS];
static struct VanillaPatchCapture
    s_patchCaptures[RACER_LOADOUT_MAX_DRIVERS];

static char *VanillaPatch_Trim(char *text)
{
    char *end;

    while (*text && isspace((unsigned char)*text))
        text++;

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        end--;

    *end = 0;
    return text;
}

static void VanillaPatch_HeaderName(const struct ModelHeader *mh,
                                    char out[17])
{
    if (out == NULL)
        return;

    memset(out, 0, 17);
    if (mh == NULL)
        return;

    memcpy(out, mh->name, 16);
    out[16] = 0;

    for (int i = 0; out[i]; i++)
    {
        unsigned char c = (unsigned char)out[i];
        if (!isalnum(c) && c != '_' && c != '-')
            out[i] = '_';
    }

    if (!out[0])
        snprintf(out, 17, "model");
}

static struct Driver *VanillaPatch_GetDriver(
    const struct Instance *inst,
    int *outDriverIndex)
{
    struct GameTracker *gGT = sdata->gGT;

    if (outDriverIndex != NULL)
        *outDriverIndex = -1;

    if (inst == NULL || gGT == NULL)
        return NULL;

    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
    {
        struct Driver *driver = gGT->drivers[i];
        if (driver != NULL && driver->instSelf == inst)
        {
            if (outDriverIndex != NULL)
                *outDriverIndex = i;
            return driver;
        }
    }

    return NULL;
}

static int VanillaPatch_AddAnchor(struct VanillaPatchVariant *variant,
                                  int sourceIndex)
{
    for (int i = 0; i < variant->anchorCount; i++)
    {
        if (variant->anchorSource[i] == sourceIndex)
            return i;
    }

    if (variant->anchorCount >= VANILLA_PATCH_MAX_ANCHORS)
        return -1;

    variant->anchorSource[variant->anchorCount] = sourceIndex;
    return variant->anchorCount++;
}
static int VanillaPatch_LoadMask(struct VanillaPatchVariant *variant,
                                 const char *slug)
{
    char relative[512];
    char path[1024];
    char line[256];
    FILE *f;

    snprintf(relative, sizeof(relative),
             "mods/vanilla_racer_masks/%s_%s.mask",
             slug, variant->headerName);

    if (!NativeAssets_BuildPath(relative, path, sizeof(path)))
        return 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return 0;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *text = VanillaPatch_Trim(line);
        int first;
        int last;

        if (!*text || *text == '#' || *text == ';')
            continue;
        if (variant->rangeCount >= VANILLA_PATCH_MAX_RANGES)
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

        variant->ranges[variant->rangeCount].first = first;
        variant->ranges[variant->rangeCount].last = last;
        variant->rangeCount++;
    }

    fclose(f);
    return variant->rangeCount > 0;
}

static int VanillaPatch_LoadBindings(struct VanillaPatchVariant *variant,
                                     const char *slug)
{
    char relative[512];
    char path[1024];
    char line[512];
    struct Model *patchModel;
    int maxObjIndex = -1;
    FILE *f;

    patchModel =
        NativeObj_LoadVanillaBodyPatch(slug, variant->headerName);
    if (patchModel == NULL)
        return 0;

    variant->bodyMesh = NativeObj_GetMesh(patchModel);
    if (variant->bodyMesh == NULL ||
        variant->bodyMesh->triangleCount <= 0)
        return 0;

    for (int i = 0; i < variant->bodyMesh->triangleCount; i++)
    {
        const struct NativeObjTriangle *tri =
            &variant->bodyMesh->triangles[i];

        for (int j = 0; j < 3; j++)
        {
            if (tri->v[j].objIndex > maxObjIndex)
                maxObjIndex = tri->v[j].objIndex;
        }
    }

    if (maxObjIndex < 0 || maxObjIndex > 65535)
        return 0;

    variant->bindingCount = maxObjIndex + 1;
    variant->bindings = calloc(
        (size_t)variant->bindingCount,
        sizeof(*variant->bindings));
    if (variant->bindings == NULL)
        return 0;

    snprintf(relative, sizeof(relative),
             "mods/vanilla_racer_masks/patches/%s/%s/body_patch.bind",
             slug, variant->headerName);

    if (!NativeAssets_BuildPath(relative, path, sizeof(path)))
        return 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return 0;

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *text = VanillaPatch_Trim(line);
        int patchVertex;
        int source[3];
        float weight[3];
        float offset[3];
        float sum;
        struct VanillaPatchBinding *binding;

        if (!*text || *text == '#' || *text == ';')
            continue;

        if (sscanf(text,
                   "v %d %d %d %d %f %f %f %f %f %f",
                   &patchVertex,
                   &source[0], &source[1], &source[2],
                   &weight[0], &weight[1], &weight[2],
                   &offset[0], &offset[1], &offset[2]) != 10)
            continue;

        patchVertex--;
        if (patchVertex < 0 ||
            patchVertex >= variant->bindingCount)
            continue;

        sum = weight[0] + weight[1] + weight[2];
        if (source[0] < 0 || source[1] < 0 || source[2] < 0 ||
            !isfinite(sum) || sum <= 0.000001f)
            continue;

        binding = &variant->bindings[patchVertex];
        memset(binding, 0, sizeof(*binding));

        for (int i = 0; i < 3; i++)
        {
            int slot;

            if (!isfinite(weight[i]) || weight[i] < 0.0f ||
                !isfinite(offset[i]))
            {
                binding->valid = 0;
                break;
            }

            slot = VanillaPatch_AddAnchor(variant, source[i]);
            if (slot < 0)
            {
                binding->valid = 0;
                break;
            }

            binding->anchorSlot[i] = (s16)slot;
            binding->weight[i] = weight[i] / sum;
            binding->offset[i] = offset[i];
            binding->valid = 1;
        }
    }

    fclose(f);

    for (int i = 0; i < variant->bodyMesh->triangleCount; i++)
    {
        const struct NativeObjTriangle *tri =
            &variant->bodyMesh->triangles[i];

        for (int j = 0; j < 3; j++)
        {
            int objIndex = tri->v[j].objIndex;
            if (objIndex < 0 ||
                objIndex >= variant->bindingCount ||
                !variant->bindings[objIndex].valid)
                return 0;
        }
    }

    return variant->anchorCount > 0;
}
static void VanillaPatch_LoadVariant(
    struct VanillaPatchVariant *variant)
{
    const struct CharacterDef *character;

    if (variant == NULL || variant->checked)
        return;

    variant->checked = 1;

    character = CharacterRegistry_GetByID(variant->characterID);
    if (character == NULL ||
        (character->flags & CHARACTER_REGISTRY_FLAG_RETAIL) == 0)
        return;

    if (!VanillaPatch_LoadMask(variant, character->slug))
        return;

    /* The retail character stays on CTR's original animated geometry.
       The old body_patch.bind path is now optional legacy data instead of a
       hard requirement for kart separation. If it exists we still load it so
       older authoring experiments remain inspectable, but a precise per-LOD
       kart mask is enough to detach the original kart safely. */
    (void)VanillaPatch_LoadBindings(variant, character->slug);

    variant->ready = 1;

    Platform_Log(
        "[VANILLA PATCH] ready character=%s header=%s "
        "ranges=%d legacyPatch=%s\n",
        character->slug,
        variant->headerName,
        variant->rangeCount,
        variant->bodyMesh ? "yes" : "no");
}

static struct VanillaPatchVariant *VanillaPatch_GetVariant(
    int characterID,
    const struct ModelHeader *mh)
{
    char headerName[17];

    if (characterID < 0 || mh == NULL)
        return NULL;

    VanillaPatch_HeaderName(mh, headerName);

    for (int i = 0; i < VANILLA_PATCH_MAX_VARIANTS; i++)
    {
        struct VanillaPatchVariant *variant = &s_patchVariants[i];

        if (variant->used &&
            variant->characterID == characterID &&
            !strcmp(variant->headerName, headerName))
        {
            VanillaPatch_LoadVariant(variant);
            return variant;
        }
    }

    for (int i = 0; i < VANILLA_PATCH_MAX_VARIANTS; i++)
    {
        struct VanillaPatchVariant *variant = &s_patchVariants[i];

        if (variant->used)
            continue;

        memset(variant, 0, sizeof(*variant));
        variant->used = 1;
        variant->characterID = characterID;
        snprintf(variant->headerName,
                 sizeof(variant->headerName),
                 "%s", headerName);

        VanillaPatch_LoadVariant(variant);
        return variant;
    }

    return NULL;
}

static struct VanillaPatchVariant *VanillaPatch_GetReadyVariant(
    const struct Instance *inst,
    const struct ModelHeader *mh,
    struct Driver **outDriver,
    int *outDriverIndex)
{
    struct Driver *driver;
    struct VanillaPatchVariant *variant;
    int driverIndex;
    int characterID;
    int kartID;

    if (outDriver != NULL)
        *outDriver = NULL;
    if (outDriverIndex != NULL)
        *outDriverIndex = -1;

    if (RacerLoadout_IsAdventureLocked())
        return NULL;

    driver = VanillaPatch_GetDriver(inst, &driverIndex);
    if (driver == NULL)
        return NULL;

    kartID = RacerLoadout_GetEffectiveKart(driverIndex);
    if (kartID < 0)
        return NULL;

    characterID = data.characterIDs[driverIndex];
    variant = VanillaPatch_GetVariant(characterID, mh);
    if (variant == NULL || !variant->ready)
        return NULL;

    /* Never remove the retail kart unless the selected replacement itself
       loaded successfully. */
    if (NativeObj_GetKartMeshForDriver(driver) == NULL)
        return NULL;

    if (outDriver != NULL)
        *outDriver = driver;
    if (outDriverIndex != NULL)
        *outDriverIndex = driverIndex;

    return variant;
}
b32 VanillaRacerPatch_CanDetach(const struct Instance *inst,
                                const struct ModelHeader *mh)
{
    return VanillaPatch_GetReadyVariant(
               inst, mh, NULL, NULL) != NULL;
}

void VanillaRacerPatch_InitDrawFilter(
    struct VanillaRacerPatchDrawFilter *out,
    const struct Instance *inst,
    const struct ModelHeader *mh)
{
    if (out == NULL)
        return;

    /* Resolve once when starting this model's draw. Never persist this
       pointer across frames, loadout changes or asset reloads. */
    out->readyVariant = VanillaPatch_GetReadyVariant(inst, mh, NULL, NULL);
}

b32 VanillaRacerPatch_ShouldHideFilteredPrimitive(
    const struct VanillaRacerPatchDrawFilter *filter,
    int primitiveIndex)
{
    const struct VanillaPatchVariant *variant =
        filter != NULL ? (const struct VanillaPatchVariant *)filter->readyVariant : NULL;

    if (variant == NULL || primitiveIndex < 0)
        return false;

    for (int i = 0; i < variant->rangeCount; i++)
    {
        if (primitiveIndex >= variant->ranges[i].first &&
            primitiveIndex <= variant->ranges[i].last)
            return true;
    }

    return false;
}

b32 VanillaRacerPatch_ShouldHidePrimitive(
    const struct Instance *inst,
    const struct ModelHeader *mh,
    int primitiveIndex)
{
    struct VanillaRacerPatchDrawFilter filter;
    VanillaRacerPatch_InitDrawFilter(&filter, inst, mh);
    return VanillaRacerPatch_ShouldHideFilteredPrimitive(&filter, primitiveIndex);
}

void VanillaRacerPatch_BeginCapture(const struct Instance *inst,
                                    const struct ModelHeader *mh)
{
    struct VanillaPatchVariant *variant;
    int driverIndex;

    if (VanillaPatch_GetDriver(inst, &driverIndex) == NULL ||
        driverIndex < 0 ||
        driverIndex >= RACER_LOADOUT_MAX_DRIVERS)
        return;

    memset(&s_patchCaptures[driverIndex], 0,
           sizeof(s_patchCaptures[driverIndex]));

    variant = VanillaPatch_GetReadyVariant(
        inst, mh, NULL, NULL);
    if (variant == NULL)
        return;

    s_patchCaptures[driverIndex].inst = inst;
    s_patchCaptures[driverIndex].variant = variant;

    {
        int kartID = RacerLoadout_GetEffectiveKart(driverIndex);
        float seat[3];

        if (kartID >= 0 &&
            VehiclePartRegistry_GetSeatOffset(kartID, seat))
        {
            s_patchCaptures[driverIndex].hasSeatOffset = 1;
            s_patchCaptures[driverIndex].seatOffset[0] = seat[0];
            s_patchCaptures[driverIndex].seatOffset[1] = seat[1];
            s_patchCaptures[driverIndex].seatOffset[2] = seat[2];
        }
    }
}

void VanillaRacerPatch_CaptureVertex(const struct Instance *inst,
                                     int sourceIndex,
                                     int x, int y, int z)
{
    int driverIndex;
    struct VanillaPatchCapture *capture;
    struct VanillaPatchVariant *variant;

    if (sourceIndex < 0 ||
        VanillaPatch_GetDriver(inst, &driverIndex) == NULL ||
        driverIndex < 0 ||
        driverIndex >= RACER_LOADOUT_MAX_DRIVERS)
        return;

    capture = &s_patchCaptures[driverIndex];
    variant = capture->variant;

    if (capture->inst != inst || variant == NULL)
        return;

    for (int i = 0; i < variant->anchorCount; i++)
    {
        if (variant->anchorSource[i] != sourceIndex)
            continue;

        capture->seen[i] = 1;
        capture->position[i][0] = (float)x;
        capture->position[i][1] = (float)y;
        capture->position[i][2] = (float)z;
        return;
    }
}

const struct NativeObjMesh *VanillaRacerPatch_GetBodyMesh(
    const struct Instance *inst,
    const struct ModelHeader *mh)
{
    struct VanillaPatchVariant *variant =
        VanillaPatch_GetReadyVariant(inst, mh, NULL, NULL);

    return variant ? variant->bodyMesh : NULL;
}

const struct NativeObjMesh *VanillaRacerPatch_GetKartMesh(
    const struct Instance *inst,
    const struct ModelHeader *mh)
{
    struct Driver *driver = NULL;

    if (VanillaPatch_GetReadyVariant(
            inst, mh, &driver, NULL) == NULL)
        return NULL;

    return NativeObj_GetKartMeshForDriver(driver);
}

b32 VanillaRacerPatch_GetSeatOffset(const struct Instance *inst,
                                    const struct ModelHeader *mh,
                                    float out[3])
{
    int driverIndex = -1;

    if (out != NULL)
        out[0] = out[1] = out[2] = 0.0f;

    if (out == NULL ||
        VanillaPatch_GetReadyVariant(
            inst, mh, NULL, &driverIndex) == NULL ||
        driverIndex < 0 ||
        driverIndex >= RACER_LOADOUT_MAX_DRIVERS)
        return false;

    {
        int kartID = RacerLoadout_GetEffectiveKart(driverIndex);
        return kartID >= 0 &&
               VehiclePartRegistry_GetSeatOffset(kartID, out);
    }
}

b32 VanillaRacerPatch_IsBodyMesh(
    const struct Instance *inst,
    const struct ModelHeader *mh,
    const struct NativeObjMesh *mesh)
{
    const struct NativeObjMesh *body =
        VanillaRacerPatch_GetBodyMesh(inst, mh);

    return body != NULL && body == mesh;
}
b32 VanillaRacerPatch_ResolveVertex(
    const struct Instance *inst,
    const struct ModelHeader *mh,
    const struct NativeObjMesh *mesh,
    int objVertexIndex,
    float out[3])
{
    struct VanillaPatchVariant *variant;
    struct VanillaPatchCapture *capture;
    struct VanillaPatchBinding *binding;
    int driverIndex;

    if (out == NULL ||
        VanillaPatch_GetDriver(inst, &driverIndex) == NULL ||
        driverIndex < 0 ||
        driverIndex >= RACER_LOADOUT_MAX_DRIVERS)
        return false;

    variant = VanillaPatch_GetReadyVariant(
        inst, mh, NULL, NULL);
    if (variant == NULL ||
        variant->bodyMesh != mesh ||
        objVertexIndex < 0 ||
        objVertexIndex >= variant->bindingCount)
        return false;

    capture = &s_patchCaptures[driverIndex];
    if (capture->inst != inst ||
        capture->variant != variant)
        return false;

    binding = &variant->bindings[objVertexIndex];
    if (!binding->valid)
        return false;

    out[0] = binding->offset[0];
    out[1] = binding->offset[1];
    out[2] = binding->offset[2];

    for (int i = 0; i < 3; i++)
    {
        int slot = binding->anchorSlot[i];

        if (slot < 0 ||
            slot >= variant->anchorCount ||
            !capture->seen[slot])
            return false;

        out[0] += capture->position[slot][0] *
                  binding->weight[i];
        out[1] += capture->position[slot][1] *
                  binding->weight[i];
        out[2] += capture->position[slot][2] *
                  binding->weight[i];
    }

    if (capture->hasSeatOffset)
    {
        out[0] += capture->seatOffset[0];
        out[1] += capture->seatOffset[1];
        out[2] += capture->seatOffset[2];
    }

    return true;
}

void VanillaRacerPatch_Shutdown(void)
{
    for (int i = 0; i < VANILLA_PATCH_MAX_VARIANTS; i++)
    {
        free(s_patchVariants[i].bindings);
        s_patchVariants[i].bindings = NULL;
    }

    memset(s_patchVariants, 0, sizeof(s_patchVariants));
    memset(s_patchCaptures, 0, sizeof(s_patchCaptures));
}
