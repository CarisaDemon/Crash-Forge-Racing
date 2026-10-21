#ifndef NATIVE_OBJ_H
#define NATIVE_OBJ_H

struct Model;
struct Instance;
struct Driver;
struct NativeObjVertex {
    float p[3], uv[2], color[3];
    float renderP[3];
    /* Custom OBJ textures can be 1K/2K+, while the embedded PS1 GT3 UV
     * fields are only 8-bit. Keep the nearest legacy byte plus a signed
     * 1/256-texel residual so native RGBA materials preserve Blender UVs. */
    unsigned char renderUV[2];
    signed char renderUVFrac[2];
    unsigned char renderColor[3];
    int objIndex;
    /* Cache geometry-only projections using a shared index for identical
     * positions. Keep objIndex untouched for binding/animation metadata. */
    int projectionIndex;
};
struct NativeObjTriangle {
    struct NativeObjVertex v[3];
    int material, wheel;
    unsigned int renderTexture;
    unsigned char renderTextured;
};
struct NativeObjWheel { float center[3], radius; };
struct NativeObjMaterial {
    char name[128];
    unsigned int texture;
    float color[3], opacity;

    /* OBJ loader-only texture source + atlas transform. sourcePixels is freed
     * once loading/atlas construction finishes; glTF materials leave it NULL. */
    unsigned char *sourcePixels;
    int sourceWidth, sourceHeight;
    float uvScale[2];
    float uvOffset[2];
};
struct NativeObjProjectedVertex {
    unsigned int sxy;
    unsigned short depth;
    unsigned short pad;
    unsigned int generation;
    /* Exact native subpixel projection cached once per unique rigid OBJ vertex.
     * This lets backface culling use the precise position directly instead of
     * re-probing the GTE address map for every triangle that references it. */
    int x16;
    int y16;
    /* Pre-encoded native B3 coordinates. Wheels reuse a projected vertex
     * across many faces, so quantize it only on a cache miss. */
    short packetX;
    short packetY;
    signed char packetXFrac;
    signed char packetYFrac;
};
#define NATIVE_OBJ_MAX_COLORS 12
struct NativeObjMesh {
    struct NativeObjTriangle *triangles;
    struct NativeObjMaterial *materials;
    int triangleCount, materialCount;
    unsigned int atlasTexture;
    /* Alternate GPU image handles share the exact same OBJ, materials,
       triangles and UV projection cache. Slot zero is the original MTL. */
    unsigned int colorBaseTexture;
    unsigned int colorTextures[NATIVE_OBJ_MAX_COLORS];
    unsigned char colorCount;
    unsigned char colorsPrepared;
    float extent[3];
    float authPerNorm[3];
    float normPerAuth[3];
    float renderOffsetScaled[3];
    float userScale;
    float userOffset[3];
    /* Standalone CNK Tornado additive texture, not regular OBJ materials. */
    unsigned char additiveEffect;
    short frameOrigin[3];
    unsigned int wheelMask;
    struct NativeObjWheel wheels[4];
    int positionCount;
    unsigned int projectionGeneration;
    struct NativeObjProjectedVertex *projectionCache;
};
#define NATIVE_OBJ_MODEL_MAGIC 0x4f424a31
struct Model *NativeObj_LoadRacer(const char *assetName);
/* Dedicated native weapon model; cannot override CTR's Warp Orb model. */
struct Model *NativeObj_LoadCnkTornado(void);
struct Model *NativeObj_LoadStaticTrack(const char *slug);
int NativeObj_CustomTrack_IsActive(void);
void NativeObj_CustomTrack_Update(struct GameTracker *gt);
void NativeObj_CustomTrack_ResetRuntime(void);
int NativeObj_CustomTrack_VerticalProbe(int x, int yA, int yB, int z, struct ScratchpadStruct *sps);
int NativeObj_CustomTrack_FixedSearch(struct Driver *d, struct ScratchpadStruct *sps);
int NativeObj_CustomTrack_MovedSearch(struct Driver *d, struct ScratchpadStruct *sps);
int NativeObj_CustomTrack_UpdateProgress(struct Driver *d);
int NativeObj_CustomTrack_GetSpawn(int *x, int *y, int *z, int *rotY);
struct Model *NativeObj_LoadVehiclePart(int kind, int partID);
struct Model *NativeObj_LoadVanillaBodyPatch(const char *characterSlug, const char *headerName);
const struct NativeObjMesh *NativeObj_GetMesh(const struct Model *model);
void NativeObj_Shutdown(void);
void NativeObj_UpdateWheels(void);
void NativeObj_GetWheelState(const struct Instance *inst, float angles[4], float *steerAngle);
void NativeObj_GetWheelAngles(const struct Instance *inst, float angles[4]);
float NativeObj_GetWheelSteerAngle(const struct Instance *inst);
const struct NativeObjMesh *NativeObj_GetRetailWheelOverrideMesh(void);
const struct NativeObjMesh *NativeObj_GetWheelMeshForDriver(const struct Driver *driver);
const struct NativeObjMesh *NativeObj_GetKartMeshForDriver(const struct Driver *driver);
const struct NativeObjMesh *NativeObj_GetKartMeshForInstance(const struct Instance *inst);
unsigned int NativeObj_GetKartColorTextureForInstance(
    const struct Instance *inst, const struct NativeObjMesh *mesh);
b32 NativeObj_GetSeatOffsetForInstance(const struct Instance *inst, float out[3]);
const struct NativeObjMesh *NativeObj_GetModularKartMeshForInstance(const struct Instance *inst);
b32 NativeObj_GetModularBodySeatOffset(const struct Instance *inst, float out[3]);
void NativeObj_SetRetailWheelOverrideAsset(const char *assetName);
const char *NativeObj_GetRetailWheelOverrideAsset(void);
int NativeObj_ShouldReplaceRetailWheels(const struct Driver *driver);

#endif
