#include <platform/native_world_math.h>
#include <platform.h>

#include <CharacterRegistry.h>
#include <NativeHangout.h>
#include <NativeSession.h>
#include <platform/native_gltf.h>
#include <platform/native_obj.h>
#include <macros.h>

#include "platform/native_audio.h"
#include "platform/native_glad.h"
#include "platform/native_gpu.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#ifdef _WIN32
#include "platform/native_win32.h"
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

SDL_Window *g_window = NULL;
int g_dbg_polygonSelected = 0;

extern int g_cfg_bilinearFiltering;
int g_cfg_psxDithering = 0;
extern int g_dbg_emulatorPaused;
extern int g_dbg_texturelessMode;
extern int g_dbg_wireframeMode;
extern int g_windowHeight;
extern int g_windowWidth;
void NativeGTE_SubpixelBeginFrame(void);
int MainFreeze_NativeControlsHandleTextInput(const char *text);
int MainFreeze_NativeControlsHandleHostKey(int key, int down, int repeat);
internal void Platform_UpdateCursorVisibility(void);

#define HOST_ALT_LEFT  (1 << 0)
#define HOST_ALT_RIGHT (1 << 1)
global_variable int s_hostAltKeyState = 0;
global_variable int s_platformInitialized = 0;
global_variable int s_videoWideMode = 0;
global_variable int s_platformBeginScene = 0;
global_variable int s_pinnedVramDisplayFrames = 0;
global_variable int s_pinnedVramDisplayCustomRect = 0;
global_variable int s_pinnedVramDisplayX = 0;
global_variable int s_pinnedVramDisplayY = 0;
global_variable int s_pinnedVramDisplayW = 0;
global_variable int s_pinnedVramDisplayH = 0;

global_variable int s_debugFreecamEnabled = 0;
global_variable int s_debugFreecamCaptured = 0;
global_variable int s_debugFreecamCameraID = -1;
global_variable s16 s_debugFreecamPos[3];
global_variable s16 s_debugFreecamRot[3];
global_variable int s_hudHidden = 0;

global_variable unsigned int s_mouseUiTapMask = 0;
global_variable int s_mouseUiActivity = 0;
global_variable u64 s_mouseUiIgnoreMotionUntil = 0;

#define NATIVE_MOUSE_UI_RESIZE_SUPPRESS_MS 200
#define NATIVE_FPS_REPORT_FRAME_WINDOW 2000
global_variable int s_fpsFrameCount = 0;
global_variable u64 s_fpsLastCounter = 0;
global_variable u64 s_frameDurationLastCounter = 0;
global_variable double s_lastFrameTimeMs = 0.0;
global_variable int s_perfOverlayEnabled = 0;
global_variable int s_experimentalFeedbackCadenceEnabled = 0;

global_variable int g_cfg_highRefreshPresentation = 1;
int g_cfg_subpixelGeometry = 1;
#define NATIVE_HOST_PRESENT_HZ_FALLBACK 60
global_variable int s_displayRefreshFPS = NATIVE_HOST_PRESENT_HZ_FALLBACK;
global_variable int s_highRefreshTargetFPS = NATIVE_HOST_PRESENT_HZ_FALLBACK;
global_variable int s_fpsLimitMode = PLATFORM_FPS_LIMIT_DISPLAY_HZ;
global_variable SDL_DisplayID s_highRefreshDisplayID = 0;
global_variable u64 s_highRefreshLastDisplayCheckCounter = 0;
global_variable u64 s_hostPresentCount = 0;
global_variable u64 s_hostPresentNextCounter = 0;
global_variable u64 s_hostInterpolationStartCounter = 0;
global_variable int s_hostInterpolationActive = 0;

/* Experimental Unlimited-only present decoupling. The game/render loop stays
 * completely uncapped; only the physical window swap is paced to the monitor.
 * This is marker-gated until A/B testing proves it removes compositor stalls
 * without changing gameplay or normal capped modes. */
static u64 s_unlimitedPresentNextCounter = 0;
static int s_unlimitedPresentDecoupleChecked = 0;
static int s_unlimitedPresentDecoupleEnabled = 0;

// When HIGH REFRESH is enabled this is the actual game-loop target, not a
// presentation-only multiplier. VBlank remains independently emulated at 60 Hz.
global_variable u64 s_nextHighRefreshFrameCounter = 0;
global_variable u64 s_highRefreshFrameRemainder = 0;

// Many retail systems use "one frame" as a unit of time. At 200 FPS those
// counters must not advance on every host update. Accumulate real elapsed
// milliseconds and emit PS1-shaped 32 ms ticks while the real loop stays fast.
global_variable int s_legacy30AccumulatorMS = 0;
global_variable int s_legacy30Ticks = 1;
static int s_nativeTickOverride = -1;

#define NATIVE_SETTINGS_FILE "ctr_settings.cfg"

typedef struct NativeResolutionPreset
{
	int width;
	int height;
} NativeResolutionPreset;

global_variable const NativeResolutionPreset s_nativeResolutionPresets[] = {
	{800, 600},
	{1280, 720},
	{1600, 900},
	{1920, 1080},
	{2560, 1440},
	{3840, 2160},
};

#define NATIVE_RESOLUTION_PRESET_COUNT ((int)(sizeof(s_nativeResolutionPresets) / sizeof(s_nativeResolutionPresets[0])))

typedef struct NativeUserSettings
{
	int loaded;
	int wideMode;
	int highRefresh;
	int fpsLimitMode;
	int subpixel;
	int bilinearFiltering;
	int dithering;
	int perfOverlay;
	int characterDetail;
	int fullscreen;
	int windowWidth;
	int windowHeight;
	int volFX;
	int volMusic;
	int volVoice;
	int stereo;
	int vibrationMask;
	char playerNames[4][32];
} NativeUserSettings;

global_variable NativeUserSettings s_nativeUserSettings = {
	.loaded = 0,
	.wideMode = 0,
	.highRefresh = 1,
	.fpsLimitMode = PLATFORM_FPS_LIMIT_DISPLAY_HZ,
	.subpixel = 1,
	.bilinearFiltering = 0,
	.dithering = 0,
	.perfOverlay = 0,
	.characterDetail = 1,
	.fullscreen = 0,
	.windowWidth = 0,
	.windowHeight = 0,
	.volFX = 215,
	.volMusic = 175,
	.volVoice = 255,
	.stereo = 1,
	.vibrationMask = 0,
};

internal int Platform_SettingsClamp(int value, int minValue, int maxValue)
{
	if (value < minValue) return minValue;
	if (value > maxValue) return maxValue;
	return value;
}

internal void Platform_CopyPlayerNameSanitized(
	char *dst,
	int dstSize,
	const char *src,
	int playerIndex)
{
	char fallback[32];
	int out = 0;
	int i;

	if (dst == NULL || dstSize <= 0)
		return;

	snprintf(
		fallback,
		sizeof(fallback),
		"PLAYER %d",
		playerIndex + 1);

	if (src == NULL || src[0] == 0)
		src = fallback;

	for (i = 0; src[i] != 0 && out < dstSize - 1; i++)
	{
		unsigned char c = (unsigned char)src[i];

		if (c < 0x20 || c == 0x7f)
			continue;

		dst[out++] = (char)c;
	}

	while (out > 0 && dst[out - 1] == ' ')
		out--;

	dst[out] = 0;

	if (out == 0)
		snprintf(dst, (size_t)dstSize, "%s", fallback);
}

internal void Platform_SettingsSanitize(void)
{
	s_nativeUserSettings.wideMode = s_nativeUserSettings.wideMode != 0;
	s_nativeUserSettings.highRefresh = 1;
	s_nativeUserSettings.fpsLimitMode = Platform_SettingsClamp(
		s_nativeUserSettings.fpsLimitMode, 0, PLATFORM_FPS_LIMIT_COUNT - 1);
	s_nativeUserSettings.subpixel = s_nativeUserSettings.subpixel != 0;
	s_nativeUserSettings.bilinearFiltering = s_nativeUserSettings.bilinearFiltering != 0;
	s_nativeUserSettings.dithering = s_nativeUserSettings.dithering != 0;
	s_nativeUserSettings.perfOverlay = s_nativeUserSettings.perfOverlay != 0;
	s_nativeUserSettings.characterDetail = Platform_SettingsClamp(s_nativeUserSettings.characterDetail, 0, 3);
	s_nativeUserSettings.fullscreen = s_nativeUserSettings.fullscreen != 0;
	s_nativeUserSettings.stereo = s_nativeUserSettings.stereo != 0;
	s_nativeUserSettings.vibrationMask &= 0xF00;
	s_nativeUserSettings.windowWidth = Platform_SettingsClamp(s_nativeUserSettings.windowWidth, 320, 7680);
	s_nativeUserSettings.windowHeight = Platform_SettingsClamp(s_nativeUserSettings.windowHeight, 240, 4320);
	s_nativeUserSettings.volFX = Platform_SettingsClamp(s_nativeUserSettings.volFX, 0, 255);
	s_nativeUserSettings.volMusic = Platform_SettingsClamp(s_nativeUserSettings.volMusic, 0, 255);
	s_nativeUserSettings.volVoice = Platform_SettingsClamp(s_nativeUserSettings.volVoice, 0, 255);

	for (int i = 0; i < 4; i++)
	{
		char sanitized[32];
		Platform_CopyPlayerNameSanitized(
			sanitized,
			(int)sizeof(sanitized),
			s_nativeUserSettings.playerNames[i],
			i);
		snprintf(
			s_nativeUserSettings.playerNames[i],
			sizeof(s_nativeUserSettings.playerNames[i]),
			"%s",
			sanitized);
	}
}

