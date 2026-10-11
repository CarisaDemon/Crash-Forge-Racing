/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/render/PsyX_render.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include "platform/native_renderer_types.h"
#include <SDL3/SDL.h>

#include "platform/native_gpu.h"
#include "platform/native_glad.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "platform/native_win32.h"

__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#endif // def WIN32

#define VRAM_FORMAT GL_RG
#define NATIVE_GPU_TIMER_QUERY_COUNT 4

/* Modern PSX semi-transparency fast path. OpenGL 3.3 / ARB_blend_func_extended
 * lets the fragment shader provide a second per-pixel blend factor. That means
 * average/add/quarter-source textured ABE can be drawn once instead of the old
 * opaque-STP pass + blended-STP pass for every tiny PS1 primitive. Keep a
 * runtime fallback for older GL drivers and for subtract mode. */
#ifndef GL_SRC1_COLOR
#define GL_SRC1_COLOR 0x88F9
#endif
#ifndef GL_ONE_MINUS_SRC1_COLOR
#define GL_ONE_MINUS_SRC1_COLOR 0x88FA
#endif
#ifndef GL_ONE_MINUS_SRC1_ALPHA
#define GL_ONE_MINUS_SRC1_ALPHA 0x88FB
#endif
#ifndef GL_MAX_DUAL_SOURCE_DRAW_BUFFERS
#define GL_MAX_DUAL_SOURCE_DRAW_BUFFERS 0x88FC
#endif

typedef void (APIENTRYP CTRPFNGLBINDFRAGDATALOCATIONINDEXEDPROC)(GLuint program, GLuint colorNumber, GLuint index, const GLchar *name);
global_variable CTRPFNGLBINDFRAGDATALOCATIONINDEXEDPROC s_glBindFragDataLocationIndexed = NULL;
global_variable int s_dualSourceBlendSupported = 0;
global_variable int s_previousPSXSinglePassBlendMode = -1;

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif

/* Default remains the conservative float storage. RG8 is exposed only as an
 * explicit experimental A/B mode because the PS1 VRAM source is two exact
 * bytes per texel and should not need 64-bit RG32F storage on modern GL. */
global_variable int s_experimentalCompactVRAM = 0;

extern SDL_Window *g_window;

#define NATIVE_SWAP_INTERVAL_SENTINEL 0x7fffffff
#define NATIVE_SWAP_WATCHDOG_SLOW_MS 2.5
#define NATIVE_SWAP_WATCHDOG_RECOVERY_STREAK 8
#define NATIVE_SWAP_WATCHDOG_RECOVERY_COOLDOWN_MS 500

/*
 * SDL/Windows/AMD can occasionally leave SwapWindow in a throttled compositor
 * state even while SDL_GL_GetSwapInterval reports 0. Keep our own swap-control
 * state and recover only after several consecutive blocking swaps; this avoids
 * touching driver state every frame (important for OBS/capture hooks).
 */
global_variable int s_lastRequestedSwapInterval = NATIVE_SWAP_INTERVAL_SENTINEL;
global_variable int s_lastLoggedSwapInterval = NATIVE_SWAP_INTERVAL_SENTINEL;
global_variable int s_swapWatchdogSlowStreak = 0;
global_variable int s_swapWatchdogFastStreak = 0;
global_variable int s_swapWatchdogRecoveryStage = 0;
global_variable int s_swapWatchdogResyncAttempts = 0;
global_variable u64 s_swapWatchdogLastRecoveryCounter = 0;
/* SDL's performance-counter frequency is constant for the process. Cache it
 * once instead of querying it on every present. */
global_variable u64 s_swapPerformanceFrequency = 0;

#define NATIVE_RENDERER_LOG(fmt, ...)   Platform_Log("[CTR Renderer] " fmt, ##__VA_ARGS__)
#define NATIVE_RENDERER_ERROR(fmt, ...) Platform_LogError("[CTR Renderer] [%s] - " fmt, __func__, ##__VA_ARGS__)

#define MAX_NUM_VERTEX_BUFFERS          (2)
#define PSX_SCREEN_ASPECT               (240.0f / 320.0f) // PSX screen is mapped always to this aspect

global_variable BlendMode s_previousBlendMode = BM_NONE;
global_variable int s_previousDepthMode = 0;
global_variable int s_previousStencilMode = 0;
global_variable int s_previousScissorState = 0;
global_variable int s_previousOffscreenState = 0;
global_variable int s_experimentalZBufferEnabled = 1;
global_variable int s_experimentalDepthEligible = 0;
global_variable RECT16 s_previousFramebuffer = {0, 0, 0, 0};
global_variable RECT16 s_previousOffscreen = {0, 0, 0, 0};

global_variable ShaderID s_previousShader = (ShaderID)-1;

global_variable TextureID s_vramTexturesDouble[2];
global_variable TextureID s_vramTexture;
global_variable TextureID s_rgLutTexture = (TextureID)-1;
global_variable int s_vramTextureIndex = 0;

global_variable TextureID s_framebufferTexture = (TextureID)-1;
global_variable TextureID s_offscreenRenderTexture = (TextureID)-1;

global_variable TextureID s_whiteTexture = (TextureID)-1;
global_variable TextureID s_lastBoundTexture = (TextureID)-1;

TextureID NativeRenderer_GetVRAMTexture(void)
{
	return s_vramTexture;
}

TextureID NativeRenderer_GetWhiteTexture(void)
{
	return s_whiteTexture;
}

int g_windowWidth = 0;
int g_windowHeight = 0;

global_variable int s_presentAspectW = 4;
global_variable int s_presentAspectH = 3;
global_variable SDL_Rect s_presentViewport = {0, 0, 0, 0};

int g_dbg_wireframeMode = 0;
int g_dbg_texturelessMode = 0;

int g_cfg_bilinearFiltering = 0;

/* Non-blocking GPU frame timer. Results are read only when the driver reports
 * them available, several frames later, so profiling never introduces a
 * glFinish-style stall into the gameplay path. */
global_variable GLuint s_gpuTimerQueries[NATIVE_GPU_TIMER_QUERY_COUNT];
global_variable u8 s_gpuTimerSubmitted[NATIVE_GPU_TIMER_QUERY_COUNT];
global_variable int s_gpuTimerWriteIndex = 0;
global_variable int s_gpuTimerOpenIndex = -1;
global_variable int s_gpuTimingEnabled = 0;
global_variable double s_lastGpuFrameMs = -1.0;

global_variable int s_vramNeedsUpdate = 1;
global_variable int s_framebufferNeedsUpdate = 0;
global_variable u32 *s_framebufferReadbackPixels = NULL;
global_variable size_t s_framebufferReadbackCapacity = 0;

internal u32 *NativeRenderer_GetFramebufferReadbackBuffer(size_t pixelCount)
{
	if (pixelCount == 0)
		return NULL;

	if (pixelCount > s_framebufferReadbackCapacity)
	{
		u32 *newPixels = (u32 *)realloc(
			s_framebufferReadbackPixels,
			pixelCount * sizeof(u32));
		if (newPixels == NULL)
			return NULL;

		s_framebufferReadbackPixels = newPixels;
		s_framebufferReadbackCapacity = pixelCount;
	}

	return s_framebufferReadbackPixels;
}

struct NativeVRAMDirtyRect
{
	int valid;
	int x0;
	int y0;
	int x1;
	int y1;
};

global_variable struct NativeVRAMDirtyRect s_vramDirty[2];

internal void NativeRenderer_MarkVRAMDirtyForTexture(int textureIndex, int x, int y, int w, int h)
{
	int x0;
	int y0;
	int x1;
	int y1;
	struct NativeVRAMDirtyRect *dirty;

	if ((textureIndex < 0) || (textureIndex >= 2) || (w <= 0) || (h <= 0))
	{
		return;
	}

	x0 = x;
	y0 = y;
	x1 = x + w;
	y1 = y + h;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > VRAM_WIDTH) x1 = VRAM_WIDTH;
	if (y1 > VRAM_HEIGHT) y1 = VRAM_HEIGHT;

	if ((x0 >= x1) || (y0 >= y1))
	{
		return;
	}

	dirty = &s_vramDirty[textureIndex];
	if (!dirty->valid)
	{
		dirty->valid = 1;
		dirty->x0 = x0;
		dirty->y0 = y0;
		dirty->x1 = x1;
		dirty->y1 = y1;
		return;
	}

	if (x0 < dirty->x0) dirty->x0 = x0;
	if (y0 < dirty->y0) dirty->y0 = y0;
	if (x1 > dirty->x1) dirty->x1 = x1;
	if (y1 > dirty->y1) dirty->y1 = y1;
}

internal void NativeRenderer_MarkVRAMDirtyRect(int x, int y, int w, int h)
{
	NativeRenderer_MarkVRAMDirtyForTexture(0, x, y, w, h);
	NativeRenderer_MarkVRAMDirtyForTexture(1, x, y, w, h);
	s_vramNeedsUpdate = 1;
}

internal void NativeRenderer_MarkOtherVRAMTextureDirtyRect(int x, int y, int w, int h)
{
	int currentIndex = -1;

	for (int i = 0; i < 2; i++)
	{
		if (s_vramTexturesDouble[i] == s_vramTexture)
		{
			currentIndex = i;
			break;
		}
	}

	if (currentIndex < 0)
	{
		NativeRenderer_MarkVRAMDirtyForTexture(0, x, y, w, h);
		NativeRenderer_MarkVRAMDirtyForTexture(1, x, y, w, h);
		return;
	}

	NativeRenderer_MarkVRAMDirtyForTexture(currentIndex ^ 1, x, y, w, h);
}

internal void NativeRenderer_ResetVRAMDirtyState(void)
{
	memset(s_vramDirty, 0, sizeof(s_vramDirty));
	NativeRenderer_MarkVRAMDirtyForTexture(0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	NativeRenderer_MarkVRAMDirtyForTexture(1, 0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	s_vramNeedsUpdate = 1;
}

internal GLenum NativeRenderer_GetVRAMInternalFormat(void)
{
	return s_experimentalCompactVRAM ? GL_RG8 : GL_RG32F;
}

internal void NativeRenderer_GpuTimerPoll(void)
{
	if (!s_gpuTimingEnabled)
		return;

	for (int i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		GLuint available = 0;
		GLuint elapsedNs = 0;

		if (!s_gpuTimerSubmitted[i])
			continue;

		glGetQueryObjectuiv(s_gpuTimerQueries[i], GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available)
			continue;

		/* A 32-bit nanosecond result covers >4 seconds, far beyond any frame
		 * we want to keep running. Avoid ui64 entry points for older GL 3.x
		 * drivers while retaining non-blocking timing. */
		glGetQueryObjectuiv(s_gpuTimerQueries[i], GL_QUERY_RESULT, &elapsedNs);
		s_lastGpuFrameMs = (double)elapsedNs / 1000000.0;
		s_gpuTimerSubmitted[i] = 0;
	}
}

internal void NativeRenderer_GpuTimerBeginFrame(void)
{
	int slot;

	if (!s_gpuTimingEnabled)
		return;

	if (s_gpuTimerQueries[0] == 0)
		glGenQueries(NATIVE_GPU_TIMER_QUERY_COUNT, s_gpuTimerQueries);

	NativeRenderer_GpuTimerPoll();

	slot = s_gpuTimerWriteIndex;
	for (int i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		const int candidate = (s_gpuTimerWriteIndex + i) % NATIVE_GPU_TIMER_QUERY_COUNT;
		if (!s_gpuTimerSubmitted[candidate])
		{
			slot = candidate;
			break;
		}
	}

	if (s_gpuTimerSubmitted[slot])
	{
		/* Driver still owns every query. Skip one sample instead of blocking. */
		s_gpuTimerOpenIndex = -1;
		return;
	}

	glBeginQuery(GL_TIME_ELAPSED, s_gpuTimerQueries[slot]);
	s_gpuTimerOpenIndex = slot;
}

internal void NativeRenderer_GpuTimerEndFrame(void)
{
	if (s_gpuTimerOpenIndex < 0)
		return;

	glEndQuery(GL_TIME_ELAPSED);
	s_gpuTimerSubmitted[s_gpuTimerOpenIndex] = 1;
	s_gpuTimerWriteIndex =
		(s_gpuTimerOpenIndex + 1) % NATIVE_GPU_TIMER_QUERY_COUNT;
	s_gpuTimerOpenIndex = -1;
}

internal int NativeRenderer_InitialiseGLContext(char *windowName, int fullscreen);
internal int NativeRenderer_InitialiseGLExt(void);
internal void NativeRenderer_DestroyTexture(TextureID texture);
internal void NativeRenderer_SetScissorState(int enable);
internal void NativeRenderer_EnableDepth(int enable);
internal void NativeRenderer_SetViewPort(int x, int y, int width, int height);
internal void NativeRenderer_SetPresentationAspect(int width, int height);
internal void NativeRenderer_UpdatePresentationViewport(void);
internal void NativeRenderer_ClearPresentationBars(void);
internal void NativeRenderer_SetWireframe(int enable);
internal void NativeRenderer_BindVertexBuffer(void);
internal void NativeRenderer_GpuTimerBeginFrame(void);
internal void NativeRenderer_GpuTimerEndFrame(void);
internal GLenum NativeRenderer_GetVRAMInternalFormat(void);

global_variable GLuint s_glVertexArray[2];
global_variable GLuint s_glVertexBuffer[2];
global_variable int s_curVertexBuffer = 0;

global_variable GLuint s_glBlitFramebuffer;

global_variable GLuint s_glVramFramebuffer;

global_variable GLuint s_glOffscreenFramebuffer;


internal int NativeRenderer_InitialiseGLContext(char *windowName, int fullscreen)
{
	SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	/* During unattended stress benchmarks leave the user's keyboard and
	 * mouse focus completely with their other desktop applications. */
	{
		FILE *bench = fopen("debug/perf.autorun", "rb");
		if (bench != NULL)
		{
			FILE *focused = fopen("debug/perf.focused", "rb");
			const int allowFocus = focused != NULL;
			fclose(bench);
			if (focused != NULL) fclose(focused);
			if (allowFocus)
			{
				/* Only for a user-authorized on-screen A/B. Never interfere
				 * with unattended background perf.autorun by default. */
				Platform_LogWarn("[PERF TEST] user-authorized FOCUSABLE gameplay benchmark\n");
			}
			else
			{
				windowFlags |= SDL_WINDOW_NOT_FOCUSABLE;
				Platform_Log("[PERF TEST] SDL window NOT_FOCUSABLE (no desktop input capture)\n");
			}
		}
	}
#endif

	if (fullscreen)
	{
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}

	g_window = SDL_CreateWindow(windowName, g_windowWidth, g_windowHeight, windowFlags);

	if (g_window == NULL)
	{
		NATIVE_RENDERER_ERROR("Failed to initialise SDL window!\n");
		return 0;
	}

	int major_version = 3;
	int minor_version = 3;
	int profile = SDL_GL_CONTEXT_PROFILE_CORE;

	// find best OpenGL version
	do
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major_version);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor_version);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);

		if (SDL_GL_CreateContext(g_window))
		{
			break;
		}

		minor_version--;

	} while (minor_version >= 0);

	if (minor_version == -1)
	{
		NATIVE_RENDERER_ERROR("Failed to initialise - OpenGL 3.x is not supported. Please update video drivers.\n");
		return 0;
	}

	return 1;
}

