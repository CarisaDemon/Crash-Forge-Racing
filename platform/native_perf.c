#include "platform/native_perf.h"

#if defined(CTR_INTERNAL)
#include <macros.h>
#include <common.h>
#include <CharacterRegistry.h>
#include <VehiclePartRegistry.h>
#include <RacerLoadout.h>

#include "platform/native_audio.h"
#include "platform/native_log.h"
#include "platform/native_path.h"
#include "platform/native_renderer.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define NATIVE_PERF_PATH_MAX         1024
#define NATIVE_PERF_DEFAULT_ROOT     "debug/perf"
#define NATIVE_PERF_FRAME_CSV_NAME   "frame_times.csv"
#define NATIVE_PERF_SUMMARY_NAME     "summary.txt"
#define NATIVE_PERF_RUNTIME_LOG_NAME "runtime.log"
#define NATIVE_PERF_TOP_FRAME_COUNT  20
#define NATIVE_PERF_FLUSH_INTERVAL   1000
#define NATIVE_PERF_CSV_BUFFER_BYTES (4 * 1024 * 1024)
#define NATIVE_PERF_BOT_CSV_BUFFER_BYTES (512 * 1024)
#define NATIVE_PERF_BOT_COUNT 8
#define NATIVE_PERF_FRAME_WARN_40_MS 40.0
#define NATIVE_PERF_FRAME_WARN_50_MS 50.0

struct NativePerfBucketInfo
{
	const char *name;
	s32 dominantCandidate;
};

struct NativePerfWorstFrame
{
	u32 frameIndex;
	f64 totalMs;
	f64 workMs;
	f64 budgetMs;
	f64 overBudgetMs;
	f64 workOverBudgetMs;
	enum NativePerfBucket dominantBucket;
	f64 dominantMs;
	struct NativePerfFrameInfo info;
	u32 vramWritePixels;
	u32 vramUploadCalls;
	u32 vramUploadBytes;
	u32 gpuSplits;
	u32 drawCalls;
	u32 triangles;
	f64 gpuFrameMs;
	u32 shaderSwitches;
	u32 textureBinds;
	u32 blendStateChanges;
	u32 depthStateChanges;
	u32 stencilStateChanges;
	s32 audioUnderrunDelta;
	s32 audioOverflowDelta;
	s32 audioQueuedFrames;
};

global_variable const struct NativePerfBucketInfo s_bucketInfo[NATIVE_PERF_BUCKET_COUNT] = {
    {"game_logic_ms", 1},
    {"render_frame_ms", 0},
    {"mainframe_setup_ms", 1},
    {"mainframe_effects_ms", 1},
    {"mainframe_hud_ms", 1},
    {"mainframe_queue_instances_ms", 1},
    {"mainframe_execute_instances_ms", 1},
    {"mainframe_level_geometry_ms", 1},
    {"level_renderlists_ms", 1},
    {"level_draw_ovr_ms", 1},
    {"level_sky_ms", 1},
    {"water_render_ms", 1},
    {"particle_render_ms", 1},
    {"mainframe_post_level_ms", 1},
    {"mainframe_refreshcard_ms", 1},
    {"mainframe_clear_screen_ms", 1},
    {"mainframe_ui_ms", 1},
    {"mainframe_render_vsync_ms", 0},
    {"platform_end_frame_ms", 0},
    {"platform_end_scene_ms", 0},
    {"render_submit_ms", 1},
    {"platform_begin_scene_ms", 1},
    {"draw_otag_ms", 0},
    {"draw_otag_parse_ms", 1},
    {"draw_all_splits_ms", 1},
    {"renderer_begin_scene_ms", 1},
    {"renderer_update_vram_ms", 1},
    {"renderer_vertex_upload_ms", 1},
    {"renderer_draw_triangles_ms", 1},
    {"framebuffer_store_ms", 0},
    {"framebuffer_resize_ms", 1},
    {"framebuffer_blit_ms", 1},
    {"framebuffer_flush_ms", 1},
    {"framebuffer_alloc_ms", 1},
    {"framebuffer_readback_ms", 1},
    {"framebuffer_pack_ms", 1},
    {"framebuffer_vram_upload_ms", 1},
    {"swap_window_ms", 1},
    {"vsync_wait_ms", 0},
    {"high_refresh_wait_ms", 0},
    {"audio_vblank_ms", 1},
};