internal void Platform_SettingsWrite(void)
{
	FILE *config;
	if (!s_nativeUserSettings.loaded) return;

	Platform_SettingsSanitize();
	config = fopen(NATIVE_SETTINGS_FILE, "wb");
	if (config == NULL)
	{
		Platform_LogWarn("[CTR Native] could not save %s\n", NATIVE_SETTINGS_FILE);
		return;
	}

	fprintf(config, "version=1\n");
	fprintf(config, "aspect_wide=%d\n", s_nativeUserSettings.wideMode);
	fprintf(config, "high_refresh=1\n");
	fprintf(config, "fps_limit_mode=%d\n", s_nativeUserSettings.fpsLimitMode);
	fprintf(config, "subpixel=%d\n", s_nativeUserSettings.subpixel);
	fprintf(config, "bilinear_filter=%d\n", s_nativeUserSettings.bilinearFiltering);
	fprintf(config, "dithering=%d\n", s_nativeUserSettings.dithering);
	fprintf(config, "perf_overlay=%d\n", s_nativeUserSettings.perfOverlay);
	fprintf(config, "character_detail=%d\n", s_nativeUserSettings.characterDetail);
	fprintf(config, "fullscreen=%d\n", s_nativeUserSettings.fullscreen);
	fprintf(config, "window_width=%d\n", s_nativeUserSettings.windowWidth);
	fprintf(config, "window_height=%d\n", s_nativeUserSettings.windowHeight);
	fprintf(config, "fx_volume=%d\n", s_nativeUserSettings.volFX);
	fprintf(config, "music_volume=%d\n", s_nativeUserSettings.volMusic);
	fprintf(config, "voice_volume=%d\n", s_nativeUserSettings.volVoice);
	fprintf(config, "stereo=%d\n", s_nativeUserSettings.stereo);
	fprintf(config, "vibration_disabled_mask=%d\n", s_nativeUserSettings.vibrationMask);
	for (int i = 0; i < 4; i++)
		fprintf(config, "player_name_%d=%s\n", i + 1, s_nativeUserSettings.playerNames[i]);
	fclose(config);
}

internal void Platform_SettingsLoad(int defaultWidth, int defaultHeight)
{
	FILE *config;
	char line[160];
	int foundSettingsFile = 0;

	if (s_nativeUserSettings.loaded) return;

	s_nativeUserSettings.windowWidth = defaultWidth;
	s_nativeUserSettings.windowHeight = defaultHeight;
	s_nativeUserSettings.wideMode = defaultWidth * 3 > defaultHeight * 4;
	for (int i = 0; i < 4; i++)
	{
		snprintf(
			s_nativeUserSettings.playerNames[i],
			sizeof(s_nativeUserSettings.playerNames[i]),
			"PLAYER %d",
			i + 1);
	}

	config = fopen(NATIVE_SETTINGS_FILE, "rb");
	if (config != NULL)
	{
		foundSettingsFile = 1;
		while (fgets(line, sizeof(line), config) != NULL)
		{
			char key[64];
			int value;
			int playerNameIndex;
			char playerNameValue[32];

			if (sscanf(
			        line,
			        "player_name_%d=%31[^\r\n]",
			        &playerNameIndex,
			        playerNameValue) == 2)
			{
				if (playerNameIndex >= 1 && playerNameIndex <= 4)
				Platform_CopyPlayerNameSanitized(
					s_nativeUserSettings.playerNames[playerNameIndex - 1],
					32,
					playerNameValue,
					playerNameIndex - 1);
				continue;
			}

			if (sscanf(line, "%63[^=]=%d", key, &value) != 2) continue;

			if (strcmp(key, "aspect_wide") == 0) s_nativeUserSettings.wideMode = value;
			else if (strcmp(key, "high_refresh") == 0) s_nativeUserSettings.highRefresh = value;
			else if (strcmp(key, "fps_limit_mode") == 0) s_nativeUserSettings.fpsLimitMode = value;
			else if (strcmp(key, "subpixel") == 0) s_nativeUserSettings.subpixel = value;
			else if (strcmp(key, "bilinear_filter") == 0) s_nativeUserSettings.bilinearFiltering = value;
			else if (strcmp(key, "dithering") == 0) s_nativeUserSettings.dithering = value;
			else if (strcmp(key, "perf_overlay") == 0) s_nativeUserSettings.perfOverlay = value;
			else if (strcmp(key, "character_detail") == 0) s_nativeUserSettings.characterDetail = value;
			else if (strcmp(key, "fullscreen") == 0) s_nativeUserSettings.fullscreen = value;
			else if (strcmp(key, "window_width") == 0) s_nativeUserSettings.windowWidth = value;
			else if (strcmp(key, "window_height") == 0) s_nativeUserSettings.windowHeight = value;
			else if (strcmp(key, "fx_volume") == 0) s_nativeUserSettings.volFX = value;
			else if (strcmp(key, "music_volume") == 0) s_nativeUserSettings.volMusic = value;
			else if (strcmp(key, "voice_volume") == 0) s_nativeUserSettings.volVoice = value;
			else if (strcmp(key, "stereo") == 0) s_nativeUserSettings.stereo = value;
			else if (strcmp(key, "vibration_disabled_mask") == 0) s_nativeUserSettings.vibrationMask = value;
		}
		fclose(config);
	}

	/* One-time compatibility with the old aspect-only file. */
	if (!foundSettingsFile)
	{
		FILE *legacy = fopen("ctr_video.cfg", "rb");
		if (legacy != NULL)
		{
			char value[16] = {0};
			if (fgets(value, sizeof(value), legacy) != NULL)
			{
				if (strncmp(value, "16:9", 4) == 0)
				{
					s_nativeUserSettings.wideMode = 1;
					s_nativeUserSettings.windowWidth = 1280;
					s_nativeUserSettings.windowHeight = 720;
				}
				else if (strncmp(value, "4:3", 3) == 0)
				{
					s_nativeUserSettings.wideMode = 0;
					s_nativeUserSettings.windowWidth = 800;
					s_nativeUserSettings.windowHeight = 600;
				}
			}
			fclose(legacy);
		}
	}

	s_nativeUserSettings.loaded = 1;
	Platform_SettingsSanitize();

	/* The native render loop is always active now. Frame limiting is selected
	 * independently (fixed FPS, display Hz, or unlimited). */
	g_cfg_highRefreshPresentation = 1;
	s_nativeUserSettings.highRefresh = 1;
	s_fpsLimitMode = s_nativeUserSettings.fpsLimitMode;
	g_cfg_subpixelGeometry = s_nativeUserSettings.subpixel;
	g_cfg_bilinearFiltering = s_nativeUserSettings.bilinearFiltering;
	g_cfg_psxDithering = s_nativeUserSettings.dithering;
	s_perfOverlayEnabled = s_nativeUserSettings.perfOverlay;
	s_videoWideMode = s_nativeUserSettings.wideMode;
	NativeSession_SetLocalPlayerName(s_nativeUserSettings.playerNames[0]);

	if (!foundSettingsFile) Platform_SettingsWrite();
}

int Platform_GetTextureFilteringMode(void)
{
	return g_cfg_bilinearFiltering != 0;
}

void Platform_SetTextureFilteringMode(int enabled)
{
	enabled = enabled != 0;
	if (g_cfg_bilinearFiltering == enabled) return;
	g_cfg_bilinearFiltering = enabled;
	s_nativeUserSettings.bilinearFiltering = enabled;
	Platform_SettingsWrite();
	Platform_LogWarn("[CTR Native] filtering mode: %s\n", enabled ? "BILINEAR" : "NEAREST");
}

int Platform_GetDitheringMode(void)
{
	return g_cfg_psxDithering != 0;
}

void Platform_SetDitheringMode(int enabled)
{
	enabled = enabled != 0;
	if (g_cfg_psxDithering == enabled) return;
	g_cfg_psxDithering = enabled;
	s_nativeUserSettings.dithering = enabled;
	Platform_SettingsWrite();
	Platform_LogWarn("[CTR Native] PS1 dithering: %s\n", enabled ? "ON" : "OFF");
}

int Platform_GetPerfOverlayMode(void)
{
	return s_perfOverlayEnabled != 0;
}

void Platform_SetPerfOverlayMode(int enabled)
{
	enabled = enabled != 0;
	if (s_perfOverlayEnabled == enabled) return;
	s_perfOverlayEnabled = enabled;
	s_nativeUserSettings.perfOverlay = enabled;
	Platform_SettingsWrite();
}

double Platform_GetLastFrameTimeMS(void)
{
	return s_lastFrameTimeMs;
}

