/* Native-only simulation snapshots. No changes to retail structure layouts. */
#include <common.h>
#include <platform/native_world_math.h>
#include <platform/native_world.h>
#include <stdlib.h>
#include <string.h>

void NativeGTE_RegisterPresentationMatrix(const MATRIX *matrix, const s64 *mQ16, const s64 *tQ16);
void NativeGTE_ClearPresentationMatrix(const MATRIX *matrix);
void NativeGTE_ClearAllPresentationMatrices(void);

typedef struct NW_Pose {
    MATRIX matrix;
    SVec3 scale;
    s16 alpha, animFrame;
    u8 animIndex;
    u32 flags;
} NW_Pose;
typedef struct NW_Instance {
    struct Instance *key;
    NW_Pose prev, curr, restore;
    u32 before, after, autoSerial;
    int valid, applied, animBlend;
} NW_Instance;
typedef struct NW_Particle {
    struct Particle *key;
    int prev[11], curr[11], restore[11];
    u32 before, after;
    int valid, applied;
} NW_Particle;
typedef struct NW_Camera {
    SVec3 prevPos, currPos, prevRot, currRot, savePos, saveRot;
    int prevZoom, currZoom, saveZoom, mode;
    int valid, applied;
} NW_Camera;
typedef struct NW_Driver {
    struct Driver *key;
    Vec3 prevPos, currPos;
    MATRIX prevMatrix, currMatrix;
    int prevGround, currGround;
    int prevRoom, currRoom;
    int prevTime, currTime;
    int prevNeedle, currNeedle;
    int prevAngle, currAngle;
    int prevRotW, currRotW;
    int prevTurnAngle, currTurnAngle;
    int prevRotZ, currRotZ;
    int valid;
} NW_Driver;
static NW_Instance *s_nwInst;
static NW_Particle *s_nwParticle;
static size_t s_nwInstCap, s_nwInstUsed, s_nwParticleCap, s_nwParticleUsed;
static NW_Camera s_nwCamera[4];
static NW_Driver s_nwDriver[8];
static MATRIX s_nwHubRestoreMatrix[8];
static u8 s_nwHubApplied[8];
static struct Level *s_nwLevel;
static int s_nwLevelId, s_nwActive, s_nwTick, s_nwRender, s_nwAlpha;
static int s_nwHubCameraLogicTick;
static u32 s_nwSerial, s_nwLastTimer;
static u64 s_nwStatCounter;
static u32 s_nwStatTicks, s_nwStatFrames;
static size_t NW_Hash(const void *p, size_t cap)
{
    uintptr_t k = (uintptr_t)p;
    return ((k >> 4) * (uintptr_t)2654435761u) & (cap - 1);
}
static NW_Instance *NW_FindInst(struct Instance *inst, int create)
{
    size_t i;
    if (!inst) return NULL;
    if (create && (s_nwInstCap == 0 || (s_nwInstUsed + 1) * 2 >= s_nwInstCap)) {
        size_t cap = s_nwInstCap ? s_nwInstCap * 2 : 1024;
        NW_Instance *slots = calloc(cap, sizeof(*slots));
        if (!slots) return NULL;
        for (i = 0; i < s_nwInstCap; ++i) if (s_nwInst[i].key) {
            size_t j = NW_Hash(s_nwInst[i].key, cap);
            while (slots[j].key) j = (j + 1) & (cap - 1);
            slots[j] = s_nwInst[i];
        }
        free(s_nwInst); s_nwInst = slots; s_nwInstCap = cap;
    }
    if (!s_nwInstCap) return NULL;
    i = NW_Hash(inst, s_nwInstCap);
    while (s_nwInst[i].key && s_nwInst[i].key != inst) i = (i + 1) & (s_nwInstCap - 1);
    if (!s_nwInst[i].key) {
        if (!create) return NULL;
        s_nwInst[i].key = inst; ++s_nwInstUsed;
    }
    return &s_nwInst[i];
}
static NW_Particle *NW_FindParticle(struct Particle *p, int create)
{
    size_t i;
    if (!p) return NULL;
    if (create && (s_nwParticleCap == 0 || (s_nwParticleUsed + 1) * 2 >= s_nwParticleCap)) {
        size_t cap = s_nwParticleCap ? s_nwParticleCap * 2 : 1024;
        NW_Particle *slots = calloc(cap, sizeof(*slots));
        if (!slots) return NULL;
        for (i = 0; i < s_nwParticleCap; ++i) if (s_nwParticle[i].key) {
            size_t j = NW_Hash(s_nwParticle[i].key, cap);
            while (slots[j].key) j = (j + 1) & (cap - 1);
            slots[j] = s_nwParticle[i];
        }
        free(s_nwParticle); s_nwParticle = slots; s_nwParticleCap = cap;
    }
    if (!s_nwParticleCap) return NULL;
    i = NW_Hash(p, s_nwParticleCap);
    while (s_nwParticle[i].key && s_nwParticle[i].key != p) i = (i + 1) & (s_nwParticleCap - 1);
    if (!s_nwParticle[i].key) {
        if (!create) return NULL;
        s_nwParticle[i].key = p; ++s_nwParticleUsed;
    }
    return &s_nwParticle[i];
}
static void NW_Reset(void)
{
    if (s_nwInst) memset(s_nwInst, 0, s_nwInstCap * sizeof(*s_nwInst));
    if (s_nwParticle) memset(s_nwParticle, 0, s_nwParticleCap * sizeof(*s_nwParticle));
    memset(s_nwCamera, 0, sizeof(s_nwCamera));
    memset(s_nwDriver, 0, sizeof(s_nwDriver));
    memset(s_nwHubApplied, 0, sizeof(s_nwHubApplied));
    NativeGTE_ClearAllPresentationMatrices();
    s_nwInstUsed = s_nwParticleUsed = 0;
    s_nwHubCameraLogicTick = 0;
    s_nwSerial = 0; s_nwRender = 0;
}
int NativeWorld_IsActive(void) { return s_nwActive; }
int NativeWorld_IsTick(void) { return s_nwTick; }
int NativeWorld_Prepare(struct GameTracker *gt)
{
    int active = Platform_GetHighRefreshMode() && gt->level1 &&
        LOAD_IsOpen_RacingOrBattle() &&
        !(gt->gameMode1 & (MAIN_MENU | ADVENTURE_ARENA | GAME_CUTSCENE | LOADING));
    if (active != s_nwActive || gt->level1 != s_nwLevel || gt->levelID != s_nwLevelId ||
        gt->timer < s_nwLastTimer) {
        NW_Reset();
        s_nwLevel = gt->level1; s_nwLevelId = gt->levelID;
        s_nwStatCounter = 0; s_nwStatTicks = s_nwStatFrames = 0;
        if (active) Platform_Log("[WORLD111] coherent world clock ON, level=%d; render snapshots enabled\n", gt->levelID);
    }
    s_nwLastTimer = gt->timer; s_nwActive = active;
    return active;
}
void NativeWorld_ForgetInstance(struct Instance *inst)
{
    NW_Instance *s = NW_FindInst(inst, 0);
    if (s) { s->valid = 0; s->applied = 0; s->before = s->after = 0; }
}
void NativeWorld_ForgetParticle(struct Particle *p)
{
    NW_Particle *s = NW_FindParticle(p, 0);
    if (s) { s->valid = 0; s->applied = 0; s->before = s->after = 0; }
}
static NW_Pose NW_ReadPose(struct Instance *inst)
{
    NW_Pose p;
    p.matrix = inst->matrix; p.scale = inst->scale; p.alpha = inst->alphaScale;
    p.animFrame = inst->animFrame; p.animIndex = inst->animIndex; p.flags = inst->flags;
    return p;
}
static void NW_CaptureInst(struct Instance *inst, int after)
{
    NW_Instance *s;
    if (!inst || !inst->model) return;
    s = NW_FindInst(inst, 1);
    if (!s) return;
    if (!after) {
        if (s->before == s_nwSerial) return;
        s->prev = NW_ReadPose(inst); s->before = s_nwSerial;
    } else {
        if (s->after == s_nwSerial) return;
        s->curr = NW_ReadPose(inst);
        if (!s->valid || s->before != s_nwSerial) s->prev = s->curr;
        /* Teleports/reset must never interpolate across the entire circuit. */
        for (int k=0;k<3;k++) if (llabs((long long)s->curr.matrix.t[k] - s->prev.matrix.t[k]) > 4096)
            s->prev = s->curr;
        s->after = s_nwSerial; s->valid = 1;
    }
}
static void NW_CaptureAll(struct GameTracker *gt, int after)
{
    struct Instance *inst;
    for (inst = (struct Instance *)gt->JitPools.instance.taken.first; inst; inst=inst->next)
        NW_CaptureInst(inst, after);
    if (gt->level1 && gt->level1->ptrInstDefs)
        for (int i=0;i<gt->level1->numInstances;i++) NW_CaptureInst(gt->level1->ptrInstDefs[i].ptrInstance, after);
    for (int list=0;list<2;list++) {
        struct Particle *p = list ? gt->particleList_heatWarp : gt->particleList_ordinary;
        for (;p;p=p->next) {
            NW_Particle *s = NW_FindParticle(p, 1);
            if (!s) continue;
            for (int k=0;k<11;k++) {
                if (!after) s->prev[k] = p->axis[k].startVal;
                else { s->curr[k] = p->axis[k].startVal;
                    if (!s->valid || s->before != s_nwSerial) s->prev[k] = s->curr[k]; }
            }
            if (after) { s->after=s_nwSerial; s->valid=1; } else s->before=s_nwSerial;
        }
    }
}

