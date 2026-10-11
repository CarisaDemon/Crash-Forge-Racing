/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include "platform/native_gpu.h"

#include <platform.h>

#include <SDL3/SDL.h>

#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include <platform/native_gpu_model_qa.h>
#include "platform/native_world.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Platform_PollHostEvents(void);
extern int g_cfg_bilinearFiltering;
extern int g_cfg_subpixelGeometry;
extern int g_cfg_psxDithering;
extern int g_dbg_emulatorPaused;
extern int g_dbg_polygonSelected;
int NativeGTE_GetStoredSubpixel(const void *address, u32 packedSxy, float *x, float *y);
int NativeGTE_GetStoredSubpixelFixed(const void *address, u32 packedSxy, s32 *x16, s32 *y16);
int NativeGTE_GetStoredSubpixelFixedLocal(const void *address, u32 packedSxy, s32 *x16, s32 *y16);
int NativeGTE_GetStoredDepth(const void *address, u32 packedSxy, u16 *depth);
int NativeGTE_GetStoredVertexMetadata(const void *address, u32 packedSxy, s32 *x16, s32 *y16, u16 *depth);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* B5 occurs inside the PS1 OT, BEFORE EndScene. Capture at the actual
 * native-terrain draw to avoid false identical before/after comparisons. */
void NativeRenderer_B5SnapshotStage(int stage);
#endif

#define NATIVE_GPU_LOG(fmt, ...)   Platform_Log("[CTR GPU] " fmt, ##__VA_ARGS__)
#define NATIVE_GPU_ERROR(fmt, ...) Platform_LogError("[CTR GPU] [%s] - " fmt, __func__, ##__VA_ARGS__)

// NOTE(aalhendi): Little-endian tag `CTRG` = CTR native GPU snapshot.
#define NATIVE_GPU_STATE_MAGIC     0x47525443
#define NATIVE_GPU_STATE_VERSION   1

#define GET_TPAGE_BLEND(tpage)     ((BlendMode)(((tpage >> 5) & 3) + 1))

#define GET_TPAGE_DITHER(tpage)    ((tpage >> 9) & 0x1)

#define GET_CLUT_X(clut)           ((clut & 0x3F) << 4)
#define GET_CLUT_Y(clut)           (clut >> 6)

internal TexFormat GetTPageFormat(int tpage)
{
	const int mode = (tpage >> 7) & 0x3;

	// NOTE(aalhendi): ctr-native local divergence from upstream PsyCross. PS1
	// mode 3 is reserved; TF_32_BIT_RGBA is only for explicit native override
	// textures, not raw retail TPAGE mode bits.
	return mode == 3 ? TF_16_BIT : (TexFormat)mode;
}

internal short GetTPageBase(int tpage)
{
	const u16 page = (u16)tpage;

	// NOTE(aalhendi): ctr-native local divergence for CTR retail emitters. The
	// shader wants xPage + yPage * 16, not raw draw-mode bits.
	return (s16)((page & 0xf) | ((page & 0x10) ? 0x10 : 0));
}

internal s16 NativeGpu_SignExtend11(u32 value)
{
	value &= 0x7ff;
	return (s16)((value ^ 0x400) - 0x400);
}

DISPENV activeDispEnv;
DRAWENV activeDrawEnv;
int g_GPUDisabledState = 0;

typedef struct
{
	DRAWENV drawenv;
	DISPENV dispenv;

	BlendMode blendMode;

	TexFormat texFormat;
	TextureID textureId;

	int drawPrimMode;
	bool psxTexturedSemiTrans;
	bool psxTextureOutputSTP;
	bool psxDrawMaskSet;
	/* Experimental native Z policy: only explicit custom opaque geometry
	 * may participate. Retail world/effects remain OT-ordered. */
	bool nativeDepthCandidate;

	u32 startVertex;
	u32 numVerts;

	const char *debugText;
} GPUDrawSplit;

#define MAX_DRAW_SPLITS 65536

typedef struct
{
	const char *currentSplitDebugText;
	TextureID overrideTexture;
	int overrideTextureWidth;
	int overrideTextureHeight;

	int drawPrimMode;
	bool psxDrawMaskSet;
	bool framebufferFeedbackRunActive;
	bool nativeDepthCandidate;

	/* Consecutive self-contained native OBJ triangles may share one external
	 * texture. Keep that override alive only across adjacent B3 packets so
	 * AddSplit can merge them; restore the previous retail/native state before
	 * any other primitive. */
	bool psyxScopedOverrideActive;
	TextureID psyxScopedPrevTexture;
	int psyxScopedPrevTextureWidth;
	int psyxScopedPrevTextureHeight;

	GrVertex vertexBuffer[MAX_VERTEX_BUFFER_SIZE];
	GPUDrawSplit splits[MAX_DRAW_SPLITS];
	int vertexIndex;
	int splitIndex;
} NativeGpuState;

global_variable NativeGpuState s_gpu;

global_variable u64 s_subpixelPrimitiveAccepted = 0;
global_variable u64 s_subpixelPrimitiveFallback = 0;
global_variable int s_subpixelStatsLogged1000 = 0;

void NativeGpu_ResetSubpixelStats(void)
{
	s_subpixelPrimitiveAccepted = 0;
	s_subpixelPrimitiveFallback = 0;
	s_subpixelStatsLogged1000 = 0;
}

internal void NativeGpu_RecordSubpixelPrimitive(int complete)
{
	/* This is diagnostic sampling only. The old code kept 64-bit counters hot
	   for every primitive for the entire session (hundreds of millions of
	   updates in a long 200 Hz run). Stop after the initial sample so the
	   renderer pays no ongoing bookkeeping cost. */
	if (s_subpixelStatsLogged1000)
	{
		return;
	}

	if (complete)
	{
		s_subpixelPrimitiveAccepted++;
	}
	else
	{
		s_subpixelPrimitiveFallback++;
	}

	if (s_subpixelPrimitiveAccepted + s_subpixelPrimitiveFallback >= 1000)
	{
		s_subpixelStatsLogged1000 = 1;
		NATIVE_GPU_LOG("V70 subpixel first 1000 primitives: complete=%llu fallback=%llu\n",
		               (unsigned long long)s_subpixelPrimitiveAccepted,
		               (unsigned long long)s_subpixelPrimitiveFallback);
	}
}

void NativeGpu_LogSubpixelStats(void)
{
	if ((s_subpixelPrimitiveAccepted + s_subpixelPrimitiveFallback) == 0)
	{
		return;
	}
	NATIVE_GPU_LOG("V70 subpixel sample: complete=%llu fallback=%llu\n",
	               (unsigned long long)s_subpixelPrimitiveAccepted,
	               (unsigned long long)s_subpixelPrimitiveFallback);
}

// Presentation history used by optional native redraw/interpolation paths.
// The normal high-refresh path runs the real game/render loop at display rate;
// these snapshots must therefore never be treated as the simulation clock.
typedef struct
{
	GrVertex vertices[MAX_VERTEX_BUFFER_SIZE];
	GPUDrawSplit splits[MAX_DRAW_SPLITS];
	int vertexCount;
	int splitCount;
	int valid;
	int unsafeFramebufferFeedback;
	u32 serial;
	DRAWENV clearDrawEnv;
	int haveClearDrawEnv;
} NativeGpuPresentationFrame;

global_variable NativeGpuPresentationFrame s_presentFrames[2];
global_variable GrVertex s_presentInterpolatedVertices[MAX_VERTEX_BUFFER_SIZE];
global_variable int s_presentCurrentFrame = -1;
global_variable int s_presentWriteFrame = 0;
global_variable int s_presentCaptureActive = 0;
global_variable u32 s_presentSerial = 0;

struct NativeGpuSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	DRAWENV drawEnv;
	DISPENV dispEnv;
	s32 gpuDisabledState;
	s32 psxDrawMaskSet;
	u16 vram[VRAM_WIDTH * VRAM_HEIGHT];
};

int NativeGpu_HasPendingSplits(void)
{
	return s_gpu.splitIndex > 0;
}

void NativeGpu_PresentationReset(void)
{
	memset(s_presentFrames, 0, sizeof(s_presentFrames));
	s_presentCurrentFrame = -1;
	s_presentWriteFrame = 0;
	s_presentCaptureActive = 0;
	s_presentSerial = 0;
}

void NativeGpu_PresentationBeginFrame(void)
{
	NativeGpuPresentationFrame *frame;

	s_presentWriteFrame = (s_presentCurrentFrame >= 0) ? (s_presentCurrentFrame ^ 1) : 0;
	frame = &s_presentFrames[s_presentWriteFrame];
	frame->vertexCount = 0;
	frame->splitCount = 0;
	frame->valid = 0;
	frame->unsafeFramebufferFeedback = 0;
	frame->serial = 0;
	frame->haveClearDrawEnv = 0;
	s_presentCaptureActive = 1;
}

void NativeGpu_PresentationEndFrame(void)
{
	NativeGpuPresentationFrame *frame;

	if (!s_presentCaptureActive)
	{
		return;
	}

	frame = &s_presentFrames[s_presentWriteFrame];
	s_presentCaptureActive = 0;

	// Advance presentation history even for empty/movie/loading frames. That
	// prevents the next normal frame from interpolating against stale geometry
	// from before a transition.
	s_presentSerial++;
	if (s_presentSerial == 0)
	{
		s_presentSerial = 1;
	}
	frame->serial = s_presentSerial;
	frame->valid = (frame->vertexCount > 0) && (frame->splitCount > 0);
	s_presentCurrentFrame = s_presentWriteFrame;
}

internal void NativeGpu_PresentationAppendCurrentBatch(void)
{
	NativeGpuPresentationFrame *frame;
	int baseVertex;

	if (!s_presentCaptureActive || (s_gpu.vertexIndex <= 0) || (s_gpu.splitIndex <= 0))
	{
		return;
	}

	frame = &s_presentFrames[s_presentWriteFrame];
	baseVertex = frame->vertexCount;
	if ((baseVertex + s_gpu.vertexIndex > (int)MAX_VERTEX_BUFFER_SIZE) ||
	    (frame->splitCount + s_gpu.splitIndex >= MAX_DRAW_SPLITS))
	{
		frame->valid = 0;
		frame->unsafeFramebufferFeedback = 1;
		frame->vertexCount = 0;
		frame->splitCount = 0;
		s_presentCaptureActive = 0;
		return;
	}

	memcpy(&frame->vertices[baseVertex], s_gpu.vertexBuffer,
	       (size_t)s_gpu.vertexIndex * sizeof(GrVertex));

	if (!frame->haveClearDrawEnv)
	{
		frame->clearDrawEnv = s_gpu.splits[1].drawenv;
		frame->haveClearDrawEnv = 1;
	}

	for (int i = 1; i <= s_gpu.splitIndex; i++)
	{
		GPUDrawSplit *dst = &frame->splits[++frame->splitCount];
		*dst = s_gpu.splits[i];
		dst->startVertex += (u32)baseVertex;
		// Debug labels are not part of the visual state and may point at
		// transient caller-owned strings. Do not retain them across host frames.
		dst->debugText = NULL;
	}

	frame->vertexCount += s_gpu.vertexIndex;
	if (s_gpu.framebufferFeedbackRunActive)
	{
		frame->unsafeFramebufferFeedback = 1;
	}
}

