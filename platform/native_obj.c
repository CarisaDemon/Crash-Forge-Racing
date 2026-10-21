/* Static OBJ racers. Geometry and GL images are host caches. The small model
 * descriptor keeps a direct host-side mesh pointer so rendering can resolve an
 * OBJ in O(1) instead of scanning the whole loaded-mod list every draw. */
#include <common.h>
#include <platform/native_obj.h>
#include <platform/native_assets.h>
#include <platform/native_renderer.h>
#include <platform/native_log.h>
#include <CharacterRegistry.h>
#include <VehiclePartRegistry.h>
#include <RacerLoadout.h>
#if defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_LIMIT 100000
struct NativeObjDescriptor { struct Model model; struct ModelHeader header; struct ModelFrame frame; struct NativeObjMesh *mesh; };
struct NativeObjCache {
    char key[160];
    struct NativeObjMesh mesh;
    struct NativeObjDescriptor *descriptor;
    float preparedScale;
    float preparedOffset[3];
    int renderCachePrepared;
    struct NativeObjCache *next;
};
static struct NativeObjCache *s_objCache;
static struct Model *s_retailWheelOverrideModel;
static int s_retailWheelOverrideTried;
static char s_retailWheelOverrideAsset[64] = "bandi_k_w";
static struct ObjWheelState {
    const struct Instance *instance;
    const struct Model *model;
    float angles[4];
    float steerAngle;
    int position[3];
    unsigned int tick;
} s_objWheels[8];

static char *Obj_Trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = 0;
    return s;
}

static int Obj_Path(char *dst, size_t size, const char *dir, const char *name)
{
    /* Exporters often leave absolute paths: assets always come from this folder. */
    const char *base = name;
    for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) return 0;
    int n = snprintf(dst, size, "%s/%s", dir, base);
    return n > 0 && (size_t)n < size;
}

static void Obj_Free(struct NativeObjMesh *mesh)
{
    /* Only alternate palette images are owned here; the default is owned
       by the existing OBJ material loader. */
    for (int index = 1; index < mesh->colorCount; ++index)
        if (mesh->colorTextures[index] != 0)
            NativeRenderer_ReleaseImageTexture(mesh->colorTextures[index]);
    if (mesh->atlasTexture != 0)
    {
        NativeRenderer_ReleaseImageTexture(mesh->atlasTexture);
    }
    else
    {
        /* Materials can reuse one native GL texture when their source
         * pixels match. Destroy each unique GPU texture only once. */
        for (int i = 0; i < mesh->materialCount; ++i)
        {
            const unsigned int texture = mesh->materials[i].texture;
            int alreadyReleased = texture == 0;
            for (int j = 0; j < i && !alreadyReleased; ++j)
                if (mesh->materials[j].texture == texture)
                    alreadyReleased = 1;
            if (!alreadyReleased)
                NativeRenderer_ReleaseImageTexture(texture);
        }
    }

    for (int i = 0; i < mesh->materialCount; ++i)
        free(mesh->materials[i].sourcePixels);

    free(mesh->materials);
    free(mesh->triangles);
    free(mesh->projectionCache);
    memset(mesh, 0, sizeof(*mesh));
}

static unsigned char Obj_SaturateU8(float value)
{
    int ivalue = (int)value;
    if (ivalue < 0) return 0;
    if (ivalue > 255) return 255;
    return (unsigned char)ivalue;
}

static void Obj_PrepareRenderCache(struct NativeObjMesh *mesh)
{
    if ((mesh == NULL) || (mesh->triangles == NULL) || (mesh->triangleCount <= 0))
        return;

    mesh->wheelMask = 0;
    for (int k = 0; k < 3; ++k)
    {
        mesh->authPerNorm[k] = mesh->extent[k] / 1016.0f;
        mesh->normPerAuth[k] = 1016.0f / mesh->extent[k];
        mesh->renderOffsetScaled[k] =
            mesh->userOffset[k] * mesh->normPerAuth[k] * mesh->userScale;
    }

    for (int i = 0; i < mesh->triangleCount; ++i)
    {
        struct NativeObjTriangle *t = &mesh->triangles[i];
        const struct NativeObjMaterial *m = &mesh->materials[t->material];
        const float colorScale = m->texture ? 128.0f : 255.0f;

        t->renderTexture = m->texture;
        t->renderTextured = m->texture != 0;

        if ((t->wheel >= 0) && (t->wheel < 4))
            mesh->wheelMask |= 1u << t->wheel;

        for (int j = 0; j < 3; ++j)
        {
            struct NativeObjVertex *v = &t->v[j];

            for (int k = 0; k < 3; ++k)
            {
                /* Keep the cached position as the scaled rest/base pose only.
                 * The per-instance alignment offset stays separate so CRASH_FALL
                 * and future animation poses can replace it without rebuilding
                 * immutable OBJ data. */
                v->renderP[k] = v->p[k] * mesh->userScale;
                v->renderColor[k] =
                    Obj_SaturateU8(v->color[k] * m->color[k] * colorScale);
            }

            {
                float uv[2] = {
                    m->uvOffset[0] + v->uv[0] * m->uvScale[0],
                    m->uvOffset[1] + (1.0f - v->uv[1]) * m->uvScale[1]
                };
                for (int axis = 0; axis < 2; ++axis)
                {
                    float value = uv[axis];
                    if (value < 0.0f) value = 0.0f;
                    else if (value > 1.0f) value = 1.0f;

                    /* Preserve native OBJ UV precision through the legacy
                     * u8 POLY_GT3 packet. renderUV holds the nearest integer
                     * virtual texel; renderUVFrac carries the signed residual
                     * in 1/256 of that texel.  The final shader reconstructs
                     * the normalized Blender UV exactly enough for 1K/2K maps. */
                    const float scaled = value * 255.0f;
                    int base = (int)floorf(scaled);
                    int frac = (int)lroundf((scaled - (float)base) * 256.0f);
                    if (frac > 127)
                    {
                        ++base;
                        frac -= 256;
                    }
                    if (base < 0) base = 0;
                    else if (base > 255) base = 255;
                    if (frac < -128) frac = -128;
                    else if (frac > 127) frac = 127;

                    v->renderUV[axis] = (unsigned char)base;
                    v->renderUVFrac[axis] = (signed char)frac;
                }
            }
        }
    }
}

/*
 * Vehicle-part getters are hot render/update paths: wheels and karts can be
 * queried several times per driver in one host frame. The OBJ geometry,
 * materials and UVs are immutable after load, so rebuilding the complete
 * per-triangle render cache on every query only multiplies CPU work with the
 * host refresh rate. Rebuild only when the authored scale/alignment changes
 * (for example after a registry refresh).
 */
static void Obj_EnsureRenderCache(struct NativeObjCache *cache)
{
    struct NativeObjMesh *mesh;

    if (cache == NULL)
        return;

    mesh = &cache->mesh;
    if (cache->renderCachePrepared &&
        cache->preparedScale == mesh->userScale &&
        cache->preparedOffset[0] == mesh->userOffset[0] &&
        cache->preparedOffset[1] == mesh->userOffset[1] &&
        cache->preparedOffset[2] == mesh->userOffset[2])
    {
        return;
    }

    Obj_PrepareRenderCache(mesh);

    cache->preparedScale = mesh->userScale;
    cache->preparedOffset[0] = mesh->userOffset[0];
    cache->preparedOffset[1] = mesh->userOffset[1];
    cache->preparedOffset[2] = mesh->userOffset[2];
    cache->renderCachePrepared = 1;
}

static int Obj_AddMaterial(struct NativeObjMesh *mesh, const char *name)
{
    if (mesh->materialCount >= 256 || strlen(name) >= sizeof(mesh->materials[0].name)) return -1;
    void *p = realloc(mesh->materials, (mesh->materialCount + 1) * sizeof(*mesh->materials));
    if (!p) return -1;
    mesh->materials = p;
    int index = mesh->materialCount++;
    struct NativeObjMaterial *m = &mesh->materials[index];
    memset(m, 0, sizeof(*m));
    strcpy(m->name, name);
    m->color[0] = m->color[1] = m->color[2] = m->opacity = 1;
    m->uvScale[0] = m->uvScale[1] = 1.0f;
    return index;
}

static int Obj_LoadMaterials(struct NativeObjMesh *mesh, const char *dir, const char *name)
{
    char path[1024], line[4096];
    if (!Obj_Path(path, sizeof(path), dir, name)) return 0;
    FILE *f = NativeAssets_OpenHost(path, "rb");
    if (!f) return 0;
    int current = -1, ok = 1;
    while (ok && fgets(line, sizeof(line), f)) {
        char *s = Obj_Trim(line);
        if (!strncmp(s, "newmtl ", 7)) { current = Obj_AddMaterial(mesh, Obj_Trim(s + 7)); ok = current >= 0; }
        else if (current >= 0) {
            struct NativeObjMaterial *m = &mesh->materials[current];
            if (!strncmp(s, "Kd ", 3)) {
                ok = sscanf(s + 3, "%f %f %f", &m->color[0], &m->color[1], &m->color[2]) == 3;
                for (int j = 0; j < 3; ++j) {
                    ok &= isfinite(m->color[j]);
                    m->color[j] = fminf(1, fmaxf(0, m->color[j]));
                }
            } else if (!strncmp(s, "d ", 2)) {
                ok = sscanf(s + 2, "%f", &m->opacity) == 1 && isfinite(m->opacity);
            } else if (!strncmp(s, "map_Kd ", 7)) {
                char resolved[1024];
                ok = Obj_Path(path, sizeof(path), dir, Obj_Trim(s + 7)) && NativeAssets_ResolvePath(path, resolved, sizeof(resolved));
                SDL_Surface *source = ok ? SDL_LoadPNG(resolved) : NULL;
                if (!source && ok) source = SDL_LoadBMP(resolved);
                SDL_Surface *rgba = source ? SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32) : NULL;
                if (source) SDL_DestroySurface(source);
                ok = rgba != NULL;
                if (rgba) {
                    /* SDL surfaces may have padded rows. */
                    unsigned char *pixels = malloc((size_t)rgba->w * rgba->h * 4);
                    ok = pixels != NULL;
                    if (pixels) {
                        for (int y = 0; y < rgba->h; ++y)
                            memcpy(pixels + (size_t)y * rgba->w * 4, (char *)rgba->pixels + y * rgba->pitch, (size_t)rgba->w * 4);

                        /* The same PNG may be referenced by several MTL
                         * entries (NF Retro uses it four times). Compare
                         * decoded RGBA bytes once at load, then share the
                         * exact same OpenGL texture handle across materials.
                         * Keep each material's pixel buffer for any atlas. */
                        unsigned int reusedTexture = 0;
                        const size_t pixelBytes =
                            (size_t)rgba->w * (size_t)rgba->h * 4u;
                        for (int j = 0; j < mesh->materialCount; ++j)
                        {
                            const struct NativeObjMaterial *other =
                                &mesh->materials[j];
                            if (other != m && other->texture != 0 &&
                                other->sourcePixels != NULL &&
                                other->sourceWidth == rgba->w &&
                                other->sourceHeight == rgba->h &&
                                memcmp(other->sourcePixels, pixels, pixelBytes) == 0)
                            {
                                reusedTexture = other->texture;
                                break;
                            }
                        }
                        if (m->texture != 0)
                        {
                            int shared = 0;
                            for (int j = 0; j < mesh->materialCount; ++j)
                                if (&mesh->materials[j] != m &&
                                    mesh->materials[j].texture == m->texture)
                                    shared = 1;
                            if (!shared)
                                NativeRenderer_ReleaseImageTexture(m->texture);
                        }
                        free(m->sourcePixels);

                        m->texture = reusedTexture ? reusedTexture :
                            NativeRenderer_CreateImageTexture(
                                pixels, rgba->w, rgba->h);
                        m->sourcePixels = pixels;
                        m->sourceWidth = rgba->w;
                        m->sourceHeight = rgba->h;
                        ok = m->texture != 0;
                        if (reusedTexture)
                            Platform_Log("[CTR Native] OBJ GPU texture reused: %s\n",
                                         m->name);
                    }
                    SDL_DestroySurface(rgba);
                }
            }
        }
    }
    ok &= !ferror(f);
    fclose(f);
    return ok;
}

