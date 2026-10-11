#ifndef VANILLA_RACER_PATCH_H
#define VANILLA_RACER_PATCH_H

struct NativeObjMesh;

void VanillaRacerPatch_Shutdown(void);
void VanillaRacerPatch_BeginCapture(const struct Instance *inst,
                                    const struct ModelHeader *mh);
void VanillaRacerPatch_CaptureVertex(const struct Instance *inst,
                                     int sourceIndex,
                                     int x, int y, int z);

b32 VanillaRacerPatch_CanDetach(const struct Instance *inst,
                                const struct ModelHeader *mh);
b32 VanillaRacerPatch_ShouldHidePrimitive(const struct Instance *inst,
                                          const struct ModelHeader *mh,
                                          int primitiveIndex);

/* Native draw path: resolve the selected kart/mask once per model, rather
 * than repeating racer lookup, model-name matching and loadout checks for
 * every polygon. The payload is opaque to RenderBucket callers. */
struct VanillaRacerPatchDrawFilter
{
    const void *readyVariant;
};
void VanillaRacerPatch_InitDrawFilter(
    struct VanillaRacerPatchDrawFilter *out,
    const struct Instance *inst,
    const struct ModelHeader *mh);
b32 VanillaRacerPatch_ShouldHideFilteredPrimitive(
    const struct VanillaRacerPatchDrawFilter *filter,
    int primitiveIndex);

const struct NativeObjMesh *VanillaRacerPatch_GetBodyMesh(
    const struct Instance *inst,
    const struct ModelHeader *mh);
const struct NativeObjMesh *VanillaRacerPatch_GetKartMesh(
    const struct Instance *inst,
    const struct ModelHeader *mh);

b32 VanillaRacerPatch_GetSeatOffset(const struct Instance *inst,
                                    const struct ModelHeader *mh,
                                    float out[3]);

b32 VanillaRacerPatch_IsBodyMesh(const struct Instance *inst,
                                 const struct ModelHeader *mh,
                                 const struct NativeObjMesh *mesh);
b32 VanillaRacerPatch_ResolveVertex(const struct Instance *inst,
                                    const struct ModelHeader *mh,
                                    const struct NativeObjMesh *mesh,
                                    int objVertexIndex,
                                    float out[3]);

#endif
