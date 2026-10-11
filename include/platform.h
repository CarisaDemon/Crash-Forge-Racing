#ifndef PLATFORM_H
#define PLATFORM_H

struct PlatformMempackArena
{
	void *base;
	void *start;
	void *endOfMemory;
	int size;
	int backingSize;
};

void Platform_Init(const char *title, int width, int height);
void Platform_Shutdown(void);
void Platform_RequestExit(void);
void Platform_InitScratchpad(void);
const struct PlatformMempackArena *Platform_InitMempackArena(void);
const struct PlatformMempackArena *Platform_GetMempackArena(void);
void Platform_BeginFrame(void);
int Platform_BeginScene(void);
void Platform_EndScene(void);
void Platform_EndFrame(void);
int Platform_GetDisplayFPS(void);
int Platform_GetHudHidden(void);
void Platform_SetHudHidden(int hidden);
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
void Platform_PollHostEvents(void);
void Platform_StartTextInput(void);
void Platform_StopTextInput(void);
int Platform_PollInput(void);
#if defined(CTR_NATIVE)
int Platform_GetWideMode(void);
void Platform_SetWideMode(int enabled);

enum PlatformFPSLimitMode
{
	PLATFORM_FPS_LIMIT_30 = 0,
	PLATFORM_FPS_LIMIT_60,
	PLATFORM_FPS_LIMIT_120,
	PLATFORM_FPS_LIMIT_144,
	PLATFORM_FPS_LIMIT_200,
	PLATFORM_FPS_LIMIT_240,
	PLATFORM_FPS_LIMIT_DISPLAY_HZ,
	PLATFORM_FPS_LIMIT_UNLIMITED,
	PLATFORM_FPS_LIMIT_COUNT
};

int Platform_GetHighRefreshMode(void);
void Platform_SetHighRefreshMode(int enabled);
int Platform_GetHighRefreshTargetFPS(void);
int Platform_GetFPSLimitMode(void);
void Platform_SetFPSLimitMode(int mode);
int Platform_GetDisplayRefreshFPS(void);
u64 Platform_GetHostPresentCount(void);
void Platform_WaitForHighRefreshFrame(void);
void Platform_UpdateLegacy30HzClock(int elapsedTimeMS);
int Platform_GetLegacy30HzTicks(void);
int Platform_GetLegacy30HzAlpha256(void);
int Platform_GetSubpixelMode(void);
void Platform_SetSubpixelMode(int enabled);
int Platform_GetTextureFilteringMode(void);
void Platform_SetTextureFilteringMode(int enabled);
int Platform_GetDitheringMode(void);
void Platform_SetDitheringMode(int enabled);
int Platform_GetPerfOverlayMode(void);
void Platform_SetPerfOverlayMode(int enabled);
double Platform_GetLastFrameTimeMS(void);
int Platform_GetGpuTimingMode(void);
void Platform_SetGpuTimingMode(int enabled);
double Platform_GetLastGpuFrameMS(void);
int Platform_GetExperimentalCompactVRAMMode(void);
void Platform_SetExperimentalCompactVRAMMode(int enabled);
int Platform_GetExperimentalZBufferMode(void);
void Platform_SetExperimentalZBufferMode(int enabled);
int Platform_GetCharacterDetailMode(void);
void Platform_SetCharacterDetailMode(int mode);
int Platform_GetResolutionPresetCount(void);
int Platform_GetResolutionPresetIndex(void);
void Platform_GetResolutionPresetSize(int index, int *width, int *height);
void Platform_SetResolutionPreset(int index);
void Platform_GetWindowSize(int *width, int *height);

#define PLATFORM_MOUSE_UI_LEFT   0x1u
#define PLATFORM_MOUSE_UI_RIGHT  0x2u
#define PLATFORM_MOUSE_UI_MIDDLE 0x4u

int Platform_GetMouseUiPosition(int *x, int *y);
unsigned int Platform_ConsumeMouseUiTaps(void);
int Platform_ConsumeMouseUiActivity(void);
int Platform_GetFullscreenMode(void);
void Platform_SetFullscreenMode(int enabled);
struct CameraDC;
struct PushBuffer;
int Platform_DebugFreecamIsEnabled(void);
int Platform_DebugFreecamConsumesKey(int key);
void Platform_DebugFreecamUpdate(struct CameraDC *cDC, struct PushBuffer *pb);
int Platform_GetSavedAudioVolume(int type, int fallback);
void Platform_SaveAudioVolume(int type, int value);
int Platform_GetSavedAudioMode(int fallback);
void Platform_SaveAudioMode(int enabled);
int Platform_GetSavedVibrationMask(void);
void Platform_SaveVibrationMask(int mask);
const char *Platform_GetPlayerName(int playerIndex);
void Platform_SetPlayerName(int playerIndex, const char *name);
#endif

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif

#if defined(CTR_NATIVE)
#include <platform/native_world.h>
#endif