static int Obj_NextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value && result < 2048)
        result <<= 1;
    return result;
}

static void Obj_ReleaseMaterialSources(struct NativeObjMesh *mesh)
{
    for (int i = 0; i < mesh->materialCount; ++i)
    {
        free(mesh->materials[i].sourcePixels);
        mesh->materials[i].sourcePixels = NULL;
        mesh->materials[i].sourceWidth = 0;
        mesh->materials[i].sourceHeight = 0;
    }
}

static void Obj_TryBuildTextureAtlas(struct NativeObjMesh *mesh)
{
    enum { ATLAS_PAD = 2, ATLAS_LIMIT = 2048, SOURCE_LIMIT = 256 };
    int texturedCount = 0;
    int packedWidth = 0;
    int packedHeight = 0;
    int eligible = 1;

    for (int i = 0; i < mesh->materialCount; ++i)
    {
        const struct NativeObjMaterial *m = &mesh->materials[i];
        if (m->texture == 0)
            continue;

        texturedCount++;

        if (m->sourcePixels == NULL ||
            m->sourceWidth <= 0 || m->sourceHeight <= 0 ||
            m->sourceWidth > SOURCE_LIMIT || m->sourceHeight > SOURCE_LIMIT)
        {
            eligible = 0;
            continue;
        }

        packedWidth += m->sourceWidth + ATLAS_PAD * 2;
        if (m->sourceHeight + ATLAS_PAD * 2 > packedHeight)
            packedHeight = m->sourceHeight + ATLAS_PAD * 2;
    }

    /* Keep the normal path for one-texture models and large HD material sets.
     * This optimization is aimed at the many tiny PS2/GC-style material maps
     * that otherwise cause a texture bind for nearly every OT run. */
    if (texturedCount < 2 || !eligible ||
        packedWidth <= 0 || packedHeight <= 0 ||
        packedWidth > ATLAS_LIMIT || packedHeight > ATLAS_LIMIT)
    {
        Obj_ReleaseMaterialSources(mesh);
        return;
    }

    const int atlasWidth = Obj_NextPowerOfTwo(packedWidth);
    const int atlasHeight = Obj_NextPowerOfTwo(packedHeight);
    if (atlasWidth < packedWidth || atlasHeight < packedHeight ||
        atlasWidth > ATLAS_LIMIT || atlasHeight > ATLAS_LIMIT)
    {
        Obj_ReleaseMaterialSources(mesh);
        return;
    }

    unsigned char *atlasPixels =
        calloc((size_t)atlasWidth * (size_t)atlasHeight, 4);
    if (atlasPixels == NULL)
    {
        Obj_ReleaseMaterialSources(mesh);
        return;
    }

    int cursorX = 0;
    for (int i = 0; i < mesh->materialCount; ++i)
    {
        struct NativeObjMaterial *m = &mesh->materials[i];
        if (m->texture == 0)
            continue;

        const int w = m->sourceWidth;
        const int h = m->sourceHeight;
        const int contentX = cursorX + ATLAS_PAD;
        const int contentY = ATLAS_PAD;

        /* Duplicate edge texels into a 2 px gutter so bilinear filtering can
         * never sample a neighboring material. */
        for (int py = 0; py < h + ATLAS_PAD * 2; ++py)
        {
            int sy = py - ATLAS_PAD;
            if (sy < 0) sy = 0;
            else if (sy >= h) sy = h - 1;

            for (int px = 0; px < w + ATLAS_PAD * 2; ++px)
            {
                int sx = px - ATLAS_PAD;
                if (sx < 0) sx = 0;
                else if (sx >= w) sx = w - 1;

                const unsigned char *src =
                    m->sourcePixels + ((size_t)sy * w + sx) * 4;
                unsigned char *dst =
                    atlasPixels +
                    ((size_t)(contentY - ATLAS_PAD + py) * atlasWidth +
                     (contentX - ATLAS_PAD + px)) * 4;
                memcpy(dst, src, 4);
            }
        }

        m->uvOffset[0] = (float)contentX / (float)atlasWidth;
        m->uvOffset[1] = (float)contentY / (float)atlasHeight;
        m->uvScale[0] = (float)w / (float)atlasWidth;
        m->uvScale[1] = (float)h / (float)atlasHeight;

        cursorX += w + ATLAS_PAD * 2;
    }

    const unsigned int atlasTexture =
        NativeRenderer_CreateImageTexture(atlasPixels, atlasWidth, atlasHeight);
    free(atlasPixels);

    if (atlasTexture == 0)
    {
        for (int i = 0; i < mesh->materialCount; ++i)
        {
            mesh->materials[i].uvScale[0] = 1.0f;
            mesh->materials[i].uvScale[1] = 1.0f;
            mesh->materials[i].uvOffset[0] = 0.0f;
            mesh->materials[i].uvOffset[1] = 0.0f;
        }
        Obj_ReleaseMaterialSources(mesh);
        return;
    }

    /* Release every distinct pre-atlas GPU texture exactly once. The MTL
     * loader may already have shared identical source images across materials. */
    for (int i = 0; i < mesh->materialCount; ++i)
    {
        const unsigned int texture = mesh->materials[i].texture;
        int alreadyReleased = texture == 0;
        for (int j = 0; j < i && !alreadyReleased; ++j)
            if (mesh->materials[j].texture == texture)
                alreadyReleased = 1;
        if (!alreadyReleased)
            NativeRenderer_ReleaseImageTexture(texture);
    }
    for (int i = 0; i < mesh->materialCount; ++i)
        if (mesh->materials[i].texture != 0)
            mesh->materials[i].texture = atlasTexture;

    mesh->atlasTexture = atlasTexture;
    SDL_Log("OBJ texture atlas: %d textures -> 1 (%dx%d)",
            texturedCount, atlasWidth, atlasHeight);

    Obj_ReleaseMaterialSources(mesh);
}

static int Obj_Index(const char *s, int count, const char **end)
{
    char *tail;
    long value = strtol(s, &tail, 10);
    *end = tail;
    if (tail == s || value == 0 || value > count || value < -count) return -1;
    return value < 0 ? count + (int)value : (int)value - 1;
}

/* Blender and other OBJ exporters frequently repeat identical positions
 * to preserve separate UVs/normals/colors. Sharing only the projection cache
 * index avoids redundant transforms while retaining every triangle, corner,
 * UV and original source index (used by body-patch animations/bindings). */