void NativeWorld_BeginTick(struct GameTracker *gt)
{
    s_nwTick=1; ++s_nwSerial;
    NW_CaptureAll(gt, 0);
    for (int i=0;i<4;i++) {
        NW_Camera *s=&s_nwCamera[i]; struct PushBuffer *pb=&gt->pushBuffer[i];
        s->prevPos=pb->pos; s->prevRot=pb->rot; s->prevZoom=pb->distanceToScreen_PREV;
    }
    for (int i=0;i<8;i++) {
        struct Driver *d=gt->drivers[i]; NW_Driver *s=&s_nwDriver[i];
        if (!d) { s->valid=0; s->key=NULL; continue; }
        if (s->key != d) s->valid=0;
        s->key=d; s->prevPos=d->posCurr; s->prevGround=d->quadBlockHeight;
        s->prevRoom=d->turbo_MeterRoomLeft;
        s->prevTime=d->timeElapsedInRace;
        s->prevNeedle=d->speedometerNeedleValue;
    }
}
void NativeWorld_EndTick(struct GameTracker *gt)
{
    NW_CaptureAll(gt, 1);
    for (int i=0;i<4;i++) {
        NW_Camera *s=&s_nwCamera[i]; struct PushBuffer *pb=&gt->pushBuffer[i];
        int snap=!s->valid || s->mode != gt->cameraDC[i].cameraMode;
        s->currPos=pb->pos; s->currRot=pb->rot; s->currZoom=pb->distanceToScreen_PREV;
        for (int k=0;k<3;k++) if (abs((int)s->currPos.v[k]-s->prevPos.v[k])>4096) snap=1;
        if (snap) { s->prevPos=s->currPos; s->prevRot=s->currRot; s->prevZoom=s->currZoom; }
        s->mode=gt->cameraDC[i].cameraMode; s->valid=1;
    }
    for (int i=0;i<8;i++) {
        struct Driver *d=gt->drivers[i]; NW_Driver *s=&s_nwDriver[i];
        if (!d) continue;
        s->currPos=d->posCurr; s->currGround=d->quadBlockHeight; s->currRoom=d->turbo_MeterRoomLeft;
        s->currTime=d->timeElapsedInRace;
        s->currNeedle=d->speedometerNeedleValue;
        if (!s->valid || s->key != d) {
            s->prevPos=s->currPos; s->prevGround=s->currGround; s->prevRoom=s->currRoom;
            s->prevTime=s->currTime; s->prevNeedle=s->currNeedle;
        }
        for (int k=0;k<3;k++) if (llabs((long long)s->currPos.v[k]-s->prevPos.v[k])>(4096LL*256))
            s->prevPos=s->currPos;
        s->key=d; s->valid=1;
    }
    s_nwLastTimer=gt->timer; ++s_nwStatTicks; s_nwTick=0;
}