internal int NativeRenderer_InitialiseGLExt(void)
{
	GLenum err = gladLoadGL();

	if (err == 0)
	{
		return 0;
	}

	const char *rend = (const char *)glGetString(GL_RENDERER);
	const char *vendor = (const char *)glGetString(GL_VENDOR);
	NATIVE_RENDERER_LOG("*Video adapter: %s by %s\n", rend, vendor);

	const char *versionStr = (const char *)glGetString(GL_VERSION);
	NATIVE_RENDERER_LOG("*OpenGL version: %s\n", versionStr);

	const char *glslVersionStr = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	NATIVE_RENDERER_LOG("*GLSL version: %s\n", glslVersionStr);

	/* GLAD in this project was generated for GL 3.1, while current desktop
	 * drivers expose the 3.3 dual-source blend entry point. Load just this one
	 * optional function through SDL and keep the old two-pass path as fallback. */
	{
		int major = 0;
		int minor = 0;
		int core33 = 0;
		int hasExtension = SDL_GL_ExtensionSupported("GL_ARB_blend_func_extended") ? 1 : 0;
		GLint maxDualSourceBuffers = 0;

		if (versionStr != NULL)
		{
			sscanf(versionStr, "%d.%d", &major, &minor);
			core33 = (major > 3) || ((major == 3) && (minor >= 3));
		}

		if (hasExtension || core33)
		{
			s_glBindFragDataLocationIndexed =
				(CTRPFNGLBINDFRAGDATALOCATIONINDEXEDPROC)SDL_GL_GetProcAddress("glBindFragDataLocationIndexed");
		}

		if (s_glBindFragDataLocationIndexed != NULL)
		{
			glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &maxDualSourceBuffers);
		}

		s_dualSourceBlendSupported =
			(s_glBindFragDataLocationIndexed != NULL) && (maxDualSourceBuffers > 0);
		NATIVE_RENDERER_LOG("*PSX single-pass textured semi-transparency: %s (dual-source buffers=%d)\n",
		                    s_dualSourceBlendSupported ? "ON" : "fallback two-pass",
		                    (int)maxDualSourceBuffers);
	}

	return 1;
}

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen)
{
	g_windowWidth = width;
	g_windowHeight = height;
	NativeRenderer_SetPresentationAspect(width, height);

	// Due to debugging in fullscreen
	SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
	/* Reserved for the hidden experimental native Z-buffer path.
	   Retail/normal rendering leaves depth testing disabled. */
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	if (!NativeRenderer_InitialiseGLContext(windowName, fullscreen))
	{
		NATIVE_RENDERER_ERROR("Failed to Initialise GL Context!\n");
		return 0;
	}

	if (!NativeRenderer_InitialiseGLExt())
	{
		NATIVE_RENDERER_ERROR("Failed to Intialise GL extensions\n");
		return 0;
	}

	return 1;
}

void NativeRenderer_Shutdown(void)
{
	/* Resident track geometry is owned by this GL context. */
	NativeRenderer_LevelPCReset();
	if (s_gpuTimerOpenIndex >= 0)
	{
		glEndQuery(GL_TIME_ELAPSED);
		s_gpuTimerOpenIndex = -1;
	}
	if (s_gpuTimerQueries[0] != 0)
	{
		glDeleteQueries(NATIVE_GPU_TIMER_QUERY_COUNT, s_gpuTimerQueries);
		memset(s_gpuTimerQueries, 0, sizeof(s_gpuTimerQueries));
		memset(s_gpuTimerSubmitted, 0, sizeof(s_gpuTimerSubmitted));
	}

	glDeleteVertexArrays(2, s_glVertexArray);
	glDeleteBuffers(2, s_glVertexBuffer);

	glDeleteFramebuffers(1, &s_glBlitFramebuffer);
	glDeleteFramebuffers(1, &s_glOffscreenFramebuffer);
	glDeleteFramebuffers(1, &s_glVramFramebuffer);

	NativeRenderer_DestroyTexture(s_vramTexturesDouble[0]);
	NativeRenderer_DestroyTexture(s_vramTexturesDouble[1]);

	NativeRenderer_DestroyTexture(s_whiteTexture);
	NativeRenderer_DestroyTexture(s_rgLutTexture);
	NativeRenderer_DestroyTexture(s_framebufferTexture);
	NativeRenderer_DestroyTexture(s_offscreenRenderTexture);

	free(s_framebufferReadbackPixels);
	s_framebufferReadbackPixels = NULL;
	s_framebufferReadbackCapacity = 0;
}

internal void NativeRenderer_ApplySwapIntervalState(
	int swapInterval,
	int force,
	const char *reason)
{
	int effectiveInterval = 0;
	bool setOk;
	bool getOk;

	/* Swap interval is GL-context state. Avoid crossing SDL/driver every frame,
	 * but allow the watchdog/window reset path to force a fresh application. */
	if (!force && (s_lastRequestedSwapInterval == swapInterval))
		return;

	s_lastRequestedSwapInterval = swapInterval;
	setOk = SDL_GL_SetSwapInterval(swapInterval);
	getOk = SDL_GL_GetSwapInterval(&effectiveInterval);

	if (force || (s_lastLoggedSwapInterval != swapInterval))
	{
		Platform_Log(
			"[CTR Renderer] swap interval request=%d set=%s effective=%d query=%s reason=%s\n",
			swapInterval,
			setOk ? "OK" : "FAIL",
			effectiveInterval,
			getOk ? "OK" : "FAIL",
			reason != NULL ? reason : "normal");
		s_lastLoggedSwapInterval = swapInterval;
	}
}

void NativeRenderer_InvalidateSwapIntervalState(void)
{
	s_lastRequestedSwapInterval = NATIVE_SWAP_INTERVAL_SENTINEL;
	s_swapWatchdogSlowStreak = 0;
	s_swapWatchdogFastStreak = 0;
	s_swapWatchdogRecoveryStage = 0;
	s_swapWatchdogResyncAttempts = 0;
	s_swapWatchdogLastRecoveryCounter = 0;
}

void NativeRenderer_UpdateSwapIntervalState(int swapInterval)
{
	NativeRenderer_ApplySwapIntervalState(swapInterval, 0, "normal");
}

void NativeRenderer_BeginScene(void)
{
	NativeRenderer_GpuTimerBeginFrame();
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
	s_lastBoundTexture = 0;

	NativeRenderer_UpdatePresentationViewport();
	NativeRenderer_ClearPresentationBars();
	s_experimentalDepthEligible = 0;
	NativeRenderer_EnableDepth(0);
	if (s_experimentalZBufferEnabled)
		glClear(GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	else
		glClear(GL_STENCIL_BUFFER_BIT);

	NativeRenderer_UpdateVRAM();
	NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);

	if (g_dbg_wireframeMode)
	{
		NativeRenderer_SetWireframe(1);

		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Same-frame PC-level visual QA. Capture only the game's own OpenGL
 * viewport, never the desktop or other applications, and never touch
 * keyboard/mouse. This opt-in readback is intentionally excluded from
 * normal gameplay and creates exactly two PPM files per process. */
static void NativeRenderer_CaptureGameViewportPPM(const char *path)
{
	const int w=s_presentViewport.w, h=s_presentViewport.h;
	if (w<8 || h<8 || w>4096 || h>2304)
	{
		Platform_Log("[LEVEL PC GPU] screenshot skipped invalid viewport %dx%d\n",w,h);
		return;
	}
	const size_t stride=(size_t)w*3u;
	u8 *pixels=(u8 *)malloc(stride*(size_t)h);
	if (!pixels) return;
	GLint oldPack=4;
	glGetIntegerv(GL_PACK_ALIGNMENT,&oldPack);
	GLenum stale=glGetError();
	if (stale!=GL_NO_ERROR)
		Platform_Log("[LEVEL PC GPU] screenshot discarded previous GL error=0x%x\n",(unsigned)stale);
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	glReadPixels(s_presentViewport.x,s_presentViewport.y,
		w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels);
	GLenum err=glGetError();
	glPixelStorei(GL_PACK_ALIGNMENT,oldPack);
	if (err == GL_NO_ERROR)
	{
		FILE *f=fopen(path,"wb");
		if (f)
		{
			fprintf(f,"P6\n%d %d\n255\n",w,h);
			for (int y=h-1;y>=0;--y)
				fwrite(pixels+(size_t)y*stride,1,stride,f);
			fclose(f);
			Platform_Log("[LEVEL PC GPU] same-frame game-only screenshot %s (%dx%d)\n",path,w,h);
		}
	}
	else
		Platform_LogError("[LEVEL PC GPU] game framebuffer read failed (GL=%u)\n",(unsigned)err);
	free(pixels);
}

/* Correct QA capture for a native B5 OT marker. The regular EndScene
 * before/after snapshots both happen AFTER B5, so matching hashes did NOT
 * establish PS1-versus-GPU visual parity. This test captures immediately
 * around the actual resident terrain draw, after flushing preceding OT.
 * Only runs in internal stress QA when the explicit marker is present. */
void NativeRenderer_B5SnapshotStage(int stage)
{
    static int checked, enabled, count, armed, completed;
    if (!checked)
    {
        FILE *f = fopen("debug/level_pc_b5_capture.enable", "rb");
        enabled = (f != NULL);
        if (f) fclose(f);
        checked = 1;
    }
    if (!enabled || completed || !NativePerf_IsStressTestMeasuring())
        return;
    if (stage == 0)
    {
        if (++count == 240)
        {
            NativeRenderer_CaptureGameViewportPPM("debug/level_pc_B5_before.ppm");
            armed = 1;
            Platform_Log("[LEVEL PC GPU] B5 QA captured pre-native-terrain OT stage; NOT a complete PS1 reference frame\n");
        }
    }
    else if (stage == 1 && armed)
    {
        NativeRenderer_CaptureGameViewportPPM("debug/level_pc_B5_after.ppm");
        completed = 1;
        armed = 0;
        Platform_Log("[LEVEL PC GPU] B5 QA captured post-native-terrain OT stage; compare the actual on-screen native draw\n");
    }
}

static int NativeRenderer_LevelPCShouldCaptureSnapshot(void)
{
	static int checked=0,enabled=0,series=0,written=0,seen=0;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_snapshot.enable","rb");
		enabled=f!=NULL;
		if(f) fclose(f);
		f=fopen("debug/level_pc_snapshot_series.enable","rb");
		series=f!=NULL;
		if(f) fclose(f);
		checked=1;
		Platform_Log("[LEVEL PC GPU] screenshot QA marker=%d series=%d presentViewport=%dx%d\n",
			enabled,series,s_presentViewport.w,s_presentViewport.h);
	}
	if (!enabled || !NativePerf_IsStressTestMeasuring())
		return 0;
	++seen;
	if(series)
	{
		switch(seen)
		{
		case 240:case 800:case 1500:case 2500:case 3800:
			return seen;
		default:return 0;
		}
	}
	if(written || seen<240)return 0;
	written=1;
	return 1;
}
#endif

void NativeRenderer_EndScene(void)
{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const int pcSnapshot=NativeRenderer_LevelPCShouldCaptureSnapshot();
	if (pcSnapshot)
	{
		char path[128];
		if (pcSnapshot==1)
			NativeRenderer_CaptureGameViewportPPM("debug/level_pc_snapshot_before.ppm");
		else
		{
			snprintf(path,sizeof(path),"debug/level_pc_series_before_%04d.ppm",pcSnapshot);
			NativeRenderer_CaptureGameViewportPPM(path);
		}
	}
#endif
	/* Explicit opt-in preview overlays only a handful of compiled blocks
	 * after the retail OT finishes. There is no extra draw by default. */
	NativeRenderer_LevelPCDrawQueued();
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (pcSnapshot)
	{
		char path[128];
		if (pcSnapshot==1)
			NativeRenderer_CaptureGameViewportPPM("debug/level_pc_snapshot_after.ppm");
		else
		{
			snprintf(path,sizeof(path),"debug/level_pc_series_after_%04d.ppm",pcSnapshot);
			NativeRenderer_CaptureGameViewportPPM(path);
		}
	}
#endif
	s_framebufferNeedsUpdate = 1;

	if (g_dbg_wireframeMode)
	{
		NativeRenderer_SetWireframe(0);
	}

	glBindVertexArray(0);
	NativeRenderer_GpuTimerEndFrame();
}

//----------------------------------------------------------------------------------------

u16 vram[VRAM_WIDTH * VRAM_HEIGHT];
global_variable u8 rgLUT[LUT_WIDTH * LUT_HEIGHT * sizeof(u32)];

internal int NativeRenderer_IntAbs(int value)
{
	return value < 0 ? -value : value;
}

internal int NativeRenderer_GCD(int a, int b)
{
	a = NativeRenderer_IntAbs(a);
	b = NativeRenderer_IntAbs(b);

	while (b != 0)
	{
		int t = a % b;
		a = b;
		b = t;
	}

	return a;
}

internal void NativeRenderer_SetPresentationAspect(int width, int height)
{
	const int divisor = NativeRenderer_GCD(width, height);

	if ((width <= 0) || (height <= 0) || (divisor <= 0))
	{
		return;
	}

	s_presentAspectW = width / divisor;
	s_presentAspectH = height / divisor;
}

void NativeRenderer_SetDisplayAspect(int wide)
{
	/* Aspect ratio is presentation-only. Window/backbuffer resolution is a
	   separate setting in v2.0.0, so switching 4:3/16:9 must not secretly
	   resize the host window anymore. */
	NativeRenderer_SetPresentationAspect(wide ? 16 : 4, wide ? 9 : 3);
	NativeRenderer_UpdatePresentationViewport();
}

internal void NativeRenderer_UpdatePresentationViewport(void)
{
	int viewportW;
	int viewportH;

	if ((g_windowWidth <= 0) || (g_windowHeight <= 0) || (s_presentAspectW <= 0) || (s_presentAspectH <= 0))
	{
		s_presentViewport.x = 0;
		s_presentViewport.y = 0;
		s_presentViewport.w = g_windowWidth;
		s_presentViewport.h = g_windowHeight;
		return;
	}

	viewportW = g_windowWidth;
	viewportH = (viewportW * s_presentAspectH) / s_presentAspectW;

	if (viewportH > g_windowHeight)
	{
		viewportH = g_windowHeight;
		viewportW = (viewportH * s_presentAspectW) / s_presentAspectH;
	}

	if (viewportW < 1)
	{
		viewportW = 1;
	}
	if (viewportH < 1)
	{
		viewportH = 1;
	}

	s_presentViewport.w = viewportW;
	s_presentViewport.h = viewportH;
	s_presentViewport.x = (g_windowWidth - viewportW) / 2;
	s_presentViewport.y = (g_windowHeight - viewportH) / 2;
}

int NativeRenderer_WindowToPSX(float windowX, float windowY, int *psxX, int *psxY)
{
	int topMargin;
	float relX;
	float relY;

	if ((psxX == NULL) || (psxY == NULL) || (s_presentViewport.w <= 0) || (s_presentViewport.h <= 0))
	{
		return 0;
	}

	topMargin = g_windowHeight - (s_presentViewport.y + s_presentViewport.h);
	if ((windowX < (float)s_presentViewport.x) ||
	    (windowX >= (float)(s_presentViewport.x + s_presentViewport.w)) ||
	    (windowY < (float)topMargin) ||
	    (windowY >= (float)(topMargin + s_presentViewport.h)))
	{
		return 0;
	}

	relX = (windowX - (float)s_presentViewport.x) / (float)s_presentViewport.w;
	relY = (windowY - (float)topMargin) / (float)s_presentViewport.h;

	*psxX = (int)(relX * (float)SCREEN_WIDTH);
	*psxY = (int)(relY * (float)SCREEN_HEIGHT);

	if (*psxX < 0) *psxX = 0;
	if (*psxX >= SCREEN_WIDTH) *psxX = SCREEN_WIDTH - 1;
	if (*psxY < 0) *psxY = 0;
	if (*psxY >= SCREEN_HEIGHT) *psxY = SCREEN_HEIGHT - 1;
	return 1;
}

internal void NativeRenderer_ClearHostRect(int x, int y, int width, int height)
{
	if ((width <= 0) || (height <= 0))
	{
		return;
	}

	glScissor(x, y, width, height);
	glClear(GL_COLOR_BUFFER_BIT);
}

internal void NativeRenderer_ClearPresentationBars(void)
{
	GLint previousScissorBox[4];
	GLfloat previousClearColor[4];
	const GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	const int viewportRight = s_presentViewport.x + s_presentViewport.w;
	const int viewportTop = s_presentViewport.y + s_presentViewport.h;

	if ((g_windowWidth <= 0) || (g_windowHeight <= 0))
	{
		return;
	}

	if ((s_presentViewport.x == 0) && (s_presentViewport.y == 0) && (s_presentViewport.w == g_windowWidth) && (s_presentViewport.h == g_windowHeight))
	{
		return;
	}

	glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

	glEnable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	NativeRenderer_ClearHostRect(0, 0, s_presentViewport.x, g_windowHeight);
	NativeRenderer_ClearHostRect(viewportRight, 0, g_windowWidth - viewportRight, g_windowHeight);
	NativeRenderer_ClearHostRect(s_presentViewport.x, 0, s_presentViewport.w, s_presentViewport.y);
	NativeRenderer_ClearHostRect(s_presentViewport.x, viewportTop, s_presentViewport.w, g_windowHeight - viewportTop);

	if (previousScissorEnabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}

	glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
	s_previousScissorState = previousScissorEnabled ? 1 : 0;
}

void NativeRenderer_ResetDevice(void)
{
	NativeRenderer_UpdatePresentationViewport();

	/* SDL/Windows can rebuild presentation state on resize/fullscreen changes.
	 * Invalidate our cached value so VSync OFF is really sent to the new state. */
	NativeRenderer_InvalidateSwapIntervalState();
	NativeRenderer_UpdateSwapIntervalState(0);
}

typedef struct
{
	// shader itself
	ShaderID shader;

	GLint projectionLoc;
	GLint bilinearFilterLoc;
	GLint texelSizeLoc;
	GLint texLoc;
	GLint lutLoc;
	GLint psxSemiTransPassLoc;
	GLint psxDrawMaskSetLoc;
	GLint psxTextureOutputStpLoc;
	u32 projectionGeneration;
} GTEShader;

internal int NativeRenderer_Shader_CheckShaderStatus(GLuint shader);
internal int NativeRenderer_Shader_CheckProgramStatus(GLuint program);
internal ShaderID NativeRenderer_Shader_Compile(const char *source, bool isPsxShader);
internal void NativeRenderer_GenerateCommonTextures(void);
internal TextureID NativeRenderer_CreateRGBATexture(int width, int height, u8 *data);
internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source);
internal void NativeRenderer_InitialisePSXShaders(void);
internal void NativeRenderer_InitRG8LUT(void);
internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar);
internal void NativeRenderer_SetShader(const ShaderID shader);
internal void NativeRenderer_CopyRGBAFramebufferToVRAM(u32 *src, int x, int y, int w, int h, int update_vram, int flip_y);