static unsigned int Obj_ProjectionPositionHash(const float p[3])
{
    const unsigned char *bytes = (const unsigned char *)p;
    unsigned int hash = 2166136261u;
    for (size_t i = 0; i < sizeof(float) * 3; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int Obj_ShareProjectionPositions(
    struct NativeObjMesh *mesh,
    const struct NativeObjVertex *positions,
    int count)
{
    size_t tableSize = 1;
    int *table;
    int *projectionIndices;
    int uniqueCount = 0;

    if (mesh == NULL || positions == NULL || count <= 0)
        return count;

    while (tableSize < (size_t)count * 2u)
        tableSize <<= 1;

    table = calloc(tableSize, sizeof(*table));
    projectionIndices = malloc((size_t)count * sizeof(*projectionIndices));
    if (table == NULL || projectionIndices == NULL)
    {
        free(table);
        free(projectionIndices);
        return count; /* Safe fallback: initial projectionIndex=objIndex. */
    }

    for (int i = 0; i < count; ++i)
    {
        size_t slot = (size_t)Obj_ProjectionPositionHash(positions[i].p) &
                      (tableSize - 1u);
        while (table[slot] != 0 &&
               memcmp(positions[table[slot] - 1].p, positions[i].p,
                      sizeof(positions[i].p)) != 0)
        {
            slot = (slot + 1u) & (tableSize - 1u);
        }

        if (table[slot] == 0)
        {
            table[slot] = i + 1;
            projectionIndices[i] = uniqueCount++;
        }
        else
        {
            projectionIndices[i] = projectionIndices[table[slot] - 1];
        }
    }

    for (int i = 0; i < mesh->triangleCount; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            struct NativeObjVertex *v = &mesh->triangles[i].v[j];
            if (v->objIndex >= 0 && v->objIndex < count)
                v->projectionIndex = projectionIndices[v->objIndex];
        }
    }

    free(table);
    free(projectionIndices);
    if (uniqueCount < count)
        Platform_Log("[CTR Native] OBJ projection reuse: %d source positions, %d unique; faces/UV untouched\n",
                     count, uniqueCount);
    return uniqueCount;
}

static int Obj_Read(struct NativeObjMesh *mesh, const char *dir, const char *filename)
{
    char path[1024], line[8192], mtl[1024] = "";
    struct NativeObjVertex *positions = NULL;
    float (*uvs)[2] = NULL;
    int positionCount = 0, uvCount = 0, current = 0, ok = 1, objectWheel = -1, groupWheel = -1;
    if (!Obj_Path(path, sizeof(path), dir, filename)) return 0;
    FILE *f = NativeAssets_OpenHost(path, "rb");
    if (!f) return 0;
    /* Materials must be available before faces, even if mtllib occurs later. */
    while (fgets(line, sizeof(line), f)) {
        char *s = Obj_Trim(line);
        if (!strncmp(s, "mtllib ", 7)) { snprintf(mtl, sizeof(mtl), "%s", Obj_Trim(s + 7)); break; }
    }
    if (Obj_AddMaterial(mesh, "") < 0) ok = 0;
    if (*mtl) {
        char resolved[1024];
        if (!Obj_Path(path, sizeof(path), dir, mtl) || !NativeAssets_ResolvePath(path, resolved, sizeof(resolved))) {
            snprintf(mtl, sizeof(mtl), "%s", filename);
            char *dot = strrchr(mtl, '.');
            if (dot) strcpy(dot, ".mtl");
        }
        ok &= Obj_LoadMaterials(mesh, dir, mtl);
        if (ok)
            Obj_TryBuildTextureAtlas(mesh);
    }
    rewind(f);
    while (ok && fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) { ok = 0; break; }
        char *s = Obj_Trim(line);
        if (!strncmp(s, "o ", 2) || !strncmp(s, "g ", 2)) {
            static const char *names[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
            int wheel = -1;
            for (int i = 0; i < 4; ++i) if (!strcmp(Obj_Trim(s + 2), names[i])) wheel = i;
            if (*s == 'o') { objectWheel = wheel; groupWheel = -1; }
            else groupWheel = wheel;
        } else if (!strncmp(s, "v ", 2)) {
            struct NativeObjVertex v = {0};
            v.color[0] = v.color[1] = v.color[2] = 1.0f;
            v.objIndex = positionCount;
            v.projectionIndex = positionCount;
            int n = sscanf(s + 2, "%f %f %f %f %f %f", &v.p[0], &v.p[1], &v.p[2], &v.color[0], &v.color[1], &v.color[2]);
            ok = (n == 3 || n == 6) && positionCount < OBJ_LIMIT;
            for (int j = 0; j < 3; ++j) ok &= isfinite(v.p[j]) && isfinite(v.color[j]) && fabsf(v.p[j]) < 1e6f;
            if (!ok) break;
            for (int j = 0; j < 3; ++j) v.color[j] = fminf(1, fmaxf(0, v.color[j]));
            void *p = realloc(positions, (positionCount + 1) * sizeof(*positions));
            if (!p) { ok = 0; break; }
            positions = p; positions[positionCount++] = v;
        } else if (!strncmp(s, "vt ", 3)) {
            float uv[2];
            ok = sscanf(s + 3, "%f %f", &uv[0], &uv[1]) == 2 && isfinite(uv[0]) && isfinite(uv[1]) && uvCount < OBJ_LIMIT;
            if (!ok) break;
            void *p = realloc(uvs, (uvCount + 1) * sizeof(*uvs));
            if (!p) { ok = 0; break; }
            uvs = p; memcpy(uvs[uvCount++], uv, sizeof(uv));
        } else if (!strncmp(s, "usemtl ", 7)) {
            current = -1;
            for (int i = 0; i < mesh->materialCount; ++i)
                if (!strcmp(mesh->materials[i].name, Obj_Trim(s + 7))) current = i;
            ok = current >= 0;
        } else if (!strncmp(s, "f ", 2)) {
            struct NativeObjVertex face[128];
            int count = 0;
            const char *p = s + 2;
            while (ok && *p) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p || *p == '#') break;
                int v = Obj_Index(p, positionCount, &p);
                if (v < 0 || count >= 128) { ok = 0; break; }
                face[count] = positions[v];
                if (*p == '/') {
                    ++p;
                    if (*p != '/') {
                        int t = Obj_Index(p, uvCount, &p);
                        if (t < 0) { ok = 0; break; }
                        memcpy(face[count].uv, uvs[t], sizeof(uvs[t]));
                    }
                    if (*p == '/') { char *end; ++p; strtol(p, &end, 10); if (end == p) { ok = 0; break; } p = end; }
                }
                if (*p && *p != ' ' && *p != '\t') { ok = 0; break; }
                ++count;
            }
            if (!ok || count < 3 || mesh->triangleCount + count - 2 > OBJ_LIMIT) { ok = 0; break; }
            void *newTriangles = realloc(mesh->triangles, (mesh->triangleCount + count - 2) * sizeof(*mesh->triangles));
            if (!newTriangles) { ok = 0; break; }
            mesh->triangles = newTriangles;
            for (int i = 1; i < count - 1; ++i) {
                struct NativeObjTriangle *t = &mesh->triangles[mesh->triangleCount++];
                t->v[0] = face[0]; t->v[1] = face[i]; t->v[2] = face[i+1]; t->material = current;
                t->wheel = objectWheel >= 0 ? objectWheel : groupWheel;
            }
        }
    }
    ok &= !ferror(f) && mesh->triangleCount > 0;
    fclose(f); free(uvs);
    if (!ok) { free(positions); return 0; }

    /* Store the number of distinct positions used by the projection cache.
     * All original OBJ indices stay intact for model bindings and UV seams. */
    mesh->positionCount = Obj_ShareProjectionPositions(mesh, positions, positionCount);
    free(positions);
    mesh->projectionCache = calloc((size_t)mesh->positionCount, sizeof(*mesh->projectionCache));
    if (mesh->projectionCache == NULL) return 0;

    float lo[3] = {1e30f,1e30f,1e30f}, hi[3] = {-1e30f,-1e30f,-1e30f};
    for (int i = 0; i < mesh->triangleCount; ++i)
        for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) {
            float p = mesh->triangles[i].v[j].p[k];
            if (p < lo[k]) lo[k] = p;
            if (p > hi[k]) hi[k] = p;
        }
    /* Include each wheel's complete sweep in the retail culling bounds. Keep
     * centres/radii in authored units: normalization differs on each axis. */
    for (int w = 0; w < 4; ++w) {
        float low[3] = {1e30f,1e30f,1e30f}, high[3] = {-1e30f,-1e30f,-1e30f};
        int found = 0;
        for (int i = 0; i < mesh->triangleCount; ++i) if (mesh->triangles[i].wheel == w)
            for (int j = 0; j < 3; ++j) {
                found = 1;
                for (int k = 0; k < 3; ++k) {
                    float p = mesh->triangles[i].v[j].p[k];
                    low[k] = fminf(low[k], p); high[k] = fmaxf(high[k], p);
                }
            }
        if (!found) continue;
        struct NativeObjWheel *wheel = &mesh->wheels[w];
        for (int k = 0; k < 3; ++k) wheel->center[k] = (low[k] + high[k]) * 0.5f;
        for (int i = 0; i < mesh->triangleCount; ++i) if (mesh->triangles[i].wheel == w)
            for (int j = 0; j < 3; ++j) {
                const float *p = mesh->triangles[i].v[j].p;
                wheel->radius = fmaxf(wheel->radius, hypotf(p[1] - wheel->center[1], p[2] - wheel->center[2]));
            }
        for (int k = 1; k < 3; ++k) {
            lo[k] = fminf(lo[k], wheel->center[k] - wheel->radius);
            hi[k] = fmaxf(hi[k], wheel->center[k] + wheel->radius);
        }
    }
    for (int k = 0; k < 3; ++k) {
        mesh->extent[k] = fmaxf(hi[k] - lo[k], 0.001f);
        float origin = floorf(lo[k] / mesh->extent[k] * 254);
        if (origin < -8191 || origin > 7935 || mesh->extent[k] > 32) return 0;
        mesh->frameOrigin[k] = (s16)origin;
    }
    /* Fit the retail frame's 255*4 bounds, retaining the authored origin and
     * Y-up axes. The game's camera matrix supplies the screen-space Y flip. */
    for (int i = 0; i < mesh->triangleCount; ++i)
        for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) {
            mesh->triangles[i].v[j].p[k] = mesh->triangles[i].v[j].p[k] / mesh->extent[k] * 1016;
        }
    return 1;
}

static struct NativeObjCache *Obj_FindInDirectory(
    const char *cacheKey,
    const char *dir,
    const char *preferredName)
{
    for (struct NativeObjCache *c = s_objCache; c; c = c->next)
        if (!strcmp(c->key, cacheKey))
            return c;

    char host[1024], filename[1024], preferredFile[256];
    if (!NativeAssets_BuildPath(dir, host, sizeof(host)))
        return NULL;

    int count = 0;
    char **files = SDL_GlobDirectory(
        host, "*.obj", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!files || !count)
    {
        SDL_free(files);
        return NULL;
    }

    /* Prefer the configured asset name, then model.obj. A one-file folder is
       accepted automatically; multi-file folders must resolve unambiguously. */
    int selected = count == 1 ? 0 : -1;
    for (int i = 0; i < count; ++i)
        if (!SDL_strcasecmp(files[i], "model.obj"))
            selected = i;

    preferredFile[0] = 0;
    if (preferredName && *preferredName)
        snprintf(preferredFile, sizeof(preferredFile),
                 "%s.obj", preferredName);

    if (preferredFile[0])
        for (int i = 0; i < count; ++i)
            if (!SDL_strcasecmp(files[i], preferredFile))
                selected = i;

    if (selected < 0)
    {
        SDL_Log("OBJ asset %s: ambiguous OBJ folder %s",
                cacheKey, dir);
        SDL_free(files);
        return NULL;
    }

    snprintf(filename, sizeof(filename), "%s", files[selected]);
    SDL_free(files);

    struct NativeObjCache *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->mesh.userScale = 1.0f;
    if (!Obj_Read(&c->mesh, dir, filename))
    {
        SDL_Log("OBJ asset %s: invalid mesh/material/texture", cacheKey);
        Obj_Free(&c->mesh);
        free(c);
        return NULL;
    }

    snprintf(c->key, sizeof(c->key), "%s", cacheKey);
    c->next = s_objCache;
    s_objCache = c;

    SDL_Log("OBJ asset %s: loaded %d triangles, %d materials",
            cacheKey, c->mesh.triangleCount, c->mesh.materialCount);
    return c;
}

static struct NativeObjCache *Obj_Find(const char *name)
{
    char key[160], dir[160];

    if (name == NULL || !*name)
        return NULL;

    snprintf(key, sizeof(key), "racer:%s", name);
    snprintf(dir, sizeof(dir), "mods/racers/%s", name);
    return Obj_FindInDirectory(key, dir, name);
}