void NativeWorld_HubDriverBeginTick(struct GameTracker *gt)
{
    if (!gt) return;
    for (int i=0;i<8;i++) {
        struct Driver *d=gt->drivers[i];
        NW_Driver *s=&s_nwDriver[i];
        if (!d) { s->valid=0; s->key=NULL; continue; }
        if (s->key != d) s->valid=0;
        s->key=d;
        s->prevPos=d->posCurr;
        s->prevGround=d->quadBlockHeight;
        s->prevRoom=d->turbo_MeterRoomLeft;
        s->prevTime=d->timeElapsedInRace;
        s->prevNeedle=d->speedometerNeedleValue;
        s->prevAngle=d->angle;
        s->prevRotW=d->rotCurr.w;
        s->prevTurnAngle=d->turnAngleCurr;
        s->prevRotZ=d->rotCurr.z;
        if (d->instSelf) s->prevMatrix=d->instSelf->matrix;
    }
}

void NativeWorld_HubDriverEndTick(struct GameTracker *gt)
{
    if (!gt) return;
    for (int i=0;i<8;i++) {
        struct Driver *d=gt->drivers[i];
        NW_Driver *s=&s_nwDriver[i];
        if (!d) continue;
        s->currPos=d->posCurr;
        s->currGround=d->quadBlockHeight;
        s->currRoom=d->turbo_MeterRoomLeft;
        s->currTime=d->timeElapsedInRace;
        s->currNeedle=d->speedometerNeedleValue;
        s->currAngle=d->angle;
        s->currRotW=d->rotCurr.w;
        s->currTurnAngle=d->turnAngleCurr;
        s->currRotZ=d->rotCurr.z;
        if (d->instSelf) s->currMatrix=d->instSelf->matrix;
        if (!s->valid || s->key != d) {
            s->prevPos=s->currPos;
            s->prevGround=s->currGround;
            s->prevRoom=s->currRoom;
            s->prevTime=s->currTime;
            s->prevNeedle=s->currNeedle;
            s->prevAngle=s->currAngle;
            s->prevRotW=s->currRotW;
            s->prevTurnAngle=s->currTurnAngle;
            s->prevRotZ=s->currRotZ;
            s->prevMatrix=s->currMatrix;
        }
        for (int k=0;k<3;k++) {
            if (llabs((long long)s->currPos.v[k]-s->prevPos.v[k])>(4096LL*256)) {
                s->prevPos=s->currPos;
                s->prevGround=s->currGround;
                s->prevRoom=s->currRoom;
                s->prevTime=s->currTime;
                s->prevNeedle=s->currNeedle;
                s->prevAngle=s->currAngle;
                s->prevRotW=s->currRotW;
                s->prevTurnAngle=s->currTurnAngle;
                s->prevRotZ=s->currRotZ;
                s->prevMatrix=s->currMatrix;
                break;
            }
        }
        s->key=d;
        s->valid=1;
    }
}