global_variable GTEShader s_gteShader4;
global_variable GTEShader s_gteShader8;
global_variable GTEShader s_gteShader16;
global_variable GTEShader s_gteShader32Rgba;

GLint u_projectionLoc;
GLint u_bilinearFilterLoc;
GLint u_texelSizeLoc;
GLint u_psxSemiTransPassLoc;
GLint u_psxDrawMaskSetLoc;
GLint u_psxTextureOutputStpLoc;

/* These uniforms used to be resent for every PS1 split. At 200+ Hz that can
 * mean thousands of redundant driver calls per second, so cache the values per
 * currently bound shader. NativeRenderer_SetShader invalidates them. */
global_variable int s_cachedBilinearFilter = -1;
global_variable int s_cachedPSXSemiTransPass = -1;
global_variable int s_cachedPSXDrawMaskSet = -1;
global_variable int s_cachedPSXTextureOutputStp = -1;
global_variable int s_cachedOverrideTextureWidth = -1;
global_variable int s_cachedOverrideTextureHeight = -1;

/* The 2D projection changes only on display/offscreen transitions, but the old
 * path resent the same 4x4 matrix once per PS1 draw split. At 180+ splits per
 * frame that is pure driver traffic. Cache one CPU copy and stamp each shader
 * program when it has received the current generation. */
global_variable float s_projectionMatrix[16];
global_variable float s_projectionLeft;
global_variable float s_projectionRight;
global_variable float s_projectionBottom;
global_variable float s_projectionTop;
global_variable float s_projectionNear;
global_variable float s_projectionFar;
global_variable int s_projectionValid = 0;
global_variable u32 s_projectionGeneration = 1;

#define GPU_SAMPLE_TEXTURE_4BIT_FUNC                                             \
	"	// returns 16 bit colour\n"                                                \
	"	vec2 samplePSX(vec2 tc) {\n"                                               \
	"		vec2 uv = (tc * vec2(0.25, 1.0) + v_page_clut.xy) * c_VRAMTexel;\n"       \
	"		vec2 comp = VRAM(uv);\n"                                                  \
	"		int index = int(fract(tc.x / 4.0 + 0.0001) * 4.0);\n"                     \
	"		float v = _idx2(comp, index / 2) * (255.0 / 16.0);\n"                     \
	"		float f = floor(v + 0.001);\n"                                            \
	"		vec2 c = vec2((v - f) * 16.0, f);\n"                                      \
	"		vec2 clut_pos = v_page_clut.zw;\n"                                        \
	"		clut_pos.x += mix(c[0], c[1], mod(float(index), 2.0)) * c_VRAMTexel.x;\n" \
	"		return VRAM(clut_pos);\n"                                                 \
	"	}\n"

#define GPU_SAMPLE_TEXTURE_8BIT_FUNC                                      \
	"	// returns 16 bit colour\n"                                         \
	"	vec2 samplePSX(vec2 tc) {\n"                                        \
	"		vec2 uv = (tc * vec2(0.5, 1.0) + v_page_clut.xy) * c_VRAMTexel;\n" \
	"		vec2 comp = VRAM(uv);\n"                                           \
	"		vec2 clut_pos = v_page_clut.zw;\n"                                 \
	"		int index = int(mod(tc.x, 2.0));\n"                                \
	"		clut_pos.x += _idx2(comp, index) * 255.0 * c_VRAMTexel.x;\n"       \
	"		return VRAM(clut_pos);\n"                                          \
	"	}\n"

#define GPU_SAMPLE_TEXTURE_16BIT_FUNC                    \
	"	vec2 samplePSX(vec2 tc) {\n"                       \
	"		vec2 uv = (tc + v_page_clut.xy) * c_VRAMTexel;\n" \
	"		return VRAM(uv);\n"                               \
	"	}\n"

#define GPU_FETCH_VRAM_FUNC                                        \
	"	const vec2 c_VRAMTexel = vec2(1.0 / 1024.0, 1.0 / 512.0);\n" \
	"	uniform sampler2D s_texture;\n"                              \
	"	vec2 VRAM(vec2 uv) { return texture2D(s_texture, uv).rg; }\n"

#define GPU_STP_PASS_FUNC                                                                     \
	"	float texelVisible(vec2 rg) { return float(rg.x + rg.y > 0.0); }\n"                     \
	"	float stpWeight(vec2 rg) { return step(0.5, rg.y); }\n"                                 \
	"	bool discardForSemiTransPass(float visible, float stpVisible, float nonStpVisible) {\n" \
	"		if(visible < 0.5) { return true; }\n"                                                  \
	"		if(psxSemiTransPass == 1 && nonStpVisible < 0.5) { return true; }\n"                   \
	"		if(psxSemiTransPass == 2 && stpVisible < 0.5) { return true; }\n"                      \
	"		return false;\n"                                                                       \
	"	}\n"

#define GPU_DITHERING                                             \
	"	const mat4 c_dither = mat4(\n"                              \
	"		-4.0,  +0.0,  -3.0,  +1.0,\n"                              \
	"		+2.0,  -2.0,  +3.0,  -1.0,\n"                              \
	"		-3.0,  +1.0,  -4.0,  +0.0,\n"                              \
	"		+3.0,  -1.0,  +2.0,  -2.0) / 255.0;\n"                     \
	"	vec4 dither(vec4 color) {\n"                                \
	"		ivec2 dc = ivec2(mod(floor(v_ditherCoord), 4.0));\n"       \
	"		color.xyz += vec3(c_dither[dc.x][dc.y] * v_texcoord.w);\n" \
	"		return color;\n"                                           \
	"	}\n"

#define GPU_ARRAY_FUNC "	float _idx2(vec2 array, int idx) { return array[idx]; }\n"

