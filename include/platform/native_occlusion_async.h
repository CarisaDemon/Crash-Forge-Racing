#pragma once

/*
 * Experimental immutable terrain occluder precomputation.
 *
 * The background thread never reads the live level, physics, GTE or OpenGL.
 * Call Ensure on the main thread; when its completed snapshot has the exact
 * level identity, GetBlock becomes available. Otherwise use retail ray tests.
 * Opt-in for QA only: CTR_OCC_ASYNC_QA=1
 */

#if defined(CTR_NATIVE)

struct mesh_info;
struct QuadBlock;

struct NativeOccAsyncTriangle
{
    float v0[3];
    float e1[3];
    float e2[3];
    unsigned char valid;
};

struct NativeOccAsyncBlock
{
    struct NativeOccAsyncTriangle triangles[8];
    unsigned char count;
};

void NativeOccAsync_Ensure(const struct mesh_info *mesh, int levelID);
const struct NativeOccAsyncBlock *NativeOccAsync_GetBlock(
    const struct mesh_info *mesh, const struct QuadBlock *quad);
int NativeOccAsync_VerifyEnabled(void);
void NativeOccAsync_DisableAfterMismatch(void);
void NativeOccAsync_Shutdown(void);

#endif
