/* Crash Forge Racing experimental multi-core wheel geometry preparation.
 * Worker tasks ONLY read frozen OBJ mesh + per-instance float parameters,
 * writing nonoverlapping scratch triangles. No GTE/SDL-GL/OT game state in
 * workers; after a synchronous barrier, the main render thread is unchanged.
 * Disabled by default. Exact output parity can be independently checked.
 */
#include <common.h>
#include <platform/native_obj.h>
#include <platform/native_jobs.h>
#include <platform/native_wheel_jobs.h>
#include <platform/native_log.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(CTR_NATIVE)

struct NativeWheelJobContext
{
    const struct NativeObjMesh *mesh;
    float angles[4];
    float sine[4];
    float cosine[4];
    float steerAngle, steerSin, steerCos;
    float drawOffsetScaled[3];
    float attachedScale[3];
    struct NativeWheelPreparedTriangle *out;
};

static struct NativeWheelPreparedTriangle *s_nativeWheelScratch;
static size_t s_nativeWheelCapacity;
static int s_nativeWheelQAChecked;
static int s_nativeWheelQAEnabled;
static int s_nativeWheelVerifyEnabled;
static int s_nativeWheelQARejected;
static uint64_t s_nativeWheelTotalTriangles;
static uint64_t s_nativeWheelCalls;
static double s_nativeWheelJobMS;