#define GPU_FRAGMENT_SAMPLE_SHADER(bit)                                                                                                               \
	GPU_FETCH_VRAM_FUNC                                                                                                                               \
	GPU_ARRAY_FUNC                                                                                                                                    \
	GPU_SAMPLE_TEXTURE_##bit##BIT_FUNC                                                                                                                \
	    "#ifdef NATIVE_DUAL_SOURCE_BLEND\n"                                                                                                          \
	    "out vec4 fragBlend;\n"                                                                                                                       \
	    "#endif\n"                                                                                                                                    \
	    "	uniform sampler2D s_rgLut;\n"                                                                                                               \
	    "	uniform int bilinearFilter;\n"                                                                                                              \
	    "	uniform int psxSemiTransPass;\n"                                                                                                            \
	    "	uniform int psxDrawMaskSet;\n"                                                                                                              \
	    "	uniform int psxTextureOutputStp;\n"                                                                                                         \
	    "	float sampledStp = 0.0;\n"                                                                                                                  \
	    "	const vec2 c_LUTTexel = vec2(1.0 / 256.0, 1.0 / 256.0);\n"                                                                                  \
	    "	vec4 lut(vec2 rg) { return texture2D(s_rgLut, rg - c_LUTTexel * 0.0001); }\n" GPU_STP_PASS_FUNC "	vec4 bilinearTextureSample(vec2 P) {\n" \
	    "		vec2 frac = fract(P);\n"                                                                                                                   \
	    "		vec2 pixel = floor(P);\n"                                                                                                                  \
	    "		vec2 C11 = samplePSX(pixel);\n"                                                                                                            \
	    "		vec2 C21 = samplePSX(pixel + vec2(1.0, 0.0));\n"                                                                                           \
	    "		vec2 C12 = samplePSX(pixel + vec2(0.0, 1.0));\n"                                                                                           \
	    "		vec2 C22 = samplePSX(pixel + vec2(1.0, 1.0));\n"                                                                                           \
	    "		float v11 = texelVisible(C11);\n"                                                                                                          \
	    "		float v21 = texelVisible(C21);\n"                                                                                                          \
	    "		float v12 = texelVisible(C12);\n"                                                                                                          \
	    "		float v22 = texelVisible(C22);\n"                                                                                                          \
	    "		float s11 = v11 * stpWeight(C11);\n"                                                                                                       \
	    "		float s21 = v21 * stpWeight(C21);\n"                                                                                                       \
	    "		float s12 = v12 * stpWeight(C12);\n"                                                                                                       \
	    "		float s22 = v22 * stpWeight(C22);\n"                                                                                                       \
	    "		float n11 = v11 - s11;\n"                                                                                                                  \
	    "		float n21 = v21 - s21;\n"                                                                                                                  \
	    "		float n12 = v12 - s12;\n"                                                                                                                  \
	    "		float n22 = v22 - s22;\n"                                                                                                                  \
	    "		float ax1 = mix(v11, v21, frac.x);\n"                                                                                                      \
	    "		float ax2 = mix(v12, v22, frac.x);\n"                                                                                                      \
	    "		float axm = mix(ax1, ax2, frac.y);\n"                                                                                                      \
	    "		float sx1 = mix(s11, s21, frac.x);\n"                                                                                                      \
	    "		float sx2 = mix(s12, s22, frac.x);\n"                                                                                                      \
	    "		float stp = mix(sx1, sx2, frac.y);\n"                                                                                                      \
	    "		float nx1 = mix(n11, n21, frac.x);\n"                                                                                                      \
	    "		float nx2 = mix(n12, n22, frac.x);\n"                                                                                                      \
	    "		float nonStp = mix(nx1, nx2, frac.y);\n"                                                                                                   \
	    "		vec2 rg = mix(mix(C11, C21, frac.x), mix(C12, C22, frac.x), frac.y);\n"                                                                    \
	    "		sampledStp = stp;\n"                                                                                                                       \
	    "		if(discardForSemiTransPass(axm, stp, nonStp)) { discard; }\n"                                                                              \
	    "		vec4 x1 = mix(lut(C11), lut(C21), frac.x);\n"                                                                                              \
	    "		vec4 x2 = mix(lut(C12), lut(C22), frac.x);\n"                                                                                              \
	    "		vec4 t = mix(x1, x2, frac.y);\n"                                                                                                           \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	vec4 nearestTextureSample(vec2 P) {\n"                                                                                                      \
	    "		vec2 rg = samplePSX(P);\n"                                                                                                                 \
	    "		float visible = texelVisible(rg);\n"                                                                                                       \
	    "		sampledStp = visible * stpWeight(rg);\n"                                                                                                   \
	    "		if(discardForSemiTransPass(visible, sampledStp, visible - sampledStp)) { discard; }\n"                                                     \
	    "		vec4 t = lut(rg);\n"                                                                                                                       \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	void main() {\n"                                                                                                                            \
	    "\t\tvec2 sampleUv = (v_texcoord_persp.z > 0.0) ? (v_texcoord_persp.xy / v_texcoord_persp.z) : v_texcoord.xy;\n"              \
	    "\t\tvec4 color = (bilinearFilter > 0) ? bilinearTextureSample(sampleUv) : nearestTextureSample(sampleUv);\n"                         \
	    "		fragColor = dither(color * v_color);\n"                                                                                                    \
	    "		fragColor.a = (psxDrawMaskSet != 0 || (psxTextureOutputStp != 0 && sampledStp >= 0.5)) ? 1.0 : 0.0;\n"                                     \
	    "#ifdef NATIVE_DUAL_SOURCE_BLEND\n"                                                                                                          \
	    "		fragBlend = vec4(1.0);\n"                                                                                                                  \
	    "		float semiPixel = step(0.5, sampledStp);\n"                                                                                                \
	    "		if (psxSemiTransPass == 3) { fragBlend.a = mix(1.0, 0.5, semiPixel); }\n"                                                               \
	    "		else if (psxSemiTransPass == 4) { fragBlend.a = 1.0 - semiPixel; }\n"                                                                     \
	    "		else if (psxSemiTransPass == 5) { fragBlend.rgb = vec3(mix(1.0, 0.25, semiPixel)); fragBlend.a = 1.0 - semiPixel; }\n"               \
	    "#endif\n"                                                                                                                                    \
	    "	}\n"

global_variable const char *gpu_shader_common = "	varying vec4 v_texcoord;\n"
                                                "\tvarying vec3 v_texcoord_persp;\n"
                                                "	varying vec4 v_color;\n"
                                                "	varying vec4 v_page_clut;\n"
                                                "	varying vec2 v_ditherCoord;\n"
                                                "	varying float v_z;\n";

const char *gte_shader_4 = GPU_FRAGMENT_SAMPLE_SHADER(4);
const char *gte_shader_8 = GPU_FRAGMENT_SAMPLE_SHADER(8);
const char *gte_shader_16 = GPU_FRAGMENT_SAMPLE_SHADER(16);
const char *gte_shader_32_rgba = "	uniform sampler2D s_texture;\n"
                                 "	uniform int psxDrawMaskSet;\n"
                                 "	uniform vec2 texelSize;\n"
                                 "	void main() {\n"
                                 "\t\tvec2 sampleUv = (v_texcoord_persp.z > 0.0) ? (v_texcoord_persp.xy / v_texcoord_persp.z) : v_texcoord.xy;\n"
                                 "\t\tbool nativeObjUV = v_texcoord.w > 254.5;\n"
                                 "\t\tvec2 tc = nativeObjUV ? clamp(sampleUv * (1.0 / 255.0), 0.0, 1.0) : (sampleUv * texelSize + texelSize * 0.5);\n"
                                 "		vec4 color = texture2D(s_texture, tc);\n"
                                 "\t\tif (color.a <= 0.001) { discard; }\n"
                                 "		fragColor = color * v_color;\n"
                                 "		fragColor.a = float(psxDrawMaskSet);\n"
                                 "	}\n";

#define GTE_PERSPECTIVE_CORRECTION                                                                                 \
	"	gl_Position = Projection * vec4(a_position.xy + (a_extra.zw * (1.0 / 256.0)), 0.0, 1.0);\n"             \
	"		if (a_depth > 0.5) gl_Position.z = clamp((a_depth / 65535.0) * 2.0 - 1.0, -0.9999, 0.9999);\n"

#define GTE_VERTEX_SHADER                                                                                          \
	"	attribute vec4 a_position;\n"                                                                                \
	"	attribute vec4 a_texcoord; // uv, color multiplier, dither\n"                                                \
	"	attribute vec4 a_color;\n"                                                                                   \
	"\tattribute vec4 a_extra; // texcoord.xy ofs, subpixel X/Y\n"                                                     \
	"\tattribute float a_depth; // GTE SZ; zero keeps retail affine UV\n"                                             \
	"	uniform mat4 Projection;\n"                                                                                  \
	"	const vec2 c_UVFudge = vec2(0.00025, 0.00025);\n"                                                            \
	"	void main() {\n"                                                                                             \
	"		v_ditherCoord = a_position.xy;\n"                                                                           \
	"		v_texcoord = a_texcoord;\n"                                                                                 \
	"		float nativeObjUvFrac = step(254.5, a_texcoord.w);\n" \
	"		v_texcoord.xy += mix(a_extra.xy * 0.5, a_extra.xy * (1.0 / 256.0), nativeObjUvFrac);\n" \
	"\t\tfloat uvQ = (a_depth > 0.5) ? (4096.0 / a_depth) : 1.0;\n"                                      \
	"\t\tv_texcoord_persp = vec3(v_texcoord.xy * uvQ, uvQ);\n"                                             \
	"		v_color = a_color;\n"                                                                                       \
	"		v_color.xyz *= a_texcoord.z;\n"                                                                             \
	"		v_page_clut.x = fract(a_position.z / 16.0) * 1024.0;\n"                                                     \
	"		v_page_clut.y = floor(a_position.z / 16.0) * 256.0;\n"                                                      \
	"		v_page_clut.z = fract(a_position.w / 64.0);\n"                                                              \
	"		v_page_clut.w = floor(a_position.w / 64.0) / 512.0;\n"                                                      \
	"		v_page_clut.xy += c_UVFudge;\n"                                                                             \
	"		v_page_clut.zw += c_UVFudge;\n" GTE_PERSPECTIVE_CORRECTION "		v_z = (gl_Position.z - 40.0) * 0.005;\n" \
	"	}\n"

internal int NativeRenderer_Shader_CheckShaderStatus(GLuint shader)
{
	char info[1024];
	GLint result;

	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

	if (result == GL_TRUE)
	{
		return 1;
	}

	glGetShaderInfoLog(shader, sizeof(info), NULL, info);
	if (info[0] && strlen(info) > 8)
	{
		NATIVE_RENDERER_ERROR("%s\n", info);
		assert(0);
	}

	return 0;
}

internal int NativeRenderer_Shader_CheckProgramStatus(GLuint program)
{
	char info[1024];
	GLint result;

	glGetProgramiv(program, GL_LINK_STATUS, &result);

	if (result == GL_TRUE)
	{
		return 1;
	}

	glGetProgramInfoLog(program, sizeof(info), NULL, info);
	if (info[0] && strlen(info) > 8)
	{
		NATIVE_RENDERER_ERROR("%s\n", info);
		assert(0);
	}

	return 0;
}

internal ShaderID NativeRenderer_Shader_Compile(const char *source, bool isPsxShader)
{
	const char *GLSL_HEADER_VERT = "	#version 140\n"
	                               "	precision lowp  int;\n"
	                               "	precision highp float;\n"
	                               "	#define varying   out\n"
	                               "	#define attribute in\n"
	                               "	#define texture2D texture\n";

	const char *GLSL_HEADER_FRAG = "	#version 140\n"
	                               "	precision lowp  int;\n"
	                               "	precision highp float;\n"
	                               "	#define varying     in\n"
	                               "	#define texture2D   texture\n"
	                               "	out vec4 fragColor;\n";

	char extra_vs_defines[1024];
	char extra_fs_defines[1024];
	extra_vs_defines[0] = 0;
	extra_fs_defines[0] = 0;

	strcat(extra_vs_defines, "#define VERTEX\n");
	strcat(extra_fs_defines, "#define FRAGMENT\n");
	if (s_dualSourceBlendSupported)
	{
		strcat(extra_fs_defines, "#define NATIVE_DUAL_SOURCE_BLEND\n");
	}
	if (g_cfg_bilinearFiltering)
	{
		strcat(extra_fs_defines, "#define BILINEAR_FILTER\n");
	}

	const char *vs_list_psx[] = {GLSL_HEADER_VERT, extra_vs_defines, gpu_shader_common, GTE_VERTEX_SHADER};
	const char *fs_list_psx[] = {GLSL_HEADER_FRAG, extra_fs_defines, gpu_shader_common, GPU_DITHERING, source};
	const char *vs_list_src[] = {
	    GLSL_HEADER_VERT,
	    extra_vs_defines,
	    source,
	};
	const char *fs_list_src[] = {GLSL_HEADER_FRAG, extra_fs_defines, source};

	const char **vs_list = isPsxShader ? vs_list_psx : vs_list_src;
	const char **fs_list = isPsxShader ? fs_list_psx : fs_list_src;
	const int vs_list_cnt = isPsxShader ? 4 : 3;
	const int fs_list_cnt = isPsxShader ? 5 : 3;

	GLuint program = glCreateProgram();

	{
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, vs_list_cnt, vs_list, NULL);
		glCompileShader(vertexShader);

		if (NativeRenderer_Shader_CheckShaderStatus(vertexShader) == 0)
		{
			NATIVE_RENDERER_ERROR("Failed to compile Vertex Shader!\n");
		}

		glAttachShader(program, vertexShader);
		glDeleteShader(vertexShader);
	}

	{
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, fs_list_cnt, fs_list, NULL);
		glCompileShader(fragmentShader);

		if (NativeRenderer_Shader_CheckShaderStatus(fragmentShader) == 0)
		{
			NATIVE_RENDERER_ERROR("Failed to compile Fragment Shader!\n");
		}

		glAttachShader(program, fragmentShader);
		glDeleteShader(fragmentShader);
	}

	glBindAttribLocation(program, a_position, "a_position");
	glBindAttribLocation(program, a_texcoord, "a_texcoord");
	glBindAttribLocation(program, a_color, "a_color");
	glBindAttribLocation(program, a_extra, "a_extra");
	glBindAttribLocation(program, a_depth, "a_depth");

	if (isPsxShader && s_dualSourceBlendSupported && s_glBindFragDataLocationIndexed != NULL)
	{
		/* Both outputs target color attachment 0. Index 1 is the per-fragment
		 * dual-source blend factor; alpha in fragColor remains CTR's PS1 mask bit. */
		s_glBindFragDataLocationIndexed(program, 0, 0, "fragColor");
		s_glBindFragDataLocationIndexed(program, 0, 1, "fragBlend");
	}

	glLinkProgram(program);
	if (NativeRenderer_Shader_CheckProgramStatus(program) == 0)
	{
		NATIVE_RENDERER_ERROR("Failed to link Shader!\n");
	}

	GLint textureSampler = 0;
	GLint lutSampler = 1;
	glUseProgram(program);
	glUniform1iv(glGetUniformLocation(program, "s_texture"), 1, &textureSampler);
	glUniform1iv(glGetUniformLocation(program, "s_rgLut"), 1, &lutSampler);
	glUseProgram(0);

	return program;
}

//--------------------------------------------------------------------------------------------

internal void NativeRenderer_GenerateCommonTextures(void)
{
	u32 whitePixelData = 0xFFFFFFFF;

	glGenTextures(1, &s_whiteTexture);
	{
		glBindTexture(GL_TEXTURE_2D, s_whiteTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixelData);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glGenTextures(1, &s_rgLutTexture);
	{
		/* The RG lookup table is immutable and every PS1 shader samples it from
		 * texture unit 1. Bind it once for the whole renderer lifetime instead of
		 * rebinding it after every VRAM/override texture change. */
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, s_rgLutTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LUT_WIDTH, LUT_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, &rgLUT);

		glActiveTexture(GL_TEXTURE0);
	}
}

internal TextureID NativeRenderer_CreateRGBATexture(int width, int height, u8 *data)
{
	TextureID newTexture;
	glGenTextures(1, &newTexture);

	glBindTexture(GL_TEXTURE_2D, newTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_cfg_bilinearFiltering ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_cfg_bilinearFiltering ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	glBindTexture(GL_TEXTURE_2D, 0);

	return newTexture;
}

TextureID NativeRenderer_CreateImageTexture(const u8 *rgba, int width, int height)
{
	if ((rgba == NULL) || (width <= 0) || (height <= 0))
		return 0;

	TextureID texture = NativeRenderer_CreateRGBATexture(width, height, (u8 *)rgba);
	s_lastBoundTexture = (TextureID)-1;
	return texture;
}

void NativeRenderer_ReleaseImageTexture(TextureID texture)
{
	if (texture != 0)
		NativeRenderer_DestroyTexture(texture);

	s_lastBoundTexture = (TextureID)-1;
}

internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source)
{
	sh->shader = NativeRenderer_Shader_Compile(source, true);

	sh->bilinearFilterLoc = glGetUniformLocation(sh->shader, "bilinearFilter");
	sh->projectionLoc = glGetUniformLocation(sh->shader, "Projection");
	sh->texelSizeLoc = glGetUniformLocation(sh->shader, "texelSize");
	sh->texLoc = glGetUniformLocation(sh->shader, "s_texture");
	sh->lutLoc = glGetUniformLocation(sh->shader, "s_rgLut");
	sh->psxSemiTransPassLoc = glGetUniformLocation(sh->shader, "psxSemiTransPass");
	sh->psxDrawMaskSetLoc = glGetUniformLocation(sh->shader, "psxDrawMaskSet");
	sh->psxTextureOutputStpLoc = glGetUniformLocation(sh->shader, "psxTextureOutputStp");
}