static struct Model *Obj_GetOrCreateModel(
    struct NativeObjCache *c,
    const char *label)
{
    if (c == NULL)
        return NULL;

    Obj_EnsureRenderCache(c);

    if (c->descriptor != NULL)
        return &c->descriptor->model;

    struct NativeObjDescriptor *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;

    snprintf(d->model.name, sizeof(d->model.name), "%s",
             label ? label : "native_obj");
    d->model.id = -1;
    d->model.numHeaders = 1;
    d->model.headers = &d->header;

    snprintf(d->header.name, sizeof(d->header.name), "%s",
             label ? label : "native_obj");
    d->header.unk1 = NATIVE_OBJ_MODEL_MAGIC;
    d->header.maxDistanceLOD = -1;

    d->header.scale.x =
        (s16)fmaxf(1, c->mesh.extent[0] * 1000);
    d->header.scale.y =
        (s16)fmaxf(1, c->mesh.extent[1] * 1000);
    d->header.scale.z =
        (s16)fmaxf(1, c->mesh.extent[2] * 1000);

    d->header.ptrFrameData = &d->frame;
    d->frame.pos.x = c->mesh.frameOrigin[0];
    d->frame.pos.y = c->mesh.frameOrigin[1];
    d->frame.pos.z = c->mesh.frameOrigin[2];
    d->frame.vertexOffset = sizeof(d->frame);
    d->mesh = &c->mesh;

    c->descriptor = d;
    return &d->model;
}

struct Model *NativeObj_LoadRacer(const char *assetName)
{
    if (assetName == NULL || !*assetName || strlen(assetName) >= 64)
    {
        SDL_Log("OBJ racer: asset_name is empty or too long; using CTR fallback");
        return NULL;
    }

    /*
     * The package directory and the drawable asset_name are independent.
     * Example: mods/mad_katzz_character/{character.ini,mad_racer.obj}
     * declares asset_name=mad_racer. Looking up only mods/racers/mad_racer
     * silently failed and displayed fallback_retail (Pura) instead.
     *
     * Resolve the owning registered racer first, using its stable folder
     * slug for the path and asset_name/model_file only for OBJ selection.
     * Keep the old asset-named lookup for unchanged retail/legacy callers.
     */
    struct NativeObjCache *c = NULL;
    for (int i = 0; i < CharacterRegistry_GetCount(); ++i)
    {
        const struct CharacterDef *character =
            CharacterRegistry_GetByRosterIndex(i);
        if (character == NULL ||
            SDL_strcasecmp(character->assetName, assetName))
            continue;

        char dir[160], key[160], preferred[128];
        const char *modelLeaf = character->modelFile[0] ?
            character->modelFile : assetName;
        snprintf(preferred, sizeof(preferred), "%s", modelLeaf);
        size_t len = strlen(preferred);
        if (len > 4 && !SDL_strcasecmp(preferred + len - 4, ".obj"))
            preferred[len - 4] = '\0';

        snprintf(dir, sizeof(dir), "mods/racers/%s", character->slug);
        snprintf(key, sizeof(key), "racer:%s", character->slug);
        c = Obj_FindInDirectory(key, dir, preferred);
        if (c)
            break;
    }
    if (!c)
        c = Obj_Find(assetName);
    if (!c)
        return NULL;

    c->mesh.userScale = 1.0f;
    c->mesh.userOffset[0] = 0.0f;
    c->mesh.userOffset[1] = 0.0f;
    c->mesh.userOffset[2] = 0.0f;

    for (int i = 0; i < CharacterRegistry_GetCount(); ++i)
    {
        const struct CharacterDef *character =
            CharacterRegistry_GetByRosterIndex(i);

        if (character != NULL &&
            !SDL_strcasecmp(character->assetName, assetName))
        {
            c->mesh.userScale = character->modelScale;
            c->mesh.userOffset[0] = character->modelOffset[0];
            c->mesh.userOffset[1] = character->modelOffset[1];
            c->mesh.userOffset[2] = character->modelOffset[2];
            break;
        }
    }

    return Obj_GetOrCreateModel(c, assetName);
}

/* CNK Tornado Top is a NEW item with its OWN native mesh and texture.
 * Load from assets/cnk_tornado, never from Warp Orb assets or racer mods. */
struct Model *NativeObj_LoadCnkTornado(void)
{
    struct NativeObjCache *c = Obj_FindInDirectory(
        "effect:cnk_tornado", "cnk_tornado", "model");
    if (c == NULL)
        return NULL;

    /* 20% smaller than prior QA (0.59 -> 0.47). CNK-only visual,
     * with no modifications to retail Warp Orb or racer models. */
    c->mesh.userScale = 0.47f;
    /* CNK original texture is 'tornadoadd.png': ADDITIVE blending keeps
     * the dark/black texels invisible instead of drawing dark opaque borders.
     * This tag is consumed only by this OBJ's primitive packets. */
    c->mesh.additiveEffect = 1;
    c->mesh.userOffset[0] = 0.0f;
    c->mesh.userOffset[1] = 0.0f;
    c->mesh.userOffset[2] = 0.0f;
    Obj_EnsureRenderCache(c);
    return Obj_GetOrCreateModel(c, "cnk_tornado");
}

struct Model *NativeObj_LoadStaticTrack(const char *slug)
{
    char key[160];
    char dir[256];
    struct NativeObjCache *c;

    if (slug == NULL || !*slug || strlen(slug) >= 96)
        return NULL;

    snprintf(key, sizeof(key), "track:%s", slug);
    snprintf(dir, sizeof(dir), "mods/tracks/%s", slug);

    c = Obj_FindInDirectory(key, dir, "model");
    if (c == NULL)
        return NULL;

    /* Assembly Lane's source OBJ is already pre-scaled by 0.008.
       The normalized native OBJ renderer maps that file to ~5413 x 7070
       CTR units with userScale=1.0. Applying another 8x made the kart
       tiny and the track ~43k x 56k, far beyond normal CTR road scale.
       Leave all static tracks at the same 1.0 native unit scale. */
    c->mesh.userScale = 1.0f;
    c->mesh.userOffset[0] = 0.0f;
    c->mesh.userOffset[1] = 0.0f;
    c->mesh.userOffset[2] = 0.0f;
    Obj_PrepareRenderCache(&c->mesh);

    SDL_Log("OBJ track %s: ready (%d triangles, scale=%.2f)",
            slug, c->mesh.triangleCount, c->mesh.userScale);
    return Obj_GetOrCreateModel(c, slug);
}


/* -------------------------------------------------------------------------
 * First custom-track prototype: Assembly Lane (CNK N-Gage)
 *
 * Crash Cove's retail LEV is temporarily used only as the game-mode/TT shell.
 * The actual visible/collidable road comes from mods/tracks/assembly_lane.
 * ------------------------------------------------------------------------- */

/* The prepared Assembly Lane OBJ is centered on its start line.
 * At native userScale 1.0 the full mesh stays in signed-16 camera space,
 * so presentation, collision, spawn and checkpoints share zero translation. */
#define ASSEMBLY_LANE_WORLD_OFFSET_Z 0

static struct Model *s_customTrackModel;
static int s_customTrackFixedDebugCount;
static struct Instance *s_customTrackInstance;
static struct Level *s_customTrackLevel;

struct NativeObjCustomTrackLapState
{
    int initialized;
    int stage;
    int lastX;
    int lastZ;
};

static struct NativeObjCustomTrackLapState s_customTrackLap[8];

static struct QuadBlock s_customTrackGroundQuad = {
    .quadFlags = QUADBLOCK_FLAG_GROUND,
    .terrain_type = TERRAIN_ASPHALT,
    .checkpointIndex = 0xff,
};

static struct QuadBlock s_customTrackWallQuad = {
    .quadFlags = QUADBLOCK_FLAG_COLLISION_SURFACE,
    .terrain_type = TERRAIN_ASPHALT,
    .checkpointIndex = 0xff,
};

/*
 * Spawn, fixed collision and moved collision must agree throughout a TT race,
 * including the loading/introduction frames. The presentation Instance can
 * wait until gameplay, but the kart must never spawn against Crash Cove's BSP.
 */
static int NativeObj_CustomTrack_IsRaceShell(const struct GameTracker *gt)
{
    /* Legacy Assembly Lane OBJ prototype is retired.
       Never hijack Crash Cove's authentic geometry, collision, or spawn.
       Native VRM/LEV tracks use their own validated level loader instead. */
    (void)gt;
    return 0;
}

static int NativeObj_CustomTrack_ShouldRun(const struct GameTracker *gt)
{
    if (!NativeObj_CustomTrack_IsRaceShell(gt) || gt->level1 == NULL)
        return 0;

    /* Race camera introductions also need the custom world Instance.
       Only menu/loading states should postpone its creation. */
    if ((gt->gameMode1 & (MAIN_MENU | LOADING)) != 0)
        return 0;

    return 1;
}

int NativeObj_CustomTrack_IsActive(void)
{
    const struct GameTracker *gt = sdata ? sdata->gGT : NULL;
    return NativeObj_CustomTrack_IsRaceShell(gt) &&
           gt->level1 != NULL;
}

static void NativeObj_CustomTrack_SetIdentity(MATRIX *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0][0] = FP_ONE;
    m->m[1][1] = FP_ONE;
    m->m[2][2] = FP_ONE;
}

void NativeObj_CustomTrack_ResetRuntime(void)
{
    /*
     * JitPool_Clear recycles every Instance during race restart. Any cached
     * Instance pointer becomes stale even when level1 itself is reused.
     */
    s_customTrackInstance = NULL;
    s_customTrackLevel = NULL;
    s_customTrackFixedDebugCount = 0;
    memset(s_customTrackLap, 0, sizeof(s_customTrackLap));
}

void NativeObj_CustomTrack_Update(struct GameTracker *gt)
{
    if (!NativeObj_CustomTrack_ShouldRun(gt))
    {
        if (s_customTrackLevel != NULL)
            memset(s_customTrackLap, 0, sizeof(s_customTrackLap));
        s_customTrackInstance = NULL;
        s_customTrackLevel = NULL;
        return;
    }

    if (s_customTrackLevel != gt->level1)
    {
        s_customTrackLevel = gt->level1;
        s_customTrackInstance = NULL;
        memset(s_customTrackLap, 0, sizeof(s_customTrackLap));
    }

    if (s_customTrackModel == NULL)
        s_customTrackModel = NativeObj_LoadStaticTrack("assembly_lane");

    if (s_customTrackModel == NULL || s_customTrackInstance != NULL)
        return;

    s_customTrackInstance =
        INSTANCE_Birth3D(s_customTrackModel, "assembly_lane", NULL);

    if (s_customTrackInstance == NULL)
        return;

    NativeObj_CustomTrack_SetIdentity(&s_customTrackInstance->matrix);
    s_customTrackInstance->matrix.t[2] = ASSEMBLY_LANE_WORLD_OFFSET_Z;
    /* Do not use DRAW_HUGE here. Retail changes the instance
       camera-relative translation for that flag; a custom world mesh then
       renders in a different place from its collision coordinates. */
    s_customTrackInstance->flags |=
        DRAW_INSTANCE | VISIBLE_DURING_GAMEPLAY;
    s_customTrackInstance->flags &= ~DRAW_HUGE;

    /* Native OBJ draw dispatch expects one of the supported retail
       Instance+0x5c setup selectors. Zero-color keeps authored vertex/texture
       colors intact without depending on a level-instance lighting callback. */
    s_customTrackInstance->funcPtr[0] =
        (void *)(uintptr_t)0x8006c974u;

    s_customTrackInstance->alphaScale = 0;
    s_customTrackInstance->depthBiasNormal = 0xfe;
    s_customTrackInstance->depthBiasSecondary = 0x0c;

    SDL_Log("Custom track: Assembly Lane TT instance created (mode1=0x%x, level=%d)",
            (unsigned)gt->gameMode1, gt->levelID);
}