void NativeWorld_HubCameraBeginTick(struct GameTracker *gt)
{
    if (!gt) return;
    s_nwHubCameraLogicTick = 1;
    for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
        NW_Camera *s=&s_nwCamera[i];
        struct PushBuffer *pb=&gt->pushBuffer[i];
        s->prevPos=pb->pos;
        s->prevRot=pb->rot;
        s->prevZoom=pb->distanceToScreen_PREV;
    }
}

void NativeWorld_HubCameraEndTick(struct GameTracker *gt)
{
    if (!gt) { s_nwHubCameraLogicTick = 0; return; }
    for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
        NW_Camera *s=&s_nwCamera[i];
        struct PushBuffer *pb=&gt->pushBuffer[i];
        int snap=!s->valid || s->mode != gt->cameraDC[i].cameraMode;
        s->currPos=pb->pos;
        s->currRot=pb->rot;
        s->currZoom=pb->distanceToScreen_PREV;
        for (int k=0;k<3;k++) if (abs((int)s->currPos.v[k]-s->prevPos.v[k])>4096) snap=1;
        if (snap) {
            s->prevPos=s->currPos;
            s->prevRot=s->currRot;
            s->prevZoom=s->currZoom;
        }
        s->mode=gt->cameraDC[i].cameraMode;
        s->valid=1;
    }
    s_nwHubCameraLogicTick = 0;
}

static int NW_HubDriverVisualPosition(struct GameTracker *gt, struct Driver *d, int out[3])
{
    int alpha, id;
    NW_Driver *s;
    if (!gt || !d || !Platform_GetHighRefreshMode()) return 0;
    if ((gt->gameMode1 & ADVENTURE_ARENA) == 0) return 0;
    if ((gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE | LOADING | PAUSE_ALL)) != 0) return 0;
    alpha = Platform_GetLegacy30HzAlpha256() * 256;
    id=(int)d->driverID;
    if (id>=0 && id<8) {
        s=&s_nwDriver[id];
        if (s->valid && s->key==d) {
            for (int k=0;k<3;k++) out[k]=NW_LerpRound(s->prevPos.v[k],s->currPos.v[k],alpha);
            return 1;
        }
    }
    for (int k=0;k<3;k++) out[k]=NW_LerpRound(d->posPrev.v[k],d->posCurr.v[k],alpha);
    return 1;
}

static int NW_HubDriverVisualMatrix(struct GameTracker *gt, struct Driver *d, MATRIX *out)
{
    int id, alpha;
    NW_Driver *s;
    if (!out || !NW_HubDriverVisualPosition(gt,d,(int[3]){0,0,0})) return 0;
    id=(int)d->driverID;
    if (id<0 || id>=8) return 0;
    s=&s_nwDriver[id];
    if (!s->valid || s->key!=d || !d->instSelf) return 0;
    alpha=Platform_GetLegacy30HzAlpha256()*256;
    *out=d->instSelf->matrix;
    for (int r=0;r<3;r++) {
        out->t[r]=NW_LerpRound(s->prevMatrix.t[r],s->currMatrix.t[r],alpha);
        for (int c=0;c<3;c++)
            out->m[r][c]=(s16)NW_LerpRound(s->prevMatrix.m[r][c],s->currMatrix.m[r][c],alpha);
    }
    return 1;
}