internal void NativeRenderer_InitialisePSXShaders(void)
{
	NativeRenderer_CompilePSXShader(&s_gteShader4, gte_shader_4);
	NativeRenderer_CompilePSXShader(&s_gteShader8, gte_shader_8);
	NativeRenderer_CompilePSXShader(&s_gteShader16, gte_shader_16);
	NativeRenderer_CompilePSXShader(&s_gteShader32Rgba, gte_shader_32_rgba);
}

internal void NativeRenderer_InitRG8LUT(void)
{
	for (u16 y = 0; y < LUT_HEIGHT; y++)
	{
		u8 *row = rgLUT + y * (LUT_WIDTH * 4);
		for (u16 x = 0; x < LUT_WIDTH; x++)
		{
			const u16 c = (y << 8) | x;
			u8 *pixel = row + x * 4;
			pixel[0] = (u8)((c & 31) << 3);
			pixel[1] = (u8)(((c >> 5) & 31) << 3);
			pixel[2] = (u8)(((c >> 10) & 31) << 3);
			pixel[3] = (u8)(((c >> 15) & 1) << 7);
		}
	}
}

int NativeRenderer_InitialisePSX(void)
{
	SDL_memset(vram, 0, VRAM_WIDTH * VRAM_HEIGHT * sizeof(u16));
	NativeRenderer_InitRG8LUT();
	NativeRenderer_GenerateCommonTextures();
	NativeRenderer_InitialisePSXShaders();

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_STENCIL_TEST);
	glBlendColor(0.5f, 0.5f, 0.5f, 0.25f);

	// gen framebuffer
	{
		// make a special texture
		// it will be resized later
		glGenTextures(1, &s_framebufferTexture);
		{
			glBindTexture(GL_TEXTURE_2D, s_framebufferTexture);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			// default to VRAM size
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VRAM_WIDTH, VRAM_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

			glBindTexture(GL_TEXTURE_2D, 0);
		}

		glGenFramebuffers(1, &s_glBlitFramebuffer);
		{
			glBindFramebuffer(GL_FRAMEBUFFER, s_glBlitFramebuffer);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_framebufferTexture, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	// gen offscreen RT
	{
		// offscreen texture render target
		glGenTextures(1, &s_offscreenRenderTexture);
		{
			glBindTexture(GL_TEXTURE_2D, s_offscreenRenderTexture);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			// default to VRAM size
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VRAM_WIDTH, VRAM_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

			glBindTexture(GL_TEXTURE_2D, 0);
		}

		glGenFramebuffers(1, &s_glOffscreenFramebuffer);
		{
			glBindFramebuffer(GL_FRAMEBUFFER, s_glOffscreenFramebuffer);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_offscreenRenderTexture, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	// gen VRAM textures.
	// double-buffered
	{
		int i;

		glGenTextures(2, s_vramTexturesDouble);

		for (i = 0; i < 2; i++)
		{
			glBindTexture(GL_TEXTURE_2D, s_vramTexturesDouble[i]);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			// set storage size
			glTexImage2D(GL_TEXTURE_2D, 0, NativeRenderer_GetVRAMInternalFormat(), VRAM_WIDTH, VRAM_HEIGHT, 0, VRAM_FORMAT, GL_UNSIGNED_BYTE, NULL);
		}

		s_vramTexture = s_vramTexturesDouble[0];
		NativeRenderer_ResetVRAMDirtyState();

		glBindTexture(GL_TEXTURE_2D, 0);

		// VRAM framebuffer for offscreen blitting to VRAM
		glGenFramebuffers(1, &s_glVramFramebuffer);
		{
			glBindFramebuffer(GL_FRAMEBUFFER, s_glVramFramebuffer);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_vramTexture, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	// gen vertex buffer and index buffer
	{
		int i;

		glGenBuffers(MAX_NUM_VERTEX_BUFFERS, s_glVertexBuffer);
		glGenVertexArrays(MAX_NUM_VERTEX_BUFFERS, s_glVertexArray);

		for (i = 0; i < MAX_NUM_VERTEX_BUFFERS; i++)
		{
			glBindVertexArray(s_glVertexArray[i]);

			glBindBuffer(GL_ARRAY_BUFFER, s_glVertexBuffer[i]);
			glBufferData(GL_ARRAY_BUFFER, sizeof(GrVertex) * MAX_VERTEX_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);

			/* Attribute layout is VAO state. Configure it once instead of repeating
			 * five enables + five glVertexAttribPointer calls for every upload. */
			glEnableVertexAttribArray(a_position);
			glEnableVertexAttribArray(a_texcoord);
			glEnableVertexAttribArray(a_color);
			glEnableVertexAttribArray(a_extra);
			glEnableVertexAttribArray(a_depth);
			glVertexAttribPointer(a_position, 4, GL_SHORT, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->x);
			glVertexAttribPointer(a_texcoord, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->u);
			glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GrVertex), &((GrVertex *)NULL)->r);
			glVertexAttribPointer(a_extra, 4, GL_BYTE, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->tcx);
			glVertexAttribPointer(a_depth, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->depth);
		}

		glBindVertexArray(0);
	}

	NativeRenderer_ResetDevice();

	return 1;
}

internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar)
{
	if (s_projectionValid &&
	    (s_projectionLeft == left) &&
	    (s_projectionRight == right) &&
	    (s_projectionBottom == bottom) &&
	    (s_projectionTop == top) &&
	    (s_projectionNear == znear) &&
	    (s_projectionFar == zfar))
	{
		return;
	}

	{
		const float a = 2.0f / (right - left);
		const float b = 2.0f / (top - bottom);
		const float c = 2.0f / (znear - zfar);
		const float x = (left + right) / (left - right);
		const float y = (bottom + top) / (bottom - top);
		const float z = (znear + zfar) / (znear - zfar);
		const float ortho[16] = {
			a, 0, 0, 0,
			0, b, 0, 0,
			0, 0, c, 0,
			x, y, z, 1
		};

		SDL_memcpy(s_projectionMatrix, ortho, sizeof(ortho));
	}

	s_projectionLeft = left;
	s_projectionRight = right;
	s_projectionBottom = bottom;
	s_projectionTop = top;
	s_projectionNear = znear;
	s_projectionFar = zfar;
	s_projectionValid = 1;
	s_projectionGeneration++;
	if (s_projectionGeneration == 0)
		s_projectionGeneration = 1;
}

internal void NativeRenderer_ApplyProjection(GTEShader *shader)
{
	if ((shader == NULL) ||
	    !s_projectionValid ||
	    (shader->projectionLoc < 0) ||
	    (shader->projectionGeneration == s_projectionGeneration))
	{
		return;
	}

	glUniformMatrix4fv(shader->projectionLoc, 1, GL_FALSE, s_projectionMatrix);
	shader->projectionGeneration = s_projectionGeneration;
}

void NativeRenderer_SetupClipMode(const RECT16 *rect, const DISPENV *displayEnv, int enable)
{
	if ((displayEnv->disp.w <= 0) || (displayEnv->disp.h <= 0))
	{
		NativeRenderer_SetScissorState(0);
		return;
	}

	if ((rect->w <= 0) || (rect->h <= 0))
	{
		// NOTE(aalhendi): Retail draw-area commands define inclusive corners.
		// Collapsed areas clip all pixels; GL scissor rejects negative sizes.
		NativeRenderer_SetScissorState(enable != 0);
		if (enable)
		{
			glScissor(0, 0, 0, 0);
		}
		return;
	}

	// [A] isinterlaced dirty hack for widescreen
	const bool scissorOn = enable && (displayEnv->isinter || (rect->x - displayEnv->disp.x > 0 || rect->y - displayEnv->disp.y > 0 ||
	                                                          rect->w < displayEnv->disp.w || rect->h < displayEnv->disp.h));

	NativeRenderer_SetScissorState(scissorOn);

	if (!scissorOn)
	{
		return;
	}

	const float emuScreenAspect = 1.0f;

	const float psxScreenWInv = 1.0f / (float)displayEnv->disp.w;
	const float psxScreenHInv = 1.0f / (float)displayEnv->disp.h;

	// first map to 0..1
	float clipRectX = (float)(rect->x - displayEnv->disp.x) * psxScreenWInv;
	float clipRectY = (float)(rect->y - displayEnv->disp.y) * psxScreenHInv;
	float clipRectW = (float)(rect->w) * psxScreenWInv;
	float clipRectH = (float)(rect->h) * psxScreenHInv;

	// then map to screen
	{
		clipRectX -= 0.5f;

		clipRectX *= emuScreenAspect;
		clipRectW *= emuScreenAspect;

		clipRectX += 0.5f;
	}

	// adjust scissor rectangle by the backbuffer size (window dimensions)
	const float viewportX = (float)s_presentViewport.x;
	const float viewportY = (float)s_presentViewport.y;
	const float viewportW = (float)s_presentViewport.w;
	const float viewportH = (float)s_presentViewport.h;
	const float flipOffset = viewportY + viewportH - clipRectH * viewportH;
	const float crx = viewportX + clipRectX * viewportW;
	const float cry = clipRectY * viewportH;
	const float crw = clipRectW * viewportW;
	const float crh = clipRectH * viewportH;

	glScissor(crx, flipOffset - cry, crw, crh);
}

internal void NativeRenderer_SetShader(const ShaderID shader)
{
	if (s_previousShader != shader)
	{
		glUseProgram(shader);
		NativePerf_AddShaderSwitch();

		s_previousShader = shader;
		/* Uniform locations are program-local. Force one refresh after a real
		 * shader switch, then suppress identical glUniform calls on later splits. */
		s_cachedBilinearFilter = -1;
		s_cachedPSXSemiTransPass = -1;
		s_cachedPSXDrawMaskSet = -1;
		s_cachedPSXTextureOutputStp = -1;
		s_cachedOverrideTextureWidth = -1;
		s_cachedOverrideTextureHeight = -1;
	}
}


void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat)
{
	GLint texLoc = -1;
	GLint lutLoc = -1;
	GTEShader *selectedShader = NULL;

	switch (texFormat)
	{
	case TF_4_BIT:
		selectedShader = &s_gteShader4;
		NativeRenderer_SetShader(s_gteShader4.shader);
		u_bilinearFilterLoc = s_gteShader4.bilinearFilterLoc;
		u_projectionLoc = s_gteShader4.projectionLoc;
		texLoc = s_gteShader4.texLoc;
		lutLoc = s_gteShader4.lutLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader4.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader4.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader4.psxTextureOutputStpLoc;
		break;
	case TF_8_BIT:
		selectedShader = &s_gteShader8;
		NativeRenderer_SetShader(s_gteShader8.shader);
		u_bilinearFilterLoc = s_gteShader8.bilinearFilterLoc;
		u_projectionLoc = s_gteShader8.projectionLoc;
		texLoc = s_gteShader8.texLoc;
		lutLoc = s_gteShader8.lutLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader8.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader8.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader8.psxTextureOutputStpLoc;
		break;
	case TF_16_BIT:
		selectedShader = &s_gteShader16;
		NativeRenderer_SetShader(s_gteShader16.shader);
		u_bilinearFilterLoc = s_gteShader16.bilinearFilterLoc;
		u_projectionLoc = s_gteShader16.projectionLoc;
		texLoc = s_gteShader16.texLoc;
		lutLoc = s_gteShader16.lutLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader16.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader16.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader16.psxTextureOutputStpLoc;
		break;
	case TF_32_BIT_RGBA:
		selectedShader = &s_gteShader32Rgba;
		NativeRenderer_SetShader(s_gteShader32Rgba.shader);
		u_bilinearFilterLoc = s_gteShader32Rgba.bilinearFilterLoc;
		u_projectionLoc = s_gteShader32Rgba.projectionLoc;
		texLoc = s_gteShader32Rgba.texLoc;
		lutLoc = s_gteShader32Rgba.lutLoc;
		u_texelSizeLoc = s_gteShader32Rgba.texelSizeLoc;
		u_psxSemiTransPassLoc = s_gteShader32Rgba.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader32Rgba.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader32Rgba.psxTextureOutputStpLoc;
		break;
	}

	NativeRenderer_ApplyProjection(selectedShader);

	if (g_dbg_texturelessMode)
	{
		texture = s_whiteTexture;
	}

	/* Texture/LUT sampler units are initialized once when each shader program
	 * is linked. Re-sending them for every PS1 split only burns driver time. */
	(void)texLoc;
	(void)lutLoc;
	if ((u_bilinearFilterLoc >= 0) && (s_cachedBilinearFilter != g_cfg_bilinearFiltering))
	{
		glUniform1i(u_bilinearFilterLoc, g_cfg_bilinearFiltering);
		s_cachedBilinearFilter = g_cfg_bilinearFiltering;
	}
	NativeRenderer_SetPSXTextureSemiTransPass(0);

	if (s_lastBoundTexture == texture)
	{
		return;
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	NativePerf_AddTextureBind();

	/* Unit 1 keeps the immutable RG LUT bound permanently. */
	s_lastBoundTexture = texture;
}

void NativeRenderer_SetOverrideTextureSize(int width, int height)
{
	if (u_texelSizeLoc == -1 || width <= 0 || height <= 0)
	{
		return;
	}
	if ((s_cachedOverrideTextureWidth == width) && (s_cachedOverrideTextureHeight == height))
	{
		return;
	}

	float vec[] = {1.0f / (float)width, 1.0f / (float)height};
	glUniform2fv(u_texelSizeLoc, 1, vec);
	s_cachedOverrideTextureWidth = width;
	s_cachedOverrideTextureHeight = height;
}

void NativeRenderer_SetPSXTextureSemiTransPass(int pass)
{
	if ((u_psxSemiTransPassLoc >= 0) && (s_cachedPSXSemiTransPass != pass))
	{
		glUniform1i(u_psxSemiTransPassLoc, pass);
		s_cachedPSXSemiTransPass = pass;
	}
}

void NativeRenderer_SetPSXTextureOutputSTP(int enabled)
{
	enabled = enabled != 0;
	if ((u_psxTextureOutputStpLoc >= 0) && (s_cachedPSXTextureOutputStp != enabled))
	{
		glUniform1i(u_psxTextureOutputStpLoc, enabled);
		s_cachedPSXTextureOutputStp = enabled;
	}
}

void NativeRenderer_SetPSXDrawMaskSet(int maskSet)
{
	maskSet = maskSet != 0;
	if ((u_psxDrawMaskSetLoc >= 0) && (s_cachedPSXDrawMaskSet != maskSet))
	{
		glUniform1i(u_psxDrawMaskSetLoc, maskSet);
		s_cachedPSXDrawMaskSet = maskSet;
	}
}

internal void NativeRenderer_DestroyTexture(TextureID texture)
{
	if (texture == (TextureID)-1)
	{
		return;
	}

	glDeleteTextures(1, &texture);
}

internal u16 NativeRenderer_PackRGB24ToPSX15(u8 r, u8 g, u8 b)
{
	return (u16)(((r >> 3) & 0x1f) | (((g >> 3) & 0x1f) << 5) | (((b >> 3) & 0x1f) << 10));
}

internal float NativeRenderer_PSXColorComponentFloat(u8 value)
{
	const u8 psx8 = (u8)((value >> 3) << 3);
	return (float)psx8 / 255.0f;
}

void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	u16 *dst = vram + x + y * VRAM_WIDTH;
	const u16 color = NativeRenderer_PackRGB24ToPSX15(r, g, b);

	if (x + w > VRAM_WIDTH)
	{
		w = VRAM_WIDTH - x;
	}

	if (y + h > VRAM_HEIGHT)
	{
		h = VRAM_HEIGHT - y;
	}

	if ((w > 0) && (h > 0))
	{
		NativeRenderer_MarkVRAMDirtyRect(x, y, w, h);
		NativePerf_AddVRAMWritePixels((u32)(w * h));
	}

	// clear VRAM region with given color
	for (int i = 0; i < h; i++)
	{
		u16 *tmp = dst;

		for (int j = 0; j < w; j++)
		{
			*tmp++ = color;
		}

		dst += VRAM_WIDTH;
	}
}

void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	if ((w <= 0) || (h <= 0) || (g_windowWidth <= 0) || (g_windowHeight <= 0))
	{
		return;
	}

	int displayX = activeDispEnv.disp.x;
	int displayY = activeDispEnv.disp.y;
	int displayW = activeDispEnv.disp.w;
	int displayH = activeDispEnv.disp.h;

	if ((displayW <= 0) || (displayH <= 0))
	{
		displayX = activeDrawEnv.clip.x;
		displayY = activeDrawEnv.clip.y;
		displayW = activeDrawEnv.clip.w;
		displayH = activeDrawEnv.clip.h;
	}

	if ((displayW <= 0) || (displayH <= 0))
	{
		return;
	}

	const int clearRight = x + w;
	const int clearBottom = y + h;
	const int displayRight = displayX + displayW;
	const int displayBottom = displayY + displayH;

	const int overlapX = x > displayX ? x : displayX;
	const int overlapY = y > displayY ? y : displayY;
	const int overlapRight = clearRight < displayRight ? clearRight : displayRight;
	const int overlapBottom = clearBottom < displayBottom ? clearBottom : displayBottom;

	if ((overlapRight <= overlapX) || (overlapBottom <= overlapY))
	{
		return;
	}

	const int relX = overlapX - displayX;
	const int relY = overlapY - displayY;
	const int relRight = overlapRight - displayX;
	const int relBottom = overlapBottom - displayY;

	const int scissorX = s_presentViewport.x + (relX * s_presentViewport.w) / displayW;
	const int scissorRight = s_presentViewport.x + (relRight * s_presentViewport.w + displayW - 1) / displayW;
	const int scissorTop = (relY * s_presentViewport.h) / displayH;
	const int scissorBottom = (relBottom * s_presentViewport.h + displayH - 1) / displayH;
	const int scissorW = scissorRight - scissorX;
	const int scissorH = scissorBottom - scissorTop;

	if ((scissorW <= 0) || (scissorH <= 0))
	{
		return;
	}

	GLint previousScissorBox[4];
	const GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);

	s_framebufferNeedsUpdate = 1;

	glEnable(GL_SCISSOR_TEST);
	glScissor(scissorX, s_presentViewport.y + s_presentViewport.h - scissorBottom, scissorW, scissorH);
	glClearColor(NativeRenderer_PSXColorComponentFloat(r), NativeRenderer_PSXColorComponentFloat(g), NativeRenderer_PSXColorComponentFloat(b), 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	if (previousScissorEnabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}

	s_previousScissorState = previousScissorEnabled ? 1 : 0;
}