int Platform_GetGpuTimingMode(void)
{
	return NativeRenderer_GetGpuTimingEnabled();
}

void Platform_SetGpuTimingMode(int enabled)
{
	NativeRenderer_SetGpuTimingEnabled(enabled != 0);
}

double Platform_GetLastGpuFrameMS(void)
{
	return NativeRenderer_GetLastGpuFrameMs();
}

int Platform_GetExperimentalCompactVRAMMode(void)
{
	return NativeRenderer_GetExperimentalCompactVRAM();
}

void Platform_SetExperimentalCompactVRAMMode(int enabled)
{
	NativeRenderer_SetExperimentalCompactVRAM(enabled != 0);
}

int Platform_GetExperimentalZBufferMode(void)
{
	return NativeRenderer_GetExperimentalZBuffer();
}

void Platform_SetExperimentalZBufferMode(int enabled)
{
	NativeRenderer_SetExperimentalZBuffer(enabled != 0);
	Platform_LogWarn(
		"[CTR Native] EXPERIMENTAL Z-buffer: %s (custom opaque only; retail world/effects stay OT-ordered; not persisted)\n",
		NativeRenderer_GetExperimentalZBuffer() ? "ON" : "OFF");
}

int Platform_GetCharacterDetailMode(void)
{
	return Platform_SettingsClamp(s_nativeUserSettings.characterDetail, 0, 3);
}

void Platform_SetCharacterDetailMode(int mode)
{
	mode = Platform_SettingsClamp(mode, 0, 3);
	if (s_nativeUserSettings.characterDetail == mode) return;
	s_nativeUserSettings.characterDetail = mode;
	Platform_SettingsWrite();
	Platform_LogWarn("[CTR Native] character detail: %s\n",
	                 mode == 0 ? "LOW" : (mode == 1 ? "SMART" : (mode == 2 ? "HIGH" : "ULTRA")));
}

int Platform_GetResolutionPresetCount(void)
{
	return NATIVE_RESOLUTION_PRESET_COUNT;
}

int Platform_GetResolutionPresetIndex(void)
{
	for (int i = 0; i < NATIVE_RESOLUTION_PRESET_COUNT; i++)
	{
		if ((s_nativeUserSettings.windowWidth == s_nativeResolutionPresets[i].width) &&
		    (s_nativeUserSettings.windowHeight == s_nativeResolutionPresets[i].height))
			return i;
	}
	return -1;
}

void Platform_GetResolutionPresetSize(int index, int *width, int *height)
{
	index = Platform_SettingsClamp(index, 0, NATIVE_RESOLUTION_PRESET_COUNT - 1);
	if (width != NULL) *width = s_nativeResolutionPresets[index].width;
	if (height != NULL) *height = s_nativeResolutionPresets[index].height;
}

void Platform_GetWindowSize(int *width, int *height)
{
	if (width != NULL) *width = g_windowWidth;
	if (height != NULL) *height = g_windowHeight;
}

internal void Platform_SuppressMouseUiMotion(void)
{
	/* A resize/fullscreen transition can synthesize a cursor recenter event.
	   Clear any pending hover-ownership bit and ignore motion briefly so the
	   mouse cannot steal menu focus from keyboard/controller just because
	   Windows/SDL moved it for us. Real clicks are intentionally preserved. */
	s_mouseUiActivity = 0;
	s_mouseUiIgnoreMotionUntil =
		SDL_GetTicks() + NATIVE_MOUSE_UI_RESIZE_SUPPRESS_MS;
}

int Platform_GetMouseUiPosition(int *x, int *y)
{
	float mouseX;
	float mouseY;

	if ((x == NULL) || (y == NULL) || (g_window == NULL))
	{
		return 0;
	}

	SDL_GetMouseState(&mouseX, &mouseY);
	return NativeRenderer_WindowToPSX(mouseX, mouseY, x, y);
}

unsigned int Platform_ConsumeMouseUiTaps(void)
{
	unsigned int taps = s_mouseUiTapMask;
	s_mouseUiTapMask = 0;
	return taps;
}

int Platform_ConsumeMouseUiActivity(void)
{
	int activity = s_mouseUiActivity;
	s_mouseUiActivity = 0;
	return activity;
}

int Platform_GetFullscreenMode(void)
{
	if (g_window == NULL)
		return s_nativeUserSettings.fullscreen != 0;
	return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

internal void Platform_ApplyExclusiveFullscreenResolution(int width, int height)
{
	if (g_window == NULL)
		return;

	SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_window);
	SDL_DisplayMode closest;

	if ((displayID != 0) &&
	    SDL_GetClosestFullscreenDisplayMode(displayID, width, height, 0.0f, true, &closest))
	{
		if (!SDL_SetWindowFullscreenMode(g_window, &closest))
			Platform_LogWarn("[CTR Native] fullscreen mode %dx%d rejected: %s\n", width, height, SDL_GetError());
	}
	else
	{
		/* Fall back to borderless desktop fullscreen if the requested exclusive
		   mode does not exist on this monitor. */
		SDL_SetWindowFullscreenMode(g_window, NULL);
		Platform_LogWarn("[CTR Native] no exclusive %dx%d mode; using desktop fullscreen\n", width, height);
	}
}

void Platform_SetResolutionPreset(int index)
{
	index = Platform_SettingsClamp(index, 0, NATIVE_RESOLUTION_PRESET_COUNT - 1);
	const int width = s_nativeResolutionPresets[index].width;
	const int height = s_nativeResolutionPresets[index].height;

	s_nativeUserSettings.windowWidth = width;
	s_nativeUserSettings.windowHeight = height;

	if (g_window != NULL)
	{
		Platform_SuppressMouseUiMotion();
		if (Platform_GetFullscreenMode())
		{
			Platform_ApplyExclusiveFullscreenResolution(width, height);
			SDL_SyncWindow(g_window);
		}
		else
		{
			if (!SDL_SetWindowSize(g_window, width, height))
				Platform_LogWarn("[CTR Native] window resize %dx%d failed: %s\n", width, height, SDL_GetError());
			SDL_SyncWindow(g_window);
		}

		SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
		NativeRenderer_ResetDevice();
	}

	Platform_SettingsWrite();
	Platform_LogWarn("[CTR Native] resolution preset: %dx%d\n", width, height);
}

void Platform_SetFullscreenMode(int enabled)
{
	enabled = enabled != 0;
	s_nativeUserSettings.fullscreen = enabled;

	if (g_window == NULL)
	{
		Platform_SettingsWrite();
		return;
	}

	Platform_SuppressMouseUiMotion();

	if (enabled)
	{
		Platform_ApplyExclusiveFullscreenResolution(s_nativeUserSettings.windowWidth,
		                                           s_nativeUserSettings.windowHeight);
		if (!SDL_SetWindowFullscreen(g_window, true))
			Platform_LogWarn("[CTR Native] fullscreen enable failed: %s\n", SDL_GetError());
		SDL_SyncWindow(g_window);
	}
	else
	{
		if (!SDL_SetWindowFullscreen(g_window, false))
			Platform_LogWarn("[CTR Native] fullscreen disable failed: %s\n", SDL_GetError());
		SDL_SyncWindow(g_window);
		SDL_SetWindowFullscreenMode(g_window, NULL);
		if (!SDL_SetWindowSize(g_window, s_nativeUserSettings.windowWidth, s_nativeUserSettings.windowHeight))
			Platform_LogWarn("[CTR Native] restore window size failed: %s\n", SDL_GetError());
		SDL_SyncWindow(g_window);
	}

	SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
	s_nativeUserSettings.fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
	Platform_SettingsWrite();
	Platform_UpdateCursorVisibility();
	NativeRenderer_ResetDevice();

	Platform_LogWarn("[CTR Native] display mode: %s (%dx%d)\n",
	                 s_nativeUserSettings.fullscreen ? "FULLSCREEN" : "WINDOWED",
	                 g_windowWidth, g_windowHeight);
}

int Platform_GetSavedAudioVolume(int type, int fallback)
{
	int value = fallback;
	if (type == 0) value = s_nativeUserSettings.volFX;
	else if (type == 1) value = s_nativeUserSettings.volMusic;
	else if (type == 2) value = s_nativeUserSettings.volVoice;
	return Platform_SettingsClamp(value, 0, 255);
}

void Platform_SaveAudioVolume(int type, int value)
{
	value = Platform_SettingsClamp(value, 0, 255);
	if (type == 0) s_nativeUserSettings.volFX = value;
	else if (type == 1) s_nativeUserSettings.volMusic = value;
	else if (type == 2) s_nativeUserSettings.volVoice = value;
	else return;
	Platform_SettingsWrite();
}

int Platform_GetSavedAudioMode(int fallback)
{
	if (!s_nativeUserSettings.loaded) return fallback != 0;
	return s_nativeUserSettings.stereo != 0;
}

void Platform_SaveAudioMode(int enabled)
{
	s_nativeUserSettings.stereo = enabled != 0;
	Platform_SettingsWrite();
}

int Platform_GetSavedVibrationMask(void)
{
	return s_nativeUserSettings.vibrationMask & 0xF00;
}

