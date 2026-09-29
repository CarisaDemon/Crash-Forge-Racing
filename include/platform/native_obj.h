#ifndef NATIVE_OBJ_H
#define NATIVE_OBJ_H

struct Model;
struct Instance;
struct Driver;
struct NativeObjVertex { float p[3], uv[2], color[3]; };
struct NativeObjTriangle { struct NativeObjVertex v[3]; int material, wheel; };
struct NativeObjWheel { float center[3], radius; };
struct NativeObjMaterial { char name[128]; unsigned int texture; float color[3], opacity; };
struct NativeObjMesh {
    struct NativeObjTriangle *triangles;
    struct NativeObjMaterial *materials;
    int triangleCount, materialCount;
    float extent[3];
    float userScale;
    float userOffset[3];
    short frameOrigin[3];
    struct NativeObjWheel wheels[4];
};
#define NATIVE_OBJ_MODEL_MAGIC 0x4f424a31
struct Model *NativeObj_LoadRacer(const char *assetName);
const struct NativeObjMesh *NativeObj_GetMesh(const struct Model *model);
void NativeObj_Shutdown(void);
void NativeObj_UpdateWheels(void);
void NativeObj_GetWheelAngles(const struct Instance *inst, float angles[4]);
float NativeObj_GetWheelSteerAngle(const struct Instance *inst);
const struct NativeObjMesh *NativeObj_GetRetailWheelOverrideMesh(void);
int NativeObj_ShouldReplaceRetailWheels(const struct Driver *driver);

#endif