void NativeWorld_BeginRender(struct GameTracker *gt)
{
    if ((!s_nwActive || s_nwLevel != gt->level1) && gt) {
        if ((gt->gameMode1 & (PAUSE_ALL|LOADING)) == 0 && (gt->gameMode1 & ADVENTURE_ARENA) != 0 && Platform_GetHighRefreshMode()) {
            s_nwAlpha=Platform_GetLegacy30HzAlpha256()*256;
            memset(s_nwHubApplied, 0, sizeof(s_nwHubApplied));
            for (int i=0;i<8;i++) {
                struct Driver *d=gt->drivers[i];
                MATRIX visualMatrix;
                if (!d || !d->instSelf || !NW_HubDriverVisualMatrix(gt,d,&visualMatrix)) continue;
                s_nwHubRestoreMatrix[i]=d->instSelf->matrix;
                d->instSelf->matrix=visualMatrix;
                s_nwHubApplied[i]=1;
            }
            /* Hub camera logic now runs on the same 30 Hz logical clock as
               the Driver. Interpolate its PushBuffer only for presentation,
               exactly like the race path, so kart and world share one alpha. */
            for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
                NW_Camera *s=&s_nwCamera[i];
                struct PushBuffer *pb=&gt->pushBuffer[i];
                struct PushBuffer prevPB, currPB;
                s64 mQ16[9], tQ16[3];
                NativeGTE_ClearPresentationMatrix(&pb->matrix_ViewProj);
                if (!s->valid) continue;
                s->savePos=pb->pos;
                s->saveRot=pb->rot;
                s->saveZoom=pb->distanceToScreen_PREV;

                prevPB=*pb;
                prevPB.pos=s->prevPos;
                prevPB.rot=s->prevRot;
                prevPB.distanceToScreen_PREV=s->prevZoom;
                PushBuffer_SetMatrixVP(&prevPB);

                currPB=*pb;
                currPB.pos=s->currPos;
                currPB.rot=s->currRot;
                currPB.distanceToScreen_PREV=s->currZoom;
                PushBuffer_SetMatrixVP(&currPB);

                for (int k=0;k<3;k++)
                    pb->pos.v[k]=(s16)NW_LerpRound(s->prevPos.v[k],s->currPos.v[k],s_nwAlpha);
                pb->rot.x=(s16)NW_AngleRound(s->prevRot.x,s->currRot.x,s_nwAlpha);
                pb->rot.y=(s16)NW_AngleRound(s->prevRot.y,s->currRot.y,s_nwAlpha);
                pb->rot.z=(s16)NW_LerpRound((s16)s->prevRot.z,(s16)s->currRot.z,s_nwAlpha);
                pb->distanceToScreen_PREV=NW_LerpRound(s->prevZoom,s->currZoom,s_nwAlpha);
                PushBuffer_SetMatrixVP(pb);

                for (int r=0;r<3;r++) {
                    for (int c=0;c<3;c++) {
                        s32 a=prevPB.matrix_ViewProj.m[r][c];
                        s32 b=currPB.matrix_ViewProj.m[r][c];
                        mQ16[r*3+c]=((s64)a<<16)+((s64)b-a)*s_nwAlpha;
                    }
                    {
                        s32 a=prevPB.matrix_ViewProj.t[r];
                        s32 b=currPB.matrix_ViewProj.t[r];
                        tQ16[r]=((s64)a<<16)+((s64)b-a)*s_nwAlpha;
                    }
                }
                NativeGTE_RegisterPresentationMatrix(&pb->matrix_ViewProj,mQ16,tQ16);
                s->applied=1;
            }
            s_nwRender=2;
        }
        return;
    }
    if (gt->gameMode1 & (PAUSE_ALL|LOADING)) return;
    s_nwAlpha=Platform_GetLegacy30HzAlpha256()*256;
    s_nwRender=1;
    for (size_t i=0;i<s_nwInstCap;i++) {
        NW_Instance *s=&s_nwInst[i]; struct Instance *inst=s->key;
        if (!s->valid || s->after != s_nwSerial) continue;
        s->restore=NW_ReadPose(inst); s->applied=1; s->animBlend=-1;
        for (int r=0;r<3;r++) {
            inst->matrix.t[r]=NW_Lerp(s->prev.matrix.t[r],s->curr.matrix.t[r],s_nwAlpha);
            inst->scale.v[r]=(s16)NW_Lerp(s->prev.scale.v[r],s->curr.scale.v[r],s_nwAlpha);
            for (int c=0;c<3;c++) inst->matrix.m[r][c]=(s16)NW_Lerp(s->prev.matrix.m[r][c],s->curr.matrix.m[r][c],s_nwAlpha);
        }
        inst->alphaScale=(s16)NW_Lerp(s->prev.alpha,s->curr.alpha,s_nwAlpha);
    }
    for (size_t i=0;i<s_nwParticleCap;i++) {
        NW_Particle *s=&s_nwParticle[i]; struct Particle *p=s->key;
        if (!s->valid || s->after != s_nwSerial) continue;
        for (int k=0;k<11;k++) {
            s->restore[k]=p->axis[k].startVal;
            /* Icon indices are discrete; do not create invalid sprite indices. */
            if (k != PARTICLE_AXIS_ICON_FRAME_OR_LINE_COLOR)
                p->axis[k].startVal=NW_Lerp(s->prev[k],s->curr[k],s_nwAlpha);
        }
        s->applied=1;
    }
    for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
        NW_Camera *s=&s_nwCamera[i]; struct PushBuffer *pb=&gt->pushBuffer[i];
        if (!s->valid) continue;
        s->savePos=pb->pos; s->saveRot=pb->rot; s->saveZoom=pb->distanceToScreen_PREV;
        for (int k=0;k<3;k++) {
            pb->pos.v[k]=(s16)NW_Lerp(s->prevPos.v[k],s->currPos.v[k],s_nwAlpha);
            pb->rot.v[k]=NW_Angle(s->prevRot.v[k],s->currRot.v[k],s_nwAlpha);
        }
        pb->distanceToScreen_PREV=NW_Lerp(s->prevZoom,s->currZoom,s_nwAlpha);
        PushBuffer_SetMatrixVP(pb);
        s->applied=1;
    }
    for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
        struct Driver *d=gt->drivers[i];
        if (!d) continue;
        int strength=d->clockFlash ? -(int)d->clockFlash :
            (d->clockReceive ? d->clockReceive : (d->clockSend ? d->clockSend : ((gt->clockEffectEnabled & 1) ? 10000 : 0)));
        if (strength) DISPLAY_Blur_Main(&gt->pushBuffer[i],strength);
    }
    ++s_nwStatFrames;
    {
        u64 now=SDL_GetPerformanceCounter(), freq=SDL_GetPerformanceFrequency();
        if (!s_nwStatCounter) { s_nwStatCounter=now; s_nwStatTicks=s_nwStatFrames=0; }
        if (now-s_nwStatCounter >= freq*2) {
            double seconds=(double)(now-s_nwStatCounter)/(double)freq;
            Platform_Log("[WORLD111] level=%d demo=%d sim=%.2fHz render=%.2fHz alpha=%d instances=%u particles=%u ghostTicks=%d ghostTime=%d\n",
                gt->levelID,gt->boolDemoMode,s_nwStatTicks/seconds,s_nwStatFrames/seconds,s_nwAlpha,
                (unsigned)s_nwInstUsed,(unsigned)s_nwParticleUsed,
                sdata->GhostRecording.countEightFrames,sdata->GhostRecording.timeElapsedInRace);
            s_nwStatCounter=now; s_nwStatTicks=s_nwStatFrames=0;
        }
    }
}
void NativeWorld_EndRender(struct GameTracker *gt)
{
    if (!s_nwRender) return;
    if (s_nwRender == 2) {
        for (int i=0;i<8;i++) {
            struct Driver *d=gt ? gt->drivers[i] : NULL;
            if (!s_nwHubApplied[i] || !d || !d->instSelf) continue;
            d->instSelf->matrix=s_nwHubRestoreMatrix[i];
            s_nwHubApplied[i]=0;
        }
        if (gt) {
            for (int i=0;i<gt->numPlyrCurrGame && i<4;i++) {
                NW_Camera *s=&s_nwCamera[i];
                struct PushBuffer *pb=&gt->pushBuffer[i];
                if (!s->applied) continue;
                pb->pos=s->savePos;
                pb->rot=s->saveRot;
                pb->distanceToScreen_PREV=s->saveZoom;
                NativeGTE_ClearPresentationMatrix(&pb->matrix_ViewProj);
                PushBuffer_SetMatrixVP(pb);
                s->applied=0;
            }
        }
        s_nwRender=0;
        return;
    }
    for (size_t i=0;i<s_nwInstCap;i++) {
        NW_Instance *s=&s_nwInst[i];
        if (!s->applied) continue;
        s->key->matrix=s->restore.matrix; s->key->scale=s->restore.scale;
        s->key->alphaScale=s->restore.alpha; s->applied=0;
    }
    for (size_t i=0;i<s_nwParticleCap;i++) {
        NW_Particle *s=&s_nwParticle[i];
        if (!s->applied) continue;
        for (int k=0;k<11;k++) s->key->axis[k].startVal=s->restore[k];
        s->applied=0;
    }
    for (int i=0;i<4;i++) {
        NW_Camera *s=&s_nwCamera[i]; struct PushBuffer *pb=&gt->pushBuffer[i];
        if (!s->applied) continue;
        pb->pos=s->savePos; pb->rot=s->saveRot; pb->distanceToScreen_PREV=s->saveZoom;
        PushBuffer_SetMatrixVP(pb); s->applied=0;
    }
    s_nwRender=0;
}
int NativeWorld_AnimFrames(struct Instance *inst, int count, int *a, int *b, int *alpha)
{
    NW_Instance *s;
    int countLogical=count & 0x7fff, doubleFrames=(count & 0x8000)!=0;
    int countReal=doubleFrames ? (countLogical+1)/2 : countLogical;
    int prev, curr, delta, base, next;
    int64_t logical;
    if (!s_nwRender || countReal<1) return 0;
    s=NW_FindInst(inst,0);
    if (!s || !s->valid || s->after != s_nwSerial) return 0;
    prev=s->prev.animFrame; curr=s->curr.animFrame;
    if (prev<0) prev=0; if (curr<0) curr=0;
    if (prev>=countLogical) prev=countLogical-1;
    if (curr>=countLogical) curr=countLogical-1;
    if (s->prev.animIndex != s->curr.animIndex) prev=curr;
    delta=curr-prev;
    if ((s->curr.flags & (ANIM_LOOP|ANIM_STOP_AT_END)) == ANIM_LOOP) {
        if (delta < -(countLogical/2)) delta+=countLogical;
        else if (delta > countLogical/2) delta-=countLogical;
    }
    logical=(int64_t)prev*65536 + (int64_t)delta*s_nwAlpha;
    if (logical<0) logical+=countLogical*65536;
    if (logical>=countLogical*65536) logical-=countLogical*65536;
    if (doubleFrames) logical/=2;
    base=logical/65536; next=base+1;
    if (next>=countReal) next=((s->curr.flags & (ANIM_LOOP|ANIM_STOP_AT_END)) == ANIM_LOOP) ? 0 : base;
    *a=base; *b=next; *alpha=logical & 65535;
    s->animBlend=*alpha;
    return 1;
}
int NativeWorld_AnimBlend(struct Instance *inst)
{
    NW_Instance *s;
    if (!s_nwRender) return -1;
    s=NW_FindInst(inst,0);
    return s && s->valid ? s->animBlend : -1;
}
int NativeWorld_HudRoom(struct Driver *d)
{
    int id=(int)d->driverID; NW_Driver *s;
    if (!s_nwRender || id<0 || id>=8) return d->turbo_MeterRoomLeft;
    s=&s_nwDriver[id];
    if (!s->valid || s->key != d || s->prevRoom<=0 || s->currRoom<=0 || s->currRoom>s->prevRoom)
        return d->turbo_MeterRoomLeft;
    return NW_Lerp(s->prevRoom,s->currRoom,s_nwAlpha);
}
int NativeWorld_HudTime(struct Driver *d)
{
    int id=(int)d->driverID; NW_Driver *s;
    if (!s_nwRender || id<0 || id>=8) return d->timeElapsedInRace;
    s=&s_nwDriver[id];
    if (!s->valid || s->key != d || s->currTime < s->prevTime)
        return d->timeElapsedInRace;
    return NW_Lerp(s->prevTime,s->currTime,s_nwAlpha);
}
int NativeWorld_HudSpeedometer(struct Driver *d)
{
    int id=(int)d->driverID; NW_Driver *s;
    if (!s_nwRender || id<0 || id>=8) return d->speedometerNeedleValue;
    s=&s_nwDriver[id];
    if (!s->valid || s->key != d) return d->speedometerNeedleValue;
    return NW_Lerp(s->prevNeedle,s->currNeedle,s_nwAlpha);
}
int NativeWorld_DriverPosition(struct Driver *d,int out[3],int *ground)
{
    int id=(int)d->driverID; NW_Driver *s;
    /* A fixed-step Hub camera must consume the logical driver state, exactly
       like the race camera. Interpolation is presentation-only. */
    if (s_nwHubCameraLogicTick) return 0;
    if (id<0 || id>=8) return 0;
    if (s_nwRender == 1) {
        s=&s_nwDriver[id];
        if (!s->valid || s->key != d) return 0;
        for (int k=0;k<3;k++) out[k]=NW_Lerp(s->prevPos.v[k],s->currPos.v[k],s_nwAlpha);
        if (ground) *ground=NW_Lerp(s->prevGround,s->currGround,s_nwAlpha);
        return 1;
    }
    if (NW_HubDriverVisualPosition(sdata->gGT,d,out)) {
        if (ground) *ground=d->quadBlockHeight;
        return 1;
    }
    return 0;
}