internal void NativeGpu_ClearScopedPsyxOverride(void)
{
	if (!s_gpu.psyxScopedOverrideActive)
		return;

	s_gpu.overrideTexture = s_gpu.psyxScopedPrevTexture;
	s_gpu.overrideTextureWidth = s_gpu.psyxScopedPrevTextureWidth;
	s_gpu.overrideTextureHeight = s_gpu.psyxScopedPrevTextureHeight;
	s_gpu.psyxScopedOverrideActive = false;
}

void ClearSplits(void)
{
	NativeGpu_ClearScopedPsyxOverride();
	s_gpu.currentSplitDebugText = NULL;
	s_gpu.vertexIndex = 0;
	s_gpu.splitIndex = 0;
	s_gpu.splits[0].texFormat = (TexFormat)0xFFFF;
	s_gpu.splits[0].psxTexturedSemiTrans = false;
	s_gpu.splits[0].psxTextureOutputSTP = false;
	s_gpu.splits[0].psxDrawMaskSet = false;
	s_gpu.framebufferFeedbackRunActive = false;
}

int NativeGpu_GetStateSize(void)
{
	return (int)sizeof(struct NativeGpuSnapshot);
}

int NativeGpu_CaptureState(void *dst, int dstSize)
{
	struct NativeGpuSnapshot *snapshot = (struct NativeGpuSnapshot *)dst;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_GPU_STATE_MAGIC;
	snapshot->version = NATIVE_GPU_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->drawEnv = activeDrawEnv;
	snapshot->dispEnv = activeDispEnv;
	snapshot->gpuDisabledState = g_GPUDisabledState;
	snapshot->psxDrawMaskSet = s_gpu.psxDrawMaskSet;

	return NativeRenderer_CaptureVRAMState(snapshot->vram, sizeof(snapshot->vram));
}

int NativeGpu_RestoreState(const void *src, int srcSize)
{
	const struct NativeGpuSnapshot *snapshot = (const struct NativeGpuSnapshot *)src;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_GPU_STATE_MAGIC) || (snapshot->version != NATIVE_GPU_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->gpuDisabledState < 0) || (snapshot->gpuDisabledState > 1) || (snapshot->psxDrawMaskSet < 0) || (snapshot->psxDrawMaskSet > 1))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}
	if (!NativeRenderer_RestoreVRAMState(snapshot->vram, sizeof(snapshot->vram)))
	{
		return 0;
	}

	activeDrawEnv = snapshot->drawEnv;
	activeDispEnv = snapshot->dispEnv;
	g_GPUDisabledState = snapshot->gpuDisabledState;
	s_gpu.psxDrawMaskSet = snapshot->psxDrawMaskSet;
	ClearSplits();
	return 1;
}

internal void DrawEnvDimensionsInt(int *width, int *height)
{
	if (activeDrawEnv.dfe)
	{
		*width = activeDispEnv.disp.w;
		*height = activeDispEnv.disp.h;
	}
	else
	{
		*width = activeDrawEnv.clip.w;
		*height = activeDrawEnv.clip.h;
	}
}

void DrawEnvOffset(float *ofsX, float *ofsY)
{
	if (activeDrawEnv.dfe)
	{
		// NOTE(aalhendi): Convert PS1 VRAM-page draw offsets into display-relative host-screen offsets.
		// CTR alternates draw pages at y=0 and y=0x128; using raw modulo VRAM offsets shifts every other native frame vertically.
		// Width/height are irrelevant to this offset; do not recompute them for every primitive.
		*ofsX = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
	}
	else
	{
		*ofsX = activeDrawEnv.ofs[0] - activeDrawEnv.clip.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDrawEnv.clip.y;
	}
}

internal void NativeGpu_PresentationEncodeAxis(float value, s16 *baseOut, s8 *fracOut);
internal void NativeGpu_PresentationEncodeAxisFixed(s32 value16, s16 *baseOut, s8 *fracOut);
internal int GetVertexPresentationMetadata(
	VERTTYPE *position, s32 *x16, s32 *y16, u16 *depth)
{
	const u32 packed =
		(u32)(u16)position[0] | ((u32)(u16)position[1] << 16);

	return NativeGTE_GetStoredVertexMetadata(
		position,
		packed,
		g_cfg_subpixelGeometry ? x16 : NULL,
		g_cfg_subpixelGeometry ? y16 : NULL,
		depth);
}

internal void MakeVertexPosition(GrVertex *vertex, VERTTYPE *position, float ofsX, float ofsY)
{
	vertex->x = position[0] + ofsX;
	vertex->y = position[1] + ofsY;
	vertex->_p0 = 0;
	vertex->_p1 = 0;
}

internal inline void NativeGpu_ClearVertexMetadata(GrVertex *vertex, int count)
{
	/* Position, UV and color are overwritten by the primitive builders below.
	 * Only the native sideband fields need a known zero baseline. Clearing just
	 * these 8 bytes avoids zeroing and immediately rewriting the full 24-byte
	 * vertex for every PS1 primitive. */
	for (int i = 0; i < count; ++i)
	{
		vertex[i].tcx = 0;
		vertex[i].tcy = 0;
		vertex[i]._p0 = 0;
		vertex[i]._p1 = 0;
		vertex[i].depth = 0;
		vertex[i]._depthPad = 0;
	}
}

void LineSwapSourceVerts(VERTTYPE **p0, VERTTYPE **p1, u8 **c0, u8 **c1)
{
	// swap line coordinates for left-to-right and up-to-bottom direction
	if (((*p0)[0] > (*p1)[0]) || ((*p0)[1] > (*p1)[1] && (*p0)[0] == (*p1)[0]))
	{
		VERTTYPE *tmp = *p0;
		*p0 = *p1;
		*p1 = tmp;

		u8 *tmpCol = *c0;
		*c0 = *c1;
		*c1 = tmpCol;
	}
}

void MakeLineArray(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1)
{
	const VERTTYPE dx = p1[0] - p0[0];
	const VERTTYPE dy = p1[1] - p0[1];

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	NativeGpu_ClearVertexMetadata(vertex, 4);

	if (dx > abs((s16)dy))
	{ // horizontal
		vertex[0].x = p0[0] + ofsX;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = p1[0] + ofsX + 1;
		vertex[1].y = p1[1] + ofsY;

		vertex[2].x = vertex[1].x;
		vertex[2].y = vertex[1].y + 1;

		vertex[3].x = vertex[0].x;
		vertex[3].y = vertex[0].y + 1;
	}
	else
	{ // vertical
		vertex[0].x = p0[0] + ofsX;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = p1[0] + ofsX;
		vertex[1].y = p1[1] + ofsY + 1;

		vertex[2].x = vertex[1].x + 1;
		vertex[2].y = vertex[1].y;

		vertex[3].x = vertex[0].x + 1;
		vertex[3].y = vertex[0].y;
	} // TODO diagonal line alignment
}

void MakeVertexTriangle(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2)
{
	assert(p0);
	assert(p1);
	assert(p2);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	NativeGpu_ClearVertexMetadata(vertex, 3);

	MakeVertexPosition(&vertex[0], p0, ofsX, ofsY);
	MakeVertexPosition(&vertex[1], p1, ofsX, ofsY);
	MakeVertexPosition(&vertex[2], p2, ofsX, ofsY);

	/* Subpixel coordinates and perspective depth come from the same native GTE
	 * metadata tables. Fetch both in one lookup per vertex instead of probing
	 * the address/value hashes twice. */
	{
		s32 x16[3], y16[3];
		u16 depth[3];
		const int meta0 = GetVertexPresentationMetadata(p0, &x16[0], &y16[0], &depth[0]);
		const int meta1 = GetVertexPresentationMetadata(p1, &x16[1], &y16[1], &depth[1]);
		const int meta2 = GetVertexPresentationMetadata(p2, &x16[2], &y16[2], &depth[2]);

		// Never mix PS1 integer vertices with native subpixel vertices inside one
		// primitive. Partial metadata was visibly warping/jittering triangles.
		if (g_cfg_subpixelGeometry)
		{
			if ((meta0 & 1) && (meta1 & 1) && (meta2 & 1))
			{
				const s32 ofsX16 = (s32)ofsX * 65536;
				const s32 ofsY16 = (s32)ofsY * 65536;
				for (int i = 0; i < 3; i++)
				{
					NativeGpu_PresentationEncodeAxisFixed(x16[i] + ofsX16, &vertex[i].x, &vertex[i]._p0);
					NativeGpu_PresentationEncodeAxisFixed(y16[i] + ofsY16, &vertex[i].y, &vertex[i]._p1);
				}
				NativeGpu_RecordSubpixelPrimitive(1);
			}
			else
			{
				NativeGpu_RecordSubpixelPrimitive(0);
			}
		}

		if ((meta0 & 2) && (meta1 & 2) && (meta2 & 2))
		{
			for (int i = 0; i < 3; i++)
				vertex[i].depth = depth[i];
		}
	}
}

void MakeVertexQuad(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2, VERTTYPE *p3)
{
	assert(p0);
	assert(p1);
	assert(p2);
	assert(p3);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	NativeGpu_ClearVertexMetadata(vertex, 4);

	MakeVertexPosition(&vertex[0], p0, ofsX, ofsY);
	MakeVertexPosition(&vertex[1], p1, ofsX, ofsY);
	MakeVertexPosition(&vertex[2], p2, ofsX, ofsY);
	MakeVertexPosition(&vertex[3], p3, ofsX, ofsY);

	{
		s32 x16[4], y16[4];
		u16 depth[4];
		const int meta0 = GetVertexPresentationMetadata(p0, &x16[0], &y16[0], &depth[0]);
		const int meta1 = GetVertexPresentationMetadata(p1, &x16[1], &y16[1], &depth[1]);
		const int meta2 = GetVertexPresentationMetadata(p2, &x16[2], &y16[2], &depth[2]);
		const int meta3 = GetVertexPresentationMetadata(p3, &x16[3], &y16[3], &depth[3]);

		if (g_cfg_subpixelGeometry)
		{
			if ((meta0 & 1) && (meta1 & 1) && (meta2 & 1) && (meta3 & 1))
			{
				const s32 ofsX16 = (s32)ofsX * 65536;
				const s32 ofsY16 = (s32)ofsY * 65536;
				for (int i = 0; i < 4; i++)
				{
					NativeGpu_PresentationEncodeAxisFixed(x16[i] + ofsX16, &vertex[i].x, &vertex[i]._p0);
					NativeGpu_PresentationEncodeAxisFixed(y16[i] + ofsY16, &vertex[i].y, &vertex[i]._p1);
				}
				NativeGpu_RecordSubpixelPrimitive(1);
			}
			else
			{
				NativeGpu_RecordSubpixelPrimitive(0);
			}
		}

		if ((meta0 & 2) && (meta1 & 2) && (meta2 & 2) && (meta3 & 2))
		{
			for (int i = 0; i < 4; i++)
				vertex[i].depth = depth[i];
		}
	}
}