void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int bReadFromFrameBuffer)
{
#define FLIP_Y (VRAM_HEIGHT - i - 1)

	(void)x;
	(void)y;
	(void)bReadFromFrameBuffer;

	FILE *fp = fopen(outputFileName, "wb");
	if (fp == NULL)
	{
		return;
	}

	u8 TGAheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 header[6];
	header[0] = (width % 256);
	header[1] = (width / 256);
	header[2] = (height % 256);
	header[3] = (height / 256);
	header[4] = 16;
	header[5] = 0;

	fwrite(TGAheader, sizeof(u8), 12, fp);
	fwrite(header, sizeof(u8), 6, fp);

	for (int i = 0; i < VRAM_HEIGHT; i++)
	{
		fwrite(vram + VRAM_WIDTH * FLIP_Y, sizeof(u16), VRAM_WIDTH, fp);
	}

	fclose(fp);

#undef FLIP_Y
}

internal void NativeRenderer_CopyRGBAFramebufferToVRAM(u32 *src, int x, int y, int w, int h, int update_vram, int flip_y)
{
	assert(x >= 0);
	assert(y >= 0);
	assert(x + w <= VRAM_WIDTH);
	assert(y + h <= VRAM_HEIGHT);

	u16 *fb = (u16 *)malloc(w * h * sizeof(u16));
	u32 *data_src = (u32 *)src;
	u16 *data_dst = (u16 *)fb;

	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			u32 c = *data_src++;

			u8 r = ((c >> 3) & 0x1F);
			u8 g = ((c >> 11) & 0x1F);
			u8 b = ((c >> 19) & 0x1F);
			// NOTE(aalhendi): framebuffer alpha carries the PS1 E6 mask/STP bit.
			u8 a = (c >> 31) & 1;

			*data_dst++ = r | (g << 5) | (b << 10) | (a << 15);
		}
	}

	u16 *ptr = (u16 *)vram + VRAM_WIDTH * y + x;

	for (int fy = 0; fy < h; fy++)
	{
		int py = flip_y ? (h - fy - 1) : fy;
		u16 *fb_ptr = fb + (h * py / h) * w;

		for (int fx = 0; fx < w; fx++)
		{
			ptr[fx] = fb_ptr[w * fx / w];
		}

		ptr += VRAM_WIDTH;
	}

	free(fb);

	if ((w > 0) && (h > 0))
	{
		NativePerf_AddVRAMWritePixels((u32)(w * h));
	}

	if (update_vram)
	{
		NativeRenderer_MarkVRAMDirtyRect(x, y, w, h);
	}
}

void NativeRenderer_ReadFramebufferDataToVRAM(void)
{
	int x, y, w, h;
	if (!s_framebufferNeedsUpdate)
	{
		return;
	}

	s_framebufferNeedsUpdate = 0;

	x = s_previousFramebuffer.x;
	y = s_previousFramebuffer.y;
	w = s_previousFramebuffer.w;
	h = s_previousFramebuffer.h;

	if (w <= 0 || h <= 0)
	{
		return;
	}

	u32 *pixels = NativeRenderer_GetFramebufferReadbackBuffer((size_t)w * (size_t)h);
	if (pixels != NULL)
	{
		glBindTexture(GL_TEXTURE_2D, s_framebufferTexture);
		// NOTE(aalhendi): DrawSync can be called immediately before screen-copy
		// effects sample PS1 VRAM. A delayed readback replays an older frame
		// into VRAM and turns clock/idle blur into flicker; use the latest
		// framebuffer texture here.
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glBindTexture(GL_TEXTURE_2D, 0);
		// NOTE(aalhendi): Keep the CPU-side VRAM mirror packed like PS1 VRAM.
		// Host texture bindings are invalid after this direct GL texture read.
		NativeRenderer_CopyRGBAFramebufferToVRAM(pixels, x, y, w, h, 1, 0);
		s_lastBoundTexture = (TextureID)-1;
	}
}

void NativeRenderer_DiscardFramebufferReadback(void)
{
	s_framebufferNeedsUpdate = 0;
}

internal int NativeRenderer_RectEquals(const RECT16 *a, const RECT16 *b)
{
	return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

internal void NativeRenderer_FlushOffscreenToVRAM(void)
{
	if (s_previousOffscreen.w <= 0 || s_previousOffscreen.h <= 0)
	{
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, s_glVramFramebuffer);

	// rebind texture
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_vramTexture, 0);

	// setup draw and read framebuffers
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_glOffscreenFramebuffer); // source is offscreen render target
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_glVramFramebuffer);

	glBlitFramebuffer(0, 0, s_previousOffscreen.w, s_previousOffscreen.h, s_previousOffscreen.x, s_previousOffscreen.y + s_previousOffscreen.h,
	                  s_previousOffscreen.x + s_previousOffscreen.w, s_previousOffscreen.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	// done, unbind
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// copy rendering results to the CPU-side PSX VRAM mirror
	{
		u32 *pixels = NativeRenderer_GetFramebufferReadbackBuffer(
			(size_t)s_previousOffscreen.w * (size_t)s_previousOffscreen.h);
		if (pixels == NULL)
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, s_offscreenRenderTexture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glBindTexture(GL_TEXTURE_2D, s_lastBoundTexture != (TextureID)-1 ? s_lastBoundTexture : 0);

		NativeRenderer_CopyRGBAFramebufferToVRAM(pixels, s_previousOffscreen.x, s_previousOffscreen.y, s_previousOffscreen.w, s_previousOffscreen.h, 0, 1);
		// The active VRAM texture already received this region through the GPU
		// blit above. Keep only the inactive texture pending so the next swap
		// can catch up without re-uploading the whole VRAM image.
		NativeRenderer_MarkOtherVRAMTextureDirtyRect(s_previousOffscreen.x, s_previousOffscreen.y,
		                                            s_previousOffscreen.w, s_previousOffscreen.h);
	}
}

internal void NativeRenderer_SetScissorState(int enable)
{
	if (s_previousScissorState == enable)
	{
		return;
	}

	if (s_previousScissorState)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	else
	{
		glEnable(GL_SCISSOR_TEST);
	}
	s_previousScissorState = enable;
}

void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, const DISPENV *displayEnv, int enable)
{
	const int sameOffscreenRect = NativeRenderer_RectEquals(&s_previousOffscreen, offscreenRect);

	if (enable)
	{
		// setup render target viewport
		NativeRenderer_Ortho2D(0, offscreenRect->w, offscreenRect->h, 0, -1.0f, 1.0f);
	}
	else
	{
		// setup default viewport
		const int displayW = displayEnv->disp.w > 0 ? displayEnv->disp.w : 1;
		const int displayH = displayEnv->disp.h > 0 ? displayEnv->disp.h : 1;
		NativeRenderer_Ortho2D(0, displayW, displayH, 0, -1.0f, 1.0f);
	}

	if (enable && s_previousOffscreenState && sameOffscreenRect)
	{
		return;
	}

	if (enable)
	{
		if (s_previousOffscreenState)
		{
			NativeRenderer_FlushOffscreenToVRAM();
		}

		s_previousOffscreenState = 1;

		// set storage size first
		if (s_previousOffscreen.w != offscreenRect->w || s_previousOffscreen.h != offscreenRect->h)
		{
			glBindTexture(GL_TEXTURE_2D, s_offscreenRenderTexture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, offscreenRect->w, offscreenRect->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		s_previousOffscreen = *offscreenRect;

		NativeRenderer_SetViewPort(0, 0, offscreenRect->w, offscreenRect->h);
		glBindFramebuffer(GL_FRAMEBUFFER, s_glOffscreenFramebuffer);

		// clear it out
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	else
	{
		if (!s_previousOffscreenState)
		{
			return;
		}

		s_previousOffscreenState = 0;

		NativeRenderer_FlushOffscreenToVRAM();
		NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);
	}
}

void NativeRenderer_StoreFrameBufferDeferred(int x, int y, int w, int h)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
	if (s_previousFramebuffer.w != w || s_previousFramebuffer.h != h)
	{
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_RESIZE);
		glBindTexture(GL_TEXTURE_2D, s_framebufferTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_RESIZE);
	}

	s_previousFramebuffer.x = x;
	s_previousFramebuffer.y = y;
	s_previousFramebuffer.w = w;
	s_previousFramebuffer.h = h;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_glBlitFramebuffer);
	glBlitFramebuffer(s_presentViewport.x, s_presentViewport.y,
	                  s_presentViewport.x + s_presentViewport.w,
	                  s_presentViewport.y + s_presentViewport.h,
	                  x, y + h, x + w, y,
	                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// End-of-frame presentation does not need an immediate CPU readback.
	// Keep the latest completed frame in the GPU texture and materialize it
	// into the PS1 VRAM mirror only if a later operation actually needs it.
	s_framebufferNeedsUpdate = 1;
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
}