void Platform_SaveVibrationMask(int mask)
{
	s_nativeUserSettings.vibrationMask = mask & 0xF00;
	Platform_SettingsWrite();
}

const char *Platform_GetPlayerName(int playerIndex)
{
	if (playerIndex < 0 || playerIndex >= 4)
		playerIndex = 0;

	return s_nativeUserSettings.playerNames[playerIndex];
}

void Platform_SetPlayerName(int playerIndex, const char *name)
{
	if (playerIndex < 0 || playerIndex >= 4)
		return;

	Platform_CopyPlayerNameSanitized(
		s_nativeUserSettings.playerNames[playerIndex],
		(int)sizeof(s_nativeUserSettings.playerNames[playerIndex]),
		name,
		playerIndex);

	if (playerIndex == 0)
		NativeSession_SetLocalPlayerName(
			s_nativeUserSettings.playerNames[0]);

	Platform_SettingsWrite();
}

void Platform_UpdateLegacy30HzClock(int elapsedTimeMS)
{
	if (!g_cfg_highRefreshPresentation)
	{
		s_legacy30AccumulatorMS = 0;
		s_legacy30Ticks = 1;
		return;
	}

	if (elapsedTimeMS < 0)
	{
		elapsedTimeMS = 0;
	}

	s_legacy30Ticks = NW_ConsumeClock(&s_legacy30AccumulatorMS, elapsedTimeMS);
}

int Platform_GetLegacy30HzTicks(void)
{
	return s_nativeTickOverride >= 0 ? s_nativeTickOverride : s_legacy30Ticks;
}

int Platform_SetLegacyTickOverride(int ticks)
{
    int previous = s_nativeTickOverride;
    s_nativeTickOverride = ticks;
    return previous;
}

int Platform_GetLegacy30HzAlpha256(void)
{
	if (!g_cfg_highRefreshPresentation)
	{
		return 256;
	}
	int alpha = (s_legacy30AccumulatorMS * 256) / 32;
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;
	return alpha;
}

int Platform_GetHudHidden(void)
{
	return s_hudHidden != 0;
}

void Platform_SetHudHidden(int hidden)
{
	s_hudHidden = hidden != 0;
}

int Platform_GetDisplayFPS(void)
{
	// Report completed CTR logical frames. High-refresh host presents are not
	// counted as extra game frames, so this remains ~30 while simulation stays 30 Hz.
	static u64 lastCounter = 0;
	static int frameCount = 0;
	static int displayedFPS = 0;
	const u64 now = SDL_GetPerformanceCounter();
	const u64 frequency = SDL_GetPerformanceFrequency();

	if (frequency == 0)
	{
		return displayedFPS;
	}
	if (lastCounter == 0)
	{
		lastCounter = now;
		return displayedFPS;
	}

	frameCount++;
	if (now - lastCounter >= frequency / 2)
	{
		displayedFPS = (int)(((double)frameCount * (double)frequency /
			(double)(now - lastCounter)) + 0.5);
		frameCount = 0;
		lastCounter = now;
	}
	return displayedFPS;
}

internal void Platform_CalcFPS(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 now = SDL_GetPerformanceCounter();

	if (freq == 0)
		return;

	if (s_frameDurationLastCounter != 0 && now > s_frameDurationLastCounter)
	{
		s_lastFrameTimeMs =
			(double)(now - s_frameDurationLastCounter) * 1000.0 / (double)freq;
	}
	s_frameDurationLastCounter = now;

#if defined(CTR_INTERNAL)

	if (s_fpsLastCounter == 0)
	{
		s_fpsLastCounter = now;
		s_fpsFrameCount = 0;
		return;
	}

	s_fpsFrameCount++;
	if (s_fpsFrameCount < NATIVE_FPS_REPORT_FRAME_WINDOW)
	{
		return;
	}

	if (now > s_fpsLastCounter)
	{
		const f64 elapsedSeconds = (f64)(now - s_fpsLastCounter) / (f64)freq;
		const f64 fps = (f64)s_fpsFrameCount / elapsedSeconds;

		Platform_Log("[CTR Native] FPS: %.2f (last %d frames)\n", fps, s_fpsFrameCount);
	}

	s_fpsFrameCount = 0;
	s_fpsLastCounter = now;
#endif
}

internal void Platform_GetWindowName(const char *appName, char *buffer, size_t bufferSize)
{
#ifdef CTR_INTERNAL
	snprintf(buffer, bufferSize, "%s | Internal", appName);
#else
	snprintf(buffer, bufferSize, "%s", appName);
#endif
}

int Platform_GetWideMode(void)
{
	return s_videoWideMode;
}

void Platform_SetWideMode(int enabled)
{
	enabled = enabled != 0;
	if (enabled == s_videoWideMode) { return; }
	s_videoWideMode = enabled;
	s_nativeUserSettings.wideMode = enabled;
	NativeRenderer_SetDisplayAspect(enabled);
	Platform_SettingsWrite();
}

internal int Platform_QueryDisplayRefreshFPS(void)
{
	int targetFPS = NATIVE_HOST_PRESENT_HZ_FALLBACK;

	if (g_window != NULL)
	{
		const SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_window);
		const SDL_DisplayMode *mode = (displayID != 0) ? SDL_GetCurrentDisplayMode(displayID) : NULL;

		if ((mode != NULL) && (mode->refresh_rate > 30.0f))
		{
			targetFPS = (int)(mode->refresh_rate + 0.5f);
			s_highRefreshDisplayID = displayID;
		}
	}

	if (targetFPS < 30) targetFPS = 30;
	if (targetFPS > 1000) targetFPS = 1000;
	return targetFPS;
}

internal int Platform_FPSLimitModeTargetFPS(int mode)
{
	switch (mode)
	{
	case PLATFORM_FPS_LIMIT_30: return 30;
	case PLATFORM_FPS_LIMIT_60: return 60;
	case PLATFORM_FPS_LIMIT_120: return 120;
	case PLATFORM_FPS_LIMIT_144: return 144;
	case PLATFORM_FPS_LIMIT_200: return 200;
	case PLATFORM_FPS_LIMIT_240: return 240;
	case PLATFORM_FPS_LIMIT_DISPLAY_HZ: return s_displayRefreshFPS;
	case PLATFORM_FPS_LIMIT_UNLIMITED: return 0;
	default: return s_displayRefreshFPS;
	}
}

internal void Platform_RefreshHighRefreshTarget(void)
{
	const int displayFPS = Platform_QueryDisplayRefreshFPS();
	const int oldDisplayFPS = s_displayRefreshFPS;
	int targetFPS;

	s_displayRefreshFPS = displayFPS;
	targetFPS = Platform_FPSLimitModeTargetFPS(s_fpsLimitMode);

	if (displayFPS != oldDisplayFPS)
	{
		Platform_LogWarn("[CTR Native] display refresh changed: %d HZ\n", displayFPS);
	}

	if (targetFPS != s_highRefreshTargetFPS)
	{
		s_highRefreshTargetFPS = targetFPS;
		s_nextHighRefreshFrameCounter = 0;
		s_highRefreshFrameRemainder = 0;
		if (s_fpsLimitMode == PLATFORM_FPS_LIMIT_UNLIMITED)
			Platform_LogWarn("[CTR Native] frame limiter target: Unlimited\n");
		else if (s_fpsLimitMode == PLATFORM_FPS_LIMIT_DISPLAY_HZ)
			Platform_LogWarn("[CTR Native] frame limiter target: %d HZ (display)\n", targetFPS);
		else
			Platform_LogWarn("[CTR Native] frame limiter target: %d FPS\n", targetFPS);
	}
}

int Platform_GetHighRefreshMode(void)
{
	/* Compatibility name: the native variable-rate render loop is always on.
	 * FPS limiting is now independent from the physical display refresh. */
	return 1;
}

int Platform_GetHighRefreshTargetFPS(void)
{
	return s_highRefreshTargetFPS;
}

int Platform_GetFPSLimitMode(void)
{
	return s_fpsLimitMode;
}

int Platform_GetDisplayRefreshFPS(void)
{
	return s_displayRefreshFPS;
}

u64 Platform_GetHostPresentCount(void)
{
	return s_hostPresentCount;
}

void Platform_SetFPSLimitMode(int mode)
{
	if (mode < 0) mode = PLATFORM_FPS_LIMIT_COUNT - 1;
	if (mode >= PLATFORM_FPS_LIMIT_COUNT) mode = 0;
	if (mode == s_fpsLimitMode)
		return;

	s_fpsLimitMode = mode;
	g_cfg_highRefreshPresentation = 1;
	s_nativeUserSettings.highRefresh = 1;
	s_nativeUserSettings.fpsLimitMode = mode;
	Platform_SettingsWrite();

	Platform_RefreshHighRefreshTarget();
	s_hostPresentNextCounter = 0;
	s_unlimitedPresentNextCounter = 0;
	s_hostInterpolationStartCounter = 0;
	s_hostInterpolationActive = 0;
	s_nextHighRefreshFrameCounter = 0;
	s_highRefreshFrameRemainder = 0;
	s_highRefreshLastDisplayCheckCounter = 0;
	s_legacy30AccumulatorMS = 0;
	s_legacy30Ticks = 1;
	NativeGpu_PresentationReset();

	if (mode == PLATFORM_FPS_LIMIT_UNLIMITED)
		Platform_LogWarn("[CTR Native] FPS limit: Unlimited (display HZ ignored for pacing)\n");
	else if (mode == PLATFORM_FPS_LIMIT_DISPLAY_HZ)
		Platform_LogWarn("[CTR Native] FPS limit: Display HZ (%d HZ)\n", s_displayRefreshFPS);
	else
		Platform_LogWarn("[CTR Native] FPS limit: %d FPS\n", s_highRefreshTargetFPS);
}