void MakeVertexRect(GrVertex *vertex, VERTTYPE *p0, s16 w, s16 h)
{
	assert(p0);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	NativeGpu_ClearVertexMetadata(vertex, 4);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = vertex[0].x;
	vertex[1].y = vertex[0].y + h;

	vertex[2].x = vertex[0].x + w;
	vertex[2].y = vertex[0].y + h;

	vertex[3].x = vertex[0].x + w;
	vertex[3].y = vertex[0].y;
}

void MakeTexcoordQuad(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, u8 *uv3, s16 page, s16 clut, u8 dither)
{
	if (!g_cfg_psxDithering) dither = 0;
	assert(uv0);
	assert(uv1);
	assert(uv2);
	assert(uv3);

	const u8 bright = 2;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv3[0];
	vertex[3].v = uv3[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;
	/*
	if (g_cfg_bilinearFiltering)
	{
	    vertex[0].tcx = -1;
	    vertex[0].tcy = -1;

	    vertex[1].tcx = -1;
	    vertex[1].tcy = -1;

	    vertex[2].tcx = -1;
	    vertex[2].tcy = -1;

	    vertex[3].tcx = -1;
	    vertex[3].tcy = -1;
	}*/
}

void MakeTexcoordTriangle(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, s16 page, s16 clut, u8 dither)
{
	if (!g_cfg_psxDithering) dither = 0;
	assert(uv0);
	assert(uv1);
	assert(uv2);

	const u8 bright = 2;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;
	/*
	if (g_cfg_bilinearFiltering)
	{
	    vertex[0].tcx = -1;
	    vertex[0].tcy = -1;

	    vertex[1].tcx = -1;
	    vertex[1].tcy = -1;

	    vertex[2].tcx = -1;
	    vertex[2].tcy = -1;

	    vertex[3].tcx = -1;
	    vertex[3].tcy = -1;
	}*/
}

void MakeTexcoordRect(GrVertex *vertex, u8 *uv, s16 page, s16 clut, s16 w, s16 h)
{
	assert(uv);

	// sim overflow
	if ((int)uv[0] + w > 255)
	{
		w = 255 - uv[0];
	}
	if ((int)uv[1] + h > 255)
	{
		h = 255 - uv[1];
	}

	const u8 bright = 2;
	const u8 dither = 0;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv[0];
	vertex[0].v = uv[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv[0];
	vertex[1].v = uv[1] + h;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv[0] + w;
	vertex[2].v = uv[1] + h;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv[0] + w;
	vertex[3].v = uv[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;

	if (g_cfg_bilinearFiltering)
	{
		vertex[0].tcx = -1;
		vertex[0].tcy = -1;

		vertex[1].tcx = -1;
		vertex[1].tcy = -1;

		vertex[2].tcx = -1;
		vertex[2].tcy = -1;

		vertex[3].tcx = -1;
		vertex[3].tcy = -1;
	}
}

void MakeTexcoordLineZero(GrVertex *vertex, u8 dither)
{
	if (!g_cfg_psxDithering) dither = 0;
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeTexcoordTriangleZero(GrVertex *vertex, u8 dither)
{
	if (!g_cfg_psxDithering) dither = 0;
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;
}

void MakeTexcoordQuadZero(GrVertex *vertex, u8 dither)
{
	if (!g_cfg_psxDithering) dither = 0;
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeColourNoShade(GrVertex *vertex, int n)
{
	--n;
	while (n >= 0)
	{
		vertex[n].r = 128;
		vertex[n].g = 128;
		vertex[n].b = 128;
		vertex[n].a = 255;
		--n;
	}
}

void MakeColourLine(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}
	assert(col0);
	assert(col1);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col1[0];
	vertex[2].g = col1[1];
	vertex[2].b = col1[2];
	vertex[2].a = 255;

	vertex[3].r = col0[0];
	vertex[3].g = col0[1];
	vertex[3].b = col0[2];
	vertex[3].a = 255;
}

void MakeColourTriangle(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 3);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;
}

void MakeColourQuad(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2, u8 *col3)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);
	assert(col3);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;

	vertex[3].r = col3[0];
	vertex[3].g = col3[1];
	vertex[3].b = col3[2];
	vertex[3].a = 255;
}

void TriangulateQuad()
{
	/*
	Triangulate like this:

	v0--v1
	|  / |
	| /  |
	v2--v3

	NOTE: v2 swapped with v3 during primitive parsing but it not shown here
	*/

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 4] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 5] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 2];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 2] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 3] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 1];
}

//------------------------------------------------------------------------------------------------------------------------

internal bool NativeGpu_RectOverlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	return (aw > 0) && (ah > 0) && (bw > 0) && (bh > 0) && (ax < bx + bw) && (bx < ax + aw) && (ay < by + bh) && (by < ay + ah);
}

internal bool NativeGpu_TPageOverlapsActiveDrawPage(int tpage)
{
	/* Textured CTR geometry tends to issue long runs with the same tpage and
	 * draw environment. This predicate is queried for every textured primitive,
	 * and AddSplit may query it again immediately for framebuffer feedback.
	 * Cache the exact input tuple so repeated primitives become a handful of
	 * integer comparisons instead of re-decoding the tpage and rectangle. */
	local_persist int cacheValid = 0;
	local_persist int cacheTPage;
	local_persist int cacheDfe;
	local_persist int cacheX;
	local_persist int cacheY;
	local_persist int cacheW;
	local_persist int cacheH;
	local_persist bool cacheResult;

	const int dfe = activeDrawEnv.dfe;
	const int clipX = activeDrawEnv.clip.x;
	const int clipY = activeDrawEnv.clip.y;
	const int clipW = activeDrawEnv.clip.w;
	const int clipH = activeDrawEnv.clip.h;

	if (cacheValid &&
	    cacheTPage == tpage &&
	    cacheDfe == dfe &&
	    cacheX == clipX && cacheY == clipY &&
	    cacheW == clipW && cacheH == clipH)
	{
		return cacheResult;
	}

	bool result = false;
	if (dfe && GetTPageFormat(tpage) == TF_16_BIT)
	{
		const int pageX = (tpage & 0xf) << 6;
		const int pageY = (tpage & 0x10) ? 0x100 : 0;
		result = NativeGpu_RectOverlaps(pageX, pageY, 0x100, 0x100, clipX, clipY, clipW, clipH);
	}

	cacheValid = 1;
	cacheTPage = tpage;
	cacheDfe = dfe;
	cacheX = clipX;
	cacheY = clipY;
	cacheW = clipW;
	cacheH = clipH;
	cacheResult = result;
	return result;
}

internal void NativeGpu_PrepareFramebufferFeedback(int tpage)
{
	if (!NativeGpu_TPageOverlapsActiveDrawPage(tpage))
	{
		return;
	}

	if (s_gpu.framebufferFeedbackRunActive)
	{
		return;
	}

	// NOTE(aalhendi): PS1 can draw into VRAM and immediately texture from that
	// same draw page. Native batches primitives, so screen-feedback effects
	// like heat warp need an explicit barrier before their framebuffer-sampling
	// polygons consume the VRAM texture.
	if (NativeGpu_HasPendingSplits())
	{
		DrawAllSplits();
	}

#if defined(CTR_INTERNAL)
	/*
	 * EXPERIMENTAL stress-test A/B:
	 * Screen-feedback state is retail 30 Hz content. On host-only presentation
	 * frames, keep the latest logical VRAM copy and only refresh the GPU-side
	 * framebuffer texture. This avoids a synchronous GL readback + 16-bit pack
	 * + VRAM re-upload hundreds of times per second.
	 *
	 * Keep normal gameplay untouched until visual QA confirms that 30 Hz
	 * feedback cadence is acceptable for heat-warp/blur-style effects.
	 */
	if (NativePerf_IsStressTestActive() &&
	    NativeWorld_IsActive() &&
	    (Platform_GetLegacy30HzTicks() <= 0))
	{
		static int s_loggedFeedbackCadenceExp = 0;
		if (!s_loggedFeedbackCadenceExp)
		{
			Platform_Log("[CTR GPU] EXP feedback cadence: host frames deferred, logical ticks materialized\n");
			s_loggedFeedbackCadenceExp = 1;
		}
		NativeRenderer_StoreFrameBufferDeferred(
			activeDrawEnv.clip.x, activeDrawEnv.clip.y,
			activeDrawEnv.clip.w, activeDrawEnv.clip.h);
	}
	else
#endif
	{
		NativeRenderer_StoreFrameBuffer(
			activeDrawEnv.clip.x, activeDrawEnv.clip.y,
			activeDrawEnv.clip.w, activeDrawEnv.clip.h);
	}
	if (s_presentCaptureActive)
	{
		s_presentFrames[s_presentWriteFrame].unsafeFramebufferFeedback = 1;
	}
	s_gpu.framebufferFeedbackRunActive = true;
}