void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
	// set storage size first
	if (s_previousFramebuffer.w != w || s_previousFramebuffer.h != h)
	{
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_RESIZE);
		glBindTexture(GL_TEXTURE_2D, s_framebufferTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_RESIZE);
	}

	s_previousFramebuffer.x = x;
	s_previousFramebuffer.y = y;
	s_previousFramebuffer.w = w;
	s_previousFramebuffer.h = h;

	glBindFramebuffer(GL_FRAMEBUFFER, s_glBlitFramebuffer);

	// before drawing set source and target
	{
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_BLIT);
		// setup draw and read framebuffers
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); // source is backbuffer
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_glBlitFramebuffer);

		glBlitFramebuffer(s_presentViewport.x, s_presentViewport.y, s_presentViewport.x + s_presentViewport.w, s_presentViewport.y + s_presentViewport.h, x,
		                  y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		// done, unbind
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_BLIT);
	}

	// after drawing
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_FLUSH);
	glFlush();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_FLUSH);

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_ALLOC);
	u32 *pixels = NativeRenderer_GetFramebufferReadbackBuffer((size_t)w * (size_t)h);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_ALLOC);
	if (pixels != NULL)
	{
		// NOTE(aalhendi): Screen-feedback effects sample PS1 VRAM as packed
		// 16-bit pixels. Do not leave the VRAM texture in host RGBA form here;
		// pack the copied framebuffer synchronously before the next primitive
		// can sample it.
		glBindTexture(GL_TEXTURE_2D, s_framebufferTexture);
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
		glBindTexture(GL_TEXTURE_2D, 0);

		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_PACK);
		NativeRenderer_CopyRGBAFramebufferToVRAM(pixels, x, y, w, h, 1, 0);
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_PACK);
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_VRAM_UPLOAD);
		NativeRenderer_UpdateVRAM();
		NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_VRAM_UPLOAD);
		s_lastBoundTexture = -1;
	}
	s_framebufferNeedsUpdate = 0;
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
}

void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dst_x, int dst_y)
{
	int stride = w;

	if (!src)
	{
		// NOTE(aalhendi): MoveImage copies from PS1 VRAM. If native has a pending
		// host framebuffer readback, pull it into the CPU VRAM mirror before the
		// copy. Do not schedule a later readback here; boot TIM display moves are
		// immediately followed by DrawSync, and a deferred black backbuffer read can
		// wipe the copied splash page.
		NativeRenderer_ReadFramebufferDataToVRAM();
		src = vram;
		stride = VRAM_WIDTH;
	}

	src += x + y * stride;

	u16 *dst = vram + dst_x + dst_y * VRAM_WIDTH;

	if ((w > 0) && (h > 0))
	{
		NativeRenderer_MarkVRAMDirtyRect(dst_x, dst_y, w, h);
		NativePerf_AddVRAMWritePixels((u32)(w * h));
	}

	for (int i = 0; i < h; i++)
	{
		SDL_memcpy(dst, src, w * sizeof(u16));
		dst += VRAM_WIDTH;
		src += stride;
	}
}

void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dst_w, int dst_h)
{
	/* CPU-side VRAM reads are the real synchronization point. Keep DrawSync
	   cheap during normal rendering and materialize the latest framebuffer only
	   when code actually asks to read VRAM (StoreImage, captures, etc.). */
	NativeRenderer_ReadFramebufferDataToVRAM();

	u16 *src = vram + x + VRAM_WIDTH * y;

	for (int i = 0; i < dst_h; i++)
	{
		SDL_memcpy(dst, src, dst_w * sizeof(u16));
		dst += dst_w;
		src += VRAM_WIDTH;
	}
}

int NativeRenderer_GetVRAMStateSize(void)
{
	return (int)sizeof(vram);
}

int NativeRenderer_CaptureVRAMState(void *dst, int dstSize)
{
	if ((dst == NULL) || (dstSize < (int)sizeof(vram)))
	{
		return 0;
	}

	// NOTE(aalhendi): Save-states own the CPU-side PSX VRAM mirror, not GL
	// textures. Pull pending framebuffer copies into the mirror first.
	NativeRenderer_ReadFramebufferDataToVRAM();
	SDL_memcpy(dst, vram, sizeof(vram));
	return 1;
}

int NativeRenderer_RestoreVRAMState(const void *src, int srcSize)
{
	local_persist const RECT16 zeroRect = {0, 0, 0, 0};

	if ((src == NULL) || (srcSize < (int)sizeof(vram)))
	{
		return 0;
	}

	SDL_memcpy(vram, src, sizeof(vram));
	NativePerf_AddVRAMWritePixels((u32)(VRAM_WIDTH * VRAM_HEIGHT));
	// NOTE(aalhendi): Restored VRAM is authoritative PSX state. Host GL caches
	// are rebuildable, so mark both backing textures dirty and drop stale bindings.
	NativeRenderer_MarkVRAMDirtyRect(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	s_framebufferNeedsUpdate = 0;
	s_previousFramebuffer = zeroRect;
	s_previousOffscreen = zeroRect;
	s_previousOffscreenState = 0;
	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
	return 1;
}

void NativeRenderer_UpdateVRAM(void)
{
	int uploadIndex;
	int x;
	int y;
	int w;
	int h;
	const u16 *src;
	struct NativeVRAMDirtyRect *dirty;

	if (!s_vramNeedsUpdate)
	{
		return;
	}

	uploadIndex = s_vramTextureIndex;
	dirty = &s_vramDirty[uploadIndex];

	if (!dirty->valid)
	{
		s_vramNeedsUpdate = 0;
		return;
	}

	x = dirty->x0;
	y = dirty->y0;
	w = dirty->x1 - dirty->x0;
	h = dirty->y1 - dirty->y0;
	src = vram + x + y * VRAM_WIDTH;

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);

	s_vramTexture = s_vramTexturesDouble[uploadIndex];
	s_vramTextureIndex = (uploadIndex + 1) & 1;

	glBindTexture(GL_TEXTURE_2D, s_vramTexture);

	// The textures are allocated once during renderer initialization. Update only
	// the dirty PS1 VRAM rectangle instead of reallocating/re-uploading 1024x512
	// on every change. GL_UNPACK_ROW_LENGTH lets the sub-rectangle point directly
	// into the CPU-side VRAM mirror without allocating a temporary copy.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, VRAM_WIDTH);
	NativePerf_AddVRAMUpload((u32)(w * h * (int)sizeof(u16)));
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, VRAM_FORMAT, GL_UNSIGNED_BYTE, src);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	dirty->valid = 0;
	s_vramNeedsUpdate = 0;

	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);
}

internal u8 NativeRenderer_Expand5To8(u16 value)
{
	value &= 0x1f;
	return (u8)((value << 3) | (value >> 2));
}

internal void NativeRenderer_BuildDisplayChunkRGBA(u8 *dst, int srcX, int srcY, int w, int h)
{
	for (int y = 0; y < h; y++)
	{
		u16 *src = vram + srcX + (srcY + y) * VRAM_WIDTH;

		for (int x = 0; x < w; x++)
		{
			u16 pixel = src[x];

			*dst++ = NativeRenderer_Expand5To8(pixel);
			*dst++ = NativeRenderer_Expand5To8(pixel >> 5);
			*dst++ = NativeRenderer_Expand5To8(pixel >> 10);
			*dst++ = 0xff;
		}
	}
}

internal void NativeRenderer_FillDisplayChunkVerts(GrVertex *verts, int dstX, int dstY, int w, int h)
{
	const int maxU = w > 0 ? w - 1 : 0;
	const int maxV = h > 0 ? h - 1 : 0;

	memset(verts, 0, sizeof(GrVertex) * 6);

	verts[0].x = dstX;
	verts[0].y = dstY;
	verts[0].u = 0;
	verts[0].v = 0;

	verts[1].x = dstX;
	verts[1].y = dstY + h;
	verts[1].u = 0;
	verts[1].v = maxV;

	verts[2].x = dstX + w;
	verts[2].y = dstY + h;
	verts[2].u = maxU;
	verts[2].v = maxV;

	verts[3] = verts[0];
	verts[4] = verts[2];

	verts[5].x = dstX + w;
	verts[5].y = dstY;
	verts[5].u = maxU;
	verts[5].v = 0;

	for (int i = 0; i < 6; i++)
	{
		verts[i].bright = 1;
		verts[i].r = 0xff;
		verts[i].g = 0xff;
		verts[i].b = 0xff;
		verts[i].a = 0xff;
	}
}

void NativeRenderer_PresentVRAMRect(int displayX, int displayY, int displayW, int displayH)
{
	const int maxChunkW = 0x100;
	const int maxChunkH = 0x100;
	const int maxChunkBytes = maxChunkW * maxChunkH * 4;

	local_persist TextureID displayTexture = (TextureID)-1;
	local_persist u8 *rgba = NULL;

	if (displayW <= 0 || displayH <= 0)
	{
		return;
	}

	if (rgba == NULL)
	{
		rgba = (u8 *)malloc(maxChunkBytes);
	}

	if (rgba == NULL)
	{
		return;
	}

	if (displayTexture == (TextureID)-1)
	{
		displayTexture = NativeRenderer_CreateRGBATexture(maxChunkW, maxChunkH, NULL);
	}

	NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_EnableDepth(0);
	NativeRenderer_SetBlendMode(BM_NONE);
	NativeRenderer_Ortho2D(0, displayW, displayH, 0, -1.0f, 1.0f);
	NativeRenderer_SetTexture(displayTexture, TF_32_BIT_RGBA);
	NativeRenderer_SetPSXDrawMaskSet(0);

	for (int y = 0; y < displayH; y += maxChunkH)
	{
		const int chunkH = (displayH - y) > maxChunkH ? maxChunkH : displayH - y;

		for (int x = 0; x < displayW; x += maxChunkW)
		{
			GrVertex verts[6];
			const int chunkW = (displayW - x) > maxChunkW ? maxChunkW : displayW - x;

			NativeRenderer_BuildDisplayChunkRGBA(rgba, displayX + x, displayY + y, chunkW, chunkH);

			glBindTexture(GL_TEXTURE_2D, displayTexture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunkW, chunkH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

			NativeRenderer_SetOverrideTextureSize(maxChunkW, maxChunkH);
			NativeRenderer_FillDisplayChunkVerts(verts, x, y, chunkW, chunkH);
			NativeRenderer_UpdateVertexBuffer(verts, 6);
			NativeRenderer_DrawTriangles(0, 2);
		}
	}
}

void NativeRenderer_PresentVRAMDisplay(void)
{
	// NOTE(aalhendi): ctr-native local divergence. Retail presents this boot
	// splash path by displaying VRAM directly after DR_MOVE packets; the native
	// OpenGL backend otherwise swaps the current framebuffer and never shows
	// those VRAM-only copies.
	NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
}

/* Very low-overhead default. Opt-in internal diagnostics identify whether
 * a slow SDL_GL_SwapWindow consumes CPU (driver/software submission) or
 * blocks the calling thread (compositor/vblank/queue scheduling). */
#if defined(_WIN32) && defined(CTR_INTERNAL)
static int NativeRenderer_SwapProbeMarkerEnabled(const char *name)
{
    /* Each marker's first-open cost happens only in the optional path.
     * Windows normal mode never calls GetThreadTimes(). */
    FILE *f=fopen(name,"rb");
    const int enabled=(f!=NULL);
    if(f)fclose(f);
    return enabled;
}

static int NativeRenderer_SwapCpuProbeEnabled(void)
{
    static int checked,enabled;
    if(!checked)
    {
        enabled=NativeRenderer_SwapProbeMarkerEnabled("debug/swap_cpu_profile.enable");
        checked=1;
        if(enabled)Platform_Log("[SWAP CPU QA] diagnostic enabled (GetThreadTimes; every 600 swaps)\n");
    }
    return enabled;
}

static int NativeRenderer_SwapBackoffEnabled(void)
{
    static int checked,enabled;
    if(!checked)
    {
        enabled=NativeRenderer_SwapProbeMarkerEnabled("debug/swap_watchdog_backoff_qa.enable");
        checked=1;
        if(enabled)Platform_Log("[SWAP CPU QA] watchdog resync backoff test enabled\n");
    }
    return enabled;
}

static u64 NativeRenderer_SwapFileTimeTicks(FILETIME t)
{
    return ((u64)t.dwHighDateTime<<32) | (u64)t.dwLowDateTime;
}

static void NativeRenderer_ReportSwapCpu(double wallMs,double threadMs)
{
    static int n,slow;
    static double wallTotal,threadTotal,blockedTotal,maxWall;
    if(threadMs<0.0 || wallMs<0.0)return;
    if(threadMs>wallMs)threadMs=wallMs;
    ++n;
    wallTotal+=wallMs;
    threadTotal+=threadMs;
    blockedTotal+=wallMs-threadMs;
    if(wallMs>=5.0)++slow;
    if(wallMs>maxWall)maxWall=wallMs;
    if(n>=600)
    {
        Platform_Log("[SWAP CPU QA] frames=%d wall=%.3fms cpu=%.3fms blocked~=%.3fms slow>=5ms=%d maxWall=%.3fms\n",
          n,wallTotal/n,threadTotal/n,blockedTotal/n,slow,maxWall);
        n=slow=0;
        wallTotal=threadTotal=blockedTotal=maxWall=0.0;
    }
}
#endif

void NativeRenderer_SwapWindow(void)
{
	const u64 beginCounter = SDL_GetPerformanceCounter();
	u64 endCounter;
	double swapMs = 0.0;

	if (s_swapPerformanceFrequency == 0)
		s_swapPerformanceFrequency = SDL_GetPerformanceFrequency();

	const u64 frequency = s_swapPerformanceFrequency;
#if defined(_WIN32) && defined(CTR_INTERNAL)
    const int profileThread=NativeRenderer_SwapCpuProbeEnabled();
    FILETIME threadCreate={0},threadExit={0},kernelBefore={0},userBefore={0};
    FILETIME kernelAfter={0},userAfter={0};
    const int haveThreadBefore=profileThread &&
        GetThreadTimes(GetCurrentThread(),&threadCreate,&threadExit,
                       &kernelBefore,&userBefore);
#endif

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);
	SDL_GL_SwapWindow(g_window);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);

	endCounter = SDL_GetPerformanceCounter();
	if ((frequency != 0) && (endCounter >= beginCounter))
		swapMs = (double)(endCounter - beginCounter) * 1000.0 / (double)frequency;
#if defined(_WIN32) && defined(CTR_INTERNAL)
    if(haveThreadBefore && GetThreadTimes(GetCurrentThread(),
        &threadCreate,&threadExit,&kernelAfter,&userAfter))
    {
        const u64 before=NativeRenderer_SwapFileTimeTicks(kernelBefore)+
                          NativeRenderer_SwapFileTimeTicks(userBefore);
        const u64 after=NativeRenderer_SwapFileTimeTicks(kernelAfter)+
                         NativeRenderer_SwapFileTimeTicks(userAfter);
        if(after>=before)
            NativeRenderer_ReportSwapCpu(swapMs,(double)(after-before)/10000.0);
    }
#endif