struct NativePerfBotSlot
{
    u64 start[NATIVE_PERF_BOT_PHASE_COUNT];
    u64 ticks[NATIVE_PERF_BOT_PHASE_COUNT];
    u32 calls[NATIVE_PERF_BOT_PHASE_COUNT];
    int lastCharacter;
    int lastKart;
    int lastWheels;
    int lastColor;
    int logged;
};
global_variable struct NativePerfBotSlot s_botSlots[NATIVE_PERF_BOT_COUNT];
global_variable FILE *s_botCsvFile;
global_variable char s_botCsvBuffer[NATIVE_PERF_BOT_CSV_BUFFER_BYTES];
global_variable u32 s_botProfilerFrames;
global_variable FILE *s_csvFile;
global_variable char s_csvBuffer[NATIVE_PERF_CSV_BUFFER_BYTES];
global_variable s32 s_enabled;
global_variable s32 s_frameOpen;
global_variable s32 s_rowsSinceFlush;
global_variable u32 s_frameIndex;
global_variable u32 s_framesOverBudget;
global_variable u32 s_workFramesOverBudget;
global_variable u32 s_framesOver40;
global_variable u32 s_framesOver50;
global_variable f64 s_totalFrameMs;
global_variable f64 s_totalWorkMs;
global_variable f64 s_totalBudgetMs;
global_variable s32 s_targetFPSMin;
global_variable s32 s_targetFPSMax;
global_variable f64 s_maxFrameMs;
global_variable f64 s_maxWorkMs;
global_variable f64 s_bucketTotals[NATIVE_PERF_BUCKET_COUNT];
global_variable f64 s_frameBucketMs[NATIVE_PERF_BUCKET_COUNT];
global_variable u32 s_frameVRAMWritePixels;
global_variable u32 s_frameVRAMUploadCalls;
global_variable u32 s_frameVRAMUploadBytes;
global_variable u32 s_frameGpuSplits;
global_variable u32 s_frameDrawCalls;
global_variable u32 s_frameTriangles;
global_variable u32 s_frameShaderSwitches;
global_variable u32 s_frameTextureBinds;
global_variable u32 s_frameBlendStateChanges;
global_variable u32 s_frameDepthStateChanges;
global_variable u32 s_frameStencilStateChanges;
global_variable u64 s_totalVRAMWritePixels;
global_variable u64 s_totalVRAMUploadCalls;
global_variable u64 s_totalVRAMUploadBytes;
global_variable u64 s_totalGpuSplits;
global_variable u64 s_totalDrawCalls;
global_variable u64 s_totalTriangles;
global_variable f64 s_totalGpuFrameMs;
global_variable u32 s_gpuFrameSampleCount;
global_variable u64 s_totalShaderSwitches;
global_variable u64 s_totalTextureBinds;
global_variable u64 s_totalBlendStateChanges;
global_variable u64 s_totalDepthStateChanges;
global_variable u64 s_totalStencilStateChanges;
global_variable u64 s_frameStartCounter;
global_variable u64 s_scopeStartCounter[NATIVE_PERF_BUCKET_COUNT];
global_variable s32 s_scopeDepth[NATIVE_PERF_BUCKET_COUNT];
global_variable struct NativePerfFrameInfo s_frameBeginInfo;
global_variable s32 s_beginUnderrunFrames;
global_variable s32 s_beginOverflowFrames;
global_variable s32 s_beginQueuedFrames;
global_variable char s_outputDir[NATIVE_PERF_PATH_MAX];
global_variable char s_csvPath[NATIVE_PERF_PATH_MAX];
global_variable char s_summaryPath[NATIVE_PERF_PATH_MAX];
global_variable char s_runtimeLogPath[NATIVE_PERF_PATH_MAX];
global_variable struct NativePerfWorstFrame s_worstFrames[NATIVE_PERF_TOP_FRAME_COUNT];
global_variable s32 s_worstFrameCount;
global_variable s32 s_stressTestActive;
global_variable s32 s_stressTestMeasuring;
global_variable u32 s_stressStartFrameIndex;
global_variable u32 s_stressStopAfterFrames;
global_variable f64 s_stressStartTotalFrameMs;
global_variable f64 s_stressStartTotalWorkMs;
global_variable f64 s_stressStartExecuteInstancesMs;
global_variable f64 s_stressStartLevelGeometryMs;
global_variable f64 s_stressStartRenderSubmitMs;
global_variable f64 s_stressStartSwapWindowMs;
global_variable f64 s_stressStartUpdateVramMs;
global_variable u64 s_stressStartDrawCalls;
global_variable u64 s_stressStartTriangles;

void NativePerf_SetStressTestActive(int active)
{
	active = active != 0;

	if (active)
	{
		FILE *limit = fopen("debug/perf.stop_after_frames", "rb");
		s_stressTestActive = 1;
		s_stressTestMeasuring = 0;
		s_stressStopAfterFrames = 0;
		if (limit != NULL)
		{
			unsigned int parsed = 0;
			if (fscanf(limit, "%u", &parsed) == 1 &&
			    parsed >= 5000 && parsed <= 200000)
				s_stressStopAfterFrames = (u32)parsed;
			fclose(limit);
			Platform_Log("[PERF TEST] frame-limited run=%u measured frames\n",
			             s_stressStopAfterFrames);
		}
		return;
	}

	if (s_stressTestActive && s_stressTestMeasuring)
	{
		const u32 frames = s_frameIndex - s_stressStartFrameIndex;

		if (frames > 0)
		{
			const f64 avgFrameMs =
				(s_totalFrameMs - s_stressStartTotalFrameMs) / (f64)frames;
			const f64 avgWorkMs =
				(s_totalWorkMs - s_stressStartTotalWorkMs) / (f64)frames;
			const f64 avgInstances =
				(s_bucketTotals[NATIVE_PERF_BUCKET_MAINFRAME_EXECUTE_INSTANCES] -
				 s_stressStartExecuteInstancesMs) / (f64)frames;
			const f64 avgGeometry =
				(s_bucketTotals[NATIVE_PERF_BUCKET_MAINFRAME_LEVEL_GEOMETRY] -
				 s_stressStartLevelGeometryMs) / (f64)frames;
			const f64 avgSubmit =
				(s_bucketTotals[NATIVE_PERF_BUCKET_RENDER_SUBMIT] -
				 s_stressStartRenderSubmitMs) / (f64)frames;
			const f64 avgSwap =
				(s_bucketTotals[NATIVE_PERF_BUCKET_SWAP_WINDOW] -
				 s_stressStartSwapWindowMs) / (f64)frames;
			const f64 avgVram =
				(s_bucketTotals[NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM] -
				 s_stressStartUpdateVramMs) / (f64)frames;
			const f64 avgDraws =
				(f64)(s_totalDrawCalls - s_stressStartDrawCalls) / (f64)frames;
			const f64 avgTriangles =
				(f64)(s_totalTriangles - s_stressStartTriangles) / (f64)frames;
			const f64 effectiveFps = (avgFrameMs > 0.0) ? (1000.0 / avgFrameMs) : 0.0;

			Platform_Log(
				"[PERF TEST] RESULT frames=%u avg=%.3fms work=%.3fms fps=%.1f "
				"instances=%.3fms geometry=%.3fms submit=%.3fms swap=%.3fms "
				"vram=%.3fms draws=%.1f triangles=%.1f\n",
				frames, avgFrameMs, avgWorkMs, effectiveFps,
				avgInstances, avgGeometry, avgSubmit, avgSwap,
				avgVram, avgDraws, avgTriangles);
		}
		else
		{
			Platform_Log("[PERF TEST] RESULT no measured frames\n");
		}
	}
	else if (s_stressTestActive)
	{
		Platform_Log("[PERF TEST] RESULT profiler measurement never started\n");
	}

	s_stressTestActive = 0;
	s_stressTestMeasuring = 0;
	Platform_LogFlush();
}

int NativePerf_IsStressTestActive(void)
{
	return s_stressTestActive;
}

int NativePerf_IsStressTestMeasuring(void)
{
	return s_stressTestMeasuring;
}

int NativePerf_StressTestReachedFrameLimit(void)
{
	return s_stressTestActive && s_stressTestMeasuring &&
	       s_stressStopAfterFrames > 0 &&
	       (s_frameIndex - s_stressStartFrameIndex) >= s_stressStopAfterFrames;
}