internal void AddSplit(bool semiTrans, bool textured, bool framebufferFeedback)
{
	int tpage = activeDrawEnv.tpage;

	if (framebufferFeedback)
	{
		NativeGpu_PrepareFramebufferFeedback(tpage);
	}
	else
	{
		s_gpu.framebufferFeedbackRunActive = false;
	}

	GPUDrawSplit *curSplit = &s_gpu.splits[s_gpu.splitIndex];

	BlendMode blendMode = semiTrans ? GET_TPAGE_BLEND(tpage) : BM_NONE;
	TexFormat texFormat = GetTPageFormat(tpage);
	TextureID textureId = textured ? NativeRenderer_GetVRAMTexture() : NativeRenderer_GetWhiteTexture();
	bool psxTexturedSemiTrans = semiTrans && textured && s_gpu.overrideTexture == 0;
	// NOTE(aalhendi): PS1 framebuffer bit 15 follows sampled texture STP for
	// textured draws unless E6 forces it. Recursive screen-copy effects depend
	// on this bit surviving after the blended textured pass.
	bool psxTextureOutputSTP = textured && s_gpu.overrideTexture == 0;
	const bool nativeDepthCandidate = s_gpu.nativeDepthCandidate;
	const bool canMergeTexturedSemiTrans =
		psxTexturedSemiTrans && NativeRenderer_CanSinglePassPSXTexturedSemiTrans(blendMode);

	if (textured && s_gpu.overrideTexture != 0)
	{
		// override texture format, zero tpage
		texFormat = TF_32_BIT_RGBA;
		textureId = s_gpu.overrideTexture;
		psxTexturedSemiTrans = false;
	}

	// FIXME: compare drawing environment too?
	if ((!psxTexturedSemiTrans || canMergeTexturedSemiTrans) &&
	    curSplit->blendMode == blendMode && curSplit->texFormat == texFormat && curSplit->textureId == textureId &&
	    curSplit->drawPrimMode == s_gpu.drawPrimMode && curSplit->psxTexturedSemiTrans == psxTexturedSemiTrans &&
	    curSplit->psxTextureOutputSTP == psxTextureOutputSTP && curSplit->psxDrawMaskSet == s_gpu.psxDrawMaskSet &&
	    curSplit->nativeDepthCandidate == nativeDepthCandidate &&
	    curSplit->drawenv.clip.x == activeDrawEnv.clip.x && curSplit->drawenv.clip.y == activeDrawEnv.clip.y &&
	    curSplit->drawenv.clip.w == activeDrawEnv.clip.w && curSplit->drawenv.clip.h == activeDrawEnv.clip.h && curSplit->drawenv.dfe == activeDrawEnv.dfe &&
	    curSplit->debugText == s_gpu.currentSplitDebugText)
	{
		return;
	}

	curSplit->numVerts = s_gpu.vertexIndex - curSplit->startVertex;

	if (s_gpu.splitIndex + 1 >= MAX_DRAW_SPLITS)
	{
		NATIVE_GPU_ERROR("MAX_DRAW_SPLITS reached (too many blend modes, texture formats, drawEnv clip rects, dfe switches), expect rendering errors\n");
		return;
	}

	GPUDrawSplit *split = &s_gpu.splits[++s_gpu.splitIndex];
	split->blendMode = blendMode;
	split->texFormat = texFormat;
	split->textureId = textureId;
	split->drawPrimMode = s_gpu.drawPrimMode;
	split->psxTexturedSemiTrans = psxTexturedSemiTrans;
	split->psxTextureOutputSTP = psxTextureOutputSTP;
	split->psxDrawMaskSet = s_gpu.psxDrawMaskSet;
	split->nativeDepthCandidate = nativeDepthCandidate;
	split->drawenv = activeDrawEnv;
	split->dispenv = activeDispEnv;
	split->debugText = s_gpu.currentSplitDebugText;

	split->drawenv.tw.w = s_gpu.overrideTextureWidth;
	split->drawenv.tw.h = s_gpu.overrideTextureHeight;

	split->startVertex = s_gpu.vertexIndex;
	split->numVerts = 0;
}

internal int NativeGpu_SplitHasFullDepth(const GPUDrawSplit *split)
{
	u32 endVertex;

	if (split == NULL || split->numVerts == 0)
		return 0;

	endVertex = split->startVertex + split->numVerts;
	if (endVertex > (u32)s_gpu.vertexIndex)
		return 0;

	for (u32 i = split->startVertex; i < endVertex; i++)
	{
		if (s_gpu.vertexBuffer[i].depth == 0)
			return 0;
	}

	return 1;
}

static int NativeGpu_LevelPCDepthInteropStressEnabled(void)
{
	static int checked, enabled;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_depth_interop_stress.enable","rb");
		enabled=(f!=NULL);
		if (f)fclose(f);
		checked=1;
	}
	return enabled && NativePerf_IsStressTestMeasuring();
}

void DrawSplit(const GPUDrawSplit *split)
{
	if (split->debugText)
	{
		NativeRenderer_PushDebugLabel(split->debugText);
	}

	const bool drawOnScreen = split->drawenv.dfe;
	if ((split->drawenv.clip.w <= 0) || (split->drawenv.clip.h <= 0))
	{
		// NOTE(aalhendi): VS/Battle end previews can shrink a losing viewport
		// into an empty retail draw area. Empty clips should consume no pixels,
		// and must not leak stale native offscreen/scissor state.
		NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
		NativeRenderer_SetOffscreenState(&split->drawenv.clip, &split->dispenv, 0);
		if (split->debugText)
		{
			NativeRenderer_PopDebugLabel();
		}
		return;
	}

	/*
	 * Hidden experimental Z-buffer:
	 * - only on-screen opaque splits;
	 * - only when every vertex carries reliable GTE depth;
	 * - never for semitransparent/offscreen/UI-style fallback geometry.
	 */
	/*
	 * Experimental Z is deliberately custom-only after QA showed that a
	 * global retail-world depth policy makes level geometry, smoke/particles
	 * and ghost-style effects disagree with CTR's authored OT ordering.
	 * Short-circuit before the vertex scan while Z is off: the previous code
	 * paid O(vertices) depth validation for every opaque split even disabled.
	 */
		/* Experimental interop makes full-depth retail PS1 triangles eligible,
	 * while unprojected UI, particles and ambiguous vertices remain OT-only. */
	/* Audit native SZ comparability against GPU terrain on a few model
	 * triangles around the bottom middle of the screen. */
	if (NativeGpu_LevelPCDepthInteropStressEnabled() &&
	    drawOnScreen && split->blendMode==BM_NONE &&
	    split->numVerts>=3)
	{
		static int sampleCount;
		if (sampleCount<22)
		{
			u32 end=split->startVertex+split->numVerts;
			if(end>(u32)s_gpu.vertexIndex)end=(u32)s_gpu.vertexIndex;
			for(u32 i=split->startVertex;i+2<end;i+=3)
			{
				const GrVertex *a=&s_gpu.vertexBuffer[i];
				const GrVertex *b=&s_gpu.vertexBuffer[i+1];
				const GrVertex *c=&s_gpu.vertexBuffer[i+2];
				float mx=((float)a->x+b->x+c->x)/3.0f;
				float my=((float)a->y+b->y+c->y)/3.0f;
				if(mx>=115.f && mx<=210.f && my>=120.f && my<=225.f &&
				    a->depth>0 && b->depth>0 && c->depth>0)
				{
					Platform_Log("[LEVEL PC GPU] PS1 SZ sample x=%.1f y=%.1f depth=%u,%u,%u nativeCandidate=%d texFmt=%d\n",
						mx,my,(unsigned)a->depth,(unsigned)b->depth,(unsigned)c->depth,
						(int)split->nativeDepthCandidate,(int)split->texFormat);
					if(++sampleCount>=22)break;
				}
			}
		}
	}
	NativeRenderer_SetExperimentalDepthEligible(
		NativeRenderer_GetExperimentalZBuffer() &&
		(split->nativeDepthCandidate || NativeGpu_LevelPCDepthInteropStressEnabled()) &&
		drawOnScreen &&
		(split->blendMode == BM_NONE) &&
		!split->psxTexturedSemiTrans &&
		NativeGpu_SplitHasFullDepth(split));

	NativeRenderer_SetStencilMode(split->drawPrimMode); // draw with mask 0x16

	NativeRenderer_SetTexture(split->textureId, split->texFormat);

	if (split->texFormat == TF_32_BIT_RGBA)
	{
		NativeRenderer_SetOverrideTextureSize(split->drawenv.tw.w, split->drawenv.tw.h);
	}

	NativeRenderer_SetPSXDrawMaskSet(split->psxDrawMaskSet);
	NativeRenderer_SetPSXTextureOutputSTP(split->psxTextureOutputSTP);

	NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
	NativeRenderer_SetOffscreenState(&split->drawenv.clip, &split->dispenv, !drawOnScreen);

	if (split->psxTexturedSemiTrans)
	{
		if (NativeRenderer_CanSinglePassPSXTexturedSemiTrans(split->blendMode))
		{
			int singlePassMode = 0;
			switch (split->blendMode)
			{
			case BM_AVERAGE: singlePassMode = 3; break;
			case BM_ADD: singlePassMode = 4; break;
			case BM_ADD_QUATER_SOURCE: singlePassMode = 5; break;
			default: break;
			}

			/* Modern path: shader emits a second per-fragment blend factor so STP
			 * and non-STP texels preserve PS1 behavior in one ordered draw call. */
			NativeRenderer_SetPSXTexturedSemiTransBlendMode(split->blendMode);
			NativeRenderer_SetPSXTextureSemiTransPass(singlePassMode);
			NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);
			NativeRenderer_SetPSXTextureSemiTransPass(0);
		}
		else
		{
			// Compatibility path (and PS1 subtract mode): two ordered passes.
			NativeRenderer_SetBlendMode(BM_NONE);
			NativeRenderer_SetPSXTextureSemiTransPass(1);
			NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);

			NativeRenderer_SetBlendMode(split->blendMode);
			NativeRenderer_SetPSXTextureSemiTransPass(2);
			NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);

			NativeRenderer_SetPSXTextureSemiTransPass(0);
		}
	}
	else
	{
		NativeRenderer_SetBlendMode(split->blendMode);
		NativeRenderer_SetPSXTextureSemiTransPass(0);
		NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);
	}

	if (split->debugText)
	{
		NativeRenderer_PopDebugLabel();
	}
}

internal int NativeGpu_PresentationSplitCompatible(const GPUDrawSplit *a, const GPUDrawSplit *b)
{
	if ((a == NULL) || (b == NULL) || (a->numVerts != b->numVerts))
	{
		return 0;
	}
	if ((a->blendMode != b->blendMode) || (a->texFormat != b->texFormat) ||
	    (a->textureId != b->textureId) || (a->drawPrimMode != b->drawPrimMode) ||
	    (a->psxTexturedSemiTrans != b->psxTexturedSemiTrans) ||
	    (a->psxTextureOutputSTP != b->psxTextureOutputSTP) ||
	    (a->psxDrawMaskSet != b->psxDrawMaskSet) ||
	    (a->drawenv.dfe != b->drawenv.dfe))
	{
		return 0;
	}
	if ((a->drawenv.clip.w != b->drawenv.clip.w) || (a->drawenv.clip.h != b->drawenv.clip.h))
	{
		return 0;
	}
	if ((a->dispenv.disp.w != b->dispenv.disp.w) || (a->dispenv.disp.h != b->dispenv.disp.h))
	{
		return 0;
	}
	return 1;
}

internal void NativeGpu_PresentationEncodeAxis(float value, s16 *baseOut, s8 *fracOut)
{
	/* Pick the nearest integer base so the residual is bounded to +/-0.5 px.
	   That lets the existing signed byte carry 1/256-pixel precision instead
	   of 1/128 without changing the vertex layout. */
	int base = (int)(value + (value >= 0.0f ? 0.5f : -0.5f));
	float fracFloat = (value - (float)base) * 256.0f;
	int frac = (int)(fracFloat + (fracFloat >= 0.0f ? 0.5f : -0.5f));

	if (frac > 127)
	{
		base++;
		frac -= 256;
	}
	else if (frac < -128)
	{
		base--;
		frac += 256;
	}

	if (base < -32768)
	{
		base = -32768;
		frac = 0;
	}
	else if (base > 32767)
	{
		base = 32767;
		frac = 0;
	}

	*baseOut = (s16)base;
	*fracOut = (s8)frac;
}

