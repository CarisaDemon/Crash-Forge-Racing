/* Static OBJ racers. Geometry and GL images are host caches; the small model
 * descriptor lives in the game arena and contains no pointers into that cache. */
#include <common.h>
#include <platform/native_obj.h>
#include <platform/native_assets.h>
#include <platform/native_renderer.h>
#include <platform/native_log.h>
#include <CharacterRegistry.h>
#if defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_LIMIT 100000
struct NativeObjDescriptor { struct Model model; struct ModelHeader header; struct ModelFrame frame; };
struct NativeObjCache { char name[64]; struct NativeObjMesh mesh; struct NativeObjDescriptor *descriptor; struct NativeObjCache *next; };
static struct NativeObjCache *s_objCache;
static struct Model *s_retailWheelOverrideModel;
static int s_retailWheelOverrideTried;
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
    for (int i = 0; i < mesh->materialCount; ++i)
        NativeRenderer_ReleaseImageTexture(mesh->materials[i].texture);
    free(mesh->materials);
    free(mesh->triangles);
    memset(mesh, 0, sizeof(*mesh));
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
                        NativeRenderer_ReleaseImageTexture(m->texture);
                        m->texture = NativeRenderer_CreateImageTexture(pixels, rgba->w, rgba->h);
                        ok = m->texture != 0;
                        free(pixels);
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