void NativePerf_StressTestBeginRace(void)
{
	if (!s_stressTestActive || s_stressTestMeasuring)
		return;

	s_stressTestMeasuring = 1;
	s_stressStartFrameIndex = s_frameIndex;
	s_stressStartTotalFrameMs = s_totalFrameMs;
	s_stressStartTotalWorkMs = s_totalWorkMs;
	s_stressStartExecuteInstancesMs =
		s_bucketTotals[NATIVE_PERF_BUCKET_MAINFRAME_EXECUTE_INSTANCES];
	s_stressStartLevelGeometryMs =
		s_bucketTotals[NATIVE_PERF_BUCKET_MAINFRAME_LEVEL_GEOMETRY];
	s_stressStartRenderSubmitMs =
		s_bucketTotals[NATIVE_PERF_BUCKET_RENDER_SUBMIT];
	s_stressStartSwapWindowMs =
		s_bucketTotals[NATIVE_PERF_BUCKET_SWAP_WINDOW];
	s_stressStartUpdateVramMs =
		s_bucketTotals[NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM];
	s_stressStartDrawCalls = s_totalDrawCalls;
	s_stressStartTriangles = s_totalTriangles;

	Platform_Log("[PERF TEST] measurement started at profiler frame=%u\n",
	             s_frameIndex);
}

internal f64 NativePerf_CounterToMs(u64 counterDelta)
{
	const u64 freq = SDL_GetPerformanceFrequency();

	if (freq == 0)
	{
		return 0.0;
	}

	return ((f64)counterDelta * 1000.0) / (f64)freq;
}

internal f64 NativePerf_TargetBudgetMs(const struct NativePerfFrameInfo *info)
{
	const s32 targetFPS = (info != NULL) ? info->targetFPS : 30;
	/* targetFPS==0 explicitly means Unlimited: there is no artificial frame
	 * budget to fail. The profiler still records total/work/GPU time. */
	if (targetFPS <= 0)
		return 0.0;
	return 1000.0 / (f64)targetFPS;
}

internal const char *NativePerf_BucketName(enum NativePerfBucket bucket)
{
	if ((bucket < 0) || (bucket >= NATIVE_PERF_BUCKET_COUNT))
	{
		return "unknown_ms";
	}

	return s_bucketInfo[bucket].name;
}

internal s32 NativePerf_PathExists(const char *path)
{
	struct stat st;

	if (path == NULL)
	{
		return 0;
	}

	return stat(path, &st) == 0;
}

internal s32 NativePerf_MakeDir(const char *path)
{
	if ((path == NULL) || (path[0] == '\0'))
	{
		return 1;
	}

#if defined(_WIN32)
	if (_mkdir(path) == 0)
		return 1;
#else
	if (mkdir(path, 0777) == 0)
	{
		return 1;
	}
#endif

	return (errno == EEXIST) && NativePerf_PathExists(path);
}

internal s32 NativePerf_CreateDirs(const char *path)
{
	char copy[NATIVE_PERF_PATH_MAX];
	char *cursor;
	s32 ok = 1;

	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}

	if (!NativePath_NormalizeSlashes(copy, sizeof(copy), NativeStr8_FromCString(path)))
	{
		return 0;
	}

	cursor = copy;
	if (NativePath_IsSeparator((u8)cursor[0]))
	{
		cursor++;
	}
#if defined(_WIN32)
	if ((cursor[0] != '\0') && (cursor[1] == ':') && NativePath_IsSeparator((u8)cursor[2]))
		cursor += 3;
#endif

	while (*cursor != '\0')
	{
		if (NativePath_IsSeparator((u8)*cursor))
		{
			char saved = *cursor;

			*cursor = '\0';
			if ((copy[0] != '\0') && !NativePerf_MakeDir(copy))
			{
				ok = 0;
				break;
			}
			*cursor = saved;
		}
		cursor++;
	}

	if ((ok != 0) && !NativePerf_MakeDir(copy))
	{
		ok = 0;
	}

	return ok;
}

internal s32 NativePerf_BuildSessionDir(char *out, size_t outSize, const char *root)
{
	time_t now;
	struct tm localTime;
	char stamp[32];
	char base[NATIVE_PERF_PATH_MAX];
	int written;

	if ((out == NULL) || (outSize == 0) || (root == NULL) || (root[0] == '\0'))
	{
		return 0;
	}

	now = time(NULL);
#if defined(_WIN32)
	if (localtime_s(&localTime, &now) != 0)
	{
		return 0;
	}
#else
	if (localtime_r(&now, &localTime) == NULL)
	{
		return 0;
	}
#endif

	if (strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &localTime) == 0)
	{
		return 0;
	}

	written = snprintf(base, sizeof(base), "%s/%s_v%s", root, stamp, CTR_NATIVE_VERSION);
	if ((written < 0) || ((size_t)written >= sizeof(base)))
	{
		return 0;
	}

	for (int suffix = 0; suffix < 100; suffix++)
	{
		if (suffix == 0)
			written = snprintf(out, outSize, "%s", base);
		else
			written = snprintf(out, outSize, "%s_%02d", base, suffix);

		if ((written < 0) || ((size_t)written >= outSize))
		{
			return 0;
		}

		if (!NativePerf_PathExists(out))
		{
			return 1;
		}
	}

	return 0;
}

internal s32 NativePerf_CopyFile(const char *srcPath, const char *dstPath)
{
	FILE *src;
	FILE *dst;
	char buffer[16384];
	size_t bytesRead;

	if ((srcPath == NULL) || (srcPath[0] == '\0') || (dstPath == NULL) || (dstPath[0] == '\0'))
	{
		return 0;
	}

	src = fopen(srcPath, "rb");
	if (src == NULL)
	{
		return 0;
	}

	dst = fopen(dstPath, "wb");
	if (dst == NULL)
	{
		fclose(src);
		return 0;
	}

	while ((bytesRead = fread(buffer, 1, sizeof(buffer), src)) != 0)
	{
		if (fwrite(buffer, 1, bytesRead, dst) != bytesRead)
		{
			fclose(src);
			fclose(dst);
			return 0;
		}
	}

	fclose(src);
	fclose(dst);
	return 1;
}

internal s32 NativePerf_SetOutputDir(const char *path)
{
	if (!NativePath_NormalizeSlashes(s_outputDir, sizeof(s_outputDir), NativeStr8_FromCString(path)))
	{
		return 0;
	}

	if (!NativePath_Join(s_csvPath, sizeof(s_csvPath), NativeStr8_FromCString(s_outputDir), NATIVE_STR8_LIT(NATIVE_PERF_FRAME_CSV_NAME)))
	{
		return 0;
	}

	if (!NativePath_Join(s_summaryPath, sizeof(s_summaryPath), NativeStr8_FromCString(s_outputDir), NATIVE_STR8_LIT(NATIVE_PERF_SUMMARY_NAME)))
	{
		return 0;
	}

	if (!NativePath_Join(s_runtimeLogPath, sizeof(s_runtimeLogPath), NativeStr8_FromCString(s_outputDir), NATIVE_STR8_LIT(NATIVE_PERF_RUNTIME_LOG_NAME)))
	{
		return 0;
	}

	return 1;
}