int NativeWorld_HubDriverVisualCameraTarget(struct Driver *d, int out[3])
{
    struct GameTracker *gt=sdata->gGT;
    if (s_nwHubCameraLogicTick) return 0;
    NW_Driver *s;
    int id, alpha;
    if (!gt || !d || !out || !Platform_GetHighRefreshMode()) return 0;
    if ((gt->gameMode1 & ADVENTURE_ARENA) == 0) return 0;
    if ((gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE | LOADING | PAUSE_ALL)) != 0) return 0;
    id=(int)d->driverID;
    if (id<0 || id>=8) return 0;
    s=&s_nwDriver[id];
    if (!s->valid || s->key!=d || !d->instSelf) return 0;

    alpha=Platform_GetLegacy30HzAlpha256()*256;
    for (int k=0;k<3;k++)
        out[k]=NW_LerpRound(s->prevMatrix.t[k],s->currMatrix.t[k],alpha) << 8;
    return 1;
}

int NativeWorld_HubDriverCameraState(struct Driver *d, int *rotW, int *angle, int *turnAngle, int *rotZ)
{
    int id, alpha;
    NW_Driver *s;
    struct GameTracker *gt=sdata->gGT;
    if (!gt || !d || !Platform_GetHighRefreshMode()) return 0;
    if (s_nwHubCameraLogicTick) return 0;
    if ((gt->gameMode1 & ADVENTURE_ARENA) == 0) return 0;
    if ((gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE | LOADING | PAUSE_ALL)) != 0) return 0;
    id=(int)d->driverID;
    if (id<0 || id>=8) return 0;
    s=&s_nwDriver[id];
    if (!s->valid || s->key!=d) return 0;
    alpha=Platform_GetLegacy30HzAlpha256()*256;
    /* angle/turnAngle are circular heading values. rotW and especially rotZ
       are signed camera/terrain offsets; wrapping them to 0..4095 turns a
       small negative bank into an almost-full revolution. */
    if (rotW) *rotW=NW_LerpRound((s16)s->prevRotW,(s16)s->currRotW,alpha);
    if (angle) *angle=NW_AngleRound((s16)s->prevAngle,(s16)s->currAngle,alpha);
    if (turnAngle) *turnAngle=NW_AngleRound((s16)s->prevTurnAngle,(s16)s->currTurnAngle,alpha);
    if (rotZ) *rotZ=NW_LerpRound((s16)s->prevRotZ,(s16)s->currRotZ,alpha);
    return 1;
}

