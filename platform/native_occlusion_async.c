#include <platform/native_occlusion_async.h>

#if defined(CTR_NATIVE)

/*
 * Dedicated, optional LOW-priority worker for immutable terrain data.
 *
 * The main thread copies the source mesh in 16 ms budgeted slices. The worker
 * reads only these copies and builds edge vectors. Ownership is transferred
 * to the main thread only after SDL's atomic completion flag is published.
 * No stale PVS/camera state, no GL calls, and absolutely no changes to the
 * game's 30 Hz simulation are involved.
 */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define NATIVE_OCC_ASYNC_MAX_QUADS 40000
#define NATIVE_OCC_ASYNC_MAX_VERTS 100000
#define NATIVE_OCC_ASYNC_VERTEX_SLICE 2048
#define NATIVE_OCC_ASYNC_QUAD_SLICE 384

struct NativeOccAsyncKey
{
    const struct mesh_info *mesh;
    const struct QuadBlock *quadPtr;
    const struct LevVertex *vertexPtr;
    int levelID, numQuads, numVerts;
};

struct NativeOccAsyncQuadInput
{
    unsigned short indices[9];
    unsigned short flags;
};

struct NativeOccAsyncTask
{
    struct NativeOccAsyncKey key;
    struct NativeOccAsyncQuadInput *quads;
    float (*verts)[3];
    struct NativeOccAsyncBlock *blocks;
    int copiedVerts, copiedQuads, acceptedQuads;
    SDL_Thread *thread;
    SDL_AtomicInt finished;
};

static struct NativeOccAsyncTask *s_occTask;
static struct NativeOccAsyncBlock *s_occActive;
static struct NativeOccAsyncKey s_occActiveKey;
static int s_occEnabled = -1;
static int s_occDisabled, s_occVerify;
static Uint64 s_occLastCopyMs;
static unsigned int s_occCacheHits;

static const unsigned char s_occIndices[8][3] =
{
    {0,4,5}, {4,6,5}, {6,4,1}, {5,6,2},
    {8,6,7}, {7,3,8}, {1,7,6}, {2,6,8}
};

static int NativeOccAsync_KeyEqual(const struct NativeOccAsyncKey *a,
                                   const struct NativeOccAsyncKey *b)
{
    return a->mesh == b->mesh && a->quadPtr == b->quadPtr &&
           a->vertexPtr == b->vertexPtr && a->levelID == b->levelID &&
           a->numQuads == b->numQuads && a->numVerts == b->numVerts;
}

static int NativeOccAsync_FlagEnabled(void)
{
    if (s_occEnabled < 0)
    {
        const char *env = SDL_getenv("CTR_OCC_ASYNC_QA");
        const char *verify = SDL_getenv("CTR_OCC_ASYNC_VERIFY");
        s_occEnabled = env != NULL && strcmp(env, "1") == 0;
        s_occVerify = verify != NULL && strcmp(verify, "1") == 0;
        if (env != NULL)
            Platform_Log("[OCC ASYNC] opt-in=%d verify=%d (original culling fallback)\n",
                         s_occEnabled, s_occVerify);
    }
    return s_occEnabled && !s_occDisabled;
}

static void NativeOccAsync_FreeTask(struct NativeOccAsyncTask *task)
{
    if (!task) return;
    /* Called only when the worker has finished or during program shutdown. */
    if (task->thread)
        SDL_WaitThread(task->thread, NULL);
    free(task->quads);
    free(task->verts);
    free(task->blocks);
    free(task);
}