internal int NativeGpu_SubpixelQ16ToQ8(s32 value16)
{
	/* Screen coordinates are far inside s32 limits. Round 16.16 -> 24.8 using
	   integer shifts so the 32-bit build never pays for a 64-bit divide here. */
	if (value16 >= 0)
		return (value16 + 0x80) >> 8;
	return -(((-value16) + 0x80) >> 8);
}

internal void NativeGpu_PresentationEncodeAxisFixed(s32 value16, s16 *baseOut, s8 *fracOut)
{
	/* Same final 1/256-pixel representation as the float encoder, directly from
	   the GTE's 16.16 coordinate. */
	int q8 = NativeGpu_SubpixelQ16ToQ8(value16);
	int base;
	int frac;

	if (q8 >= 0)
		base = (q8 + 0x80) >> 8;
	else
		base = -(((-q8) + 0x80) >> 8);

	frac = q8 - (base * 256);
	if (frac > 127)
	{
		base++;
		frac -= 256;
	}
	else if (frac < -128)
	{
		base--;
		frac += 256;
	}

	if (base < -32768)
	{
		base = -32768;
		frac = 0;
	}
	else if (base > 32767)
	{
		base = 32767;
		frac = 0;
	}

	*baseOut = (s16)base;
	*fracOut = (s8)frac;
}

/* V152: Visibility must use the same 1/256-pixel positions sent to GL.
   The retail integer NCLIP can collapse distant floor triangles to a line.
   Do not change the emulated GTE: collision still needs retail arithmetic. */
global_variable volatile int s_nativeGpuSubpixelCullEnabled = 1;
s32 NativeGpu_CorrectSubpixelNclip(const void *p0, const void *p1, const void *p2, s32 retailNclip)
{
    const s16 *positions[3] = {p0, p1, p2};
    s64 x[3], y[3], area;
    s32 fineNclip;
    static u32 corrected = 0;
    if (!g_cfg_subpixelGeometry || !s_nativeGpuSubpixelCullEnabled) return retailNclip;
    for (int i = 0; i < 3; i++)
    {
        s32 x16, y16;
        u32 packed;
        if (positions[i] == NULL) return retailNclip;
        packed = (u32)(u16)positions[i][0] | ((u32)(u16)positions[i][1] << 16);
        if (!NativeGTE_GetStoredSubpixelFixed(positions[i], packed, &x16, &y16)) return retailNclip;

        /* Cull with the exact same 1/256-pixel quantization used by GL. */
        x[i] = NativeGpu_SubpixelQ16ToQ8(x16);
        y[i] = NativeGpu_SubpixelQ16ToQ8(y16);
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    fineNclip = (area > 0) - (area < 0);
    if (((retailNclip > 0) - (retailNclip < 0)) == fineNclip) return retailNclip;
    if (corrected++ < 12)
        NATIVE_GPU_LOG("V152 subpixel cull correction: integer=%d preciseSign=%d areaQ16=%lld\n",
                       retailNclip, fineNclip, (long long)area);
    return fineNclip;
}

/* OBJ-specific culling path: preserve the exact per-store fractional
   projection instead of the canonical shared-edge value used by level geometry. */
s32 NativeGpu_CorrectSubpixelNclipLocal(const void *p0, const void *p1, const void *p2, s32 retailNclip)
{
    const s16 *positions[3] = {p0, p1, p2};
    s64 x[3], y[3], area;
    s32 fineNclip;
    static u32 corrected = 0;

    if (!g_cfg_subpixelGeometry || !s_nativeGpuSubpixelCullEnabled)
        return retailNclip;

    for (int i = 0; i < 3; i++)
    {
        s32 x16, y16;
        u32 packed;
        if (positions[i] == NULL) return retailNclip;
        packed = (u32)(u16)positions[i][0] | ((u32)(u16)positions[i][1] << 16);
        if (!NativeGTE_GetStoredSubpixelFixedLocal(positions[i], packed, &x16, &y16))
            return retailNclip;
        x[i] = NativeGpu_SubpixelQ16ToQ8(x16);
        y[i] = NativeGpu_SubpixelQ16ToQ8(y16);
    }

    area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    fineNclip = (area > 0) - (area < 0);
    if (((retailNclip > 0) - (retailNclip < 0)) == fineNclip)
        return retailNclip;

    if (corrected++ < 12)
        NATIVE_GPU_LOG("OBJ local subpixel cull correction: integer=%d preciseSign=%d areaQ16=%lld\n",
                       retailNclip, fineNclip, (long long)area);
    return fineNclip;
}

internal void NativeGpu_PresentationInterpolateVertex(GrVertex *dst, const GrVertex *prev, const GrVertex *curr, float alpha)
{
	const float prevX = (float)prev->x + ((float)prev->_p0 * (1.0f / 256.0f));
	const float prevY = (float)prev->y + ((float)prev->_p1 * (1.0f / 256.0f));
	const float currX = (float)curr->x + ((float)curr->_p0 * (1.0f / 256.0f));
	const float currY = (float)curr->y + ((float)curr->_p1 * (1.0f / 256.0f));
	const float x = prevX + ((currX - prevX) * alpha);
	const float y = prevY + ((currY - prevY) * alpha);

	*dst = *curr;
	NativeGpu_PresentationEncodeAxis(x, &dst->x, &dst->_p0);
	NativeGpu_PresentationEncodeAxis(y, &dst->y, &dst->_p1);
}

int NativeGpu_PresentationDrawInterpolated(float alpha)
{
	NativeGpuPresentationFrame *curr;
	NativeGpuPresentationFrame *prev;
	int prevIndex;

	if (s_presentCurrentFrame < 0)
	{
		return 0;
	}

	curr = &s_presentFrames[s_presentCurrentFrame];
	prevIndex = s_presentCurrentFrame ^ 1;
	prev = &s_presentFrames[prevIndex];

	if (!curr->valid)
	{
		return 0;
	}

	if (!prev->valid || prev->serial+1 != curr->serial || curr->unsafeFramebufferFeedback || prev->unsafeFramebufferFeedback) alpha=1.0f;
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;

	memcpy(s_presentInterpolatedVertices, curr->vertices,
	       (size_t)curr->vertexCount * sizeof(GrVertex));

	for (int i = 1; alpha < 1.0f && i <= curr->splitCount; i++)
	{
		const GPUDrawSplit *currSplit = &curr->splits[i];
		const GPUDrawSplit *prevSplit;

		if ((i > prev->splitCount) || !currSplit->drawenv.dfe)
		{
			continue;
		}

		prevSplit = &prev->splits[i];
		if (!NativeGpu_PresentationSplitCompatible(prevSplit, currSplit))
		{
			continue;
		}

		for (u32 j = 0; j+2 < currSplit->numVerts; j+=3)
		{
			u32 ci=currSplit->startVertex+j, pi=prevSplit->startVertex+j;
			if (ci+2 >= (u32)curr->vertexCount || pi+2 >= (u32)prev->vertexCount) break;
			int match=1;
			for (int k=0;k<3;k++)
			{
				const GrVertex *p=&prev->vertices[pi+k], *c=&curr->vertices[ci+k];
				// All render attributes must match. Reject a complete triangle if
				// topology, UV animation, material, color or visibility changed.
				if (memcmp(&p->page,&c->page,12) || p->tcx!=c->tcx || p->tcy!=c->tcy ||
				    abs(c->x-p->x)>32 || abs(c->y-p->y)>32) match=0;
			}
			if (match) for (int k=0;k<3;k++)
				NativeGpu_PresentationInterpolateVertex(&s_presentInterpolatedVertices[ci+k], &prev->vertices[pi+k], &curr->vertices[ci+k], alpha);
		}
	}

	// Host-only redraw: build a fresh backbuffer from the saved native render
	// list without running CTR logic or feeding this presentation back into PS1 VRAM.
	NativeRenderer_BeginScene();
	if (curr->haveClearDrawEnv && curr->clearDrawEnv.isbg)
	{
		NativeRenderer_Clear(curr->clearDrawEnv.clip.x, curr->clearDrawEnv.clip.y,
		                     curr->clearDrawEnv.clip.w, curr->clearDrawEnv.clip.h,
		                     curr->clearDrawEnv.r0, curr->clearDrawEnv.g0, curr->clearDrawEnv.b0);
	}

	NativeRenderer_UpdateVertexBuffer(s_presentInterpolatedVertices, curr->vertexCount);
	for (int i = 1; i <= curr->splitCount; i++)
	{
		// Offscreen passes already produced their textures during the logical
		// frame. Replaying them here would mutate renderer resources used by CTR.
		if (curr->splits[i].drawenv.dfe)
		{
			DrawSplit(&curr->splits[i]);
		}
	}
	NativeRenderer_EndScene();
	NativeRenderer_DiscardFramebufferReadback();
	return 1;
}

internal void SetPSXMaskState(u32 code)
{
	s_gpu.psxDrawMaskSet = (code & 1) != 0;
}

//
// Draws all polygons after AggregatePTAG
//
void DrawAllSplits()
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
#ifdef CTR_INTERNAL
	if (g_dbg_emulatorPaused)
	{
		for (int i = 0; i < 3; i++)
		{
			GrVertex *vert = &s_gpu.vertexBuffer[g_dbg_polygonSelected + i];
			vert->r = 255;
			vert->g = 0;
			vert->b = 0;

			NATIVE_GPU_LOG("==========================================\n");
			NATIVE_GPU_LOG("POLYGON: %d\n", g_dbg_polygonSelected);
			NATIVE_GPU_LOG("X: %d Y: %d\n", vert->x, vert->y);
			NATIVE_GPU_LOG("U: %d V: %d\n", vert->u, vert->v);
			NATIVE_GPU_LOG("TP: %d CLT: %d\n", vert->page, vert->clut);

			NATIVE_GPU_LOG("==========================================\n");
		}

		Platform_PollHostEvents();
	}
#endif

	// Preserve this complete batch for host-only high-refresh interpolation
	// before the live GPU state is cleared below.
	NativeGpu_PresentationAppendCurrentBatch();

	/* Stress-only native terrain ordering experiment:
	 * draw the GPU-resident track before drawing PS1 OT split primitives,
	 * rather than after characters in NativeRenderer_EndScene.
	 * This intentionally lets karts/HUD draw on top of the track, but still
	 * needs full depth interop for correct wall/kart occlusion.
	 * Default path unchanged unless explicitly enabled and measured.
	 */
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	{
		static int nativeEarlyChecked, nativeEarlyEnabled, logged;
		if (!nativeEarlyChecked)
		{
			FILE *f=fopen("debug/level_pc_gpu_draw_early_stress.enable","rb");
			nativeEarlyEnabled=(f!=NULL);
			if(f)fclose(f);
			nativeEarlyChecked=1;
		}
		if(nativeEarlyEnabled && NativePerf_IsStressTestMeasuring())
		{
			if(!logged)
			{
				Platform_Log("[LEVEL PC GPU] STRESS ONLY: draw resident terrain BEFORE PS1 splits\n");
				logged=1;
			}
			NativeRenderer_LevelPCDrawQueued();
		}
	}
#endif
	// next code ideally should be called before EndScene
	NativeRenderer_UpdateVertexBuffer(s_gpu.vertexBuffer, s_gpu.vertexIndex);
	NativePerf_AddSplits((u32)((s_gpu.splitIndex > 0) ? s_gpu.splitIndex : 0));

	for (int i = 1; i <= s_gpu.splitIndex; i++)
	{
		DrawSplit(&s_gpu.splits[i]);
	}

	/* QA-only resident GPU buffers for OBJ assets staged by draw workers.
	 * Actual draw and OT translation remain retail and bit-identical. */
	NativeGpuModel_FlushStaged();
	ClearSplits();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
}