internal void NativePerf_WriteCsvHeader(FILE *file)
{
	fprintf(file, "frame_index,total_ms,work_ms,target_fps,budget_ms,fullscreen,over_budget_ms,work_over_budget_ms,dominant_bucket,dominant_ms,frameCounter,timer,level_id,game_mode1,"
	              "loading_stage,bool_demo_mode,num_plyr,elapsed_time_ms,vsync_till_flip,vSync_between_drawSync,frameTimer_VsyncCallback,"
	              "vram_write_pixels,vram_upload_calls,vram_upload_bytes,gpu_splits,draw_calls,triangles,"
	              "shader_switches,texture_binds,blend_state_changes,depth_state_changes,stencil_state_changes,gpu_frame_ms_latest,"
	              "audio_underrun_delta,audio_overflow_delta,audio_queued_frames");

	for (s32 bucket = 0; bucket < NATIVE_PERF_BUCKET_COUNT; bucket++)
	{
		fprintf(file, ",%s", NativePerf_BucketName((enum NativePerfBucket)bucket));
	}

	fprintf(file, "\n");
}

internal enum NativePerfBucket NativePerf_FindDominantBucket(f64 *dominantMsOut)
{
	enum NativePerfBucket dominantBucket = NATIVE_PERF_BUCKET_GAME_LOGIC;
	f64 dominantMs = 0.0;

	for (s32 bucket = 0; bucket < NATIVE_PERF_BUCKET_COUNT; bucket++)
	{
		if (s_bucketInfo[bucket].dominantCandidate == 0)
		{
			continue;
		}

		if (s_frameBucketMs[bucket] > dominantMs)
		{
			dominantBucket = (enum NativePerfBucket)bucket;
			dominantMs = s_frameBucketMs[bucket];
		}
	}

	*dominantMsOut = dominantMs;
	return dominantBucket;
}

internal void NativePerf_InsertWorstFrame(const struct NativePerfWorstFrame *frame)
{
	s32 insertAt = s_worstFrameCount;

	if (s_worstFrameCount == NATIVE_PERF_TOP_FRAME_COUNT && frame->workMs <= s_worstFrames[s_worstFrameCount - 1].workMs)
	{
		return;
	}

	while ((insertAt > 0) && (frame->workMs > s_worstFrames[insertAt - 1].workMs))
	{
		if (insertAt < NATIVE_PERF_TOP_FRAME_COUNT)
		{
			s_worstFrames[insertAt] = s_worstFrames[insertAt - 1];
		}
		insertAt--;
	}

	if (insertAt < NATIVE_PERF_TOP_FRAME_COUNT)
	{
		s_worstFrames[insertAt] = *frame;
	}

	if (s_worstFrameCount < NATIVE_PERF_TOP_FRAME_COUNT)
	{
		s_worstFrameCount++;
	}
}

