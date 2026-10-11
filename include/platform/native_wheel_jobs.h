#pragma once
#if defined(CTR_NATIVE)
#include <stdint.h>
struct NativeObjMesh;

/* Pure CPU wheel vertex preparation. One output per OBJ face, read-only
 * geometry inputs. The main thread retains all PS1 GTE/OT and OpenGL work. */
struct NativeWheelPreparedTriangle
{
    int16_t pos[3][3];
};
const struct NativeWheelPreparedTriangle *NativeWheelJobs_Prepare(
    const struct NativeObjMesh *mesh,
    const float angles[4],
    const float sine[4],
    const float cosine[4],
    float steerAngle,
    float steerSin,
    float steerCos,
    const float drawOffsetScaled[3],
    const float attachedScale[3]);
void NativeWheelJobs_Shutdown(void);
#endif