static int Obj_Index(const char *s, int count, const char **end)
{
    char *tail;
    long value = strtol(s, &tail, 10);
    *end = tail;
    if (tail == s || value == 0 || value > count || value < -count) return -1;
    return value < 0 ? count + (int)value : (int)value - 1;
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
            struct NativeObjVertex v = {{0}, {0}, {1,1,1}};
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
    fclose(f); free(positions); free(uvs);
    if (!ok) return 0;
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

static struct NativeObjCache *Obj_Find(const char *name)
{
    for (struct NativeObjCache *c = s_objCache; c; c = c->next) if (!strcmp(c->name, name)) return c;
    char dir[128], host[1024], filename[1024];
    snprintf(dir, sizeof(dir), "mods/racers/%s", name);
    if (!NativeAssets_BuildPath(dir, host, sizeof(host))) return NULL;
    int count = 0;
    char **files = SDL_GlobDirectory(host, "*.obj", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!files || !count) { SDL_free(files); return NULL; }
    /* Prefer assetName.obj, then model.obj; otherwise require an unambiguous file. */
    int selected = count == 1 ? 0 : -1;
    for (int i = 0; i < count; ++i) if (!SDL_strcasecmp(files[i], "model.obj")) selected = i;
    snprintf(filename, sizeof(filename), "%s.obj", name);
    for (int i = 0; i < count; ++i) if (!SDL_strcasecmp(files[i], filename)) selected = i;
    if (selected < 0) { SDL_Log("OBJ racer %s: ambiguous OBJ folder; using CTR fallback", name); SDL_free(files); return NULL; }
    snprintf(filename, sizeof(filename), "%s", files[selected]);
    SDL_free(files);
    struct NativeObjCache *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->mesh.userScale = 1.0f;
    if (!Obj_Read(&c->mesh, dir, filename)) {
        SDL_Log("OBJ racer %s: invalid mesh, material or texture; using CTR fallback", name);
        Obj_Free(&c->mesh); free(c); return NULL;
    }
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->next = s_objCache; s_objCache = c;
    SDL_Log("OBJ racer %s: loaded %d triangles, %d materials", name, c->mesh.triangleCount, c->mesh.materialCount);
    return c;
}

struct Model *NativeObj_LoadRacer(const char *assetName)
{
    /* CharacterRegistry keeps native asset keys separately from the retail
     * 16-byte Model::name field. Keep the full native key in the OBJ cache;
     * the retail name may be truncated only as a compatibility label. */
    if ((assetName == NULL) || !*assetName ||
        (strlen(assetName) >= sizeof(((struct NativeObjCache *)0)->name)))
    {
        SDL_Log("OBJ racer: asset_name is empty or too long; using CTR fallback");
        return NULL;
    }

    struct NativeObjCache *c = Obj_Find(assetName);
    if (!c) return NULL;

    for (int i = 0; i < CharacterRegistry_GetCount(); ++i)
    {
        const struct CharacterDef *character = CharacterRegistry_GetByRosterIndex(i);
        if ((character != NULL) && !SDL_strcasecmp(character->assetName, assetName))
        {
            c->mesh.userScale = character->modelScale;
            c->mesh.userOffset[0] = character->modelOffset[0];
            c->mesh.userOffset[1] = character->modelOffset[1];
            c->mesh.userOffset[2] = character->modelOffset[2];
            break;
        }
    }

    if (c->descriptor != NULL)
        return &c->descriptor->model;

    struct NativeObjDescriptor *d = calloc(1, sizeof(*d));
    if (!d) return NULL;

    snprintf(d->model.name, sizeof(d->model.name), "%s", assetName);
    d->model.id = -1;
    d->model.numHeaders = 1;
    d->model.headers = &d->header;
    snprintf(d->header.name, sizeof(d->header.name), "%s", assetName);
    d->header.unk1 = NATIVE_OBJ_MODEL_MAGIC;
    d->header.maxDistanceLOD = -1;

    /* Retail header scales use a different unit from instance translation. */
    d->header.scale.x = (s16)fmaxf(1, c->mesh.extent[0] * 1000);
    d->header.scale.y = (s16)fmaxf(1, c->mesh.extent[1] * 1000);
    d->header.scale.z = (s16)fmaxf(1, c->mesh.extent[2] * 1000);
    d->header.ptrFrameData = &d->frame;
    d->frame.pos.x = c->mesh.frameOrigin[0];
    d->frame.pos.y = c->mesh.frameOrigin[1];
    d->frame.pos.z = c->mesh.frameOrigin[2];
    d->frame.vertexOffset = sizeof(d->frame);

    c->descriptor = d;
    return &d->model;
}

const struct NativeObjMesh *NativeObj_GetMesh(const struct Model *model)
{
    if (!model || !model->headers || model->headers[0].unk1 != NATIVE_OBJ_MODEL_MAGIC) return NULL;

    /* Do not recover the native asset key from Model::name: that retail field
     * is only 16 bytes. Resolve by descriptor identity so long mod names remain
     * unique and never alias merely because their first 15 characters match. */
    for (struct NativeObjCache *c = s_objCache; c; c = c->next)
        if ((c->descriptor != NULL) && (&c->descriptor->model == model))
            return &c->mesh;

    return NULL;
}

const struct NativeObjMesh *NativeObj_GetRetailWheelOverrideMesh(void)
{
    if (!s_retailWheelOverrideTried)
    {
        s_retailWheelOverrideTried = 1;
        s_retailWheelOverrideModel = NativeObj_LoadRacer("_global_wheels");
        if (s_retailWheelOverrideModel != NULL)
        {
            const struct NativeObjMesh *mesh = NativeObj_GetMesh(s_retailWheelOverrideModel);
            Platform_Log("[CTR Native] 3D wheels: loaded _global_wheels (%d triangles)\n",
                         mesh ? mesh->triangleCount : 0);
        }
        else
        {
            Platform_Log("[CTR Native] 3D wheels: _global_wheels unavailable, using 2D fallback\n");
        }
    }

    return s_retailWheelOverrideModel ? NativeObj_GetMesh(s_retailWheelOverrideModel) : NULL;
}

int NativeObj_ShouldReplaceRetailWheels(const struct Driver *driver)
{
    if ((driver == NULL) || (driver->instSelf == NULL))
        return 0;

    /* Detect a retail kart by the model actually being rendered, not by
       characterIDs[]. Ghosts, AI and previews can temporarily use different
       roster slots, while an external OBJ is unambiguous here. */
    if (NativeObj_GetMesh(driver->instSelf->model) != NULL)
        return 0;

    return NativeObj_GetRetailWheelOverrideMesh() != NULL;
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
    const struct NativeObjMesh *retailOverride = NativeObj_GetRetailWheelOverrideMesh();

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

        if ((retailOverride != NULL) && (driverMesh == NULL) && (d->wheelSize > 0)) {
            mesh = retailOverride;
            wheelModel = s_retailWheelOverrideModel;
            retailOverrideActive = 1;
        } else if ((driverMesh != NULL) && !CharacterRegistry_HasWheels(characterID)) {
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

void NativeObj_GetWheelAngles(const struct Instance *inst, float angles[4])
{
    memset(angles, 0, 4 * sizeof(*angles));
    if (inst == NULL) return;

    for (int i = 0; i < 8; ++i)
        if (s_objWheels[i].instance == inst) {
            memcpy(angles, s_objWheels[i].angles, sizeof(s_objWheels[i].angles));
            return;
        }
}

float NativeObj_GetWheelSteerAngle(const struct Instance *inst)
{
    if (inst == NULL) return 0.0f;
    for (int i = 0; i < 8; ++i)
        if (s_objWheels[i].instance == inst)
            return s_objWheels[i].steerAngle;
    return 0.0f;
}