internal void NativePerf_WriteSummary(FILE *file)
{
	f64 averageMs = (s_frameIndex == 0) ? 0.0 : s_totalFrameMs / (f64)s_frameIndex;
	f64 averageWorkMs = (s_frameIndex == 0) ? 0.0 : s_totalWorkMs / (f64)s_frameIndex;
	f64 averageBudgetMs = (s_frameIndex == 0) ? 0.0 : s_totalBudgetMs / (f64)s_frameIndex;

	fprintf(file, "version=%s\n", CTR_NATIVE_VERSION);
	fprintf(file, "build_id=%s\n", CTR_NATIVE_BUILD_ID);
	fprintf(file, "session_dir=%s\n", s_outputDir);
	fprintf(file, "runtime_log=%s\n", s_runtimeLogPath);
	fprintf(file, "frames=%u\n", s_frameIndex);
	fprintf(file, "average_ms=%.3f\n", averageMs);
	fprintf(file, "average_work_ms=%.3f\n", averageWorkMs);
	fprintf(file, "average_budget_ms=%.3f\n", averageBudgetMs);
	fprintf(file, "target_fps_min=%d\n", s_targetFPSMin);
	fprintf(file, "target_fps_max=%d\n", s_targetFPSMax);
	fprintf(file, "average_vram_write_pixels=%.2f\n", (s_frameIndex == 0) ? 0.0 : (f64)s_totalVRAMWritePixels / (f64)s_frameIndex);
	fprintf(file, "average_vram_upload_calls=%.3f\n", (s_frameIndex == 0) ? 0.0 : (f64)s_totalVRAMUploadCalls / (f64)s_frameIndex);
	fprintf(file, "average_vram_upload_mb=%.3f\n", (s_frameIndex == 0) ? 0.0 : ((f64)s_totalVRAMUploadBytes / (1024.0 * 1024.0)) / (f64)s_frameIndex);
	fprintf(file, "average_gpu_splits=%.2f\n", (s_frameIndex == 0) ? 0.0 : (f64)s_totalGpuSplits / (f64)s_frameIndex);
	fprintf(file, "average_draw_calls=%.2f\n", (s_frameIndex == 0) ? 0.0 : (f64)s_totalDrawCalls / (f64)s_frameIndex);
	fprintf(file, "average_triangles=%.2f\n", (s_frameIndex == 0) ? 0.0 : (f64)s_totalTriangles / (f64)s_frameIndex);
	fprintf(file, "average_gpu_frame_ms=%.3f\n", s_gpuFrameSampleCount == 0 ? -1.0 : s_totalGpuFrameMs / (f64)s_gpuFrameSampleCount);
	fprintf(file, "gpu_frame_samples=%u\n", s_gpuFrameSampleCount);
	fprintf(file, "average_shader_switches=%.3f\n", s_frameIndex == 0 ? 0.0 : (f64)s_totalShaderSwitches / (f64)s_frameIndex);
	fprintf(file, "average_texture_binds=%.3f\n", s_frameIndex == 0 ? 0.0 : (f64)s_totalTextureBinds / (f64)s_frameIndex);
	fprintf(file, "average_blend_state_changes=%.3f\n", s_frameIndex == 0 ? 0.0 : (f64)s_totalBlendStateChanges / (f64)s_frameIndex);
	fprintf(file, "average_depth_state_changes=%.3f\n", s_frameIndex == 0 ? 0.0 : (f64)s_totalDepthStateChanges / (f64)s_frameIndex);
	fprintf(file, "average_stencil_state_changes=%.3f\n", s_frameIndex == 0 ? 0.0 : (f64)s_totalStencilStateChanges / (f64)s_frameIndex);
	fprintf(file, "total_vram_upload_mb=%.3f\n", (f64)s_totalVRAMUploadBytes / (1024.0 * 1024.0));
	fprintf(file, "max_ms=%.3f\n", s_maxFrameMs);
	fprintf(file, "max_work_ms=%.3f\n", s_maxWorkMs);
	fprintf(file, "frames_over_budget=%u\n", s_framesOverBudget);
	fprintf(file, "work_frames_over_budget=%u\n", s_workFramesOverBudget);
	fprintf(file, "frames_over_40ms=%u\n", s_framesOver40);
	fprintf(file, "frames_over_50ms=%u\n", s_framesOver50);
	fprintf(file, "csv_path=%s\n", s_csvPath);

	fprintf(file, "\nbucket_totals_ms:\n");
	for (s32 bucket = 0; bucket < NATIVE_PERF_BUCKET_COUNT; bucket++)
	{
		fprintf(file, "  %s: %.3f\n", NativePerf_BucketName((enum NativePerfBucket)bucket), s_bucketTotals[bucket]);
	}

	fprintf(file, "\ntop_worst_work_frames:\n");
	fprintf(file, "frame,total_ms,work_ms,target_fps,budget_ms,fullscreen,over_budget_ms,work_over_budget_ms,dominant_bucket,dominant_ms,frameCounter,timer,level_id,game_mode1,loading_stage,"
	              "bool_demo_mode,num_plyr,elapsed_time_ms,vsync_till_flip,vSync_between_drawSync,frameTimer_VsyncCallback,"
	              "vram_write_pixels,vram_upload_calls,vram_upload_bytes,gpu_splits,draw_calls,triangles,"
	              "shader_switches,texture_binds,blend_state_changes,depth_state_changes,stencil_state_changes,gpu_frame_ms_latest,"
	              "audio_underrun_delta,audio_overflow_delta,audio_queued_frames\n");

	for (s32 i = 0; i < s_worstFrameCount; i++)
	{
		const struct NativePerfWorstFrame *frame = &s_worstFrames[i];

		fprintf(file, "%u,%.3f,%.3f,%d,%.3f,%d,%.3f,%.3f,%s,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
		        frame->frameIndex, frame->totalMs, frame->workMs, frame->info.targetFPS, frame->budgetMs, frame->info.fullscreen,
		        frame->overBudgetMs, frame->workOverBudgetMs, NativePerf_BucketName(frame->dominantBucket), frame->dominantMs,
		        frame->info.frameCounter, frame->info.timer, frame->info.levelID, frame->info.gameMode1, frame->info.loadingStage,
		        frame->info.boolDemoMode, frame->info.numPlyrCurrGame, frame->info.elapsedTimeMS, frame->info.vsyncTillFlip,
		        frame->info.vSync_between_drawSync, frame->info.frameTimer_VsyncCallback);
		fprintf(file, ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f",
		        frame->vramWritePixels, frame->vramUploadCalls, frame->vramUploadBytes,
		        frame->gpuSplits, frame->drawCalls, frame->triangles,
		        frame->shaderSwitches, frame->textureBinds, frame->blendStateChanges,
		        frame->depthStateChanges, frame->stencilStateChanges, frame->gpuFrameMs);
		fprintf(file, ",%d,%d,%d\n",
		        frame->audioUnderrunDelta, frame->audioOverflowDelta, frame->audioQueuedFrames);
	}
}

internal const char *NativePerf_ArgValue(int argc, char **argv, const char *name)
{
	for (s32 i = 1; i < argc - 1; i++)
	{
		if (strcmp(argv[i], name) == 0)
		{
			return argv[i + 1];
		}
	}

	return NULL;
}

internal s32 NativePerf_ArgPresent(int argc, char **argv, const char *name)
{
	for (s32 i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], name) == 0)
		{
			return 1;
		}
	}

	return 0;
}

int NativePerf_ConfigureFromArgs(int argc, char **argv)
{
	const char *outputRoot = NativePerf_ArgValue(argc, argv, "--perf-dir");
	FILE *markerFile = fopen("debug/perf.enable", "rb");
    FILE *botMarker = fopen("debug/perf.bot_profile", "rb");
    const s32 botRequested = botMarker != NULL;
    if(botMarker)fclose(botMarker);
	const s32 markerEnabled = (markerFile != NULL);
	const s32 perfEnabled = NativePerf_ArgPresent(argc, argv, "--perf") || (outputRoot != NULL) || markerEnabled || botRequested;
	char sessionDir[NATIVE_PERF_PATH_MAX];

	if (markerFile != NULL)
	{
		fclose(markerFile);
	}

	if (!perfEnabled)
	{
		return 0;
	}

	if (outputRoot == NULL)
	{
		outputRoot = NATIVE_PERF_DEFAULT_ROOT;
	}

	if (!NativePerf_BuildSessionDir(sessionDir, sizeof(sessionDir), outputRoot))
	{
		fprintf(stderr, "[CTR Perf] failed to build timestamped perf path under: %s\n", outputRoot);
		return 1;
	}

	if (!NativePerf_SetOutputDir(sessionDir))
	{
		fprintf(stderr, "[CTR Perf] invalid perf output path: %s\n", sessionDir);
		return 1;
	}

	if (!NativePerf_CreateDirs(s_outputDir))
	{
		fprintf(stderr, "[CTR Perf] failed to create perf output directory: %s\n", s_outputDir);
		return 1;
	}

	s_csvFile = fopen(s_csvPath, "wb");
	if (s_csvFile == NULL)
	{
		fprintf(stderr, "[CTR Perf] failed to open frame CSV: %s\n", s_csvPath);
		return 1;
	}

	/* Profiling is a developer feature; use a few MiB of otherwise cheap PC
	 * RAM so hundreds of high-refresh CSV rows stay in memory instead of
	 * provoking frequent small writes while the game is being measured. */
	setvbuf(s_csvFile, s_csvBuffer, _IOFBF, sizeof(s_csvBuffer));

	NativePerf_WriteCsvHeader(s_csvFile);
	fflush(s_csvFile);
    if(botRequested)
    {
        char path[NATIVE_PERF_PATH_MAX];
        if(snprintf(path,sizeof(path),"%s/bot_profile.csv",s_outputDir)>0)
        {
            s_botCsvFile=fopen(path,"wb");
            if(s_botCsvFile)
            {
                setvbuf(s_botCsvFile,s_botCsvBuffer,_IOFBF,sizeof(s_botCsvBuffer));
                fprintf(s_botCsvFile,
                    "frame,level_id,driver_id,character_id,kart_id,wheels_id,kart_color,ai_cpu_ms,render_cpu_ms,ai_calls,render_calls\n");
                for(int i=0;i<NATIVE_PERF_BOT_COUNT;++i)
                {
                    s_botSlots[i].lastCharacter=-9999;
                    s_botSlots[i].lastKart=-9999;
                    s_botSlots[i].lastWheels=-9999;
                    s_botSlots[i].lastColor=-9999;
                }
                Platform_Log("[BOTPROFILE] enabled, output=%s; render=CPU model preparation, NOT dedicated GPU timings\n",path);
            }
            else Platform_Log("[BOTPROFILE] could not open output %s\n",path);
        }
    }
	s_enabled = 1;

	/* GPU queries are asynchronous/non-blocking; profiler sessions enable them
	 * automatically so CPU work, SwapWindow/compositor time and GPU time can be
	 * separated in the next capture. */
	NativeRenderer_SetGpuTimingEnabled(1);
	Platform_Log("[CTR Perf] capturing frame times: %s\n", s_csvPath);
	return 0;
}