void NativeWorld_DrawGhostWarning(void)
{
    if (!s_nwActive || !sdata->ghostOverflowTextTimer) return;
    {
        int color=(sdata->ghostOverflowTextTimer & 1) ? 0xFFFF8003 : 0xFFFF8004;
        DecalFont_DrawLine(sdata->lngStrings[LNG_GHOST_DATA_OVERFLOW],0x100,0x28,2,color);
        DecalFont_DrawLine(sdata->lngStrings[LNG_CAN_NOT_SAVE_GHOST_DATA],0x100,0x32,2,color);
    }
}

static void NW_AdvanceAutoInst(struct Instance *inst)
{
    NW_Instance *s;
    struct ModelHeader *mh;
    struct ModelAnim *anim;
    u32 flags;
    if (!inst || !inst->model || !inst->model->headers || !(inst->flags & 0x30)) return;
    s=NW_FindInst(inst,1);
    if (!s || s->autoSerial==s_nwSerial) return;
    s->autoSerial=s_nwSerial;
    mh=&inst->model->headers[0];
    if (!mh->ptrAnimations || inst->animIndex>=mh->numAnimations) return;
    anim=mh->ptrAnimations[inst->animIndex];
    if (!anim || !(anim->numFrames & 0x7fff)) return;
    flags=inst->flags;
    RenderBucket_AdvanceInstanceAnimWord(inst,0,0,(anim->numFrames & 0x7fff)-1,&flags);
    inst->flags=(inst->flags & ~0x30u) | (flags & 0x30u);
}
void NativeWorld_AdvanceAutoAnimations(struct GameTracker *gt)
{
    struct Instance *inst;
    for (inst=(struct Instance *)gt->JitPools.instance.taken.first;inst;inst=inst->next) NW_AdvanceAutoInst(inst);
    if (gt->level1 && gt->level1->ptrInstDefs)
        for (int i=0;i<gt->level1->numInstances;i++) NW_AdvanceAutoInst(gt->level1->ptrInstDefs[i].ptrInstance);
}