void Platform_SetHighRefreshMode(int enabled)
{
	/* Legacy callers map cleanly onto the new limiter: OFF means a 30 FPS
	 * native loop, ON means follow the physical display refresh. */
	Platform_SetFPSLimitMode(enabled ? PLATFORM_FPS_LIMIT_DISPLAY_HZ : PLATFORM_FPS_LIMIT_30);
}

int Platform_GetSubpixelMode(void)
{
	return g_cfg_subpixelGeometry;
}

void Platform_SetSubpixelMode(int enabled)
{
	enabled = enabled != 0;
	if (enabled == g_cfg_subpixelGeometry)
	{
		return;
	}
	if (!enabled)
	{
		NativeGpu_LogSubpixelStats();
	}
	g_cfg_subpixelGeometry = enabled;
	s_nativeUserSettings.subpixel = enabled;
	Platform_SettingsWrite();
	NativeGpu_ResetSubpixelStats();
	Platform_LogWarn("[CTR Native] subpixel geometry: %s\n", enabled ? "ON" : "OFF");
}

internal void Platform_HandleWindowResize(int width, int height)
{
	Platform_SuppressMouseUiMotion();
	g_windowWidth = width;
	g_windowHeight = height;
	if ((g_window == NULL) || ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) == 0))
	{
		s_nativeUserSettings.windowWidth = width;
		s_nativeUserSettings.windowHeight = height;
		Platform_SettingsWrite();
	}
	NativeRenderer_ResetDevice();
}

internal void Platform_UpdateCursorVisibility(void)
{
	if (g_window == NULL)
	{
		return;
	}

/* Temporary touch-UI mode needs a visible cursor in windowed and fullscreen. */
	SDL_ShowCursor();
}

internal void Platform_HandleFullscreenToggle(void)
{
	Platform_SetFullscreenMode(!Platform_GetFullscreenMode());
}

internal void Platform_UpdateHostAltKeyState(const s32 key, const s8 down)
{
	s32 altKeyBit = 0;

	if (key == SDL_SCANCODE_LALT)
	{
		altKeyBit = HOST_ALT_LEFT;
	}
	else if (key == SDL_SCANCODE_RALT)
	{
		altKeyBit = HOST_ALT_RIGHT;
	}

	if (altKeyBit == 0)
	{
		return;
	}

	if (down != 0)
	{
		s_hostAltKeyState |= altKeyBit;
	}
	else
	{
		s_hostAltKeyState &= ~altKeyBit;
	}
}

