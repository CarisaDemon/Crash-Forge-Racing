#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#if __GNUC__
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()
#endif

#include "platform/native_assets.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_jobs.h"
#include "platform/native_wheel_jobs.h"
#include "platform/native_perf.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"
#include "platform/native_pc_render.h"

#ifndef __GNUC__
#define __attribute__(x)
#endif

#include <platform.h>

#include "game/game_unity.h"

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"
#include "platform/native_world.c"

#undef RECT

#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_mod_packages.c"
#include "platform/native_custom_tracks.c"
#include "platform/native_music_selection.c"
#include "platform/native_localization.c"
#include "platform/native_audio.c"

#include "platform/native_menu_music_clock.c"
#include "platform/native_custom_music.c"
#include "platform/native_engine_audio.c"
#include "platform/native_jobs.c"
#include "platform/native_occlusion_async.c"
#include "platform/native_wheel_jobs.c"
#include "platform/native_memory.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_pc_render.c"
#include "platform/native_gte_core.c"
#include "platform/native_glad.c"
#include "platform/native_input.c"
#include "platform/native_input_glyphs.c"
#include "platform/native_inline_c.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_renderer.c"
#include "platform/native_level_pc.c"
#include "platform/native_level_gpu_projection_qa.c"
#include "platform/native_level_gpu_midpoint_qa.c"
#include "platform/native_gltf.c"
#include "platform/native_obj.c"
#include "platform/native_gpu_model_qa.c"
#include "platform/native_savestate.c"
#include "platform/native_state.c"
#include "platform/native_str.c"

#ifndef CC
#if __GNUC__
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

static s32 NativeConsole_Return(const u32 result)
{
	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}



int main(int argc, char *argv[])
{
#if defined(CTR_WIN64)
	{
		// P32 pointers keep only 32 bits: refuse to run if code, stack or heap ended up above 2 GB.
		void *probe = malloc(64u << 20);
		const uintptr_t limit = 0x80000000u;
		if (((uintptr_t)&probe >= limit) || ((uintptr_t)probe >= limit) || ((uintptr_t)(void *)&main >= limit))
		{
			fprintf(stderr, "CTR Native Win64: memory above 2 GB (exe must be built without LARGEADDRESSAWARE)\n");
			return 3;
		}
		free(probe);
	}
#endif
#if defined(CTR_NATIVE)
	NativeSession_ConfigureFromArgs(argc, argv);
#endif

	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			printf("CTR Native %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}
#if defined(CTR_NATIVE)
		/* Launcher feature probe: do not initialize SDL, scan mods or access
		 * the original disc. Clean launch is supported from this version. */
		if (strcmp(argv[argIndex], "--supports-no-mods") == 0)
		{
			puts("CFR_NO_MODS_V1");
			return 0;
		}
#endif
	}

#if defined(CTR_NATIVE)
	if (NativeSession_NoModsActive())
		printf("[CFR CLEAN] --no-mods: community racers, karts, wheels, tracks and soundpacks disabled for this process\n");
#endif
	printf("[CTR Native] Starting...\n");
	fflush(stdout);

	const char *sdlBasePath = SDL_GetBasePath();
	printf("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");
	fflush(stdout);

	if (!NativeAssets_Init(sdlBasePath))
	{
		fprintf(stderr, "[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}

	printf("[CTR Native] Version: %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("[CTR Native] Built with: " CC "\n");
	printf("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	printf("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());
	fflush(stdout);

	if (chdir(NativeAssets_GetBaseDir()) != 0)
	{
		fprintf(stderr, "[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_NATIVE)
	/* Fast, non-graphical verification for creator-installed LEV/VRM packs.
	   Does not start the game or touch keyboard/mouse. */
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], "--list-custom-tracks") == 0)
		{
			NativeCustomTracks_Scan();
			printf("[CustomTracks] found=%d\\n", NativeCustomTracks_Count());
			for (int trackIndex = 0; trackIndex < NativeCustomTracks_Count(); trackIndex++)
			{
				printf("[CustomTracks] %d: %s\\n", trackIndex,
				       NativeCustomTracks_Name(trackIndex));
			}
			fflush(stdout);
			return 0;
		}
	}
#endif

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

#ifdef USE_16BY9
	printf("[CTR Native] Widescreen\n");
	Platform_Init("Crash Team Racing", 1920, 1080);
#else
	printf("[CTR Native] 4:3\n");
	Platform_Init("Crash Team Racing", 800, 600);
#endif

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#endif

	NativeJobs_Init();

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		NativeJobs_Shutdown();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	const int result = CTR_Main();

#if defined(CTR_NATIVE)
	/* Release host-side mod resources while the renderer/context still exists. */
#if defined(CTR_CSEQ_LIBERATION)
	NativeMusicRegistry_Shutdown();
#endif
	NativeOccAsync_Shutdown();
	NativeObj_Shutdown();
	CharacterRegistry_Shutdown();
#endif

	NativeWheelJobs_Shutdown();
	NativeJobs_Shutdown();
	Platform_Shutdown();
	return NativeConsole_Return(result);
}