// forward declarations
int ParsePrimitive(P_TAG *polyTag);
int ParseTaglessPrimitive(u32 *command);

internal bool NativeGpu_IsValidOTLink(uintptr_t link)
{
	/* nextPrim() reached this point through NativeGpuLinks_ToHostPointer(),
	 * which already rejects tokens outside every registered range. Do not
	 * scan the host-range table a second time for the same OT hop. */
	return (link != 0) && ((link & (sizeof(u32) - 1)) == 0);
}

internal u32 NativeGpu_ReadPacketWordForLog(uintptr_t packet, int wordIndex)
{
	const struct PlatformMempackArena *arena = Platform_GetMempackArena();
	const uintptr_t word = packet + (uintptr_t)wordIndex * sizeof(u32);
	const uintptr_t end = word + sizeof(u32);

	if ((NativeGpuLinks_IsRegisteredHostRange((const void *)word, sizeof(u32))) && ((word & (sizeof(u32) - 1)) == 0))
	{
		return *(const u32 *)word;
	}

	if ((word < (uintptr_t)arena->base) || (end > (uintptr_t)arena->endOfMemory) || ((word & (sizeof(u32) - 1)) != 0))
	{
		return 0xffffffffu;
	}

	return *(const u32 *)word;
}

internal void NativeGpu_FormatPointerRegion(char *dst, size_t dstSize, uintptr_t ptr)
{
	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		snprintf(dst, dstSize, "no-gGT");
		return;
	}

	struct GameTracker *gGT = sdata->gGT;

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[playerIndex];
		const uintptr_t start = (uintptr_t)pb->ptrOT;
		const uintptr_t end = (uintptr_t)pb->renderBucketOTRangeEnd;
		if ((start != 0) && (end != 0) && (ptr >= start) && (ptr <= end))
		{
			snprintf(dst, dstSize, "pb%d.ot+0x%zx", playerIndex, (size_t)(ptr - start));
			return;
		}
	}

	struct PushBuffer *uiPB = &gGT->pushBuffer_UI;
	const uintptr_t uiStart = (uintptr_t)uiPB->ptrOT;
	const uintptr_t uiEnd = (uintptr_t)uiPB->renderBucketOTRangeEnd;
	if ((uiStart != 0) && (uiEnd != 0) && (ptr >= uiStart) && (ptr <= uiEnd))
	{
		snprintf(dst, dstSize, "ui.ot+0x%zx", (size_t)(ptr - uiStart));
		return;
	}

	for (int dbIndex = 0; dbIndex < 2; dbIndex++)
	{
		struct DB *db = &gGT->db[dbIndex];
		const uintptr_t primStart = (uintptr_t)db->primMem.start;
		const uintptr_t primEnd = (uintptr_t)db->primMem.end;
		if ((primStart != 0) && (ptr >= primStart) && (ptr < primEnd))
		{
			snprintf(dst, dstSize, "db%d.prim+0x%zx", dbIndex, (size_t)(ptr - primStart));
			return;
		}

		const uintptr_t otStart = (uintptr_t)db->otMem.start;
		const uintptr_t otEnd = (uintptr_t)db->otMem.end;
		if ((otStart != 0) && (ptr >= otStart) && (ptr < otEnd))
		{
			snprintf(dst, dstSize, "db%d.ot+0x%zx", dbIndex, (size_t)(ptr - otStart));
			return;
		}
	}

	snprintf(dst, dstSize, "unknown");
}

void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive)
{
	if (!p)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);

#ifdef CTR_NATIVE
	if (!singlePrimitive && !NativeGpuLinks_IsRegisteredHostPointer(p) && !isendprim(p))
	{
		char packetRegion[64];
		NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)p);
		NATIVE_GPU_ERROR("unregistered linked DrawOTag packet: packet=%p region=%s addr=%06x len=%d code=%02x words=%08x %08x %08x %08x\n", (void *)p,
		                 packetRegion, getaddr(p), getlen(p), getcode(p), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 0),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 2),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 3));
		NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
		return;
	}
#endif

	// setup single primitive flag (needed for AddSplits)
	s_gpu.drawPrimMode = singlePrimitive;

	if (singlePrimitive)
	{
		P_TAG *polyTag = (P_TAG *)p;
		ParsePrimitive(polyTag);

		GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
		lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;
	}
	else
	{
		// walk OT_TAG linked list
		u8 *basePacket = (u8 *)p;
		while (true)
		{
			const int tagLength = getlen(basePacket);
			if (tagLength > 0)
			{
				if (tagLength > 32)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("got invalid tag length %d, code %d packet=%p region=%s words=%08x %08x %08x %08x\n", tagLength,
					                 ((P_TAG *)basePacket)->code, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
					break;
				}

				u8 *currentPacket = basePacket;
				u8 *endPacket = basePacket + (tagLength + P_LEN) * sizeof(u32);
				int primLength = 0;
				if (currentPacket < endPacket)
				{
					primLength = ParsePrimitive((P_TAG *)currentPacket);
					currentPacket += (primLength + P_LEN) * sizeof(u32);
				}

				while (currentPacket < endPacket)
				{
					primLength = ParseTaglessPrimitive((u32 *)currentPacket);
					currentPacket += primLength * sizeof(u32);
				}

				if (currentPacket != endPacket)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("did not output valid primitive or ptag length is not valid (diff=%d packet=%p region=%s words=%08x %08x %08x %08x)\n",
					                 endPacket - currentPacket, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				}
			}

			GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
			lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;

			if (isendprim(basePacket))
			{
				break;
			}

			u8 *nextPacket = nextPrim(basePacket);
			if (!NativeGpu_IsValidOTLink((uintptr_t)nextPacket))
			{
				char packetRegion[64];
				char nextRegion[64];
				NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
				NativeGpu_FormatPointerRegion(nextRegion, sizeof(nextRegion), (uintptr_t)nextPacket);
				NATIVE_GPU_ERROR("invalid OT link: packet=%p region=%s addr=%06x next=%p nextRegion=%s len=%d code=%02x words=%08x %08x %08x %08x\n",
				                 (void *)basePacket, packetRegion, getaddr(basePacket), (void *)nextPacket, nextRegion, getlen(basePacket), getcode(basePacket),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				break;
			}

			basePacket = nextPacket;
		}
	}

	/* Never let a self-contained B3 override leak to the next DrawOTag call. */
	NativeGpu_ClearScopedPsyxOverride();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
}

internal inline int IsNull(POLY_FT3 *poly)
{
	return poly->x0 == -1 && poly->y0 == -1 && poly->x1 == -1 && poly->y1 == -1 && poly->x2 == -1 && poly->y2 == -1;
}

internal int ProcessFlatLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_F2 *poly = (LINE_F2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = c0;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x8: // TODO (unused)
	{
		LINE_F3 *poly = (LINE_F3 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 5;
	}
	case 0xc:
	{
		LINE_F4 *poly = (LINE_F4 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x2;
			VERTTYPE *p1 = &poly->x3;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 6;
	}
	}
	return 0;
}

internal int ProcessGouraudLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_G2 *poly = (LINE_G2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = &poly->r1;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x8:
	{
		// TODO: LINE_G3
		return 7;
	}
	case 0xC:
	{
		// TODO: LINE_G4
		return 9;
	}
	}
	return 0;
}

/* Native GPU decode for ordinary PS1 textured level/instance packets.
 * Packet/OT order, blend state, framebuffer feedback, texture sampling, and
 * full GTE metadata are unchanged. Only combine the redundant position, UV,
 * color and native metadata passes over each polygon into one vertex write.
 * Rollback marker (checked once at startup): debug/gpu_fused_legacy.disable
 */
internal bool NativeGpu_FusedLegacyTexturedEnabled(void)
{
	static int checked;
	static bool enabled;
	if (!checked)
	{
		FILE *marker = fopen("debug/gpu_fused_legacy.disable", "rb");
		enabled = (marker == NULL);
		if (marker != NULL) fclose(marker);
		checked = 1;
	}
	return enabled;
}

internal void NativeGpu_BuildTexturedPolygonFused(
	VERTTYPE *const *positions, const u8 *const *uvs,
	const u8 *const *colors, int vertexCount, int tpage, int clut,
	bool shadeTexOn)
{
	GrVertex *dst = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
	s32 x16[4], y16[4];
	u16 depths[4];
	int allSubpixel = g_cfg_subpixelGeometry != 0;
	int allDepth = 1;
	float ofsX, ofsY;
	const short texPage = GetTPageBase(tpage);
	const u8 dither = (g_cfg_psxDithering &&
		(GET_TPAGE_DITHER(tpage) || activeDrawEnv.dtd)) ? 1 : 0;

	assert(vertexCount == 3 || vertexCount == 4);
	DrawEnvOffset(&ofsX, &ofsY);

	for (int i = 0; i < vertexCount; ++i)
	{
		GrVertex *v = &dst[i];
		VERTTYPE *xy = positions[i];
		const u8 *uv = uvs[i];
		const u8 *color = colors[i];

		/* Match MakeVertexPosition and NativeGpu_ClearVertexMetadata. */
		v->x = xy[0] + ofsX;
		v->y = xy[1] + ofsY;
		v->_p0 = 0;
		v->_p1 = 0;
		v->tcx = 0;
		v->tcy = 0;
		v->depth = 0;
		v->_depthPad = 0;

		/* Match MakeTexcoordTriangle/Quad. */
		v->page = texPage;
		v->clut = clut;
		v->u = uv[0];
		v->v = uv[1];
		v->bright = 2;
		v->dither = dither;

		/* Match MakeColourTriangle/Quad, including flat unshaded FT*. */
		v->r = shadeTexOn ? color[0] : 128;
		v->g = shadeTexOn ? color[1] : 128;
		v->b = shadeTexOn ? color[2] : 128;
		v->a = 255;

		/* Read metadata while the source PS1 packet is still alive. */
		const int meta = GetVertexPresentationMetadata(
			xy, &x16[i], &y16[i], &depths[i]);
		allSubpixel &= (meta & 1) != 0;
		allDepth &= (meta & 2) != 0;
	}

	/* Match the all-or-nothing subpixel rule (no cracked PS1 edges).
	 * Keep the original integral draw-env offset conversion, depth and
	 * 1/256-pixel quantization exactly. */
	if (g_cfg_subpixelGeometry)
	{
		if (allSubpixel)
		{
			const s32 ofsX16 = (s32)ofsX * 65536;
			const s32 ofsY16 = (s32)ofsY * 65536;
			for (int i = 0; i < vertexCount; ++i)
			{
				NativeGpu_PresentationEncodeAxisFixed(
					x16[i] + ofsX16, &dst[i].x, &dst[i]._p0);
				NativeGpu_PresentationEncodeAxisFixed(
					y16[i] + ofsY16, &dst[i].y, &dst[i]._p1);
			}
			NativeGpu_RecordSubpixelPrimitive(1);
		}
		else
		{
			NativeGpu_RecordSubpixelPrimitive(0);
		}
	}

	if (allDepth)
	{
		for (int i = 0; i < vertexCount; ++i)
			dst[i].depth = depths[i];
	}
}