int NativeObj_CustomTrack_GetSpawn(int *x, int *y, int *z, int *rotY)
{
    /*
     * Keep spawn/respawn available for the whole temporary Crash Cove TT shell.
     * Mask-grab/restart can transiently set mode bits that make IsActive()
     * false even though this race still belongs to Assembly Lane.
     */
    const struct GameTracker *gt = sdata ? sdata->gGT : NULL;
    if (!NativeObj_CustomTrack_IsRaceShell(gt))
        return 0;

    /*
     * VehBirth can ask for the start position before MainFrame has created the
     * presentation Instance. Make the collision model available here too so
     * the very first spawn probe cannot race the renderer initialization.
     */
    if (s_customTrackModel == NULL)
        s_customTrackModel = NativeObj_LoadStaticTrack("assembly_lane");
    if (s_customTrackModel == NULL)
        return 0;

    /* model.obj is preprocessed with the exact centre of start.png
       as local (0,0,0). Keep the player close to the Instance origin so CTR's
       near/far matrix path cannot rescale world geometry behind our back. */
    if (x) *x = -110;
    if (y) *y = 96;
    if (z) *z = ASSEMBLY_LANE_WORLD_OFFSET_Z;
    if (rotY) *rotY = ANG_HALF_PI;
    return 1;
}

int NativeObj_CustomTrack_UpdateProgress(struct Driver *d)
{
    if (!NativeObj_CustomTrack_IsActive() || d == NULL)
        return 0;

    int driverID = d->driverID;
    if (driverID < 0 || driverID >= 8)
        return 1;

    struct NativeObjCustomTrackLapState *state =
        &s_customTrackLap[driverID];

    int x = CTR_MipsSra(d->posCurr.x, 8);
    /* Checkpoints were authored relative to the original start line.
       Remove the same global Z translation used by renderer/collider. */
    int z = CTR_MipsSra(d->posCurr.z, 8) - ASSEMBLY_LANE_WORLD_OFFSET_Z;

    /*
     * TT-only checkpoint scaffold.
     *
     * Assembly Lane is a single continuous route. The four broad gates below
     * intentionally follow the large geographic features instead of tiny BSP
     * quads, which makes this first importer tolerant of the N-Gage mesh.
     *
     * stage -1 : initial start-line crossing
     * stage  0 : reach the right-hand side
     * stage  1 : reach the upper factory section
     * stage  2 : reach the far-left section
     * stage  3 : return to the lower half
     * stage  4 : armed for a valid finish-line crossing
     */
    const int finishX = 0;
    const int finishZ = 0;
    const int finishHalfWidthZ = 115;

    if (!state->initialized)
    {
        state->initialized = 1;
        state->stage =
            (d->actionsFlagSet & ACTION_BEHIND_START_LINE) ? -1 : 0;
        state->lastX = x;
        state->lastZ = z;
    }

    int nearFinish =
        abs(z - finishZ) <= finishHalfWidthZ;

    int crossedForward =
        nearFinish &&
        state->lastX < finishX &&
        x >= finishX;

    if (state->stage < 0)
    {
        /*
         * Keep the pre-start value inside PlayLevel's "near" band. Crossing
         * forward jumps to the far side and lets the retail code clear
         * ACTION_BEHIND_START_LINE without counting a lap.
         */
        d->distanceToFinish_curr = crossedForward ? 33000 : 500;

        if (crossedForward)
            state->stage = 0;
    }
    else
    {
        if (state->stage == 0 && x > 720)
            state->stage = 1;

        if (state->stage == 1 && z < -2166)
            state->stage = 2;

        if (state->stage == 2 && x < -730)
            state->stage = 3;

        if (state->stage == 3 && z > -816)
            state->stage = 4;

        switch (state->stage)
        {
        default:
        case 0:
            d->distanceToFinish_curr = 28000;
            break;
        case 1:
            d->distanceToFinish_curr = 22000;
            break;
        case 2:
            d->distanceToFinish_curr = 15000;
            break;
        case 3:
            d->distanceToFinish_curr = 8000;
            break;
        case 4:
            /*
             * Entering the finish approach from the left makes the previous
             * frame "near". The forward line crossing then wraps to "far",
             * exactly matching PlayLevel's retail lap trigger.
             */
            if (nearFinish && x < finishX)
                d->distanceToFinish_curr = 500;
            else if (crossedForward)
                d->distanceToFinish_curr = 33000;
            else
                d->distanceToFinish_curr = 2500;
            break;
        }

        if (state->stage == 4 && crossedForward)
            state->stage = 0;
    }

    d->distanceToFinish_checkpoint = d->distanceToFinish_curr;
    d->checkpoint.currentIndex =
        (u8)((state->stage < 0) ? 0 : state->stage);
    d->actionsFlagSet &= ~ACTION_DRIVING_WRONG_WAY;

    state->lastX = x;
    state->lastZ = z;
    return 1;
}

static const struct NativeObjMesh *NativeObj_CustomTrack_Mesh(void)
{
    /*
     * VehBirth runs before the first MainFrame. Load the custom track lazily
     * here so the very first spawn probe already has collision geometry.
     */
    if (s_customTrackModel == NULL &&
        NativeObj_CustomTrack_IsRaceShell(sdata ? sdata->gGT : NULL))
        s_customTrackModel = NativeObj_LoadStaticTrack("assembly_lane");

    if (s_customTrackModel == NULL)
        return NULL;

    return NativeObj_GetMesh(s_customTrackModel);
}

static int NativeObj_CustomTrack_MaterialSolid(
    const struct NativeObjMaterial *m)
{
    if (m == NULL)
        return 0;

    if (strstr(m->name, "NotSolid") != NULL)
        return 0;

    if (strstr(m->name, "Pit") != NULL)
        return 0;

    return 1;
}

static int NativeObj_CustomTrack_MaterialWall(
    const struct NativeObjMaterial *m)
{
    return m != NULL && strstr(m->name, "Wall") != NULL;
}

static int NativeObj_CustomTrack_MaterialSlow(
    const struct NativeObjMaterial *m)
{
    return m != NULL && strstr(m->name, "MoveSlow") != NULL;
}

static void NativeObj_CustomTrack_WorldVertex(
    const struct NativeObjVertex *v,
    float out[3])
{
    const struct NativeObjMesh *mesh = NativeObj_CustomTrack_Mesh();

    if (mesh == NULL)
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }

    /*
     * Do not use ModelHeader.scale here. It is s16 and Assembly Lane needs
     * roughly 59k-61k world units in X/Z, so the retail field overflows.
     * Reconstruct the intended native scale directly in float.
     */
    out[0] =
        v->renderP[0] * (mesh->extent[0] * 1000.0f) / 4096.0f;
    out[1] =
        v->renderP[1] * (mesh->extent[1] * 1000.0f) / 4096.0f;
    out[2] =
        v->renderP[2] * (mesh->extent[2] * 1000.0f) / 4096.0f;

    if (s_customTrackInstance != NULL)
    {
        out[0] += (float)s_customTrackInstance->matrix.t[0];
        out[1] += (float)s_customTrackInstance->matrix.t[1];
    }
    /* Spawn collision queries run before the presentation instance exists.
       Thus both pre-birth and live probes must use the SAME Z shift. */
    out[2] += s_customTrackInstance != NULL
        ? (float)s_customTrackInstance->matrix.t[2]
        : (float)ASSEMBLY_LANE_WORLD_OFFSET_Z;
}