if (swapMs >= 8.0)
	{
		/* Wall-time is enough to diagnose a pathological blocking swap. Avoid
		 * two GetThreadTimes() syscalls on every normal frame just to split CPU
		 * and blocked time in this rare diagnostic. */
		Platform_Log("[CTR Renderer] swap stall: wall=%.2fms\n", swapMs);
	}

	/*
	 * Recovery for a real issue observed on Windows + AMD: the compositor can
	 * occasionally make SwapWindow block ~7 ms every frame even though SDL
	 * reports swap interval 0. In that state Unlimited becomes ~120 FPS while
	 * the exact same scene normally runs 400-800 FPS.
	 *
	 * Do not react to one slow frame. Only recover after a consecutive streak,
	 * first by re-applying interval 0, then by syncing the SDL window and
	 * applying it again if the driver/compositor remains stuck.
	 */
	if (s_lastRequestedSwapInterval == 0)
	{
		if (swapMs >= NATIVE_SWAP_WATCHDOG_SLOW_MS)
		{
			s_swapWatchdogSlowStreak++;
			s_swapWatchdogFastStreak = 0;
		}
		else
		{
			s_swapWatchdogSlowStreak = 0;

			if (swapMs < 0.75)
			{
				if (s_swapWatchdogFastStreak < 120)
					s_swapWatchdogFastStreak++;
				if (s_swapWatchdogFastStreak >= 120)
				{
					s_swapWatchdogRecoveryStage = 0;
					s_swapWatchdogResyncAttempts = 0;
					s_swapWatchdogLastRecoveryCounter = 0;
				}
			}
			else
			{
				s_swapWatchdogFastStreak = 0;
			}
		}

		if (s_swapWatchdogSlowStreak >= NATIVE_SWAP_WATCHDOG_RECOVERY_STREAK)
		{
			u64 cooldownMs =
				(s_swapWatchdogRecoveryStage == 0) ?
				NATIVE_SWAP_WATCHDOG_RECOVERY_COOLDOWN_MS : 2000;
#if defined(_WIN32) && defined(CTR_INTERNAL)
            /* Once three consecutive resyncs failed, stop hammering the
             * already blocked compositor every two seconds. Normal retail
             * behavior is unchanged unless this specific QA marker exists. */
            if(s_swapWatchdogRecoveryStage>=2 &&
                s_swapWatchdogResyncAttempts>=3 &&
                NativeRenderer_SwapBackoffEnabled())
                cooldownMs=16000;
#endif
			const u64 cooldownTicks =
				(frequency != 0) ? (frequency * cooldownMs) / 1000 : 0;
			const int cooldownReady =
				(s_swapWatchdogLastRecoveryCounter == 0) ||
				(cooldownTicks == 0) ||
				(endCounter - s_swapWatchdogLastRecoveryCounter >= cooldownTicks);

			if (cooldownReady)
			{
				if (s_swapWatchdogRecoveryStage == 0)
				{
					Platform_LogWarn(
						"[CTR Renderer] swap watchdog: %.2f ms x%d; reapplying VSync OFF\n",
						swapMs, s_swapWatchdogSlowStreak);
					NativeRenderer_ApplySwapIntervalState(
						0, 1, "watchdog-reapply");
					s_swapWatchdogRecoveryStage = 1;
				}
				else
				{
					const bool syncOk = (g_window != NULL) ?
						SDL_SyncWindow(g_window) : false;
					Platform_LogWarn(
						"[CTR Renderer] swap watchdog: still blocked %.2f ms; window resync=%s\n",
						swapMs, syncOk ? "OK" : "FAIL");
					NativeRenderer_ApplySwapIntervalState(
						0, 1, "watchdog-window-resync");
					s_swapWatchdogRecoveryStage = 2;
					if(s_swapWatchdogResyncAttempts<1000)
						++s_swapWatchdogResyncAttempts;
				}

				s_swapWatchdogLastRecoveryCounter = endCounter;
				s_swapWatchdogSlowStreak = 0;
			}
		}
	}
}

internal void NativeRenderer_EnableDepth(int enable)
{
	const int active =
		(s_experimentalZBufferEnabled != 0) &&
		(s_experimentalDepthEligible != 0) &&
		(enable != 0);

	if (s_previousDepthMode == active)
		return;

	s_previousDepthMode = active;
	NativePerf_AddDepthStateChange();

	if (active)
	{
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
	}
	else
	{
		glDisable(GL_DEPTH_TEST);
	}
}

int NativeRenderer_GetGpuTimingEnabled(void)
{
	return s_gpuTimingEnabled != 0;
}

void NativeRenderer_SetGpuTimingEnabled(int enabled)
{
	enabled = enabled != 0;
	if (s_gpuTimingEnabled == enabled)
		return;

	if (!enabled && s_gpuTimerOpenIndex >= 0)
	{
		glEndQuery(GL_TIME_ELAPSED);
		s_gpuTimerSubmitted[s_gpuTimerOpenIndex] = 1;
		s_gpuTimerOpenIndex = -1;
	}

	s_gpuTimingEnabled = enabled;
	if (enabled)
	{
		if (s_gpuTimerQueries[0] == 0)
			glGenQueries(NATIVE_GPU_TIMER_QUERY_COUNT, s_gpuTimerQueries);
		Platform_LogWarn("[CTR Renderer] GPU timing: ON (non-blocking query ring)\n");
	}
	else
	{
		Platform_LogWarn("[CTR Renderer] GPU timing: OFF\n");
	}
}

double NativeRenderer_GetLastGpuFrameMs(void)
{
	return s_gpuTimingEnabled ? s_lastGpuFrameMs : -1.0;
}

int NativeRenderer_GetExperimentalCompactVRAM(void)
{
	return s_experimentalCompactVRAM != 0;
}

void NativeRenderer_SetExperimentalCompactVRAM(int enabled)
{
	enabled = enabled != 0;
	if (s_experimentalCompactVRAM == enabled)
		return;

	s_experimentalCompactVRAM = enabled;

	/* Reallocate in place. Texture object IDs stay stable, so the VRAM FBO and
	 * split references remain valid. Both buffers are marked fully dirty and
	 * repopulated from the authoritative CPU u16 VRAM mirror. */
	if (s_vramTexturesDouble[0] != 0)
	{
		for (int i = 0; i < 2; i++)
		{
			glBindTexture(GL_TEXTURE_2D, s_vramTexturesDouble[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, NativeRenderer_GetVRAMInternalFormat(),
				VRAM_WIDTH, VRAM_HEIGHT, 0, VRAM_FORMAT, GL_UNSIGNED_BYTE, NULL);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		NativeRenderer_ResetVRAMDirtyState();
	}

	Platform_LogWarn("[CTR Renderer] experimental VRAM storage: %s\n",
		enabled ? "RG8" : "RG32F");
}

int NativeRenderer_GetExperimentalZBuffer(void)
{
	return s_experimentalZBufferEnabled != 0;
}

void NativeRenderer_SetExperimentalZBuffer(int enabled)
{
	s_experimentalZBufferEnabled = enabled != 0;
	s_previousDepthMode = -1;
	if (!s_experimentalZBufferEnabled)
	{
		s_experimentalDepthEligible = 0;
		glDisable(GL_DEPTH_TEST);
		s_previousDepthMode = 0;
	}
}

void NativeRenderer_SetExperimentalDepthEligible(int eligible)
{
	s_experimentalDepthEligible = eligible != 0;
	NativeRenderer_EnableDepth(s_previousBlendMode == BM_NONE);
}

void NativeRenderer_SetStencilMode(int drawPrim)
{
	if (s_previousStencilMode == drawPrim)
	{
		return;
	}

	s_previousStencilMode = drawPrim;
	NativePerf_AddStencilStateChange();

	if (drawPrim)
	{
		glStencilFunc(GL_ALWAYS, 1, 0x10);
		glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
	}
	else
	{
		glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
		glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
	}
}

void NativeRenderer_SetBlendMode(BlendMode blendMode)
{
	/* A dual-source draw uses a different blend function even when the logical
	 * PS1 mode number matches. Invalidate the classic cache when leaving it. */
	if (s_previousPSXSinglePassBlendMode != -1)
	{
		s_previousPSXSinglePassBlendMode = -1;
		s_previousBlendMode = (BlendMode)-1;
	}

	if (s_previousBlendMode == blendMode)
	{
		return;
	}

	NativePerf_AddBlendStateChange();

	if (blendMode != BM_NONE)
	{
		if (s_previousBlendMode == BM_NONE || s_previousBlendMode == (BlendMode)-1)
		{
			glBlendColor(0.25f, 0.25f, 0.25f, 0.5f);
			glEnable(GL_BLEND);
		}

		NativeRenderer_EnableDepth(0);
	}

	switch (blendMode)
	{
	case BM_NONE:
		if (s_previousBlendMode != BM_NONE)
		{
			glBlendColor(1.f, 1.f, 1.f, 1.f);
			glDisable(GL_BLEND);
		}

		NativeRenderer_EnableDepth(1);
		break;
	case BM_AVERAGE:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		// NOTE(aalhendi): keep RGB blend weight constant so alpha can carry the PS1 mask bit.
		glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
		break;
	case BM_ADD:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	case BM_SUBTRACT:
		glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	case BM_ADD_QUATER_SOURCE:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
		break;
	}

	s_previousBlendMode = blendMode;
}

int NativeRenderer_CanSinglePassPSXTexturedSemiTrans(BlendMode blendMode)
{
	if (!s_dualSourceBlendSupported)
	{
		return 0;
	}

	/* Subtract needs dst-src for STP texels but a straight src replacement for
	 * non-STP texels; those two equations cannot share one fixed-function pass. */
	return blendMode == BM_AVERAGE ||
	       blendMode == BM_ADD ||
	       blendMode == BM_ADD_QUATER_SOURCE;
}

void NativeRenderer_SetPSXTexturedSemiTransBlendMode(BlendMode blendMode)
{
	if (!NativeRenderer_CanSinglePassPSXTexturedSemiTrans(blendMode))
	{
		NativeRenderer_SetBlendMode(blendMode);
		return;
	}

	if (s_previousPSXSinglePassBlendMode == (int)blendMode)
	{
		return;
	}

	NativePerf_AddBlendStateChange();
	glEnable(GL_BLEND);
	NativeRenderer_EnableDepth(0);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

	switch (blendMode)
	{
	case BM_AVERAGE:
		/* secondary alpha: 1.0 opaque texel, 0.5 STP texel */
		glBlendFuncSeparate(GL_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA, GL_ONE, GL_ZERO);
		break;
	case BM_ADD:
		/* secondary alpha: 1.0 opaque texel, 0.0 STP texel */
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC1_ALPHA, GL_ONE, GL_ZERO);
		break;
	case BM_ADD_QUATER_SOURCE:
		/* secondary RGB: 1.0 opaque / 0.25 STP; alpha controls dst 0 / 1 */
		glBlendFuncSeparate(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_ALPHA, GL_ONE, GL_ZERO);
		break;
	default:
		return;
	}

	s_previousPSXSinglePassBlendMode = (int)blendMode;
	/* Classic state is now unknown until NativeRenderer_SetBlendMode reapplies it. */
	s_previousBlendMode = (BlendMode)-1;
}

internal void NativeRenderer_SetViewPort(int x, int y, int width, int height)
{
	glViewport(x, y, width, height);
}

internal void NativeRenderer_SetWireframe(int enable)
{
	glPolygonMode(GL_FRONT_AND_BACK, enable ? GL_LINE : GL_FILL);
}

internal void NativeRenderer_BindVertexBuffer(void)
{
	const int bufferIndex = s_curVertexBuffer;

	glBindVertexArray(s_glVertexArray[bufferIndex]);
	/* ARRAY_BUFFER binding selects the target for glBufferSubData. Attribute
	 * formats/pointers were captured once in this VAO during PSX init. */
	glBindBuffer(GL_ARRAY_BUFFER, s_glVertexBuffer[bufferIndex]);

	s_curVertexBuffer++;
	s_curVertexBuffer &= 1;
}

void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int num_vertices)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
	if ((u32)num_vertices >= MAX_VERTEX_BUFFER_SIZE)
	{
		NATIVE_RENDERER_ERROR("MAX_VERTEX_BUFFER_SIZE reached, expect rendering errors\n");
		num_vertices = MAX_VERTEX_BUFFER_SIZE;
	}

	// assert(num_vertices <= MAX_VERTEX_BUFFER_SIZE);
	NativeRenderer_BindVertexBuffer();

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	/* Turbocharged-style GL_STREAM_DRAW storage orphaning, Opt-in STRESS
	 * measurement ONLY. Keeping the existing double-buffered VAOs intact
	 * preserves all vertex attributes and every draw-call start index.
	 * Exact batch-sized storage avoids allocating the maximum one-million
	 * vertices for each small OT split/upload. Normal play remains classic. */
	{
		static int checked, requested, logged;
		if (!checked)
		{
			FILE *flag=fopen("debug/gpu_stream_orphan_stress.enable","rb");
			requested=flag!=NULL;
			if (flag)fclose(flag);
			checked=1;
		}
		if (requested && NativePerf_IsStressTestMeasuring() && num_vertices > 0)
		{
			const GLsizeiptr bytes=(GLsizeiptr)num_vertices*(GLsizeiptr)sizeof(GrVertex);
			glBufferData(GL_ARRAY_BUFFER,bytes,NULL,GL_STREAM_DRAW);
			if(!logged)
			{
				Platform_Log("[CTR GPU STREAM] STRESS: GL_STREAM_DRAW orphaning enabled; first batch=%u bytes\\n",(unsigned)bytes);
				logged=1;
			}
		}
	}
#endif
	glBufferSubData(GL_ARRAY_BUFFER, 0, num_vertices * sizeof(GrVertex), vertices);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
}

void NativeRenderer_DrawTriangles(int start_vertex, int triangles)
{
	NativePerf_AddDrawCall((u32)((triangles > 0) ? triangles : 0));
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);
	glDrawArrays(GL_TRIANGLES, start_vertex, triangles * 3);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);
}

void NativeRenderer_PushDebugLabel(const char *label)
{
	if (!GLAD_GL_KHR_debug)
	{
		return;
	}
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x8000, strlen(label), label);
}

void NativeRenderer_PopDebugLabel(void)
{
	if (!GLAD_GL_KHR_debug)
	{
		return;
	}
	glPopDebugGroup();
}