void NativePerf_Shutdown(void)
{
	FILE *summaryFile;

	if (!s_enabled)
	{
		return;
	}

	if (s_frameOpen)
	{
		NativePerf_EndFrame(&s_frameBeginInfo);
	}

	if (s_csvFile != NULL)
	{
		fflush(s_csvFile);
		fclose(s_csvFile);
		s_csvFile = NULL;
	}
    if(s_botCsvFile != NULL)
    {
        fflush(s_botCsvFile);
        fclose(s_botCsvFile);
        s_botCsvFile=NULL;
        Platform_Log("[BOTPROFILE] saved %u measured bot-profile frames\n",s_botProfilerFrames);
    }

	summaryFile = fopen(s_summaryPath, "wb");
	if (summaryFile != NULL)
	{
		NativePerf_WriteSummary(summaryFile);
		fclose(summaryFile);
	}
	else
	{
		Platform_Log("[CTR Perf] failed to write summary: %s\n", s_summaryPath);
	}

	Platform_Log("[CTR Perf] frames=%u avg=%.3fms avgWork=%.3fms avgBudget=%.3fms targetFPS=%d..%d max=%.3fms maxWork=%.3fms overBudget=%u workOverBudget=%u over40=%u over50=%u\n",
	             s_frameIndex,
	             (s_frameIndex == 0) ? 0.0 : s_totalFrameMs / (f64)s_frameIndex,
	             (s_frameIndex == 0) ? 0.0 : s_totalWorkMs / (f64)s_frameIndex,
	             (s_frameIndex == 0) ? 0.0 : s_totalBudgetMs / (f64)s_frameIndex,
	             s_targetFPSMin, s_targetFPSMax, s_maxFrameMs, s_maxWorkMs,
	             s_framesOverBudget, s_workFramesOverBudget, s_framesOver40, s_framesOver50);
	Platform_Log("[CTR Perf] summary: %s\n", s_summaryPath);
	Platform_Log("[CTR Perf] runtime log copy: %s\n", s_runtimeLogPath);
	Platform_LogFlush();

	if (!NativePerf_CopyFile(Platform_LogGetPath(), s_runtimeLogPath))
	{
		fprintf(stderr, "[CTR Perf] warning: could not copy runtime log to: %s\n", s_runtimeLogPath);
	}

	s_enabled = 0;
}

void NativePerf_BeginFrame(const struct NativePerfFrameInfo *info)
{
	if (!s_enabled || (info == NULL))
	{
		return;
	}

	if (s_frameOpen)
	{
		NativePerf_EndFrame(info);
	}

	memset(s_frameBucketMs, 0, sizeof(s_frameBucketMs));
    if(s_botCsvFile)
        for(int i=0;i<NATIVE_PERF_BOT_COUNT;++i)
        {
            memset(s_botSlots[i].start,0,sizeof(s_botSlots[i].start));
            memset(s_botSlots[i].ticks,0,sizeof(s_botSlots[i].ticks));
            memset(s_botSlots[i].calls,0,sizeof(s_botSlots[i].calls));
        }
	memset(s_scopeDepth, 0, sizeof(s_scopeDepth));
	memset(s_scopeStartCounter, 0, sizeof(s_scopeStartCounter));
	s_frameVRAMWritePixels = 0;
	s_frameVRAMUploadCalls = 0;
	s_frameVRAMUploadBytes = 0;
	s_frameGpuSplits = 0;
	s_frameDrawCalls = 0;
	s_frameTriangles = 0;
	s_frameShaderSwitches = 0;
	s_frameTextureBinds = 0;
	s_frameBlendStateChanges = 0;
	s_frameDepthStateChanges = 0;
	s_frameStencilStateChanges = 0;

	s_frameBeginInfo = *info;
	NativeAudio_GetOutputStats(&s_beginUnderrunFrames, &s_beginOverflowFrames, &s_beginQueuedFrames);
	s_frameStartCounter = SDL_GetPerformanceCounter();
	s_frameOpen = 1;
}