static int SDLCALL NativeOccAsync_Worker(void *opaque)
{
    struct NativeOccAsyncTask *task = (struct NativeOccAsyncTask *)opaque;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    for (int block = 0; block < task->key.numQuads; ++block)
    {
        const struct NativeOccAsyncQuadInput *input = &task->quads[block];
        struct NativeOccAsyncBlock *out = &task->blocks[block];
        if (!(input->flags &
              (QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE)) ||
             (input->flags &
              (QUADBLOCK_FLAG_NO_COLLISION_RESPONSE |
               QUADBLOCK_FLAG_TRIGGER | QUADBLOCK_FLAG_KILL_PLANE)))
            continue;

        const int count = input->indices[2] == input->indices[3] ? 4 : 8;
        out->count = (unsigned char)count;
        for (int face = 0; face < count; ++face)
        {
            struct NativeOccAsyncTriangle *t = &out->triangles[face];
            const int i0 = input->indices[s_occIndices[face][0]];
            const int i1 = input->indices[s_occIndices[face][1]];
            const int i2 = input->indices[s_occIndices[face][2]];
            if (i0 >= task->key.numVerts ||
                i1 >= task->key.numVerts ||
                i2 >= task->key.numVerts)
                continue;

            const float *v0 = task->verts[i0];
            const float *v1 = task->verts[i1];
            const float *v2 = task->verts[i2];
            for (int axis = 0; axis < 3; ++axis)
            {
                t->v0[axis] = v0[axis];
                t->e1[axis] = v1[axis] - v0[axis];
                t->e2[axis] = v2[axis] - v0[axis];
            }
            t->valid = 1;
        }
    }
    SDL_SetAtomicInt(&task->finished, 1);
    return 0;
}

static struct NativeOccAsyncTask *NativeOccAsync_Create(
    const struct NativeOccAsyncKey *key)
{
    struct NativeOccAsyncTask *task;
    const size_t nBlocks = (size_t)key->numQuads;
    const size_t nVerts = (size_t)key->numVerts;
    const size_t bytes = nBlocks *
        (sizeof(struct NativeOccAsyncBlock) +
         sizeof(struct NativeOccAsyncQuadInput)) +
         nVerts * sizeof(float[3]);

    /* Prevent accidental memory explosions from malformed custom levels. */
    if (bytes > 24u * 1024u * 1024u)
        return NULL;

    task = calloc(1, sizeof(*task));
    if (!task) return NULL;
    task->key = *key;
    task->quads = calloc(nBlocks, sizeof(*task->quads));
    task->verts = malloc(nVerts * sizeof(*task->verts));
    task->blocks = calloc(nBlocks, sizeof(*task->blocks));
    if (!task->quads || !task->verts || !task->blocks)
    {
        NativeOccAsync_FreeTask(task);
        return NULL;
    }

    SDL_SetAtomicInt(&task->finished, 0);
    Platform_Log("[OCC ASYNC] copying %d verts and %d quadblocks in bounded slices (%u bytes)\n",
        key->numVerts, key->numQuads, (unsigned)bytes);
    return task;
}