static float NativeObj_CustomTrack_Dot3(
    const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void NativeObj_CustomTrack_Sub3(
    const float a[3], const float b[3], float out[3])
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void NativeObj_CustomTrack_Cross3(
    const float a[3], const float b[3], float out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static int NativeObj_CustomTrack_Normal(
    const float a[3], const float b[3], const float c[3],
    float n[3])
{
    float ab[3], ac[3];
    NativeObj_CustomTrack_Sub3(b, a, ab);
    NativeObj_CustomTrack_Sub3(c, a, ac);
    NativeObj_CustomTrack_Cross3(ab, ac, n);

    float len2 = NativeObj_CustomTrack_Dot3(n, n);
    if (len2 < 0.000001f)
        return 0;

    float invLen = 1.0f / sqrtf(len2);
    n[0] *= invLen;
    n[1] *= invLen;
    n[2] *= invLen;
    return 1;
}

static int NativeObj_CustomTrack_PointInTri3D(
    const float p[3],
    const float a[3], const float b[3], const float c[3],
    const float n[3])
{
    const float eps = -0.75f;
    float edge[3], rel[3], cross[3];

    NativeObj_CustomTrack_Sub3(b, a, edge);
    NativeObj_CustomTrack_Sub3(p, a, rel);
    NativeObj_CustomTrack_Cross3(edge, rel, cross);
    if (NativeObj_CustomTrack_Dot3(cross, n) < eps)
        return 0;

    NativeObj_CustomTrack_Sub3(c, b, edge);
    NativeObj_CustomTrack_Sub3(p, b, rel);
    NativeObj_CustomTrack_Cross3(edge, rel, cross);
    if (NativeObj_CustomTrack_Dot3(cross, n) < eps)
        return 0;

    NativeObj_CustomTrack_Sub3(a, c, edge);
    NativeObj_CustomTrack_Sub3(p, c, rel);
    NativeObj_CustomTrack_Cross3(edge, rel, cross);
    if (NativeObj_CustomTrack_Dot3(cross, n) < eps)
        return 0;

    return 1;
}

static int NativeObj_CustomTrack_PointInTriXZ(
    float x, float z,
    const float a[3], const float b[3], const float c[3],
    float *wa, float *wb, float *wc)
{
    float denom =
        (b[2] - c[2]) * (a[0] - c[0]) +
        (c[0] - b[0]) * (a[2] - c[2]);

    if (fabsf(denom) < 0.00001f)
        return 0;

    float u =
        ((b[2] - c[2]) * (x - c[0]) +
         (c[0] - b[0]) * (z - c[2])) / denom;
    float v =
        ((c[2] - a[2]) * (x - c[0]) +
         (a[0] - c[0]) * (z - c[2])) / denom;
    float w = 1.0f - u - v;

    const float eps = -0.0025f;
    if (u < eps || v < eps || w < eps)
        return 0;

    if (wa) *wa = u;
    if (wb) *wb = v;
    if (wc) *wc = w;
    return 1;
}

static s16 NativeObj_CustomTrack_NormalS16(float v)
{
    int out = (int)lroundf(v * 4096.0f);
    if (out < -4096) out = -4096;
    if (out > 4096) out = 4096;
    return (s16)out;
}

static void NativeObj_CustomTrack_SetHit(
    struct ScratchpadStruct *sps,
    struct QuadBlock *quad,
    const float hit[3],
    const float normal[3],
    int hitFraction)
{
    SVec3 pos = {
        .x = (s16)lroundf(hit[0]),
        .y = (s16)lroundf(hit[1]),
        .z = (s16)lroundf(hit[2]),
    };

    sps->boolDidTouchQuadblock = 1;
    sps->hit.ptrQuadblock = quad;
    sps->hit.hitPos = pos;
    sps->Union.QuadBlockColl.hitPos = pos;
    sps->hit.plane.normal.x = NativeObj_CustomTrack_NormalS16(normal[0]);
    sps->hit.plane.normal.y = NativeObj_CustomTrack_NormalS16(normal[1]);
    sps->hit.plane.normal.z = NativeObj_CustomTrack_NormalS16(normal[2]);
    sps->hit.reorderResult = COLL_TRIANGLE_CLIP_FACE;
    sps->hit.triangleID = 0;
    sps->hitFraction = hitFraction;

    sps->hitLevelTriangle.v0 = NULL;
    sps->hitLevelTriangle.v1 = NULL;
    sps->hitLevelTriangle.v2 = NULL;
}

int NativeObj_CustomTrack_VerticalProbe(
    int x, int yA, int yB, int z,
    struct ScratchpadStruct *sps)
{
    const struct NativeObjMesh *mesh = NativeObj_CustomTrack_Mesh();
    if (!NativeObj_CustomTrack_IsRaceShell(sdata ? sdata->gGT : NULL) ||
        mesh == NULL || sps == NULL)
        return 0;

    float lowY = (float)((yA < yB) ? yA : yB);
    float highY = (float)((yA > yB) ? yA : yB);
    float bestY = -1000000.0f;
    float bestN[3] = {0.0f, 1.0f, 0.0f};
    const struct NativeObjMaterial *bestMat = NULL;
    int found = 0;

    for (int i = 0; i < mesh->triangleCount; ++i)
    {
        const struct NativeObjTriangle *t = &mesh->triangles[i];
        const struct NativeObjMaterial *m =
            &mesh->materials[t->material];

        if (!NativeObj_CustomTrack_MaterialSolid(m) ||
            NativeObj_CustomTrack_MaterialWall(m))
            continue;

        float a[3], b[3], c[3], n[3];
        NativeObj_CustomTrack_WorldVertex(&t->v[0], a);
        NativeObj_CustomTrack_WorldVertex(&t->v[1], b);
        NativeObj_CustomTrack_WorldVertex(&t->v[2], c);

        if (!NativeObj_CustomTrack_Normal(a, b, c, n))
            continue;

        if (n[1] < 0.0f)
        {
            n[0] = -n[0];
            n[1] = -n[1];
            n[2] = -n[2];
        }

        if (n[1] < 0.20f)
            continue;

        float wa, wb, wc;
        if (!NativeObj_CustomTrack_PointInTriXZ(
                (float)x, (float)z, a, b, c, &wa, &wb, &wc))
            continue;

        float y = wa*a[1] + wb*b[1] + wc*c[1];
        if (y < lowY - 2.0f || y > highY + 2.0f)
            continue;

        if (!found || y > bestY)
        {
            found = 1;
            bestY = y;
            bestN[0] = n[0];
            bestN[1] = n[1];
            bestN[2] = n[2];
            bestMat = m;
        }
    }

    if (!found)
        return 0;

    s_customTrackGroundQuad.terrain_type =
        NativeObj_CustomTrack_MaterialSlow(bestMat)
            ? TERRAIN_MUD
            : TERRAIN_ASPHALT;

    float hit[3] = {(float)x, bestY, (float)z};
    NativeObj_CustomTrack_SetHit(
        sps, &s_customTrackGroundQuad, hit, bestN, 0);
    return 1;
}

int NativeObj_CustomTrack_FixedSearch(
    struct Driver *d, struct ScratchpadStruct *sps)
{
    if (d == NULL || sps == NULL)
        return 0;

    int x = CTR_MipsSra(d->posCurr.x, 8);
    int y = CTR_MipsSra(d->posCurr.y, 8);
    int z = CTR_MipsSra(d->posCurr.z, 8);

    /*
     * Search symmetrically around the kart. CTR uses +Y upward (gravity makes
     * velocity.y negative), so this also lets us recover a kart that crossed a
     * few units below the custom road before the fixed collision tick ran.
     */
    int hit = NativeObj_CustomTrack_VerticalProbe(
        x, y - 1024, y + 1024, z, sps);

    /*
     * If a late load / long frame let the kart pass farther than the normal
     * local probe, recover from any valid floor at this X/Z instead of letting
     * the prototype fall forever.
     */
    if (!hit)
        hit = NativeObj_CustomTrack_VerticalProbe(
            x, -30000, 30000, z, sps);

    if (hit && d->velocity.y <= 0)
    {
        const int groundY = (int)sps->Union.QuadBlockColl.hitPos.y;
        const int groundFixed = CTR_MipsSll(groundY, 8);

        /*
         * +Y is upward in CTR. If we are already under the floor, snap back to
         * it immediately. This is only a safety net for the custom TT track.
         */
        if (d->posCurr.y < groundFixed)
        {
            d->posCurr.y = groundFixed;
            d->posPrev.y = groundFixed;
            d->velocity.y = 0;
            d->ySpeed = 0;
            y = groundY;
        }
    }

    if (d->driverID == 0 && s_customTrackFixedDebugCount < 40)
    {
        SDL_Log(
            "AssemblyLane FixedSearch[%d]: pos=(%d,%d,%d) model=%p hit=%d hitY=%d groundedQuad=%p",
            s_customTrackFixedDebugCount,
            x, y, z,
            (void *)s_customTrackModel,
            hit,
            hit ? (int)sps->Union.QuadBlockColl.hitPos.y : -32768,
            hit ? (void *)sps->hit.ptrQuadblock : NULL);
        s_customTrackFixedDebugCount++;
    }

    return hit;
}

int NativeObj_CustomTrack_MovedSearch(
    struct Driver *d, struct ScratchpadStruct *sps)
{
    const struct NativeObjMesh *mesh = NativeObj_CustomTrack_Mesh();
    if (!NativeObj_CustomTrack_IsActive() ||
        mesh == NULL || d == NULL || sps == NULL)
        return 0;

    float start[3] = {
        (float)sps->Union.QuadBlockColl.pos.x,
        (float)sps->Union.QuadBlockColl.pos.y,
        (float)sps->Union.QuadBlockColl.pos.z,
    };
    float end[3] = {
        (float)sps->Input1.pos.x,
        (float)sps->Input1.pos.y,
        (float)sps->Input1.pos.z,
    };
    float delta[3] = {
        end[0] - start[0],
        end[1] - start[1],
        end[2] - start[2],
    };

    float bestT = 2.0f;
    float bestHit[3] = {0,0,0};
    float bestN[3] = {0,1,0};
    struct QuadBlock *bestQuad = NULL;
    const struct NativeObjMaterial *bestMat = NULL;
    const float radius = 25.0f;

    for (int i = 0; i < mesh->triangleCount; ++i)
    {
        const struct NativeObjTriangle *t = &mesh->triangles[i];
        const struct NativeObjMaterial *m =
            &mesh->materials[t->material];

        /*
         * Swept movement collision is ONLY for authored walls.
         * Ground/ramp triangles are handled exclusively by VerticalProbe.
         * Feeding ground into the swept-sphere path causes the kart to collide
         * with the road every frame and only move while airborne.
         */
        if (!NativeObj_CustomTrack_MaterialSolid(m) ||
            !NativeObj_CustomTrack_MaterialWall(m))
            continue;

        float a[3], b[3], c[3], n[3];
        NativeObj_CustomTrack_WorldVertex(&t->v[0], a);
        NativeObj_CustomTrack_WorldVertex(&t->v[1], b);
        NativeObj_CustomTrack_WorldVertex(&t->v[2], c);

        if (!NativeObj_CustomTrack_Normal(a, b, c, n))
            continue;

        const int isWall = 1;

        float fromA[3], toA[3];
        NativeObj_CustomTrack_Sub3(start, a, fromA);
        NativeObj_CustomTrack_Sub3(end, a, toA);
        float d0 = NativeObj_CustomTrack_Dot3(fromA, n);
        float d1 = NativeObj_CustomTrack_Dot3(toA, n);

        if (isWall && d0 < 0.0f)
        {
            n[0] = -n[0];
            n[1] = -n[1];
            n[2] = -n[2];
            d0 = -d0;
            d1 = -d1;
        }

        float planeRadius = radius;
        if (!isWall && n[1] < 0.20f)
            continue;

        float tHit;
        if (d0 > planeRadius && d1 <= planeRadius)
        {
            float denom = d0 - d1;
            if (fabsf(denom) < 0.00001f)
                continue;
            tHit = (d0 - planeRadius) / denom;
        }
        else if (d0 >= 0.0f && d0 <= planeRadius && d1 < d0)
        {
            tHit = 0.0f;
        }
        else
        {
            continue;
        }

        if (tHit < 0.0f || tHit > 1.0f || tHit >= bestT)
            continue;

        float center[3] = {
            start[0] + delta[0] * tHit,
            start[1] + delta[1] * tHit,
            start[2] + delta[2] * tHit,
        };
        float surface[3] = {
            center[0] - n[0] * planeRadius,
            center[1] - n[1] * planeRadius,
            center[2] - n[2] * planeRadius,
        };

        if (!NativeObj_CustomTrack_PointInTri3D(
                surface, a, b, c, n))
            continue;

        bestT = tHit;
        bestHit[0] = surface[0];
        bestHit[1] = surface[1];
        bestHit[2] = surface[2];
        bestN[0] = n[0];
        bestN[1] = n[1];
        bestN[2] = n[2];
        bestQuad = isWall
            ? &s_customTrackWallQuad
            : &s_customTrackGroundQuad;
        bestMat = m;
    }

    if (bestQuad == NULL)
        return 0;

    if (bestQuad == &s_customTrackGroundQuad)
    {
        s_customTrackGroundQuad.terrain_type =
            NativeObj_CustomTrack_MaterialSlow(bestMat)
                ? TERRAIN_MUD
                : TERRAIN_ASPHALT;
    }

    int fraction = (int)lroundf(bestT * 4096.0f);
    if (fraction < 0) fraction = 0;
    if (fraction > 4095) fraction = 4095;

    NativeObj_CustomTrack_SetHit(
        sps, bestQuad, bestHit, bestN, fraction);
    return 1;
}

struct Model *NativeObj_LoadVanillaBodyPatch(const char *characterSlug, const char *headerName)
{
    char key[192];
    char dir[320];
    struct NativeObjCache *c;

    if (characterSlug == NULL || !*characterSlug ||
        headerName == NULL || !*headerName)
        return NULL;

    snprintf(key, sizeof(key), "bodypatch:%s:%s",
             characterSlug, headerName);
    snprintf(dir, sizeof(dir),
             "mods/vanilla_racer_masks/patches/%s/%s",
             characterSlug, headerName);

    c = Obj_FindInDirectory(key, dir, "body_patch");
    if (c == NULL)
        return NULL;

    return Obj_GetOrCreateModel(c, "body_patch");
}

static void Obj_LoadKartColors(struct NativeObjMesh *mesh,
                               const struct VehiclePartDef *part,
                               const char *dir)
{
    unsigned int base = 0;
    if (mesh == NULL || part == NULL || mesh->colorsPrepared)
        return;
    mesh->colorsPrepared = 1;
    if (part->colorCount < 2 || part->colorCount > NATIVE_OBJ_MAX_COLORS)
        return;

    /* Only a single atlas can be swapped without changing material UVs.
       Complex multi-texture vehicles keep their original appearance. */
    for (int i = 0; i < mesh->materialCount; ++i)
    {
        unsigned int texture = mesh->materials[i].texture;
        if (texture == 0)
            continue;
        if (base != 0 && base != texture)
        {
            Platform_Log("[KART COLOR] multi-atlas kart %s: palette unsupported\n",
                         part->slug);
            return;
        }
        base = texture;
    }
    if (base == 0)
        return;

    mesh->colorBaseTexture = base;
    mesh->colorTextures[0] = base;
    for (int index = 1; index < part->colorCount; ++index)
    {
        char local[512], path[1024];
        SDL_Surface *source = NULL;
        SDL_Surface *rgba = NULL;
        unsigned char *pixels = NULL;
        unsigned int texture = 0;
        if (Obj_Path(local, sizeof(local), dir,
                     part->colorTextureNames[index]) &&
            NativeAssets_ResolvePath(local, path, sizeof(path)))
        {
            source = SDL_LoadPNG(path);
            if (source == NULL)
                source = SDL_LoadBMP(path);
        }
        if (source != NULL)
        {
            rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(source);
        }
        if (rgba != NULL && rgba->w > 0 && rgba->h > 0)
        {
            size_t rowBytes = (size_t)rgba->w * 4u;
            pixels = malloc(rowBytes * (size_t)rgba->h);
            if (pixels != NULL)
            {
                for (int y = 0; y < rgba->h; ++y)
                    memcpy(pixels + rowBytes * (size_t)y,
                           (const char *)rgba->pixels + rgba->pitch * y,
                           rowBytes);
                texture = NativeRenderer_CreateImageTexture(
                    pixels, rgba->w, rgba->h);
            }
        }
        free(pixels);
        if (rgba != NULL)
            SDL_DestroySurface(rgba);
        if (texture == 0)
        {
            Platform_Log("[KART COLOR] failed loading %s for %s\n",
                         part->colorTextureNames[index], part->slug);
            for (int used = 1; used < index; ++used)
            {
                NativeRenderer_ReleaseImageTexture(
                    mesh->colorTextures[used]);
                mesh->colorTextures[used] = 0;
            }
            mesh->colorBaseTexture = 0;
            mesh->colorCount = 0;
            return;
        }
        mesh->colorTextures[index] = texture;
    }

    mesh->colorCount = part->colorCount;
    Platform_Log("[KART COLOR] %s: loaded %d colors with one OBJ\n",
                 part->slug, (int)mesh->colorCount);
}

struct Model *NativeObj_LoadVehiclePart(int kind, int partID)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(kind, partID);
    const char *category;
    char key[160];
    char dir[256];

    if (part == NULL)
        return NULL;

    category = kind == VEHICLE_PART_KART ? "karts" : "wheels";

    snprintf(key, sizeof(key), "part:%s:%s",
             category, part->slug);
    if (part->unifiedMod)
        snprintf(dir, sizeof(dir), "mods/%s", part->slug);
    else
        snprintf(dir, sizeof(dir), "mods/%s/%s", category, part->slug);

    struct NativeObjCache *c =
        Obj_FindInDirectory(key, dir, part->assetName);
    if (c == NULL)
        return NULL;

    if (kind == VEHICLE_PART_KART)
        Obj_LoadKartColors(&c->mesh, part, dir);

    c->mesh.userScale = part->modelScale;
    c->mesh.userOffset[0] = part->modelOffset[0];
    c->mesh.userOffset[1] = part->modelOffset[1];
    c->mesh.userOffset[2] = part->modelOffset[2];

    return Obj_GetOrCreateModel(c, part->assetName);
}
const struct NativeObjMesh *NativeObj_GetMesh(const struct Model *model)
{
    if (!model || !model->headers || model->headers[0].unk1 != NATIVE_OBJ_MODEL_MAGIC) return NULL;

    /* Model is the first member of NativeObjDescriptor. Once the magic header
     * confirms this is one of our native OBJ models, resolve the mesh directly
     * instead of scanning every loaded mod on every draw. */
    const struct NativeObjDescriptor *descriptor = (const struct NativeObjDescriptor *)model;
    return descriptor->mesh;
}

static struct Model *NativeObj_LoadSelectedRetailWheelPart(void)
{
    int count = VehiclePartRegistry_GetCount(VEHICLE_PART_WHEELS);

    for (int i = 0; i < count; i++)
    {
        const struct VehiclePartDef *part =
            VehiclePartRegistry_GetByIndex(VEHICLE_PART_WHEELS, i);

        if (part == NULL)
            continue;

        if (!SDL_strcasecmp(part->assetName, s_retailWheelOverrideAsset) ||
            !SDL_strcasecmp(part->slug, s_retailWheelOverrideAsset))
        {
            return NativeObj_LoadVehiclePart(VEHICLE_PART_WHEELS, i);
        }
    }

    return NULL;
}

const struct NativeObjMesh *NativeObj_GetRetailWheelOverrideMesh(void)
{
    if (!s_retailWheelOverrideTried)
    {
        s_retailWheelOverrideTried = 1;
        s_retailWheelOverrideModel =
            s_retailWheelOverrideAsset[0] ?
            NativeObj_LoadSelectedRetailWheelPart() : NULL;

        if (s_retailWheelOverrideModel != NULL)
        {
            const struct NativeObjMesh *mesh =
                NativeObj_GetMesh(s_retailWheelOverrideModel);
            Platform_Log("[CTR Native] 3D wheels: loaded %s (%d triangles)\n",
                         s_retailWheelOverrideAsset,
                         mesh ? mesh->triangleCount : 0);
        }
        else if (s_retailWheelOverrideAsset[0])
        {
            Platform_Log("[CTR Native] 3D wheels: %s unavailable, using 2D fallback\n",
                         s_retailWheelOverrideAsset);
        }
    }

    return s_retailWheelOverrideModel ?
           NativeObj_GetMesh(s_retailWheelOverrideModel) : NULL;
}

static struct Model *NativeObj_GetWheelModelForDriver(const struct Driver *driver)
{
    if (driver == NULL)
        return NULL;

    /* Adventure deliberately ignores free-form vehicle loadouts and the
       global wheel override so story mode keeps CTR's original combinations. */
    if (RacerLoadout_IsAdventureLocked())
        return NULL;

    /* A hovercraft suppresses all modular and global replacement wheels. */
    if (VehiclePartRegistry_HidesWheels(
            RacerLoadout_GetEffectiveKart((int)driver->driverID)))
        return NULL;

    {
        const int driverID = (int)driver->driverID;
        if (driverID >= 0 && driverID < RACER_LOADOUT_MAX_DRIVERS)
        {
            const int wheelID = RacerLoadout_GetEffectiveWheels(driverID);
            if (wheelID >= 0)
                return NativeObj_LoadVehiclePart(VEHICLE_PART_WHEELS, wheelID);

            /* ORIGINAL is an explicit per-racer vanilla choice. INHERIT means
               no per-racer decision was made, so the Mods-menu global wheel
               selection may still supply the default (useful for bots). */
            if (wheelID == RACER_LOADOUT_ORIGINAL)
                return NULL;
        }
    }

    (void)NativeObj_GetRetailWheelOverrideMesh();
    return s_retailWheelOverrideModel;
}

const struct NativeObjMesh *NativeObj_GetWheelMeshForDriver(const struct Driver *driver)
{
    struct Model *model = NativeObj_GetWheelModelForDriver(driver);
    return model ? NativeObj_GetMesh(model) : NULL;
}

const struct NativeObjMesh *NativeObj_GetKartMeshForDriver(const struct Driver *driver)
{
    int driverID;
    int kartID;
    struct Model *model;

    if (driver == NULL || RacerLoadout_IsAdventureLocked())
        return NULL;

    driverID = (int)driver->driverID;
    if (driverID < 0 || driverID >= RACER_LOADOUT_MAX_DRIVERS)
        return NULL;

    kartID = RacerLoadout_GetEffectiveKart(driverID);
    if (kartID < 0)
        return NULL;

    model = NativeObj_LoadVehiclePart(VEHICLE_PART_KART, kartID);
    return model ? NativeObj_GetMesh(model) : NULL;
}

static const struct Driver *NativeObj_GetDriverForInstance(
    const struct Instance *inst,
    int *outDriverID)
{
    struct GameTracker *gt = sdata->gGT;

    if (outDriverID != NULL)
        *outDriverID = -1;

    if (inst == NULL || gt == NULL)
        return NULL;

    /*
     * Racer instances already point at their Driver through thread->object.
     * Hit that O(1) path first; this function is called repeatedly while
     * drawing modular body/kart/seat attachments for every visible racer.
     */
    if (inst->thread != NULL &&
        inst->thread->object != NULL)
    {
        const struct Driver *driver =
            (const struct Driver *)inst->thread->object;
        const int driverID = (int)driver->driverID;

        if (driver->instSelf == inst &&
            driverID >= 0 &&
            driverID < RACER_LOADOUT_MAX_DRIVERS)
        {
            if (outDriverID != NULL)
                *outDriverID = driverID;
            return driver;
        }
    }

    /* Fallback for unusual instances whose thread object is not their Driver. */
    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
    {
        const struct Driver *driver = gt->drivers[i];
        if (driver != NULL && driver->instSelf == inst)
        {
            if (outDriverID != NULL)
                *outDriverID = (int)driver->driverID;
            return driver;
        }
    }

    return NULL;
}

const struct NativeObjMesh *NativeObj_GetKartMeshForInstance(
    const struct Instance *inst)
{
    int driverID;
    const struct Driver *driver =
        NativeObj_GetDriverForInstance(inst, &driverID);

    if (driver == NULL ||
        driverID < 0 ||
        RacerLoadout_IsAdventureLocked())
        return NULL;

    return NativeObj_GetKartMeshForDriver(driver);
}

unsigned int NativeObj_GetKartColorTextureForInstance(
    const struct Instance *inst, const struct NativeObjMesh *mesh)
{
    int driverID = -1;
    const struct Driver *driver;
    int colorID;
    if (inst == NULL || mesh == NULL || mesh->colorCount < 2 ||
        RacerLoadout_IsAdventureLocked())
        return 0;

    driver = NativeObj_GetDriverForInstance(inst, &driverID);
    if (driver == NULL || driverID < 0)
        return 0;
    colorID = RacerLoadout_GetKartColor(driverID);
    if (colorID <= 0 || colorID >= mesh->colorCount)
        return 0;
    if (mesh != NativeObj_GetKartMeshForDriver(driver))
        return 0;
    return mesh->colorTextures[colorID];
}

b32 NativeObj_GetSeatOffsetForInstance(
    const struct Instance *inst,
    float out[3])
{
    int driverID;
    int kartID;
    const struct Driver *driver =
        NativeObj_GetDriverForInstance(inst, &driverID);

    if (out != NULL)
        out[0] = out[1] = out[2] = 0.0f;

    if (out == NULL ||
        driver == NULL ||
        driverID < 0 ||
        RacerLoadout_IsAdventureLocked())
        return false;

    kartID = RacerLoadout_GetEffectiveKart(driverID);
    if (kartID < 0)
        return false;

    return VehiclePartRegistry_GetSeatOffset(kartID, out);
}

const struct NativeObjMesh *NativeObj_GetModularKartMeshForInstance(
    const struct Instance *inst)
{
    int driverID;
    const struct Driver *driver =
        NativeObj_GetDriverForInstance(inst, &driverID);

    if (driver == NULL ||
        driverID < 0 ||
        !CharacterRegistry_IsModularBody(data.characterIDs[driverID]))
        return NULL;

    return NativeObj_GetKartMeshForInstance(inst);
}

b32 NativeObj_GetModularBodySeatOffset(
    const struct Instance *inst,
    float out[3])
{
    int driverID;
    const struct Driver *driver =
        NativeObj_GetDriverForInstance(inst, &driverID);

    if (out != NULL)
        out[0] = out[1] = out[2] = 0.0f;

    if (out == NULL ||
        driver == NULL ||
        driverID < 0 ||
        !CharacterRegistry_IsModularBody(data.characterIDs[driverID]))
        return false;

    return NativeObj_GetSeatOffsetForInstance(inst, out);
}

void NativeObj_SetRetailWheelOverrideAsset(const char *assetName)
{
    const char *next = assetName ? assetName : "";

    if (!strcmp(s_retailWheelOverrideAsset, next))
        return;

    snprintf(s_retailWheelOverrideAsset,
             sizeof(s_retailWheelOverrideAsset), "%s", next);
    s_retailWheelOverrideModel = NULL;
    s_retailWheelOverrideTried = 0;
    memset(s_objWheels, 0, sizeof(s_objWheels));

    Platform_Log("[CTR Native] 3D wheels: selected asset=%s\n",
                 s_retailWheelOverrideAsset[0] ?
                 s_retailWheelOverrideAsset : "(none)");
}

const char *NativeObj_GetRetailWheelOverrideAsset(void)
{
    return s_retailWheelOverrideAsset;
}

int NativeObj_ShouldReplaceRetailWheels(const struct Driver *driver)
{
    int driverID;
    int characterID;
    const struct NativeObjMesh *driverMesh;

    if ((driver == NULL) || (driver->instSelf == NULL))
        return 0;

    driverID = (int)driver->driverID;
    if (driverID < 0 || driverID >= RACER_LOADOUT_MAX_DRIVERS)
        return 0;

    characterID = (int)data.characterIDs[driverID];
    driverMesh = NativeObj_GetMesh(driver->instSelf->model);

    /* Legacy combined OBJ racers keep their existing wheel behavior.
       Body-only modular racers are the exception: their selected wheel set
       belongs to the independent vehicle loadout. */
    if (driverMesh != NULL &&
        !CharacterRegistry_IsModularBody(characterID))
        return 0;

    return NativeObj_GetWheelMeshForDriver(driver) != NULL;
}

void NativeObj_Shutdown(void)
{
    memset(s_objWheels, 0, sizeof(s_objWheels));
    s_retailWheelOverrideModel = NULL;
    s_retailWheelOverrideTried = 0;
    while (s_objCache) { struct NativeObjCache *next = s_objCache->next; Obj_Free(&s_objCache->mesh); free(s_objCache->descriptor); free(s_objCache); s_objCache = next; }
}

/* Called after simulation, independent of visibility or number of viewports.
 * Project Q8 world travel onto the model's forward axis. Sideways sliding
 * must not count as wheel rolling or borrow a sign from the physics heading. */
void NativeObj_UpdateWheels(void)
{
    struct GameTracker *gt = sdata->gGT;

    if (gt == NULL) return;

    for (int i = 0; i < 8; ++i) {
        struct Driver *d = gt->drivers[i];
        struct ObjWheelState *state = &s_objWheels[i];
        const struct NativeObjMesh *mesh = NULL;
        const struct Model *wheelModel = NULL;
        int retailOverrideActive = 0;

        if (!d || !d->instSelf) {
            memset(state, 0, sizeof(*state));
            continue;
        }

        const int characterID = (int)data.characterIDs[i];
        const struct NativeObjMesh *driverMesh = NativeObj_GetMesh(d->instSelf->model);
        struct Model *selectedWheelModel = NativeObj_GetWheelModelForDriver(d);
        const struct NativeObjMesh *selectedWheelMesh =
            selectedWheelModel ? NativeObj_GetMesh(selectedWheelModel) : NULL;

        if ((selectedWheelMesh != NULL) &&
            ((driverMesh == NULL) ||
             CharacterRegistry_IsModularBody(characterID)) &&
            (d->wheelSize > 0)) {
            mesh = selectedWheelMesh;
            wheelModel = selectedWheelModel;
            retailOverrideActive = 1;
        } else if ((driverMesh != NULL) &&
                   !CharacterRegistry_IsModularBody(characterID) &&
                   !CharacterRegistry_HasWheels(characterID)) {
            mesh = driverMesh;
            wheelModel = d->instSelf->model;
        }

        if (!mesh || !wheelModel) {
            memset(state, 0, sizeof(*state));
            continue;
        }

        if (state->instance == d->instSelf && state->model == wheelModel &&
            (unsigned int)gt->timer - state->tick == 1) {
            float dx = ((double)d->posCurr.x - state->position[0]) / 256.0;
            float dy = ((double)d->posCurr.y - state->position[1]) / 256.0;
            float dz = ((double)d->posCurr.z - state->position[2]) / 256.0;
            const MATRIX *orientation = &d->instSelf->matrix;
            float fx = orientation->m[0][2], fy = orientation->m[1][2], fz = orientation->m[2][2];
            float forwardLength = sqrtf(fx * fx + fy * fy + fz * fz);

            /* Ignore relocation/respawn jumps and stopped vehicles. */
            if (dx * dx + dy * dy + dz * dz < 512.0f * 512.0f && forwardLength > 0) {
                float travel = (dx * fx + dy * fy + dz * fz) / forwardLength;
                for (int w = 0; w < 4; ++w) {
                    float radius;
                    if (retailOverrideActive) {
                        /* DrawTires' generated wheel radius is approximately
                           wheelSize/64 in kart-local units. Use that same real
                           radius so the imported mesh rolls at CTR's speed. */
                        radius = fabsf((float)d->wheelSize) / 64.0f;
                    } else {
                        float scale = fabsf(d->instSelf->scale.y / 4096.0f);
                        radius = mesh->wheels[w].radius * (1016.0f * 1000 / 16384) * scale;
                    }

                    if (radius > 0.001f)
                        state->angles[w] = remainderf(state->angles[w] + travel / radius, 6.28318530718f);
                }
            }
        } else {
            memset(state->angles, 0, sizeof(state->angles));
        }

        state->steerAngle = (float)d->wheelRotation * (6.28318530718f / 1024.0f);
        state->instance = d->instSelf;
        state->model = wheelModel;
        state->tick = gt->timer;
        state->position[0] = d->posCurr.x;
        state->position[1] = d->posCurr.y;
        state->position[2] = d->posCurr.z;
    }
}

void NativeObj_GetWheelState(const struct Instance *inst, float angles[4], float *steerAngle)
{
    if (angles != NULL)
        memset(angles, 0, 4 * sizeof(*angles));
    if (steerAngle != NULL)
        *steerAngle = 0.0f;
    if (inst == NULL)
        return;

    for (int i = 0; i < 8; ++i)
    {
        if (s_objWheels[i].instance == inst)
        {
            if (angles != NULL)
                memcpy(angles, s_objWheels[i].angles, sizeof(s_objWheels[i].angles));
            if (steerAngle != NULL)
                *steerAngle = s_objWheels[i].steerAngle;
            return;
        }
    }
}

void NativeObj_GetWheelAngles(const struct Instance *inst, float angles[4])
{
    NativeObj_GetWheelState(inst, angles, NULL);
}

float NativeObj_GetWheelSteerAngle(const struct Instance *inst)
{
    float steerAngle = 0.0f;
    NativeObj_GetWheelState(inst, NULL, &steerAngle);
    return steerAngle;
}
