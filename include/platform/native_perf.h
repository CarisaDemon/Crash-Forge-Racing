#ifndef NATIVE_PERF_H
#define NATIVE_PERF_H

#include <macros.h>

enum NativePerfBucket
{
	NATIVE_PERF_BUCKET_GAME_LOGIC,
	NATIVE_PERF_BUCKET_RENDER_FRAME,
	NATIVE_PERF_BUCKET_MAINFRAME_SETUP,
	NATIVE_PERF_BUCKET_MAINFRAME_EFFECTS,
	NATIVE_PERF_BUCKET_MAINFRAME_HUD,
	NATIVE_PERF_BUCKET_MAINFRAME_QUEUE_INSTANCES,
	NATIVE_PERF_BUCKET_MAINFRAME_EXECUTE_INSTANCES,
	NATIVE_PERF_BUCKET_MAINFRAME_LEVEL_GEOMETRY,
	NATIVE_PERF_BUCKET_LEVEL_RENDERLISTS,
	NATIVE_PERF_BUCKET_LEVEL_DRAW_OVR,
	NATIVE_PERF_BUCKET_LEVEL_SKY,
	NATIVE_PERF_BUCKET_WATER_RENDER,
	NATIVE_PERF_BUCKET_PARTICLE_RENDER,
	NATIVE_PERF_BUCKET_MAINFRAME_POST_LEVEL,
	NATIVE_PERF_BUCKET_MAINFRAME_REFRESHCARD,
	NATIVE_PERF_BUCKET_MAINFRAME_CLEAR_SCREEN,
	NATIVE_PERF_BUCKET_MAINFRAME_UI,
	NATIVE_PERF_BUCKET_MAINFRAME_RENDER_VSYNC,
	NATIVE_PERF_BUCKET_PLATFORM_END_FRAME,
	NATIVE_PERF_BUCKET_PLATFORM_END_SCENE,
	NATIVE_PERF_BUCKET_RENDER_SUBMIT,
	NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE,
	NATIVE_PERF_BUCKET_DRAW_OTAG,
	NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE,
	NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS,
	NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE,
	NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM,
	NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD,
	NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_RESIZE,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_BLIT,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_FLUSH,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_ALLOC,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_PACK,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_VRAM_UPLOAD,
	NATIVE_PERF_BUCKET_SWAP_WINDOW,
	NATIVE_PERF_BUCKET_VSYNC_WAIT,
	NATIVE_PERF_BUCKET_HIGH_REFRESH_WAIT,
	NATIVE_PERF_BUCKET_AUDIO_VBLANK,
	NATIVE_PERF_BUCKET_COUNT
};

/* Per-bot CPU scopes. GPU time stays a shared asynchronous frame metric;
 * it CANNOT accurately be split across batched material draws by bot. */
enum NativePerfBotPhase
{
    NATIVE_PERF_BOT_AI = 0,
    NATIVE_PERF_BOT_RENDER,
    NATIVE_PERF_BOT_PHASE_COUNT
};

struct Instance;

struct NativePerfFrameInfo
{
	s32 frameCounter;
	s32 timer;
	s32 levelID;
	s32 gameMode1;
	s32 loadingStage;
	s32 boolDemoMode;
	s32 numPlyrCurrGame;
	s32 elapsedTimeMS;
	s32 vsyncTillFlip;
	s32 vSync_between_drawSync;
	s32 frameTimer_VsyncCallback;
	s32 targetFPS;
	s32 fullscreen;
};

#if defined(CTR_INTERNAL)
int NativePerf_ConfigureFromArgs(int argc, char **argv);
void NativePerf_Shutdown(void);
void NativePerf_BeginFrame(const struct NativePerfFrameInfo *info);
void NativePerf_EndFrame(const struct NativePerfFrameInfo *info);
void NativePerf_BeginScope(enum NativePerfBucket bucket);
void NativePerf_EndScope(enum NativePerfBucket bucket);
void NativePerf_AddVRAMWritePixels(u32 pixels);
void NativePerf_AddVRAMUpload(u32 bytes);
void NativePerf_AddDrawCall(u32 triangles);
void NativePerf_AddSplits(u32 splits);
void NativePerf_AddShaderSwitch(void);
void NativePerf_AddTextureBind(void);
void NativePerf_AddBlendStateChange(void);
void NativePerf_AddDepthStateChange(void);
void NativePerf_AddStencilStateChange(void);
void NativePerf_SetStressTestActive(int active);
int NativePerf_IsStressTestActive(void);
int NativePerf_IsStressTestMeasuring(void);
int NativePerf_StressTestReachedFrameLimit(void);
void NativePerf_StressTestBeginRace(void);
void NativePerf_BotBegin(int driverID, enum NativePerfBotPhase phase);
void NativePerf_BotEnd(int driverID, enum NativePerfBotPhase phase);
int NativePerf_BotIDForInstance(const struct Instance *inst);
#else
static inline int NativePerf_ConfigureFromArgs(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

static inline void NativePerf_Shutdown(void)
{
}

static inline void NativePerf_BeginFrame(const struct NativePerfFrameInfo *info)
{
	(void)info;
}

static inline void NativePerf_EndFrame(const struct NativePerfFrameInfo *info)
{
	(void)info;
}

static inline void NativePerf_BeginScope(enum NativePerfBucket bucket)
{
	(void)bucket;
}

static inline void NativePerf_EndScope(enum NativePerfBucket bucket)
{
	(void)bucket;
}

static inline void NativePerf_AddVRAMWritePixels(u32 pixels)
{
	(void)pixels;
}

static inline void NativePerf_AddVRAMUpload(u32 bytes)
{
	(void)bytes;
}

static inline void NativePerf_AddDrawCall(u32 triangles)
{
	(void)triangles;
}

static inline void NativePerf_AddSplits(u32 splits)
{
	(void)splits;
}

static inline void NativePerf_AddShaderSwitch(void) {}
static inline void NativePerf_AddTextureBind(void) {}
static inline void NativePerf_AddBlendStateChange(void) {}
static inline void NativePerf_AddDepthStateChange(void) {}
static inline void NativePerf_AddStencilStateChange(void) {}

static inline void NativePerf_SetStressTestActive(int active)
{
	(void)active;
}

static inline int NativePerf_IsStressTestActive(void)
{
	return 0;
}

static inline int NativePerf_IsStressTestMeasuring(void)
{
	return 0;
}

static inline void NativePerf_StressTestBeginRace(void)
{
}
static inline void NativePerf_BotBegin(int driverID, enum NativePerfBotPhase phase)
{
    (void)driverID; (void)phase;
}
static inline void NativePerf_BotEnd(int driverID, enum NativePerfBotPhase phase)
{
    (void)driverID; (void)phase;
}
static inline int NativePerf_BotIDForInstance(const struct Instance *inst)
{
    (void)inst; return -1;
}
#endif

#endif