void NativePerf_EndFrame(const struct NativePerfFrameInfo *info)
{
	f64 totalMs;
	f64 workMs;
	f64 budgetMs;
	f64 overBudgetMs;
	f64 workOverBudgetMs;
	f64 dominantMs;
	f64 gpuFrameMs;
	enum NativePerfBucket dominantBucket;
	s32 endUnderrunFrames = 0;
	s32 endOverflowFrames = 0;
	s32 endQueuedFrames = 0;
	struct NativePerfWorstFrame worstFrame;
	const struct NativePerfFrameInfo *rowInfo;

	if (!s_enabled || !s_frameOpen)
	{
		return;
	}

	rowInfo = (info != NULL) ? info : &s_frameBeginInfo;
	budgetMs = NativePerf_TargetBudgetMs(rowInfo);

	totalMs = NativePerf_CounterToMs(SDL_GetPerformanceCounter() - s_frameStartCounter);
	workMs = totalMs -
	         s_frameBucketMs[NATIVE_PERF_BUCKET_VSYNC_WAIT] -
	         s_frameBucketMs[NATIVE_PERF_BUCKET_HIGH_REFRESH_WAIT];
	if (workMs < 0.0)
	{
		workMs = 0.0;
	}

	overBudgetMs = (budgetMs > 0.0 && totalMs > budgetMs) ? (totalMs - budgetMs) : 0.0;
	workOverBudgetMs = (budgetMs > 0.0 && workMs > budgetMs) ? (workMs - budgetMs) : 0.0;
	dominantBucket = NativePerf_FindDominantBucket(&dominantMs);
	gpuFrameMs = NativeRenderer_GetLastGpuFrameMs();

	NativeAudio_GetOutputStats(&endUnderrunFrames, &endOverflowFrames, &endQueuedFrames);

	fprintf(s_csvFile, "%u,%.3f,%.3f,%d,%.3f,%d,%.3f,%.3f,%s,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
	        s_frameIndex, totalMs, workMs, rowInfo->targetFPS, budgetMs, rowInfo->fullscreen,
	        overBudgetMs, workOverBudgetMs, NativePerf_BucketName(dominantBucket), dominantMs,
	        rowInfo->frameCounter, rowInfo->timer, rowInfo->levelID, rowInfo->gameMode1,
	        rowInfo->loadingStage, rowInfo->boolDemoMode, rowInfo->numPlyrCurrGame, rowInfo->elapsedTimeMS,
	        rowInfo->vsyncTillFlip, rowInfo->vSync_between_drawSync, rowInfo->frameTimer_VsyncCallback);
	fprintf(s_csvFile, ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f",
	        s_frameVRAMWritePixels, s_frameVRAMUploadCalls, s_frameVRAMUploadBytes,
	        s_frameGpuSplits, s_frameDrawCalls, s_frameTriangles,
	        s_frameShaderSwitches, s_frameTextureBinds, s_frameBlendStateChanges,
	        s_frameDepthStateChanges, s_frameStencilStateChanges, gpuFrameMs);
	fprintf(s_csvFile, ",%d,%d,%d",
	        endUnderrunFrames - s_beginUnderrunFrames, endOverflowFrames - s_beginOverflowFrames, endQueuedFrames);

	for (s32 bucket = 0; bucket < NATIVE_PERF_BUCKET_COUNT; bucket++)
	{
		fprintf(s_csvFile, ",%.3f", s_frameBucketMs[bucket]);
		s_bucketTotals[bucket] += s_frameBucketMs[bucket];
	}
	fprintf(s_csvFile, "\n");

    if(s_botCsvFile && sdata && sdata->gGT)
    {
        const double toMs=1000.0/(double)SDL_GetPerformanceFrequency();
        struct GameTracker *gt=sdata->gGT;
        for(int i=0;i<NATIVE_PERF_BOT_COUNT;++i)
        {
            struct Driver *driver=gt->drivers[i];
            if(driver==NULL || (driver->actionsFlagSet & ACTION_BOT)==0)
                continue;
            const int c=RacerLoadout_GetCharacter(i);
            const int k=RacerLoadout_GetEffectiveKart(i);
            const int w=RacerLoadout_GetEffectiveWheels(i);
            const int color=RacerLoadout_GetKartColor(i);
            struct NativePerfBotSlot *bot=&s_botSlots[i];
            if(!bot->logged || bot->lastCharacter!=c ||
               bot->lastKart!=k || bot->lastWheels!=w ||
               bot->lastColor!=color)
            {
                const char *charName=CharacterRegistry_GetDisplayName(c);
                const char *kartName=VehiclePartRegistry_GetDisplayName(VEHICLE_PART_KART,k);
                const char *wheelName=VehiclePartRegistry_GetDisplayName(VEHICLE_PART_WHEELS,w);
                Platform_Log("[BOTLOADOUT LIVE] level=%d bot=%d character=%d(%s) kart=%d(%s) wheels=%d(%s) color=%d\n",
                    gt->levelID,i,c,charName?charName:"retail/unknown",
                    k,kartName?kartName:"original/inherited",
                    w,wheelName?wheelName:"original/inherited",color);
                bot->logged=1;
                bot->lastCharacter=c;bot->lastKart=k;bot->lastWheels=w;
                bot->lastColor=color;
            }
            fprintf(s_botCsvFile,"%u,%d,%d,%d,%d,%d,%d,%.5f,%.5f,%u,%u\n",
                s_frameIndex,gt->levelID,i,c,k,w,color,
                (double)bot->ticks[NATIVE_PERF_BOT_AI]*toMs,
                (double)bot->ticks[NATIVE_PERF_BOT_RENDER]*toMs,
                bot->calls[NATIVE_PERF_BOT_AI],
                bot->calls[NATIVE_PERF_BOT_RENDER]);
        }
        ++s_botProfilerFrames;
        if(s_botProfilerFrames%600u==0u)
        {
            fflush(s_botCsvFile);
            Platform_Log("[BOTPROFILE] frames=%u, per-bot AI/render CPU samples saved (GPU draws still shared)\n",s_botProfilerFrames);
        }
    }

	s_rowsSinceFlush++;
	if (s_rowsSinceFlush >= NATIVE_PERF_FLUSH_INTERVAL)
	{
		fflush(s_csvFile);
		s_rowsSinceFlush = 0;
	}

	s_totalFrameMs += totalMs;
	s_totalWorkMs += workMs;
	s_totalBudgetMs += budgetMs;
	s_totalVRAMWritePixels += s_frameVRAMWritePixels;
	s_totalVRAMUploadCalls += s_frameVRAMUploadCalls;
	s_totalVRAMUploadBytes += s_frameVRAMUploadBytes;
	s_totalGpuSplits += s_frameGpuSplits;
	s_totalDrawCalls += s_frameDrawCalls;
	s_totalTriangles += s_frameTriangles;
	s_totalShaderSwitches += s_frameShaderSwitches;
	s_totalTextureBinds += s_frameTextureBinds;
	s_totalBlendStateChanges += s_frameBlendStateChanges;
	s_totalDepthStateChanges += s_frameDepthStateChanges;
	s_totalStencilStateChanges += s_frameStencilStateChanges;
	if (gpuFrameMs >= 0.0)
	{
		s_totalGpuFrameMs += gpuFrameMs;
		s_gpuFrameSampleCount++;
	}
	if ((s_targetFPSMin == 0) || (rowInfo->targetFPS < s_targetFPSMin))
	{
		s_targetFPSMin = rowInfo->targetFPS;
	}
	if (rowInfo->targetFPS > s_targetFPSMax)
	{
		s_targetFPSMax = rowInfo->targetFPS;
	}
	if (totalMs > s_maxFrameMs)
	{
		s_maxFrameMs = totalMs;
	}
	if (workMs > s_maxWorkMs)
	{
		s_maxWorkMs = workMs;
	}
	if (budgetMs > 0.0 && totalMs > budgetMs)
	{
		s_framesOverBudget++;
	}
	if (budgetMs > 0.0 && workMs > budgetMs)
	{
		s_workFramesOverBudget++;
	}
	if (totalMs > NATIVE_PERF_FRAME_WARN_40_MS)
	{
		s_framesOver40++;
	}
	if (totalMs > NATIVE_PERF_FRAME_WARN_50_MS)
	{
		s_framesOver50++;
	}

	worstFrame.frameIndex = s_frameIndex;
	worstFrame.totalMs = totalMs;
	worstFrame.workMs = workMs;
	worstFrame.budgetMs = budgetMs;
	worstFrame.overBudgetMs = overBudgetMs;
	worstFrame.workOverBudgetMs = workOverBudgetMs;
	worstFrame.dominantBucket = dominantBucket;
	worstFrame.dominantMs = dominantMs;
	worstFrame.info = *rowInfo;
	worstFrame.vramWritePixels = s_frameVRAMWritePixels;
	worstFrame.vramUploadCalls = s_frameVRAMUploadCalls;
	worstFrame.vramUploadBytes = s_frameVRAMUploadBytes;
	worstFrame.gpuSplits = s_frameGpuSplits;
	worstFrame.drawCalls = s_frameDrawCalls;
	worstFrame.triangles = s_frameTriangles;
	worstFrame.shaderSwitches = s_frameShaderSwitches;
	worstFrame.textureBinds = s_frameTextureBinds;
	worstFrame.blendStateChanges = s_frameBlendStateChanges;
	worstFrame.depthStateChanges = s_frameDepthStateChanges;
	worstFrame.stencilStateChanges = s_frameStencilStateChanges;
	worstFrame.gpuFrameMs = gpuFrameMs;
	worstFrame.audioUnderrunDelta = endUnderrunFrames - s_beginUnderrunFrames;
	worstFrame.audioOverflowDelta = endOverflowFrames - s_beginOverflowFrames;
	worstFrame.audioQueuedFrames = endQueuedFrames;
	NativePerf_InsertWorstFrame(&worstFrame);

	s_frameIndex++;
	s_frameOpen = 0;
}

