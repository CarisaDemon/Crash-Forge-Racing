#include <common.h>
#if defined(CTR_NATIVE)
#include <platform/native_renderer.h>
void NativeGTE_GetLevelPCView(s16 outM[3][3], s32 outT[3], int *screen, int *wide);
s32 NativeGpu_CorrectSubpixelNclip(const void *p0, const void *p1, const void *p2, s32 retailNclip);
void NativeGTE_GetLevelPCProjectionOrigin(s32 *x16,s32 *y16);
int NativeGTE_GetStoredSubpixelFixedLocal(
	const void *address, u32 packedSxy, s32 *x16, s32 *y16);
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_perf.h>
#include <SDL3/SDL.h>
int RenderLists_NativeBoxPassesCameraFrustum(struct PushBuffer *pb, const struct BoundingBox *box);
int RenderLists_NativeBoxFullyInsideCameraFrustum(struct PushBuffer *pb, const struct BoundingBox *box);
#define WATER_PERF_BEGIN() NativePerf_BeginScope(NATIVE_PERF_BUCKET_WATER_RENDER)
#define WATER_PERF_END()   NativePerf_EndScope(NATIVE_PERF_BUCKET_WATER_RENDER)
#else
#define WATER_PERF_BEGIN() ((void)0)
#define WATER_PERF_END()   ((void)0)
#endif


struct DrawLevelOvr1PFaceSelector
{
	u32 selector;
	u8 drawOrderShift;
};

struct DrawLevelOvr1PNearSubdivisionCase
{
	u32 listHandlerAddress;
	u32 renderedHandlerAddress;
	u8 subIndices[2][4];
	u32 directMasks[2];
	u32 slotWords[2];
};

enum DrawLevelOvr1PLod
{
	DRAW_LEVEL_OVR1P_LOD_HIGH,
	DRAW_LEVEL_OVR1P_LOD_LOW,
};

enum DrawLevelOvr1PGteFlag
{
	DRAW_LEVEL_OVR1P_GTE_RTPT_OVERFLOW = 0x20000,
};

enum DrawLevelOvr1PDepth
{
	DRAW_LEVEL_OVR1P_MAX_OT_INDEX = 1020,
	DRAW_LEVEL_OVR1P_RECURSION_FRAME_SIZE = 0xb8,
	DRAW_LEVEL_OVR1P_MAX_NEAR_SUBDIV_DEPTH = 2,
	DRAW_LEVEL_OVR1P_CLIP_RECORD_GT3_SIZE = 0x30,
	DRAW_LEVEL_OVR1P_CLIP_RECORD_GT4_SIZE = 0x3c,
	DRAW_LEVEL_OVR1P_CLIP_RECORD_PRIM_RESERVE = 0xd68,
	DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X1 = 0x1040,
	DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X2 = 0x1380,
	DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT = 0x1a00,
	DRAW_LEVEL_OVR1P_BUCKET_RESERVE_FULL_DYNAMIC = 0x2700,
};

enum DrawLevelOvr1PDirectMask
{
	DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY = 0x4,
	DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY = 0x8,
	DRAW_LEVEL_OVR1P_DIRECT_QUAD = 0xc,
};

enum DrawLevelOvr1PClipBytes
{
	DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST = 0,
	DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED = 1,
};

enum DrawLevelOvr1PPrimCode
{
	DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO = -1,
};

enum DrawLevelOvr1PProjectedSource
{
	DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS,
	DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_WATER_COLOR_LO_FLAGS,
};

enum DrawLevelOvr1PGridSlotMode
{
	DRAW_LEVEL_OVR1P_GRID_SLOT_NONE,
	DRAW_LEVEL_OVR1P_GRID_SLOT_FACE,
	DRAW_LEVEL_OVR1P_GRID_SLOT_WORD,
};

static const u32 DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE = 0xffffffff;
static int sDrawLevelOvr1P_FullDynamicInheritedOtIndex;
static struct QuadBlock **sDrawLevelOvr1P_RenderedOverflowBase;
static u8 *sDrawLevelOvr1P_ClipRecordStart;
static u32 sDrawLevelOvr1P_PrimReserveBias;
static u32 sDrawLevelOvr1P_MosaicReloadSpanOverride;
static int sDrawLevelOvr1P_ListHandlersSeedRenderedCursor;

static inline u32 DrawLevelOvr1P_ReadPackedWord(const void *src);
static inline void DrawLevelOvr1P_WritePackedWord(void *dst, u32 value);

#if defined(CTR_NATIVE)
struct DrawLevelOvr1PProjectionCacheEntry
{
	u32 generation;
	u32 packedSxy;
	s32 x16;
	s32 y16;
	u16 depth;
};

static struct DrawLevelOvr1PProjectionCacheEntry *sDrawLevelOvr1PProjectionCache;
static int sDrawLevelOvr1PProjectionCacheCapacity;
static int sDrawLevelOvr1PProjectionCacheCount;
static const struct LevVertex *sDrawLevelOvr1PProjectionCacheBase;
static u32 sDrawLevelOvr1PProjectionCacheGeneration = 1;
static int sDrawLevelOvr1PProjectionCacheEnabled;
static int sDrawLevelOvr1PProjectionCacheGeomScreen;
static u64 sDrawLevelOvr1PProjectionCacheRequests;
static u64 sDrawLevelOvr1PProjectionCacheHits;
static u32 sDrawLevelOvr1PProjectionCacheFrames;

static int DrawLevelOvr1P_ProjectionCacheMarkerEnabled(void)
{
	static int checked;
	static int enabled;

	if (!checked)
	{
		FILE *marker = fopen("debug/level_projection_cache.enable", "rb");
		enabled = marker != NULL;
		if (marker != NULL)
			fclose(marker);
		checked = 1;
	}

	return enabled;
}

static void DrawLevelOvr1P_BeginProjectionCache(
	const struct mesh_info *mesh,
	const struct PushBuffer *pb)
{
	sDrawLevelOvr1PProjectionCacheEnabled = 0;

	if (!DrawLevelOvr1P_ProjectionCacheMarkerEnabled() ||
	    (mesh == NULL) || (pb == NULL) ||
	    (mesh->ptrVertexArray == NULL) ||
	    (mesh->numVertex <= 0))
	{
		return;
	}

	if (mesh->numVertex > sDrawLevelOvr1PProjectionCacheCapacity)
	{
		struct DrawLevelOvr1PProjectionCacheEntry *newCache;
		newCache = (struct DrawLevelOvr1PProjectionCacheEntry *)realloc(
			sDrawLevelOvr1PProjectionCache,
			(size_t)mesh->numVertex *
				sizeof(struct DrawLevelOvr1PProjectionCacheEntry));
		if (newCache == NULL)
			return;

		memset(
			newCache + sDrawLevelOvr1PProjectionCacheCapacity,
			0,
			(size_t)(mesh->numVertex -
				sDrawLevelOvr1PProjectionCacheCapacity) *
				sizeof(struct DrawLevelOvr1PProjectionCacheEntry));
		sDrawLevelOvr1PProjectionCache = newCache;
		sDrawLevelOvr1PProjectionCacheCapacity = mesh->numVertex;
	}

	sDrawLevelOvr1PProjectionCacheGeneration++;
	if (sDrawLevelOvr1PProjectionCacheGeneration == 0)
	{
		memset(
			sDrawLevelOvr1PProjectionCache,
			0,
			(size_t)sDrawLevelOvr1PProjectionCacheCapacity *
				sizeof(struct DrawLevelOvr1PProjectionCacheEntry));
		sDrawLevelOvr1PProjectionCacheGeneration = 1;
	}

	sDrawLevelOvr1PProjectionCacheBase = mesh->ptrVertexArray;
	sDrawLevelOvr1PProjectionCacheCount = mesh->numVertex;
	sDrawLevelOvr1PProjectionCacheGeomScreen = pb->distanceToScreen_PREV;
	sDrawLevelOvr1PProjectionCacheEnabled = 1;
	sDrawLevelOvr1PProjectionCacheFrames++;
}

static int DrawLevelOvr1P_ProjectionCacheIndex(
	const struct LevVertex *vertex)
{
	ptrdiff_t index;

	if (!sDrawLevelOvr1PProjectionCacheEnabled ||
	    (vertex == NULL) ||
	    (sDrawLevelOvr1PProjectionCacheBase == NULL))
		return -1;

	index = vertex - sDrawLevelOvr1PProjectionCacheBase;
	if ((index < 0) ||
	    (index >= sDrawLevelOvr1PProjectionCacheCount))
		return -1;

	return (int)index;
}

static int DrawLevelOvr1P_ProjectionCacheHas(
	const struct LevVertex *vertex)
{
	int index;

	index = DrawLevelOvr1P_ProjectionCacheIndex(vertex);
	if (index < 0)
		return 0;

	if (sDrawLevelOvr1PProjectionCache[index].generation !=
	    sDrawLevelOvr1PProjectionCacheGeneration)
	{
		return 0;
	}

	return 1;
}

static void DrawLevelOvr1P_ProjectionCacheRecordQuery(
	u32 requestedVertices,
	u32 reusedVertices)
{
	if (!sDrawLevelOvr1PProjectionCacheEnabled)
		return;

	sDrawLevelOvr1PProjectionCacheRequests += requestedVertices;
	sDrawLevelOvr1PProjectionCacheHits += reusedVertices;
}

static int DrawLevelOvr1P_ProjectionCacheGetDepth(
	const struct LevVertex *vertex,
	u32 *depthOut)
{
	int index;

	if (depthOut == NULL)
		return 0;

	index = DrawLevelOvr1P_ProjectionCacheIndex(vertex);
	if ((index < 0) ||
	    (sDrawLevelOvr1PProjectionCache[index].generation !=
	     sDrawLevelOvr1PProjectionCacheGeneration))
	{
		return 0;
	}

	*depthOut = sDrawLevelOvr1PProjectionCache[index].depth;
	return 1;
}

static int DrawLevelOvr1P_ProjectionCacheLoad(
	const struct LevVertex *vertex,
	struct DrawLevelOvr1PScratchVertex *projected,
	u32 *depthOut)
{
	int index;
	struct DrawLevelOvr1PProjectionCacheEntry *entry;

	if ((projected == NULL) || (depthOut == NULL))
		return 0;

	index = DrawLevelOvr1P_ProjectionCacheIndex(vertex);
	if (index < 0)
		return 0;

	entry = &sDrawLevelOvr1PProjectionCache[index];
	if (entry->generation != sDrawLevelOvr1PProjectionCacheGeneration)
		return 0;

	DrawLevelOvr1P_WritePackedWord(
		&projected->posScreen[0],
		entry->packedSxy);
	NativeGTE_SaveSXYStoreExact(
		&projected->posScreen[0],
		entry->packedSxy,
		entry->x16,
		entry->y16,
		entry->depth);
	*depthOut = entry->depth;
	return 1;
}

static void DrawLevelOvr1P_ProjectionCacheStore(
	const struct LevVertex *vertex,
	const struct DrawLevelOvr1PScratchVertex *projected,
	u32 depth)
{
	int index;
	u32 packedSxy;
	s32 x16;
	s32 y16;
	struct DrawLevelOvr1PProjectionCacheEntry *entry;

	index = DrawLevelOvr1P_ProjectionCacheIndex(vertex);
	if ((index < 0) || (projected == NULL))
		return;

	packedSxy = DrawLevelOvr1P_ReadPackedWord(&projected->posScreen[0]);
	if (!NativeGTE_GetStoredSubpixelFixedLocal(
			&projected->posScreen[0],
			packedSxy,
			&x16,
			&y16))
	{
		x16 = (s32)(s16)packedSxy * 65536;
		y16 = (s32)(s16)(packedSxy >> 16) * 65536;
	}

	entry = &sDrawLevelOvr1PProjectionCache[index];
	entry->generation = sDrawLevelOvr1PProjectionCacheGeneration;
	entry->packedSxy = packedSxy;
	entry->x16 = x16;
	entry->y16 = y16;
	entry->depth = (u16)depth;
}

static int DrawLevelOvr1P_ProjectionCacheDepthOverflows(u32 depth)
{
	return (u32)sDrawLevelOvr1PProjectionCacheGeomScreen >= (depth * 2u);
}

static void DrawLevelOvr1P_LogProjectionCache(void)
{
	if (!sDrawLevelOvr1PProjectionCacheEnabled ||
	    (sDrawLevelOvr1PProjectionCacheFrames < 300))
		return;

	Platform_Log(
		"[LEVEL PROJECTION CACHE] requests=%llu hits=%llu hitRate=%.1f%%\n",
		(unsigned long long)sDrawLevelOvr1PProjectionCacheRequests,
		(unsigned long long)sDrawLevelOvr1PProjectionCacheHits,
		sDrawLevelOvr1PProjectionCacheRequests != 0 ?
			(100.0 *
			 (double)sDrawLevelOvr1PProjectionCacheHits /
			 (double)sDrawLevelOvr1PProjectionCacheRequests) :
			0.0);

	sDrawLevelOvr1PProjectionCacheRequests = 0;
	sDrawLevelOvr1PProjectionCacheHits = 0;
	sDrawLevelOvr1PProjectionCacheFrames = 0;
}
#endif

static int DrawLevelOvr1P_NativeCameraAllowsQuadBlock(struct PushBuffer *pb, const struct QuadBlock *block)
{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	return (block != NULL) &&
	       RenderLists_NativeBoxPassesCameraFrustum(pb, &block->bbox);
#else
	(void)pb;
	(void)block;
	return 1;
#endif
}

static inline u32 DrawLevelOvr1P_ReadPackedWord(const void *src)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
	u32 value;
	memcpy(&value, src, sizeof(value));
	return value;
#else
	const u8 *bytes = (const u8 *)src;
	return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
#endif
}

static inline u16 DrawLevelOvr1P_ReadPackedHalf(const void *src)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
	u16 value;
	memcpy(&value, src, sizeof(value));
	return value;
#else
	const u8 *bytes = (const u8 *)src;
	return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
#endif
}

static inline void DrawLevelOvr1P_WritePackedWord(void *dst, u32 value)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
	memcpy(dst, &value, sizeof(value));
#else
	u8 *bytes = (u8 *)dst;
	bytes[0] = (u8)value;
	bytes[1] = (u8)(value >> 8);
	bytes[2] = (u8)(value >> 16);
	bytes[3] = (u8)(value >> 24);
#endif
}

static inline void DrawLevelOvr1P_WritePackedHalf(void *dst, u16 value)
{
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
	memcpy(dst, &value, sizeof(value));
#else
	u8 *bytes = (u8 *)dst;
	bytes[0] = (u8)value;
	bytes[1] = (u8)(value >> 8);
#endif
}

struct DrawLevelOvr1PFullDynamicRecursiveGate
{
	u32 directMask;
	int forceDirect;
};

static const int sDrawLevelOvr1PHighLodIndices[16] = {
    5, 0, 6, 4, 6, 4, 7, 1, 2, 5, 8, 6, 8, 6, 3, 7,
};

static const int sDrawLevelOvr1PLowLodIndices[4] = {
    2,
    0,
    3,
    1,
};

static const int sDrawLevelOvr1PFullDynamicLowIndices[4] = {
    0,
    1,
    2,
    3,
};

static struct TextureLayout *DrawLevelOvr1P_ResolveTexturePointerChecked(uintptr_t texturePtr);
static const int sDrawLevelOvr1PGridFaceIndices[4][4] = {
    {0, 4, 5, 6},
    {4, 1, 6, 7},
    {5, 6, 2, 8},
    {6, 7, 8, 3},
};

static const int sDrawLevelOvr1PGridExtraFaceIndices[4][4] = {
    {5, 0, 6, 1},
    {1, 3, 6, 8},
    {2, 0, 6, 4},
    {2, 6, 3, 7},
};

static const int sDrawLevelOvr1PGridMixedFaceIndices[4][4] = {
    {4, 1, 6, 3},
    {5, 6, 2, 3},
    {0, 1, 6, 7},
    {0, 6, 2, 8},
};

static const struct DrawLevelOvr1PFaceSelector sDrawLevelOvr1P4x1FaceSelectors[4] = {
    {0x00506478, 8},
    {0x5014788c, 13},
    {0x647828a0, 18},
    {0x788ca03c, 23},
};

// NOTE(aalhendi): Deepest generic-grid helpers at 0x800a535c..0x800a557c /
// 0x800a6260..0x800a6480 fall through to a 3x3 topology, not the 4x1 compact
// topology above. The bottom/right cases use projected record 8.
static const struct DrawLevelOvr1PNearSubdivisionCase sDrawLevelOvr1PDeepestGridSubdivisionCases[3] = {
    {0,
     0,
     {{0, 4, 2, 3}, {1, 3, 4, 2}},
     {DRAW_LEVEL_OVR1P_DIRECT_QUAD, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY},
     {DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE, DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE}},
    {0,
     0,
     {{1, 8, 0, 2}, {0, 1, 8, 3}},
     {DRAW_LEVEL_OVR1P_DIRECT_QUAD, DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY},
     {DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE, DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE}},
    {0, 0, {{0, 4, 2, 8}, {4, 1, 8, 3}}, {DRAW_LEVEL_OVR1P_DIRECT_QUAD, DRAW_LEVEL_OVR1P_DIRECT_QUAD}, {0x0, 0xc}},
};

// NOTE(aalhendi): Retail overlay 226 dispatches the render-list offsets from
// FULL_DYNAMIC_LIST down to 4X4_RENDERED through tables at 0x800ab40c/0x800ab438.
static const struct DrawLevelOvr1PBucket sDrawLevelOvr1PBuckets[] = {
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X4_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_4X4_RENDERED,
     DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X4_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_4X4_LIST, DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_DYNAMIC_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED,
     DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_DYNAMIC_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST, DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X2_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED,
     DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X2_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST, DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X1_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED,
     DRAW_LEVEL_OVR1P_LOD_LOW},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X1_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST, DRAW_LEVEL_OVR1P_LOD_LOW},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_WATER_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED, DRAW_LEVEL_OVR1P_BUCKET_WATER_RENDERED,
     DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_WATER_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_WATER_LIST, DRAW_LEVEL_OVR1P_LOD_HIGH},
    {DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_FULL_DYNAMIC_LIST, DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST, DRAW_LEVEL_OVR1P_BUCKET_FULL_DYNAMIC_LIST,
     DRAW_LEVEL_OVR1P_LOD_LOW},
};

CTR_STATIC_ASSERT(sizeof(sDrawLevelOvr1PBuckets) / sizeof(sDrawLevelOvr1PBuckets[0]) == OVR226_BUCKET_COUNT);
CTR_STATIC_ASSERT(sizeof(struct OverlayRDATA_226) == 0x564);
CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvrBucketSetupRecord) == 0x64);
CTR_STATIC_ASSERT(sizeof(((struct OverlayRDATA_226 *)0)->scratchInitTable) == 0x60);
CTR_STATIC_ASSERT(sizeof(((struct OverlayRDATA_226 *)0)->clipRecordJumpTable) == 0x60);

static void DrawLevelOvr1P_SetGridFaceSlot(const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex);
static int DrawLevelOvr1P_IsDeepestSubdivisionFrame(const struct DrawLevelOvr1PScratchVertex *projected);
static void Ovr226_800a0d34_SetEntryGteAndCameraScratch(struct PushBuffer *pb);

static struct DrawLevelOvr1PStableScratch *DrawLevelOvr1P_Scratch(void)
{
	return CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PStableScratch, 0);
}

static struct MainRenderLevelGeometryScratch *DrawLevelOvr1P_RenderScratch(void)
{
	return &DrawLevelOvr1P_Scratch()->render;
}

static u32 *DrawLevelOvr1P_TerminalReturnPcScratch(void)
{
	return CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_TERMINAL_RETURN_PC_OFFSET);
}

static struct DrawLevelOvr1PScratchVertex *DrawLevelOvr1P_TerminalClipVertex(int index)
{
	return CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex,
	                          DRAW_LEVEL_OVR1P_TERMINAL_CLIP_VERTEX_OFFSET + (index * (int)sizeof(struct DrawLevelOvr1PScratchVertex)));
}

static void DrawLevelOvr1P_SetActiveDrawOrderLow(const struct QuadBlock *block)
{
	DrawLevelOvr1P_Scratch()->drawOrderOrHeader = block->draw_order_low;
}

static u32 DrawLevelOvr1P_GetActiveDrawOrderLow(void)
{
	return DrawLevelOvr1P_Scratch()->drawOrderOrHeader;
}

static void DrawLevelOvr1P_CopyScratchWords(const u32 *source, const struct DrawLevelOvrBucketSetupCopy *copy)
{
	u32 *scratch = CTR_SCRATCHPAD_PTR(u32, copy->scratchOffset);
	memcpy(scratch, source, ((size_t)copy->lastWordIndex + 1u) * sizeof(u32));
}

static void Ovr226_800ab3dc_CopyClipRecordJumpTable(void)
{
	u32 *clipRecordJumpTable = CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_GT3_CLIP_RECORD_JUMP_TABLE_OFFSET);
	memcpy(clipRecordJumpTable, R226.clipRecordJumpTable,
	       (size_t)OVR226_CLIP_RECORD_JUMP_WORD_COUNT * sizeof(u32));
}

static int Ovr226_800ab3d4_EmptyGT4ClipRecordLabel(void)
{
	return 1;
}

static const struct DrawLevelOvrBucketSetupRecord *DrawLevelOvr1P_FindBucketSetupRecord(u32 setupAddress)
{
	for (s32 bucketIndex = 0; bucketIndex < OVR226_BUCKET_COUNT; bucketIndex++)
	{
		u32 recordAddress = OVR226_RDATA_BUCKET_SETUP_BASE + (u32)(bucketIndex * sizeof(R226.bucketSetups[0]));

		if (recordAddress == setupAddress)
		{
			return &R226.bucketSetups[bucketIndex];
		}
	}

	return NULL;
}

static void Ovr226_800a0ddc_CopyScratchInitTable(void)
{
	u32 *scratch = CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET);
	memcpy(scratch, R226.scratchInitTable,
	       (size_t)OVR226_SCRATCH_INIT_WORD_COUNT * sizeof(u32));
}

static void Ovr226_800a0e44_ApplyBucketSetup(u32 setupAddress)
{
	const struct DrawLevelOvrBucketSetupRecord *setup = DrawLevelOvr1P_FindBucketSetupRecord(setupAddress);

	if (setup == NULL)
	{
		return;
	}

	DrawLevelOvr1P_CopyScratchWords(setup->copy0, &setup->copies[0]);
	DrawLevelOvr1P_CopyScratchWords(setup->copy1, &setup->copies[1]);
}

static u32 DrawLevelOvr1P_Select4x1ProjectedTableWord(const struct QuadBlock *block, const struct DrawLevelOvr1PFaceSelector *selector)
{
	(void)block;

	u32 tableIndex = (DrawLevelOvr1P_GetActiveDrawOrderLow() >> selector->drawOrderShift) & 0x1f;

	return *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET + (int)(tableIndex * sizeof(u32)));
}

#if defined(CTR_NATIVE)
static int DrawLevelOvr1P_TryCached4x1Selector(
    const struct QuadBlock *block,
    const struct DrawLevelOvr1PFaceSelector *selector,
    u32 tableWord, int *indices);
#endif

static u32 DrawLevelOvr1P_Select4x1ProjectedIndices(const struct QuadBlock *block, const struct DrawLevelOvr1PFaceSelector *selector, int *indices)
{
	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedTableWord(block, selector);

	DrawLevelOvr1P_Scratch()->selected4x1TableWord = tableWord;

#if defined(CTR_NATIVE)
    /* Selected tableWord and slot still come from LIVE retail scratch data;
     * caching only the immutable selector decode preserves OT/UV semantics. */
    if (DrawLevelOvr1P_TryCached4x1Selector(block,selector,tableWord,indices))
        return tableWord;
#endif
	for (s32 vertexIndex = 0; vertexIndex < 4; vertexIndex++)
	{
		u32 tableShift = (tableWord >> ((3 - vertexIndex) * 8)) & 0x1f;
		u32 recordOffset = (selector->selector >> tableShift) & 0xff;

		indices[vertexIndex] = recordOffset / (int)sizeof(struct DrawLevelOvr1PScratchVertex);
	}

	return tableWord;
}

static u32 DrawLevelOvr1P_Select4x1ProjectedFace(const struct DrawLevelOvr1PScratchVertex *projected, const struct QuadBlock *block, int faceIndex,
                                                 int *indices)
{
	// NOTE(aalhendi): Retail selector helpers store the active face slot at
	// frame+0xb4 before decoding the 0xec table word.
	DrawLevelOvr1P_SetGridFaceSlot(projected, faceIndex);
	return DrawLevelOvr1P_Select4x1ProjectedIndices(block, &sDrawLevelOvr1P4x1FaceSelectors[faceIndex], indices);
}

static u32 DrawLevelOvr1P_GetGridFaceSlotWord(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return DrawLevelOvr1P_ReadPackedWord((const u8 *)projected + 0xb4);
}

static struct TextureLayout *DrawLevelOvr1P_ResolveTexturePointer(uintptr_t texturePtr)
{
	if (texturePtr == 0)
	{
		return NULL;
	}

	if ((texturePtr & 1) != 0)
	{
		return *(struct TextureLayout **)(texturePtr - 1);
	}

	return (struct TextureLayout *)texturePtr;
}

static struct TextureLayout *DrawLevelOvr1P_ResolveMidTexture(const struct QuadBlock *block, int faceIndex)
{
	return DrawLevelOvr1P_ResolveTexturePointer((uintptr_t)block->ptr_texture_mid[faceIndex]);
}

static int DrawLevelOvr1P_IsPlausibleTextureLayout(const struct TextureLayout *texture)
{
	if (texture == NULL)
	{
		return 0;
	}

	return (texture->tpage & 0xfe00) == 0;
}

#ifdef CTR_NATIVE
#define DRAW_LEVEL_NATIVE_POINTER_CACHE_SIZE 1024u

struct DrawLevelOvr1PNativeSpanCacheEntry
{
	uintptr_t ptr;
	uintptr_t size;
	u32 generation;
	u8 result;
};

struct DrawLevelOvr1PNativeTextureCacheEntry
{
	u32 value;
	u32 generation;
	u8 result;
};

struct DrawLevelOvr1PNativePsxWordCacheEntry
{
	u32 hostWord;
	u32 psxWord;
	u32 generation;
	u8 result;
};

static struct DrawLevelOvr1PNativeSpanCacheEntry sDrawLevelOvr1P_NativeSpanCache[DRAW_LEVEL_NATIVE_POINTER_CACHE_SIZE];
static struct DrawLevelOvr1PNativeTextureCacheEntry sDrawLevelOvr1P_NativeTextureCache[DRAW_LEVEL_NATIVE_POINTER_CACHE_SIZE];
static struct DrawLevelOvr1PNativePsxWordCacheEntry sDrawLevelOvr1P_NativePsxWordCache[DRAW_LEVEL_NATIVE_POINTER_CACHE_SIZE];
static u32 sDrawLevelOvr1P_NativePointerCacheGeneration = 1;
static const struct mesh_info *sDrawLevelOvr1P_NativePointerCacheMesh;
static const void *sDrawLevelOvr1P_NativePointerCacheQuadBlocks;
static s32 sDrawLevelOvr1P_NativePointerCacheLevelID = -1;
static void *sDrawLevelOvr1P_NativePointerCachePackStart[4];
static void *sDrawLevelOvr1P_NativePointerCachePackEnd[4];
static s32 sDrawLevelOvr1P_NativePointerCachePackSize[4];

/* Native x86 level-topology predecode. The source vertex memory is NEVER
 * copied: SCVert/water animations still update the exact same live positions
 * and colors, and GTE/clipping/OT emission are unchanged. The nine index-to-
 * pointer conversions per quad are performed once per loaded mesh instead
 * of once per visible quad every frame. Invalid/out-of-range topology falls
 * back to the retail index path. Disable for A/B with
 * debug/level_compiled_vertices.disable before starting the process. */
#define DRAW_LEVEL_PREDECODE_MAX_QUADS 131072
struct DrawLevelOvr1PCompiledVertices
{
    struct LevVertex *v[9];
    /* Native classic 4x1 selector decode, loaded once per mesh.
     * The exact table word and active draw_order_low are both checked at
     * runtime to avoid stale data if custom maps mutate the overlay data. */
    u32 nativeSelectorWord[4];
    u8 nativeSelectorCorner[4][4];
    u8 nativeSelectorMask;
    /* One-time retail selector decode for each 4x1 face. Live vertex
     * pointers are still authoritative, including custom vertex colors.
     * No queued native face is eligible for LOD QA unless all four bits
     * were validated while generating the immutable GPU mesh. */
    u8 nativeFaceCorner[4][4];
    u8 nativeLodFaceMask;
    u8 valid;
};
static struct DrawLevelOvr1PCompiledVertices *sDrawLevelOvr1P_CompiledVertices;
static struct QuadBlock *sDrawLevelOvr1P_CompiledQuadBase;
static struct LevVertex *sDrawLevelOvr1P_CompiledVertexBase;
static s32 sDrawLevelOvr1P_CompiledQuadCount;

/* Never enabled in a stable build unless the user deliberately creates the
 * QA marker. No change to gameplay, render output, or OT packet structure. */
static int DrawLevelOvr1P_TryCached4x1Selector(
    const struct QuadBlock *block,
    const struct DrawLevelOvr1PFaceSelector *selector,
    u32 tableWord, int *indices)
{
    static int checked,enabled,logged;
    static unsigned long hits;
    if(!checked)
    {
        FILE *f=fopen("debug/level_selector_cache_qa.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
    }
    if(!enabled || block==NULL || selector==NULL || indices==NULL)
        return 0;
    if (!logged)
    {
        Platform_Log("[LEVEL CPU] opt-in classic 4x1 selector cache active (exact retail table-word check)\n");
        logged=1;
    }
    const uintptr_t addr=(uintptr_t)block;
    const uintptr_t start=(uintptr_t)sDrawLevelOvr1P_CompiledQuadBase;
    if (start==0 || addr<start ||
        sDrawLevelOvr1P_CompiledVertices==NULL ||
        sDrawLevelOvr1P_CompiledQuadCount<=0 ||
        DrawLevelOvr1P_GetActiveDrawOrderLow()!=block->draw_order_low)
        return 0;
    const uintptr_t diff=addr-start;
    if ((diff%sizeof(*block))!=0 ||
        diff/sizeof(*block)>=(size_t)sDrawLevelOvr1P_CompiledQuadCount)
        return 0;
    const struct DrawLevelOvr1PCompiledVertices *entry=
        &sDrawLevelOvr1P_CompiledVertices[diff/sizeof(*block)];
    int face=-1;
    for(int f=0;f<4;++f)
    {
        if(selector==&sDrawLevelOvr1P4x1FaceSelectors[f])
        {
            face=f;
            break;
        }
    }
    if(face<0 || !(entry->nativeSelectorMask & (1u<<face)) ||
       entry->nativeSelectorWord[face]!=tableWord)
        return 0;

    for(int c=0;c<4;++c)
        indices[c]=entry->nativeSelectorCorner[face][c];
    ++hits;
    if((hits&65535ul)==0)
        Platform_Log("[LEVEL CPU] classic 4x1 selector cache hits=%lu\n",hits);
    return 1;
}

static void DrawLevelOvr1P_ResetCompiledVertices(void)
{
    free(sDrawLevelOvr1P_CompiledVertices);
    sDrawLevelOvr1P_CompiledVertices = NULL;
    sDrawLevelOvr1P_CompiledQuadBase = NULL;
    sDrawLevelOvr1P_CompiledVertexBase = NULL;
    sDrawLevelOvr1P_CompiledQuadCount = 0;
}

/* Turbocharged-style immutable level exclusion. GPU compilation is still
 * experimental; keep animated water/scenery in the authoritative retail OT.
 * Address bounds are checked before computing a vertex index (custom maps). */
static void DrawLevelOvr1P_NativeGpuMarkAnimatedVertex(
    u8 *bits, const struct mesh_info *mesh, const struct LevVertex *vertex)
{
    if (!bits || !mesh || !vertex || mesh->numVertex <= 0)
        return;
    const uintptr_t base = (uintptr_t)mesh->ptrVertexArray;
    const uintptr_t addr = (uintptr_t)vertex;
    const size_t bytes = (size_t)mesh->numVertex * sizeof(struct LevVertex);
    if (addr < base || addr - base >= bytes)
        return;
    const size_t offset = (size_t)(addr - base);
    if (offset % sizeof(struct LevVertex) == 0)
        bits[offset / sizeof(struct LevVertex)] = 1;
}

static void DrawLevelOvr1P_CompileLevelVertices(const struct mesh_info *mesh)
{
    FILE *disableMarker;
    DrawLevelOvr1P_ResetCompiledVertices();
    if (mesh == NULL || mesh->ptrVertexArray == NULL || mesh->ptrQuadBlockArray == NULL ||
        mesh->numVertex <= 0 || mesh->numQuadBlock <= 0 ||
        mesh->numQuadBlock > DRAW_LEVEL_PREDECODE_MAX_QUADS)
        return;
    disableMarker = fopen("debug/level_compiled_vertices.disable", "rb");
    if (disableMarker != NULL)
    {
        fclose(disableMarker);
        return;
    }
    const size_t n = (size_t)mesh->numQuadBlock;
    struct DrawLevelOvr1PCompiledVertices *out =
        (struct DrawLevelOvr1PCompiledVertices *)calloc(n, sizeof(*out));
    if (out == NULL)
        return;
    for (size_t i = 0; i < n; ++i)
    {
        const struct QuadBlock *block = &mesh->ptrQuadBlockArray[i];
        int valid = 1;
        for (int j = 0; j < 9; ++j)
        {
            u32 vertexIndex = block->index[j];
            if (vertexIndex >= (u32)mesh->numVertex)
            {
                valid = 0;
                break;
            }
            out[i].v[j] = &mesh->ptrVertexArray[vertexIndex];
        }
        out[i].valid = (u8)valid;
        if (!valid) continue;
        /* Predecode retail 4x1 face table once. A custom table that does not
         * map every corner to one of our 9 live projected vertices simply
         * leaves that face on the unchanged retail selector path. */
        for(int face=0;face<4;++face)
        {
            const struct DrawLevelOvr1PFaceSelector *sel=
                &sDrawLevelOvr1P4x1FaceSelectors[face];
            const u32 pattern=(block->draw_order_low >> sel->drawOrderShift)&0x1fu;
            if(pattern>=OVR226_SCRATCH_INIT_WORD_COUNT)continue;
            const u32 word=R226.scratchInitTable[pattern];
            u8 corners[4];
            int legal=1;
            for(int c=0;c<4;++c)
            {
                const u32 shift=(word>>((3-c)*8))&0x1fu;
                const u32 offset=(sel->selector>>shift)&0xffu;
                if(offset % sizeof(struct DrawLevelOvr1PScratchVertex)!=0 ||
                   offset/sizeof(struct DrawLevelOvr1PScratchVertex)>=9u)
                {
                    legal=0;
                    break;
                }
                corners[c]=(u8)(offset/sizeof(struct DrawLevelOvr1PScratchVertex));
            }
            if(!legal)continue;
            out[i].nativeSelectorWord[face]=word;
            for(int c=0;c<4;++c)
                out[i].nativeSelectorCorner[face][c]=corners[c];
            out[i].nativeSelectorMask|=(u8)(1u<<face);
        }
    }
    /* Stage 1: precompile the world-space 3x3 topology of each quadblock into
     * GPU-resident triangles for native renderer validation. Still draw ALL
     * retail geometry; replacement awaits camera/texture/OT visual QA.
     * Texture, water and animated positions are intentionally not captured
     * here because retail live render remains authoritative. */
    if (NativeRenderer_LevelPCEnabled() && n <= 16384)
    {
        /* Retail GT3 topology: first 0-1-2, second 1-3-2. */
        static const u8 faceToTriangle[6] = {0, 1, 2, 1, 3, 2};
        NativeLevelPCVertex *gpuVertices =
            (NativeLevelPCVertex *)calloc(n * 24u, sizeof(*gpuVertices));
        u8 *gpuFaceTypes = (u8 *)malloc(n * 4u);
        if (gpuFaceTypes != NULL) memset(gpuFaceTypes, 255, n * 4u);
        /* Only classify/cache a level whose live asset tables match this mesh.
         * Missing metadata or allocation failures keep all terrain on PS1. */
        const struct Level *nativeGpuLevel =
            (sdata->gGT != NULL) ? sdata->gGT->level1 : NULL;
        const int nativeGpuLevelTrusted =
            nativeGpuLevel && nativeGpuLevel->ptr_mesh_info == mesh &&
            nativeGpuLevel->numWaterVertices >= 0 &&
            nativeGpuLevel->numWaterVertices <= mesh->numVertex * 2 &&
            nativeGpuLevel->numSCVert >= 0 &&
            nativeGpuLevel->numSCVert <= mesh->numVertex * 2 &&
            (nativeGpuLevel->numWaterVertices == 0 || nativeGpuLevel->ptr_water != NULL) &&
            (nativeGpuLevel->numSCVert == 0 || nativeGpuLevel->ptrSCVert != NULL);
        u8 *nativeAnimated = (u8 *)calloc((size_t)mesh->numVertex, 1);
        u8 *nativeWaterBlocks = (u8 *)calloc(n, 1);
        if (gpuVertices != NULL && gpuFaceTypes != NULL &&
            nativeGpuLevelTrusted && nativeAnimated != NULL &&
            nativeWaterBlocks != NULL)
        {
            /* Animations change the source vertex positions/colors after VBO
             * upload. Exclude all quadblocks referencing any such vertex. */
            for (int i = 0; nativeGpuLevel->ptr_water != NULL &&
                 i < nativeGpuLevel->numWaterVertices; ++i)
                DrawLevelOvr1P_NativeGpuMarkAnimatedVertex(
                    nativeAnimated, mesh, nativeGpuLevel->ptr_water[i].v);
            for (int i = 0; nativeGpuLevel->ptrSCVert != NULL &&
                 i < nativeGpuLevel->numSCVert; ++i)
                DrawLevelOvr1P_NativeGpuMarkAnimatedVertex(
                    nativeAnimated, mesh, nativeGpuLevel->ptrSCVert[i].v);

            /* Retail's water BSP leaf type must remain dynamic, including
             * water blocks that do not reference animated source vertices. */
            for (int i = 0; mesh->bspRoot != NULL &&
                 i < mesh->numBspNodes; ++i)
            {
                const struct BSP *leaf = &mesh->bspRoot[i];
                if ((leaf->flag & (BSP_NODE_FLAG_LEAF | BSP_LEAF_FLAG_WATER)) !=
                    (BSP_NODE_FLAG_LEAF | BSP_LEAF_FLAG_WATER) ||
                    leaf->data.leaf.ptrQuadBlockArray == NULL)
                    continue;
                const uintptr_t base = (uintptr_t)mesh->ptrQuadBlockArray;
                const uintptr_t addr = (uintptr_t)leaf->data.leaf.ptrQuadBlockArray;
                if (addr < base || addr - base >= n * sizeof(struct QuadBlock) ||
                    (addr - base) % sizeof(struct QuadBlock) != 0)
                    continue;
                const size_t start = (size_t)(addr - base) / sizeof(struct QuadBlock);
                for (int j = 0; j < leaf->data.leaf.numQuads &&
                     start + (size_t)j < n; ++j)
                    nativeWaterBlocks[start + (size_t)j] = 1;
            }
            int staticRejectedWater=0;
            int staticRejectedAnimated=0;
            int staticRejectedSelector=0;
            int staticRejectedUnresolved=0;
            int staticCandidateBlocks=0;
            int staticRejectedStrictBlocks=0;
            int staticRejectedStrictBlendFaces=0;
            int staticRejectedStrictOrderFaces=0;
            int staticRejectedStrictUnsupportedFaces=0;
            int strictStatic=0;
            /* Opt-in defensive QA: the static cache is NOT allowed to
             * replace retail transparent materials and OT-order-biased faces.
             * Any unsafe face makes the entire quadblock fall back to PS1. */
            FILE *strictFlag=fopen("debug/level_pc_gpu_strict_material_stress.enable","rb");
            if (strictFlag)
            {
                strictStatic=1;
                fclose(strictFlag);
            }
            int previewTextureLod=0;
            int lodAccepted=0;
            int lodFallback=0;
            int selectorExact=0;
            int selectorFallback=0;
            int selectorUvFlipped=0;
            int selectorSingleTriangles=0;
            /* Test-only diagnostic: compare the geometrically coherent 3x3 grid
             * against the 4x1 retail-selector vertex layout, without changing
             * PS1 OT or game physics. Enable before process startup only. */
            int previewGridTopology=0;
            FILE *gridFlag=fopen("debug/level_pc_gpu_grid_topology_qa.enable","rb");
            if(gridFlag)
            {
                previewGridTopology=1;
                fclose(gridFlag);
                Platform_Log("[LEVEL PC GPU] stress QA using grid 3x3 topology, not 4x1 selectors\n");
            }
            int previewLowQuad=0;
            FILE *lowFlag=fopen("debug/level_pc_gpu_far_low_quad_qa.enable","rb");
            if(lowFlag)
            {
                previewLowQuad=1;
                fclose(lowFlag);
                Platform_Log("[LEVEL PC GPU] QA using one full low-detail quad per block\n");
            }
            FILE *lodFile=fopen("debug/level_pc_texture_lod_index.txt","rb");
            if (lodFile!=NULL)
            {
                int readIndex=0;
                if (fscanf(lodFile,"%d",&readIndex)==1 &&
                    readIndex>=0 && readIndex<=3)
                    previewTextureLod=readIndex;
                fclose(lodFile);
            }
            for (size_t qi = 0; qi < n; ++qi)
            {
                if (!out[qi].valid) continue;
                const struct QuadBlock *staticBlock = &mesh->ptrQuadBlockArray[qi];
                if (nativeWaterBlocks[qi])
                {
                    ++staticRejectedWater;
                    continue;
                }
                int moving = 0;
                for (int j = 0; j < 9; ++j)
                    if (nativeAnimated[staticBlock->index[j]]) { moving = 1; break; }
                if (moving)
                {
                    ++staticRejectedAnimated;
                    continue;
                }
                int animatedTexture = (((uintptr_t)staticBlock->ptr_texture_low & 1u) != 0);
                for (int f = 0; f < 4; ++f)
                    animatedTexture |= (((uintptr_t)staticBlock->ptr_texture_mid[f] & 1u) != 0);
                if (animatedTexture)
                {
                    ++staticRejectedSelector;
                    continue;
                }
                ++staticCandidateBlocks;
                int strictBlockUnsafe=0;
                if (strictStatic)
                {
                    /* A textured level is opaque ONLY when both tpage blend
                     * bits are set (Turbocharged's 0x60 retail test). */
                    for (int f=0; f<4; ++f)
                    {
                        const struct TextureLayout *baseTexture =
                            DrawLevelOvr1P_ResolveTexturePointerChecked(
                                (uintptr_t)staticBlock->ptr_texture_mid[f]);
                        if (baseTexture==NULL || ((baseTexture->tpage >> 7) & 3u) >= 3u)
                        {
                            ++staticRejectedStrictUnsupportedFaces;
                            strictBlockUnsafe=1;
                        }
                        else if ((baseTexture->tpage & 0x60u) != 0x60u)
                        {
                            ++staticRejectedStrictBlendFaces;
                            strictBlockUnsafe=1;
                        }
                        /* Per-face signed draw order changes OT slot relative
                         * to camera depth. Static scene layer cannot yet
                         * reproduce that, so leave the entire block retail. */
                        const s8 order=(s8)(staticBlock->draw_order_high >> (f*8));
                        if (order!=0)
                        {
                            ++staticRejectedStrictOrderFaces;
                            strictBlockUnsafe=1;
                        }
                    }
                }
                if (strictBlockUnsafe)
                {
                    ++staticRejectedStrictBlocks;
                    continue;
                }
                if (previewLowQuad)
                {
                    /* Retail FULL_DYNAMIC far path uses the single low-LOD
                     * face {0,1,2,3}, not four 4x1 mid-LOD faces. This
                     * immutable QA VBO is consumed ONLY when retail's own
                     * near-mask and offscreen checks permit the direct path. */
                    const struct TextureLayout *lowTex =
                        DrawLevelOvr1P_ResolveTexturePointerChecked(
                            (uintptr_t)mesh->ptrQuadBlockArray[qi].ptr_texture_low);
                    const int format=lowTex ? ((lowTex->tpage>>7)&3) : 3;
                    if (format < 3)
                    {
                        const u8 U[4]={lowTex->u0,lowTex->u1,lowTex->u2,lowTex->u3};
                        const u8 V[4]={lowTex->v0,lowTex->v1,lowTex->v2,lowTex->v3};
                        gpuFaceTypes[qi*4u]=(u8)format;
                        for (int j=0;j<6;++j)
                        {
                            const int corner=faceToTriangle[j];
                            const struct LevVertex *v=out[qi].v[corner];
                            NativeLevelPCVertex *dst=&gpuVertices[qi*24u+(size_t)j];
                            dst->x=(float)v->pos.x;
                            dst->y=(float)v->pos.y;
                            dst->z=(float)v->pos.z;
                            dst->u=(float)U[corner];
                            dst->v=(float)V[corner];
                            dst->page=(float)(lowTex->tpage & 0x1f);
                            dst->clut=(float)lowTex->clut;
                            dst->r=v->color_hi[0];
                            dst->g=v->color_hi[1];
                            dst->b=v->color_hi[2];
                            dst->a=255;
                        }
                    }
                    continue;
                }
                for (int face = 0; face < 4; ++face)
                {
                    /* Retail uses a selector table derived from the packed
                     * per-face rotation/mode, not fixed grid indices.
                     * Precompute this ONCE per mesh, not every camera frame.
                     * If its table index is outside the 24 known words or
                     * a byte does not map onto a projected record, preserve
                     * the legacy QA grid as a safe fallback.
                     */
                    const struct DrawLevelOvr1PFaceSelector *selector=
                        &sDrawLevelOvr1P4x1FaceSelectors[face];
                    const u32 patternIndex=
                        (mesh->ptrQuadBlockArray[qi].draw_order_low >>
                         selector->drawOrderShift)&0x1fu;
                    int faceVertexIndex[4];
                    int faceSelectorValid=(!previewGridTopology && patternIndex<OVR226_SCRATCH_INIT_WORD_COUNT);
                    int uvFlipped=0;
                    int oneTriangle=0;
                    int reverseWinding=0;
                    if (faceSelectorValid)
                    {
                        const u32 tableWord=R226.scratchInitTable[patternIndex];
                        uvFlipped=((s32)(tableWord<<8))<0;
                        /* CTR retail table bit 7: GT3, not GT4.
                         * Do not draw a fabricated second triangle. */
                        oneTriangle=(tableWord&0x80u)!=0;
                        reverseWinding=(tableWord&0x80000000u)!=0;
                        for (int c=0;c<4;++c)
                        {
                            const u32 shift=(tableWord>>((3-c)*8))&0x1fu;
                            const u32 rawOffset=(selector->selector>>shift)&0xffu;
                            if (rawOffset%sizeof(struct DrawLevelOvr1PScratchVertex)!=0 ||
                                rawOffset/sizeof(struct DrawLevelOvr1PScratchVertex)>=9u)
                            {
                                faceSelectorValid=0;
                                break;
                            }
                            faceVertexIndex[c]=(int)(rawOffset/
                                sizeof(struct DrawLevelOvr1PScratchVertex));
                        }
                    }
                    if(faceSelectorValid)
                    {
                        ++selectorExact;
                        if(uvFlipped)++selectorUvFlipped;
                        if(oneTriangle)++selectorSingleTriangles;
                    }
                    else ++selectorFallback;
                    const struct TextureLayout *tex =
                        DrawLevelOvr1P_ResolveTexturePointerChecked(
                            (uintptr_t)mesh->ptrQuadBlockArray[qi].ptr_texture_mid[face]);
                    if(tex && previewTextureLod)
                    {
                        /* PS1 quadblock mid textures are sequential layouts
                         * for distance-based LOD (+1/+2/+3). Choose an
                         * explicit source for controlled native GPU QA.
                         * This does not alter retail texture choice. */
                        const struct TextureLayout *candidate =
                            DrawLevelOvr1P_ResolveTexturePointerChecked(
                                (uintptr_t)(tex+previewTextureLod));
                        if(candidate)
                        {
                            tex=candidate;
                            ++lodAccepted;
                        }
                        else ++lodFallback;
                    }
                    int format=tex?((tex->tpage>>7)&3):3;
                    /* Static native geometry buckets match Turbocharged:
                     * [0..2] texture format, [0/1] GT4/GT3,
                     * [0..2] cull both/normal/reversed. 255=ineligible.
                     * The retail per-face scratch selector determines
                     * triangle count AND winding. */
                    const int bothSides=
                        (staticBlock->draw_order_low & QUADBLOCK_DRAW_ORDER_LOW_DOUBLE_SIDED)!=0;
                    const int cullClass=bothSides ? 0 : (reverseWinding ? 2 : 1);
                    if(format<3 && faceSelectorValid)
                    {
                        gpuFaceTypes[qi*4u+(size_t)face]=(u8)(
                            format + (oneTriangle ? 3 : 0) + cullClass * 6);
                        for (int c=0;c<4;++c)
                            out[qi].nativeFaceCorner[face][c]=(u8)faceVertexIndex[c];
                        out[qi].nativeLodFaceMask|=(u8)(1u<<face);
                    }
                    for (int j = 0; j < (oneTriangle ? 3 : 6); ++j)
                    {
                        const int corner=faceToTriangle[j];
                        const int gridIndex = faceSelectorValid ?
                            faceVertexIndex[corner] : sDrawLevelOvr1PGridFaceIndices[face][corner];
                        const struct LevVertex *v = out[qi].v[gridIndex];
                        NativeLevelPCVertex *dst = &gpuVertices[qi * 24u + (size_t)face * 6u + (size_t)j];
                        dst->x = (float)v->pos.x;
                        dst->y = (float)v->pos.y;
                        dst->z = (float)v->pos.z;
                        if (tex && format<3)
                        {
                            const u8 U[4]={tex->u0,tex->u1,tex->u2,tex->u3};
                            const u8 V[4]={tex->v0,tex->v1,tex->v2,tex->v3};
                            const int uvCorner=(faceSelectorValid && uvFlipped) ?
                                (corner^1) : corner;
                            dst->u=(float)U[uvCorner];
                            dst->v=(float)V[uvCorner];
                            dst->page=(float)(tex->tpage & 0x1f);
                            dst->clut=(float)tex->clut;
                            dst->dither=0.0f;
                        }
                        dst->r = v->color_hi[0];
                        dst->g = v->color_hi[1];
                        dst->b = v->color_hi[2];
                        dst->a = 255;
                    }
                }
                /* A replacement draw cannot silently omit one of four faces.
                 * Keep that entire block in retail unless all materials
                 * resolved. Low-LOD preview intentionally uses one face. */
                int facesResolved = 1;
                for (int f = 0; f < 4; ++f)
                    if (gpuFaceTypes[qi * 4u + (size_t)f] >= 18u)
                        facesResolved = 0;
                if (!facesResolved)
                {
                    memset(&gpuFaceTypes[qi * 4u], 255, 4u);
                    ++staticRejectedUnresolved;
                }
            }
            Platform_Log("[LEVEL PC GPU] static eligibility: candidates=%d water=%d animated=%d animatedTex=%d unresolved=%d total=%u GT3faces=%d\n",
                staticCandidateBlocks,staticRejectedWater,
                staticRejectedAnimated,staticRejectedSelector,
                staticRejectedUnresolved,(unsigned)n,selectorSingleTriangles);
            if (strictStatic)
                Platform_Log("[LEVEL PC GPU] STRICT material policy: retained=%d rejectedBlocks=%d blendFaces=%d orderFaces=%d unsupportedFaces=%d (far LOD fallback recommended)\n",
                    staticCandidateBlocks-staticRejectedStrictBlocks-staticRejectedUnresolved,
                    staticRejectedStrictBlocks,
                    staticRejectedStrictBlendFaces,
                    staticRejectedStrictOrderFaces,
                    staticRejectedStrictUnsupportedFaces);
            NativeRenderer_LevelPCUpload(mesh->ptrQuadBlockArray, gpuVertices, gpuFaceTypes,
                (int)(n * 24u), (int)n);
            NativeRenderer_LevelPCProjectionQAUpload(
                mesh->ptrQuadBlockArray, mesh->ptrVertexArray, mesh->numVertex);
            Platform_Log("[LEVEL PC GPU] texture LOD experiment=%d validated=%d fallback=%d\n",
                previewTextureLod,lodAccepted,lodFallback);
            Platform_Log("[LEVEL PC GPU] retail selector faces exact=%d fallback=%d flippedUV=%d\n",
                selectorExact,selectorFallback,selectorUvFlipped);
        }
        if (!nativeGpuLevelTrusted)
            Platform_LogWarn("[LEVEL PC GPU] static cache not prepared: missing/mismatched live level metadata\n");
        free(nativeAnimated);
        free(nativeWaterBlocks);
        free(gpuVertices);
        free(gpuFaceTypes);
    }
    sDrawLevelOvr1P_CompiledVertices = out;
    sDrawLevelOvr1P_CompiledQuadBase = mesh->ptrQuadBlockArray;
    sDrawLevelOvr1P_CompiledVertexBase = mesh->ptrVertexArray;
    sDrawLevelOvr1P_CompiledQuadCount = mesh->numQuadBlock;
    Platform_Log("[LEVEL NATIVE] predecoded %d quadblocks (%u bytes) - positions remain live\n",
        mesh->numQuadBlock, (unsigned)(n * sizeof(*out)));
}

/* Stress bypass may queue ONLY native blocks replacing actual PS1 packets.
 * Overlay-only mode retains automatic diagnostic queuing. */
/* The retail far-LOD FULL_DYNAMIC bucket emits one quad from vertices 0..3.
 * Strict diagnostic: replace ONLY actual retail direct GT4 packets; the
 * near-subdivided and triangular cases stay on the PS1 OT path.
 * Marker ignored outside active stress test, never for user gameplay. */
static int DrawLevelOvr1P_FarLowDirectStressEnabled(void)
{
    static int checked, enabled;
    if(!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_far_low_direct_stress.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(enabled)Platform_Log("[LEVEL PC GPU] strict far-LOD GT4 bucket marker active\n");
    }
    return enabled;
}

static int DrawLevelOvr1P_NativeGpuStressExplicitQueues(void)
{
    static int checked,mask;
    if (!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_replace_stress.txt","rb");
        if (f)
        {
            if (fscanf(f,"%d",&mask)!=1 || mask<1 || mask>3) mask=0;
            fclose(f);
        }
        checked=1;
    }
    return ((mask!=0 || DrawLevelOvr1P_FarLowDirectStressEnabled()) && NativePerf_IsStressTestMeasuring());
}

static struct LevVertex *const *DrawLevelOvr1P_GetCompiledBlockVertices(
    const struct LevVertex *sourceVertices, const struct QuadBlock *block)
{
    const uintptr_t start = (uintptr_t)sDrawLevelOvr1P_CompiledQuadBase;
    const uintptr_t address = (uintptr_t)block;
    uintptr_t delta;
    size_t index;
    if (sDrawLevelOvr1P_CompiledVertices == NULL ||
        sourceVertices != sDrawLevelOvr1P_CompiledVertexBase ||
        address < start)
        return NULL;
    delta = address - start;
    if ((delta % sizeof(*block)) != 0)
        return NULL;
    index = delta / sizeof(*block);
    if (index >= (size_t)sDrawLevelOvr1P_CompiledQuadCount ||
        !sDrawLevelOvr1P_CompiledVertices[index].valid)
        return NULL;
    /* A visible quadblock already selected by retail BSP can be queued for
     * the optional native VBO overlay without touching its OT packets. */
    if (NativeRenderer_LevelPCEnabled() &&
            !DrawLevelOvr1P_NativeGpuStressExplicitQueues())
        NativeRenderer_LevelPCQueueBlock(sDrawLevelOvr1P_CompiledQuadBase, (int)index);
    return sDrawLevelOvr1P_CompiledVertices[index].v;
}

static void DrawLevelOvr1P_InvalidateNativePointerCache(void)
{
	sDrawLevelOvr1P_NativePointerCacheGeneration++;
	if (sDrawLevelOvr1P_NativePointerCacheGeneration == 0)
	{
		memset(sDrawLevelOvr1P_NativeSpanCache, 0, sizeof(sDrawLevelOvr1P_NativeSpanCache));
		memset(sDrawLevelOvr1P_NativeTextureCache, 0, sizeof(sDrawLevelOvr1P_NativeTextureCache));
		memset(sDrawLevelOvr1P_NativePsxWordCache, 0, sizeof(sDrawLevelOvr1P_NativePsxWordCache));
		sDrawLevelOvr1P_NativePointerCacheGeneration = 1;
	}
}

static void DrawLevelOvr1P_BeginNativePointerCachePass(const struct mesh_info *mesh)
{
	struct GameTracker *gGT = sdata->gGT;
	const void *quadBlocks = (mesh != NULL) ? mesh->ptrQuadBlockArray : NULL;
	s32 levelID = (gGT != NULL) ? gGT->levelID : -1;
	int changed = (mesh != sDrawLevelOvr1P_NativePointerCacheMesh) ||
	              (quadBlocks != sDrawLevelOvr1P_NativePointerCacheQuadBlocks) ||
	              (levelID != sDrawLevelOvr1P_NativePointerCacheLevelID);

	for (s32 packIndex = 0; packIndex < 4; packIndex++)
	{
		const struct Mempack *pack = &sdata->mempack[packIndex];
		if ((pack->start != sDrawLevelOvr1P_NativePointerCachePackStart[packIndex]) ||
		    (pack->endOfMemory != sDrawLevelOvr1P_NativePointerCachePackEnd[packIndex]) ||
		    (pack->packSize != sDrawLevelOvr1P_NativePointerCachePackSize[packIndex]))
		{
			changed = 1;
		}
	}

	if (!changed)
	{
		return;
	}

	sDrawLevelOvr1P_NativePointerCacheMesh = mesh;
	sDrawLevelOvr1P_NativePointerCacheQuadBlocks = quadBlocks;
	sDrawLevelOvr1P_NativePointerCacheLevelID = levelID;

	for (s32 packIndex = 0; packIndex < 4; packIndex++)
	{
		const struct Mempack *pack = &sdata->mempack[packIndex];
		sDrawLevelOvr1P_NativePointerCachePackStart[packIndex] = pack->start;
		sDrawLevelOvr1P_NativePointerCachePackEnd[packIndex] = pack->endOfMemory;
		sDrawLevelOvr1P_NativePointerCachePackSize[packIndex] = pack->packSize;
	}

	DrawLevelOvr1P_InvalidateNativePointerCache();
	DrawLevelOvr1P_CompileLevelVertices(mesh);
}

static u32 DrawLevelOvr1P_NativePointerCacheIndex(uintptr_t ptr, uintptr_t salt)
{
	u32 key = (u32)ptr;
	key ^= (u32)(ptr >> 12);
	key ^= (u32)salt * 2654435761u;
	return (key ^ (key >> 16)) & (DRAW_LEVEL_NATIVE_POINTER_CACHE_SIZE - 1);
}

// NOTE(aalhendi): Native data-boundary shim. Retail ptrmap leaves PSX address
// words in level data; native level loads store host-rebased pointers instead.
static int DrawLevelOvr1P_MempackContains(const struct Mempack *pack, uintptr_t ptr, uintptr_t *span)
{
	uintptr_t start = (uintptr_t)pack->start;
	if ((start == 0) || (pack->packSize <= 0))
	{
		return 0;
	}

	uintptr_t end = start + (uintptr_t)pack->packSize;

	if ((end < start) || (ptr < start) || (ptr >= end))
	{
		return 0;
	}

	if (span != NULL)
	{
		*span = end - start;
	}

	return 1;
}

static const struct Mempack *DrawLevelOvr1P_FindMempackContaining(uintptr_t ptr)
{
	const struct Mempack *bestPack = NULL;
	uintptr_t bestSpan = UINTPTR_MAX;
	struct GameTracker *gGT = sdata->gGT;

	// PtrMempack can point at inactive mask/podium loads while level1 renders.
	if ((gGT == NULL) || ((gGT->gameMode2 & LEV_SWAP) == 0))
	{
		const struct Mempack *mainPack = &sdata->mempack[0];

		return DrawLevelOvr1P_MempackContains(mainPack, ptr, NULL) ? mainPack : NULL;
	}

	for (s32 packIndex = 0; packIndex < 4; packIndex++)
	{
		const struct Mempack *pack = &sdata->mempack[packIndex];
		uintptr_t span;

		if (DrawLevelOvr1P_MempackContains(pack, ptr, &span))
		{
			if (span < bestSpan)
			{
				bestPack = pack;
				bestSpan = span;
			}
		}
	}

	return bestPack;
}

static int DrawLevelOvr1P_IsNativeLevelSpan(uintptr_t ptr, uintptr_t size)
{
	u32 cacheIndex;
	struct DrawLevelOvr1PNativeSpanCacheEntry *cache;
	const struct Mempack *pack;
	uintptr_t end;
	uintptr_t packStart;
	uintptr_t packEnd;
	int result = 0;

	if (size == 0)
	{
		return 0;
	}

	cacheIndex = DrawLevelOvr1P_NativePointerCacheIndex(ptr, size);
	cache = &sDrawLevelOvr1P_NativeSpanCache[cacheIndex];
	if ((cache->generation == sDrawLevelOvr1P_NativePointerCacheGeneration) &&
	    (cache->ptr == ptr) && (cache->size == size))
	{
		return cache->result != 0;
	}

	pack = DrawLevelOvr1P_FindMempackContaining(ptr);
	end = ptr + size;
	if ((pack != NULL) && (end >= ptr) && (pack->start != NULL) && (pack->packSize > 0))
	{
		packStart = (uintptr_t)pack->start;
		packEnd = packStart + (uintptr_t)pack->packSize;
		result = (packEnd >= packStart) && (ptr >= packStart) && (end <= packEnd);
	}

	cache->ptr = ptr;
	cache->size = size;
	cache->result = result ? 1 : 0;
	cache->generation = sDrawLevelOvr1P_NativePointerCacheGeneration;
	return result;
}

#endif

static int DrawLevelOvr1P_IsNativeLevelTexturePointer(u32 value)
{
#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native classifies host-rebased level texture pointers at
	// the data boundary; renderer control flow still follows retail sign tests.
	const u32 cacheIndex = DrawLevelOvr1P_NativePointerCacheIndex((uintptr_t)value, sizeof(struct TextureLayout));
	struct DrawLevelOvr1PNativeTextureCacheEntry *cache = &sDrawLevelOvr1P_NativeTextureCache[cacheIndex];
	uintptr_t ptr = (uintptr_t)value;
	int result;

	if ((cache->generation == sDrawLevelOvr1P_NativePointerCacheGeneration) && (cache->value == value))
	{
		return cache->result != 0;
	}

	result = DrawLevelOvr1P_IsNativeLevelSpan(ptr, sizeof(struct TextureLayout)) &&
	         DrawLevelOvr1P_IsPlausibleTextureLayout((const struct TextureLayout *)ptr);

	cache->value = value;
	cache->result = result ? 1 : 0;
	cache->generation = sDrawLevelOvr1P_NativePointerCacheGeneration;
	return result;
#else
	(void)value;
	return 0;
#endif
}

static int DrawLevelOvr1P_TreatAsRetailNegativeTextureWord(u32 value)
{
	return (s32)value < 0 || DrawLevelOvr1P_IsNativeLevelTexturePointer(value);
}

static int DrawLevelOvr1P_IsRetailOtActiveSlotWord(u32 slotWord)
{
	return (slotWord & 3) == 0 && slotWord <= 0x3c;
}

static u32 DrawLevelOvr1P_GetProjectedOtSlotWord(const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex)
{
	u32 slotWord = projected != NULL ? DrawLevelOvr1P_GetGridFaceSlotWord(projected) : (u32)(faceIndex * 4);

	if (!DrawLevelOvr1P_IsRetailOtActiveSlotWord(slotWord))
	{
		return (u32)(faceIndex * 4);
	}

	return slotWord;
}

#ifdef CTR_NATIVE
static int DrawLevelOvr1P_TryConvertNativeMempackPointerToPsxWord(u32 hostWord, u32 *psxWord)
{
	const u32 psxRamBase = 0x80000000u;
	const uintptr_t psxRamSize = 0x200000u;
	const u32 cacheIndex = DrawLevelOvr1P_NativePointerCacheIndex((uintptr_t)hostWord, 0x505358u);
	struct DrawLevelOvr1PNativePsxWordCacheEntry *cache = &sDrawLevelOvr1P_NativePsxWordCache[cacheIndex];
	uintptr_t hostPtr;
	const struct Mempack *pack;
	uintptr_t hostEnd;
	uintptr_t hostBase;
	u32 converted = 0;
	int result = 0;

	if ((cache->generation == sDrawLevelOvr1P_NativePointerCacheGeneration) && (cache->hostWord == hostWord))
	{
		if (cache->result && (psxWord != NULL))
		{
			*psxWord = cache->psxWord;
		}
		return cache->result != 0;
	}

	hostPtr = (uintptr_t)hostWord;
	pack = DrawLevelOvr1P_FindMempackContaining(hostPtr);
	if ((pack != NULL) && (pack->endOfMemory != NULL))
	{
		hostEnd = (uintptr_t)pack->endOfMemory;
		if (hostEnd >= psxRamSize)
		{
			hostBase = hostEnd - psxRamSize;
			if ((hostPtr >= hostBase) && (hostPtr < hostEnd))
			{
				converted = psxRamBase + (u32)(hostPtr - hostBase);
				result = 1;
			}
		}
	}

	cache->hostWord = hostWord;
	cache->psxWord = converted;
	cache->result = result ? 1 : 0;
	cache->generation = sDrawLevelOvr1P_NativePointerCacheGeneration;

	if (result && (psxWord != NULL))
	{
		*psxWord = converted;
	}
	return result;
}
#endif

static struct TextureLayout *DrawLevelOvr1P_ResolveTexturePointerChecked(uintptr_t texturePtr)
{
#ifdef CTR_NATIVE
	struct TextureLayout *texture;

	if (texturePtr == 0)
	{
		return NULL;
	}

	if ((texturePtr & 1) != 0)
	{
		uintptr_t activePtrSlot = texturePtr - 1;

		if (!DrawLevelOvr1P_IsNativeLevelSpan(activePtrSlot, sizeof(texture)))
		{
			return NULL;
		}

		texture = *(struct TextureLayout **)activePtrSlot;
	}
	else
	{
		texture = (struct TextureLayout *)texturePtr;
	}

	if (!DrawLevelOvr1P_IsNativeLevelSpan((uintptr_t)texture, sizeof(*texture)))
	{
		return NULL;
	}

	return DrawLevelOvr1P_IsPlausibleTextureLayout(texture) ? texture : NULL;
#else
	return DrawLevelOvr1P_ResolveTexturePointer(texturePtr);
#endif
}

static s8 DrawLevelOvr1P_ReadRetailQuadBlockByte(const struct QuadBlock *block, u32 byteOffset)
{
#ifdef CTR_NATIVE
	if (byteOffset >= 0x1c && byteOffset < 0x2c)
	{
		u32 pointerWordOffset = byteOffset & ~3u;
		u32 hostWord = DrawLevelOvr1P_ReadPackedWord((const u8 *)block + pointerWordOffset);
		u32 psxWord;

		// NOTE(aalhendi): Retail reads raw post-ptrmap PSX pointer bytes here;
		// native level pointer words are host-rebased, so reconstruct that byte.
		if (DrawLevelOvr1P_TryConvertNativeMempackPointerToPsxWord(hostWord, &psxWord))
		{
			return (s8)((psxWord >> ((byteOffset & 3u) * 8)) & 0xff);
		}
	}
#endif

	return *(const s8 *)((const u8 *)block + byteOffset);
}

static struct TextureLayout *DrawLevelOvr1P_ResolveProjectedMidTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected)
{
	if (projected == NULL)
	{
		return NULL;
	}

	u32 slotWord = DrawLevelOvr1P_GetGridFaceSlotWord(projected);
	if (!DrawLevelOvr1P_IsRetailOtActiveSlotWord(slotWord))
	{
		return NULL;
	}

	// NOTE(aalhendi): Retail selector bodies load raw `quad+0x1c+slot`.
	// Native validates the host-rebased word before following it.
	return DrawLevelOvr1P_ResolveTexturePointerChecked((uintptr_t)*(void *const *)((const u8 *)block + 0x1c + slotWord));
}

static struct TextureLayout *DrawLevelOvr1P_GetProjectedMidTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                   int faceIndex, u32 maxDepth)
{
	struct TextureLayout *texture = DrawLevelOvr1P_ResolveProjectedMidTexture(block, projected);

	// NOTE(aalhendi): Retail 0x800a8504/0x800aa124 uses the active slot, not
	// the caller face index. Fall back only when the active slot itself is not
	// a retail slot, matching the caller's face-index fallback contract.
	if (texture == NULL && (projected == NULL || !DrawLevelOvr1P_IsRetailOtActiveSlotWord(DrawLevelOvr1P_GetGridFaceSlotWord(projected))))
	{
		texture = DrawLevelOvr1P_ResolveMidTexture(block, faceIndex);
	}

	if (texture == NULL)
	{
		return NULL;
	}

	u32 mosaicWord = DrawLevelOvr1P_ReadPackedWord((const u8 *)texture + 0x24);

	// NOTE(aalhendi): Retail stores texture+0x24 at scratch 0x84 for the
	// deepest-frame UV reload path.
	DrawLevelOvr1P_Scratch()->mosaicTextureWord = mosaicWord;

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold0)
	{
		texture++;
	}

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold1)
	{
		texture++;
	}

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->topLevelNearDepthThreshold)
	{
		// NOTE(aalhendi): Retail tests this word as signed PS1 data. Pointer-map
		// rebasing turns valid 0x80xxxxxx hi-texture pointers into positive native
		// addresses, so keep those on the retail negative path.
		if (!DrawLevelOvr1P_TreatAsRetailNegativeTextureWord(mosaicWord))
		{
			texture++;
		}
	}

	return texture;
}

static u16 DrawLevelOvr1P_PackUv(u8 u, u8 v)
{
	return (u16)u | ((u16)v << 8);
}

static void DrawLevelOvr1P_WriteProjectedUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                            u32 tableWord)
{
	u16 uv[4];

	uv[0] = DrawLevelOvr1P_PackUv(texture->u0, texture->v0);
	uv[1] = DrawLevelOvr1P_PackUv(texture->u1, texture->v1);
	uv[2] = DrawLevelOvr1P_PackUv(texture->u2, texture->v2);
	uv[3] = DrawLevelOvr1P_PackUv(texture->u3, texture->v3);

	DrawLevelOvr1P_Scratch()->uv.uv0 = uv[0] | ((u32)texture->clut << 16);
	DrawLevelOvr1P_Scratch()->uv.uv1 = uv[1] | ((u32)texture->tpage << 16);
	DrawLevelOvr1P_Scratch()->uv.savedUv0 = uv[0] | ((u32)texture->clut << 16);
	DrawLevelOvr1P_Scratch()->uv.savedUv1 = uv[1] | ((u32)texture->tpage << 16);

	// NOTE(aalhendi): Retail 4x1 handlers store selected UV halfwords at scratch-record offset 0x6 before primitive emission.
	if ((s32)(tableWord << 8) < 0)
	{
		projected[indices[0]].flags = uv[1];
		projected[indices[1]].flags = uv[0];
		projected[indices[2]].flags = uv[3];

		if ((s32)(tableWord << 24) >= 0)
		{
			projected[indices[3]].flags = uv[2];
		}
	}
	else
	{
		projected[indices[0]].flags = uv[0];
		projected[indices[1]].flags = uv[1];
		projected[indices[2]].flags = uv[2];

		if ((s32)(tableWord << 24) >= 0)
		{
			projected[indices[3]].flags = uv[3];
		}
	}
}

static void DrawLevelOvr1P_StoreProjectedDirectUvScratch(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	// NOTE(aalhendi): Retail terminal direct helpers update UV scratch before packet emission.
	DrawLevelOvr1P_Scratch()->uv.flag0 = (s16)projected[indices[0]].flags;
	DrawLevelOvr1P_Scratch()->uv.flag1 = (s16)projected[indices[1]].flags;
	DrawLevelOvr1P_Scratch()->uv.flag2 = (s16)projected[indices[2]].flags;

	if (count == 4)
	{
		DrawLevelOvr1P_Scratch()->uv.flag3 = (s16)projected[indices[3]].flags;
	}
}

static void DrawLevelOvr1P_RestoreProjectedUvScratch(void)
{
	DrawLevelOvr1P_Scratch()->uv.uv0 = DrawLevelOvr1P_Scratch()->uv.savedUv0;
	DrawLevelOvr1P_Scratch()->uv.uv1 = DrawLevelOvr1P_Scratch()->uv.savedUv1;
}

static int DrawLevelOvr1P_GetDeepestMosaicReloadGate(u32 directHandlerAddress, u32 previousHandlerAddress, u32 *reloadSpan)
{
	u32 expectedHandlerAddress;

	switch (directHandlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_HELPER_DEFAULT;
		*reloadSpan = 0x30;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_HELPER_DEFAULT;
		*reloadSpan = 0x30;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_DEFAULT;
		*reloadSpan = 0x60;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_DEFAULT;
		*reloadSpan = 0x60;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_HELPER_DEFAULT;
		*reloadSpan = 0xc0;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_228:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_DEFAULT_228;
		*reloadSpan = 0xc0;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_229:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_DEFAULT_229;
		*reloadSpan = 0xc0;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_HELPER_DEFAULT;
		*reloadSpan = 0xc0;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_DEFAULT;
		*reloadSpan = 0xc0;
		break;

	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT4:
		expectedHandlerAddress = DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_DEFAULT;
		*reloadSpan = 0xc0;
		break;

	default:
		return 0;
	}

	if (previousHandlerAddress != expectedHandlerAddress)
	{
		return 0;
	}

	if (sDrawLevelOvr1P_MosaicReloadSpanOverride != 0)
	{
		*reloadSpan = sDrawLevelOvr1P_MosaicReloadSpanOverride;
	}

	return 1;
}

static void DrawLevelOvr1P_SetMosaicReloadSpanOverride(u32 reloadSpan)
{
	sDrawLevelOvr1P_MosaicReloadSpanOverride = reloadSpan;
}

static void DrawLevelOvr1P_PrepareDeepestMosaicUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 directHandlerAddress)
{
	u32 reloadSpan;

	if (projected != CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET))
	{
		return;
	}

	u32 mosaicBase = DrawLevelOvr1P_Scratch()->mosaicTextureWord;
	int mosaicIsNative = 0;
#ifdef CTR_NATIVE
	// NOTE(aalhendi): Retail uses scratch 0x84 directly. Native can inherit
	// host-rebased pointer words when a wide slot preserves texture state, so
	// only dereference values that are valid level texture data.
	mosaicIsNative = DrawLevelOvr1P_IsNativeLevelTexturePointer(mosaicBase);
	if (mosaicBase == 0 || ((s32)mosaicBase < 0 && !mosaicIsNative))
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}
#endif
	// NOTE(aalhendi): Retail 0x800a8714/0x800aa334 restores saved UV scratch
	// for positive inline sentinels, but rebased native hi-texture pointers must
	// still follow PS1's negative-pointer reload path.
	if ((s32)mosaicBase > 0 && !mosaicIsNative)
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	if (!DrawLevelOvr1P_GetDeepestMosaicReloadGate(directHandlerAddress, DrawLevelOvr1P_Scratch()->previousDirectHandlerAddress, &reloadSpan))
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	u32 sourceOffset = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_INDEX_OFFSET) << 1;
	if ((s32)(DrawLevelOvr1P_Scratch()->selected4x1TableWord << 8) < 0)
	{
		sourceOffset += reloadSpan;
	}
	sourceOffset += *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_BIAS_OFFSET);

	const u8 *source = (const u8 *)(uintptr_t)(mosaicBase + sourceOffset);
	u32 uv0 = DrawLevelOvr1P_ReadPackedWord(source + 0);
	u32 uv1 = DrawLevelOvr1P_ReadPackedWord(source + 4);

	// NOTE(aalhendi): Retail deepest fallthrough rewrites selected scratch-record
	// UV halfwords before jumping through the direct table.
	struct DrawLevelOvr1PScratchVertex *mutableProjected = (struct DrawLevelOvr1PScratchVertex *)projected;
	DrawLevelOvr1P_Scratch()->uv.uv0 = uv0;
	DrawLevelOvr1P_Scratch()->uv.uv1 = uv1;
	mutableProjected[indices[0]].flags = (u16)uv0;
	mutableProjected[indices[1]].flags = (u16)uv1;
	mutableProjected[indices[2]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 8);
	mutableProjected[indices[3]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 10);
}

static void DrawLevelOvr1P_SetGridFaceSlot(const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex)
{
	// NOTE(aalhendi): Retail 3x3 helper frames use frame+0xb4 for face slot*4.
	DrawLevelOvr1P_WritePackedWord((u8 *)projected + 0xb4, (u32)(faceIndex * 4));
}

static void DrawLevelOvr1P_SetGridFaceSlotWord(const struct DrawLevelOvr1PScratchVertex *projected, u32 slotWord)
{
	DrawLevelOvr1P_WritePackedWord((u8 *)projected + 0xb4, slotWord);
}

static u32 DrawLevelOvr1P_GetDefaultGridFaceSlotWord(u32 handlerAddress, int faceIndex)
{
	static const u32 sNormalSlotWords[] = {0x0, 0x4, 0x8, 0xc};
	static const u32 sWide2SlotWords[] = {0x0, 0xc, 0x18, 0x24};
	static const u32 sWide4SlotWords[] = {0x0, 0xc, 0x30, 0x3c};

	// NOTE(aalhendi): Retail default composite helpers stride frame+0xb4 by the
	// copied helper family's deepest UV reload span, not always by faceIndex*4.
	// The wide4 helpers intentionally use QuadBlock-relative slots 0/0xc/0x30/0x3c.
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_DEFAULT:
		return sWide2SlotWords[faceIndex];

	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_DEFAULT:
		return sWide4SlotWords[faceIndex];

	default:
		return sNormalSlotWords[faceIndex];
	}
}

static struct DrawLevelOvr1PScratchVertex *DrawLevelOvr1P_GetScratchVertices(void)
{
	// NOTE(aalhendi): Retail 4x1 handlers build 20-byte projected vertex records at scratch 0x1f8001b4.
	return CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET);
}

static s32 DrawLevelOvr1P_GetDepthClipThreshold(void)
{
	return DrawLevelOvr1P_Scratch()->depthClipThreshold;
}

static u8 *DrawLevelOvr1P_GetClipRecordCursor(void)
{
	return (u8 *)(uintptr_t)DrawLevelOvr1P_Scratch()->clipCursorPtr32;
}

static void DrawLevelOvr1P_SetClipRecordCursor(u8 *cursor)
{
	DrawLevelOvr1P_Scratch()->clipCursorPtr32 = (u32)(uintptr_t)cursor;
}

static u8 *DrawLevelOvr1P_GetClipRecordStart(void)
{
	if (sDrawLevelOvr1P_ClipRecordStart != NULL)
	{
		return sDrawLevelOvr1P_ClipRecordStart;
	}

	return data.PtrClipBuffer[0];
}

static void DrawLevelOvr1P_SetClipRecordStart(u8 *start)
{
	sDrawLevelOvr1P_ClipRecordStart = start;
}

static u8 *DrawLevelOvr1P_GetClipRecordEnd(void)
{
	u8 *start = DrawLevelOvr1P_GetClipRecordStart();
	struct GameTracker *gGT = sdata->gGT;

	if (start == NULL || gGT == NULL)
	{
		return start;
	}

	return start + ((size_t)MainDB_GetClipSize(gGT->levelID, gGT->numPlyrCurrGame) << 2);
}

static size_t DrawLevelOvr1P_GetClipRecordSize(int count)
{
	return count == 4 ? DRAW_LEVEL_OVR1P_CLIP_RECORD_GT4_SIZE : DRAW_LEVEL_OVR1P_CLIP_RECORD_GT3_SIZE;
}

static int DrawLevelOvr1P_HasClipRecordSpace(size_t size)
{
	u8 *start = DrawLevelOvr1P_GetClipRecordStart();
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	u8 *end = DrawLevelOvr1P_GetClipRecordEnd();
	int ok = start != NULL && cursor != NULL && end != NULL && cursor >= start && cursor <= end && size <= (size_t)(end - cursor);

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (!ok && NativePerf_IsStressTestMeasuring())
	{
		static u32 s_lastClipCapacityLogFrame = 0xffffffffu;
		u32 frame = (u32)sdata->frameCounter;
		if (frame != s_lastClipCapacityLogFrame)
		{
			size_t remaining = (cursor != NULL && end != NULL && cursor <= end) ? (size_t)(end - cursor) : 0;
			s_lastClipCapacityLogFrame = frame;
			Platform_Log("[RENDER CAP] clip full frame=%u remaining=%u need=%u\n",
				frame, (unsigned)remaining, (unsigned)size);
		}
	}
#endif

	return ok;
}

static u8 DrawLevelOvr1P_MipsSubuSignBit(u32 lhs, u32 rhs)
{
	return (u8)((lhs - rhs) >> 31);
}

static void DrawLevelOvr1P_CopyColorWord(u8 *dst, const u8 *src)
{
	DrawLevelOvr1P_WritePackedWord(dst, DrawLevelOvr1P_ReadPackedWord(src));
}

static void DrawLevelOvr1P_CopySourcePosFlags(struct DrawLevelOvr1PScratchVertex *projected, const struct LevVertex *vertex)
{
	projected->posVec = vertex->pos;
	projected->flags = vertex->flags;
}

static void DrawLevelOvr1P_CopySourceVertex(struct DrawLevelOvr1PScratchVertex *projected, const struct LevVertex *vertex)
{
	DrawLevelOvr1P_CopySourcePosFlags(projected, vertex);
	DrawLevelOvr1P_CopyColorWord(projected->color_hi, vertex->color_hi);
}

static void DrawLevelOvr1P_CopyProjectedScreenDepth(struct DrawLevelOvr1PScratchVertex *dst, const struct DrawLevelOvr1PScratchVertex *src)
{
	dst->posScreenVec = src->posScreenVec;
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(dst->posScreen,src->posScreen,(u16)src->posScreen[0]|((u32)(u16)src->posScreen[1]<<16));
#endif
	dst->depth = src->depth;
	dst->clipNear = src->clipNear;
	dst->clipHalfNear = src->clipHalfNear;
}

static void DrawLevelOvr1P_SetProjectedDepth(struct DrawLevelOvr1PScratchVertex *projected, u32 depth, int writeClipBytes)
{
	u32 threshold = (u32)DrawLevelOvr1P_GetDepthClipThreshold();

	projected->depth = (u16)depth;

	if (writeClipBytes)
	{
		// NOTE(aalhendi): Retail stores the `subu` sign-bit result as byte 0/1.
		projected->clipNear = DrawLevelOvr1P_MipsSubuSignBit(depth, threshold);
		projected->clipHalfNear = DrawLevelOvr1P_MipsSubuSignBit(depth << 1, threshold);
	}
	else
	{
		projected->clipNear = 0;
		projected->clipHalfNear = 0;
	}
}

static void DrawLevelOvr1P_StoreProjectedDepthWord(struct DrawLevelOvr1PScratchVertex *projected, u32 depth);

static void Ovr226_800a0f78_ProjectVertexTripleFullDepth(struct LevVertex *vertices, const struct QuadBlock *block,
                                                         struct DrawLevelOvr1PScratchVertex *projected, int index0, int index1, int index2)
{
	struct LevVertex *vertex0 = &vertices[block->index[index0]];
	struct LevVertex *vertex1 = &vertices[block->index[index1]];
	struct LevVertex *vertex2 = &vertices[block->index[index2]];
	u32 depth0;
	u32 depth1;
	u32 depth2;

	DrawLevelOvr1P_CopySourceVertex(&projected[index0], vertex0);
	DrawLevelOvr1P_CopySourceVertex(&projected[index1], vertex1);
	DrawLevelOvr1P_CopySourceVertex(&projected[index2], vertex2);

#if defined(CTR_NATIVE)
	{
		const int cacheHit =
			DrawLevelOvr1P_ProjectionCacheHas(vertex0) &&
			DrawLevelOvr1P_ProjectionCacheHas(vertex1) &&
			DrawLevelOvr1P_ProjectionCacheHas(vertex2);

		DrawLevelOvr1P_ProjectionCacheRecordQuery(3, cacheHit ? 3 : 0);
		if (cacheHit)
		{
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex0, &projected[index0], &depth0);
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex1, &projected[index1], &depth1);
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex2, &projected[index2], &depth2);
			DrawLevelOvr1P_StoreProjectedDepthWord(
				&projected[index0], depth0);
			DrawLevelOvr1P_StoreProjectedDepthWord(
				&projected[index1], depth1);
			DrawLevelOvr1P_StoreProjectedDepthWord(
				&projected[index2], depth2);
			return;
		}
	}
#endif

	CTR_GteLoadSVec3V3(&vertex0->pos, &vertex1->pos, &vertex2->pos);
	gte_rtpt();
	CTR_GteStoreSXY3(&projected[index0].posScreen[0], &projected[index1].posScreen[0], &projected[index2].posScreen[0]);
	gte_stsz3(&depth0, &depth1, &depth2);

	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index0], depth0);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index1], depth1);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index2], depth2);
#if defined(CTR_NATIVE)
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex0,
        projected[index0].posScreen[0],projected[index0].posScreen[1],depth0);
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex1,
        projected[index1].posScreen[0],projected[index1].posScreen[1],depth1);
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex2,
        projected[index2].posScreen[0],projected[index2].posScreen[1],depth2);
	DrawLevelOvr1P_ProjectionCacheStore(vertex0, &projected[index0], depth0);
	DrawLevelOvr1P_ProjectionCacheStore(vertex1, &projected[index1], depth1);
	DrawLevelOvr1P_ProjectionCacheStore(vertex2, &projected[index2], depth2);
#endif
}

static void Ovr226_800a1024_ProjectFullDynamicLowFourth(struct LevVertex *vertices, const struct QuadBlock *block,
                                                        struct DrawLevelOvr1PScratchVertex *projected)
{
	struct LevVertex *vertex = &vertices[block->index[3]];
	u32 depth;

	DrawLevelOvr1P_CopySourceVertex(&projected[3], vertex);

#if defined(CTR_NATIVE)
	if (DrawLevelOvr1P_ProjectionCacheHas(vertex))
	{
		DrawLevelOvr1P_ProjectionCacheRecordQuery(1, 1);
		DrawLevelOvr1P_ProjectionCacheLoad(
			vertex, &projected[3], &depth);
		DrawLevelOvr1P_StoreProjectedDepthWord(&projected[3], depth);
		return;
	}
	DrawLevelOvr1P_ProjectionCacheRecordQuery(1, 0);
#endif

	CTR_GteLoadSVec3V0(&vertex->pos);
	gte_rtps();
	CTR_GteStoreSXY(&projected[3].posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[3], depth);
#if defined(CTR_NATIVE)
	DrawLevelOvr1P_ProjectionCacheStore(vertex, &projected[3], depth);
#endif
}

static void Ovr226_800a0f78_ProjectFullDynamicLowQuad(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected)
{
	DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
	Ovr226_800a0f78_ProjectVertexTripleFullDepth(vertices, block, projected, 0, 1, 2);
	Ovr226_800a1024_ProjectFullDynamicLowFourth(vertices, block, projected);
	DrawLevelOvr1P_SetActiveDrawOrderLow(block);
}

static void Ovr226_800a0d20_SeedEntryScratchPointers(struct DrawLevelOvr1PRenderList *renderList, struct PushBuffer *pb)
{
	DrawLevelOvr1P_Scratch()->clipCursorPtr32 = (u32)(uintptr_t)data.PtrClipBuffer[0];
	DrawLevelOvr1P_Scratch()->pushBufferPtr32[0] = (u32)(uintptr_t)pb;
	DrawLevelOvr1P_Scratch()->renderListPtr32 = (u32)(uintptr_t)renderList;
}

static void Ovr226_800a0dc4_ClearProjectedScratch(void)
{
	u32 *projectedScratch = CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET);

	// NOTE(aalhendi): Retail 0x800a0dc4 clears through scratch word 0x3d8.
	for (int offset = 0; offset < 0x228; offset += (int)sizeof(u32))
	{
		projectedScratch[offset / (int)sizeof(u32)] = 0;
	}
}

static void DrawLevelOvr1P_CopyProjectedSource(struct LevVertex *vertex, struct DrawLevelOvr1PScratchVertex *projected,
                                               enum DrawLevelOvr1PProjectedSource source, int copyColorHi)
{
	switch (source)
	{
	case DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS:
		DrawLevelOvr1P_CopySourcePosFlags(projected, vertex);
		break;
	case DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_WATER_COLOR_LO_FLAGS:
		projected->posVec = vertex->pos;
		projected->flags = (u16)vertex->color_lo[0] | ((u16)vertex->color_lo[1] << 8);
		break;
	}

	if (copyColorHi)
	{
		DrawLevelOvr1P_CopyColorWord(projected->color_hi, vertex->color_hi);
	}
}

static int DrawLevelOvr1P_ProjectListVertexTriple(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected,
                                                  int index0, int index1, int index2, enum DrawLevelOvr1PProjectedSource source, struct LevVertex *const *compiled)
{
#ifdef CTR_NATIVE
	struct LevVertex *vertex0 = compiled ? compiled[index0] : &vertices[block->index[index0]];
	struct LevVertex *vertex1 = compiled ? compiled[index1] : &vertices[block->index[index1]];
	struct LevVertex *vertex2 = compiled ? compiled[index2] : &vertices[block->index[index2]];
#else
	(void)compiled;
	struct LevVertex *vertex0 = &vertices[block->index[index0]];
	struct LevVertex *vertex1 = &vertices[block->index[index1]];
	struct LevVertex *vertex2 = &vertices[block->index[index2]];
#endif
	u32 depth0;
	u32 depth1;
	u32 depth2;
	s32 gteFlag;

	CTR_GteLoadSVec3V3(&vertex0->pos, &vertex1->pos, &vertex2->pos);
	gte_rtpt();

	DrawLevelOvr1P_CopyColorWord(projected[index0].color_hi, vertex0->color_hi);
	DrawLevelOvr1P_CopyColorWord(projected[index1].color_hi, vertex1->color_hi);
	DrawLevelOvr1P_CopyColorWord(projected[index2].color_hi, vertex2->color_hi);
	CTR_GteStoreSXY0(&projected[index0].posScreen[0]);
	gte_stflg(&gteFlag);
	depth0 = MFC2(17);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index0], depth0);
	if (((u32)gteFlag & DRAW_LEVEL_OVR1P_GTE_RTPT_OVERFLOW) != 0)
	{
		return 1;
	}

	CTR_GteStoreSXY1(&projected[index1].posScreen[0]);
	depth1 = MFC2(18);
	CTR_GteStoreSXY2(&projected[index2].posScreen[0]);
	depth2 = MFC2(19);
	DrawLevelOvr1P_CopyProjectedSource(vertex0, &projected[index0], source, 0);
	DrawLevelOvr1P_CopyProjectedSource(vertex1, &projected[index1], source, 0);
	DrawLevelOvr1P_CopyProjectedSource(vertex2, &projected[index2], source, 0);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index1], depth1);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[index2], depth2);

	return 0;
}

static void DrawLevelOvr1P_SetGridSlotMode(const struct DrawLevelOvr1PScratchVertex *projected, enum DrawLevelOvr1PGridSlotMode slotMode)
{
	switch (slotMode)
	{
	case DRAW_LEVEL_OVR1P_GRID_SLOT_NONE:
		break;
	case DRAW_LEVEL_OVR1P_GRID_SLOT_FACE:
		DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
		break;
	case DRAW_LEVEL_OVR1P_GRID_SLOT_WORD:
		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0);
		break;
	}
}

static int DrawLevelOvr1P_ProjectListGrid(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected,
                                          enum DrawLevelOvr1PProjectedSource source, enum DrawLevelOvr1PGridSlotMode slotMode)
{
	struct LevVertex *const *compiled = NULL;
#ifdef CTR_NATIVE
	compiled = DrawLevelOvr1P_GetCompiledBlockVertices(vertices, block);
#endif
	DrawLevelOvr1P_SetGridSlotMode(projected, slotMode);

	if (DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 0, 1, 2, source, compiled))
	{
		return 1;
	}

	if (DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 3, 4, 5, source, compiled))
	{
		return 1;
	}

	DrawLevelOvr1P_SetActiveDrawOrderLow(block);
	int result = DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 6, 7, 8, source, compiled);
	return result;
}

static void DrawLevelOvr1P_ProjectRenderedVertexTriple(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected,
                                                       int index0, int index1, int index2, enum DrawLevelOvr1PProjectedSource source, struct LevVertex *const *compiled)
{
#ifdef CTR_NATIVE
	struct LevVertex *vertex0 = compiled ? compiled[index0] : &vertices[block->index[index0]];
	struct LevVertex *vertex1 = compiled ? compiled[index1] : &vertices[block->index[index1]];
	struct LevVertex *vertex2 = compiled ? compiled[index2] : &vertices[block->index[index2]];
#else
	(void)compiled;
	struct LevVertex *vertex0 = &vertices[block->index[index0]];
	struct LevVertex *vertex1 = &vertices[block->index[index1]];
	struct LevVertex *vertex2 = &vertices[block->index[index2]];
#endif
	u32 depth0;
	u32 depth1;
	u32 depth2;

	DrawLevelOvr1P_CopyProjectedSource(vertex0, &projected[index0], source, 1);
	DrawLevelOvr1P_CopyProjectedSource(vertex1, &projected[index1], source, 1);
	DrawLevelOvr1P_CopyProjectedSource(vertex2, &projected[index2], source, 1);

#if defined(CTR_NATIVE)
	{
		const int cacheHit =
			DrawLevelOvr1P_ProjectionCacheHas(vertex0) &&
			DrawLevelOvr1P_ProjectionCacheHas(vertex1) &&
			DrawLevelOvr1P_ProjectionCacheHas(vertex2);

		DrawLevelOvr1P_ProjectionCacheRecordQuery(3, cacheHit ? 3 : 0);
		if (cacheHit)
		{
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex0, &projected[index0], &depth0);
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex1, &projected[index1], &depth1);
			DrawLevelOvr1P_ProjectionCacheLoad(
				vertex2, &projected[index2], &depth2);
			DrawLevelOvr1P_SetProjectedDepth(
				&projected[index0],
				depth0,
				DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
			DrawLevelOvr1P_SetProjectedDepth(
				&projected[index1],
				depth1,
				DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
			DrawLevelOvr1P_SetProjectedDepth(
				&projected[index2],
				depth2,
				DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
			return;
		}
	}
#endif

	CTR_GteLoadSVec3V3(&vertex0->pos, &vertex1->pos, &vertex2->pos);
	gte_rtpt();
	CTR_GteStoreSXY3(&projected[index0].posScreen[0], &projected[index1].posScreen[0], &projected[index2].posScreen[0]);
	gte_stsz3(&depth0, &depth1, &depth2);

	DrawLevelOvr1P_SetProjectedDepth(&projected[index0], depth0, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
	DrawLevelOvr1P_SetProjectedDepth(&projected[index1], depth1, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
	DrawLevelOvr1P_SetProjectedDepth(&projected[index2], depth2, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
#if defined(CTR_NATIVE)
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex0,
        projected[index0].posScreen[0],projected[index0].posScreen[1],depth0);
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex1,
        projected[index1].posScreen[0],projected[index1].posScreen[1],depth1);
    NativeRenderer_LevelPCProjectionQARecord(sDrawLevelOvr1P_CompiledQuadBase,
        sDrawLevelOvr1P_CompiledVertexBase,vertex2,
        projected[index2].posScreen[0],projected[index2].posScreen[1],depth2);
	DrawLevelOvr1P_ProjectionCacheStore(vertex0, &projected[index0], depth0);
	DrawLevelOvr1P_ProjectionCacheStore(vertex1, &projected[index1], depth1);
	DrawLevelOvr1P_ProjectionCacheStore(vertex2, &projected[index2], depth2);
#endif
}

static void DrawLevelOvr1P_ProjectRenderedGrid(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected,
                                               enum DrawLevelOvr1PProjectedSource source, enum DrawLevelOvr1PGridSlotMode slotMode)
{
	struct LevVertex *const *compiled = NULL;
#ifdef CTR_NATIVE
	compiled = DrawLevelOvr1P_GetCompiledBlockVertices(vertices, block);
#endif
	DrawLevelOvr1P_SetGridSlotMode(projected, slotMode);
	DrawLevelOvr1P_ProjectRenderedVertexTriple(vertices, block, projected, 0, 1, 2, source, compiled);
	DrawLevelOvr1P_ProjectRenderedVertexTriple(vertices, block, projected, 3, 4, 5, source, compiled);
	DrawLevelOvr1P_SetActiveDrawOrderLow(block);
	DrawLevelOvr1P_ProjectRenderedVertexTriple(vertices, block, projected, 6, 7, 8, source, compiled);
}

static u32 DrawLevelOvr1P_GetProjectedColorWord(const struct DrawLevelOvr1PScratchVertex *projected);

static u32 Ovr226_800a2234_ApplyWaterListColorFade(u32 color, s16 x, s16 z)
{
	const s16 *center = DrawLevelOvr1P_Scratch()->projectedCenter.v;
	s32 deltaX = (s32)x - center[0];
	s32 deltaZ = (s32)z - center[2];

	if (deltaX < 0)
	{
		deltaX = -deltaX;
	}

	if (deltaZ < 0)
	{
		deltaZ = -deltaZ;
	}

	s32 major = deltaX;
	s32 minor = deltaZ;
	if (deltaX < deltaZ)
	{
		major = deltaZ;
		minor = deltaX;
	}

	s32 falloff = major + (minor >> 2) - 0x1000;
	if (falloff >= 0)
	{
		return 0;
	}

	s32 factor = (s32)((u32)falloff << 2) + 0x1000;
	// NOTE(aalhendi): Retail 0x800a2234 fades water-list vertex color with DPCS.
	MTC2(color, 6);
	if (factor < 0)
	{
		return color;
	}

	MTC2(factor, 8);
	gte_dpcs();
	return MFC2(22);
}

static u32 Ovr226_800a2d30_ApplyWaterRenderedColorFade(u32 color, s16 x, s16 z)
{
	const s16 *center = DrawLevelOvr1P_Scratch()->projectedCenter.v;
	s32 deltaX = (s32)x - center[0];
	s32 deltaZ = (s32)z - center[2];

	if (deltaX < 0)
	{
		deltaX = -deltaX;
	}

	if (deltaZ < 0)
	{
		deltaZ = -deltaZ;
	}

	s32 major = deltaX;
	s32 minor = deltaZ;
	if (deltaX < deltaZ)
	{
		major = deltaZ;
		minor = deltaX;
	}

	s32 falloff = major + (minor >> 2) - 0x1000;
	if (falloff >= 0)
	{
		return 0;
	}

	s32 factor = (s32)((u32)falloff << 2) + 0x1000;
	// NOTE(aalhendi): Retail 0x800a2d30 fades water-rendered vertex color with DPCS.
	MTC2(color, 6);
	if (factor < 0)
	{
		return color;
	}

	MTC2(factor, 8);
	gte_dpcs();
	return MFC2(22);
}

static void Ovr226_800a211c_ApplyWaterListColorFades(struct DrawLevelOvr1PScratchVertex *projected)
{
	for (s32 waterVertexIndex = 0; waterVertexIndex < 9; waterVertexIndex++)
	{
		DrawLevelOvr1P_WritePackedWord(projected[waterVertexIndex].color_hi,
		                               Ovr226_800a2234_ApplyWaterListColorFade(DrawLevelOvr1P_GetProjectedColorWord(&projected[waterVertexIndex]),
		                                                                       projected[waterVertexIndex].pos[0], projected[waterVertexIndex].pos[2]));
	}
}

static void Ovr226_800a2c4c_ApplyWaterRenderedColorFades(struct DrawLevelOvr1PScratchVertex *projected)
{
	for (s32 waterVertexIndex = 0; waterVertexIndex < 9; waterVertexIndex++)
	{
		DrawLevelOvr1P_WritePackedWord(projected[waterVertexIndex].color_hi,
		                               Ovr226_800a2d30_ApplyWaterRenderedColorFade(DrawLevelOvr1P_GetProjectedColorWord(&projected[waterVertexIndex]),
		                                                                           projected[waterVertexIndex].pos[0], projected[waterVertexIndex].pos[2]));
	}
}

static int DrawLevelOvr1P_NclipProjected(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	const struct DrawLevelOvr1PScratchVertex *vertex0 = &projected[indices[0]];
	const struct DrawLevelOvr1PScratchVertex *vertex1 = &projected[indices[1]];
	const struct DrawLevelOvr1PScratchVertex *vertex2 = &projected[indices[2]];
	u32 sxy2;
	s32 nclip;

	u32 sxy0 = (u16)vertex0->posScreen[0] | ((u32)(u16)vertex0->posScreen[1] << 16);
	u32 sxy1 = (u16)vertex1->posScreen[0] | ((u32)(u16)vertex1->posScreen[1] << 16);
	sxy2 = (u16)vertex2->posScreen[0] | ((u32)(u16)vertex2->posScreen[1] << 16);

#if defined(CTR_NATIVE)
    /* Modes: '1' conservatively compares the optimized code to all the
     * existing MTC2/GTE/MFC2 side effects, returning retail's result.
     * '2' uses the fused native path with the same register writes.
     * Unset leaves normal rendering entirely unchanged. */
    static int checked,mode;
    static unsigned long verified,mismatches,fused;
    if(!checked)
    {
        FILE *f=fopen("debug/level_fused_nclip_qa.txt","rb");
        if(f)
        {
            const int c=fgetc(f);
            mode=(c=='1') ? 1 : ((c=='2') ? 2 : 0);
            fclose(f);
        }
        checked=1;
        if(mode)Platform_Log("[LEVEL CPU] fused NCLIP QA mode=%d (1=verify, 2=fast)\n",mode);
    }
    if(mode==2)
    {
        nclip=NativeGTE_NCLIPLoadedTriple(sxy0,sxy1,sxy2);
        ++fused;
    }
    else
#endif
    {
        /* Retail helper paths need MAC0 and the exact GTE register state. */
        MTC2(sxy0, 12);
        MTC2(sxy1, 13);
        MTC2(sxy2, 14);
        gte_nclip();
        gte_stopz(&nclip);
    }
#if defined(CTR_NATIVE)
    if(mode==1)
    {
        const s32 optimized=NativeGTE_NCLIPLoadedTriple(sxy0,sxy1,sxy2);
        ++verified;
        if(optimized!=nclip)
        {
            ++mismatches;
            if(mismatches<=8)
                Platform_LogWarn("[LEVEL CPU] fused NCLIP MISMATCH retail=%d optimized=%d count=%lu\n",
                                 nclip,optimized,mismatches);
        }
        if((verified&0x1fffful)==0)
            Platform_Log("[LEVEL CPU] fused NCLIP verification samples=%lu mismatches=%lu\n",
                         verified,mismatches);
    }
    if(mode==2 && (fused&0x3fffful)==0)
        Platform_Log("[LEVEL CPU] fused NCLIP fast calls=%lu\n",fused);
    return NativeGpu_CorrectSubpixelNclip(
        vertex0->posScreen, vertex1->posScreen, vertex2->posScreen, nclip);
#else
    return nclip;
#endif
}

static u32 DrawLevelOvr1P_PackProjectedSxy(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return (u16)projected->posScreen[0] | ((u32)(u16)projected->posScreen[1] << 16);
}

static int DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	u32 packedWindow = DrawLevelOvr1P_Scratch()->clipWindowPacked;
	u32 packedAnd = 0xffffffff;
	u32 packedSubOr = 0;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		u32 packedSxy = DrawLevelOvr1P_PackProjectedSxy(&projected[indices[vertexIndex]]);

		packedAnd &= packedSxy;
		packedSubOr |= packedSxy - packedWindow;
	}

	// NOTE(aalhendi): Retail uses packed SXY sign bits to reject polys that are
	// wholly left/right/top/bottom of the pushbuffer rectangle.
	u32 packedReject = ~packedSubOr | packedAnd;
	return ((s32)packedReject < 0) || ((s32)(packedReject << 16) < 0);
}

static int DrawLevelOvr1P_IsProjectedFaceOffscreen(struct PushBuffer *pb, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	(void)pb;

	return DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(projected, indices, 4);
}

static int DrawLevelOvr1P_IsProjectedTriOffscreen(struct PushBuffer *pb, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	(void)pb;

	return DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(projected, indices, 3);
}

static int DrawLevelOvr1P_IsProjectedFaceFullyNear(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	return projected[indices[0]].clipHalfNear && projected[indices[1]].clipHalfNear && projected[indices[2]].clipHalfNear && projected[indices[3]].clipHalfNear;
}

static u32 DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 scratchOffset)
{
	static const u32 bits[4] = {0x4, 0x8, 0x10, 0x20};
	u32 threshold = *CTR_SCRATCHPAD_PTR(u32, scratchOffset);
	u32 mask = 0;

	for (s32 bitIndex = 0; bitIndex < 4; bitIndex++)
	{
		if (DrawLevelOvr1P_MipsSubuSignBit(projected[indices[bitIndex]].depth, threshold))
		{
			mask |= bits[bitIndex];
		}
	}

	return mask;
}

static u32 DrawLevelOvr1P_GetProjectedNearMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, 0x24);
}

static u32 DrawLevelOvr1P_GetProjectedRecursiveNearMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	// NOTE(aalhendi): Retail helper recursion switches from the top-level
	// 0x24 threshold to scratch 0x28 before selecting the next child handler.
	return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, 0x28);
}

static u32 DrawLevelOvr1P_GetProjectedWaterNearMaskAtScratchOffset(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                                    u32 scratchOffset)
{
	static const u32 bits[4] = {0x4, 0x8, 0x10, 0x20};
	u32 threshold = *CTR_SCRATCHPAD_PTR(u32, scratchOffset);
	u32 mask = 0;

#if defined(CTR_NATIVE)
	/* Native keeps the retail recursive near thresholds for both ground and
	   water. High-detail source buckets still stay visible farther away, but
	   extra PS1-era recursive tessellation is intentionally not extended. */
#endif

	for (s32 bitIndex = 0; bitIndex < 4; bitIndex++)
	{
		if (DrawLevelOvr1P_MipsSubuSignBit(projected[indices[bitIndex]].depth, threshold))
			mask |= bits[bitIndex];
	}

	return mask;
}

static u32 DrawLevelOvr1P_GetProjectedWaterNearMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	// NOTE(aalhendi): Retail water shared bodies test s8 against scratch 0x1b4;
	// recursive frames use scratch 0x28 before selecting the next child.
	if (projected == DrawLevelOvr1P_GetScratchVertices())
		return DrawLevelOvr1P_GetProjectedWaterNearMaskAtScratchOffset(projected, indices, 0x24);

	return DrawLevelOvr1P_GetProjectedWaterNearMaskAtScratchOffset(projected, indices, 0x28);
}

static u32 DrawLevelOvr1P_GetProjectedCopiedGridNearMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	// NOTE(aalhendi): Copied/default body entries use scratch 0x28 here.
	// Selector labels inside the same spans, like 0x800a7668/0x800a8380,
	// reselect texture first and then compute the near mask from 0x24.
	return DrawLevelOvr1P_GetProjectedRecursiveNearMask(projected, indices);
}

static void DrawLevelOvr1P_StoreProjectedDepthWord(struct DrawLevelOvr1PScratchVertex *projected, u32 depth)
{
	projected->depth = (u16)depth;
	projected->clipNear = (u8)(depth >> 16);
	projected->clipHalfNear = (u8)(depth >> 24);
}

static void DrawLevelOvr1P_ProjectCopiedGridListMidpoint(struct DrawLevelOvr1PScratchVertex *projected)
{
	u32 depth;

	CTR_GteLoadS16TripletV0(&projected->pos[0]);
	gte_rtps();
	CTR_GteStoreSXY(&projected->posScreen[0]);
	gte_stsz(&depth);

	// NOTE(aalhendi): Retail copied/default list helpers store full SZ3 at +0x10.
	DrawLevelOvr1P_StoreProjectedDepthWord(projected, depth);
}

static void DrawLevelOvr1P_ProjectCopiedGridRenderedMidpoint(struct DrawLevelOvr1PScratchVertex *projected)
{
	u32 depth;
	u32 threshold = (u32)DrawLevelOvr1P_GetDepthClipThreshold();

	CTR_GteLoadS16TripletV0(&projected->pos[0]);
	gte_rtps();
	CTR_GteStoreSXY(&projected->posScreen[0]);
	gte_stsz(&depth);

	DrawLevelOvr1P_StoreProjectedDepthWord(projected, depth);
	projected->clipNear = DrawLevelOvr1P_MipsSubuSignBit(depth, threshold);
	projected->clipHalfNear = DrawLevelOvr1P_MipsSubuSignBit(depth << 1, threshold);
}

static void DrawLevelOvr1P_BuildMidpointValue(struct DrawLevelOvr1PScratchVertex *dstMid, const struct DrawLevelOvr1PScratchVertex *srcA,
                                              const struct DrawLevelOvr1PScratchVertex *srcB, int writeClipBytes)
{
	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	u8 *dstUv = (u8 *)&dstMid->flags;
	const u8 *srcAUv = (const u8 *)&srcA->flags;
	const u8 *srcBUv = (const u8 *)&srcB->flags;
	dstUv[0] = (u8)(((u32)srcAUv[0] + (u32)srcBUv[0]) >> 1);
	dstUv[1] = (u8)(((u32)srcAUv[1] + (u32)srcBUv[1]) >> 1);

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	// NOTE(aalhendi): Retail midpoint helpers only write RGB at 0x8..0xa; model
	// the pre-cleared code byte explicitly in native scratch.
	dstMid->color_hi[3] = 0;
	if (writeClipBytes)
	{
		DrawLevelOvr1P_ProjectCopiedGridRenderedMidpoint(dstMid);
	}
	else
	{
		DrawLevelOvr1P_ProjectCopiedGridListMidpoint(dstMid);
	}
}

static void Ovr226_800a3a78_BuildGround4x1ListMidpointPair(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstB,
                                                           struct DrawLevelOvr1PScratchVertex *dstMid, const struct DrawLevelOvr1PScratchVertex *srcA,
                                                           const struct DrawLevelOvr1PScratchVertex *srcB)
{
	u8 *dstABytes = (u8 *)dstA;
	u8 *dstBBytes = (u8 *)dstB;
	u8 *dstMidBytes = (u8 *)dstMid;
	const u8 *srcABytes = (const u8 *)srcA;
	const u8 *srcBBytes = (const u8 *)srcB;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstB->pos[axisIndex] = srcB->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (int offset = 8; offset <= 10; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	for (int offset = 6; offset <= 7; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	DrawLevelOvr1P_CopyProjectedScreenDepth(dstB, srcB);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
}

static void Ovr226_800a3a78_BuildGround4x1ListSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                               const int *indices)
{
	Ovr226_800a3a78_BuildGround4x1ListMidpointPair(&sub[0], &sub[1], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a3a78_BuildGround4x1ListMidpointPair(&sub[2], &sub[3], &sub[8], &projected[indices[2]], &projected[indices[3]]);
}

static void Ovr226_800a560c_BuildGround4x2ListEdgeMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                           const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	u8 *dstABytes = (u8 *)dstA;
	u8 *dstMidBytes = (u8 *)dstMid;
	const u8 *srcABytes = (const u8 *)srcA;
	const u8 *srcBBytes = (const u8 *)srcB;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (int offset = 8; offset <= 10; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	for (int offset = 6; offset <= 7; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
}

static void Ovr226_800a56f4_BuildGround4x2ListPairMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstB,
                                                           struct DrawLevelOvr1PScratchVertex *dstMid, const struct DrawLevelOvr1PScratchVertex *srcA,
                                                           const struct DrawLevelOvr1PScratchVertex *srcB)
{
	u8 *dstABytes = (u8 *)dstA;
	u8 *dstBBytes = (u8 *)dstB;
	u8 *dstMidBytes = (u8 *)dstMid;
	const u8 *srcABytes = (const u8 *)srcA;
	const u8 *srcBBytes = (const u8 *)srcB;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstB->pos[axisIndex] = srcB->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (int offset = 8; offset <= 10; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	for (int offset = 6; offset <= 7; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	DrawLevelOvr1P_CopyProjectedScreenDepth(dstB, srcB);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
}

static void Ovr226_800a56f4_BuildGround4x2ListSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                               const int *indices)
{
	Ovr226_800a56f4_BuildGround4x2ListPairMidpoint(&sub[0], &sub[1], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a56f4_BuildGround4x2ListPairMidpoint(&sub[2], &sub[3], &sub[8], &projected[indices[2]], &projected[indices[3]]);

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(sub))
	{
		return;
	}

	Ovr226_800a560c_BuildGround4x2ListEdgeMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a560c_BuildGround4x2ListEdgeMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
	Ovr226_800a560c_BuildGround4x2ListEdgeMidpoint(&sub[3], &sub[7], &projected[indices[3]], &projected[indices[1]]);
}

static void Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                               const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	u8 *dstABytes = (u8 *)dstA;
	u8 *dstMidBytes = (u8 *)dstMid;
	const u8 *srcABytes = (const u8 *)srcA;
	const u8 *srcBBytes = (const u8 *)srcB;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (int offset = 8; offset <= 10; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	for (int offset = 6; offset <= 7; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_SetProjectedDepth(dstMid, depth, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
}

static void Ovr226_800a6510_BuildGround4x2RenderedSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                   const int *indices)
{
	Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(&sub[2], &sub[8], &projected[indices[2]], &projected[indices[3]]);
	Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
	Ovr226_800a6510_BuildGround4x2RenderedEdgeMidpoint(&sub[3], &sub[7], &projected[indices[3]], &projected[indices[1]]);
}

static void Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                              const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	const u8 *srcAUv = (const u8 *)&srcA->flags;
	const u8 *srcBUv = (const u8 *)&srcB->flags;
	u8 *dstAUv = (u8 *)&dstA->flags;
	u8 *dstMidUv = (u8 *)&dstMid->flags;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstA->color_hi[colorChannel] = srcA->color_hi[colorChannel];
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	for (s32 uvByte = 0; uvByte < 2; uvByte++)
	{
		dstAUv[uvByte] = srcAUv[uvByte];
		dstMidUv[uvByte] = (u8)(((u32)srcAUv[uvByte] + (u32)srcBUv[uvByte]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
#if defined(CTR_NATIVE)
    /* GPU tessellation diagnostic after original GTE/UV/ARGB calculations:
     * no change to PS1 OT, visual output, clipping or collision. */
    NativeRenderer_LevelPCMidpointQARecord(srcA,srcB,dstMid);
#endif
}

static void Ovr226_800a74a0_BuildDynamicListSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                             const int *indices)
{
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
}

static void Ovr226_800a90c0_BuildWideDynamicSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                             const int *indices)
{
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a74a0_BuildDynamicListSubdivideMidpoint(&sub[4], &sub[6], &sub[4], &sub[8]);
}

static void Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                                  const struct DrawLevelOvr1PScratchVertex *srcA,
                                                                  const struct DrawLevelOvr1PScratchVertex *srcB)
{
	const u8 *srcAUv = (const u8 *)&srcA->flags;
	const u8 *srcBUv = (const u8 *)&srcB->flags;
	u8 *dstAUv = (u8 *)&dstA->flags;
	u8 *dstMidUv = (u8 *)&dstMid->flags;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstA->color_hi[colorChannel] = srcA->color_hi[colorChannel];
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	for (s32 uvByte = 0; uvByte < 2; uvByte++)
	{
		dstAUv[uvByte] = srcAUv[uvByte];
		dstMidUv[uvByte] = (u8)(((u32)srcAUv[uvByte] + (u32)srcBUv[uvByte]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_SetProjectedDepth(dstMid, depth, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
}

static void Ovr226_800a8150_BuildDynamicRenderedSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                 const int *indices)
{
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
}

static void Ovr226_800a9d70_BuildQuad4x4RenderedSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                 const int *indices)
{
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a8150_BuildDynamicRenderedSubdivideMidpoint(&sub[4], &sub[6], &sub[4], &sub[8]);
}

static void Ovr226_800a4594_BuildGround4x1RenderedMidpointPair(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstB,
                                                               struct DrawLevelOvr1PScratchVertex *dstMid, const struct DrawLevelOvr1PScratchVertex *srcA,
                                                               const struct DrawLevelOvr1PScratchVertex *srcB)
{
	u8 *dstABytes = (u8 *)dstA;
	u8 *dstBBytes = (u8 *)dstB;
	u8 *dstMidBytes = (u8 *)dstMid;
	const u8 *srcABytes = (const u8 *)srcA;
	const u8 *srcBBytes = (const u8 *)srcB;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstB->pos[axisIndex] = srcB->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (int offset = 8; offset <= 10; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	for (int offset = 6; offset <= 7; offset++)
	{
		dstABytes[offset] = srcABytes[offset];
		dstBBytes[offset] = srcBBytes[offset];
		dstMidBytes[offset] = (u8)(((u32)srcABytes[offset] + (u32)srcBBytes[offset]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	DrawLevelOvr1P_CopyProjectedScreenDepth(dstB, srcB);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_SetProjectedDepth(dstMid, depth, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
}

static void Ovr226_800a4594_BuildGround4x1RenderedSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                   const int *indices)
{
	Ovr226_800a4594_BuildGround4x1RenderedMidpointPair(&sub[0], &sub[1], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a4594_BuildGround4x1RenderedMidpointPair(&sub[2], &sub[3], &sub[8], &projected[indices[2]], &projected[indices[3]]);
}

static void DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                          const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB,
                                                          int writeClipBytes)
{
	*dstA = *srcA;
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(dstA->posScreen,srcA->posScreen,DrawLevelOvr1P_PackProjectedSxy(srcA));
#endif
	dstA->color_hi[3] = 0;
	DrawLevelOvr1P_BuildMidpointValue(dstMid, srcA, srcB, writeClipBytes);
}

static void Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                              const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	const u8 *srcABytes = (const u8 *)&srcA->flags;
	const u8 *srcBBytes = (const u8 *)&srcB->flags;
	u8 *dstABytes = (u8 *)&dstA->flags;
	u8 *dstMidBytes = (u8 *)&dstMid->flags;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstA->color_hi[colorChannel] = srcA->color_hi[colorChannel];
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	for (s32 uvByte = 0; uvByte < 2; uvByte++)
	{
		dstABytes[uvByte] = srcABytes[uvByte];
		dstMidBytes[uvByte] = (u8)(((u32)srcABytes[uvByte] + (u32)srcBBytes[uvByte]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
}

static void Ovr226_800a17d8_BuildFullDynamicSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                             const int *indices)
{
	Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a17d8_BuildFullDynamicSubdivideMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
}

static u32 DrawLevelOvr1P_GetProjectedMaxDepth(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 maxDepth = projected[indices[0]].depth;

	for (s32 vertexIndex = 1; vertexIndex < 4; vertexIndex++)
	{
		if (maxDepth < projected[indices[vertexIndex]].depth)
		{
			maxDepth = projected[indices[vertexIndex]].depth;
		}
	}

	return maxDepth;
}

static u32 DrawLevelOvr1P_GetProjectedTriMaxDepth(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 maxDepth = projected[indices[0]].depth;

	for (s32 vertexIndex = 1; vertexIndex < 3; vertexIndex++)
	{
		if (maxDepth < projected[indices[vertexIndex]].depth)
		{
			maxDepth = projected[indices[vertexIndex]].depth;
		}
	}

	return maxDepth;
}

static int DrawLevelOvr1P_GetProjectedOtIndex(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth, int faceIndex)
{
	s32 otIndex;

	if (faceIndex < 0)
	{
		otIndex = (s32)(maxDepth >> 6) + (s8)(block->draw_order_low & 0xff);
	}
	else
	{
		u32 slotWord = DrawLevelOvr1P_GetProjectedOtSlotWord(projected, faceIndex);
		s8 drawOrder = DrawLevelOvr1P_ReadRetailQuadBlockByte(block, 0x18 + (slotWord >> 2));

		otIndex = (s32)(maxDepth >> 6) + drawOrder;
	}

	if (otIndex < 0)
	{
		return 0;
	}

	if (otIndex > DRAW_LEVEL_OVR1P_MAX_OT_INDEX)
	{
		return DRAW_LEVEL_OVR1P_MAX_OT_INDEX;
	}

	return otIndex;
}

static void DrawLevelOvr1P_SetFullDynamicInheritedOtIndex(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth,
                                                          int faceIndex)
{
	// NOTE(aalhendi): Retail full-dynamic paths keep the selected OT entry in `gp`
	// until 0x800a1cc4 links the direct primitive.
	sDrawLevelOvr1P_FullDynamicInheritedOtIndex = DrawLevelOvr1P_GetProjectedOtIndex(block, projected, maxDepth, faceIndex);
}

static int DrawLevelOvr1P_ResolveProjectedOtIndex(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth,
                                                  int faceIndex, int otIndexOverride)
{
	if (otIndexOverride >= 0)
	{
		return otIndexOverride;
	}

	return DrawLevelOvr1P_GetProjectedOtIndex(block, projected, maxDepth, faceIndex);
}

static u32 DrawLevelOvr1P_GetWaterTopFrameOtDepth(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	s32 depth0 = projected[indices[0]].depth;
	s32 depth1 = projected[indices[1]].depth;
	s32 depth2 = projected[indices[2]].depth;
	s32 depth3 = projected[indices[3]].depth;
	s32 delta;
	s32 deltaToDepth3;
	u32 selectedDepth;

	selectedDepth = depth0;
	delta = depth0 - depth1;
	if (delta <= 0)
	{
		selectedDepth = depth1;
	}

	delta = (s32)selectedDepth - depth2;
	deltaToDepth3 = (s32)selectedDepth - depth3;
	if (delta <= 0)
	{
		selectedDepth = depth2;
	}

	// NOTE(aalhendi): Retail water top-frame OT setup compares vertex 3 from the
	// pre-depth2 selected value in the branch delay slot.
	if (deltaToDepth3 <= 0)
	{
		selectedDepth = depth3;
	}

	return selectedDepth;
}

static uint32_t *Ovr226_800a2690_ResolveWaterListInheritedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                  const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                  uint32_t *inheritedOtEntry)
{
	u32 slotWord;
	s8 drawOrder;
	u32 selectedDepth;
	s32 otIndex;

	if (inheritedOtEntry != NULL)
	{
		return inheritedOtEntry;
	}

	// NOTE(aalhendi): Retail 0x800a2690..0x800a26d8 computes GP only on the
	// top water-list scratch frame and does not clamp the signed OT index.
	if (projected != DrawLevelOvr1P_GetScratchVertices())
	{
		return NULL;
	}

	selectedDepth = DrawLevelOvr1P_GetWaterTopFrameOtDepth(projected, indices);
	slotWord = DrawLevelOvr1P_GetProjectedOtSlotWord(projected, faceIndex);
	drawOrder = DrawLevelOvr1P_ReadRetailQuadBlockByte(block, 0x18 + (slotWord >> 2));
	otIndex = (s32)(selectedDepth >> 6) + drawOrder;

	return &pb->ptrOT[otIndex];
}

static void DrawLevelOvr1P_AddRawPrimToOt(struct PrimMem *primMem, void *packet, u32 wordCount, uint32_t *otEntry)
{
	u32 *packetTag = packet;

	*packetTag = CtrGpu_PackOTTag(*otEntry, wordCount << 24);
	*otEntry = CtrGpu_PrimToOTLink24(packet);
	// NOTE(aalhendi): Retail keeps the overlay primitive count in `sp`, seeded
	// from PrimMem+0x14 and stored back at the epilogue.
	primMem->primitiveCount++;
}

static u32 DrawLevelOvr1P_GetProjectedColorCode(const struct DrawLevelOvr1PScratchVertex *projected, u32 code)
{
	return DrawLevelOvr1P_ReadPackedWord(projected->color_hi) | (code << 24);
}

static u32 DrawLevelOvr1P_GetProjectedColorWord(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return DrawLevelOvr1P_ReadPackedWord(projected->color_hi);
}

static void DrawLevelOvr1P_WriteProjectedGT3(POLY_GT3 *poly, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 code, u32 uv0,
                                             u32 uv1, u32 uv2)
{
	CtrGpu_WriteColorCode(&poly->r0, DrawLevelOvr1P_GetProjectedColorCode(&projected[indices[0]], code));
	CtrGpu_WritePackedXY(&poly->x0, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[0]]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x0, projected[indices[0]].posScreen, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[0]]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u0, uv0);
	CtrGpu_WriteColorCode(&poly->r1, DrawLevelOvr1P_GetProjectedColorCode(&projected[indices[1]], 0));
	CtrGpu_WritePackedXY(&poly->x1, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[1]]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x1, projected[indices[1]].posScreen, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[1]]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u1, uv1);
	CtrGpu_WriteColorCode(&poly->r2, DrawLevelOvr1P_GetProjectedColorCode(&projected[indices[2]], 0));
	CtrGpu_WritePackedXY(&poly->x2, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[2]]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x2, projected[indices[2]].posScreen, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[2]]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u2, uv2);
}

static void DrawLevelOvr1P_WriteProjectedGT4(POLY_GT4 *poly, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 code, u32 uv0,
                                             u32 uv1, u32 uv2)
{
	DrawLevelOvr1P_WriteProjectedGT3((POLY_GT3 *)poly, projected, indices, code, uv0, uv1, uv2);
	CtrGpu_WriteColorCode(&poly->r3, DrawLevelOvr1P_GetProjectedColorCode(&projected[indices[3]], 0));
	CtrGpu_WritePackedXY(&poly->x3, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[3]]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x3, projected[indices[3]].posScreen, DrawLevelOvr1P_PackProjectedSxy(&projected[indices[3]]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u3, uv2 >> 16);
}

static u32 DrawLevelOvr1P_SelectRawPrimitiveCode(u32 uv1Word, u32 semiTransCode, u32 opaqueCode)
{
	return ((uv1Word & 0x00600000) != 0x00600000) ? semiTransCode : opaqueCode;
}

static int DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                                    const struct TextureLayout *texture, uint32_t *otEntry, int primCodeOverride)
{
	(void)pb;
	(void)block;

	POLY_GT4 *prim = primMem->cursor;
	POLY_GT4 *nextPrim = prim + 1;

	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, indices, 4);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;

	// NOTE(aalhendi): Generic retail raw GT4 writers select from scratch
	// UV1/tpage; water terminal labels pass a fixed-code override.
	(void)texture;
	u32 code = primCodeOverride >= 0 ? (u32)primCodeOverride : DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x3e, 0x3c);

	DrawLevelOvr1P_WriteProjectedGT4(prim, projected, indices, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 12, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, u32 maxDepth, int otIndexOverride, int primCodeOverride)
{
	int otIndex = DrawLevelOvr1P_ResolveProjectedOtIndex(block, projected, maxDepth, faceIndex, otIndexOverride);

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, &pb->ptrOT[otIndex], primCodeOverride);
}

static int DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                                   const struct TextureLayout *texture, uint32_t *otEntry, int primCodeOverride)
{
	(void)pb;
	(void)block;

	POLY_GT3 *prim = primMem->cursor;
	POLY_GT3 *nextPrim = prim + 1;

	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, indices, 3);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;

	// NOTE(aalhendi): Generic retail raw GT3 writers select from scratch
	// UV1/tpage; water terminal labels pass a fixed-code override.
	(void)texture;
	u32 code = primCodeOverride >= 0 ? (u32)primCodeOverride : DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x36, 0x34);

	DrawLevelOvr1P_WriteProjectedGT3(prim, projected, indices, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 9, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                              const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                              const struct TextureLayout *texture, u32 maxDepth, int otIndexOverride, int primCodeOverride)
{
	int otIndex = DrawLevelOvr1P_ResolveProjectedOtIndex(block, projected, maxDepth, faceIndex, otIndexOverride);

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, &pb->ptrOT[otIndex], primCodeOverride);
}

static int DrawLevelOvr1P_IsClipByteSet(u8 clipByte)
{
	return clipByte != 0;
}

static int DrawLevelOvr1P_AreProjectedVerticesHalfNear(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		if (!DrawLevelOvr1P_IsClipByteSet(projected[indices[vertexIndex]].clipHalfNear))
		{
			return 0;
		}
	}

	return 1;
}

static int DrawLevelOvr1P_HasProjectedVertexNear(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		if (DrawLevelOvr1P_IsClipByteSet(projected[indices[vertexIndex]].clipNear))
		{
			return 1;
		}
	}

	return 0;
}

static s32 DrawLevelOvr1P_MulShift16(s32 delta, s32 factor)
{
	u32 product = (u32)((s64)delta * factor);

	return ((s32)product) >> 16;
}

static s16 DrawLevelOvr1P_LerpS16_16(s16 a, s16 b, s32 factor)
{
	return (s16)((s32)a + DrawLevelOvr1P_MulShift16((s32)b - (s32)a, factor));
}

static u8 DrawLevelOvr1P_LerpU8_16(u8 a, u8 b, s32 factor)
{
	return (u8)((s32)a + DrawLevelOvr1P_MulShift16((s32)b - (s32)a, factor));
}

static s32 DrawLevelOvr1P_ShiftLeft16Wrap(s32 value)
{
	return (s32)((u32)value << 16);
}

static s16 DrawLevelOvr1P_ShiftLeft1S16Wrap(s16 value)
{
	return (s16)((u32)(s32)value << 1);
}

static u32 DrawLevelOvr1P_ReadWord(const void *base, u32 offset)
{
	return DrawLevelOvr1P_ReadPackedWord((const u8 *)base + offset);
}

static void Ovr226_800a1408_AdjustFullDynamicMidVertex(struct DrawLevelOvr1PScratchVertex *projected, struct LevVertex *vertices, const struct QuadBlock *block,
                                                       int midIndex, int endpointAIndex, int endpointBIndex)
{
	struct DrawLevelOvr1PScratchVertex *mid = &projected[midIndex];
	const struct DrawLevelOvr1PScratchVertex *endpointA = &projected[endpointAIndex];
	const struct DrawLevelOvr1PScratchVertex *endpointB = &projected[endpointBIndex];
	const struct LevVertex *levMid = &vertices[block->index[midIndex]];
	s16 midpoint[3];

	s32 factor = (s32)((DrawLevelOvr1P_ReadWord(mid, 0x10) - DrawLevelOvr1P_RenderScratch()->fullDynamicFadeDepthStart) << 2);
	if (factor < 0)
	{
		return;
	}

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		midpoint[axisIndex] = (s16)(((s32)endpointA->pos[axisIndex] + (s32)endpointB->pos[axisIndex]) >> 1);
	}

	u32 color = DrawLevelOvr1P_ReadWord(levMid, 0xc);

	if (factor < 0x1000)
	{
		u32 farColor = color << 4;

		// NOTE(aalhendi): Retail full-dynamic helper 0x800a1408 uses DPCS/GPL12
		// to fade the projected midpoint toward the endpoint midpoint.
		MTC2(factor, 8);
		MTC2(DrawLevelOvr1P_ReadWord(mid, 0x8), 6);
		CTC2((farColor >> 16) & 0xff0, 23);
		CTC2((farColor >> 8) & 0xff0, 22);
		CTC2(farColor & 0xff0, 21);
		gte_dpcs();
		color = MFC2(22);

		MTC2((u32)(s32)mid->pos[0], 25);
		MTC2((u32)(s32)(midpoint[0] - mid->pos[0]), 9);
		MTC2((u32)(s32)mid->pos[1], 26);
		MTC2((u32)(s32)(midpoint[1] - mid->pos[1]), 10);
		MTC2((u32)(s32)mid->pos[2], 27);
		MTC2((u32)(s32)(midpoint[2] - mid->pos[2]), 11);
		gte_gpl12();

		midpoint[0] = (s16)MFC2_S(25);
		midpoint[1] = (s16)MFC2_S(26);
		midpoint[2] = (s16)MFC2_S(27);
	}

	mid->pos[0] = midpoint[0];
	mid->pos[1] = midpoint[1];
	mid->pos[2] = midpoint[2];
	DrawLevelOvr1P_WritePackedWord(mid->color_hi, color);

	u32 depth;
	MTC2(DrawLevelOvr1P_ReadWord(&mid->pos[0], 0), 0);
	MTC2((u32)(s32)mid->pos[2], 1);
	gte_rtps();
	CTR_GteStoreSXY(&mid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(mid, depth);
}

static void DrawLevelOvr1P_AdjustFullDynamicMidVertices(struct DrawLevelOvr1PScratchVertex *projected, struct LevVertex *vertices,
                                                        const struct QuadBlock *block)
{
	Ovr226_800a1408_AdjustFullDynamicMidVertex(projected, vertices, block, 4, 0, 1);
	Ovr226_800a1408_AdjustFullDynamicMidVertex(projected, vertices, block, 5, 0, 2);
	Ovr226_800a1408_AdjustFullDynamicMidVertex(projected, vertices, block, 6, 1, 2);
	Ovr226_800a1408_AdjustFullDynamicMidVertex(projected, vertices, block, 7, 1, 3);
	Ovr226_800a1408_AdjustFullDynamicMidVertex(projected, vertices, block, 8, 2, 3);
}

static u32 DrawLevelOvr1P_GetPreparedProjectedMaxDepthCount(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	u32 maxDepth = projected[indices[0]].depth;

	for (s32 vertexIndex = 1; vertexIndex < count; vertexIndex++)
	{
		if (maxDepth < projected[indices[vertexIndex]].depth)
		{
			maxDepth = projected[indices[vertexIndex]].depth;
		}
	}

	return maxDepth;
}

static int DrawLevelOvr1P_SourceInsideClipRecordWindow(const struct DrawLevelOvr1PScratchVertex *src)
{
	const s16 *center = DrawLevelOvr1P_Scratch()->projectedCenter.v;
	s32 x = (s32)src->pos[0] - center[0];
	s32 y = (s32)src->pos[1] - center[1];
	s32 z = (s32)src->pos[2] - center[2];

	if (x < 0)
	{
		x = -x;
	}

	if (y < 0)
	{
		y = -y;
	}

	if (z < 0)
	{
		z = -z;
	}

	// NOTE(aalhendi): Retail 0x800a4f38/0x800a3640 use the sign bit of these
	// extent checks ANDed together; negative means the source point is inside.
	u32 insideBits = (u32)(x - 0x100) & (u32)(y - 0x180) & (u32)(z - 0x100);
	return (s32)insideBits < 0;
}

static int DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		if (DrawLevelOvr1P_SourceInsideClipRecordWindow(&projected[indices[vertexIndex]]))
		{
			return 1;
		}
	}

	return 0;
}

static void DrawLevelOvr1P_CopyClipRecordVertex(struct DrawLevelOvr1PClipRecordVertex *dst, const struct DrawLevelOvr1PScratchVertex *src)
{
	dst->posVec = src->posVec;
	dst->flags = src->flags;
	DrawLevelOvr1P_CopyColorWord(dst->color_hi, src->color_hi);

	// NOTE(aalhendi): Retail 0x800a4f78/0x800a3680 OR scratch byte 0x13 into
	// the record color/code word; the 0x800aa790 consumer uses it for near masks.
	dst->color_hi[3] |= src->clipHalfNear;
}

static void DrawLevelOvr1P_StoreRenderedClipRecordHeader(u32 tableWord)
{
	// NOTE(aalhendi): Retail rendered helpers 0x800a84dc/0x800aa0fc store
	// clipped-record polarity at scratch 0x80 before helper-local dispatch.
	DrawLevelOvr1P_Scratch()->clipRecordHeader = (DrawLevelOvr1P_GetActiveDrawOrderLow() & 0x80000000u) | ((tableWord & 0x80000000u) >> 1);
}

static u32 DrawLevelOvr1P_GetRenderedClipRecordHeader(const struct QuadBlock *block, int count)
{
	u32 header = DrawLevelOvr1P_Scratch()->clipRecordHeader;

	(void)block;

	// NOTE(aalhendi): Retail 0x800a4dcc/0x800a4e70 ORs the GT4 count bit into
	// the stored scratch 0x80 polarity word when writing clipped records.
	if (count == 4)
	{
		header |= 1;
	}

	return header;
}

static int DrawLevelOvr1P_WriteRenderedClippedRecordAtOt(struct PushBuffer *pb, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count, int faceIndex,
                                                         const struct TextureLayout *texture, int otIndexOverride)
{
	struct DrawLevelOvr1PClipRecord *record;
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);
	u32 maxDepth;
	int otIndex;

	(void)texture;

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	if (!DrawLevelOvr1P_HasClipRecordSpace(recordSize))
	{
		return 1;
	}

	maxDepth = DrawLevelOvr1P_GetPreparedProjectedMaxDepthCount(projected, indices, count);
	otIndex = DrawLevelOvr1P_ResolveProjectedOtIndex(block, projected, maxDepth, faceIndex, otIndexOverride);

	record = (struct DrawLevelOvr1PClipRecord *)cursor;
	record->header = DrawLevelOvr1P_GetRenderedClipRecordHeader(block, count);
	record->otEntry = (u32)(uintptr_t)&pb->ptrOT[otIndex];
	// NOTE(aalhendi): Retail terminal near writers 0x800a89dc/0x800aa5fc
	// store the freshly selected scratch UV metadata, not the caller texture.
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int DrawLevelOvr1P_WriteWaterRenderedClippedRecordAtOt(struct PushBuffer *pb, const struct QuadBlock *block,
                                                              const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count, int faceIndex,
                                                              int otIndexOverride)
{
	struct DrawLevelOvr1PClipRecord *record;
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);
	u32 maxDepth;
	int otIndex;

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	if (!DrawLevelOvr1P_HasClipRecordSpace(recordSize))
	{
		return 1;
	}

	maxDepth = DrawLevelOvr1P_GetPreparedProjectedMaxDepthCount(projected, indices, count);
	otIndex = DrawLevelOvr1P_ResolveProjectedOtIndex(block, projected, maxDepth, faceIndex, otIndexOverride);

	record = (struct DrawLevelOvr1PClipRecord *)cursor;
	// NOTE(aalhendi): Retail water direct helpers 0x800a34d4/0x800a3578 set
	// bit 31 on clipped-record headers so the consumer keeps the NCLIP result.
	record->header = count == 4 ? 0x80000001u : 0x80000000u;
	record->otEntry = (u32)(uintptr_t)&pb->ptrOT[otIndex];
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int Ovr226_800a34d4_WriteWaterRenderedClippedRecordAtOtEntry(struct PushBuffer *pb, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                    const int *indices, int count, uint32_t *otEntry)
{
	struct DrawLevelOvr1PClipRecord *record;
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

	(void)pb;

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	record = (struct DrawLevelOvr1PClipRecord *)cursor;
	// NOTE(aalhendi): Retail water-rendered clipped-record writers
	// 0x800a34d4/0x800a3578 store the inherited GP/OT pointer directly.
	record->header = count == 4 ? 0x80000001u : 0x80000000u;
	record->otEntry = (u32)(uintptr_t)otEntry;
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int DrawLevelOvr1P_ShouldEmitClipRecordNclip(s32 nclip, const struct DrawLevelOvr1PClipRecord *record)
{
	s32 header = (s32)record->header;

	if (nclip == 0)
	{
		return 0;
	}

	if (header < 0)
	{
		return 1;
	}

	return (s32)((u32)nclip ^ ((u32)header << 1)) > 0;
}

static u32 DrawLevelOvr1P_GetClipRecordColorCode(const struct DrawLevelOvr1PScratchVertex *projected, u32 code)
{
	return (DrawLevelOvr1P_ReadPackedWord(projected->color_hi) & 0x00ffffff) | (code << 24);
}

static u32 DrawLevelOvr1P_GetClipRecordSignedUvWord(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return (u32)(s32)(s16)projected->flags;
}

static void DrawLevelOvr1P_SetClipRecordPageScratch(const struct DrawLevelOvr1PClipRecord *record)
{
	DrawLevelOvr1P_Scratch()->uv.tpage = record->tpage;
	DrawLevelOvr1P_Scratch()->uv.clut = record->clut;
}

static struct DrawLevelOvr1PScratchVertex *DrawLevelOvr1P_GetClipRecordWorkspace(void)
{
	return CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET);
}

static u32 DrawLevelOvr1P_StoreClipRecordUvScratch(const struct DrawLevelOvr1PScratchVertex *projected, enum DrawLevelOvr1PUvScratchSlot slot)
{
	struct DrawLevelOvr1PUvScratch *scratch = &DrawLevelOvr1P_Scratch()->uv;

	switch (slot)
	{
	case DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_0:
		scratch->flag0 = (s16)projected->flags;
		return scratch->uv0;

	case DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_1:
		scratch->flag1 = (s16)projected->flags;
		return scratch->uv1;

	case DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_2:
	default:
		scratch->flag2 = (s16)projected->flags;
		return scratch->uv2;
	}
}

static void DrawLevelOvr1P_WriteClipRecordGT3(POLY_GT3 *poly, const struct DrawLevelOvr1PScratchVertex *emit, u32 code, u32 uv0, u32 uv1, u32 uv2)
{
	CtrGpu_WriteColorCode(&poly->r0, DrawLevelOvr1P_GetClipRecordColorCode(&emit[0], code));
	CtrGpu_WritePackedXY(&poly->x0, DrawLevelOvr1P_PackProjectedSxy(&emit[0]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x0, emit[0].posScreen, DrawLevelOvr1P_PackProjectedSxy(&emit[0]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u0, uv0);
	CtrGpu_WriteColorCode(&poly->r1, DrawLevelOvr1P_GetClipRecordColorCode(&emit[1], 0));
	CtrGpu_WritePackedXY(&poly->x1, DrawLevelOvr1P_PackProjectedSxy(&emit[1]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x1, emit[1].posScreen, DrawLevelOvr1P_PackProjectedSxy(&emit[1]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u1, uv1);
	CtrGpu_WriteColorCode(&poly->r2, DrawLevelOvr1P_GetClipRecordColorCode(&emit[2], 0));
	CtrGpu_WritePackedXY(&poly->x2, DrawLevelOvr1P_PackProjectedSxy(&emit[2]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x2, emit[2].posScreen, DrawLevelOvr1P_PackProjectedSxy(&emit[2]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u2, uv2);
}

static void DrawLevelOvr1P_WriteClipRecordGT4(POLY_GT4 *poly, const struct DrawLevelOvr1PScratchVertex *emit, u32 code, u32 uv0, u32 uv1, u32 uv2)
{
	DrawLevelOvr1P_WriteClipRecordGT3((POLY_GT3 *)poly, emit, code, uv0, uv1, uv2);
	CtrGpu_WriteColorCode(&poly->r3, DrawLevelOvr1P_GetClipRecordColorCode(&emit[3], 0));
	CtrGpu_WritePackedXY(&poly->x3, DrawLevelOvr1P_PackProjectedSxy(&emit[3]));
#if defined(CTR_NATIVE)
	NativeGTE_CopySXYStore(&poly->x3, emit[3].posScreen, DrawLevelOvr1P_PackProjectedSxy(&emit[3]));
#endif
	CtrGpu_WritePackedUVWord(&poly->u3, DrawLevelOvr1P_GetClipRecordSignedUvWord(&emit[3]));
}

static void DrawLevelOvr1P_SetClipRecordSourceDelta(struct DrawLevelOvr1PScratchVertex *projected, s16 delta)
{
	DrawLevelOvr1P_WritePackedHalf(&projected->clipNear, (u16)delta);
}

static s16 DrawLevelOvr1P_GetClipRecordSourceDelta(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return (s16)DrawLevelOvr1P_ReadPackedHalf(&projected->clipNear);
}

static void DrawLevelOvr1P_GetClipRecordSourceVector(const struct DrawLevelOvr1PScratchVertex *projected, SVECTOR *source)
{
	if (DrawLevelOvr1P_GetClipRecordSourceDelta(projected) < 0)
	{
		source->vx = projected->posScreen[0];
		source->vy = projected->posScreen[1];
		source->vz = (s16)projected->depth;
	}
	else
	{
		source->vx = projected->pos[0];
		source->vy = projected->pos[1];
		source->vz = projected->pos[2];
	}

	source->pad = 0;
}

static s32 DrawLevelOvr1P_ProjectClipRecordEmitVertices(struct DrawLevelOvr1PScratchVertex *dst, const struct DrawLevelOvr1PScratchVertex *projected,
                                                        const int *indices, int count)
{
	SVECTOR source[4];
	s32 nclip;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		dst[vertexIndex] = projected[indices[vertexIndex]];
		DrawLevelOvr1P_GetClipRecordSourceVector(&projected[indices[vertexIndex]], &source[vertexIndex]);
	}

	CTR_GteLoadSV3(&source[0], &source[1], &source[2]);
	gte_rtpt();
	CTR_GteStoreSXY3(&dst[0].posScreen[0], &dst[1].posScreen[0], &dst[2].posScreen[0]);
	gte_nclip();
	gte_stopz(&nclip);

	if (count == 4)
	{
		CTR_GteLoadSV0(&source[3]);
		gte_rtps();
		CTR_GteStoreSXY(&dst[3].posScreen[0]);
	}

	#if defined(CTR_NATIVE)
	return NativeGpu_CorrectSubpixelNclip(dst[0].posScreen, dst[1].posScreen, dst[2].posScreen, nclip);
#else
	return nclip;
#endif
}

static void DrawLevelOvr1P_ProjectClipRecordEmitVertex(struct DrawLevelOvr1PScratchVertex *dst, const struct DrawLevelOvr1PScratchVertex *projected)
{
	SVECTOR source;

	*dst = *projected;
	DrawLevelOvr1P_GetClipRecordSourceVector(projected, &source);

	CTR_GteLoadSV0(&source);
	gte_rtps();
	CTR_GteStoreSXY(&dst->posScreen[0]);
}

static int Ovr226_800aac00_EmitClipRecordGT3(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                             const struct DrawLevelOvr1PClipRecord *record);

static int Ovr226_800aad44_EmitClipRecordGT4(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                             const struct DrawLevelOvr1PClipRecord *record)
{
	struct DrawLevelOvr1PScratchVertex emit[4];
	int emitIndices[4] = {0, 1, 2, 3};
	int fallbackIndices[3] = {indices[1], indices[3], indices[2]};

	// NOTE(aalhendi): Retail 0x800aad44 projects the fourth vertex only after
	// the first triangle survives the NCLIP/header fold.
	s32 nclip = DrawLevelOvr1P_ProjectClipRecordEmitVertices(emit, projected, indices, 3);

	// NOTE(aalhendi): Retail GT4 helper 0x800aad44 falls back to the GT3 helper
	// with vertices 1/3/2 when the first triangle fails its NCLIP fold.
	if (!DrawLevelOvr1P_ShouldEmitClipRecordNclip(nclip, record))
	{
		return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, projected, fallbackIndices, record);
	}

	DrawLevelOvr1P_ProjectClipRecordEmitVertex(&emit[3], &projected[indices[3]]);

	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(pb, emit, emitIndices))
	{
		return 1;
	}

	// NOTE(aalhendi): Retail relies on the 0x800aa848 per-record 0xd68 prim reserve.
	POLY_GT4 *prim = primMem->cursor;
	POLY_GT4 *nextPrim = prim + 1;
	u32 uv0 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[0], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_0);
	u32 uv1 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[1], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_1);
	u32 uv2 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[2], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_2);
	u32 code = DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x3e, 0x3c);

	DrawLevelOvr1P_WriteClipRecordGT4(prim, emit, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 12, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int Ovr226_800aac00_EmitClipRecordGT3(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                             const struct DrawLevelOvr1PClipRecord *record)
{
	struct DrawLevelOvr1PScratchVertex emit[3];
	int emitIndices[3] = {0, 1, 2};

	s32 nclip = DrawLevelOvr1P_ProjectClipRecordEmitVertices(emit, projected, indices, 3);

	if (!DrawLevelOvr1P_ShouldEmitClipRecordNclip(nclip, record))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsProjectedTriOffscreen(pb, emit, emitIndices))
	{
		return 1;
	}

	// NOTE(aalhendi): Retail relies on the 0x800aa848 per-record 0xd68 prim reserve.
	POLY_GT3 *prim = primMem->cursor;
	POLY_GT3 *nextPrim = prim + 1;
	u32 uv0 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[0], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_0);
	u32 uv1 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[1], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_1);
	u32 uv2 = DrawLevelOvr1P_StoreClipRecordUvScratch(&emit[2], DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_2);
	u32 code = DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x36, 0x34);

	DrawLevelOvr1P_WriteClipRecordGT3(prim, emit, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 9, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static void DrawLevelOvr1P_StoreCurrentIrVector(s16 *out)
{
#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native reads PsyCross-backed GTE IR registers directly.
	out[0] = (s16)MFC2_S(9);
	out[1] = (s16)MFC2_S(10);
	out[2] = (s16)MFC2_S(11);
#else
	s32 ir0;
	s32 ir1;
	s32 ir2;

	__asm__ volatile("mfc2 %0,$9\n"
	                 "mfc2 %1,$10\n"
	                 "mfc2 %2,$11\n"
	                 : "=r"(ir0), "=r"(ir1), "=r"(ir2));

	out[0] = (s16)ir0;
	out[1] = (s16)ir1;
	out[2] = (s16)ir2;
#endif
}

static void Ovr226_800aaad0_PrepareClipRecordDepthScratch(struct DrawLevelOvr1PScratchVertex *projected, s32 threshold)
{
	// NOTE(aalhendi): Retail 0x800aaad0..0x800aab00 doubles the transformed
	// IR vector with MIPS `sll 1` wrap semantics and stores sourceZ-threshold.
	projected->posScreen[0] = DrawLevelOvr1P_ShiftLeft1S16Wrap(projected->pos[0]);
	projected->posScreen[1] = DrawLevelOvr1P_ShiftLeft1S16Wrap(projected->pos[1]);
	projected->depth = (u16)DrawLevelOvr1P_ShiftLeft1S16Wrap(projected->pos[2]);
	DrawLevelOvr1P_SetClipRecordSourceDelta(projected, (s16)((s32)projected->pos[2] - threshold));
}

static void Ovr226_800aa858_ProjectClipRecordRawVertex(struct DrawLevelOvr1PScratchVertex *projected, const struct DrawLevelOvr1PClipRecordVertex *src)
{
	s16 ir[3];

	projected->posVec = src->posVec;
	projected->flags = src->flags;
	DrawLevelOvr1P_CopyColorWord(projected->color_hi, src->color_hi);

	CTR_GteLoadS16TripletV0(&projected->pos[0]);
	// NOTE(aalhendi): Retail 0x800aa86c/0x800aa8b0/0x800aa8e8/0x800aaee4
	// uses LLV0BK for this source-vector transform.
	gte_llv0bk_b();
	DrawLevelOvr1P_StoreCurrentIrVector(ir);

	projected->pos[0] = ir[0];
	projected->pos[1] = ir[1];
	projected->pos[2] = ir[2];
}

static void DrawLevelOvr1P_PrepareClipRecordDepthScratchRange(struct DrawLevelOvr1PScratchVertex *projected, int count)
{
	s32 threshold = DrawLevelOvr1P_GetDepthClipThreshold();

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		Ovr226_800aaad0_PrepareClipRecordDepthScratch(&projected[vertexIndex], threshold);
	}
}

static u32 DrawLevelOvr1P_GetClipRecordProjectedNearMask(const struct DrawLevelOvr1PScratchVertex *projected, int count)
{
	static const u32 bits[4] = {0x4, 0x8, 0x10, 0x20};
	u32 mask = 0;

	for (s32 bitIndex = 0; bitIndex < count; bitIndex++)
	{
		if (DrawLevelOvr1P_IsClipByteSet(projected[bitIndex].color_hi[3]))
		{
			mask |= bits[bitIndex];
		}
	}

	return mask;
}

static void DrawLevelOvr1P_ClearClipRecordProjectedNearBytes(struct DrawLevelOvr1PScratchVertex *projected, int count)
{
	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		projected[vertexIndex].color_hi[3] = 0;
	}
}

static void Ovr226_800aab00_InterpolateClipRecordVertex(struct DrawLevelOvr1PScratchVertex *dst, const struct DrawLevelOvr1PScratchVertex *inside,
                                                        const struct DrawLevelOvr1PScratchVertex *outside)
{
	const u8 *insideUv = (const u8 *)&inside->flags;
	const u8 *outsideUv = (const u8 *)&outside->flags;
	u8 *dstUv = (u8 *)&dst->flags;
	s32 threshold = DrawLevelOvr1P_GetDepthClipThreshold();
	s32 denom = (s16)outside->depth - (s16)inside->depth;
	s32 factor = DrawLevelOvr1P_ShiftLeft16Wrap(threshold - (s32)(s16)inside->depth) / denom;

	// NOTE(aalhendi): Retail 0x800aab00..0x800aac00 writes only offsets
	// 0x00, 0x02, and bytes 0x06..0x0a. Offset 0x04 and source delta 0x12
	// are preseeded by 0x800aa7f8.
	dst->pos[0] = DrawLevelOvr1P_LerpS16_16(inside->posScreen[0], outside->posScreen[0], factor);
	dst->pos[1] = DrawLevelOvr1P_LerpS16_16(inside->posScreen[1], outside->posScreen[1], factor);
	dstUv[0] = DrawLevelOvr1P_LerpU8_16(insideUv[0], outsideUv[0], factor);
	dstUv[1] = DrawLevelOvr1P_LerpU8_16(insideUv[1], outsideUv[1], factor);

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dst->color_hi[colorChannel] = DrawLevelOvr1P_LerpU8_16(inside->color_hi[colorChannel], outside->color_hi[colorChannel], factor);
	}
}

static u32 DrawLevelOvr1P_GetClipRecordJumpAddress(int count, u32 nearMask)
{
	u32 tableOffset = count == 4 ? DRAW_LEVEL_OVR1P_GT4_CLIP_RECORD_JUMP_TABLE_OFFSET : DRAW_LEVEL_OVR1P_GT3_CLIP_RECORD_JUMP_TABLE_OFFSET;

	return *CTR_SCRATCHPAD_PTR(u32, tableOffset + (int)nearMask);
}

static int Ovr226_800aa96c_DispatchGT3ClipRecordLabel(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                      struct DrawLevelOvr1PScratchVertex *work, const struct DrawLevelOvr1PClipRecord *record,
                                                      u32 handlerAddress)
{
	int indices[4];

	// NOTE(aalhendi): Retail 0x800aa96c..0x800aaad0 jump labels branch back to
	// 0x800aa848 after advancing the clipped-record cursor by 0x30.
	//  Work records live at retail scratch 0x1b4/0x1c8/0x1dc/0x204/0x218.
	switch (handlerAddress)
	{
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_NONE:
	{
		int tri[3] = {0, 1, 2};
		return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, work, tri, record);
	}
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V0:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[0]);
		indices[0] = 1;
		indices[1] = 2;
		indices[2] = 4;
		indices[3] = 5;
		return Ovr226_800aad44_EmitClipRecordGT4(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V1:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[0], &work[1]);
		indices[0] = 2;
		indices[1] = 0;
		indices[2] = 4;
		indices[3] = 5;
		return Ovr226_800aad44_EmitClipRecordGT4(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V0_V1:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[0]);
		indices[0] = 2;
		indices[1] = 5;
		indices[2] = 4;
		return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 4;
		indices[3] = 5;
		return Ovr226_800aad44_EmitClipRecordGT4(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V0_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		indices[0] = 1;
		indices[1] = 5;
		indices[2] = 4;
		return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_V1_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[0], &work[1]);
		indices[0] = 0;
		indices[1] = 5;
		indices[2] = 4;
		return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, work, indices, record);
	case OVR226_RETAIL_LABEL_GT3_CLIP_NEAR_ALL:
		return 1;
	default:
		return 1;
	}
}

static int DrawLevelOvr1P_EmitClipRecordGT3Table(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const struct DrawLevelOvr1PClipRecord *record)
{
	struct DrawLevelOvr1PScratchVertex *work = DrawLevelOvr1P_GetClipRecordWorkspace();

	if (work != projected)
	{
		work[0] = projected[0];
		work[1] = projected[1];
		work[2] = projected[2];
	}

	// NOTE(aalhendi): Retail 0x800aa934..0x800aa968 reads the projected
	// scratch near bytes to select the GT3 table, then clears those bytes.
	u32 nearMask = DrawLevelOvr1P_GetClipRecordProjectedNearMask(work, 3);
	DrawLevelOvr1P_ClearClipRecordProjectedNearBytes(work, 3);
	u32 handlerAddress = DrawLevelOvr1P_GetClipRecordJumpAddress(3, nearMask);

	return Ovr226_800aa96c_DispatchGT3ClipRecordLabel(pb, primMem, otEntry, work, record, handlerAddress);
}

static int DrawLevelOvr1P_EmitClipRecordTableTri(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                 const struct DrawLevelOvr1PScratchVertex *work, const struct DrawLevelOvr1PClipRecord *record, int a, int b,
                                                 int c)
{
	int indices[3] = {a, b, c};

	return Ovr226_800aac00_EmitClipRecordGT3(pb, primMem, otEntry, work, indices, record);
}

static int DrawLevelOvr1P_EmitClipRecordTableQuad(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                  const struct DrawLevelOvr1PScratchVertex *work, const struct DrawLevelOvr1PClipRecord *record, int a, int b,
                                                  int c, int d)
{
	int indices[4] = {a, b, c, d};

	return Ovr226_800aad44_EmitClipRecordGT4(pb, primMem, otEntry, work, indices, record);
}

static int Ovr226_800aaf70_DispatchGT4ClipRecordLabel(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                      struct DrawLevelOvr1PScratchVertex *work, const struct DrawLevelOvr1PClipRecord *record,
                                                      u32 handlerAddress)
{
	// NOTE(aalhendi): Retail 0x800aaf70..0x800ab3d4 jump labels branch back to
	// 0x800aa848 after advancing the clipped-record cursor by 0x3c.
	//  Work records live at retail scratch 0x1b4..0x22c.
	switch (handlerAddress)
	{
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_NONE:
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 0, 1, 2, 3);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[0]);
		if (!DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 3, 2, 1))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 1, 2, 4, 5);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V1:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[3], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[0], &work[1]);
		if (!DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 3, 2, 4, 5))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 2, 0, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V1:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[3], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[2], &work[0]);
		if (!DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 3, 2, 4, 5))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 2, 6, 5);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[3], &work[2]);
		if (!DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 0, 1, 4, 5))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 1, 3, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[3], &work[2]);
		if (!DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 1, 5, 4))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 1, 3, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V1_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[0], &work[1]);
		if (!DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 0, 5, 4))
		{
			return 0;
		}
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[3], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[3], &work[1]);
		return DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 3, 5, 4);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V1_V2:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[3], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[3], &work[2]);
		return DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 3, 5, 4);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[2], &work[3]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[3]);
		if (!DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 0, 1, 2))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 2, 1, 4, 5);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[0]);
		if (!DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 1, 2, 4, 5))
		{
			return 0;
		}
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[3]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[3]);
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 1, 2, 4, 5);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V1_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[2], &work[3]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[0], &work[1]);
		if (!DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 2, 5, 4))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 2, 0, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V1_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[2], &work[3]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[2], &work[1]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[2], &work[0]);
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 4, 2, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V2_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[1], &work[3]);
		if (!DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 0, 1, 4, 5))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 1, 6, 5);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V0_V2_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[1], &work[0]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[1], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[6], &work[1], &work[3]);
		return DrawLevelOvr1P_EmitClipRecordTableQuad(pb, primMem, otEntry, work, record, 4, 1, 5, 6);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_V1_V2_V3:
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[4], &work[0], &work[2]);
		Ovr226_800aab00_InterpolateClipRecordVertex(&work[5], &work[0], &work[1]);
		return DrawLevelOvr1P_EmitClipRecordTableTri(pb, primMem, otEntry, work, record, 0, 5, 4);
	case OVR226_RETAIL_LABEL_GT4_CLIP_NEAR_ALL:
		return Ovr226_800ab3d4_EmptyGT4ClipRecordLabel();
	default:
		return 1;
	}
}

static int DrawLevelOvr1P_EmitClipRecordGT4Table(struct PushBuffer *pb, struct PrimMem *primMem, uint32_t *otEntry,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const struct DrawLevelOvr1PClipRecord *record)
{
	struct DrawLevelOvr1PScratchVertex *work = DrawLevelOvr1P_GetClipRecordWorkspace();

	if (work != projected)
	{
		work[0] = projected[0];
		work[1] = projected[1];
		work[2] = projected[2];
		work[3] = projected[3];
	}

	// NOTE(aalhendi): Retail 0x800aaf28..0x800aaf6c mirrors the GT3 path for
	// GT4 records, including the table-select clear of the source near bytes.
	u32 nearMask = DrawLevelOvr1P_GetClipRecordProjectedNearMask(work, 4);
	DrawLevelOvr1P_ClearClipRecordProjectedNearBytes(work, 4);
	u32 handlerAddress = DrawLevelOvr1P_GetClipRecordJumpAddress(4, nearMask);

	return Ovr226_800aaf70_DispatchGT4ClipRecordLabel(pb, primMem, otEntry, work, record, handlerAddress);
}

static int Ovr226_800aaed4_ProjectFourthClipRecordAndDispatchGT4(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                                 const struct DrawLevelOvr1PClipRecord *record)
{
	uint32_t *otEntry = (uint32_t *)(uintptr_t)record->otEntry;

	Ovr226_800aa858_ProjectClipRecordRawVertex(&projected[3], &record->vertex[3]);
	DrawLevelOvr1P_PrepareClipRecordDepthScratchRange(projected, 4);

	// NOTE(aalhendi): Retail 0x800aaf28..0x800aaf6c dispatches through scratch
	// 0x260; native keeps the handler bodies as C cases keyed by the copied
	// retail addresses.
	return DrawLevelOvr1P_EmitClipRecordGT4Table(pb, primMem, otEntry, projected, record);
}

static int DrawLevelOvr1P_HasClipRecordConsumerPrimReserve(const struct PrimMem *primMem);

static int Ovr226_800aa848_ProjectFirstThreeClipRecordsAndDispatch(struct PushBuffer *pb, struct PrimMem *primMem,
                                                                   const struct DrawLevelOvr1PClipRecord *record)
{
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetClipRecordWorkspace();
	u32 header = record->header;

	// NOTE(aalhendi): Retail 0x800aa848 preflights primMem->end against
	// curr+0xd68 before each clipped record.
	if (!DrawLevelOvr1P_HasClipRecordConsumerPrimReserve(primMem))
	{
		return 0;
	}

	DrawLevelOvr1P_SetClipRecordPageScratch(record);

	for (s32 vertexIndex = 0; vertexIndex < 3; vertexIndex++)
	{
		Ovr226_800aa858_ProjectClipRecordRawVertex(&projected[vertexIndex], &record->vertex[vertexIndex]);
	}

	// NOTE(aalhendi): Retail delay-slot stores the clipped-record header at
	// scratch 0x7c before selecting the GT3/GT4 consumer path.
	DrawLevelOvr1P_Scratch()->drawOrderOrHeader = header;

	if ((header & 1) != 0)
	{
		return Ovr226_800aaed4_ProjectFourthClipRecordAndDispatchGT4(pb, primMem, projected, record);
	}

	DrawLevelOvr1P_PrepareClipRecordDepthScratchRange(projected, 3);
	// NOTE(aalhendi): Retail 0x800aa934..0x800aa968 dispatches through scratch
	// 0x240; native keeps the handler bodies as C cases keyed by copied addresses.
	return DrawLevelOvr1P_EmitClipRecordGT3Table(pb, primMem, (uint32_t *)(uintptr_t)record->otEntry, projected, record);
}

static int Ovr226_800aa790_TerminalPreamble(struct PushBuffer *pb, const u8 *cursor, const u8 *end)
{
	s16 threshold = (s16)(((u32)pb->distanceToScreen_PREV >> 1) + 1);

	// NOTE(aalhendi): Retail saves the terminal call return address at scratch
	// 0x2a0 before the empty-queue branch. Native models the scratch side
	// effect with the retail return PC; the host ABI owns the real return.
	*DrawLevelOvr1P_TerminalReturnPcScratch() = DRAW_LEVEL_OVR_RETAIL_LABEL_TERMINAL_RETURN;

	if (cursor == end)
	{
		return 0;
	}

	for (s32 controlWordIndex = 0; controlWordIndex < 8; controlWordIndex++)
	{
		CTC2(DrawLevelOvr1P_ReadWord(&pb->matrix_ViewProj, controlWordIndex * 4), 8 + controlWordIndex);
	}

	CTC2((u32)(s32)pb->rect.w << 15, 24);
	CTC2((u32)(s32)pb->rect.h << 15, 25);
	CTC2((u32)pb->distanceToScreen_PREV, 26);

	// NOTE(aalhendi): Retail 0x800aa7f8 mirrors the clip threshold into the
	// temporary vertices used by the 0x800aab00 interpolation helper.
	for (s32 vertexIndex = 0; vertexIndex < 3; vertexIndex++)
	{
		struct DrawLevelOvr1PScratchVertex *vertex = DrawLevelOvr1P_TerminalClipVertex(vertexIndex);

		vertex->pos[2] = threshold;
		vertex->clipNear = 0;
	}
	DrawLevelOvr1P_Scratch()->clipWindowPacked = DrawLevelOvr1P_ReadWord(&pb->rect, 4);

	CTC2(0x1000, 0);
	CTC2(0, 1);
	CTC2(0x1000, 2);
	CTC2(0, 3);
	CTC2(0x1000, 4);
	CTC2(0, 5);
	CTC2(0, 6);
	CTC2(0, 7);

	return 1;
}

static int DrawLevelOvr1P_HasClipRecordConsumerPrimReserve(const struct PrimMem *primMem)
{
	u8 *curr = primMem->cursor;
	u8 *end = primMem->end;
	u32 need = DRAW_LEVEL_OVR1P_CLIP_RECORD_PRIM_RESERVE + sDrawLevelOvr1P_PrimReserveBias;
	int ok = curr <= end && (size_t)need <= (size_t)(end - curr);

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (!ok && NativePerf_IsStressTestMeasuring())
	{
		static u32 s_lastClipPrimCapacityLogFrame = 0xffffffffu;
		u32 frame = (u32)sdata->frameCounter;
		if (frame != s_lastClipPrimCapacityLogFrame)
		{
			size_t remaining = (curr <= end) ? (size_t)(end - curr) : 0;
			s_lastClipPrimCapacityLogFrame = frame;
			Platform_Log("[RENDER CAP] clip-consumer prim full frame=%u remaining=%u need=%u\n",
				frame, (unsigned)remaining, (unsigned)need);
		}
	}
#endif

	return ok;
}

static int DrawLevelOvr1P_HasBucketPrimReserve(const struct PrimMem *primMem, u32 reserve)
{
	u8 *curr = primMem->cursor;
	u8 *end = primMem->end;
	int ok = curr <= end && (size_t)(reserve + sDrawLevelOvr1P_PrimReserveBias) <= (size_t)(end - curr);

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (!ok && NativePerf_IsStressTestMeasuring())
	{
		static u32 s_lastPrimCapacityLogFrame = 0xffffffffu;
		u32 frame = (u32)sdata->frameCounter;
		if (frame != s_lastPrimCapacityLogFrame)
		{
			size_t remaining = (curr <= end) ? (size_t)(end - curr) : 0;
			s_lastPrimCapacityLogFrame = frame;
			Platform_Log("[RENDER CAP] prim full frame=%u remaining=%u need=%u bias=%u\n",
				frame, (unsigned)remaining, (unsigned)reserve, (unsigned)sDrawLevelOvr1P_PrimReserveBias);
		}
	}
#endif

	return ok;
}

static void DrawLevelOvr1P_SetPrimReserveBias(u32 bias)
{
	sDrawLevelOvr1P_PrimReserveBias = bias;
}

static void DrawLevelOvr1P_SetListHandlersSeedRenderedCursor(int enabled)
{
	sDrawLevelOvr1P_ListHandlersSeedRenderedCursor = enabled;
}

static int DrawLevelOvr1P_ConsumeClipRecords(struct PushBuffer *pb, struct PrimMem *primMem)
{
	u8 *start = DrawLevelOvr1P_GetClipRecordStart();
	u8 *cursor = start;
	u8 *end = DrawLevelOvr1P_GetClipRecordCursor();
	u8 *bufferEnd = DrawLevelOvr1P_GetClipRecordEnd();

	if (start == NULL || end == NULL || bufferEnd == NULL)
	{
		return 1;
	}

	if (end < start || end > bufferEnd)
	{
		DrawLevelOvr1P_SetClipRecordCursor(start);
		return 1;
	}

	if (!Ovr226_800aa790_TerminalPreamble(pb, cursor, end))
	{
		return 1;
	}

	while (cursor < end)
	{
		struct DrawLevelOvr1PClipRecord *record = (struct DrawLevelOvr1PClipRecord *)cursor;
		int count = (record->header & 1) != 0 ? 4 : 3;
		size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

		if (recordSize > (size_t)(end - cursor))
		{
			break;
		}

		if (!Ovr226_800aa848_ProjectFirstThreeClipRecordsAndDispatch(pb, primMem, record))
		{
			return 0;
		}

		cursor += recordSize;
	}

	DrawLevelOvr1P_SetClipRecordCursor(start);
	return 1;
}

static int DrawLevelOvr1P_SelectDirectBit(s32 nclipResult, u32 tableWord, u32 drawOrderLow, int negateResult, u32 directBit)
{
	u32 resultBits;

	if (nclipResult == 0)
	{
		resultBits = 0;
	}
	else if (negateResult)
	{
		resultBits = (0u - (u32)nclipResult) ^ tableWord;
	}
	else
	{
		resultBits = (u32)nclipResult ^ tableWord;
	}

	if ((s32)(resultBits | drawOrderLow) < 0)
	{
		return directBit;
	}

	return 0;
}

static u32 DrawLevelOvr1P_SelectDirectMask(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                           u32 tableWord, u32 allowedMask)
{
	int primaryIndices[3] = {indices[0], indices[1], indices[2]};
	int secondaryNclipIndices[3] = {indices[3], indices[1], indices[2]};
	u32 drawOrderLow = DrawLevelOvr1P_GetActiveDrawOrderLow();
	u32 directMask;

	// NOTE(aalhendi): Retail derives direct offsets 4/8 from the GTE NCLIP
	// sign folded with scratch 0x7c and the selected scratch table word.
	(void)block;
	directMask = DrawLevelOvr1P_SelectDirectBit(DrawLevelOvr1P_NclipProjected(projected, primaryIndices), tableWord, drawOrderLow, 1,
	                                            DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY);
	directMask |= DrawLevelOvr1P_SelectDirectBit(DrawLevelOvr1P_NclipProjected(projected, secondaryNclipIndices), tableWord, drawOrderLow, 0,
	                                             DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY);

	return directMask & allowedMask;
}

static u32 DrawLevelOvr1P_SelectAndStoreDirectMask(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                   u32 tableWord, u32 allowedMask)
{
	u32 directMask = DrawLevelOvr1P_SelectDirectMask(block, projected, indices, tableWord, allowedMask);

	DrawLevelOvr1P_Scratch()->directMask = directMask;
	return directMask;
}

static int Ovr226_800a18c0_FullDynamicRecursiveGate(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 allowedMask,
                                                    struct DrawLevelOvr1PFullDynamicRecursiveGate *gate)
{
	u32 tableWord = DrawLevelOvr1P_Scratch()->selected4x1TableWord;

	gate->directMask = 0;
	gate->forceDirect = 0;
	DrawLevelOvr1P_Scratch()->directMask = allowedMask;

	if (DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(projected, indices, 4))
	{
		return 0;
	}

	gate->directMask = DrawLevelOvr1P_SelectDirectMask(NULL, projected, indices, tableWord, allowedMask);
	DrawLevelOvr1P_Scratch()->directMask = gate->directMask;
	if (gate->directMask == 0)
	{
		return 0;
	}

	gate->forceDirect = projected == CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET);

	return 1;
}

static u32 DrawLevelOvr1P_GetDirectHandlerAddress(u32 directMask)
{
	if (directMask == 0)
	{
		return 0;
	}

	return *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_HANDLER_TABLE_OFFSET + (int)directMask);
}

static int DrawLevelOvr1P_EmitPreparedProjectedTriDirectCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                 int otIndexOverride, int primCodeOverride);
static int DrawLevelOvr1P_EmitSignedClipProjectedTriDirectAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                               int otIndexOverride, int primCodeOverride);
static int DrawLevelOvr1P_EmitNonzeroClipProjectedQuadDirectAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                 int otIndexOverride, int primCodeOverride);
static int DrawLevelOvr1P_EmitPreparedProjectedQuadDirectCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                  const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                  const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                  int otIndexOverride, int primCodeOverride);
static int DrawLevelOvr1P_EmitPreparedProjectedDirectMaskRawAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, u32 directMask, int otIndexOverride);
static int DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                              const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                              const struct TextureLayout *texture, u32 directMask, int writeClipBytes, int waterRenderedDirect,
                                                              int otIndexOverride);

static int DrawLevelOvr1P_IsRawListDirectHandlerAddress(u32 handlerAddress)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT4:
		return 1;
	default:
		return 0;
	}
}

static int DrawLevelOvr1P_IsSignedClipTerminalTriHandlerAddress(u32 handlerAddress)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_SECOND:
		return 1;
	default:
		return 0;
	}
}

static int DrawLevelOvr1P_IsNonzeroClipTerminalQuadHandlerAddress(u32 handlerAddress)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT4:
		return 1;
	default:
		return 0;
	}
}

static int DrawLevelOvr1P_IsTargetGridTerminalHandlerAddress(u32 handlerAddress)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT4:
		return 1;
	default:
		return 0;
	}
}

static int DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                              const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                              const struct TextureLayout *texture, u32 directMask, int writeClipBytes, int waterRenderedDirect,
                                                              int otIndexOverride)
{
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress == 0)
	{
		return 1;
	}

	// NOTE(aalhendi): Retail list-bucket direct handlers outside the target
	// grid terminal family are raw packet writers after the caller's
	// cull/direct-mask checks. The target family keeps local terminal clip
	// checks even though list-projected clip bytes normally make them inert.
	if (writeClipBytes == DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST && !waterRenderedDirect && DrawLevelOvr1P_IsRawListDirectHandlerAddress(handlerAddress) &&
	    !DrawLevelOvr1P_IsTargetGridTerminalHandlerAddress(handlerAddress))
	{
		return DrawLevelOvr1P_EmitPreparedProjectedDirectMaskRawAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, otIndexOverride);
	}

	// NOTE(aalhendi): Retail direct table offsets 4/8/12 map to GT3 primary,
	// GT3 secondary, and GT4 handlers through the copied setup1 scratch slot.
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_PRIMARY:
	{
		int triIndices[3] = {indices[0], indices[1], indices[2]};
		int primCodeOverride = (handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226 ||
		                        handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_226)
		                           ? 0x36
		                           : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO;

		if (DrawLevelOvr1P_IsSignedClipTerminalTriHandlerAddress(handlerAddress))
		{
			return DrawLevelOvr1P_EmitSignedClipProjectedTriDirectAtOt(pb, primMem, block, projected, triIndices, faceIndex, texture, writeClipBytes,
			                                                           waterRenderedDirect, otIndexOverride, primCodeOverride);
		}

		return DrawLevelOvr1P_EmitPreparedProjectedTriDirectCodeAtOt(pb, primMem, block, projected, triIndices, faceIndex, texture, writeClipBytes,
		                                                             waterRenderedDirect, otIndexOverride, primCodeOverride);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_SECOND:
	{
		int triIndices[3] = {indices[1], indices[3], indices[2]};
		int primCodeOverride = (handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226 ||
		                        handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_226)
		                           ? 0x36
		                           : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO;

		if (DrawLevelOvr1P_IsSignedClipTerminalTriHandlerAddress(handlerAddress))
		{
			return DrawLevelOvr1P_EmitSignedClipProjectedTriDirectAtOt(pb, primMem, block, projected, triIndices, faceIndex, texture, writeClipBytes,
			                                                           waterRenderedDirect, otIndexOverride, primCodeOverride);
		}

		return DrawLevelOvr1P_EmitPreparedProjectedTriDirectCodeAtOt(pb, primMem, block, projected, triIndices, faceIndex, texture, writeClipBytes,
		                                                             waterRenderedDirect, otIndexOverride, primCodeOverride);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT4:
	{
		int primCodeOverride = (handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226 ||
		                        handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_226)
		                           ? 0x3e
		                           : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO;

		if (DrawLevelOvr1P_IsNonzeroClipTerminalQuadHandlerAddress(handlerAddress))
		{
			return DrawLevelOvr1P_EmitNonzeroClipProjectedQuadDirectAtOt(pb, primMem, block, projected, indices, faceIndex, texture, writeClipBytes,
			                                                             waterRenderedDirect, otIndexOverride, primCodeOverride);
		}

		return DrawLevelOvr1P_EmitPreparedProjectedQuadDirectCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture, writeClipBytes,
		                                                              waterRenderedDirect, otIndexOverride, primCodeOverride);
	}

	default:
		return 1;
	}
}

static int DrawLevelOvr1P_EmitPreparedProjectedDirectMaskRawAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, u32 directMask, int otIndexOverride)
{
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress == 0)
	{
		return 1;
	}

	// NOTE(aalhendi): List direct helpers trust the caller's
	// offscreen/direct/near decisions and write raw packets.
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_PRIMARY:
	{
		int triIndices[3] = {indices[0], indices[1], indices[2]};

		return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(
		    pb, primMem, block, projected, triIndices, faceIndex, texture, DrawLevelOvr1P_GetProjectedTriMaxDepth(projected, triIndices), otIndexOverride,
		    handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226 ? 0x36 : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_SECOND:
	{
		int triIndices[3] = {indices[1], indices[3], indices[2]};

		return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(
		    pb, primMem, block, projected, triIndices, faceIndex, texture, DrawLevelOvr1P_GetProjectedTriMaxDepth(projected, triIndices), otIndexOverride,
		    handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226 ? 0x36 : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT4:
		return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOt(
		    pb, primMem, block, projected, indices, faceIndex, texture, DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices), otIndexOverride,
		    handlerAddress == DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226 ? 0x3e : DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);

	default:
		return DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask,
		                                                          DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST, 0, otIndexOverride);
	}
}

static int DrawLevelOvr1P_EmitPreparedProjectedTriDirectCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                 int otIndexOverride, int primCodeOverride)
{
	(void)writeClipBytes;
	(void)waterRenderedDirect;

	// NOTE(aalhendi): Retail GT3 direct helpers use `bltz` on clip bytes that
	// the rendered/list projection paths store as 0/1, so GT3 stays direct here.
	// Terminal direct handlers write raw packets after the caller-side cull.

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture,
	                                                          DrawLevelOvr1P_GetProjectedTriMaxDepth(projected, indices), otIndexOverride, primCodeOverride);
}

static int DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	int value = (s8)projected[indices[0]].clipHalfNear;

	for (s32 vertexIndex = 1; vertexIndex < count; vertexIndex++)
	{
		value &= (s8)projected[indices[vertexIndex]].clipHalfNear;
	}

	return value < 0;
}

static int DrawLevelOvr1P_HasProjectedVertexNearSigned(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count)
{
	int value = 0;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		value |= (s8)projected[indices[vertexIndex]].clipNear;
	}

	return value < 0;
}

static int DrawLevelOvr1P_EmitSignedClipProjectedTriDirectAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                               int otIndexOverride, int primCodeOverride)
{
	(void)writeClipBytes;

	// NOTE(aalhendi): Retail rendered/water GT3 terminals use signed `bltz`
	// gates; current projection bytes are 0/1, so these are normally inert.
	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, indices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, indices, 3))
	{
		if (waterRenderedDirect)
		{
			return DrawLevelOvr1P_WriteWaterRenderedClippedRecordAtOt(pb, block, projected, indices, 3, faceIndex, otIndexOverride);
		}

		return DrawLevelOvr1P_WriteRenderedClippedRecordAtOt(pb, block, projected, indices, 3, faceIndex, texture, otIndexOverride);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture,
	                                                          DrawLevelOvr1P_GetProjectedTriMaxDepth(projected, indices), otIndexOverride, primCodeOverride);
}

static int DrawLevelOvr1P_EmitNonzeroClipProjectedQuadDirectAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                 const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                 int otIndexOverride, int primCodeOverride)
{
	(void)writeClipBytes;

	// NOTE(aalhendi): Retail rendered/water GT4 terminals use `bnez` gates at
	// the terminal labels before raw packet or clipped-record emission.
	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		if (waterRenderedDirect)
		{
			return DrawLevelOvr1P_WriteWaterRenderedClippedRecordAtOt(pb, block, projected, indices, 4, faceIndex, otIndexOverride);
		}

		return DrawLevelOvr1P_WriteRenderedClippedRecordAtOt(pb, block, projected, indices, 4, faceIndex, texture, otIndexOverride);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture,
	                                                           DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices), otIndexOverride, primCodeOverride);
}

static int DrawLevelOvr1P_EmitPreparedProjectedQuadDirectCodeAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                  const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                  const struct TextureLayout *texture, int writeClipBytes, int waterRenderedDirect,
                                                                  int otIndexOverride, int primCodeOverride)
{
	// NOTE(aalhendi): Retail terminal GT4 helpers consume the current scratch
	// 0x80 clipped-record header; selector/rendered owners refresh it before
	// dispatch, while copied recursive helpers inherit it unchanged.
	if (writeClipBytes)
	{
		if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
		{
			return 1;
		}

		if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
		{
			if (waterRenderedDirect)
			{
				return DrawLevelOvr1P_WriteWaterRenderedClippedRecordAtOt(pb, block, projected, indices, 4, faceIndex, otIndexOverride);
			}

			return DrawLevelOvr1P_WriteRenderedClippedRecordAtOt(pb, block, projected, indices, 4, faceIndex, texture, otIndexOverride);
		}
	}

	// NOTE(aalhendi): Retail GT4 terminal direct handlers have no second
	// screen-space cull after the caller-side direct-mask reduction.
	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture,
	                                                           DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices), otIndexOverride, primCodeOverride);
}

static u32 DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(u32 nearMask, int writeClipBytes)
{
	u32 tableIndex = (nearMask >> 2) - 1;

	if (tableIndex >= 15)
	{
		return writeClipBytes ? DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_HELPER_DEFAULT : DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_HELPER_DEFAULT;
	}

	return *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_NEAR_SUBDIVISION_HANDLER_TABLE_OFFSET + (int)(tableIndex * sizeof(u32)));
}

static int DrawLevelOvr1P_GetNearSubdivisionTableSlot(u32 nearMask)
{
	u32 tableIndex = (nearMask >> 2) - 1;

	if (tableIndex >= 15)
	{
		return 5;
	}

	return (int)tableIndex;
}

static void DrawLevelOvr1P_SetPreviousRecursiveHandler(u32 handlerAddress)
{
	// NOTE(aalhendi): Retail stores the active recursive handler immediately
	// before jumping through the copied setup0 scratch table.
	DrawLevelOvr1P_Scratch()->previousDirectHandlerAddress = handlerAddress;
}

static struct DrawLevelOvr1PScratchVertex *DrawLevelOvr1P_GetSubdivisionFrame(int depth)
{
	return CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex,
	                          DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + ((depth + 1) * DRAW_LEVEL_OVR1P_RECURSION_FRAME_SIZE));
}

static void DrawLevelOvr1P_BuildGridSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                     const int *indices, int writeClipBytes);
static void DrawLevelOvr1P_BuildGridSubdivisionFrame4x4(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                        const int *indices, int writeClipBytes);

static int DrawLevelOvr1P_EmitDeepestProjectedDirectStoredMaskAtOt(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                   const struct TextureLayout *texture, u32 directMask, int writeClipBytes, int otIndexOverride)
{
	DrawLevelOvr1P_Scratch()->directMask = directMask;
	u32 directHandlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);
	if (directHandlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, directHandlerAddress);
	}

	int result = DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, writeClipBytes, 0,
	                                                                otIndexOverride);

	// NOTE(aalhendi): Retail deepest-frame dispatch restores saved UV scratch
	// after jumping through the current direct table mask.
	DrawLevelOvr1P_RestoreProjectedUvScratch();

	return result;
}

static int DrawLevelOvr1P_DispatchCopiedGridNear(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, u32 nearMask, int depth, int writeClipBytes, u32 allowedMask,
                                                 int inheritedOtIndex);

static int DrawLevelOvr1P_DispatchCopiedGridRenderedNearBranch(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, int depth, int writeClipBytes, int inheritedOtIndex)
{
	const u32 directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;

	DrawLevelOvr1P_Scratch()->directMask = directMask;

	// NOTE(aalhendi): Retail 0x800a8360/0x800a9f80 enters the deepest
	// handler from frame 0x324 before selecting another near child.
	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return DrawLevelOvr1P_EmitDeepestProjectedDirectStoredMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, writeClipBytes,
		                                                               inheritedOtIndex);
	}

	u32 nearMask = DrawLevelOvr1P_GetProjectedCopiedGridNearMask(projected, indices);
	if (nearMask != 0)
	{
		return DrawLevelOvr1P_DispatchCopiedGridNear(pb, primMem, block, projected, indices, faceIndex, texture, nearMask, depth, writeClipBytes, directMask,
		                                             inheritedOtIndex);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, writeClipBytes, 0,
	                                                          inheritedOtIndex);
}

static int DrawLevelOvr1P_CopiedGridDirectPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, int writeClipBytes, u32 allowedMask, int inheritedOtIndex)
{
	u32 directMask;

	// NOTE(aalhendi): Retail 0x800a82a0/0x800a9ec0 stores caller t2 before
	// packed-SXY/offscreen and NCLIP reduce it to the surviving direct mask.
	// List/default 0x800a7588/0x800a91a8 use this same branch shape.
	DrawLevelOvr1P_Scratch()->directMask = allowedMask;

	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(pb, projected, indices))
	{
		return 1;
	}

	// NOTE(aalhendi): Retail projected-grid helpers reduce scratch 0x70 before
	// near recursion, so child dispatch only sees the surviving direct cases.
	directMask = DrawLevelOvr1P_SelectAndStoreDirectMask(block, projected, indices, DrawLevelOvr1P_Scratch()->selected4x1TableWord, allowedMask);
	if (directMask == 0)
	{
		return 1;
	}

	// NOTE(aalhendi): The shared retail helper uses the deepest-frame direct
	// path for frame 0x324 even when no further near child is selected.
	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return DrawLevelOvr1P_EmitDeepestProjectedDirectStoredMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, writeClipBytes,
		                                                               inheritedOtIndex);
	}

	u32 nearMask = DrawLevelOvr1P_GetProjectedCopiedGridNearMask(projected, indices);
	if (nearMask != 0)
	{
		return DrawLevelOvr1P_DispatchCopiedGridNear(pb, primMem, block, projected, indices, faceIndex, texture, nearMask, depth, writeClipBytes, directMask,
		                                             inheritedOtIndex);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedDirectMaskAtOt(pb, primMem, block, projected, indices, faceIndex, texture, directMask, writeClipBytes, 0,
	                                                          inheritedOtIndex);
}

static int DrawLevelOvr1P_DispatchCopiedGridFace(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, int depth, int writeClipBytes, u32 allowedMask, int inheritedOtIndex)
{
	// NOTE(aalhendi): The branch-level retail split is modeled here:
	// 0x800a7588/0x800a91a8 skip clip bytes, while 0x800a825c/0x800a9e7c
	// test bytes 0x13/0x12 before the shared direct/near path.
	// Refreshed objdump 2026-06-01 checked the deepest branch handoff and the
	// non-deepest near continuations at 0x800a78a8/0x800a861c/0x800a94c8/0x800aa23c.
	// NOTE(aalhendi): Copied recursive/default grid helpers preserve inherited
	// UV/texture state. Texture-slot selection belongs to the selector entries
	// before this path, such as retail 0x800a7668, 0x800a8380, and 0x800a9fa0.
	if (writeClipBytes && DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	// NOTE(aalhendi): Rendered helpers branch on clip bytes before direct-mask
	// reduction. The clipped-record header scratch word is inherited here.
	if (writeClipBytes && DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return DrawLevelOvr1P_DispatchCopiedGridRenderedNearBranch(pb, primMem, block, projected, indices, faceIndex, texture, depth, writeClipBytes,
		                                                           inheritedOtIndex);
	}

	return DrawLevelOvr1P_CopiedGridDirectPreflight(pb, primMem, block, projected, indices, faceIndex, texture, depth, writeClipBytes, allowedMask,
	                                                inheritedOtIndex);
}

static int DrawLevelOvr1P_EmitProjectedGridFace(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                const struct TextureLayout *texture, int depth, int writeClipBytes, u32 allowedMask, int inheritedOtIndex)
{
	return DrawLevelOvr1P_DispatchCopiedGridFace(pb, primMem, block, projected, indices, faceIndex, texture, depth, writeClipBytes, allowedMask,
	                                             inheritedOtIndex);
}

static int DrawLevelOvr1P_IsDeepestSubdivisionFrame(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return projected == CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET);
}

static int DrawLevelOvr1P_HandlerUsesDeepestCompactGrid(u32 handlerAddress)
{
	// NOTE(aalhendi): Only the 0x60 generic-grid helper families branch from
	// scratch frame 0x324 back to the compact topology labels.
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_SLOT11:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_SLOT11:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_HELPER_DEFAULT:
		return 1;

	default:
		return 0;
	}
}

static int DrawLevelOvr1P_HandlerUses4x4GridFrame(u32 handlerAddress)
{
	// NOTE(aalhendi): Dynamic-rendered 0x800a7fac..0x800a810c keeps the
	// normal 3x3 builder despite wide slot strides.
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_HELPER_SLOT11:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_DEFAULT:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_HELPER_SLOT11:
		return 1;

	default:
		return 0;
	}
}

static const struct DrawLevelOvr1PNearSubdivisionCase *DrawLevelOvr1P_GetDeepestGridCompactCase(int slot)
{
	switch (slot)
	{
	case 0:
	case 1:
	case 2:
		return &sDrawLevelOvr1PDeepestGridSubdivisionCases[0];

	case 3:
	case 7:
	case 11:
		return &sDrawLevelOvr1PDeepestGridSubdivisionCases[1];

	default:
		return &sDrawLevelOvr1PDeepestGridSubdivisionCases[2];
	}
}

static int DrawLevelOvr1P_DispatchProjectedGridDeepestCompact(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                              const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int depth,
                                                              const struct TextureLayout *texture, int writeClipBytes, u32 allowedMask, int slot,
                                                              int inheritedOtIndex)
{
	const struct DrawLevelOvr1PNearSubdivisionCase *subdivisionCase = DrawLevelOvr1P_GetDeepestGridCompactCase(slot);

	(void)allowedMask;

	for (s32 subdivisionIndex = 0; subdivisionIndex < 2; subdivisionIndex++)
	{
		int subIndices[4];

		for (s32 vertexIndex = 0; vertexIndex < 4; vertexIndex++)
		{
			subIndices[vertexIndex] = subdivisionCase->subIndices[subdivisionIndex][vertexIndex];
		}

		// NOTE(aalhendi): Retail deepest generic-grid handlers branch back to
		// the compact topology labels, including the special +0xb4 writes.
		if (subdivisionCase->slotWords[subdivisionIndex] != DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE)
		{
			DrawLevelOvr1P_SetGridFaceSlotWord(projected, subdivisionCase->slotWords[subdivisionIndex]);
		}

		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, subIndices, faceIndex, texture, depth, writeClipBytes,
		                                          subdivisionCase->directMasks[subdivisionIndex], inheritedOtIndex))
		{
			return 0;
		}
	}

	return 1;
}

static int DrawLevelOvr1P_DispatchProjectedGridHelper(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int depth,
                                                      const struct TextureLayout *texture, u32 handlerAddress, int handlerSlot, int writeClipBytes,
                                                      u32 allowedMask, int inheritedOtIndex)
{
	int slot = handlerSlot;
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	if (DrawLevelOvr1P_HandlerUsesDeepestCompactGrid(handlerAddress) && DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return DrawLevelOvr1P_DispatchProjectedGridDeepestCompact(pb, primMem, block, projected, faceIndex, depth, texture, writeClipBytes, allowedMask, slot,
		                                                          inheritedOtIndex);
	}

	// NOTE(aalhendi): Non-4x1 ground helper tables share the 3x3 subdivision
	// topology used by water-rendered, but emit through ground direct handlers.
	// Retail child labels pass their own t2 immediates into the shared helper;
	// the parent's reduced scratch 0x70 is not intersected into those labels.
	switch (slot)
	{
	case 0:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	case 1:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                            writeClipBytes, allowedQuad, inheritedOtIndex);
	case 2:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
		                                          allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	case 3:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                            writeClipBytes, allowedQuad, inheritedOtIndex);
	case 4:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
		                                          allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	case 7:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	case 9:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
		                                          allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	case 11:
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                          writeClipBytes, allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
		                                          allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
		                                          allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, writeClipBytes,
		                                            allowedQuad, inheritedOtIndex);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, DrawLevelOvr1P_GetDefaultGridFaceSlotWord(handlerAddress, 0));
	if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, writeClipBytes,
	                                          allowedQuad, inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, DrawLevelOvr1P_GetDefaultGridFaceSlotWord(handlerAddress, 1));
	if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, writeClipBytes,
	                                          allowedQuad, inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, DrawLevelOvr1P_GetDefaultGridFaceSlotWord(handlerAddress, 2));
	if (!DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, writeClipBytes,
	                                          allowedQuad, inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, DrawLevelOvr1P_GetDefaultGridFaceSlotWord(handlerAddress, 3));
	return DrawLevelOvr1P_EmitProjectedGridFace(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, writeClipBytes,
	                                            allowedQuad, inheritedOtIndex);
}

static int DrawLevelOvr1P_DispatchCopiedGridNear(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, u32 nearMask, int depth, int writeClipBytes, u32 allowedMask,
                                                 int inheritedOtIndex)
{
	// NOTE(aalhendi): Retail reaches these continuations only after the caller's
	// deepest-frame branch has failed, then builds the next frame and jumps
	// through the copied near-handler table at scratch 0x148 + nearMask.
	u32 handlerAddress = DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, writeClipBytes);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	if (DrawLevelOvr1P_HandlerUses4x4GridFrame(handlerAddress))
	{
		DrawLevelOvr1P_BuildGridSubdivisionFrame4x4(sub, projected, indices, writeClipBytes);
	}
	else
	{
		DrawLevelOvr1P_BuildGridSubdivisionFrame(sub, projected, indices, writeClipBytes);
	}

	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);
	return DrawLevelOvr1P_DispatchProjectedGridHelper(pb, primMem, block, sub, faceIndex, depth + 1, texture, handlerAddress, handlerSlot, writeClipBytes,
	                                                  allowedMask, inheritedOtIndex);
}

static int Ovr226_800a3eb0_Ground4x1NearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex);
static int Ovr226_800a3f74_DispatchGround4x1Deepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int inheritedOtIndex);
static int Ovr226_800a402c_DispatchGround4x1DirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int inheritedOtIndex);
static int Ovr226_800a46d0_Ground4x1RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry);
static int Ovr226_800a44e0_DispatchGround4x1RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                   const struct TextureLayout *texture, int depth, u32 handlerAddress,
                                                                   uint32_t *inheritedOtEntry);
static int Ovr226_800a4ad0_Ground4x1RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset,
                                                         uint32_t *inheritedOtEntry);
static int Ovr226_800a4b54_DispatchGround4x1RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                            const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                            const struct TextureLayout *texture, uint32_t *inheritedOtEntry);
static int Ovr226_800a4c0c_DispatchGround4x1RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, uint32_t *inheritedOtEntry);

static int Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 allowedMask,
                                                             u32 *directMask)
{
	int primaryIndices[3] = {indices[0], indices[1], indices[2]};
	int secondaryIndices[3] = {indices[3], indices[1], indices[2]};
	u32 tableWord = DrawLevelOvr1P_Scratch()->selected4x1TableWord;
	u32 drawOrderLow = DrawLevelOvr1P_GetActiveDrawOrderLow();

	// NOTE(aalhendi): Retail 0x800a3b90 leaves the caller mask in scratch 0x70
	// when the packed-SXY reject exits before final direct-mask storage.
	DrawLevelOvr1P_Scratch()->directMask = allowedMask;

	s32 primaryNclip = DrawLevelOvr1P_NclipProjected(projected, primaryIndices);
	s32 secondaryNclip = DrawLevelOvr1P_NclipProjected(projected, secondaryIndices);

	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(NULL, projected, indices))
	{
		return 0;
	}

	*directMask = DrawLevelOvr1P_SelectDirectBit(primaryNclip, tableWord, drawOrderLow, 1, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY);
	*directMask |= DrawLevelOvr1P_SelectDirectBit(secondaryNclip, tableWord, drawOrderLow, 0, DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY);
	*directMask &= allowedMask;
	DrawLevelOvr1P_Scratch()->directMask = *directMask;

	return *directMask != 0;
}

static int Ovr226_800a3b90_Ground4x1DirectPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, int inheritedOtIndex)
{
	u32 directMask = 0;

	if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a3f74_DispatchGround4x1Deepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	return Ovr226_800a3eb0_Ground4x1NearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtIndex);
}

static int Ovr226_800a39c4_DispatchGround4x1HelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                           int depth, u32 handlerAddress, int inheritedOtIndex)
{
	switch (handlerAddress)
	{
	case OVR226_RETAIL_LABEL_GROUND_4X1_LIST_SUBDIV_A:
	{
		const int firstIndices[4] = {0, 4, 2, 3};
		const int secondIndices[4] = {1, 3, 4, 2};

		if (!Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                              inheritedOtIndex))
		{
			return 0;
		}

		return Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth,
		                                                DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY, inheritedOtIndex);
	}

	case OVR226_RETAIL_LABEL_GROUND_4X1_LIST_SUBDIV_B:
	{
		const int firstIndices[4] = {1, 8, 0, 2};
		const int secondIndices[4] = {0, 1, 8, 3};

		if (!Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                              inheritedOtIndex))
		{
			return 0;
		}

		return Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth,
		                                                DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtIndex);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_HELPER_DEFAULT:
	{
		const int firstIndices[4] = {0, 4, 2, 8};
		const int secondIndices[4] = {4, 1, 8, 3};

		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
		if (!Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                              inheritedOtIndex))
		{
			return 0;
		}

		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
		return Ovr226_800a3b90_Ground4x1DirectPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                inheritedOtIndex);
	}

	default:
		return 1;
	}
}

static int Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 tableWord,
                                                                     u32 *directMask)
{
	int primaryIndices[3] = {indices[0], indices[1], indices[2]};
	int secondaryIndices[3] = {indices[3], indices[1], indices[2]};
	u32 drawOrderLow = DrawLevelOvr1P_GetActiveDrawOrderLow();

	s32 primaryNclip = DrawLevelOvr1P_NclipProjected(projected, primaryIndices);
	s32 secondaryNclip = DrawLevelOvr1P_NclipProjected(projected, secondaryIndices);

	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(NULL, projected, indices))
	{
		return 0;
	}

	*directMask = DrawLevelOvr1P_SelectDirectBit(primaryNclip, tableWord, drawOrderLow, 1, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY);
	*directMask |= DrawLevelOvr1P_SelectDirectBit(secondaryNclip, tableWord, drawOrderLow, 0, DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY);
	DrawLevelOvr1P_Scratch()->directMask = *directMask;

	return *directMask != 0;
}

static int Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth,
                                                           int faceIndex)
{
	u32 slotWord = DrawLevelOvr1P_GetProjectedOtSlotWord(projected, faceIndex);
	s8 drawOrder = DrawLevelOvr1P_ReadRetailQuadBlockByte(block, 0x18 + (slotWord >> 2));
	s32 otIndex = (s32)(maxDepth >> 6) + drawOrder;

	// NOTE(aalhendi): Retail 0x800a3df4 clamps only negative GP/OT offsets
	// back to the OT base. There is no upper clamp in this selector span.
	return otIndex < 0 ? 0 : otIndex;
}

static struct TextureLayout *Ovr226_800a3e00_SelectGround4x1SelectorTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                            u32 maxDepth)
{
	struct TextureLayout *texture = DrawLevelOvr1P_ResolveProjectedMidTexture(block, projected);

	if (texture == NULL)
	{
		return NULL;
	}

	u32 mosaicWord = DrawLevelOvr1P_ReadPackedWord((const u8 *)texture + 0x24);
	DrawLevelOvr1P_Scratch()->mosaicTextureWord = mosaicWord;

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold0)
	{
		texture++;
	}

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold1)
	{
		texture++;
	}

	if ((s32)maxDepth < DrawLevelOvr1P_RenderScratch()->topLevelNearDepthThreshold && !DrawLevelOvr1P_TreatAsRetailNegativeTextureWord(mosaicWord))
	{
		texture++;
	}

	return texture;
}

static void Ovr226_800a3e44_WriteGround4x1SelectorUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                     u32 tableWord)
{
	DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
}

static int Ovr226_800a3eb0_Ground4x1NearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a402c_DispatchGround4x1DirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a3a78_BuildGround4x1ListSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a39c4_DispatchGround4x1HelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, inheritedOtIndex);
}

static void Ovr226_800a3f74_PrepareGround4x1DeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	if (projected != CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET))
	{
		return;
	}

	u32 mosaicBase = DrawLevelOvr1P_Scratch()->mosaicTextureWord;
	int mosaicIsNative = 0;
#ifdef CTR_NATIVE
	// NOTE(aalhendi): Retail dereferences scratch 0x84 directly. Native can
	// carry host-rebased level words here, so only follow pointer-shaped
	// values that belong to level texture data.
	mosaicIsNative = DrawLevelOvr1P_IsNativeLevelTexturePointer(mosaicBase);
	if (mosaicBase == 0 || ((s32)mosaicBase < 0 && !mosaicIsNative))
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}
#endif
	if ((s32)mosaicBase > 0 && !mosaicIsNative)
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	if (DrawLevelOvr1P_Scratch()->previousDirectHandlerAddress != DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_HELPER_DEFAULT)
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	u32 sourceOffset = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_INDEX_OFFSET) << 1;
	if ((s32)(DrawLevelOvr1P_Scratch()->selected4x1TableWord << 8) < 0)
	{
		sourceOffset += 0x30;
	}
	sourceOffset += *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_BIAS_OFFSET);

	const u8 *source = (const u8 *)(uintptr_t)(mosaicBase + sourceOffset);
	u32 uv0 = DrawLevelOvr1P_ReadPackedWord(source + 0);
	u32 uv1 = DrawLevelOvr1P_ReadPackedWord(source + 4);

	struct DrawLevelOvr1PScratchVertex *mutableProjected = (struct DrawLevelOvr1PScratchVertex *)projected;
	DrawLevelOvr1P_Scratch()->uv.uv0 = uv0;
	mutableProjected[indices[0]].flags = (u16)uv0;
	DrawLevelOvr1P_Scratch()->uv.uv1 = uv1;
	mutableProjected[indices[1]].flags = (u16)uv1;
	mutableProjected[indices[2]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 8);
	mutableProjected[indices[3]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 10);
}

static int Ovr226_800a3f74_DispatchGround4x1Deepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int inheritedOtIndex)
{
	Ovr226_800a3f74_PrepareGround4x1DeepestUv(projected, indices);
	int result = Ovr226_800a402c_DispatchGround4x1DirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static uint32_t *Ovr226_800a4158_ResolveGround4x1DirectOtEntry(struct PushBuffer *pb, int inheritedOtIndex)
{
	if (inheritedOtIndex < 0)
	{
		return NULL;
	}

	return &pb->ptrOT[inheritedOtIndex];
}

static int Ovr226_800a4034_EmitGround4x1GT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                               int inheritedOtIndex, int secondary)
{
	int triIndices[3];
	uint32_t *otEntry = Ovr226_800a4158_ResolveGround4x1DirectOtEntry(pb, inheritedOtIndex);

	(void)block;
	(void)texture;

	if (otEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	POLY_GT3 *prim = primMem->cursor;
	POLY_GT3 *nextPrim = prim + 1;

	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, triIndices, 3);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;
	u32 code = DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x36, 0x34);

	DrawLevelOvr1P_WriteProjectedGT3(prim, projected, triIndices, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 9, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int Ovr226_800a40b8_EmitGround4x1GT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                               int inheritedOtIndex)
{
	uint32_t *otEntry = Ovr226_800a4158_ResolveGround4x1DirectOtEntry(pb, inheritedOtIndex);

	(void)block;
	(void)texture;

	if (otEntry == NULL)
	{
		return 1;
	}

	POLY_GT4 *prim = primMem->cursor;
	POLY_GT4 *nextPrim = prim + 1;

	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, indices, 4);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;
	u32 code = DrawLevelOvr1P_SelectRawPrimitiveCode(uv1, 0x3e, 0x3c);

	DrawLevelOvr1P_WriteProjectedGT4(prim, projected, indices, code, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 12, otEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int Ovr226_800a402c_DispatchGround4x1DirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int inheritedOtIndex)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_PRIMARY:
		return Ovr226_800a4034_EmitGround4x1GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT3_SECOND:
		return Ovr226_800a4034_EmitGround4x1GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_LIST_DIRECT_GT4:
		return Ovr226_800a40b8_EmitGround4x1GT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
	default:
		return 1;
	}
}

static int Ovr226_800a3c70_Ground4x1SelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                     struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	int inheritedOtIndex = Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a3e00_SelectGround4x1SelectorTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		Ovr226_800a3e44_WriteGround4x1SelectorUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a3eb0_Ground4x1NearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtIndex);
}

static uint32_t *Ovr226_800a4978_ResolveGround4x1RenderedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth, int faceIndex)
{
	int otIndex = Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(block, projected, maxDepth, faceIndex);

	return &pb->ptrOT[otIndex];
}

static struct TextureLayout *Ovr226_800a49e0_SelectGround4x1RenderedTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                            u32 maxDepth)
{
	return Ovr226_800a3e00_SelectGround4x1SelectorTexture(block, projected, maxDepth);
}

static void Ovr226_800a4a28_WriteGround4x1RenderedUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                     u32 tableWord)
{
	DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
}

static void Ovr226_800a4950_StoreGround4x1RenderedClipHeader(u32 tableWord)
{
	DrawLevelOvr1P_StoreRenderedClipRecordHeader(tableWord);
}

static int Ovr226_800a4dcc_WriteGround4x1RenderedClippedRecordAtOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                        const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count,
                                                                        uint32_t *otEntry)
{
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

	(void)pb;

	if (otEntry == NULL)
	{
		return 1;
	}

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	struct DrawLevelOvr1PClipRecord *record = (struct DrawLevelOvr1PClipRecord *)cursor;
	record->header = DrawLevelOvr1P_GetRenderedClipRecordHeader(block, count);
	record->otEntry = (u32)(uintptr_t)otEntry;
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int Ovr226_800a4c14_EmitGround4x1RenderedGT3RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry, int secondary)
{
	int triIndices[3];

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, triIndices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, triIndices, 3))
	{
		return Ovr226_800a4dcc_WriteGround4x1RenderedClippedRecordAtOtEntry(pb, block, projected, triIndices, 3, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, texture, inheritedOtEntry,
	                                                               DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a4cc8_EmitGround4x1RenderedGT4RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800a4dcc_WriteGround4x1RenderedClippedRecordAtOtEntry(pb, block, projected, indices, 4, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, inheritedOtEntry,
	                                                                DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a4c0c_DispatchGround4x1RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_PRIMARY:
		return Ovr226_800a4c14_EmitGround4x1RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT3_SECOND:
		return Ovr226_800a4c14_EmitGround4x1RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_DIRECT_GT4:
		return Ovr226_800a4cc8_EmitGround4x1RenderedGT4RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry);
	default:
		return 1;
	}
}

static void Ovr226_800a4b54_PrepareGround4x1RenderedDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	if (projected != CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET))
	{
		return;
	}

	u32 mosaicBase = DrawLevelOvr1P_Scratch()->mosaicTextureWord;
	int mosaicIsNative = 0;
#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native keeps this as a data-boundary guard for
	// host-rebased level texture words; non-native code follows retail.
	mosaicIsNative = DrawLevelOvr1P_IsNativeLevelTexturePointer(mosaicBase);
	if (mosaicBase == 0 || ((s32)mosaicBase < 0 && !mosaicIsNative))
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}
#endif
	if ((s32)mosaicBase > 0 && !mosaicIsNative)
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	if (DrawLevelOvr1P_Scratch()->previousDirectHandlerAddress != DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_HELPER_DEFAULT)
	{
		DrawLevelOvr1P_RestoreProjectedUvScratch();
		return;
	}

	u32 sourceOffset = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_INDEX_OFFSET) << 1;
	if ((s32)(DrawLevelOvr1P_Scratch()->selected4x1TableWord << 8) < 0)
	{
		sourceOffset += 0x30;
	}
	sourceOffset += *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_BIAS_OFFSET);

	const u8 *source = (const u8 *)(uintptr_t)(mosaicBase + sourceOffset);
	u32 uv0 = DrawLevelOvr1P_ReadPackedWord(source + 0);
	u32 uv1 = DrawLevelOvr1P_ReadPackedWord(source + 4);

	struct DrawLevelOvr1PScratchVertex *mutableProjected = (struct DrawLevelOvr1PScratchVertex *)projected;
	DrawLevelOvr1P_Scratch()->uv.uv0 = uv0;
	mutableProjected[indices[0]].flags = (u16)uv0;
	DrawLevelOvr1P_Scratch()->uv.uv1 = uv1;
	mutableProjected[indices[1]].flags = (u16)uv1;
	mutableProjected[indices[2]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 8);
	mutableProjected[indices[3]].flags = DrawLevelOvr1P_ReadPackedHalf(source + 10);
}

static int Ovr226_800a4b54_DispatchGround4x1RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                            const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                            const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	Ovr226_800a4b54_PrepareGround4x1RenderedDeepestUv(projected, indices);
	int result = Ovr226_800a4c0c_DispatchGround4x1RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800a4ad0_Ground4x1RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a4c0c_DispatchGround4x1RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a4594_BuildGround4x1RenderedSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a44e0_DispatchGround4x1RenderedHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, inheritedOtEntry);
}

static int Ovr226_800a46d0_Ground4x1RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	u32 directMask = 0;

	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a4b54_DispatchGround4x1RenderedDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	return Ovr226_800a4ad0_Ground4x1RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtEntry);
}

static int Ovr226_800a44e0_DispatchGround4x1RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                   const struct TextureLayout *texture, int depth, u32 handlerAddress,
                                                                   uint32_t *inheritedOtEntry)
{
	switch (handlerAddress)
	{
	case OVR226_RETAIL_LABEL_GROUND_4X1_RENDERED_SUBDIV_A:
	{
		const int firstIndices[4] = {0, 4, 2, 3};
		const int secondIndices[4] = {1, 3, 4, 2};

		if (!Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                inheritedOtEntry))
		{
			return 0;
		}

		return Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth,
		                                                  DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY, inheritedOtEntry);
	}

	case OVR226_RETAIL_LABEL_GROUND_4X1_RENDERED_SUBDIV_B:
	{
		const int firstIndices[4] = {1, 8, 0, 2};
		const int secondIndices[4] = {0, 1, 8, 3};

		if (!Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                inheritedOtEntry))
		{
			return 0;
		}

		return Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth,
		                                                  DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtEntry);
	}

	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X1_RENDERED_HELPER_DEFAULT:
	{
		const int firstIndices[4] = {0, 4, 2, 8};
		const int secondIndices[4] = {4, 1, 8, 3};

		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
		if (!Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, firstIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                inheritedOtEntry))
		{
			return 0;
		}

		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
		return Ovr226_800a46d0_Ground4x1RenderedPreflight(pb, primMem, block, projected, secondIndices, faceIndex, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                  inheritedOtEntry);
	}

	default:
		return 1;
	}
}

static int Ovr226_800a47f4_Ground4x1RenderedSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                             struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	Ovr226_800a4950_StoreGround4x1RenderedClipHeader(tableWord);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	uint32_t *inheritedOtEntry = Ovr226_800a4978_ResolveGround4x1RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a49e0_SelectGround4x1RenderedTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		Ovr226_800a4a28_WriteGround4x1RenderedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a4ad0_Ground4x1RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtEntry);
}

static int Ovr226_800a52bc_DispatchGround4x2HelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                           int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex);
static int Ovr226_800a5b2c_Ground4x2NearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex);
static int Ovr226_800a5c54_DispatchGround4x2Deepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int inheritedOtIndex);
static int Ovr226_800a5d0c_DispatchGround4x2DirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int inheritedOtIndex);

static int Ovr226_800a580c_Ground4x2DirectPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, int inheritedOtIndex)
{
	u32 directMask = 0;

	if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a5c54_DispatchGround4x2Deepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	return Ovr226_800a5b2c_Ground4x2NearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtIndex);
}

static int Ovr226_800a52bc_DispatchGround4x2DeepestCompact(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                           const struct TextureLayout *texture, int depth, int handlerSlot, int inheritedOtIndex)
{
	const struct DrawLevelOvr1PNearSubdivisionCase *subdivisionCase = DrawLevelOvr1P_GetDeepestGridCompactCase(handlerSlot);

	for (s32 subdivisionIndex = 0; subdivisionIndex < 2; subdivisionIndex++)
	{
		int subIndices[4];

		for (s32 vertexIndex = 0; vertexIndex < 4; vertexIndex++)
		{
			subIndices[vertexIndex] = subdivisionCase->subIndices[subdivisionIndex][vertexIndex];
		}

		if (subdivisionCase->slotWords[subdivisionIndex] != DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE)
		{
			DrawLevelOvr1P_SetGridFaceSlotWord(projected, subdivisionCase->slotWords[subdivisionIndex]);
		}

		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, subIndices, faceIndex, texture, depth,
		                                              subdivisionCase->directMasks[subdivisionIndex], inheritedOtIndex))
		{
			return 0;
		}
	}

	return 1;
}

static int Ovr226_800a52bc_DispatchGround4x2HelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                           int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	if (DrawLevelOvr1P_HandlerUsesDeepestCompactGrid(handlerAddress) && DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a52bc_DispatchGround4x2DeepestCompact(pb, primMem, block, projected, faceIndex, texture, depth, handlerSlot, inheritedOtIndex);
	}

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 1:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 2:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 3:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 4:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 7:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 9:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	case 11:
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x18);
	if (!Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x24);
	return Ovr226_800a580c_Ground4x2DirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex);
}

static int Ovr226_800a5b2c_Ground4x2NearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a5d0c_DispatchGround4x2DirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a56f4_BuildGround4x2ListSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a52bc_DispatchGround4x2HelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                       inheritedOtIndex);
}

static void Ovr226_800a5c54_PrepareGround4x2DeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800a5c54_DispatchGround4x2Deepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int inheritedOtIndex)
{
	Ovr226_800a5c54_PrepareGround4x2DeepestUv(projected, indices);
	int result = Ovr226_800a5d0c_DispatchGround4x2DirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static uint32_t *Ovr226_800a5e38_ResolveGround4x2DirectOtEntry(struct PushBuffer *pb, int inheritedOtIndex)
{
	if (inheritedOtIndex < 0)
	{
		return NULL;
	}

	return &pb->ptrOT[inheritedOtIndex];
}

static int Ovr226_800a5d14_EmitGround4x2GT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                               int inheritedOtIndex, int secondary)
{
	int triIndices[3];
	uint32_t *otEntry = Ovr226_800a5e38_ResolveGround4x2DirectOtEntry(pb, inheritedOtIndex);

	if (otEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, texture, otEntry,
	                                                               DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a5d98_EmitGround4x2GT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                               int inheritedOtIndex)
{
	uint32_t *otEntry = Ovr226_800a5e38_ResolveGround4x2DirectOtEntry(pb, inheritedOtIndex);

	if (otEntry == NULL)
	{
		return 1;
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, otEntry, DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a5d0c_DispatchGround4x2DirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int inheritedOtIndex)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_PRIMARY:
		return Ovr226_800a5d14_EmitGround4x2GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT3_SECOND:
		return Ovr226_800a5d14_EmitGround4x2GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_LIST_DIRECT_GT4:
		return Ovr226_800a5d98_EmitGround4x2GT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
	default:
		return 1;
	}
}

static int Ovr226_800a58ec_Ground4x2SelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                     struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	int inheritedOtIndex = Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a3e00_SelectGround4x1SelectorTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a5b2c_Ground4x2NearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtIndex);
}

static int Ovr226_800a725c_DispatchDynamicListHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                             int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex);
static int Ovr226_800a78a8_DynamicListNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                   const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex);
static int Ovr226_800a79a0_DispatchDynamicListDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int inheritedOtIndex);
static int Ovr226_800a7a58_DispatchDynamicListDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int inheritedOtIndex);

static int Ovr226_800a7588_DynamicListDirectPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, int inheritedOtIndex)
{
	u32 directMask = 0;

	if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a79a0_DispatchDynamicListDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	return Ovr226_800a78a8_DynamicListNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtIndex);
}

static int Ovr226_800a725c_DispatchDynamicListHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                             int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	(void)handlerAddress;

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 1:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 2:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 3:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 4:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 7:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 9:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 11:
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x30);
	if (!Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x3c);
	return Ovr226_800a7588_DynamicListDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                  inheritedOtIndex);
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Runtime test flag; never touch normal CTR rendering without marker. */
static int DrawLevelOvr1P_DynamicNearStageProfileEnabled(void)
{
    static int checked,enabled;
    if(!checked)
    {
        FILE *f=fopen("debug/level_dynamic_near_stages.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(enabled)Platform_Log("[DYN NEAR STAGES] instrumenting 1/8 root near calls\n");
    }
    return enabled && NativePerf_IsStressTestMeasuring();
}
static void DrawLevelOvr1P_DynamicNearStageProfileRecord(
    u64 buildNs,u64 dispatchNs,u64 setupNs,u32 mask,int ok)
{
    static u64 sumBuild,sumDispatch,sumSetup;
    static u32 calls,failed,halfMask;
    sumBuild+=buildNs;
    sumDispatch+=dispatchNs;
    sumSetup+=setupNs;
    failed+=(u32)(!ok);
    halfMask+=(u32)((mask & 0x3cu)!=0);
    if(++calls>=4000u)
    {
        Platform_Log("[DYN NEAR STAGES] rootCalls=%u midpointMs=%.3f dispatchMs=%.3f setupMs=%.3f failed=%u nonzeroMask=%u\n",
            calls,(double)sumBuild/1000000.0,
            (double)sumDispatch/1000000.0,
            (double)sumSetup/1000000.0,failed,halfMask);
        calls=0;failed=0;halfMask=0;
        sumBuild=0;sumDispatch=0;sumSetup=0;
    }
}
#endif

static int Ovr226_800a78a8_DynamicListNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                   const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a7a58_DispatchDynamicListDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	static u32 sNearStageSampleIndex;
	const int stageSample = (depth == 0) &&
	    DrawLevelOvr1P_DynamicNearStageProfileEnabled() &&
	    ((++sNearStageSampleIndex & 7u) == 0u);
	const u64 t0 = stageSample ? SDL_GetTicksNS() : 0;
#endif
	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a74a0_BuildDynamicListSubdivisionFrame(sub, projected, indices);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const u64 t1 = stageSample ? SDL_GetTicksNS() : 0;
#endif
	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const u64 t2 = stageSample ? SDL_GetTicksNS() : 0;
#endif
	int result=Ovr226_800a725c_DispatchDynamicListHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                            inheritedOtIndex);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if(stageSample)
	{
		const u64 t3=SDL_GetTicksNS();
		DrawLevelOvr1P_DynamicNearStageProfileRecord(t1-t0,t3-t2,t2-t1,nearMask,result);
	}
#endif
	return result;
}

static void Ovr226_800a79a0_PrepareDynamicListDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800a79a0_DispatchDynamicListDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int inheritedOtIndex)
{
	Ovr226_800a79a0_PrepareDynamicListDeepestUv(projected, indices);
	int result = Ovr226_800a7a58_DispatchDynamicListDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800a7a60_EmitDynamicListGT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                 int inheritedOtIndex, int secondary)
{
	return Ovr226_800a5d14_EmitGround4x2GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, secondary);
}

static int Ovr226_800a7ae4_EmitDynamicListGT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                 int inheritedOtIndex)
{
	return Ovr226_800a5d98_EmitGround4x2GT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
}

static int Ovr226_800a7a58_DispatchDynamicListDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int inheritedOtIndex)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_PRIMARY:
		return Ovr226_800a7a60_EmitDynamicListGT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT3_SECOND:
		return Ovr226_800a7a60_EmitDynamicListGT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_LIST_DIRECT_GT4:
		return Ovr226_800a7ae4_EmitDynamicListGT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
	default:
		return 1;
	}
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Sample 1 of 13 face calls: preserve retail selection, texture and OT packet
 * logic, measure substage costs without GPU or altered user graphics. */
static int DrawLevelOvr1P_DynamicFaceProfileEnabled(void)
{
    static int checked,enabled;
    if (!checked)
    {
        FILE *f=fopen("debug/level_dynamic_face_subprofile.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(enabled)Platform_Log("[DYN FACE PROFILE] 1/13 sample active\n");
    }
    return enabled && NativePerf_IsStressTestMeasuring();
}
static void DrawLevelOvr1P_DynamicFaceSample(
    u64 selectNs,u64 uvNs,u64 emitNs,int rejected,int failed,int nearKind)
{
    static u64 totalSelect,totalUv,totalEmit,totalDirect,totalNear;
    static u32 calls,rejectedCalls,failedCalls,directCalls,nearCalls;
    totalSelect+=selectNs;totalUv+=uvNs;totalEmit+=emitNs;
    rejectedCalls+=(u32)(rejected!=0);
    failedCalls+=(u32)(failed!=0);
    if(nearKind==0){totalDirect+=emitNs;++directCalls;}
    if(nearKind==1){totalNear+=emitNs;++nearCalls;}
    if(++calls>=20000u)
    {
        Platform_Log("[DYN FACE PROFILE] calls=%u selectMs=%.3f textureMs=%.3f emitMs=%.3f rejected=%u failed=%u directCalls=%u nearCalls=%u directMs=%.3f nearMs=%.3f\n",
            calls,(double)totalSelect/1000000.0,
            (double)totalUv/1000000.0,(double)totalEmit/1000000.0,
            rejectedCalls,failedCalls,directCalls,nearCalls,
            (double)totalDirect/1000000.0,(double)totalNear/1000000.0);
        calls=0;rejectedCalls=0;failedCalls=0;directCalls=0;nearCalls=0;
        totalSelect=0;totalUv=0;totalEmit=0;totalDirect=0;totalNear=0;
    }
}
static int Ovr226_800a7668_ProfiledFaceGate(
    struct PushBuffer *pb,struct PrimMem *primMem,
    struct DrawLevelOvr1PScratchVertex *projected,
    struct QuadBlock *block,int faceIndex)
{
    int indices[4];
    u32 directMask=0;
    const u64 t0=SDL_GetTicksNS();
    const u32 tableWord=DrawLevelOvr1P_Select4x1ProjectedFace(projected,block,faceIndex,indices);
    const int passes=Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(
        projected,indices,tableWord,&directMask);
    const u64 t1=SDL_GetTicksNS();
    if(!passes)
    {
        DrawLevelOvr1P_DynamicFaceSample(t1-t0,0,0,1,0,-1);
        return 1;
    }
    const u32 maxDepth=DrawLevelOvr1P_GetProjectedMaxDepth(projected,indices);
    const int inheritedOtIndex=Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(
        block,projected,maxDepth,faceIndex);
    struct TextureLayout *texture=Ovr226_800a3e00_SelectGround4x1SelectorTexture(
        block,projected,maxDepth);
    if(texture!=NULL)DrawLevelOvr1P_WriteProjectedUv(projected,indices,texture,tableWord);
    const int nearKind=(DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(
        projected,indices,0x24)!=0);
    const u64 t2=SDL_GetTicksNS();
    const int result=Ovr226_800a78a8_DynamicListNearOrDirect(
        pb,primMem,block,projected,indices,faceIndex,texture,0,0x24,inheritedOtIndex);
    const u64 t3=SDL_GetTicksNS();
    DrawLevelOvr1P_DynamicFaceSample(t1-t0,t2-t1,t3-t2,0,!result,nearKind);
    return result;
}
#endif

static int Ovr226_800a7668_DynamicListSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                       struct QuadBlock *block, int faceIndex)
{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    static u32 qaSampleCounter;
    if(DrawLevelOvr1P_DynamicFaceProfileEnabled() &&
       (++qaSampleCounter % 13u)==0u)
        return Ovr226_800a7668_ProfiledFaceGate(pb,primMem,projected,block,faceIndex);
#endif
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	int inheritedOtIndex = Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a3e00_SelectGround4x1SelectorTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a78a8_DynamicListNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtIndex);
}

static int Ovr226_800a8e7c_DispatchWideDynamicHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                             int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex);
static int Ovr226_800a94c8_WideDynamicNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                   const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex);
static int Ovr226_800a95c0_DispatchWideDynamicDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int inheritedOtIndex);
static int Ovr226_800a9678_DispatchWideDynamicDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int inheritedOtIndex);

static int Ovr226_800a91a8_WideDynamicDirectPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, int inheritedOtIndex)
{
	u32 directMask = 0;

	if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a95c0_DispatchWideDynamicDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	return Ovr226_800a94c8_WideDynamicNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtIndex);
}

static int Ovr226_800a8e7c_DispatchWideDynamicHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, const struct TextureLayout *texture,
                                                             int depth, u32 handlerAddress, int handlerSlot, int inheritedOtIndex)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	(void)handlerAddress;

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 1:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 2:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 3:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 4:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 7:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 9:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	case 11:
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtIndex))
		{
			return 0;
		}
		if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtIndex))
		{
			return 0;
		}
		return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtIndex);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x30);
	if (!Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtIndex))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x3c);
	return Ovr226_800a91a8_WideDynamicDirectPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                  inheritedOtIndex);
}

static int Ovr226_800a94c8_WideDynamicNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                   const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, int inheritedOtIndex)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a9678_DispatchWideDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a90c0_BuildWideDynamicSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a8e7c_DispatchWideDynamicHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                         inheritedOtIndex);
}

static void Ovr226_800a95c0_PrepareWideDynamicDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800a95c0_DispatchWideDynamicDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int inheritedOtIndex)
{
	Ovr226_800a95c0_PrepareWideDynamicDeepestUv(projected, indices);
	int result = Ovr226_800a9678_DispatchWideDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtIndex);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800a9680_EmitWideDynamicGT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                 int inheritedOtIndex, int secondary)
{
	return Ovr226_800a5d14_EmitGround4x2GT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, secondary);
}

static int Ovr226_800a9704_EmitWideDynamicGT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                 int inheritedOtIndex)
{
	return Ovr226_800a5d98_EmitGround4x2GT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
}

static int Ovr226_800a9678_DispatchWideDynamicDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int inheritedOtIndex)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_PRIMARY:
		return Ovr226_800a9680_EmitWideDynamicGT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT3_SECOND:
		return Ovr226_800a9680_EmitWideDynamicGT3Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_LIST_DIRECT_GT4:
		return Ovr226_800a9704_EmitWideDynamicGT4Raw(pb, primMem, block, projected, indices, texture, inheritedOtIndex);
	default:
		return 1;
	}
}

static int Ovr226_800a9288_WideDynamicSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                       struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	int inheritedOtIndex = Ovr226_800a3d98_ResolveGround4x1SelectorOtIndex(block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a3e00_SelectGround4x1SelectorTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a94c8_WideDynamicNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtIndex);
}

static int Ovr226_800a661c_Ground4x2RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry);
static int Ovr226_800a61c0_DispatchGround4x2RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                   const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                   uint32_t *inheritedOtEntry);
static int Ovr226_800a69dc_Ground4x2RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset,
                                                         uint32_t *inheritedOtEntry);
static int Ovr226_800a6af4_DispatchGround4x2RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                            const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                            const struct TextureLayout *texture, uint32_t *inheritedOtEntry);
static int Ovr226_800a6bac_DispatchGround4x2RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, uint32_t *inheritedOtEntry);

static void Ovr226_800a689c_StoreGround4x2RenderedClipHeader(u32 tableWord)
{
	DrawLevelOvr1P_StoreRenderedClipRecordHeader(tableWord);
}

static uint32_t *Ovr226_800a68c8_ResolveGround4x2RenderedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                 const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth, int faceIndex)
{
	return Ovr226_800a4978_ResolveGround4x1RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
}

static struct TextureLayout *Ovr226_800a6930_SelectGround4x2RenderedTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                            u32 maxDepth)
{
	return Ovr226_800a49e0_SelectGround4x1RenderedTexture(block, projected, maxDepth);
}

static void Ovr226_800a6970_WriteGround4x2RenderedUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                     u32 tableWord)
{
	DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
}

static int Ovr226_800a6d6c_WriteGround4x2RenderedClippedRecordAtOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                        const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count,
                                                                        uint32_t *otEntry)
{
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

	(void)pb;

	if (otEntry == NULL)
	{
		return 1;
	}

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	struct DrawLevelOvr1PClipRecord *record = (struct DrawLevelOvr1PClipRecord *)cursor;
	record->header = DrawLevelOvr1P_GetRenderedClipRecordHeader(block, count);
	record->otEntry = (u32)(uintptr_t)otEntry;
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int Ovr226_800a6bb4_EmitGround4x2RenderedGT3RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry, int secondary)
{
	int triIndices[3];

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, triIndices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, triIndices, 3))
	{
		return Ovr226_800a6d6c_WriteGround4x2RenderedClippedRecordAtOtEntry(pb, block, projected, triIndices, 3, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, texture, inheritedOtEntry,
	                                                               DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a6c68_EmitGround4x2RenderedGT4RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800a6d6c_WriteGround4x2RenderedClippedRecordAtOtEntry(pb, block, projected, indices, 4, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, inheritedOtEntry,
	                                                                DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a6bac_DispatchGround4x2RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                               const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_PRIMARY:
		return Ovr226_800a6bb4_EmitGround4x2RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT3_SECOND:
		return Ovr226_800a6bb4_EmitGround4x2RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_GROUND_4X2_RENDERED_DIRECT_GT4:
		return Ovr226_800a6c68_EmitGround4x2RenderedGT4RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry);
	default:
		return 1;
	}
}

static void Ovr226_800a6af4_PrepareGround4x2RenderedDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800a6af4_DispatchGround4x2RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                            const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                            const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	Ovr226_800a6af4_PrepareGround4x2RenderedDeepestUv(projected, indices);
	int result = Ovr226_800a6bac_DispatchGround4x2RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800a69dc_Ground4x2RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                         const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a6bac_DispatchGround4x2RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a6510_BuildGround4x2RenderedSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a61c0_DispatchGround4x2RenderedHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                               inheritedOtEntry);
}

static int Ovr226_800a661c_Ground4x2RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                      const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	u32 directMask = 0;

	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a6af4_DispatchGround4x2RenderedDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	return Ovr226_800a69dc_Ground4x2RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtEntry);
}

static int Ovr226_800a6260_DispatchGround4x2RenderedDeepestCompact(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                   const struct TextureLayout *texture, int depth, int handlerSlot, uint32_t *inheritedOtEntry)
{
	const struct DrawLevelOvr1PNearSubdivisionCase *subdivisionCase = DrawLevelOvr1P_GetDeepestGridCompactCase(handlerSlot);

	for (s32 subdivisionIndex = 0; subdivisionIndex < 2; subdivisionIndex++)
	{
		int subIndices[4];

		for (s32 vertexIndex = 0; vertexIndex < 4; vertexIndex++)
		{
			subIndices[vertexIndex] = subdivisionCase->subIndices[subdivisionIndex][vertexIndex];
		}

		if (subdivisionCase->slotWords[subdivisionIndex] != DRAW_LEVEL_OVR1P_SLOT_WORD_PRESERVE)
		{
			DrawLevelOvr1P_SetGridFaceSlotWord(projected, subdivisionCase->slotWords[subdivisionIndex]);
		}

		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, subIndices, faceIndex, texture, depth,
		                                                subdivisionCase->directMasks[subdivisionIndex], inheritedOtEntry))
		{
			return 0;
		}
	}

	return 1;
}

static int Ovr226_800a61c0_DispatchGround4x2RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                   struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                   const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                   uint32_t *inheritedOtEntry)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	if (DrawLevelOvr1P_HandlerUsesDeepestCompactGrid(handlerAddress) && DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a6260_DispatchGround4x2RenderedDeepestCompact(pb, primMem, block, projected, faceIndex, texture, depth, handlerSlot, inheritedOtEntry);
	}

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 1:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 2:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 3:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 4:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 7:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 9:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	case 11:
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                  allowedQuad, inheritedOtEntry);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x18);
	if (!Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x24);
	return Ovr226_800a661c_Ground4x2RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                  inheritedOtEntry);
}

static int Ovr226_800a6740_Ground4x2RenderedSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                             struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	Ovr226_800a689c_StoreGround4x2RenderedClipHeader(tableWord);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	uint32_t *inheritedOtEntry = Ovr226_800a68c8_ResolveGround4x2RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a6930_SelectGround4x2RenderedTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		Ovr226_800a6970_WriteGround4x2RenderedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a69dc_Ground4x2RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtEntry);
}

static int Ovr226_800a825c_DynamicRenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry);
static int Ovr226_800a7f0c_DispatchDynamicRenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                 const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                 uint32_t *inheritedOtEntry);
static int Ovr226_800a861c_DynamicRenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry);
static int Ovr226_800a8714_DispatchDynamicRenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                          const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                          const struct TextureLayout *texture, uint32_t *inheritedOtEntry);
static int Ovr226_800a87cc_DispatchDynamicRenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry);

static void Ovr226_800a84d0_StoreDynamicRenderedClipHeader(u32 tableWord)
{
	DrawLevelOvr1P_StoreRenderedClipRecordHeader(tableWord);
}

static uint32_t *Ovr226_800a8504_ResolveDynamicRenderedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth, int faceIndex)
{
	return Ovr226_800a68c8_ResolveGround4x2RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
}

static struct TextureLayout *Ovr226_800a856c_SelectDynamicRenderedTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                          u32 maxDepth)
{
	return Ovr226_800a6930_SelectGround4x2RenderedTexture(block, projected, maxDepth);
}

static void Ovr226_800a85b0_WriteDynamicRenderedUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                   u32 tableWord)
{
	DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
}

static int Ovr226_800a898c_WriteDynamicRenderedClippedRecordAtOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count,
                                                                      uint32_t *otEntry)
{
	struct DrawLevelOvr1PClipRecord *record;
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

	(void)pb;

	if (otEntry == NULL)
	{
		return 1;
	}

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	record = (struct DrawLevelOvr1PClipRecord *)cursor;
	record->header = DrawLevelOvr1P_GetRenderedClipRecordHeader(block, count);
	record->otEntry = (u32)(uintptr_t)otEntry;
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int Ovr226_800a87d4_EmitDynamicRenderedGT3RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                           const struct TextureLayout *texture, uint32_t *inheritedOtEntry, int secondary)
{
	int triIndices[3];

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, triIndices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, triIndices, 3))
	{
		return Ovr226_800a898c_WriteDynamicRenderedClippedRecordAtOtEntry(pb, block, projected, triIndices, 3, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, texture, inheritedOtEntry,
	                                                               DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a8888_EmitDynamicRenderedGT4RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                           const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800a898c_WriteDynamicRenderedClippedRecordAtOtEntry(pb, block, projected, indices, 4, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, inheritedOtEntry,
	                                                                DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a87cc_DispatchDynamicRenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_PRIMARY:
		return Ovr226_800a87d4_EmitDynamicRenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT3_SECOND:
		return Ovr226_800a87d4_EmitDynamicRenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_DYNAMIC_RENDERED_DIRECT_GT4:
		return Ovr226_800a8888_EmitDynamicRenderedGT4RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry);
	default:
		return 1;
	}
}

static void Ovr226_800a8714_PrepareDynamicRenderedDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800a8714_DispatchDynamicRenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                          const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                          const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	Ovr226_800a8714_PrepareDynamicRenderedDeepestUv(projected, indices);
	int result = Ovr226_800a87cc_DispatchDynamicRenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800a861c_DynamicRenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800a87cc_DispatchDynamicRenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a8150_BuildDynamicRenderedSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a7f0c_DispatchDynamicRenderedHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                             inheritedOtEntry);
}

static int Ovr226_800a825c_DynamicRenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	u32 directMask = 0;

	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a8714_DispatchDynamicRenderedDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	return Ovr226_800a861c_DynamicRenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtEntry);
}

static int Ovr226_800a7f0c_DispatchDynamicRenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                 const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                 uint32_t *inheritedOtEntry)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	(void)handlerAddress;

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 1:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 2:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 3:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 4:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 7:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 9:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 11:
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x30);
	if (!Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x3c);
	return Ovr226_800a825c_DynamicRenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtEntry);
}

static int Ovr226_800a8380_DynamicRenderedSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                           struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	Ovr226_800a84d0_StoreDynamicRenderedClipHeader(tableWord);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	uint32_t *inheritedOtEntry = Ovr226_800a8504_ResolveDynamicRenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800a856c_SelectDynamicRenderedTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		Ovr226_800a85b0_WriteDynamicRenderedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800a861c_DynamicRenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtEntry);
}

static int Ovr226_800a9e7c_Quad4x4RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry);
static int Ovr226_800a9b2c_DispatchQuad4x4RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                 const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                 uint32_t *inheritedOtEntry);
static int Ovr226_800aa23c_Quad4x4RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry);
static int Ovr226_800aa334_DispatchQuad4x4RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                          const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                          const struct TextureLayout *texture, uint32_t *inheritedOtEntry);
static int Ovr226_800aa3ec_DispatchQuad4x4RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry);

static void Ovr226_800aa0fc_StoreQuad4x4RenderedClipHeader(u32 tableWord)
{
	DrawLevelOvr1P_StoreRenderedClipRecordHeader(tableWord);
}

static uint32_t *Ovr226_800aa124_ResolveQuad4x4RenderedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, u32 maxDepth, int faceIndex)
{
	return Ovr226_800a68c8_ResolveGround4x2RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
}

static struct TextureLayout *Ovr226_800aa18c_SelectQuad4x4RenderedTexture(const struct QuadBlock *block, const struct DrawLevelOvr1PScratchVertex *projected,
                                                                          u32 maxDepth)
{
	return Ovr226_800a6930_SelectGround4x2RenderedTexture(block, projected, maxDepth);
}

static void Ovr226_800aa1d0_WriteQuad4x4RenderedUv(struct DrawLevelOvr1PScratchVertex *projected, const int *indices, const struct TextureLayout *texture,
                                                   u32 tableWord)
{
	DrawLevelOvr1P_WriteProjectedUv(projected, indices, texture, tableWord);
}

static int Ovr226_800aa5ac_WriteQuad4x4RenderedClippedRecordAtOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int count,
                                                                      uint32_t *otEntry)
{
	u8 *cursor = DrawLevelOvr1P_GetClipRecordCursor();
	size_t recordSize = DrawLevelOvr1P_GetClipRecordSize(count);

	(void)pb;

	if (otEntry == NULL)
	{
		return 1;
	}

	if (!DrawLevelOvr1P_ShouldWriteRenderedClippedRecord(projected, indices, count))
	{
		return 1;
	}

	struct DrawLevelOvr1PClipRecord *record = (struct DrawLevelOvr1PClipRecord *)cursor;
	record->header = DrawLevelOvr1P_GetRenderedClipRecordHeader(block, count);
	record->otEntry = (u32)(uintptr_t)otEntry;
	record->tpage = DrawLevelOvr1P_Scratch()->uv.tpage;
	record->clut = DrawLevelOvr1P_Scratch()->uv.clut;

	for (s32 vertexIndex = 0; vertexIndex < count; vertexIndex++)
	{
		DrawLevelOvr1P_CopyClipRecordVertex(&record->vertex[vertexIndex], &projected[indices[vertexIndex]]);
	}

	DrawLevelOvr1P_SetClipRecordCursor(cursor + recordSize);
	return 1;
}

static int Ovr226_800aa3f4_EmitQuad4x4RenderedGT3RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                           const struct TextureLayout *texture, uint32_t *inheritedOtEntry, int secondary)
{
	int triIndices[3];

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, triIndices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, triIndices, 3))
	{
		return Ovr226_800aa5ac_WriteQuad4x4RenderedClippedRecordAtOtEntry(pb, block, projected, triIndices, 3, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, texture, inheritedOtEntry,
	                                                               DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800aa4a8_EmitQuad4x4RenderedGT4RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, const int *indices,
                                                           const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800aa5ac_WriteQuad4x4RenderedClippedRecordAtOtEntry(pb, block, projected, indices, 4, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, texture, inheritedOtEntry,
	                                                                DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800aa3ec_DispatchQuad4x4RenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                             const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	(void)faceIndex;

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_PRIMARY:
		return Ovr226_800aa3f4_EmitQuad4x4RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT3_SECOND:
		return Ovr226_800aa3f4_EmitQuad4x4RenderedGT3RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_QUAD_4X4_RENDERED_DIRECT_GT4:
		return Ovr226_800aa4a8_EmitQuad4x4RenderedGT4RawOrClip(pb, primMem, block, projected, indices, texture, inheritedOtEntry);
	default:
		return 1;
	}
}

static void Ovr226_800aa334_PrepareQuad4x4RenderedDeepestUv(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	u32 handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	if (handlerAddress != 0)
	{
		DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	}
}

static int Ovr226_800aa334_DispatchQuad4x4RenderedDeepest(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                          const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                          const struct TextureLayout *texture, uint32_t *inheritedOtEntry)
{
	Ovr226_800aa334_PrepareQuad4x4RenderedDeepestUv(projected, indices);
	int result = Ovr226_800aa3ec_DispatchQuad4x4RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);

	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static int Ovr226_800aa23c_Quad4x4RenderedNearOrDirect(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                       const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                       const struct TextureLayout *texture, int depth, u32 thresholdScratchOffset, uint32_t *inheritedOtEntry)
{
	u32 nearMask = DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, thresholdScratchOffset);

	if (nearMask == 0)
	{
		return Ovr226_800aa3ec_DispatchQuad4x4RenderedDirectTail(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a9d70_BuildQuad4x4RenderedSubdivisionFrame(sub, projected, indices);

	u32 handlerAddress = *CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET + nearMask);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	return Ovr226_800a9b2c_DispatchQuad4x4RenderedHelperWrappers(pb, primMem, block, sub, faceIndex, texture, depth + 1, handlerAddress, handlerSlot,
	                                                             inheritedOtEntry);
}

static int Ovr226_800a9e7c_Quad4x4RenderedPreflight(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                    const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                    const struct TextureLayout *texture, int depth, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	u32 directMask = 0;

	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3b90_SelectAndStoreGround4x1DirectMask(projected, indices, allowedMask, &directMask))
	{
		return 1;
	}

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800aa334_DispatchQuad4x4RenderedDeepest(pb, primMem, block, projected, indices, faceIndex, texture, inheritedOtEntry);
	}

	return Ovr226_800aa23c_Quad4x4RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, depth, 0x28, inheritedOtEntry);
}

static int Ovr226_800a9b2c_DispatchQuad4x4RenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                                 struct DrawLevelOvr1PScratchVertex *projected, int faceIndex,
                                                                 const struct TextureLayout *texture, int depth, u32 handlerAddress, int handlerSlot,
                                                                 uint32_t *inheritedOtEntry)
{
	const u32 allowedQuad = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
	const u32 allowedTriPrimary = DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	const u32 allowedTriSecondary = DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;

	(void)handlerAddress;

	switch (handlerSlot)
	{
	case 0:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 1:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 2:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 3:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 4:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriPrimary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 7:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 9:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	case 11:
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, texture, depth,
		                                              allowedQuad, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth,
		                                              allowedTriSecondary, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
		                                              inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth,
		                                                allowedQuad, inheritedOtEntry);
	default:
		break;
	}

	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x0);
	if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0xc);
	if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x30);
	if (!Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, texture, depth, allowedQuad,
	                                              inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0x3c);
	return Ovr226_800a9e7c_Quad4x4RenderedPreflight(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, texture, depth, allowedQuad,
	                                                inheritedOtEntry);
}

static int Ovr226_800a9fa0_Quad4x4RenderedSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                           struct QuadBlock *block, int faceIndex)
{
	int indices[4];
	u32 directMask = 0;

	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);
	if (DrawLevelOvr1P_IsProjectedFaceFullyNear(projected, indices))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		directMask = DRAW_LEVEL_OVR1P_DIRECT_QUAD;
		DrawLevelOvr1P_Scratch()->directMask = directMask;
	}
	else if (!Ovr226_800a3c70_SelectAndStoreGround4x1SelectorDirectMask(projected, indices, tableWord, &directMask))
	{
		return 1;
	}

	Ovr226_800aa0fc_StoreQuad4x4RenderedClipHeader(tableWord);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	uint32_t *inheritedOtEntry = Ovr226_800aa124_ResolveQuad4x4RenderedOtEntry(pb, block, projected, maxDepth, faceIndex);
	struct TextureLayout *texture = Ovr226_800aa18c_SelectQuad4x4RenderedTexture(block, projected, maxDepth);
	if (texture != NULL)
	{
		Ovr226_800aa1d0_WriteQuad4x4RenderedUv(projected, indices, texture, tableWord);
	}

	return Ovr226_800aa23c_Quad4x4RenderedNearOrDirect(pb, primMem, block, projected, indices, faceIndex, texture, 0, 0x24, inheritedOtEntry);
}

static int DrawLevelOvr1P_Emit4x1ListSelectedFace(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                  struct QuadBlock *block, int faceIndex)
{
	// NOTE(aalhendi): Retail 0x800a3c70 owns selector decode, table-word
	// storage, offscreen/direct-mask preflight, texture/UV selection, and
	// near-mask dispatch for each 4x1 list face.
	return Ovr226_800a3c70_Ground4x1SelectorNearGate(pb, primMem, projected, block, faceIndex);
}

static void DrawLevelOvr1P_SetRenderedListCursor(struct QuadBlock **renderedList)
{
	DrawLevelOvr1P_Scratch()->renderedOverflowPtr32 = (u32)(uintptr_t)renderedList;
}

static void DrawLevelOvr1P_SetRenderedOverflowBase(struct QuadBlock **renderedList)
{
	sDrawLevelOvr1P_RenderedOverflowBase = renderedList;
}

static void DrawLevelOvr1P_SetViewportScratchContext(struct PushBuffer *pb, const int *visFaceList, u8 *clipStart, u8 *clipCursor,
                                                     struct QuadBlock **renderedOverflowBase)
{
	DrawLevelOvr1P_SetRenderedListCursor(renderedOverflowBase);
	DrawLevelOvr1P_SetClipRecordCursor(clipCursor);
	DrawLevelOvr1P_Scratch()->visFaceListPtr32 = (u32)(uintptr_t)visFaceList;
	DrawLevelOvr1P_SetClipRecordStart(clipStart);
	DrawLevelOvr1P_SetRenderedOverflowBase(renderedOverflowBase);
	Ovr226_800a0d34_SetEntryGteAndCameraScratch(pb);
}

static void DrawLevelOvr_ClearRenderedOverflowBase(int playerIndex)
{
	struct QuadBlock **renderedOverflowBase = (struct QuadBlock **)data.ptrRenderedQuadblockDestination_forEachPlayer[playerIndex];

	if (renderedOverflowBase != NULL)
	{
		*renderedOverflowBase = NULL;
	}
}

static int DrawLevelOvr_ConsumeClipRecordsForViewport(struct PushBuffer *pb, struct PrimMem *primMem, u8 *clipCursor, int playerIndex,
                                                      int (*consume)(struct PushBuffer *pb, struct PrimMem *primMem, u8 *clipCursor, int playerIndex))
{
	u8 *start = data.PtrClipBuffer[playerIndex];

	DrawLevelOvr1P_SetClipRecordStart(start);
	DrawLevelOvr1P_SetClipRecordCursor(clipCursor);
	return consume(pb, primMem, clipCursor, playerIndex);
}

static struct QuadBlock **DrawLevelOvr1P_GetRenderedOverflowBase(void)
{
	if (sDrawLevelOvr1P_RenderedOverflowBase != NULL)
	{
		return sDrawLevelOvr1P_RenderedOverflowBase;
	}

	return sdata_static.quadBlocksRendered;
}

static struct QuadBlock **DrawLevelOvr1P_GetRenderedListCursor(void)
{
	return (struct QuadBlock **)(uintptr_t)DrawLevelOvr1P_Scratch()->renderedOverflowPtr32;
}

static void DrawLevelOvr1P_AppendRenderedQuadBlock(struct QuadBlock *block)
{
	struct QuadBlock **renderedList = DrawLevelOvr1P_GetRenderedListCursor();

	if (renderedList == NULL)
	{
		return;
	}

	*renderedList = block;
	DrawLevelOvr1P_SetRenderedListCursor(renderedList + 1);
}

static void DrawLevelOvr1P_TerminateRenderedListCursor(void)
{
	struct QuadBlock **renderedList = DrawLevelOvr1P_GetRenderedListCursor();

	if (renderedList != NULL)
	{
		*renderedList = NULL;
	}
}

static struct TextureLayout *Ovr226_800a1058_PrepareFullDynamicLowUv(struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected)
{
	const int *indices = sDrawLevelOvr1PFullDynamicLowIndices;
	struct TextureLayout *texture = block->ptr_texture_low;

	// NOTE(aalhendi): Retail full-dynamic 0x800a0ef4 seeds low-LOD UVs before
	// choosing either the direct low quad or the near/transition helper table.
	DrawLevelOvr1P_Scratch()->selected4x1TableWord = 0;

	u32 uv0 = DrawLevelOvr1P_ReadWord(texture, 0);
	u32 uv1 = DrawLevelOvr1P_ReadWord(texture, 4);
	u32 uv2 = DrawLevelOvr1P_ReadWord(texture, 8);
	DrawLevelOvr1P_Scratch()->uv.uv0 = uv0;
	DrawLevelOvr1P_Scratch()->uv.uv1 = uv1;
	DrawLevelOvr1P_Scratch()->uv.uv2 = uv2;
	projected[indices[0]].flags = (u16)uv0;
	projected[indices[1]].flags = (u16)uv1;
	projected[indices[2]].flags = (u16)uv2;
	projected[indices[3]].flags = (u16)(uv2 >> 16);

	return texture;
}

static u16 DrawLevelOvr1P_AverageFullDynamicUv(u16 uvA, u16 uvB)
{
	u8 u = (u8)((((u32)uvA & 0xff) + ((u32)uvB & 0xff)) >> 1);
	u8 v = (u8)((((u32)uvA >> 8) + ((u32)uvB >> 8)) >> 1);

	return DrawLevelOvr1P_PackUv(u, v);
}

static void DrawLevelOvr1P_AdjustFullDynamicMidUvs(struct DrawLevelOvr1PScratchVertex *projected)
{
	projected[4].flags = DrawLevelOvr1P_AverageFullDynamicUv(projected[0].flags, projected[1].flags);
	projected[5].flags = DrawLevelOvr1P_AverageFullDynamicUv(projected[0].flags, projected[2].flags);
	projected[6].flags = DrawLevelOvr1P_AverageFullDynamicUv(projected[1].flags, projected[2].flags);
	projected[7].flags = DrawLevelOvr1P_AverageFullDynamicUv(projected[1].flags, projected[3].flags);
	projected[8].flags = DrawLevelOvr1P_AverageFullDynamicUv(projected[2].flags, projected[3].flags);
}

static int DrawLevelOvr1P_DispatchFullDynamicHelperSequence(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                            struct DrawLevelOvr1PScratchVertex *projected, DrawLevelOvrRetailLabel handlerAddress,
                                                            struct TextureLayout *texture, int depth);

static int Ovr226_800a1ce8_EmitFullDynamicGT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                 struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 struct TextureLayout *texture, int secondary)
{
	int triIndices[3];

	if (secondary)
	{
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOt(pb, primMem, block, projected, triIndices, faceIndex, texture, 0,
	                                                          sDrawLevelOvr1P_FullDynamicInheritedOtIndex, DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a1d6c_EmitFullDynamicGT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                 struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                 struct TextureLayout *texture)
{
	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOt(pb, primMem, block, projected, indices, faceIndex, texture, 0,
	                                                           sDrawLevelOvr1P_FullDynamicInheritedOtIndex, DRAW_LEVEL_OVR1P_PRIM_CODE_AUTO);
}

static int Ovr226_800a1cc4_EmitFullDynamicDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                     struct TextureLayout *texture)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	DrawLevelOvrRetailLabel handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_229:
		return Ovr226_800a1ce8_EmitFullDynamicGT3Raw(pb, primMem, block, projected, indices, faceIndex, texture, 0);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_229:
		return Ovr226_800a1ce8_EmitFullDynamicGT3Raw(pb, primMem, block, projected, indices, faceIndex, texture, 1);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_229:
		return Ovr226_800a1d6c_EmitFullDynamicGT4Raw(pb, primMem, block, projected, indices, faceIndex, texture);
	default:
		return 1;
	}
}

static int Ovr226_800a1cc4_IsSplitGroundListADirectHandler(DrawLevelOvrRetailLabel handlerAddress)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_PRIMARY_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT3_SECOND_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_229:
		return 1;
	default:
		return 0;
	}
}

static int Ovr226_800a1cc4_EmitFullDynamicDeepestDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                            struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                            struct TextureLayout *texture)
{
	DrawLevelOvrRetailLabel handlerAddress = DrawLevelOvr1P_GetDirectHandlerAddress(DrawLevelOvr1P_Scratch()->directMask);

	if (!Ovr226_800a1cc4_IsSplitGroundListADirectHandler(handlerAddress))
	{
		return Ovr226_800a1cc4_EmitFullDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture);
	}

	DrawLevelOvr1P_PrepareDeepestMosaicUv(projected, indices, handlerAddress);
	int result = Ovr226_800a1cc4_EmitFullDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture);
	DrawLevelOvr1P_RestoreProjectedUvScratch();
	return result;
}

static u32 Ovr226_800a1be8_SelectFullDynamicNearMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 scratchOffset)
{
	return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, indices, scratchOffset);
}

static int Ovr226_800a1be8_DispatchFullDynamicNearSubdivision(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                              struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                              struct TextureLayout *texture, int depth, u32 directMask, u32 thresholdScratchOffset)
{
	(void)directMask;
	u32 nearMask = Ovr226_800a1be8_SelectFullDynamicNearMask(projected, indices, thresholdScratchOffset);
	if (nearMask == 0)
	{
		return Ovr226_800a1cc4_EmitFullDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture);
	}

	if (depth >= DRAW_LEVEL_OVR1P_MAX_NEAR_SUBDIV_DEPTH)
	{
		return Ovr226_800a1cc4_EmitFullDynamicDirectTail(pb, primMem, block, projected, indices, faceIndex, texture);
	}

	DrawLevelOvrRetailLabel handlerAddress = DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST);
	DrawLevelOvr1P_SetPreviousRecursiveHandler(handlerAddress);

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	Ovr226_800a17d8_BuildFullDynamicSubdivisionFrame(sub, projected, indices);

	return DrawLevelOvr1P_DispatchFullDynamicHelperSequence(pb, primMem, block, sub, handlerAddress, texture, depth + 1);
}

static int DrawLevelOvr1P_EmitFullDynamicTerminalFaceSlotMode(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                              struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                              struct TextureLayout *texture, int depth, u32 allowedMask, int updateSlot)
{
	struct DrawLevelOvr1PFullDynamicRecursiveGate gate;

	if (updateSlot)
	{
		DrawLevelOvr1P_SetGridFaceSlot(projected, faceIndex);
	}

	// NOTE(aalhendi): Retail 0x800a18c0..0x800a19a8 owns the full-dynamic
	// packed-SXY cull, direct-mask fold, max-recursion direct handoff, and
	// recursive scratch 0x28 near-threshold selection.
	if (!Ovr226_800a18c0_FullDynamicRecursiveGate(projected, indices, allowedMask, &gate))
	{
		return 1;
	}

	if (gate.forceDirect)
	{
		return Ovr226_800a1cc4_EmitFullDynamicDeepestDirectTail(pb, primMem, block, projected, indices, faceIndex, texture);
	}

	return Ovr226_800a1be8_DispatchFullDynamicNearSubdivision(pb, primMem, block, projected, indices, faceIndex, texture, depth, gate.directMask, 0x28);
}

static int DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                                  struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                  struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFaceSlotMode(pb, primMem, block, projected, indices, faceIndex, texture, depth, allowedMask, 0);
}

static int Ovr226_800a19a8_SelectFullDynamicDirectSlot(struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int *indices,
                                                       struct TextureLayout **texture, u32 *directMask)
{
	u32 tableWord = DrawLevelOvr1P_Select4x1ProjectedFace(projected, block, faceIndex, indices);

	if (DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(projected, indices, 4))
	{
		return 0;
	}

	*directMask = DrawLevelOvr1P_SelectDirectMask(block, projected, indices, tableWord, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
	DrawLevelOvr1P_Scratch()->directMask = *directMask;
	if (*directMask == 0)
	{
		return 0;
	}

	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, indices);
	DrawLevelOvr1P_SetFullDynamicInheritedOtIndex(block, projected, maxDepth, faceIndex);
	*texture = DrawLevelOvr1P_GetProjectedMidTexture(block, projected, faceIndex, maxDepth);

	if (*texture != NULL)
	{
		DrawLevelOvr1P_WriteProjectedUv(projected, indices, *texture, tableWord);
	}

	return 1;
}

static int DrawLevelOvr1P_EmitFullDynamicSelectorFace(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                      struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int depth)
{
	struct TextureLayout *texture = NULL;
	int indices[4];
	u32 directMask;

	// NOTE(aalhendi): Retail 0x800a19a8..0x800a1be8 selects the top-level
	// full-dynamic face slot, rejects empty direct masks before texture/UV
	// selection, and uses scratch 0x24 for the first near-mask handoff.
	if (!Ovr226_800a19a8_SelectFullDynamicDirectSlot(block, projected, faceIndex, indices, &texture, &directMask))
	{
		return 1;
	}

	return Ovr226_800a1be8_DispatchFullDynamicNearSubdivision(pb, primMem, block, projected, indices, faceIndex, texture, depth, directMask, 0x24);
}

static int Ovr226_800a1534_EmitFullDynamicFace0(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], 0, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a1548_EmitFullDynamicFace1(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], 1, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a155c_EmitFullDynamicFace2(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], 2, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a1570_EmitFullDynamicFace3(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], 3, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a1584_EmitFullDynamicExtraFace0(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], 0, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a1598_EmitFullDynamicExtraFace1(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], 1, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a15ac_EmitFullDynamicExtraFace2(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], 2, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a15c0_EmitFullDynamicExtraFace3(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth, u32 allowedMask)
{
	return DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], 3, texture, depth,
	                                                              allowedMask);
}

static int Ovr226_800a15d4_FullDynamicHelperSlot0(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1534_EmitFullDynamicFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a1614_FullDynamicHelperSlot1(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a15ac_EmitFullDynamicExtraFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a15c0_EmitFullDynamicExtraFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a1634_FullDynamicHelperSlot3(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a1584_EmitFullDynamicExtraFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1598_EmitFullDynamicExtraFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a1654_FullDynamicHelperSlot7(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], 0, texture, depth,
	                                                            DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!DrawLevelOvr1P_EmitFullDynamicTerminalFacePreserveSlot(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], 0, texture, depth,
	                                                            DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1570_EmitFullDynamicFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a1694_FullDynamicHelperSlot2(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a15c0_EmitFullDynamicExtraFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY))
	{
		return 0;
	}
	if (!Ovr226_800a1534_EmitFullDynamicFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a16bc_FullDynamicHelperSlot4(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a1598_EmitFullDynamicExtraFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY))
	{
		return 0;
	}
	if (!Ovr226_800a1534_EmitFullDynamicFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a16e4_FullDynamicHelperSlot9(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                  struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a15ac_EmitFullDynamicExtraFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY))
	{
		return 0;
	}
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1570_EmitFullDynamicFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a170c_FullDynamicHelperSlot11(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                   struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (!Ovr226_800a1584_EmitFullDynamicExtraFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY))
	{
		return 0;
	}
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	return Ovr226_800a1570_EmitFullDynamicFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int Ovr226_800a1734_FullDynamicDefaultHelper(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                    struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture, int depth)
{
	if (projected == CTR_SCRATCHPAD_PTR(struct DrawLevelOvr1PScratchVertex, DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET))
	{
		for (int faceIndex = 0; faceIndex < 4; faceIndex++)
		{
			if (!DrawLevelOvr1P_EmitFullDynamicSelectorFace(pb, primMem, block, projected, faceIndex, depth))
			{
				return 0;
			}
		}

		return 1;
	}

	DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
	if (!Ovr226_800a1534_EmitFullDynamicFace0(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 1);
	if (!Ovr226_800a1548_EmitFullDynamicFace1(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 2);
	if (!Ovr226_800a155c_EmitFullDynamicFace2(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 3);
	return Ovr226_800a1570_EmitFullDynamicFace3(pb, primMem, block, projected, texture, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
}

static int DrawLevelOvr1P_DispatchFullDynamicHelperSequence(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                            struct DrawLevelOvr1PScratchVertex *projected, DrawLevelOvrRetailLabel handlerAddress,
                                                            struct TextureLayout *texture, int depth)
{
	switch (handlerAddress)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT0_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT0_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT0_229:
		return Ovr226_800a15d4_FullDynamicHelperSlot0(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT1_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT1_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT1_229:
		return Ovr226_800a1614_FullDynamicHelperSlot1(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT3_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT3_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT3_229:
		return Ovr226_800a1634_FullDynamicHelperSlot3(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT7_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT7_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT7_229:
		return Ovr226_800a1654_FullDynamicHelperSlot7(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT2_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT2_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT2_229:
		return Ovr226_800a1694_FullDynamicHelperSlot2(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT4_229:
		return Ovr226_800a16bc_FullDynamicHelperSlot4(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT9_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT9_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT9_229:
		return Ovr226_800a16e4_FullDynamicHelperSlot9(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT11_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT11_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_SLOT11_229:
		return Ovr226_800a170c_FullDynamicHelperSlot11(pb, primMem, block, projected, texture, depth);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_DEFAULT_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_DEFAULT_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_HELPER_DEFAULT_229:
		return Ovr226_800a1734_FullDynamicDefaultHelper(pb, primMem, block, projected, texture, depth);
	default:
		return 1;
	}
}

static u32 Ovr226_800a10dc_SelectFullDynamicTopNearMask(const struct DrawLevelOvr1PScratchVertex *projected)
{
	return DrawLevelOvr1P_GetProjectedNearMaskAtScratchOffset(projected, sDrawLevelOvr1PFullDynamicLowIndices, 0x14);
}

static int Ovr226_800a1338_DispatchFullDynamicLowDirect(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                        struct DrawLevelOvr1PScratchVertex *projected, struct TextureLayout *texture)
{
	const int *indices = sDrawLevelOvr1PFullDynamicLowIndices;
	u32 directMask;

	if (DrawLevelOvr1P_IsProjectedPolyOffscreenPacked(projected, indices, 4))
	{
		return 1;
	}

	directMask = DrawLevelOvr1P_SelectDirectMask(block, projected, indices, 0, DRAW_LEVEL_OVR1P_DIRECT_QUAD);
	DrawLevelOvr1P_Scratch()->directMask = directMask;
    if (directMask == 0)
    {
        return 1;
    }
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    if (DrawLevelOvr1P_FarLowDirectStressEnabled() &&
        NativePerf_IsStressTestMeasuring() && NativeRenderer_LevelPCEnabled() &&
        texture != NULL && ((texture->tpage>>7)&3)<3 &&
        DrawLevelOvr1P_GetDirectHandlerAddress(directMask) ==
             DRAW_LEVEL_OVR_RETAIL_LABEL_FULL_DYNAMIC_DIRECT_GT4_226)
    {
        const uintptr_t base=(uintptr_t)sDrawLevelOvr1P_CompiledQuadBase;
        const uintptr_t ptr=(uintptr_t)block;
        if (base != 0 && ptr >= base &&
            (ptr-base)%sizeof(*block)==0)
        {
            const size_t idx=(ptr-base)/sizeof(*block);
            if (idx < (size_t)sDrawLevelOvr1P_CompiledQuadCount &&
                sDrawLevelOvr1P_CompiledVertices != NULL &&
                sDrawLevelOvr1P_CompiledVertices[idx].valid &&
                NativeRenderer_LevelPCQueueBlock(sDrawLevelOvr1P_CompiledQuadBase, (int)idx))
            {
                static unsigned long accepted;
                if ((++accepted & 2047ul)==0)
                    Platform_Log("[LEVEL PC GPU] far-LOD direct GT4 queued=%lu\n",accepted);
                return 1; /* Never emit the matching retail OT quad twice. */
            }
        }
    }
#endif
    return Ovr226_800a1cc4_EmitFullDynamicDirectTail(pb, primMem, block, projected, indices, -1, texture);
}

static void Ovr226_800a11e0_ProjectFullDynamicLastPair(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected)
{
	struct LevVertex *vertex7 = &vertices[block->index[7]];
	struct LevVertex *vertex8 = &vertices[block->index[8]];
	s16 unusedSxy[2];
	u32 depth7;
	u32 depth8;
	u32 unusedDepth;

	DrawLevelOvr1P_CopySourceVertex(&projected[7], vertex7);
	DrawLevelOvr1P_CopySourceVertex(&projected[8], vertex8);

	// NOTE(aalhendi): Retail 0x800a11e0 uses ldv01, preserving V2 from vertex 6.
	MTC2(DrawLevelOvr1P_ReadPackedWord(&vertex7->pos.x), 0);
	MTC2(DrawLevelOvr1P_ReadPackedWord(&vertex7->pos.z), 1);
	MTC2(DrawLevelOvr1P_ReadPackedWord(&vertex8->pos.x), 2);
	MTC2(DrawLevelOvr1P_ReadPackedWord(&vertex8->pos.z), 3);
	gte_rtpt();
	CTR_GteStoreSXY3(&projected[7].posScreen[0], &projected[8].posScreen[0], &unusedSxy[0]);
	gte_stsz3(&depth7, &depth8, &unusedDepth);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[7], depth7);
	DrawLevelOvr1P_StoreProjectedDepthWord(&projected[8], depth8);
}

static void Ovr226_800a1128_ProjectFullDynamicHighGrid(struct LevVertex *vertices, const struct QuadBlock *block, struct DrawLevelOvr1PScratchVertex *projected)
{
	Ovr226_800a0f78_ProjectVertexTripleFullDepth(vertices, block, projected, 4, 5, 6);
	Ovr226_800a11e0_ProjectFullDynamicLastPair(vertices, block, projected);
}

static int Ovr226_800a1128_DispatchFullDynamicTransitionGrid(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                             struct LevVertex *vertices, struct DrawLevelOvr1PScratchVertex *projected, u32 nearMask,
                                                             struct TextureLayout *texture)
{
	Ovr226_800a1128_ProjectFullDynamicHighGrid(vertices, block, projected);
	DrawLevelOvr1P_AdjustFullDynamicMidVertices(projected, vertices, block);
	DrawLevelOvr1P_AdjustFullDynamicMidUvs(projected);

	return DrawLevelOvr1P_DispatchFullDynamicHelperSequence(
	    pb, primMem, block, projected, DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST), texture, 0);
}

static int Ovr226_800a0f78_EmitFullDynamicQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block, struct LevVertex *vertices)
{
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	Ovr226_800a0f78_ProjectFullDynamicLowQuad(vertices, block, projected);
	struct TextureLayout *texture = Ovr226_800a1058_PrepareFullDynamicLowUv(block, projected);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, sDrawLevelOvr1PFullDynamicLowIndices);
	DrawLevelOvr1P_SetFullDynamicInheritedOtIndex(block, projected, maxDepth, -1);
	u32 nearMask = Ovr226_800a10dc_SelectFullDynamicTopNearMask(projected);

	if (nearMask == 0)
	{
		return Ovr226_800a1338_DispatchFullDynamicLowDirect(pb, primMem, block, projected, texture);
	}

	return Ovr226_800a1128_DispatchFullDynamicTransitionGrid(pb, primMem, block, vertices, projected, nearMask, texture);
}

static void Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch(const int *visFaceList, const struct QuadBlock *block)
{
	u32 blockID = (u16)block->blockID;
	const u32 *word = (const u32 *)((const u8 *)visFaceList + ((blockID >> 3) & 0x1fc));

	DrawLevelOvr1P_Scratch()->visibilityWordPtr32 = (u32)(uintptr_t)word;
	DrawLevelOvr1P_Scratch()->visibilityBitIndex = blockID & 0x1f;
	DrawLevelOvr1P_Scratch()->visibilityWord = *word;
}

static int Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit(void)
{
	s32 bitIndex = DrawLevelOvr1P_Scratch()->visibilityBitIndex;
	u32 word = DrawLevelOvr1P_Scratch()->visibilityWord;
	u32 shifted;

	if (bitIndex < 0)
	{
		u32 *wordPtr = (u32 *)(uintptr_t)DrawLevelOvr1P_Scratch()->visibilityWordPtr32;

		bitIndex = 0x1f;
		word = wordPtr[1];
		wordPtr++;
		DrawLevelOvr1P_Scratch()->visibilityWordPtr32 = (u32)(uintptr_t)wordPtr;
		DrawLevelOvr1P_Scratch()->visibilityWord = word;
	}

	shifted = word << bitIndex;
	DrawLevelOvr1P_Scratch()->visibilityBitIndex = bitIndex - 1;
	return (s32)shifted < 0;
}

static int Ovr226_800a0ef4_DrawFullDynamicBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                  const int *visFaceList)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const int nativeStressMeasuring = NativePerf_IsStressTestMeasuring();
#endif

	while (slot != NULL)
	{
		struct BSP *bsp = slot->bsp;
		struct QuadBlock *block = bsp->data.leaf.ptrQuadBlockArray;
		s32 quadCount = bsp->data.leaf.numQuads;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		const int nativeStressLeafFullyInside =
			nativeStressMeasuring && (quadCount >= 4) &&
			RenderLists_NativeBoxFullyInsideCameraFrustum(pb, &bsp->box);
#endif

		DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		if (quadCount > 0)
		{
			Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch(visFaceList, block);
		}

		while (quadCount > 0)
		{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			if (nativeStressMeasuring)
			{
				if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit() &&
				    (nativeStressLeafFullyInside ||
				     DrawLevelOvr1P_NativeCameraAllowsQuadBlock(pb, block)))
				{
					if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_FULL_DYNAMIC))
						return 0;
					if (!Ovr226_800a0f78_EmitFullDynamicQuadBlock(pb, primMem, block, vertices))
						return 0;
				}
			}
			else
#endif
			{
				if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_FULL_DYNAMIC))
					return 0;

				if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit())
				{
					if (!Ovr226_800a0f78_EmitFullDynamicQuadBlock(pb, primMem, block, vertices))
						return 0;
				}
			}

			block++;
			quadCount--;
			DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		}

		slot = slot->next;
	}

	return 1;
}

static void DrawLevelOvr1P_SetSplitGroundThresholdScratch(void)
{
	DrawLevelOvr1P_RenderScratch()->depthScale = 0x780;
	DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold0 = 0x640;
	DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold1 = 0x500;
	DrawLevelOvr1P_RenderScratch()->topLevelNearDepthThreshold = 0x280;
	DrawLevelOvr1P_RenderScratch()->recursiveNearDepthThreshold = 0x140;
}

static int DrawLevelOvr1P_ProjectSplitGroundListALowGrid(struct LevVertex *vertices, const struct QuadBlock *block,
                                                         struct DrawLevelOvr1PScratchVertex *projected)
{
	DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
	if (DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 0, 1, 2, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, NULL))
	{
		return 1;
	}

	DrawLevelOvr1P_SetActiveDrawOrderLow(block);
	return DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 3, 4, 5, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, NULL);
}

static int DrawLevelOvr1P_ProjectSplitGroundListATransitionGrid(struct LevVertex *vertices, const struct QuadBlock *block,
                                                                struct DrawLevelOvr1PScratchVertex *projected)
{
	if (DrawLevelOvr1P_ProjectListVertexTriple(vertices, block, projected, 6, 7, 8, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, NULL))
	{
		return 1;
	}

	DrawLevelOvr1P_AdjustFullDynamicMidUvs(projected);
	return 0;
}

static int DrawLevelOvr1P_EmitSplitGroundListAQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectSplitGroundListALowGrid(vertices, block, projected))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	struct TextureLayout *texture = Ovr226_800a1058_PrepareFullDynamicLowUv(block, projected);
	u32 maxDepth = DrawLevelOvr1P_GetProjectedMaxDepth(projected, sDrawLevelOvr1PFullDynamicLowIndices);
	DrawLevelOvr1P_SetFullDynamicInheritedOtIndex(block, projected, maxDepth, -1);
	u32 nearMask = Ovr226_800a10dc_SelectFullDynamicTopNearMask(projected);
	if (nearMask == 0)
	{
		return Ovr226_800a1338_DispatchFullDynamicLowDirect(pb, primMem, block, projected, texture);
	}

	if (DrawLevelOvr1P_ProjectSplitGroundListATransitionGrid(vertices, block, projected))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	return DrawLevelOvr1P_DispatchFullDynamicHelperSequence(
	    pb, primMem, block, projected, DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST), texture, 0);
}

static int DrawLevelOvr1P_DrawSplitGroundListABspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                      const int *visFaceList)
{
	DrawLevelOvr1P_SetSplitGroundThresholdScratch();
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const int nativeStressMeasuring = NativePerf_IsStressTestMeasuring();
#endif

	while (slot != NULL)
	{
		struct BSP *bsp = slot->bsp;
		struct QuadBlock *block = bsp->data.leaf.ptrQuadBlockArray;
		s32 quadCount = bsp->data.leaf.numQuads;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		const int nativeStressLeafFullyInside =
			nativeStressMeasuring && (quadCount >= 4) &&
			RenderLists_NativeBoxFullyInsideCameraFrustum(pb, &bsp->box);
#endif

		DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch(visFaceList, block);

		while (quadCount > 0)
		{
			if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT))
				return 0;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			if (nativeStressMeasuring)
			{
				if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit() &&
				    (nativeStressLeafFullyInside ||
				     DrawLevelOvr1P_NativeCameraAllowsQuadBlock(pb, block)))
				{
					if (!DrawLevelOvr1P_EmitSplitGroundListAQuadBlock(pb, primMem, mesh, block))
						return 0;
				}
			}
			else
#endif
			if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit())
			{
				if (!DrawLevelOvr1P_EmitSplitGroundListAQuadBlock(pb, primMem, mesh, block))
					return 0;
			}

			block++;
			quadCount--;
			DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		}

		slot = slot->next;
	}

	DrawLevelOvr1P_TerminateRenderedListCursor();
	return 1;
}

static int Ovr226_800a3738_EmitGround4x1ListQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectListGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_FACE))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	for (int faceIndex = 0; faceIndex < 4; faceIndex++)
	{
		if (!DrawLevelOvr1P_Emit4x1ListSelectedFace(pb, primMem, projected, block, faceIndex))
		{
			return 0;
		}
	}

	return 1;
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Turbocharged-style native level OT insertion: stress-only test.
 * The B5 primitive carries one command word (0xB5000000) and is parsed by
 * ProcessPsyXPrims before the normal later-frame PS1 splits. B3 is reserved
 * for our native modular OBJ packets, so DO NOT reuse upstream's B3 here.
 * This marker must be armed before ANY retail terrain can be bypassed. */
static int sDrawLevelOvr1P_NativeGpuOtMarkerArmed;

/* Human-controlled QA path, restricted to Oxide Station single-player.
 * In every other scene or multiplayer session, retail rendering is retained.
 * This is NOT the production native renderer. */
static int DrawLevelOvr1P_NativeGpuManualPlaytestAllowed(void)
{
    const struct GameTracker *gt=(sdata!=NULL)?sdata->gGT:NULL;
    return NativeRenderer_LevelPCManualPlaytest() && gt != NULL &&
           gt->levelID == OXIDE_STATION && gt->numPlyrCurrGame == 1 &&
           (gt->gameMode1 & (MAIN_MENU | GAME_CUTSCENE)) == 0;
}

static int DrawLevelOvr1P_NativeGpuOtMarkerRequested(void)
{
    static int checked, requested;
    if (!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_ot_marker_stress.enable","rb");
        requested=(f!=NULL);
        if(f)fclose(f);
        checked=1;
    }
    return requested &&
           (NativePerf_IsStressTestMeasuring() ||
            DrawLevelOvr1P_NativeGpuManualPlaytestAllowed()) &&
           NativeRenderer_LevelPCEnabled();
}

/* Test-only native GPU terrain bypass. Numeric marker:
 * debug/level_pc_gpu_replace_stress.txt
 * 1=DYNAMIC, 2=GROUND_4X1, 3=both. Other buckets retain retail PS1.
 * Requires the native material shader opt-in and an ACTIVE 8-bot stress
 * measurement. Never run this bypass during player gameplay.
 * When the queue is full/unavailable ALWAYS fall back to retail packets. */
static int DrawLevelOvr1P_NativeGpuBypassRole(const struct mesh_info *mesh, int role)
{
	static int checked=0,mode=0;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_gpu_replace_stress.txt","rb");
		if (f)
		{
			int parsed=0;
			if (fscanf(f,"%d",&parsed)==1 && parsed>=0 && parsed<=3)
				mode=parsed;
			fclose(f);
		}
		checked=1;
		if (mode)
			Platform_Log("[LEVEL PC GPU] stress-only PS1 terrain bypass mask=%d\n",mode);
	}
	/* No marker in the OT means any skipped polygons would disappear.
	 * Preserve the retail path if marker injection was requested but failed. */
	if ((DrawLevelOvr1P_NativeGpuOtMarkerRequested() &&
	     !sDrawLevelOvr1P_NativeGpuOtMarkerArmed) ||
	    !mode || !(NativePerf_IsStressTestMeasuring() ||
	               DrawLevelOvr1P_NativeGpuManualPlaytestAllowed()) ||
	    !NativeRenderer_LevelPCEnabled() || mesh==NULL ||
	    mesh->ptrQuadBlockArray==NULL)
		return 0;
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED:
		return (mode & 1)!=0;
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
		return (mode & 2)!=0;
	default:
		return 0;
	}
}
/* Conservative stress-only near-camera fallback for the resident GPU mesh.
 * The native quad has no PS1 near subdivision, so preserve retail rendering
 * whenever ANY of its original nine vertices is too close to the camera. */
static int DrawLevelOvr1P_NativeGpuNearFallback(
    const struct mesh_info *mesh,const struct QuadBlock *block)
{
    static int checked,threshold;
    if(!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_near_fallback_stress.txt","rb");
        if(f)
        {
            if(fscanf(f,"%d",&threshold)!=1 || threshold<1 || threshold>5000)
                threshold=0;
            fclose(f);
        }
        checked=1;
        if(threshold)
            Platform_Log("[LEVEL PC GPU] stress near-zone fallback minZ=%d\n",threshold);
    }
    if(!threshold || !(NativePerf_IsStressTestMeasuring() ||
                     DrawLevelOvr1P_NativeGpuManualPlaytestAllowed()))return 0;
    for(int j=0;j<9;++j)
    {
        const u32 vi=block->index[j];
        if(vi>=(u32)mesh->numVertex)return 1;
        const struct LevVertex *v=&mesh->ptrVertexArray[vi];
        const float z=NativeRenderer_LevelPCWorldDepthQA(
            (float)v->pos.x,(float)v->pos.y,(float)v->pos.z);
        if(!(z>=(float)threshold))return 1;
    }
    return 0;
}

/* The GPU VBO uses the first (far) texture layout for every face.
 * Retail selects ptr_texture_mid[face]+1/+2 as a function of EACH face's
 * maximum projected depth. Passing a block whose faces select different
 * textures causes wrong tunnel/wall colors even when vertices are correct.
 * This extra selection check is QA-only; it never changes retail physics,
 * collisions, geometry, or the installed executable.
 *
 * Unlike a global minZ>=1600 cutoff, this checks each face's actual four
 * vertices so an adjacent face can remain eligible when its furthest
 * corner is beyond retail's far texture LOD threshold. */
static int DrawLevelOvr1P_NativeGpuLodGuard(
    const struct mesh_info *mesh, const struct QuadBlock *block)
{
    static int checked,enabled;
    static unsigned long examined,excludedLod,excludedInvalid;
    if (!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_lod_guard.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(enabled)
            Platform_Log("[LEVEL PC GPU] native face LOD-safe fallback enabled (test only)\n");
    }
    if(!enabled)return 0;

    /* RenderScratch is populated by the retail ground passes. Fail closed
     * rather than using a stale threshold from a custom level. */
    const s32 threshold=DrawLevelOvr1P_RenderScratch()->textureLodDepthThreshold0;
    const uintptr_t base=(uintptr_t)sDrawLevelOvr1P_CompiledQuadBase;
    const uintptr_t addr=(uintptr_t)block;
    const size_t count=(size_t)sDrawLevelOvr1P_CompiledQuadCount;
    if(mesh==NULL || block==NULL ||
       sDrawLevelOvr1P_CompiledVertices==NULL || base==0 ||
       mesh->ptrQuadBlockArray!=sDrawLevelOvr1P_CompiledQuadBase ||
       mesh->ptrVertexArray!=sDrawLevelOvr1P_CompiledVertexBase ||
       addr<base || (addr-base)%sizeof(*block)!=0 ||
       (addr-base)/sizeof(*block)>=count ||
       threshold<=0 || threshold>65535)
    {
        ++excludedInvalid;
        return 1;
    }
    const size_t idx=(addr-base)/sizeof(*block);
    const struct DrawLevelOvr1PCompiledVertices *entry=
        &sDrawLevelOvr1P_CompiledVertices[idx];
    if(!entry->valid || entry->nativeLodFaceMask!=0xfu)
    {
        ++excludedInvalid;
        return 1;
    }

    float z[9];
    for(int i=0;i<9;++i)
    {
        const struct LevVertex *v=entry->v[i];
        if(v==NULL){++excludedInvalid;return 1;}
        z[i]=NativeRenderer_LevelPCWorldDepthQA(
             (float)v->pos.x,(float)v->pos.y,(float)v->pos.z);
    }
    ++examined;
    /* PS1 GTE SZ is integer and can saturate near the LOD boundary:
     * retain the retail path in a small 32-unit safety margin. */
    const float safeDepth=(float)threshold+32.0f;
    for(int f=0;f<4;++f)
    {
        float maxDepth=-1.0e20f;
        for(int c=0;c<4;++c)
        {
            const u8 vi=entry->nativeFaceCorner[f][c];
            if(vi>=9){++excludedInvalid;return 1;}
            if(z[vi]>maxDepth)maxDepth=z[vi];
        }
        if(!(maxDepth>=safeDepth))
        {
            ++excludedLod;
            if((examined&8191ul)==0)
                Platform_Log("[LEVEL PC GPU] LOD guard blocks=%lu retailLOD=%lu invalid=%lu\n",
                             examined,excludedLod,excludedInvalid);
            return 1;
        }
    }
    return 0;
}

static int DrawLevelOvr1P_NativeGpuBypassTryQueue(
	const struct mesh_info *mesh,const struct QuadBlock *block)
{
	static int groundOnlyChecked, groundOnly;
	if (!groundOnlyChecked)
	{
		FILE *flag=fopen("debug/level_pc_gpu_ground_only_stress.enable","rb");
		groundOnly=(flag!=NULL);
		if (flag) fclose(flag);
		groundOnlyChecked=1;
		if (groundOnly)
			Platform_Log("[LEVEL PC GPU] stress bypass: GROUND blocks only\\n");
	}
	if (block < mesh->ptrQuadBlockArray || 
	    block >= mesh->ptrQuadBlockArray + mesh->numQuadBlock)
		return 0;
	if (groundOnly && !(block->quadFlags & QUADBLOCK_FLAG_GROUND))return 0;
    if(DrawLevelOvr1P_NativeGpuNearFallback(mesh,block))return 0;
    if(DrawLevelOvr1P_NativeGpuLodGuard(mesh,block))return 0;
	return NativeRenderer_LevelPCQueueBlock(
		mesh->ptrQuadBlockArray,(int)(block - mesh->ptrQuadBlockArray));
}
#endif

static u32 DrawLevelOvr1P_GetBspListReserve(int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X1;

	case DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X2;

	default:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT;
	}
}

static u32 DrawLevelOvr1P_GetNonWaterRenderedListReserve(int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X1;

	case DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_4X2;

	default:
		return DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT;
	}
}

static enum DrawLevelOvr1PGridSlotMode DrawLevelOvr1P_GetNonWaterRenderedListSlotMode(int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
	case DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED:
		return DRAW_LEVEL_OVR1P_GRID_SLOT_FACE;

	default:
		return DRAW_LEVEL_OVR1P_GRID_SLOT_WORD;
	}
}

static int DrawLevelOvr1P_NonWaterRenderedSelectorNearGate(struct PushBuffer *pb, struct PrimMem *primMem, struct DrawLevelOvr1PScratchVertex *projected,
                                                           struct QuadBlock *block, int faceIndex, int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
		return Ovr226_800a47f4_Ground4x1RenderedSelectorNearGate(pb, primMem, projected, block, faceIndex);

	case DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED:
		return Ovr226_800a6740_Ground4x2RenderedSelectorNearGate(pb, primMem, projected, block, faceIndex);

	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED:
		return Ovr226_800a8380_DynamicRenderedSelectorNearGate(pb, primMem, projected, block, faceIndex);

	default:
		return Ovr226_800a9fa0_Quad4x4RenderedSelectorNearGate(pb, primMem, projected, block, faceIndex);
	}
}

static int DrawLevelOvr1P_DrawNonWaterRenderedList(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                   int role)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();
	u32 reserve = DrawLevelOvr1P_GetNonWaterRenderedListReserve(role);
	enum DrawLevelOvr1PGridSlotMode slotMode = DrawLevelOvr1P_GetNonWaterRenderedListSlotMode(role);

	if (renderedList == NULL)
	{
		return 1;
	}

	while (1)
	{
		struct QuadBlock *block = *renderedList;

		if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, reserve))
		{
			return 0;
		}

		DrawLevelOvr1P_SetGridFaceSlotWord(projected, 0);
		if (block == NULL)
		{
			return 1;
		}
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		if (DrawLevelOvr1P_NativeGpuBypassRole(mesh, role) &&
		    DrawLevelOvr1P_NativeGpuBypassTryQueue(mesh,block))
		{
			renderedList++;
			continue;
		}
#endif
		DrawLevelOvr1P_ProjectRenderedGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, slotMode);

		for (int faceIndex = 0; faceIndex < 4; faceIndex++)
		{
			if (!DrawLevelOvr1P_NonWaterRenderedSelectorNearGate(pb, primMem, projected, block, faceIndex, role))
			{
				return 0;
			}
		}

		renderedList++;
	}
}

static int Ovr226_800a5030_EmitGround4x2ListQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectListGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_FACE))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	for (int faceIndex = 0; faceIndex < 4; faceIndex++)
	{
		if (!Ovr226_800a58ec_Ground4x2SelectorNearGate(pb, primMem, projected, block, faceIndex))
		{
			return 0;
		}
	}

	return 1;
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Non-shipping diagnostic: quantify GTE projection vs dynamic face writer.
 * If debug/level_dynamic_subprofile.enable is absent, normal retail path is unchanged. */
static int DrawLevelOvr1P_DynamicListProfileEnabled(void)
{
    static int checked, enabled;
    if (!checked)
    {
        FILE *f=fopen("debug/level_dynamic_subprofile.enable","rb");
        enabled=(f!=NULL);
        if (f) fclose(f);
        checked=1;
        if (enabled) Platform_Log("[DYN PROFILE] experimental timing active for DYNAMIC_LIST only\n");
    }
    return enabled && NativePerf_IsStressTestMeasuring();
}
static int Ovr226_800a6fd0_ProfiledDynamicListQuadBlock(
    struct PushBuffer *pb, struct PrimMem *primMem,
    struct mesh_info *mesh, struct QuadBlock *block)
{
    static u64 projectionNs, faceNs;
    static u32 blocks, overflowBlocks, failedBlocks;
    struct LevVertex *vertices=mesh->ptrVertexArray;
    struct DrawLevelOvr1PScratchVertex *projected=DrawLevelOvr1P_GetScratchVertices();
    const u64 t0=SDL_GetTicksNS();
    const int overflow=DrawLevelOvr1P_ProjectListGrid(
        vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS,
        DRAW_LEVEL_OVR1P_GRID_SLOT_WORD);
    const u64 t1=SDL_GetTicksNS();
    int result=1;
    if (overflow)
    {
        DrawLevelOvr1P_AppendRenderedQuadBlock(block);
        ++overflowBlocks;
    }
    else
    {
        for (int faceIndex=0;faceIndex<4;++faceIndex)
        {
            if(!Ovr226_800a7668_DynamicListSelectorNearGate(
                pb,primMem,projected,block,faceIndex))
            {
                result=0;
                ++failedBlocks;
                break;
            }
        }
    }
    const u64 t2=SDL_GetTicksNS();
    projectionNs+=t1-t0;
    faceNs+=t2-t1;
    if (++blocks>=10000u)
    {
        Platform_Log("[DYN PROFILE] blocks=%u projectMs=%.3f facesMs=%.3f overflow=%u failed=%u\n",
            blocks,(double)projectionNs/1000000.0,
            (double)faceNs/1000000.0,overflowBlocks,failedBlocks);
        blocks=0; projectionNs=0; faceNs=0;overflowBlocks=0;failedBlocks=0;
    }
    return result;
}
#endif

static int Ovr226_800a6fd0_EmitDynamicListQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    if (DrawLevelOvr1P_DynamicListProfileEnabled())
        return Ovr226_800a6fd0_ProfiledDynamicListQuadBlock(pb,primMem,mesh,block);
#endif
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectListGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_WORD))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	for (int faceIndex = 0; faceIndex < 4; faceIndex++)
	{
		if (!Ovr226_800a7668_DynamicListSelectorNearGate(pb, primMem, projected, block, faceIndex))
		{
			return 0;
		}
	}

	return 1;
}

static int Ovr226_800a8bf0_EmitWideDynamicQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectListGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_POS_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_WORD))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	for (int faceIndex = 0; faceIndex < 4; faceIndex++)
	{
		if (!Ovr226_800a9288_WideDynamicSelectorNearGate(pb, primMem, projected, block, faceIndex))
		{
			return 0;
		}
	}

	return 1;
}

static int DrawLevelOvr1P_EmitBspListQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block, int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST:
		return Ovr226_800a3738_EmitGround4x1ListQuadBlock(pb, primMem, mesh, block);

	case DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST:
		return Ovr226_800a5030_EmitGround4x2ListQuadBlock(pb, primMem, mesh, block);

	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST:
		return Ovr226_800a6fd0_EmitDynamicListQuadBlock(pb, primMem, mesh, block);

	default:
		return Ovr226_800a8bf0_EmitWideDynamicQuadBlock(pb, primMem, mesh, block);
	}
}

static int DrawLevelOvr1P_DrawBspListQuadBlocks(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                const int *visFaceList, int role)
{
	if (sDrawLevelOvr1P_ListHandlersSeedRenderedCursor)
	{
		DrawLevelOvr1P_SetRenderedListCursor(DrawLevelOvr1P_GetRenderedOverflowBase());
	}

	u32 reserve = DrawLevelOvr1P_GetBspListReserve(role);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    /* During manual 1P Oxide playtest retain the same conservative visibility
     * and bypass branch already validated by the automatic stress QA. */
	const int nativeStressMeasuring = NativePerf_IsStressTestMeasuring() ||
        DrawLevelOvr1P_NativeGpuManualPlaytestAllowed();
#endif

	while (slot != NULL)
	{
		struct BSP *bsp = slot->bsp;
		struct QuadBlock *block = bsp->data.leaf.ptrQuadBlockArray;
		s32 quadCount = bsp->data.leaf.numQuads;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		const int nativeStressLeafFullyInside =
			nativeStressMeasuring && (quadCount >= 4) &&
			RenderLists_NativeBoxFullyInsideCameraFrustum(pb, &bsp->box);
#endif

		DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch(visFaceList, block);

		while (quadCount > 0)
		{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			if (nativeStressMeasuring)
			{
				if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit() &&
				    (nativeStressLeafFullyInside ||
				     DrawLevelOvr1P_NativeCameraAllowsQuadBlock(pb, block)))
				{
					if (!(DrawLevelOvr1P_NativeGpuBypassRole(mesh, role) &&
					      DrawLevelOvr1P_NativeGpuBypassTryQueue(mesh,block)))
					{
						if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, reserve))
							return 0;
						if (!DrawLevelOvr1P_EmitBspListQuadBlock(pb, primMem, mesh, block, role))
							return 0;
					}
				}
			}
			else
#endif
			{
				if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, reserve))
					return 0;

				if (Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit())
				{
					if (!DrawLevelOvr1P_EmitBspListQuadBlock(pb, primMem, mesh, block, role))
						return 0;
				}
			}

			block++;
			quadCount--;
			DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		}

		slot = slot->next;
	}

	DrawLevelOvr1P_TerminateRenderedListCursor();
	return 1;
}

static struct QuadBlock **DrawLevelOvr1P_GetRenderedListForRole(struct DrawLevelOvr1PRenderList *renderList, int role)
{
	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_FULL_DYNAMIC_LIST:
		// NOTE(aalhendi): Retail 0x800a0e7c clears 0x80096404 for every
		// empty bucket, including full-dynamic which has no rendered slot.
		return DrawLevelOvr1P_GetRenderedOverflowBase();
	case DRAW_LEVEL_OVR1P_BUCKET_4X4_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X4_RENDERED:
		return renderList->list[0].ptrQuadBlocksRendered;
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED:
		return renderList->list[1].ptrQuadBlocksRendered;
	case DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED:
		return renderList->list[2].ptrQuadBlocksRendered;
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
		return renderList->list[3].ptrQuadBlocksRendered;
	case DRAW_LEVEL_OVR1P_BUCKET_WATER_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_WATER_RENDERED:
		return renderList->list[4].ptrQuadBlocksRendered;
	default:
		return NULL;
	}
}

static void DrawLevelOvr1P_ClearRenderedListForRole(struct DrawLevelOvr1PRenderList *renderList, int role)
{
	struct QuadBlock **renderedList = DrawLevelOvr1P_GetRenderedListForRole(renderList, role);

	if (renderedList != NULL)
	{
		*renderedList = NULL;
	}
}

static u32 Ovr226_800a262c_SelectWaterDirectMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 allowedMask)
{
	u32 directMask = 0;

	// NOTE(aalhendi): Retail 0x800a261c..0x800a2658 tests full color words at
	// scratch vertex +0x8, then masks the two water GT3 direct bits through t2.
	if ((DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[0]]) | DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[1]]) |
	     DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[2]])) != 0)
	{
		directMask |= DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY;
	}

	if ((DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[1]]) | DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[2]]) |
	     DrawLevelOvr1P_GetProjectedColorWord(&projected[indices[3]])) != 0)
	{
		directMask |= DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY;
	}

	return directMask & allowedMask;
}

static u32 Ovr226_800a262c_SelectAndStoreWaterDirectMask(const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, u32 allowedMask)
{
	u32 directMask = Ovr226_800a262c_SelectWaterDirectMask(projected, indices, allowedMask);

	DrawLevelOvr1P_Scratch()->directMask = directMask;
	return directMask;
}

static void DrawLevelOvr1P_BuildGridSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                     const int *indices, int writeClipBytes)
{
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]], writeClipBytes);
}

static void Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                            const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	const u8 *srcAUv = (const u8 *)&srcA->flags;
	const u8 *srcBUv = (const u8 *)&srcB->flags;
	u8 *dstAUv = (u8 *)&dstA->flags;
	u8 *dstMidUv = (u8 *)&dstMid->flags;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstA->color_hi[colorChannel] = srcA->color_hi[colorChannel];
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	for (s32 uvByte = 0; uvByte < 2; uvByte++)
	{
		dstAUv[uvByte] = srcAUv[uvByte];
		dstMidUv[uvByte] = (u8)(((u32)srcAUv[uvByte] + (u32)srcBUv[uvByte]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_StoreProjectedDepthWord(dstMid, depth);
}

static void Ovr226_800a24e8_BuildWaterListSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                           const int *indices)
{
	Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a24e8_BuildWaterListSubdivideMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
}

static void Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(struct DrawLevelOvr1PScratchVertex *dstA, struct DrawLevelOvr1PScratchVertex *dstMid,
                                                                const struct DrawLevelOvr1PScratchVertex *srcA, const struct DrawLevelOvr1PScratchVertex *srcB)
{
	const u8 *srcAUv = (const u8 *)&srcA->flags;
	const u8 *srcBUv = (const u8 *)&srcB->flags;
	u8 *dstAUv = (u8 *)&dstA->flags;
	u8 *dstMidUv = (u8 *)&dstMid->flags;
	u32 depth;

	for (s32 axisIndex = 0; axisIndex < 3; axisIndex++)
	{
		dstA->pos[axisIndex] = srcA->pos[axisIndex];
		dstMid->pos[axisIndex] = (s16)(((s32)srcA->pos[axisIndex] + (s32)srcB->pos[axisIndex]) >> 1);
	}

	CTR_GteLoadS16TripletV0(&dstMid->pos[0]);
	gte_rtps();

	for (s32 colorChannel = 0; colorChannel < 3; colorChannel++)
	{
		dstA->color_hi[colorChannel] = srcA->color_hi[colorChannel];
		dstMid->color_hi[colorChannel] = (u8)(((u32)srcA->color_hi[colorChannel] + (u32)srcB->color_hi[colorChannel]) >> 1);
	}

	for (s32 uvByte = 0; uvByte < 2; uvByte++)
	{
		dstAUv[uvByte] = srcAUv[uvByte];
		dstMidUv[uvByte] = (u8)(((u32)srcAUv[uvByte] + (u32)srcBUv[uvByte]) >> 1);
	}

	DrawLevelOvr1P_CopyProjectedScreenDepth(dstA, srcA);
	CTR_GteStoreSXY(&dstMid->posScreen[0]);
	gte_stsz(&depth);
	DrawLevelOvr1P_SetProjectedDepth(dstMid, depth, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
}

static void Ovr226_800a2fe4_BuildWaterRenderedSubdivisionFrame(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                               const int *indices)
{
	Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]]);
	Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]]);
	Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]]);
	Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]]);
	Ovr226_800a2fe4_BuildWaterRenderedSubdivideMidpoint(&sub[1], &sub[6], &projected[indices[1]], &projected[indices[2]]);
}

static void DrawLevelOvr1P_BuildGridSubdivisionFrame4x4(struct DrawLevelOvr1PScratchVertex *sub, const struct DrawLevelOvr1PScratchVertex *projected,
                                                        const int *indices, int writeClipBytes)
{
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[0], &sub[4], &projected[indices[0]], &projected[indices[1]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[1], &sub[7], &projected[indices[1]], &projected[indices[3]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[2], &sub[5], &projected[indices[2]], &projected[indices[0]], writeClipBytes);
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[3], &sub[8], &projected[indices[3]], &projected[indices[2]], writeClipBytes);
	// NOTE(aalhendi): Retail 4x4 helpers build the center from edge midpoints.
	DrawLevelOvr1P_BuildMidpointFromFirstEndpoint(&sub[4], &sub[6], &sub[4], &sub[8], writeClipBytes);
}

static int Ovr226_800a25d0_WaterListFaceGate(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth, u32 allowedMask,
                                             uint32_t *inheritedOtEntry);

static const int *Ovr226_800a22a4_GetWaterListLeafWrapperIndices(DrawLevelOvrRetailLabel handlerLabel)
{
	// NOTE(aalhendi): Retail water-list leaf wrappers 0x800a22a4..0x800a2330
	// are contiguous 0x14-byte labels selecting Face[0..3], then Extra[0..3].
	if ((handlerLabel < DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_LEAF_FACE0) || (handlerLabel > DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_LEAF_EXTRA3))
	{
		return NULL;
	}

	u32 offset = handlerLabel - DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_LEAF_FACE0;
	if ((offset % DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_STRIDE) != 0)
	{
		return NULL;
	}

	u32 index = offset / DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_STRIDE;
	if (index < 4)
	{
		return sDrawLevelOvr1PGridFaceIndices[index];
	}

	return sDrawLevelOvr1PGridExtraFaceIndices[index - 4];
}

static int Ovr226_800a22a4_DispatchWaterListHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int depth,
                                                           DrawLevelOvrRetailLabel handlerLabel, int handlerSlot, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	int slot = handlerSlot;
	const int *leafIndices = Ovr226_800a22a4_GetWaterListLeafWrapperIndices(handlerLabel);

	if (leafIndices != NULL)
	{
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, leafIndices, faceIndex, depth, allowedMask, inheritedOtEntry);
	}

	switch (handlerLabel)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT0_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT0_ALIAS_B:
		slot = 0;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT1_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT1_ALIAS_B:
		slot = 1;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT2_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT2_ALIAS_B:
		slot = 2;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT3_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT3_ALIAS_B:
		slot = 3;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT4_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT4_ALIAS_B:
		slot = 4;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT7_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT7_ALIAS_B:
		slot = 7;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT9_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT9_ALIAS_B:
		slot = 9;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT11:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT11_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT11_ALIAS_B:
		slot = 11;
		break;
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT5:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT5_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT5_ALIAS_B:
		slot = 5;
		break;
	default:
		break;
	}

	// NOTE(aalhendi): Retail 0x800a22a4..0x800a24a4 wrappers are selected by
	// copied setup0 scratch addresses and pass their own t2 direct-mask
	// constants into shared face helper 0x800a25d0.
	switch (slot)
	{
	case 0:
		// 0x800a2344
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 1:
		// 0x800a2384
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 2:
		// 0x800a2404
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[3], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 3:
		// 0x800a23a4
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 4:
		// 0x800a242c
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[1], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 7:
		// 0x800a23c4
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 9:
		// 0x800a2454
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[2], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	case 11:
		// 0x800a247c
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridExtraFaceIndices[0], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], faceIndex, depth,
		                                       DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], faceIndex, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                       inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], faceIndex, depth,
		                                         DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	default:
		break;
	}

	// 0x800a24a4
	DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
	if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[0], 0, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
	                                       inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 1);
	if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[1], 1, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
	                                       inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 2);
	if (!Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[2], 2, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
	                                       inheritedOtEntry))
	{
		return 0;
	}
	DrawLevelOvr1P_SetGridFaceSlot(projected, 3);
	return Ovr226_800a25d0_WaterListFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridFaceIndices[3], 3, depth, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
	                                         inheritedOtEntry);
}

static int Ovr226_800a27dc_EmitWaterListGT3Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int secondary,
                                               uint32_t *inheritedOtEntry)
{
	int triIndices[3];

	(void)pb;
	(void)block;
	(void)faceIndex;

	if (secondary)
	{
		// NOTE(aalhendi): Retail label 0x800a27d4 does `s3=s4; s4=s6`
		// before falling through to the fixed-code GT3 writer at 0x800a27dc.
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	POLY_GT3 *prim = primMem->cursor;
	POLY_GT3 *nextPrim = prim + 1;
	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, triIndices, 3);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;

	DrawLevelOvr1P_WriteProjectedGT3(prim, projected, triIndices, 0x36, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 9, inheritedOtEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int Ovr226_800a2850_EmitWaterListGT4Raw(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                               const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                               uint32_t *inheritedOtEntry)
{
	(void)pb;
	(void)block;
	(void)faceIndex;

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	POLY_GT4 *prim = primMem->cursor;
	POLY_GT4 *nextPrim = prim + 1;
	DrawLevelOvr1P_StoreProjectedDirectUvScratch(projected, indices, 4);
	u32 uv0 = DrawLevelOvr1P_Scratch()->uv.uv0;
	u32 uv1 = DrawLevelOvr1P_Scratch()->uv.uv1;
	u32 uv2 = DrawLevelOvr1P_Scratch()->uv.uv2;

	DrawLevelOvr1P_WriteProjectedGT4(prim, projected, indices, 0x3e, uv0, uv1, uv2);
	DrawLevelOvr1P_AddRawPrimToOt(primMem, prim, 12, inheritedOtEntry);
	primMem->cursor = nextPrim;
	return 1;
}

static int Ovr226_800a27b8_EmitWaterListDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                   const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                   uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	DrawLevelOvrRetailLabel handlerLabel = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	// NOTE(aalhendi): 227/228/229 water-list direct tables use shifted retail
	// labels with the same packet ABI; alias them to the owned native writers.
	switch (handlerLabel)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_227:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_PRIMARY_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_229:
		return Ovr226_800a27dc_EmitWaterListGT3Raw(pb, primMem, block, projected, indices, faceIndex, 0, inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_227:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT3_SECOND_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_229:
		return Ovr226_800a27dc_EmitWaterListGT3Raw(pb, primMem, block, projected, indices, faceIndex, 1, inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_227:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_DIRECT_GT4_229:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_229:
		return Ovr226_800a2850_EmitWaterListGT4Raw(pb, primMem, block, projected, indices, faceIndex, inheritedOtEntry);
	default:
		return 1;
	}
}

static int Ovr226_800a2660_WaterListNearDispatch(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth,
                                                 DrawLevelOvrRetailLabel handlerLabel, int handlerSlot, u32 allowedMask, uint32_t *inheritedOtEntry)
{
	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		// NOTE(aalhendi): Retail 0x800a265c preserves the current t2 mask,
		// and 0x800a2668 jumps directly to the direct table at 0x800a27b8
		// when the recursion frame reaches scratch 0x324.
		DrawLevelOvr1P_Scratch()->directMask = allowedMask;
		return Ovr226_800a27b8_EmitWaterListDirectTail(pb, primMem, block, projected, indices, faceIndex, inheritedOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	// NOTE(aalhendi): Retail 0x800a272c..0x800a27a4 advances by 0xb8,
	// builds five 0x800a24e8 midpoint pairs, then jumps through scratch 0x148.
	Ovr226_800a24e8_BuildWaterListSubdivisionFrame(sub, projected, indices);

	return Ovr226_800a22a4_DispatchWaterListHelperWrappers(pb, primMem, block, sub, faceIndex, depth + 1, handlerLabel, handlerSlot, allowedMask,
	                                                       inheritedOtEntry);
}

static int Ovr226_800a25d0_WaterListFaceGate(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                             const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth, u32 allowedMask,
                                             uint32_t *inheritedOtEntry)
{
	// NOTE(aalhendi): Retail 0x800a25d0..0x800a27b8 owns the shared water-list
	// face gate: packed rectangle reject, color-derived direct mask, top-frame
	// OT inheritance, near-mask selection, and recursive child dispatch.
	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(pb, projected, indices))
	{
		return 1;
	}

	u32 directMask = Ovr226_800a262c_SelectAndStoreWaterDirectMask(projected, indices, allowedMask);
	if (directMask == 0)
	{
		return 1;
	}

	uint32_t *faceOtEntry = Ovr226_800a2690_ResolveWaterListInheritedOtEntry(pb, block, projected, indices, faceIndex, inheritedOtEntry);
	u32 nearMask = DrawLevelOvr1P_GetProjectedWaterNearMask(projected, indices);
	if (nearMask != 0)
	{
		DrawLevelOvrRetailLabel handlerLabel = DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, DRAW_LEVEL_OVR1P_CLIP_BYTES_LIST);
		int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);

		return Ovr226_800a2660_WaterListNearDispatch(pb, primMem, block, projected, indices, faceIndex, depth, handlerLabel, handlerSlot, directMask,
		                                             faceOtEntry);
	}

	return Ovr226_800a27b8_EmitWaterListDirectTail(pb, primMem, block, projected, indices, faceIndex, faceOtEntry);
}

static void Ovr226_800a1e30_SeedWaterListState(void)
{
	const struct TextureLayout *waterEnvMap = (const struct TextureLayout *)(uintptr_t)DrawLevelOvr1P_Scratch()->waterEnvMapPtr32;

	// NOTE(aalhendi): Retail 0x800a1e30 uses the global 1P retry list, not the
	// current render-list field, before walking the water BSP list.
	if (sDrawLevelOvr1P_ListHandlersSeedRenderedCursor)
	{
		DrawLevelOvr1P_SetRenderedListCursor(DrawLevelOvr1P_GetRenderedOverflowBase());
	}
	CTC2(0, 21);
	CTC2(0, 22);
	CTC2(0, 23);
	DrawLevelOvr1P_Scratch()->uv.uv0 = DrawLevelOvr1P_ReadPackedWord((const u8 *)waterEnvMap + 0);
	DrawLevelOvr1P_Scratch()->uv.uv1 = DrawLevelOvr1P_ReadPackedWord((const u8 *)waterEnvMap + 4);
}

static void Ovr226_800a1e74_SeedWaterVisibilityScratch(const int *visFaceList, const struct QuadBlock *block)
{
	Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch(visFaceList, block);
}

static int Ovr226_800a1eb0_ConsumeWaterVisibilityBit(void)
{
	return Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit();
}

static int Ovr226_800a1ee0_EmitWaterListQuadBlock(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh, struct QuadBlock *block)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	if (DrawLevelOvr1P_ProjectListGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_WATER_COLOR_LO_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_FACE))
	{
		DrawLevelOvr1P_AppendRenderedQuadBlock(block);
		return 1;
	}

	Ovr226_800a211c_ApplyWaterListColorFades(projected);

	return Ovr226_800a22a4_DispatchWaterListHelperWrappers(pb, primMem, block, projected, 0, 0, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_LIST_WRAPPER_SLOT5, 5,
	                                                       DRAW_LEVEL_OVR1P_DIRECT_QUAD, NULL);
}

static int Ovr226_800a1e30_DrawWaterBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                            const int *visFaceList)
{
	WATER_PERF_BEGIN();
	Ovr226_800a1e30_SeedWaterListState();
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	const int nativeStressMeasuring = NativePerf_IsStressTestMeasuring();
#endif

	while (slot != NULL)
	{
		struct BSP *bsp = slot->bsp;
		struct QuadBlock *block = bsp->data.leaf.ptrQuadBlockArray;
		s32 quadCount = bsp->data.leaf.numQuads;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		const int nativeStressLeafFullyInside =
			nativeStressMeasuring && (quadCount >= 4) &&
			RenderLists_NativeBoxFullyInsideCameraFrustum(pb, &bsp->box);
#endif

		DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		Ovr226_800a1e74_SeedWaterVisibilityScratch(visFaceList, block);

		while (quadCount > 0)
		{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			if (nativeStressMeasuring)
			{
				if (Ovr226_800a1eb0_ConsumeWaterVisibilityBit() &&
				    (nativeStressLeafFullyInside ||
				     DrawLevelOvr1P_NativeCameraAllowsQuadBlock(pb, block)))
				{
					if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT))
					{
						WATER_PERF_END();
						return 0;
					}
					if (!Ovr226_800a1ee0_EmitWaterListQuadBlock(pb, primMem, mesh, block))
					{
						WATER_PERF_END();
						return 0;
					}
				}
			}
			else
#endif
			{
				if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT))
				{
					WATER_PERF_END();
					return 0;
				}

				if (Ovr226_800a1eb0_ConsumeWaterVisibilityBit())
				{
					if (!Ovr226_800a1ee0_EmitWaterListQuadBlock(pb, primMem, mesh, block))
					{
						WATER_PERF_END();
						return 0;
					}
				}
			}

			block++;
			quadCount--;
			DrawLevelOvr1P_Scratch()->quadCount = quadCount;
		}

		slot = slot->next;
	}

	DrawLevelOvr1P_TerminateRenderedListCursor();
	WATER_PERF_END();
	return 1;
}

static int Ovr226_800a30f0_WaterRenderedFaceGate(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth,
                                                 u32 allowedMask, uint32_t *inheritedOtEntry);

static const int *Ovr226_800a2da0_GetWaterRenderedLeafWrapperIndices(DrawLevelOvrRetailLabel handlerLabel)
{
	DrawLevelOvrRetailLabel baseLabel;

	if ((handlerLabel >= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0) && (handlerLabel <= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA3))
	{
		baseLabel = DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0;
	}
	else if ((handlerLabel >= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0_ALIAS_A) &&
	         (handlerLabel <= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA3_ALIAS_A))
	{
		baseLabel = DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0_ALIAS_A;
	}
	else if ((handlerLabel >= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0_ALIAS_B) &&
	         (handlerLabel <= DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA3_ALIAS_B))
	{
		baseLabel = DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0_ALIAS_B;
	}
	else
	{
		return NULL;
	}

	u32 offset = handlerLabel - baseLabel;
	if ((offset % DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_STRIDE) != 0)
	{
		return NULL;
	}

	u32 index = offset / DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_STRIDE;
	if (index < 4)
	{
		return sDrawLevelOvr1PGridFaceIndices[index];
	}

	return sDrawLevelOvr1PGridExtraFaceIndices[index - 4];
}

static int Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                               const struct DrawLevelOvr1PScratchVertex *projected, int faceIndex, int depth,
                                                               DrawLevelOvrRetailLabel handlerLabel, int handlerSlot, u32 allowedMask,
                                                               uint32_t *inheritedOtEntry)
{
	const int *leafIndices = Ovr226_800a2da0_GetWaterRenderedLeafWrapperIndices(handlerLabel);

	(void)handlerSlot;

	// NOTE(aalhendi): Retail 0x800a2da0..0x800a2fe4 wrapper labels either
	// select four projected scratch records and tail-call 0x800a30f0, or call
	// those leaf labels in a fixed sequence before branching to the final leaf.
	// Native uses the C call stack for the retail t9/ra scratch return stack, but
	// the label order, frame+0xb4 writes, and t2 direct-mask constants match the
	// refreshed NTSC-U 926 objdump for this span.
	// 228/229 water-rendered RDATA uses shifted copies of this wrapper table;
	// those labels alias to the same owned helper sequences after target checks.
	if (leafIndices != NULL)
	{
		return Ovr226_800a30f0_WaterRenderedFaceGate(pb, primMem, block, projected, leafIndices, faceIndex, depth, allowedMask, inheritedOtEntry);
	}

	switch (handlerLabel)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT0:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT0_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT0_ALIAS_B:
		if (!Ovr226_800a30f0_WaterRenderedFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[0], faceIndex, depth,
		                                           DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a30f0_WaterRenderedFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[1], faceIndex, depth,
		                                           DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT1:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT1_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT1_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA2, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT3:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT3_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT3_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA0, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA1, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT7:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT7_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT7_ALIAS_B:
		if (!Ovr226_800a30f0_WaterRenderedFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[2], faceIndex, depth,
		                                           DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a30f0_WaterRenderedFaceGate(pb, primMem, block, projected, sDrawLevelOvr1PGridMixedFaceIndices[3], faceIndex, depth,
		                                           DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT2:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT2_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT2_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2, -1, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT4:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT4_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT4_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA1, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1, -1, DRAW_LEVEL_OVR1P_DIRECT_TRI_PRIMARY,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT9:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT9_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT9_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA2, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2, -1,
		                                                         DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT11:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT11_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT11_ALIAS_B:
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_EXTRA0, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1, -1,
		                                                         DRAW_LEVEL_OVR1P_DIRECT_TRI_SECONDARY, inheritedOtEntry))
		{
			return 0;
		}
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                         DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                         inheritedOtEntry))
		{
			return 0;
		}
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, faceIndex, depth,
		                                                           DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
		                                                           inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT5:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT5_ALIAS_A:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT5_ALIAS_B:
		DrawLevelOvr1P_SetGridFaceSlot(projected, 0);
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, 0, depth, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE0,
		                                                         -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		DrawLevelOvr1P_SetGridFaceSlot(projected, 1);
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, 1, depth, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE1,
		                                                         -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		DrawLevelOvr1P_SetGridFaceSlot(projected, 2);
		if (!Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, 2, depth, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE2,
		                                                         -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry))
		{
			return 0;
		}
		DrawLevelOvr1P_SetGridFaceSlot(projected, 3);
		return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(
		    pb, primMem, block, projected, 3, depth, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_LEAF_FACE3, -1, DRAW_LEVEL_OVR1P_DIRECT_QUAD, inheritedOtEntry);
	default:
		return 1;
	}
}

static uint32_t *Ovr226_800a31f0_ResolveWaterRenderedInheritedOtEntry(struct PushBuffer *pb, const struct QuadBlock *block,
                                                                      const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex,
                                                                      uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry != NULL)
	{
		return inheritedOtEntry;
	}

	// NOTE(aalhendi): Retail 0x800a31f0..0x800a3238 computes GP only on the
	// top water-rendered scratch frame. Recursive children inherit the pointer.
	if (projected != DrawLevelOvr1P_GetScratchVertices())
	{
		return NULL;
	}

	u32 selectedDepth = DrawLevelOvr1P_GetWaterTopFrameOtDepth(projected, indices);
	u32 slotWord = DrawLevelOvr1P_GetProjectedOtSlotWord(projected, faceIndex);
	s8 drawOrder = DrawLevelOvr1P_ReadRetailQuadBlockByte(block, 0x18 + (slotWord >> 2));
	s32 otIndex = (s32)(selectedDepth >> 6) + drawOrder;

	return &pb->ptrOT[otIndex];
}

static int Ovr226_800a333c_EmitWaterRenderedGT3RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int secondary,
                                                         uint32_t *inheritedOtEntry)
{
	int triIndices[3];

	(void)block;

	if (secondary)
	{
		// NOTE(aalhendi): Retail label 0x800a3334 does `s3=s4; s4=s6`
		// before falling through to the water-rendered GT3 terminal at 0x800a333c.
		triIndices[0] = indices[1];
		triIndices[1] = indices[3];
		triIndices[2] = indices[2];
	}
	else
	{
		triIndices[0] = indices[0];
		triIndices[1] = indices[1];
		triIndices[2] = indices[2];
	}

	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNearSigned(projected, triIndices, 3))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNearSigned(projected, triIndices, 3))
	{
		return Ovr226_800a34d4_WriteWaterRenderedClippedRecordAtOtEntry(pb, projected, triIndices, 3, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(pb, primMem, block, projected, triIndices, NULL, inheritedOtEntry, 0x36);
}

static int Ovr226_800a33e0_EmitWaterRenderedGT4RawOrClip(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                         const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, uint32_t *inheritedOtEntry)
{
	if (inheritedOtEntry == NULL)
	{
		return 1;
	}

	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800a34d4_WriteWaterRenderedClippedRecordAtOtEntry(pb, projected, indices, 4, inheritedOtEntry);
	}

	return DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(pb, primMem, block, projected, indices, NULL, inheritedOtEntry, 0x3e);
}

static int Ovr226_800a3318_DispatchWaterRenderedDirectTail(struct PushBuffer *pb, struct PrimMem *primMem, const struct QuadBlock *block,
                                                           const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, uint32_t *inheritedOtEntry)
{
	u32 directMask = DrawLevelOvr1P_Scratch()->directMask;
	DrawLevelOvrRetailLabel handlerLabel = DrawLevelOvr1P_GetDirectHandlerAddress(directMask);

	// NOTE(aalhendi): 228/229 water-rendered direct tables use shifted retail
	// labels with the same raw/clip packet ABI; alias them to the owned writers.
	switch (handlerLabel)
	{
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_PRIMARY_229:
		return Ovr226_800a333c_EmitWaterRenderedGT3RawOrClip(pb, primMem, block, projected, indices, 0, inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT3_SECOND_229:
		return Ovr226_800a333c_EmitWaterRenderedGT3RawOrClip(pb, primMem, block, projected, indices, 1, inheritedOtEntry);
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_226:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_228:
	case DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_DIRECT_GT4_229:
		return Ovr226_800a33e0_EmitWaterRenderedGT4RawOrClip(pb, primMem, block, projected, indices, inheritedOtEntry);
	default:
		return 1;
	}
}

static int Ovr226_800a31bc_WaterRenderedNearDispatch(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                     const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth,
                                                     u32 allowedMask, uint32_t *inheritedOtEntry)
{
	DrawLevelOvr1P_Scratch()->directMask = allowedMask;

	if (DrawLevelOvr1P_IsDeepestSubdivisionFrame(projected))
	{
		return Ovr226_800a3318_DispatchWaterRenderedDirectTail(pb, primMem, block, projected, indices, inheritedOtEntry);
	}

	uint32_t *faceOtEntry = Ovr226_800a31f0_ResolveWaterRenderedInheritedOtEntry(pb, block, projected, indices, faceIndex, inheritedOtEntry);
	u32 nearMask = DrawLevelOvr1P_GetProjectedWaterNearMask(projected, indices);
	if (nearMask == 0)
	{
		return Ovr226_800a3318_DispatchWaterRenderedDirectTail(pb, primMem, block, projected, indices, faceOtEntry);
	}

	struct DrawLevelOvr1PScratchVertex *sub = DrawLevelOvr1P_GetSubdivisionFrame(depth);
	// NOTE(aalhendi): Retail 0x800a31c0 builds the water 3x3 subdivision
	// frame before jumping through the copied recursive-handler table.
	Ovr226_800a2fe4_BuildWaterRenderedSubdivisionFrame(sub, projected, indices);

	DrawLevelOvrRetailLabel handlerAddress = DrawLevelOvr1P_GetNearSubdivisionHandlerAddress(nearMask, DRAW_LEVEL_OVR1P_CLIP_BYTES_RENDERED);
	int handlerSlot = DrawLevelOvr1P_GetNearSubdivisionTableSlot(nearMask);
	return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, sub, faceIndex, depth + 1, handlerAddress, handlerSlot, allowedMask,
	                                                           faceOtEntry);
}

static int Ovr226_800a30f0_WaterRenderedFaceGate(struct PushBuffer *pb, struct PrimMem *primMem, struct QuadBlock *block,
                                                 const struct DrawLevelOvr1PScratchVertex *projected, const int *indices, int faceIndex, int depth,
                                                 u32 allowedMask, uint32_t *inheritedOtEntry)
{
	if (DrawLevelOvr1P_AreProjectedVerticesHalfNear(projected, indices, 4))
	{
		return 1;
	}

	if (DrawLevelOvr1P_HasProjectedVertexNear(projected, indices, 4))
	{
		return Ovr226_800a31bc_WaterRenderedNearDispatch(pb, primMem, block, projected, indices, faceIndex, depth, allowedMask, inheritedOtEntry);
	}

	if (DrawLevelOvr1P_IsProjectedFaceOffscreen(pb, projected, indices))
	{
		return 1;
	}

	u32 directMask = Ovr226_800a262c_SelectAndStoreWaterDirectMask(projected, indices, allowedMask);
	if (directMask == 0)
	{
		return 1;
	}

	return Ovr226_800a31bc_WaterRenderedNearDispatch(pb, primMem, block, projected, indices, faceIndex, depth, directMask, inheritedOtEntry);
}

static int DrawLevelOvr1P_DrawRenderedWaterQuadBlockWithDefaultHandler(struct PushBuffer *pb, struct PrimMem *primMem, struct mesh_info *mesh,
                                                                       struct QuadBlock *block, DrawLevelOvrRetailLabel defaultHandlerAddress)
{
	struct LevVertex *vertices = mesh->ptrVertexArray;
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();

	DrawLevelOvr1P_ProjectRenderedGrid(vertices, block, projected, DRAW_LEVEL_OVR1P_PROJECTED_SOURCE_WATER_COLOR_LO_FLAGS, DRAW_LEVEL_OVR1P_GRID_SLOT_NONE);
	Ovr226_800a2c4c_ApplyWaterRenderedColorFades(projected);

	// NOTE(aalhendi): Retail water-rendered helper dispatch enters the current
	// overlay's default wrapper with no inherited GP/OT pointer.
	return Ovr226_800a2da0_DispatchWaterRenderedHelperWrappers(pb, primMem, block, projected, 0, 0, defaultHandlerAddress, 5, DRAW_LEVEL_OVR1P_DIRECT_QUAD,
	                                                           NULL);
}

static int Ovr226_800a2904_DrawWaterRenderedListWithDefaultHandler(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh,
                                                                   struct PrimMem *primMem, DrawLevelOvrRetailLabel defaultHandlerAddress)
{
	WATER_PERF_BEGIN();

	if (renderedList == NULL)
	{
		WATER_PERF_END();
		return 1;
	}

	while (1)
	{
		struct QuadBlock *block = *renderedList;

		if (!DrawLevelOvr1P_HasBucketPrimReserve(primMem, DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT))
		{
			WATER_PERF_END();
			return 0;
		}

		if (block == NULL)
		{
			*CTR_SCRATCHPAD_PTR(u32, DRAW_LEVEL_OVR1P_WATER_RENDERED_SENTINEL_OFFSET) = 0;
			WATER_PERF_END();
			return 1;
		}

		if (!DrawLevelOvr1P_DrawRenderedWaterQuadBlockWithDefaultHandler(pb, primMem, mesh, block, defaultHandlerAddress))
		{
			WATER_PERF_END();
			return 0;
		}

		renderedList++;
	}
}

static int Ovr226_800a2904_DrawWaterRenderedList(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem)
{
	return Ovr226_800a2904_DrawWaterRenderedListWithDefaultHandler(renderedList, pb, mesh, primMem, DRAW_LEVEL_OVR_RETAIL_LABEL_WATER_RENDERED_WRAPPER_SLOT5);
}

static int DrawLevelOvr1P_DrawRenderedQuadBlocks(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                 int role)
{
	if (renderedList == NULL)
	{
		return 1;
	}

	switch (role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED:
	case DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED:
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED:
	case DRAW_LEVEL_OVR1P_BUCKET_4X4_RENDERED:
		return DrawLevelOvr1P_DrawNonWaterRenderedList(renderedList, pb, mesh, primMem, role);

	case DRAW_LEVEL_OVR1P_BUCKET_WATER_RENDERED:
		return Ovr226_800a2904_DrawWaterRenderedList(renderedList, pb, mesh, primMem);

	default:
		return 0;
	}
}

static void *DrawLevelOvr1P_GetRenderListBucketValue(struct DrawLevelOvr1PRenderList *renderList, const struct DrawLevelOvr1PBucket *bucket)
{
	int renderListOffset = bucket->renderListOffset;

	if (renderListOffset == offsetof(struct DrawLevelOvr1PRenderList, bspListStart_FullDynamic))
	{
		return renderList->bspListStart_FullDynamic;
	}

	if (renderListOffset == offsetof(struct DrawLevelOvr1PRenderList, ptrQuadBlocksRendered_FullDynamic))
	{
		return renderList->ptrQuadBlocksRendered_FullDynamic;
	}

	u32 slotIndex = (u32)renderListOffset / sizeof(renderList->list[0]);

	if (slotIndex >= DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT)
	{
		return NULL;
	}

	if (bucket->kind == DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED)
	{
		return renderList->list[slotIndex].ptrQuadBlocksRendered;
	}

	return renderList->list[slotIndex].bspListStart;
}

static void Ovr226_800a0d34_SetEntryGteAndCameraScratch(struct PushBuffer *pb)
{
	s16 *data6 = DrawLevelOvr1P_Scratch()->projectedCenter.v;

	for (s32 controlWordIndex = 0; controlWordIndex < 8; controlWordIndex++)
	{
		CTC2(DrawLevelOvr1P_ReadWord(&pb->matrix_ViewProj, (u32)(controlWordIndex * 4)), controlWordIndex);
	}

	for (s32 centerIndex = 0; centerIndex < 3; centerIndex++)
	{
		data6[centerIndex] = (s16)((u16)(u8)pb->data6[centerIndex * 2] | ((u16)(u8)pb->data6[centerIndex * 2 + 1] << 8));
	}

	CTC2((u32)(s32)pb->rect.w << 15, 24);
	CTC2((u32)(s32)pb->rect.h << 15, 25);
	CTC2((u32)pb->distanceToScreen_PREV, 26);

	// NOTE(aalhendi): Retail stores `(distanceToScreen_PREV >> 1) + 1` at scratch 0x5c for 4x1 depth clip flags.
	DrawLevelOvr1P_Scratch()->depthClipThreshold = (pb->distanceToScreen_PREV >> 1) + 1;
	DrawLevelOvr1P_Scratch()->clipWindowPacked = DrawLevelOvr1P_ReadWord(&pb->rect, 4);
}

static const struct DrawLevelOvr1PBucket *Ovr226_800a0e78_FindBucketByHandler(u32 handlerAddress)
{
	for (s32 bucketIndex = 0; bucketIndex < OVR226_BUCKET_COUNT; bucketIndex++)
	{
		if (R226.bucketHandlerAddresses[bucketIndex] == handlerAddress)
		{
			return &sDrawLevelOvr1PBuckets[bucketIndex];
		}
	}

	return NULL;
}

static int Ovr226_800a0e78_DispatchBucketHandler(u32 handlerAddress, void *bucketValue, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                                 const int *visFaceList)
{
	const struct DrawLevelOvr1PBucket *bucket = Ovr226_800a0e78_FindBucketByHandler(handlerAddress);

	if (bucket == NULL)
	{
		return 0;
	}

	if (bucket->kind == DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED)
	{
		return DrawLevelOvr1P_DrawRenderedQuadBlocks((struct QuadBlock **)bucketValue, pb, mesh, primMem, bucket->role);
	}

	if (bucket->role == DRAW_LEVEL_OVR1P_BUCKET_FULL_DYNAMIC_LIST)
	{
		return Ovr226_800a0ef4_DrawFullDynamicBspList((struct VisMemBspListNode *)bucketValue, pb, mesh, primMem, visFaceList);
	}

	if (bucket->role == DRAW_LEVEL_OVR1P_BUCKET_WATER_LIST)
	{
		return Ovr226_800a1e30_DrawWaterBspList((struct VisMemBspListNode *)bucketValue, pb, mesh, primMem, visFaceList);
	}

	// NOTE(aalhendi): Overlay 226 consumes VisMem BSP-list nodes: word zero is
	// next, word one is the BSP pointer preserved by VisMem initialization.
	switch (bucket->role)
	{
	case DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST:
	case DRAW_LEVEL_OVR1P_BUCKET_4X4_LIST:
		return DrawLevelOvr1P_DrawBspListQuadBlocks((struct VisMemBspListNode *)bucketValue, pb, mesh, primMem, visFaceList, bucket->role);

	default:
		return 0;
	}
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Experimental diagnostic only. Disabled by default unless the marker exists.
 * Measures the original retail bucket dispatcher without changing its output.
 * Run this in an isolated build, then remove once a direct native path is ready.
 */
static int DrawLevelOvr1P_BucketProfileEnabled(void)
{
    static int checked, enabled;
    if (!checked)
    {
        FILE *f = fopen("debug/level_bucket_profile.enable", "rb");
        enabled = (f != NULL);
        if (f != NULL) fclose(f);
        checked = 1;
    }
    return enabled;
}
static u64 sDrawLevelOvr1P_BucketNs[OVR226_BUCKET_COUNT];
static u64 sDrawLevelOvr1P_BucketCalls[OVR226_BUCKET_COUNT];
static u32 sDrawLevelOvr1P_BucketFrames;
static u64 DrawLevelOvr1P_BucketProfileBegin(void)
{
    return SDL_GetTicksNS();
}
static void DrawLevelOvr1P_BucketProfileEnd(u32 index, u64 start)
{
    if (index < OVR226_BUCKET_COUNT)
    {
        sDrawLevelOvr1P_BucketNs[index] += SDL_GetTicksNS() - start;
        ++sDrawLevelOvr1P_BucketCalls[index];
    }
}
static void DrawLevelOvr1P_BucketProfileFrame(void)
{
    if (++sDrawLevelOvr1P_BucketFrames < 1200) return;
    sDrawLevelOvr1P_BucketFrames = 0;
    Platform_Log("[LEVEL BUCKET PROFILE] 1200 frames / 11 retail buckets (ms total and calls)\n");
    for (u32 i = 0; i < OVR226_BUCKET_COUNT; ++i)
    {
        Platform_Log("[LEVEL BUCKET PROFILE] bucket=%u kind=%u role=%u totalMs=%.3f calls=%llu\n",
            i, (u32)sDrawLevelOvr1PBuckets[i].kind, (u32)sDrawLevelOvr1PBuckets[i].role,
            (double)sDrawLevelOvr1P_BucketNs[i] / 1000000.0,
            (unsigned long long)sDrawLevelOvr1P_BucketCalls[i]);
        sDrawLevelOvr1P_BucketNs[i] = 0;
        sDrawLevelOvr1P_BucketCalls[i] = 0;
    }
}
#endif

static int Ovr226_800a0e10_DispatchBucketTable(struct DrawLevelOvr1PRenderList *renderList, struct PushBuffer *pb, struct mesh_info *mesh,
                                               struct PrimMem *primMem, const int *visFaceList)
{
	for (s32 renderListOffset = DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_FULL_DYNAMIC_LIST; renderListOffset >= 0; renderListOffset -= (s32)sizeof(u32))
	{
		u32 bucketIndex = (u32)renderListOffset / sizeof(u32);
		const struct DrawLevelOvr1PBucket *bucket = &sDrawLevelOvr1PBuckets[bucketIndex];
		void *bucketValue = DrawLevelOvr1P_GetRenderListBucketValue(renderList, bucket);
		u32 setupAddress = R226.bucketSetupAddresses[bucketIndex];
		u32 handlerAddress = R226.bucketHandlerAddresses[bucketIndex];

		DrawLevelOvr1P_Scratch()->currentBucketOffset = (u32)renderListOffset;

		if (bucketValue == NULL)
		{
			// NOTE(aalhendi): Retail 0x800a0e7c clears the shared rendered
			// quadblock destination before advancing past an empty bucket.
			DrawLevelOvr1P_ClearRenderedListForRole(renderList, bucket->role);
			continue;
		}

		Ovr226_800a0e44_ApplyBucketSetup(setupAddress);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		if (DrawLevelOvr1P_BucketProfileEnabled())
		{
			const u64 bucketStart = DrawLevelOvr1P_BucketProfileBegin();
			const int success = Ovr226_800a0e78_DispatchBucketHandler(handlerAddress, bucketValue, pb, mesh, primMem, visFaceList);
			DrawLevelOvr1P_BucketProfileEnd(bucketIndex, bucketStart);
			if (!success) return 0;
		}
		else
#endif
		if (!Ovr226_800a0e78_DispatchBucketHandler(handlerAddress, bucketValue, pb, mesh, primMem, visFaceList))
		{
			return 0;
		}
	}

	return 1;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800a0cbc-0x800ab970
void DrawLevelOvr1P(void *LevRenderList, struct PushBuffer *pb, struct BSP *bspList, struct PrimMem *primMem, const int *visFaceList,
                    const struct TextureLayout *waterEnvMap)
{
	struct DrawLevelOvr1PRenderList *renderList = LevRenderList;
	struct mesh_info *mesh = (struct mesh_info *)bspList;
	u32 hostStackAnchor;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    sDrawLevelOvr1P_NativeGpuOtMarkerArmed=0;
#endif
#ifdef CTR_NATIVE
	DrawLevelOvr1P_BeginNativePointerCachePass(mesh);
#endif

	// NOTE(aalhendi): Retail 0x800a0cf0 saves `sp` in scratch 0x38 and
	// restores it at 0x800a0eb8. Native records a stack anchor only so later
	// scratch users see the same entry-owned word; the host ABI owns SP.
	DrawLevelOvr1P_Scratch()->savedStackPtr32 = (u32)(uintptr_t)&hostStackAnchor;

	DrawLevelOvr1P_Scratch()->primMemEndPtr32 = (u32)(uintptr_t)primMem->end;
	DrawLevelOvr1P_Scratch()->visFaceListPtr32 = (u32)(uintptr_t)visFaceList;

	if (visFaceList == NULL)
	{
		return;
	}

	DrawLevelOvr1P_Scratch()->waterEnvMapPtr32 = (u32)(uintptr_t)waterEnvMap;

	if (mesh->ptrQuadBlockArray == NULL)
	{
		return;
	}

	DrawLevelOvr1P_SetClipRecordStart(data.PtrClipBuffer[0]);
	DrawLevelOvr1P_SetRenderedOverflowBase(sdata_static.quadBlocksRendered);
	DrawLevelOvr1P_SetPrimReserveBias(0);
	DrawLevelOvr1P_SetListHandlersSeedRenderedCursor(1);
	Ovr226_800a0d20_SeedEntryScratchPointers(renderList, pb);
	Ovr226_800a0d34_SetEntryGteAndCameraScratch(pb);
#if defined(CTR_NATIVE)
	if (NativeRenderer_LevelPCEnabled())
	{
		s16 viewM[3][3];
		s32 viewT[3];
		int nativeScreen = 0;
		int nativeWide = 0;
		NativeGTE_GetLevelPCView(viewM, viewT, &nativeScreen, &nativeWide);
		/* CTR's 1P logical projection is 320x240; the host viewport may be
		 * widescreen, but the presentation mapper handles host pixels. */
		NativeRenderer_LevelPCBeginFrame(
			mesh->ptrQuadBlockArray, viewM, viewT,
			nativeScreen, 320, 240, nativeWide);
        s32 originX=0,originY=0;
        NativeGTE_GetLevelPCProjectionOrigin(&originX,&originY);
        NativeRenderer_LevelPCProjectionQABegin(
            mesh->ptrQuadBlockArray,viewM,viewT,
            nativeScreen,nativeWide,originX,originY);
	}
	DrawLevelOvr1P_BeginProjectionCache(mesh, pb);
#endif
	Ovr226_800a0dc4_ClearProjectedScratch();
	Ovr226_800a0ddc_CopyScratchInitTable();

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    /* OT 0x3fc mirrors Turbocharged's layer placement: sky negative-depth
     * entries precede resident terrain, later gameplay/HUD entries follow.
     * Keep this packet in the SAME PrimMem as the original overlay so its
     * OT address is properly registered and survives native PS1 packet walk.
     * Reserve it BEFORE rasterizing: an incomplete bucket render can safely
     * fall back without ever suppressing geometry whose marker is absent.
     * This does NOT switch on any new mode for normal player gameplay. */
    if (DrawLevelOvr1P_NativeGpuOtMarkerRequested() &&
        pb->ptrOT != NULL && primMem->cursor != NULL &&
        primMem->end != NULL &&
        (uintptr_t)primMem->end >= (uintptr_t)primMem->cursor &&
        (uintptr_t)primMem->end - (uintptr_t)primMem->cursor >= 8u)
    {
        u32 *marker=(u32 *)primMem->cursor;
        marker[1]=0xB5000000u; /* 0xB5: reserved native terrain packet */
        DrawLevelOvr1P_AddRawPrimToOt(primMem,marker,1,&pb->ptrOT[0x3fc]);
        primMem->cursor=(void *)(marker+2);
        sDrawLevelOvr1P_NativeGpuOtMarkerArmed=1;
        static int loggedMarker;
        if(!loggedMarker)
        {
            Platform_Log("[LEVEL PC GPU] STRESS OT terrain marker inserted at 0x3fc (B5, 1 word)\n");
            loggedMarker=1;
        }
    }
#endif

	if (!Ovr226_800a0e10_DispatchBucketTable(renderList, pb, mesh, primMem, visFaceList))
	{
		return;
	}

	// NOTE(aalhendi): Retail 0x800a0e98 reloads 0x800ab910 into scratch
	// 0x240 immediately before 0x800aa790 consumes clipped records.
	Ovr226_800ab3dc_CopyClipRecordJumpTable();
	if (!DrawLevelOvr1P_ConsumeClipRecords(pb, primMem))
	{
		return;
	}
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (DrawLevelOvr1P_BucketProfileEnabled())
		DrawLevelOvr1P_BucketProfileFrame();
#endif
#if defined(CTR_NATIVE)
	DrawLevelOvr1P_LogProjectionCache();
#endif
}