#if defined(CTR_INTERNAL)
internal void Platform_TakeScreenshot(void)
{
	u8 *pixels = (u8 *)malloc(g_windowWidth * g_windowHeight * 4);

	glReadPixels(0, 0, g_windowWidth, g_windowHeight, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	SDL_Surface *surface = SDL_CreateSurfaceFrom(g_windowWidth, g_windowHeight, SDL_PIXELFORMAT_BGRA8888, pixels, g_windowWidth * 4);

	SDL_SaveBMP(surface, "SCREENSHOT.BMP");
	SDL_DestroySurface(surface);

	free(pixels);
}
#endif

internal void Platform_HandleKey(int key, char down)
{
	if (down == 0)
	{
		SubmitName_UseKeyboard(0);
	}
	else
	{
		SubmitName_UseKeyboard(key);
	}

#ifdef CTR_INTERNAL
	if (!down)
	{
		switch (key)
		{
		case SDL_SCANCODE_F1:
			g_dbg_wireframeMode ^= 1;
			Platform_LogWarn("[CTR Native] wireframe mode: %d\n", g_dbg_wireframeMode);
			break;

		case SDL_SCANCODE_F2:
			g_dbg_texturelessMode ^= 1;
			Platform_LogWarn("[CTR Native] textureless mode: %d\n", g_dbg_texturelessMode);
			break;
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
			if (g_dbg_emulatorPaused)
			{
				g_dbg_polygonSelected += (key == SDL_SCANCODE_UP) ? 3 : -3;
			}
			break;
		case SDL_SCANCODE_F9:
			if (NativeReplayScheduler_RequestStart() != 0)
			{
				break;
			}
			break;
		case SDL_SCANCODE_F10:
			NativeReplayScheduler_RequestStop();
			break;
		case SDL_SCANCODE_F7:
			Platform_LogWarn("[CTR Native] saving VRAM.TGA\n");
			NativeRenderer_SaveVRAM("VRAM.TGA", 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
			break;
		case SDL_SCANCODE_F12:
			Platform_LogWarn("[CTR Native] Saving screenshot...\n");
			Platform_TakeScreenshot();
			break;
		case SDL_SCANCODE_F3:
			Platform_SetTextureFilteringMode(!Platform_GetTextureFilteringMode());
			break;
		case SDL_SCANCODE_F5:
			NativeSaveState_RequestSave();
			break;
		case SDL_SCANCODE_F8:
			NativeSaveState_RequestLoad();
			break;
		}
	}
#endif
}

void Platform_Init(const char *title, int width, int height)
{
	char windowName[128];
	Platform_SettingsLoad(width, height);
	width = s_nativeUserSettings.windowWidth;
	height = s_nativeUserSettings.windowHeight;

	Platform_LogInit(title);
	Platform_GetWindowName(title, windowName, sizeof(windowName));

#ifdef _WIN32
	{
		DWORD priority = HIGH_PRIORITY_CLASS;
#if defined(CTR_INTERNAL)
		FILE *benchmark = fopen("debug/perf.autorun", "rb");
		if (benchmark != NULL)
		{
			fclose(benchmark);
			/* Don't starve unrelated apps when the user is working. */
			priority = NORMAL_PRIORITY_CLASS;
		}
#endif
		if (SetPriorityClass(GetCurrentProcess(), priority))
			Platform_Log("[CTR Native] process priority: %s\n",
				priority == HIGH_PRIORITY_CLASS ? "HIGH" : "NORMAL (unattended benchmark)");
		else
			Platform_LogWarn("[CTR Native] process priority setup failed: %lu\n", (unsigned long)GetLastError());
	}
#endif

	Platform_Log("[CTR Native] Initialising platform\n");
	Platform_Log("[CTR Native] BUILD: Crash Forge Racing %s EXPERIMENTAL\n", CTR_NATIVE_VERSION);
	Platform_Log("[CTR Native] startup options: highRefresh=%d subpixel=%d filtering=%s wide=%d fullscreen=%d dithering=%s perfOverlay=%d\n",
	             g_cfg_highRefreshPresentation, g_cfg_subpixelGeometry,
	             g_cfg_bilinearFiltering ? "BILINEAR" : "NEAREST",
	             s_videoWideMode, s_nativeUserSettings.fullscreen,
	             g_cfg_psxDithering ? "PS1" : "OFF",
	             s_perfOverlayEnabled);

	if (SDL_Init(SDL_INIT_VIDEO) == 0)
	{
		Platform_LogError("[CTR Native] Failed to initialise SDL\n");
		Platform_LogShutdown();
		return;
	}

	s_platformInitialized = 1;

	if (!NativeRenderer_InitialiseRender(windowName, width, height, s_nativeUserSettings.fullscreen))
	{
		Platform_LogError("[CTR Native] Failed to initialise window\n");
		Platform_Shutdown();
		return;
	}

	if (!NativeRenderer_InitialisePSX())
	{
		Platform_LogError("[CTR Native] Failed to initialise PSX renderer state\n");
		Platform_Shutdown();
		return;
	}

	NativeRenderer_SetDisplayAspect(s_videoWideMode);
	if (s_nativeUserSettings.fullscreen)
		Platform_SetFullscreenMode(1);
	Platform_RefreshHighRefreshTarget();
	Platform_Log("[CTR Native] physical display: %d HZ\n", s_displayRefreshFPS);
	if (s_fpsLimitMode == PLATFORM_FPS_LIMIT_UNLIMITED)
		Platform_Log("[CTR Native] FPS limit: Unlimited\n");
	else if (s_fpsLimitMode == PLATFORM_FPS_LIMIT_DISPLAY_HZ)
		Platform_Log("[CTR Native] FPS limit: %d HZ (display)\n", s_highRefreshTargetFPS);
	else
		Platform_Log("[CTR Native] FPS limit: %d FPS\n", s_highRefreshTargetFPS);

	atexit(Platform_Shutdown);
	Platform_UpdateCursorVisibility();
	Platform_InputInit();
}

void Platform_RequestExit(void)
{
	/* Use the same clean shutdown path as closing the host window. The atexit
	   handler persists settings and releases native renderer/audio/input state. */
	exit(0);
}

void Platform_Shutdown(void)
{
	if (s_platformInitialized == 0)
	{
		return;
	}

	Platform_SettingsWrite();
	s_platformInitialized = 0;
#if defined(CTR_INTERNAL)
	NativePerf_Shutdown();
	NativeReplayScheduler_Shutdown();
#endif
	Platform_InputShutdown();

	/* External racer textures belong to the live GL context. Release them
	 * before the window/context is destroyed, then discard registry metadata. */
	NativeGltf_Shutdown();
	NativeObj_Shutdown();
	CharacterRegistry_Shutdown();

	if (g_window != NULL)
	{
		SDL_DestroyWindow(g_window);
		g_window = NULL;
	}

	NativeGpu_LogSubpixelStats();
	NativeAudio_Shutdown();
	NativeRenderer_Shutdown();

	SDL_Quit();

	Platform_LogShutdown();
}

void Platform_BeginFrame(void)
{
	// Keep presentation of the previous completed frame alive through the
	// upcoming RenderVSYNC stall. The new GPU snapshot starts later, in
	// Platform_BeginScene(), after that wait has completed.

	// Start a fresh native-only sidecar generation. PS1 packet memory and GTE
	// registers remain untouched; this only lets the renderer associate the
	// current frame's packet XY stores with their pre-quantized 16.16 positions.
	NativeGTE_SubpixelBeginFrame();

	// NOTE(aalhendi): Normal rendering begins from DrawOTag after the current
	// draw env is installed. Starting a host scene here clears the previous env
	// and can force the host GL driver to block before the retail render-submit path.
}

int Platform_BeginScene(void)
{
	if (s_platformBeginScene)
	{
		return 0;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);

	// High refresh now runs the real game/render loop at the requested rate.
	// The old presentation-only interpolation path is intentionally inactive.
	s_hostInterpolationActive = 0;
	s_hostPresentNextCounter = 0;

	// NOTE(aalhendi): CTR already throttles through the retail VSync/draw-sync
	// path. Do not add a second SDL swap wait; some GL drivers charge that wait
	// to the next frame's first clear instead of SDL_GL_SwapWindow.
	NativeRenderer_UpdateSwapIntervalState(0);

	NativeRenderer_BeginScene();

	if (activeDrawEnv.isbg)
	{
		const RECT16 clipenv = activeDrawEnv.clip;
		const u8 r = activeDrawEnv.r0;
		const u8 g = activeDrawEnv.g0;
		const u8 b = activeDrawEnv.b0;

		NativeRenderer_Clear(clipenv.x, clipenv.y, clipenv.w, clipenv.h, r, g, b);
	}

	s_platformBeginScene = 1;

	// Keep the normal log useful without forcing a synchronous file flush
	// every rendered frame. Warnings/errors still flush immediately.
	{
		static u32 s_logFlushFrameCounter;
		s_logFlushFrameCounter++;
		if ((s_logFlushFrameCounter & 0x7f) == 0)
		{
			Platform_LogFlush();
		}
	}

	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	return 1;
}

internal int Platform_UnlimitedPresentDecoupleMarkerEnabled(void)
{
	if (!s_unlimitedPresentDecoupleChecked)
	{
		FILE *marker = fopen("debug/unlimited_present_decouple.enable", "rb");
		s_unlimitedPresentDecoupleEnabled = marker != NULL;
		if (marker != NULL)
			fclose(marker);
		s_unlimitedPresentDecoupleChecked = 1;
		if (s_unlimitedPresentDecoupleEnabled)
		{
			Platform_LogWarn(
				"[CTR Native] Unlimited present decouple: ON (physical swap target %d HZ)\n",
				s_displayRefreshFPS);
		}
	}

	return s_unlimitedPresentDecoupleEnabled;
}

internal int Platform_ShouldSwapWindowThisFrame(void)
{
	u64 frequency;
	u64 now;
	u64 period;
	int presentHz;

	if ((s_fpsLimitMode != PLATFORM_FPS_LIMIT_UNLIMITED) ||
	    !Platform_UnlimitedPresentDecoupleMarkerEnabled())
	{
		s_unlimitedPresentNextCounter = 0;
		return 1;
	}

	frequency = SDL_GetPerformanceFrequency();
	presentHz = s_displayRefreshFPS;
	if ((frequency == 0) || (presentHz <= 0))
	{
		return 1;
	}

	if (presentHz > 1000)
		presentHz = 1000;

	now = SDL_GetPerformanceCounter();
	period = (frequency + (u64)(presentHz / 2)) / (u64)presentHz;
	if (period == 0)
		period = 1;

	if ((s_unlimitedPresentNextCounter != 0) &&
	    (now < s_unlimitedPresentNextCounter))
	{
		return 0;
	}

	/* Rebase instead of catching up after a delayed compositor frame. The
	 * latest fully-rendered back buffer is what matters; stale presents do not. */
	s_unlimitedPresentNextCounter = now + period;
	return 1;
}

internal void Platform_SwapWindowTracked(void)
{
	NativeRenderer_SwapWindow();
	s_hostPresentCount++;
	s_hostPresentNextCounter = 0;
}

void Platform_EndScene(void)
{
	if (!s_platformBeginScene)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
	s_platformBeginScene = 0;

	NativeRenderer_EndScene();

	if (s_pinnedVramDisplayFrames > 0)
	{
		s_hostInterpolationActive = 0;
		s_hostPresentNextCounter = 0;
		// NOTE(aalhendi): Direct VRAM presentation skips StoreFrameBuffer.
		// Do not let the next DrawSync read stale framebuffer texture data back
		// into PSX VRAM after a movie/frame upload.
		NativeRenderer_DiscardFramebufferReadback();
		if (s_pinnedVramDisplayCustomRect)
		{
			NativeRenderer_PresentVRAMRect(s_pinnedVramDisplayX, s_pinnedVramDisplayY, s_pinnedVramDisplayW, s_pinnedVramDisplayH);
		}
		else
		{
			NativeRenderer_PresentVRAMDisplay();
		}
		Platform_SwapWindowTracked();
		s_pinnedVramDisplayFrames--;
		if (s_pinnedVramDisplayFrames <= 0)
		{
			s_pinnedVramDisplayCustomRect = 0;
		}
		NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
		return;
	}

	NativeRenderer_StoreFrameBufferDeferred(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);

	s_hostInterpolationStartCounter = 0;
	s_hostInterpolationActive = 0;
	if (Platform_ShouldSwapWindowThisFrame())
	{
		Platform_SwapWindowTracked();
	}
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
}

// NOTE(aalhendi): Frame timing is handled by VSync() in the platform layer,
// matching PS1 hardware behavior. Platform_EndFrame only does buffer swap + FPS.
void Platform_EndFrame(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
	Platform_EndScene();
	Platform_CalcFPS();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
}

void Platform_PresentVRAMDisplay(void)
{
	Platform_BeginScene();
	NativeRenderer_PresentVRAMDisplay();
	Platform_EndFrame();
}

void Platform_PinVRAMDisplayFrames(int frameCount)
{
	if (frameCount > s_pinnedVramDisplayFrames)
	{
		s_pinnedVramDisplayFrames = frameCount;
		s_pinnedVramDisplayCustomRect = 0;
	}
}

void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount)
{
	if ((frameCount <= 0) || (w <= 0) || (h <= 0))
	{
		return;
	}

	s_pinnedVramDisplayX = x;
	s_pinnedVramDisplayY = y;
	s_pinnedVramDisplayW = w;
	s_pinnedVramDisplayH = h;
	s_pinnedVramDisplayFrames = frameCount;
	s_pinnedVramDisplayCustomRect = 1;
}

void Platform_StartTextInput(void)
{
	if (g_window != NULL)
		SDL_StartTextInput(g_window);
}

void Platform_StopTextInput(void)
{
	if (g_window != NULL)
		SDL_StopTextInput(g_window);
}

void Platform_PollHostEvents(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_GAMEPAD_ADDED:
			Platform_InputControllerAdded(event.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			Platform_InputControllerRemoved(event.gdevice.which);
			break;
		case SDL_EVENT_QUIT:
			exit(0);
			break;
		case SDL_EVENT_WINDOW_RESIZED:
			Platform_HandleWindowResize(event.window.data1, event.window.data2);
			break;
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			Platform_SuppressMouseUiMotion();
			Platform_UpdateCursorVisibility();
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			exit(0);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			/* SDL/Windows can move the cursor as a side effect of a resolution or
			   fullscreen transition. Do not treat that synthetic recenter as a
			   request to take menu focus away from the controller. */
			if ((SDL_GetTicks() >= s_mouseUiIgnoreMotionUntil) &&
			    ((event.motion.xrel != 0.0f) ||
			     (event.motion.yrel != 0.0f)))
			{
				s_mouseUiActivity = 1;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			s_mouseUiActivity = 1;
			if (event.button.button == SDL_BUTTON_LEFT)
				s_mouseUiTapMask |= PLATFORM_MOUSE_UI_LEFT;
			else if (event.button.button == SDL_BUTTON_RIGHT)
				s_mouseUiTapMask |= PLATFORM_MOUSE_UI_RIGHT;
			else if (event.button.button == SDL_BUTTON_MIDDLE)
				s_mouseUiTapMask |= PLATFORM_MOUSE_UI_MIDDLE;
			break;
		case SDL_EVENT_TEXT_INPUT:
			if (!MainFreeze_NativeControlsHandleTextInput(event.text.text))
				NativeHangout_HandleTextInput(event.text.text);
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = event.key.scancode;
			char down = (event.type == SDL_EVENT_KEY_UP) ? 0 : 1;

			Platform_UpdateHostAltKeyState(key, down);

			if (key == SDL_SCANCODE_F11)
			{
				if ((down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if ((key == SDL_SCANCODE_RETURN) &&
			    (s_hostAltKeyState != 0) &&
			    (down != 0) &&
			    (event.key.repeat == 0))
			{
				Platform_HandleFullscreenToggle();
				break;
			}

			if (MainFreeze_NativeControlsHandleHostKey(
			        key,
			        down,
			        event.key.repeat))
				break;

			if (NativeHangout_HandleHostKey(key, down, event.key.repeat))
				break;

			if (key == SDL_SCANCODE_RETURN)
				break;

			if (key == SDL_SCANCODE_RSHIFT)
			{
				key = SDL_SCANCODE_LSHIFT;
			}
			else if (key == SDL_SCANCODE_RCTRL)
			{
				key = SDL_SCANCODE_LCTRL;
			}
			else if (key == SDL_SCANCODE_RALT)
			{
				key = SDL_SCANCODE_LALT;
			}

#ifdef CTR_INTERNAL
			/* Hidden local Hangout shortcuts. Function keys stay untouched. */
			if ((down != 0) &&
			    (event.key.repeat == 0) &&
			    ((event.key.mod & SDL_KMOD_CTRL) != 0) &&
			    ((event.key.mod & SDL_KMOD_ALT) != 0) &&
			    ((event.key.mod & SDL_KMOD_SHIFT) != 0))
			{
				if (key == SDL_SCANCODE_H)
				{
					NativeHangout_RequestLaunch();
					Platform_LogWarn("[Hangout] LOCAL requested (Ctrl+Alt+Shift+H)\n");
					break;
				}
				if (key == SDL_SCANCODE_X)
				{
					NativeHangout_RequestExit();
					Platform_LogWarn("[Hangout] exit requested (Ctrl+Alt+Shift+X)\n");
					break;
				}
			}
#endif

			/*
			 * Hidden experimental Z-buffer. This is intentionally absent from
			 * every public menu and is never persisted between launches.
			 * Text-entry/rebind handlers above keep first ownership.
			 */
			if ((key == SDL_SCANCODE_Z) &&
			    (down != 0) &&
			    (event.key.repeat == 0) &&
			    ((event.key.mod & SDL_KMOD_CTRL) != 0) &&
			    ((event.key.mod & SDL_KMOD_ALT) != 0) &&
			    ((event.key.mod & SDL_KMOD_SHIFT) != 0))
			{
				Platform_SetExperimentalZBufferMode(
					!Platform_GetExperimentalZBufferMode());
				break;
			}

			/* Plain H toggles the gameplay HUD/floating overlays. Text-entry
			 * handlers above get first ownership, so typing H in chat/player-name
			 * fields never toggles this state. The hidden Hangout chord also gets
			 * first ownership and therefore remains Ctrl+Alt+Shift+H. */
			if ((key == SDL_SCANCODE_H) &&
			    (down != 0) &&
			    (event.key.repeat == 0) &&
			    ((event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT)) == 0))
			{
				Platform_SetHudHidden(!Platform_GetHudHidden());
				Platform_LogWarn("[CTR Native] HUD/floating overlays: %s (H)\n",
				                 Platform_GetHudHidden() ? "HIDDEN" : "VISIBLE");
				break;
			}

			if ((key == SDL_SCANCODE_J) && (down != 0) && (event.key.repeat == 0))
			{
				s_debugFreecamEnabled = !s_debugFreecamEnabled;
				s_debugFreecamCaptured = 0;
				s_debugFreecamCameraID = -1;
				Platform_LogWarn("[CTR Native] Debug freecam %s (J, visual-only)\n", s_debugFreecamEnabled ? "ON" : "OFF");
				break;
			}

			if ((key == SDL_SCANCODE_F4) && (down == 0))
			{
#ifdef CTR_INTERNAL
				Platform_LogWarn("[CTR Native] Keyboard assigned to player %d\n", Platform_InputCycleKeyboardController());
#endif
				break;
			}

			if ((key == SDL_SCANCODE_F6) && (down == 0))
			{
#ifdef CTR_INTERNAL
				int player = Platform_InputCycleGamepadController();
				if (player == 0)
				{
					Platform_LogWarn("[CTR Native] No gamepad connected\n");
				}
				else
				{
					Platform_LogWarn("[CTR Native] Gamepad assigned to player %d\n", player);
				}
#endif
				break;
			}

			Platform_HandleKey(key, down);
			break;
		}
		}
	}
}

int Platform_DebugFreecamIsEnabled(void)
{
	return s_debugFreecamEnabled != 0;
}

int Platform_DebugFreecamConsumesKey(int key)
{
	/* J is permanently reserved for the debug camera toggle. While freecam is
	   active, its movement/look keys must never leak into the player's kart. */
	if (key == SDL_SCANCODE_J)
		return 1;

	if (!s_debugFreecamEnabled)
		return 0;

	switch (key)
	{
	case SDL_SCANCODE_W:
	case SDL_SCANCODE_A:
	case SDL_SCANCODE_S:
	case SDL_SCANCODE_D:
	case SDL_SCANCODE_Q:
	case SDL_SCANCODE_E:
	case SDL_SCANCODE_R:
	case SDL_SCANCODE_I:
	case SDL_SCANCODE_K:
	case SDL_SCANCODE_U:
	case SDL_SCANCODE_O:
		return 1;
	default:
		return 0;
	}
}

void Platform_DebugFreecamUpdate(struct CameraDC *cDC, struct PushBuffer *pb)
{
	const bool *kb;
	int moveSpeed;
	int turnSpeed;
	double yaw;
	double forwardX;
	double forwardZ;
	double rightX;
	double rightZ;
	double moveX = 0.0;
	double moveZ = 0.0;

	if ((cDC == NULL) || (pb == NULL))
		return;

	if (!s_debugFreecamEnabled)
	{
		s_debugFreecamCaptured = 0;
		s_debugFreecamCameraID = -1;
		return;
	}

	/* The debug camera is a visual override of the primary view only. Most
	   importantly, it never writes CameraDC.cameraMode or any transition state.
	   CTR's real camera, race intro, traffic lights, pause state and cutscenes
	   therefore continue ticking underneath it exactly as if freecam were off. */
	if (cDC->cameraID != 0)
		return;

	if (!s_debugFreecamCaptured ||
	    s_debugFreecamCameraID != cDC->cameraID)
	{
		s_debugFreecamPos[0] = pb->pos.x;
		s_debugFreecamPos[1] = pb->pos.y;
		s_debugFreecamPos[2] = pb->pos.z;
		s_debugFreecamRot[0] = pb->rot.x;
		s_debugFreecamRot[1] = pb->rot.y;
		s_debugFreecamRot[2] = pb->rot.z;
		s_debugFreecamCaptured = 1;
		s_debugFreecamCameraID = cDC->cameraID;
	}

	kb = SDL_GetKeyboardState(NULL);
	if (kb == NULL)
		return;

	moveSpeed = kb[SDL_SCANCODE_R] ? 96 : 24;
	turnSpeed = 24;

	if (kb[SDL_SCANCODE_U]) s_debugFreecamRot[1] = (s16)((s_debugFreecamRot[1] + turnSpeed) & 0xfff);
	if (kb[SDL_SCANCODE_O]) s_debugFreecamRot[1] = (s16)((s_debugFreecamRot[1] - turnSpeed) & 0xfff);
	if (kb[SDL_SCANCODE_I]) s_debugFreecamRot[0] = (s16)((s_debugFreecamRot[0] + turnSpeed) & 0xfff);
	if (kb[SDL_SCANCODE_K]) s_debugFreecamRot[0] = (s16)((s_debugFreecamRot[0] - turnSpeed) & 0xfff);

	yaw = ((double)(s_debugFreecamRot[1] & 0xfff) *
	       (2.0 * 3.14159265358979323846)) / 4096.0;
	forwardX = -sin(yaw);
	forwardZ = -cos(yaw);
	rightX = cos(yaw);
	rightZ = -sin(yaw);

	if (kb[SDL_SCANCODE_W]) { moveX += forwardX; moveZ += forwardZ; }
	if (kb[SDL_SCANCODE_S]) { moveX -= forwardX; moveZ -= forwardZ; }
	if (kb[SDL_SCANCODE_D]) { moveX += rightX; moveZ += rightZ; }
	if (kb[SDL_SCANCODE_A]) { moveX -= rightX; moveZ -= rightZ; }

	s_debugFreecamPos[0] =
		(s16)(s_debugFreecamPos[0] + (s16)lrint(moveX * moveSpeed));
	s_debugFreecamPos[2] =
		(s16)(s_debugFreecamPos[2] + (s16)lrint(moveZ * moveSpeed));
	if (kb[SDL_SCANCODE_E])
		s_debugFreecamPos[1] = (s16)(s_debugFreecamPos[1] + moveSpeed);
	if (kb[SDL_SCANCODE_Q])
		s_debugFreecamPos[1] = (s16)(s_debugFreecamPos[1] - moveSpeed);

	pb->pos.x = s_debugFreecamPos[0];
	pb->pos.y = s_debugFreecamPos[1];
	pb->pos.z = s_debugFreecamPos[2];
	pb->rot.x = s_debugFreecamRot[0];
	pb->rot.y = s_debugFreecamRot[1];
	pb->rot.z = s_debugFreecamRot[2];
}

int Platform_PollInput(void)
{
	Platform_PollHostEvents();
	Platform_InputUpdate();
	return 1;
}

int NikoGetEnterKey(void)
{
	const bool *kb = SDL_GetKeyboardState(NULL);
	return (kb && kb[SDL_SCANCODE_RETURN]) ? 1 : 0;
}

// NOTE(aalhendi): Native owns the CTR VBlank clock instead of PsyCross's
// autonomous interrupt thread. The retail-shaped VSyncCallback storage lives in
// native_libetc.c; native VSync emits that callback at each emulated VBlank.
#define NATIVE_VSYNC_HZ          60
#define NATIVE_VSYNC_CATCHUP_MAX 8
#define NATIVE_VSYNC_SPIN_US     1000

global_variable u64 s_nextVBlankCounter = 0;
global_variable u64 s_vblankRemainder = 0;
global_variable int s_nativeVBlankCount = 0;

internal u64 Native_CounterFromMicroseconds(u64 freq, u64 microseconds)
{
	return (freq * microseconds) / 1000000;
}

internal void Native_AdvanceVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 hz = NATIVE_VSYNC_HZ;

	s_nextVBlankCounter += freq / hz;
	s_vblankRemainder += freq % hz;
	if (s_vblankRemainder >= hz)
	{
		s_nextVBlankCounter++;
		s_vblankRemainder -= hz;
	}
}

internal void Native_EnsureVBlankTarget(void)
{
	const u64 now = SDL_GetPerformanceCounter();

	if (s_nextVBlankCounter == 0)
	{
		s_nextVBlankCounter = now;
		s_vblankRemainder = 0;
		Native_AdvanceVBlankTarget();
	}
}

internal void Native_WaitUntilVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 spinWindow = Native_CounterFromMicroseconds(freq, NATIVE_VSYNC_SPIN_US);

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
	while (1)
	{
		const u64 now = SDL_GetPerformanceCounter();
		u64 remaining;
		u64 sleepMs;

		if (now >= s_nextVBlankCounter)
		{
			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		remaining = s_nextVBlankCounter - now;
		if (remaining <= spinWindow)
		{
			while (SDL_GetPerformanceCounter() < s_nextVBlankCounter)
			{
			}
			continue;
		}

		sleepMs = ((remaining - spinWindow) * 1000) / freq;
		if (sleepMs > 0)
		{
			SDL_Delay((u32)sleepMs);
		}
	}
}

internal void Native_EmitVBlank(void)
{
	NativeRCnt_EmitVBlank();

	if (vsync_callback != NULL)
	{
		vsync_callback();
	}

	NativeAudio_StepVBlank();
	s_nativeVBlankCount++;
}

internal int Native_CatchUpDueVBlanks(void)
{
	int emittedVBlanks = 0;

	Native_EnsureVBlankTarget();

	while (SDL_GetPerformanceCounter() >= s_nextVBlankCounter)
	{
		const u64 now = SDL_GetPerformanceCounter();

		Native_EmitVBlank();
		emittedVBlanks++;

		if (emittedVBlanks >= NATIVE_VSYNC_CATCHUP_MAX)
		{
			// NOTE(aalhendi): Debugger stalls can otherwise replay minutes of
			// VBlank callbacks at once. Keep normal late frames faithful, but
			// rebase pathological host pauses.
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			Native_AdvanceVBlankTarget();
			break;
		}

		Native_AdvanceVBlankTarget();
	}

	return emittedVBlanks;
}

internal void Native_AdvanceHighRefreshFrameTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 hz = (u64)s_highRefreshTargetFPS;

	if ((freq == 0) || (hz == 0))
		return;

	s_nextHighRefreshFrameCounter += freq / hz;
	s_highRefreshFrameRemainder += freq % hz;
	if (s_highRefreshFrameRemainder >= hz)
	{
		s_nextHighRefreshFrameCounter++;
		s_highRefreshFrameRemainder -= hz;
	}
}