void NativePerf_BotBegin(int driverID, enum NativePerfBotPhase phase)
{
    if(!s_botCsvFile || !s_frameOpen || driverID<0 ||
       driverID>=NATIVE_PERF_BOT_COUNT || phase<0 ||
       phase>=NATIVE_PERF_BOT_PHASE_COUNT)return;
    struct NativePerfBotSlot *slot=&s_botSlots[driverID];
    if(slot->start[phase]==0)
        slot->start[phase]=SDL_GetPerformanceCounter();
}

void NativePerf_BotEnd(int driverID, enum NativePerfBotPhase phase)
{
    if(!s_botCsvFile || !s_frameOpen || driverID<0 ||
       driverID>=NATIVE_PERF_BOT_COUNT || phase<0 ||
       phase>=NATIVE_PERF_BOT_PHASE_COUNT)return;
    struct NativePerfBotSlot *slot=&s_botSlots[driverID];
    const u64 now=SDL_GetPerformanceCounter();
    if(slot->start[phase] != 0 && now>=slot->start[phase])
    {
        slot->ticks[phase]+=now-slot->start[phase];
        ++slot->calls[phase];
    }
    slot->start[phase]=0;
}

int NativePerf_BotIDForInstance(const struct Instance *inst)
{
    if(!s_botCsvFile || !s_frameOpen || !inst || !sdata || !sdata->gGT)
        return -1;
    for(int i=0;i<NATIVE_PERF_BOT_COUNT;++i)
    {
        const struct Driver *driver=sdata->gGT->drivers[i];
        if(driver && driver->instSelf==inst &&
           (driver->actionsFlagSet & ACTION_BOT)!=0)
            return i;
    }
    return -1;
}

void NativePerf_BeginScope(enum NativePerfBucket bucket)
{
	if (!s_enabled || !s_frameOpen || (bucket < 0) || (bucket >= NATIVE_PERF_BUCKET_COUNT))
	{
		return;
	}

	if (s_scopeDepth[bucket]++ == 0)
	{
		s_scopeStartCounter[bucket] = SDL_GetPerformanceCounter();
	}
}

void NativePerf_EndScope(enum NativePerfBucket bucket)
{
	if (!s_enabled || !s_frameOpen || (bucket < 0) || (bucket >= NATIVE_PERF_BUCKET_COUNT))
	{
		return;
	}

	if (s_scopeDepth[bucket] <= 0)
	{
		return;
	}

	s_scopeDepth[bucket]--;
	if (s_scopeDepth[bucket] == 0)
	{
		s_frameBucketMs[bucket] += NativePerf_CounterToMs(SDL_GetPerformanceCounter() - s_scopeStartCounter[bucket]);
	}
}

void NativePerf_AddVRAMWritePixels(u32 pixels)
{
	if (!s_enabled || !s_frameOpen)
	{
		return;
	}
	s_frameVRAMWritePixels += pixels;
}

void NativePerf_AddVRAMUpload(u32 bytes)
{
	if (!s_enabled || !s_frameOpen)
	{
		return;
	}
	s_frameVRAMUploadCalls++;
	s_frameVRAMUploadBytes += bytes;
}

void NativePerf_AddDrawCall(u32 triangles)
{
	if (!s_enabled || !s_frameOpen)
	{
		return;
	}
	s_frameDrawCalls++;
	s_frameTriangles += triangles;
}

void NativePerf_AddSplits(u32 splits)
{
	if (!s_enabled || !s_frameOpen)
	{
		return;
	}
	s_frameGpuSplits += splits;
}

void NativePerf_AddShaderSwitch(void)
{
	if (s_enabled && s_frameOpen) s_frameShaderSwitches++;
}

void NativePerf_AddTextureBind(void)
{
	if (s_enabled && s_frameOpen) s_frameTextureBinds++;
}

void NativePerf_AddBlendStateChange(void)
{
	if (s_enabled && s_frameOpen) s_frameBlendStateChanges++;
}

void NativePerf_AddDepthStateChange(void)
{
	if (s_enabled && s_frameOpen) s_frameDepthStateChanges++;
}

void NativePerf_AddStencilStateChange(void)
{
	if (s_enabled && s_frameOpen) s_frameStencilStateChanges++;
}
#endif