internal int ProcessFlatPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_F3 *poly = (POLY_F3 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 0);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);

		s_gpu.vertexIndex += 3;

		return 4;
	}
	case 0x4:
	{
		POLY_FT3 *poly = (POLY_FT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		// It is an official hack from SCE devs to not use DR_TPAGE and instead use null polygon
		if (!IsNull(poly))
		{
			AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			if (NativeGpu_FusedLegacyTexturedEnabled())
			{
				NativeGpu_BuildTexturedPolygonFused(
					(VERTTYPE *[3]){ &poly->x0, &poly->x1, &poly->x2 },
					(const u8 *[3]){ &poly->u0, &poly->u1, &poly->u2 },
					(const u8 *[3]){ &poly->r0, &poly->r0, &poly->r0 },
					3, poly->tpage, poly->clut, shadeTexOn);
			}
			else
			{
				MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
				MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut,
				                     GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
				MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);
			}

			s_gpu.vertexIndex += 3;
		}
		return 7;
	}
	case 0x8:
	{
		POLY_F4 *poly = (POLY_F4 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;
		return 5;
	}
	case 0xC:
	{
		POLY_FT4 *poly = (POLY_FT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		if (NativeGpu_FusedLegacyTexturedEnabled())
		{
			NativeGpu_BuildTexturedPolygonFused(
				(VERTTYPE *[4]){ &poly->x0, &poly->x1, &poly->x3, &poly->x2 },
				(const u8 *[4]){ &poly->u0, &poly->u1, &poly->u3, &poly->u2 },
				(const u8 *[4]){ &poly->r0, &poly->r0, &poly->r0, &poly->r0 },
				4, poly->tpage, poly->clut, shadeTexOn);
		}
		else
		{
			MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
			MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
			                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
			MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);
		}

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 9;
	}
	}
	return 0;
}

internal int ProcessGouraudPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_G3 *poly = (POLY_G3 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 1);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);

		s_gpu.vertexIndex += 3;

		return 6;
	}
	case 0x4:
	{
		POLY_GT3 *poly = (POLY_GT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		if (NativeGpu_FusedLegacyTexturedEnabled())
		{
			NativeGpu_BuildTexturedPolygonFused(
				(VERTTYPE *[3]){ &poly->x0, &poly->x1, &poly->x2 },
				(const u8 *[3]){ &poly->u0, &poly->u1, &poly->u2 },
				(const u8 *[3]){ &poly->r0, &poly->r1, &poly->r2 },
				3, poly->tpage, poly->clut, shadeTexOn);
		}
		else
		{
			MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
			MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut, GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
			MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);
		}

		s_gpu.vertexIndex += 3;

		return 9;
	}
	case 0x8:
	{
		POLY_G4 *poly = (POLY_G4 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 1);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 8;
	}
	case 0xC:
	{
		POLY_GT4 *poly = (POLY_GT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		if (NativeGpu_FusedLegacyTexturedEnabled())
		{
			NativeGpu_BuildTexturedPolygonFused(
				(VERTTYPE *[4]){ &poly->x0, &poly->x1, &poly->x3, &poly->x2 },
				(const u8 *[4]){ &poly->u0, &poly->u1, &poly->u3, &poly->u2 },
				(const u8 *[4]){ &poly->r0, &poly->r1, &poly->r3, &poly->r2 },
				4, poly->tpage, poly->clut, shadeTexOn);
		}
		else
		{
			MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
			MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
			                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
			MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);
		}

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 12;
	}
	}
	return 0;
}

