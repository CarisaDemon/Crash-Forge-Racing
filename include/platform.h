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
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
void Platform_PollHostEvents(void);
int Platform_PollInput(void);
#if defined(CTR_NATIVE)
int Platform_GetWideMode(void);
void Platform_SetWideMode(int enabled);
int Platform_GetHighRefreshMode(void);
void Platform_SetHighRefreshMode(int enabled);
int Platform_GetHighRefreshTargetFPS(void);
void Platform_WaitForHighRefreshFrame(void);
void Platform_UpdateLegacy30HzClock(int elapsedTimeMS);
int Platform_GetLegacy30HzTicks(void);
int Platform_GetLegacy30HzAlpha256(void);
int Platform_GetSubpixelMode(void);
void Platform_SetSubpixelMode(int enabled);
int Platform_GetTextureFilteringMode(void);
void Platform_SetTextureFilteringMode(int enabled);
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
int Platform_GetFullscreenMode(void);
void Platform_SetFullscreenMode(int enabled);
struct CameraDC;
struct PushBuffer;
void Platform_DebugFreecamUpdate(struct CameraDC *cDC, struct PushBuffer *pb);
int Platform_GetSavedAudioVolume(int type, int fallback);
void Platform_SaveAudioVolume(int type, int value);
int Platform_GetSavedAudioMode(int fallback);
void Platform_SaveAudioMode(int enabled);
int Platform_GetSavedVibrationMask(void);
void Platform_SaveVibrationMask(int mask);
#endif

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif

#if defined(CTR_NATIVE)
#include <platform/native_world.h>
#endif