static int NativeWheelJobs_FlagPresent(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int NativeWheelJobs_IsEnabled(void)
{
#if defined(CTR_INTERNAL)
    if (!s_nativeWheelQAChecked)
    {
        s_nativeWheelQAEnabled =
            NativeWheelJobs_FlagPresent("debug/wheel_transform_parallel_qa.enable");
        s_nativeWheelVerifyEnabled =
            NativeWheelJobs_FlagPresent("debug/wheel_transform_verify_qa.enable");
        s_nativeWheelQAChecked = 1;
        if (s_nativeWheelQAEnabled)
            Platform_Log("[WHEEL THREAD QA] opt-in pure-math worker prepass enabled; workers=%d\n",
                NativeJobs_GetWorkerCount());
    }
    return s_nativeWheelQAEnabled && !s_nativeWheelQARejected;
#else
    return 0;
#endif
}

/* Match the original RenderBucket_DrawObj operations/order, including the
 * saturate-to-s16 step. No approximations or SIMD fast-math. */
static void NativeWheelJobs_TransformTriangle(
    const struct NativeWheelJobContext *ctx,
    int index,
    struct NativeWheelPreparedTriangle *dst)
{
    const struct NativeObjTriangle *t = &ctx->mesh->triangles[index];
    const int wheel = t->wheel;
    if (wheel < 0 || wheel > 3)
        return;
    const float *center = ctx->mesh->wheels[wheel].center;
    for (int j = 0; j < 3; ++j)
    {
        float x = t->v[j].p[0];
        float y = t->v[j].p[1];
        float z = t->v[j].p[2];

        if (ctx->angles[wheel] != 0.0f)
        {
            const float dy = y * ctx->mesh->authPerNorm[1] - center[1];
            const float dz = z * ctx->mesh->authPerNorm[2] - center[2];
            y = (center[1] + dy * ctx->cosine[wheel] -
                 dz * ctx->sine[wheel]) * ctx->mesh->normPerAuth[1];
            z = (center[2] + dy * ctx->sine[wheel] +
                 dz * ctx->cosine[wheel]) * ctx->mesh->normPerAuth[2];
        }

        if ((wheel < 2) && (ctx->steerAngle != 0.0f))
        {
            const float dx = x * ctx->mesh->authPerNorm[0] - center[0];
            const float dz = z * ctx->mesh->authPerNorm[2] - center[2];
            x = (center[0] + dx * ctx->steerCos +
                 dz * ctx->steerSin) * ctx->mesh->normPerAuth[0];
            z = (center[2] - dx * ctx->steerSin +
                 dz * ctx->steerCos) * ctx->mesh->normPerAuth[2];
        }

        x = (x * ctx->mesh->userScale + ctx->drawOffsetScaled[0]) * ctx->attachedScale[0];
        y = (y * ctx->mesh->userScale + ctx->drawOffsetScaled[1]) * ctx->attachedScale[1];
        z = (z * ctx->mesh->userScale + ctx->drawOffsetScaled[2]) * ctx->attachedScale[2];

        if (x < -32767.0f) x = -32767.0f;
        else if (x > 32767.0f) x = 32767.0f;
        if (y < -32767.0f) y = -32767.0f;
        else if (y > 32767.0f) y = 32767.0f;
        if (z < -32767.0f) z = -32767.0f;
        else if (z > 32767.0f) z = 32767.0f;

        dst->pos[j][0] = (int16_t)x;
        dst->pos[j][1] = (int16_t)y;
        dst->pos[j][2] = (int16_t)z;
    }
}

static void NativeWheelJobs_DoRange(int first, int last, void *opaque)
{
    struct NativeWheelJobContext *ctx = (struct NativeWheelJobContext *)opaque;
    for (int i = first; i < last; ++i)
    {
        if (ctx->mesh->triangles[i].wheel >= 0 &&
            ctx->mesh->triangles[i].wheel < 4)
            NativeWheelJobs_TransformTriangle(ctx, i, &ctx->out[i]);
    }
}

const struct NativeWheelPreparedTriangle *NativeWheelJobs_Prepare(
    const struct NativeObjMesh *mesh,
    const float angles[4], const float sine[4], const float cosine[4],
    float steerAngle, float steerSin, float steerCos,
    const float drawOffsetScaled[3], const float attachedScale[3])
{
    struct NativeWheelJobContext ctx;
    size_t count;
    uint64_t t0,t1;
    if (!NativeWheelJobs_IsEnabled() ||
        !mesh || !mesh->triangles || mesh->triangleCount < 256 ||
        mesh->wheelMask == 0 || NativeJobs_GetWorkerCount() < 2)
        return NULL;
    count = (size_t)mesh->triangleCount;
    if (count > SIZE_MAX / sizeof(*s_nativeWheelScratch))
        return NULL;
    if (count > s_nativeWheelCapacity)
    {
        struct NativeWheelPreparedTriangle *buf =
            realloc(s_nativeWheelScratch, count * sizeof(*buf));
        if (!buf) return NULL;
        s_nativeWheelScratch = buf;
        s_nativeWheelCapacity = count;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.mesh = mesh;
    ctx.out = s_nativeWheelScratch;
    ctx.steerAngle = steerAngle;
    ctx.steerSin = steerSin;
    ctx.steerCos = steerCos;
    memcpy(ctx.angles, angles, sizeof(ctx.angles));
    memcpy(ctx.sine, sine, sizeof(ctx.sine));
    memcpy(ctx.cosine, cosine, sizeof(ctx.cosine));
    memcpy(ctx.drawOffsetScaled, drawOffsetScaled, sizeof(ctx.drawOffsetScaled));
    memcpy(ctx.attachedScale, attachedScale, sizeof(ctx.attachedScale));

    t0 = SDL_GetPerformanceCounter();
    if (!NativeJobs_ParallelFor(mesh->triangleCount, 56,
                                NativeWheelJobs_DoRange, &ctx))
        return NULL;
    t1 = SDL_GetPerformanceCounter();

    /* Exact mechanical parity check, separately enabled for debugging.
     * Returning NULL falls back to the untouched retail loop immediately. */
    if (s_nativeWheelVerifyEnabled &&
        ((s_nativeWheelCalls < 6) || ((s_nativeWheelCalls % 512u) == 0)))
    {
        for (int i = 0; i < mesh->triangleCount; ++i)
        {
            if (mesh->triangles[i].wheel < 0 || mesh->triangles[i].wheel >= 4)
                continue;
            struct NativeWheelPreparedTriangle expected = {{{0}}};
            NativeWheelJobs_TransformTriangle(&ctx, i, &expected);
            if (memcmp(&expected, &ctx.out[i], sizeof(expected)) != 0)
            {
                Platform_Log("[WHEEL THREAD QA] PARITY MISMATCH triangle=%d; disabling workers\n", i);
                s_nativeWheelQARejected = 1;
                return NULL;
            }
        }
    }

    ++s_nativeWheelCalls;
    s_nativeWheelTotalTriangles += count;
    s_nativeWheelJobMS +=
        (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if ((s_nativeWheelCalls % 768u) == 0)
        Platform_Log("[WHEEL THREAD QA] calls=%llu faces=%llu prepass_mean_ms=%.4f workers=%d\n",
            (unsigned long long)s_nativeWheelCalls,
            (unsigned long long)s_nativeWheelTotalTriangles,
            s_nativeWheelJobMS / (double)s_nativeWheelCalls,
            NativeJobs_GetWorkerCount());
    return s_nativeWheelScratch;
}

void NativeWheelJobs_Shutdown(void)
{
    free(s_nativeWheelScratch);
    s_nativeWheelScratch = NULL;
    s_nativeWheelCapacity = 0;
    s_nativeWheelQAChecked = 0;
    s_nativeWheelQAEnabled = 0;
    s_nativeWheelVerifyEnabled = 0;
    s_nativeWheelQARejected = 0;
}
#endif