void NativeOccAsync_Ensure(const struct mesh_info *mesh, int levelID)
{
    struct NativeOccAsyncKey key;
    struct NativeOccAsyncTask *task;
    Uint64 now;

    if (!NativeOccAsync_FlagEnabled() || !mesh ||
        !mesh->ptrVertexArray || !mesh->ptrQuadBlockArray ||
        mesh->numQuadBlock < 1 || mesh->numVertex < 1 ||
        mesh->numQuadBlock > NATIVE_OCC_ASYNC_MAX_QUADS ||
        mesh->numVertex > NATIVE_OCC_ASYNC_MAX_VERTS)
        return;

    key.mesh = mesh;
    key.quadPtr = mesh->ptrQuadBlockArray;
    key.vertexPtr = mesh->ptrVertexArray;
    key.levelID = levelID;
    key.numQuads = mesh->numQuadBlock;
    key.numVerts = mesh->numVertex;

    /* The old terrain must never be used for the next track, even if it
     * happens to share the same allocator address or block count. */
    if (s_occActive && !NativeOccAsync_KeyEqual(&s_occActiveKey, &key))
    {
        free(s_occActive);
        s_occActive = NULL;
        s_occCacheHits = 0;
    }
    if (s_occActive) return;

    task = s_occTask;
    if (task && task->thread && SDL_GetAtomicInt(&task->finished))
    {
        /* This join cannot block: finished is set at the end of the worker. */
        SDL_WaitThread(task->thread, NULL);
        task->thread = NULL;
        if (NativeOccAsync_KeyEqual(&task->key, &key))
        {
            s_occActive = task->blocks;
            s_occActiveKey = task->key;
            task->blocks = NULL;
            Platform_Log("[OCC ASYNC] READY level=%d quads=%d (original ray fallback until ready)\n",
                         key.levelID, key.numQuads);
        }
        NativeOccAsync_FreeTask(task);
        s_occTask = task = NULL;
        if (s_occActive) return;
    }

    /* If a different level arrived while a worker was in flight, its
     * snapshot is harmless; let it finish before creating a new request. */
    if (task && !NativeOccAsync_KeyEqual(&task->key, &key))
    {
        if (task->thread) return;
        NativeOccAsync_FreeTask(task);
        s_occTask = task = NULL;
    }

    if (!task)
    {
        task = NativeOccAsync_Create(&key);
        if (!task)
        {
            s_occDisabled = 1;
            Platform_Log("[OCC ASYNC] allocation rejected, retaining original BSP culling\n");
            return;
        }
        s_occTask = task;
        s_occLastCopyMs = 0;
    }

    if (task->thread) return;
    now = SDL_GetTicks();

    /* At most one slice per 16 ms, not one slice per bot or render call. */
    if (s_occLastCopyMs && now - s_occLastCopyMs < 16)
        return;
    s_occLastCopyMs = now;

    if (task->copiedVerts < task->key.numVerts)
    {
        int end = task->copiedVerts + NATIVE_OCC_ASYNC_VERTEX_SLICE;
        if (end > task->key.numVerts) end = task->key.numVerts;
        for (int i = task->copiedVerts; i < end; ++i)
        {
            const struct LevVertex *v = &mesh->ptrVertexArray[i];
            task->verts[i][0] = (float)v->pos.x;
            task->verts[i][1] = (float)v->pos.y;
            task->verts[i][2] = (float)v->pos.z;
        }
        task->copiedVerts = end;
        return;
    }

    if (task->copiedQuads < task->key.numQuads)
    {
        int end = task->copiedQuads + NATIVE_OCC_ASYNC_QUAD_SLICE;
        if (end > task->key.numQuads) end = task->key.numQuads;
        for (int i = task->copiedQuads; i < end; ++i)
        {
            const struct QuadBlock *block = &mesh->ptrQuadBlockArray[i];
            struct NativeOccAsyncQuadInput *out = &task->quads[i];
            memcpy(out->indices, block->index, sizeof(out->indices));
            out->flags = block->quadFlags;
        }
        task->copiedQuads = end;
        return;
    }

    task->thread = SDL_CreateThread(
        NativeOccAsync_Worker, "CTR async terrain-prep", task);
    if (!task->thread)
    {
        Platform_Log("[OCC ASYNC] thread creation failed, using original BSP culling\n");
        NativeOccAsync_FreeTask(task);
        s_occTask = NULL;
        s_occDisabled = 1;
    }
}

const struct NativeOccAsyncBlock *NativeOccAsync_GetBlock(
    const struct mesh_info *mesh, const struct QuadBlock *quad)
{
    size_t index;
    uintptr_t base, address;
    if (!s_occActive || s_occDisabled || !mesh ||
        s_occActiveKey.mesh != mesh ||
        s_occActiveKey.quadPtr != mesh->ptrQuadBlockArray ||
        s_occActiveKey.vertexPtr != mesh->ptrVertexArray ||
        s_occActiveKey.numQuads != mesh->numQuadBlock)
        return NULL;

    base = (uintptr_t)mesh->ptrQuadBlockArray;
    address = (uintptr_t)quad;
    if (address < base || (address - base) % sizeof(*quad) != 0)
        return NULL;
    index = (address - base) / sizeof(*quad);
    if (index >= (size_t)s_occActiveKey.numQuads) return NULL;

    if (++s_occCacheHits % 30000u == 0)
        Platform_Log("[OCC ASYNC] active cache ray quad checks=%u\n",s_occCacheHits);
    return &s_occActive[index];
}

int NativeOccAsync_VerifyEnabled(void)
{
    return !s_occDisabled && s_occVerify;
}

void NativeOccAsync_DisableAfterMismatch(void)
{
    if (!s_occDisabled)
    {
        Platform_Log("[OCC ASYNC] PARITY MISMATCH; disabling cache, retaining legacy geometry tests\n");
        s_occDisabled = 1;
    }
}

void NativeOccAsync_Shutdown(void)
{
    /* Shutdown is outside the frame loop; it may safely join the worker. */
    NativeOccAsync_FreeTask(s_occTask);
    s_occTask = NULL;
    free(s_occActive);
    s_occActive = NULL;
    memset(&s_occActiveKey, 0, sizeof(s_occActiveKey));
    s_occDisabled = 0;
    s_occEnabled = -1;
    s_occVerify = 0;
}
#endif
