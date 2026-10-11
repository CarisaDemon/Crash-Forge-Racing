#ifndef NATIVE_GLTF_H
#define NATIVE_GLTF_H

struct Model;
struct Instance;
struct NativeObjMesh;

#define NATIVE_GLTF_MODEL_MAGIC 0x474c5431

struct Model *NativeGltf_LoadRacer(int characterID);
b32 NativeGltf_IsModel(const struct Model *model);
const struct NativeObjMesh *NativeGltf_GetRenderMesh(const struct Instance *inst);
int NativeGltf_GetCompatFrameCount(const struct Instance *inst, int animIndex);
void NativeGltf_PrepareRegistryRefresh(void);
void NativeGltf_Shutdown(void);

#endif