internal int ProcessTileAndSprt(P_TAG *polyTag)
{
	// NOTE: TILE does not support switching shadeTex on real PSX
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);

	switch (polyTag->code & 0xFD)
	{
	case 0x60:
	{
		TILE *poly = (TILE *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x64:
	{
		SPRT *poly = (SPRT *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, poly->w, poly->h);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x68:
	{
		TILE_1 *poly = (TILE_1 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 1, 1);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x70:
	{
		TILE_8 *poly = (TILE_8 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x74:
	{
		SPRT_8 *poly = (SPRT_8 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 8, 8);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x78:
	{
		TILE_16 *poly = (TILE_16 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x7C:
	{
		SPRT_16 *poly = (SPRT_16 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 16, 16);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	}
	return 0;
}

internal int ProcessDrawEnv(P_TAG *polyTag)
{
	const u32 *codePtr = (u32 *)&polyTag->pad0;
	int processedLongs = 0;
	bool fullDrawEnvPacket = false;
	for (int i = 0; i < polyTag->len; ++i)
	{
		const u32 code = codePtr[i];
		const int primType = code >> 24 & 0xF0;
		const int primSubType = code >> 24 & 0x0F;

		// NOTE(aalhendi): CTR can pack draw-env commands, tagless geometry,
		// and more draw-env commands into one OT entry. Stop at the first
		// non-E command so ParseTaglessPrimitive owns the geometry payload.
		if (primType != 0xE0)
		{
			return processedLongs;
		}

		switch (primSubType)
		{
		case 0x1:
		{
			// DR_TPAGE
			activeDrawEnv.tpage = (code & 0x1FF);
			activeDrawEnv.dtd = (code >> 9) & 1;
			// NOTE(aalhendi): Standalone DR_TPAGE packets use the same E1 word
			// for blend changes; only full DRAWENV packets retarget native
			// on-screen/offscreen rendering.
			if (fullDrawEnvPacket)
			{
				activeDrawEnv.dfe = (code >> 10) & 1;
			}
			break;
		}
		case 0x2:
		{
			// DR_TWIN
			activeDrawEnv.tw.w = (code & 0x1F);
			activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
			activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
			activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
			break;
		}
		case 0x3:
		{
			// DR_AREA
			activeDrawEnv.clip.x = code & 1023;
			activeDrawEnv.clip.y = (code >> 10) & 1023;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x4:
		{
			// DR_AREA (second part)
			activeDrawEnv.clip.w = code & 1023;
			activeDrawEnv.clip.h = (code >> 10) & 1023;

			activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
			activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x5:
		{
			// DR_OFFSET
			activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
			activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
			fullDrawEnvPacket = true;
			break;
		}
		case 0x6:
		{
			SetPSXMaskState(code);
			break;
		}
		case 0:
			// NOTE(aalhendi): ctr-native local divergence for CTR OTs. A zero
			// word can be terminal draw-env padding, or the tag word for the
			// next primitive packed into the same OT entry.
			// return processedLongs;
			if (i + 1 != polyTag->len)
			{
				return processedLongs;
			}
			break;
		}
		++processedLongs;
	}

	return processedLongs;
}

internal void ProcessDrawEnvCommand(u32 code)
{
	const int primSubType = code >> 24 & 0x0F;

	switch (primSubType)
	{
	case 0x1:
		activeDrawEnv.tpage = (code & 0x1FF);
		activeDrawEnv.dtd = (code >> 9) & 1;
		break;
	case 0x2:
		activeDrawEnv.tw.w = (code & 0x1F);
		activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
		activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
		activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
		break;
	case 0x3:
		activeDrawEnv.clip.x = code & 1023;
		activeDrawEnv.clip.y = (code >> 10) & 1023;
		break;
	case 0x4:
		activeDrawEnv.clip.w = code & 1023;
		activeDrawEnv.clip.h = (code >> 10) & 1023;
		activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
		activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
		break;
	case 0x5:
		activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
		activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
		break;
	case 0x6:
		SetPSXMaskState(code);
		break;
	}
}

internal int ProcessPsyXPrims(P_TAG *polyTag)
{
	const int primSubType = polyTag->code & 0x0F;

	switch (primSubType)
	{
	case 0x01:
	{
		DR_PSYX_TEX *psytex = (DR_PSYX_TEX *)polyTag;
		s_gpu.overrideTexture = psytex->code[0] & 0xFFFFFF;
		s_gpu.overrideTextureWidth = psytex->code[1] & 0xFFF;
		s_gpu.overrideTextureHeight = psytex->code[1] >> 16 & 0xFFF;
		return 2;
	}
	case 0x03:
	{
		DR_PSYX_TEX_GT3 *psytri = (DR_PSYX_TEX_GT3 *)polyTag;

		/*
		 * B3 is self-contained, but consecutive custom triangles commonly use
		 * the same external texture. Keep the override scoped across adjacent B3
		 * packets so AddSplit can batch them. ParsePrimitive restores the state
		 * immediately before any non-B3 primitive, preserving retail ordering.
		 */
		if (!s_gpu.psyxScopedOverrideActive)
		{
			s_gpu.psyxScopedPrevTexture = s_gpu.overrideTexture;
			s_gpu.psyxScopedPrevTextureWidth = s_gpu.overrideTextureWidth;
			s_gpu.psyxScopedPrevTextureHeight = s_gpu.overrideTextureHeight;
			s_gpu.psyxScopedOverrideActive = true;
		}
		s_gpu.overrideTexture = psytri->code[0] & 0xFFFFFF;
		s_gpu.overrideTextureWidth = psytri->code[1] & 0xFFF;
		s_gpu.overrideTextureHeight = psytri->code[1] >> 16 & 0xFFF;

		/* DR_PSYX_TEX_GT3 is emitted by the native custom OBJ/vehicle path.
		 * Tag only this primitive as eligible for the experimental custom-only
		 * depth buffer; retail world, particles, ghosts and UI stay OT-only. */
		{
			const int firstVertexIndex = s_gpu.vertexIndex;
			s_gpu.nativeDepthCandidate = true;

			if (psytri->nativeFlags & DR_PSYX_TEX_GT3_FLAG_EXACT_METADATA)
			{
				POLY_GT3 *poly = &psytri->poly;
				const bool semiTrans = (poly->code & 2) != 0;
				GrVertex *firstVertex;
				float ofsX, ofsY;

				activeDrawEnv.tpage = poly->tpage;
				AddSplit(semiTrans, true,
				         NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

				firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
				DrawEnvOffset(&ofsX, &ofsY);

				/* Fused native B3 decoder: fill every packed GrVertex field in
				 * one pass. The old path cleared metadata then ran separate
				 * position, UV, color and high-resolution sideband passes over
				 * the same three vertices for every OBJ wheel/kart triangle.
				 * No change to triangle ordering, depth, color or UV data. */
				const VERTTYPE *xy[3] = {
					&poly->x0, &poly->x1, &poly->x2
				};
				const u8 *uv[3] = {
					&poly->u0, &poly->u1, &poly->u2
				};
				const u8 *colors[3] = {
					&poly->r0, &poly->r1, &poly->r2
				};
				const short texPage = GetTPageBase(poly->tpage);
				const u8 dither = (g_cfg_psxDithering &&
					(GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd)) ? 1 : 0;
				const bool hiResUV =
					(psytri->nativeFlags & DR_PSYX_TEX_GT3_FLAG_HIRES_UV) != 0;

				for (int j = 0; j < 3; ++j)
				{
					GrVertex *v = &firstVertex[j];
					v->x = xy[j][0] + ofsX;
					v->y = xy[j][1] + ofsY;
					v->page = texPage;
					v->clut = poly->clut;
					v->u = uv[j][0];
					v->v = uv[j][1];
					v->bright = 2;
					v->dither = hiResUV ? 255 : dither;
					v->r = colors[j][0];
					v->g = colors[j][1];
					v->b = colors[j][2];
					v->a = 255;
					v->tcx = hiResUV ? psytri->uvFrac[j][0] : 0;
					v->tcy = hiResUV ? psytri->uvFrac[j][1] : 0;
					v->_p0 = g_cfg_subpixelGeometry ? psytri->xyFrac[j][0] : 0;
					v->_p1 = g_cfg_subpixelGeometry ? psytri->xyFrac[j][1] : 0;
					v->depth = psytri->depth[j];
					v->_depthPad = 0;
				}
				if (g_cfg_subpixelGeometry)
					NativeGpu_RecordSubpixelPrimitive(1);
				s_gpu.vertexIndex += 3;
			}
			else
			{
				ProcessGouraudPoly((P_TAG *)&psytri->poly);
			}

			s_gpu.nativeDepthCandidate = false;

			/* B3 is the native custom-OBJ path. The embedded POLY_GT3 keeps
			 * PS1-compatible integer UVs, while this sideband restores the
			 * fractional Blender UV bits after the decoder has emitted
			 * its three GrVertex entries. dither=255 is an otherwise-unused
			 * marker consumed only by the external RGBA shader. */
			if ((psytri->nativeFlags & DR_PSYX_TEX_GT3_FLAG_HIRES_UV) &&
			    !(psytri->nativeFlags & DR_PSYX_TEX_GT3_FLAG_EXACT_METADATA) &&
			    (s_gpu.vertexIndex == firstVertexIndex + 3))
			{
				/* Exact native B3 metadata already writes fractional UVs and
				 * the RGBA shader marker in the fused vertex pass above. */
				for (int j = 0; j < 3; ++j)
				{
					GrVertex *vertex = &s_gpu.vertexBuffer[firstVertexIndex + j];
					vertex->tcx = psytri->uvFrac[j][0];
					vertex->tcy = psytri->uvFrac[j][1];
					vertex->dither = 255;
				}
			}
		}

		return (int)(sizeof(*psytri) / sizeof(u32)) - P_LEN;
	}
	case 0x05:
	{
		/* Native 3D static level packet at its native PS1 OT position.
		 * Unlike upstream Turbocharged B3, Crash Forge reserves B3 for
		 * modular OBJ triangles; B5 is intentionally collision-free.
		 * Flush only prior PS1 batches, draw queued VBO once, then continue
		 * parsing later PS1 sprites, characters, particles and UI in order. */
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		if (NativeRenderer_LevelPCEnabled())
		{
			if (NativeGpu_HasPendingSplits())
				DrawAllSplits();
			NativeRenderer_B5SnapshotStage(0);
			NativeRenderer_LevelPCDrawQueued();
			NativeRenderer_B5SnapshotStage(1);
			static int markerSeen;
			if(!markerSeen)
			{
				Platform_Log("[LEVEL PC GPU] OT B5 marker consumed; prior PS1 splits flushed, native terrain drawn\n");
				markerSeen=1;
			}
		}
#endif
		return 1;
	}
	case 0x04:
	{
		DR_PSYX_TEX_FT4 *psyquad = (DR_PSYX_TEX_FT4 *)polyTag;

		/* UI glyph/image packet: scope the external RGBA texture to one FT4.
		   This keeps the normal OT ordering while avoiding a bind/reset pair. */
		s_gpu.overrideTexture = psyquad->code[0] & 0xFFFFFF;
		s_gpu.overrideTextureWidth = psyquad->code[1] & 0xFFF;
		s_gpu.overrideTextureHeight = psyquad->code[1] >> 16 & 0xFFF;

		ProcessFlatPoly((P_TAG *)&psyquad->poly);

		s_gpu.overrideTexture = 0;
		s_gpu.overrideTextureWidth = 256;
		s_gpu.overrideTextureHeight = 256;

		return (int)(sizeof(*psyquad) / sizeof(u32)) - P_LEN;
	}
	case 0x02:
	{
		// [A] Psy-X custom debug marker packet
		DR_PSYX_DBGMARKER *psydbg = (DR_PSYX_DBGMARKER *)polyTag;
		s_gpu.currentSplitDebugText = psydbg->text;
		return 2;
	}
	}

	return 0;
}

// Processes primitive
// returns processed primitive primLength in longs
int ParsePrimitive(P_TAG *polyTag)
{
	const int primType = polyTag->code & 0xF0;
	const int primSubType = polyTag->code & 0x0F;

	/* B3 custom triangles carry their texture inline and may batch with the
	 * immediately adjacent B3 packet. Any other drawable primitive must first
	 * restore the texture state that existed before the custom run. */
	if (s_gpu.psyxScopedOverrideActive &&
	    !((primType == 0xB0) && (primSubType == 0x03)))
	{
		NativeGpu_ClearScopedPsyxOverride();
	}

	int primLength = 0;
	bool handledZeroLength = false;

	switch (primType)
	{
	case 0x00:
	{
		const int primSubType = polyTag->code & 0x0F;
		const u32 *codePtr = (u32 *)&polyTag->pad0;
		// NOTE(aalhendi): ctr-native local divergence. CTR RenderWeather can
		// emit a retail length-2 zero packet when weather is enabled but the
		// level has no fill-mode payload. The PSX consumes it by tag length;
		// the native parser must advance past it too.
		if (polyTag->len == 2 && codePtr[0] == 0 && codePtr[1] == 0)
		{
			primLength = 2;
		}
		else if (polyTag->len == 0 && *(u32 *)polyTag == 0)
		{
			// CTR ghost transparency packets include raw GPU NOP words between
			// draw-mode changes and triangle commands. They consume exactly one
			// command word; ParsePrimitivesLinkedList adds P_LEN to the return.
			handledZeroLength = true;
		}
		else if (primSubType == 0x0)
		{
			primLength = 3;
		}
		else if (primSubType == 0x1)
		{
			DR_MOVE *drmove = (DR_MOVE *)polyTag;
			const u32 rectPos = drmove->code[2];
			const u32 rectSize = drmove->code[4];

			const int y = drmove->code[3] >> 0x10 & 0xFFFF;
			const int x = drmove->code[3] & 0xFFFF;

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			MoveImage(&rect, x, y);
			primLength = 5;
		}
		else if (primSubType == 0x2)
		{
			// NOTE(aalhendi): ctr-native local divergence. CTR emits retail
			// FILL packets in OTs; the old PsyCross parser did not consume them, which caused
			// zero-length primitive spam.
			TILE *fill = (TILE *)polyTag;
			RECT16 rect;

			rect.x = fill->x0;
			rect.y = fill->y0;
			rect.w = fill->w;
			rect.h = fill->h;

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			ClearImage(&rect, fill->r0, fill->g0, fill->b0);
			primLength = 3;
		}
		break;
	}
	case 0x20:
		// Flat polygons
		primLength = ProcessFlatPoly(polyTag);
		break;
	case 0x30:
		// Gouraud shaded polygons
		primLength = ProcessGouraudPoly(polyTag);
		break;
	case 0x40:
		// Flat (single colour) Lines
		primLength = ProcessFlatLines(polyTag);
		break;
	case 0x50:
		// Gouraud lines
		primLength = ProcessGouraudLines(polyTag);
		break;
	case 0x60:
	case 0x70:
		// TILE and SPRT
		primLength = ProcessTileAndSprt(polyTag);
		break;
	case 0xA0:
		// DR_LOAD
		{
			DR_LOAD *drload = (DR_LOAD *)polyTag;
			const u32 rectPos = drload->code[1];
			const u32 rectSize = drload->code[2];

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			LoadImage(&rect, (uint32_t *)drload->p);

			// TODO(aalhendi): Audit whether CTR ever appends additional GPU
			// commands after a DR_LOAD payload in the same packet.
		}
		primLength = getlen(polyTag);
		break;
	case 0xB0:
		// [A] Psy-X custom primitives
		primLength = ProcessPsyXPrims(polyTag);
		break;
	case 0xE0:
		// Draw Env setup
		primLength = ProcessDrawEnv(polyTag);
		break;
		// default:
		//	NATIVE_GPU_ERROR("got %0x primitive\n", primType);
	}

	if (primLength == 0 && !handledZeroLength)
	{
		NATIVE_GPU_ERROR("Unhandled zero length %0x primitive\n", primType);
	}

	return primLength;
}

int ParseTaglessPrimitive(u32 *command)
{
	const u32 code = *command;
	const int primType = (code >> 24) & 0xF0;

	if (code == 0)
	{
		return 1;
	}

	if (primType == 0xE0)
	{
		ProcessDrawEnvCommand(code);
		return 1;
	}

	P_TAG *polyTag = (P_TAG *)(command - P_LEN);
	int primLength = ParsePrimitive(polyTag);

	if (primLength == 0)
	{
		NATIVE_GPU_ERROR("Unhandled tagless primitive %08x\n", code);
		return 1;
	}

	return primLength;
}