void Platform_WaitForHighRefreshFrame(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 spinWindow = Native_CounterFromMicroseconds(freq, 500);
	u64 now;

	if (!g_cfg_highRefreshPresentation || (freq == 0))
	{
		return;
	}

	/* Unlimited is a true benchmark/stress mode: no wait against the physical
	 * monitor and no artificial target. Keep PS1 VBlank services alive at 60 Hz
	 * so input/audio/callback timing still advances independently. */
	if ((s_fpsLimitMode == PLATFORM_FPS_LIMIT_UNLIMITED) ||
	    (s_highRefreshTargetFPS <= 0))
	{
		s_nextHighRefreshFrameCounter = 0;
		s_highRefreshFrameRemainder = 0;
		Native_CatchUpDueVBlanks();
		return;
	}

	now = SDL_GetPerformanceCounter();
	if ((s_fpsLimitMode == PLATFORM_FPS_LIMIT_DISPLAY_HZ) &&
	    ((s_highRefreshLastDisplayCheckCounter == 0) ||
	     (now - s_highRefreshLastDisplayCheckCounter >= freq * 2)))
	{
		s_highRefreshLastDisplayCheckCounter = now;
		Platform_RefreshHighRefreshTarget();
	}
	if (s_nextHighRefreshFrameCounter == 0)
	{
		s_nextHighRefreshFrameCounter = now;
		s_highRefreshFrameRemainder = 0;
		Native_AdvanceHighRefreshFrameTarget();
	}

	/* Never "catch up" missed presentation deadlines.
	   The old scheduler kept advancing from the stale target after a slow
	   SwapWindow/driver/compositor frame. At 200 Hz a 20 ms stall left several
	   deadlines in the past, so the next few host frames ran almost unthrottled
	   to catch up. That produced the visible 200 -> 120 -> 230 -> 170 FPS
	   sawtooth even when average throughput was high.

	   If this frame is already late, rebase the phase to now. The advance at
	   the end of this function will schedule exactly one fresh display period
	   from the new base, giving stable pacing instead of burst recovery. */
	if (now > s_nextHighRefreshFrameCounter)
	{
		s_nextHighRefreshFrameCounter = now;
		s_highRefreshFrameRemainder = 0;
	}

	now = SDL_GetPerformanceCounter();
	if (now < s_nextHighRefreshFrameCounter)
	{
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_HIGH_REFRESH_WAIT);
		while ((now = SDL_GetPerformanceCounter()) < s_nextHighRefreshFrameCounter)
		{
			u64 remaining;
			u64 sleepMs;

			// Keep PS1 VBlank services (audio/input/callbacks) at 60 Hz independently
			// while the actual game/render loop follows the active display refresh.
			Native_CatchUpDueVBlanks();

			remaining = s_nextHighRefreshFrameCounter - now;
			if (remaining <= spinWindow)
			{
				while (SDL_GetPerformanceCounter() < s_nextHighRefreshFrameCounter)
				{
				}
				break;
			}

			sleepMs = ((remaining - spinWindow) * 1000) / freq;
			if (sleepMs > 0)
			{
				SDL_Delay((u32)sleepMs);
			}
		}
		NativePerf_EndScope(NATIVE_PERF_BUCKET_HIGH_REFRESH_WAIT);
	}

	Native_CatchUpDueVBlanks();
	Native_AdvanceHighRefreshFrameTarget();
}

internal void Native_WaitAndEmitVBlank(void)
{
	Native_EnsureVBlankTarget();
	Native_WaitUntilVBlankTarget();
	Native_EmitVBlank();
	Native_AdvanceVBlankTarget();
}

int VSync(int mode)
{
	int requestedVBlanks;
	int emittedVBlanks;

	if (mode < 0)
	{
		return s_nativeVBlankCount;
	}

	requestedVBlanks = (mode == 0) ? 1 : mode;
	emittedVBlanks = 0;

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return s_nativeVBlankCount;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	for (s32 i = 0; i < requestedVBlanks; i++)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif

	return s_nativeVBlankCount;
}

int Platform_GetVBlankCount(void)
{
	return s_nativeVBlankCount;
}

void Platform_WaitUntilVBlank(int targetVBlank)
{
	int emittedVBlanks = 0;
	int requestedVBlanks = targetVBlank - s_nativeVBlankCount;

	if (requestedVBlanks <= 0)
	{
		return;
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	while (s_nativeVBlankCount < targetVBlank)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif
}
