#ifndef NATIVE_GPU_MODEL_QA_H
#define NATIVE_GPU_MODEL_QA_H
struct NativeObjMesh;
/* Stress-only first stage of GPU-resident OBJ rendering.
 * Stage may run during PS1 packet construction; Flush must run on
 * the OpenGL render thread and never alters retail pixels. */
/* Samples exact existing PS1 rigid-OBJ GTE projections, then compares them
 * to GPU transform feedback from the ALREADY RESIDENT mesh VBO. Debug only. */
struct NativeGpuModelQASample
{
    int vertexIndex;
    float offset[3], scale[3], rot[9], translation[3];
    float h, centerX, centerY;
    int expectedX, expectedY, expectedDepth;
};
int NativeGpuModel_ProjectionQaActive(void);
void NativeGpuModel_StageRigidSample(const struct NativeObjMesh *mesh,
    const struct NativeGpuModelQASample *sample);
void NativeGpuModel_StageMesh(const struct NativeObjMesh *mesh);
void NativeGpuModel_FlushStaged(void);
#endif
