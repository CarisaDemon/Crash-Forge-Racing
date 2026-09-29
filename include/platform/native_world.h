#ifndef CTR_NATIVE_WORLD_H
#define CTR_NATIVE_WORLD_H
struct GameTracker;
struct Instance;
struct Driver;
struct Particle;
int NativeWorld_Prepare(struct GameTracker *gt);
int NativeWorld_IsActive(void);
int NativeWorld_IsTick(void);
void NativeWorld_BeginTick(struct GameTracker *gt);
void NativeWorld_EndTick(struct GameTracker *gt);
void NativeWorld_HubDriverBeginTick(struct GameTracker *gt);
void NativeWorld_HubDriverEndTick(struct GameTracker *gt);
void NativeWorld_HubCameraBeginTick(struct GameTracker *gt);
void NativeWorld_HubCameraEndTick(struct GameTracker *gt);
void NativeWorld_BeginRender(struct GameTracker *gt);
void NativeWorld_EndRender(struct GameTracker *gt);
void NativeWorld_ForgetInstance(struct Instance *inst);
void NativeWorld_ForgetParticle(struct Particle *p);
int NativeWorld_AnimFrames(struct Instance *inst, int count, int *a, int *b, int *alpha);
int NativeWorld_AnimBlend(struct Instance *inst);
int NativeWorld_HudRoom(struct Driver *d);
int NativeWorld_HudTime(struct Driver *d);
int NativeWorld_HudSpeedometer(struct Driver *d);
int NativeWorld_DriverPosition(struct Driver *d, int out[3], int *ground);
int NativeWorld_HubDriverVisualCameraTarget(struct Driver *d, int out[3]);
int NativeWorld_HubDriverCameraState(struct Driver *d, int *rotW, int *angle, int *turnAngle, int *rotZ);
void NativeWorld_AdvanceAutoAnimations(struct GameTracker *gt);
void NativeWorld_DrawGhostWarning(void);
int Platform_SetLegacyTickOverride(int ticks);
#endif
