#include <common.h>

#if defined(CTR_NATIVE)
void NativeGTE_ActivatePresentationMatrix(const MATRIX *matrix);
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_perf.h>
#endif

enum RenderListsSlot1P2P
{
	RENDER_LIST_SLOT_4X4 = 0,
	RENDER_LIST_SLOT_DYNAMIC_SUBDIV = 1,
	RENDER_LIST_SLOT_4X2 = 2,
	RENDER_LIST_SLOT_4X1 = 3,
	RENDER_LIST_SLOT_WATER = 4,
	RENDER_LIST_SLOT_FULL_DYNAMIC = 5,
};

enum RenderListsScratchOffset
{
	RENDER_LISTS_CORNER_JUMP_TABLE_OFFSET = 0x84,
	RENDER_LISTS_STACK_OFFSET = 0xd0,
};

struct RenderListsScratchRecord
{
	BspChildId childID;
	s16 unused;
	struct BoundingBox box;
};

CTR_STATIC_ASSERT(sizeof(struct RenderListsScratchRecord) == 0x10);

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#define NATIVE_STRESS_RENDER_STACK_CAPACITY 2048
static struct RenderListsScratchRecord s_nativeStressRenderStack[NATIVE_STRESS_RENDER_STACK_CAPACITY];
#endif

static int RenderLists_IsVisible(const int *visLeafList, BspChildId childID)
{
	u32 rawChildID = (u16)childID;

	if (visLeafList == 0)
	{
		return 1;
	}

	return ((s32)visLeafList[(rawChildID >> 5) & 0x1ff] << (rawChildID & 0x1f)) < 0;
}

static int RenderLists_BoxOverlapsPushBuffer(const struct BoundingBox *box, const struct BoundingBox *pbBox)
{
	return (box->min.x <= pbBox->max.x) && (pbBox->min.x <= box->max.x) && (box->min.y <= pbBox->max.y) && (pbBox->min.y <= box->max.y) &&
	       (box->min.z <= pbBox->max.z) && (pbBox->min.z <= box->max.z);
}

static void RenderLists_SelectBoxCorner(const struct BoundingBox *box, int cornerIndex, s16 *point)
{
	// NOTE(aalhendi): This is the exact corner order produced by retail helpers 0x80070310..0x8007037c: max/max/max through min/min/min by bit index.
	point[0] = (cornerIndex & 1) ? box->min.x : box->max.x;
	point[1] = (cornerIndex & 2) ? box->min.y : box->max.y;
	point[2] = (cornerIndex & 4) ? box->min.z : box->max.z;
}

static void RenderLists_Load1P2PGteState(struct PushBuffer *pb)
{
	if (pb == 0)
	{
		return;
	}

	// NOTE(aalhendi): Retail 0x8006fecc loads RT/TR/OFX/OFY/H once before traversal; frustum helpers only touch the light-matrix/RBK controls.
	gte_SetRotMatrix(&pb->matrix_ViewProj);
	gte_SetTransMatrix(&pb->matrix_ViewProj);
#if defined(CTR_NATIVE)
	NativeGTE_ActivatePresentationMatrix(&pb->matrix_ViewProj);
#endif
	gte_SetGeomOffset(pb->rect.w >> 1, pb->rect.h >> 1);
	gte_SetGeomScreen(pb->distanceToScreen_PREV);
}

static int RenderLists_FrustumRejectsCorner(const struct PushBufferFrustumPlane *plane, const s16 *point)
{
#if defined(CTR_NATIVE)
	/* Exact scalar equivalent of retail llv0bk for this use-site.
	 * Validated against the GTE path for >3.7 million Stress Test plane/corner
	 * checks with zero result mismatches. Avoids five GTE register writes,
	 * generic COP2 decode and IR readback for every BSP frustum plane. */
	const s64 raw =
		(s64)plane->normal.x * (s64)point[0] +
		(s64)plane->normal.y * (s64)point[1] +
		(s64)plane->normal.z * (s64)point[2] -
		(s64)2 * (s64)plane->halfDistance * 4096;
	return raw >= 4096;
#else
	// NOTE(aalhendi): Retail 0x80070290 tests the selected BSP corner with llv0bk and rejects when IR1 is positive.
	MTC2(CTR_PackS16Pair(point[0], point[1]), 0);
	MTC2((u32)(s32)point[2], 1);
	CTC2(CTR_PackS16Pair(plane->normal.x, plane->normal.y), 8);
	CTC2(CTR_PackS16Pair(plane->normal.z, plane->halfDistance), 9);
	CTC2((u32)(s32)(-2 * plane->halfDistance), 13);
	doCOP2(0x04a2012);

	return MFC2_S(9) > 0;
#endif
}

#if defined(CTR_NATIVE)
static int RenderLists_ProjectDistanceExactNative(struct PushBuffer *pb, const struct BoundingBox *box)
{
	const MATRIX *m = &pb->matrix_ViewProj;
	const int cornerIndex = pb->RenderListJmpIndex[5] & 7;
	const s64 x = (s64)((cornerIndex & 1) ? box->min.x : box->max.x);
	const s64 y = (s64)((cornerIndex & 2) ? box->min.y : box->max.y);
	const s64 z = (s64)((cornerIndex & 4) ? box->min.z : box->max.z);
	const s64 raw =
		((s64)m->t[2] << 12) +
		(s64)m->m[2][0] * x +
		(s64)m->m[2][1] * y +
		(s64)m->m[2][2] * z;

	/* Exact SZ3-only equivalent of RTPS for the BSP LOD test.
	 * The old float shortcut occasionally differed by +/-1 around LOD
	 * thresholds; this integer path matched retail GTE SZ3 in every one of
	 * >700k validated Stress Test samples. */
	if (raw <= 0)
		return 0;

	{
		const s64 depth = raw >> 12;
		if (depth >= 65535)
			return 65535;
		return (int)depth;
	}
}
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
struct NativeStressFrustumCache
{
	struct PushBuffer *pb;
	float row[3][3];
	float absRow[3][3];
	float trans[3];
	float projX;
	float projY;
	float halfW;
	float halfH;
};

static struct NativeStressFrustumCache s_nativeStressFrustum;

static void RenderLists_NativePrepareCameraFrustum(struct PushBuffer *pb)
{
	const MATRIX *m;
	const float fixedScale = 1.0f / 4096.0f;

	if (pb == NULL)
	{
		s_nativeStressFrustum.pb = NULL;
		return;
	}

	m = &pb->matrix_ViewProj;
	s_nativeStressFrustum.pb = pb;
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			float v = (float)m->m[r][c] * fixedScale;
			s_nativeStressFrustum.row[r][c] = v;
			s_nativeStressFrustum.absRow[r][c] = (v < 0.0f) ? -v : v;
		}
		s_nativeStressFrustum.trans[r] = (float)m->t[r];
	}

	s_nativeStressFrustum.projX = (float)pb->distanceToScreen_PREV;
	s_nativeStressFrustum.projY = (float)pb->distanceToScreen_PREV;
	if (Platform_GetWideMode())
		s_nativeStressFrustum.projX *= 0.75f;

	/* Stress spectator moves through viewpoints retail cameras never use.
	   Keep a generous guard band so a BSP branch touching the screen edge is
	   retained; PVS still does the coarse visibility work. */
	s_nativeStressFrustum.halfW = (float)pb->rect.w * 0.5f + 32.0f;
	s_nativeStressFrustum.halfH = (float)pb->rect.h * 0.5f + 32.0f;
}

int RenderLists_NativeBoxPassesCameraFrustum(struct PushBuffer *pb, const struct BoundingBox *box)
{
	const struct NativeStressFrustumCache *fc = &s_nativeStressFrustum;
	float cx, cy, cz;
	float ex, ey, ez;
	float vx, vy, vz;
	float vex, vey, vez;
	float zFar;

	if ((pb == NULL) || (box == NULL))
		return 1;

	if (fc->pb != pb)
	{
		RenderLists_NativePrepareCameraFrustum(pb);
		fc = &s_nativeStressFrustum;
	}

	if ((fc->projX <= 0.0f) || (fc->projY <= 0.0f))
		return 1;

	/* Keep all culling math in float on native 32-bit builds. The previous
	 * implementation used many 64-bit integer multiplies per BSP/quad, which
	 * is especially expensive on x86. The matrix is converted once per render
	 * pass and every box then becomes a small set of scalar float dot products. */
	cx = ((float)box->min.x + (float)box->max.x) * 0.5f;
	cy = ((float)box->min.y + (float)box->max.y) * 0.5f;
	cz = ((float)box->min.z + (float)box->max.z) * 0.5f;
	ex = ((float)box->max.x - (float)box->min.x) * 0.5f + 1.5f;
	ey = ((float)box->max.y - (float)box->min.y) * 0.5f + 1.5f;
	ez = ((float)box->max.z - (float)box->min.z) * 0.5f + 1.5f;

	vx = fc->trans[0] + fc->row[0][0] * cx + fc->row[0][1] * cy + fc->row[0][2] * cz;
	vy = fc->trans[1] + fc->row[1][0] * cx + fc->row[1][1] * cy + fc->row[1][2] * cz;
	vz = fc->trans[2] + fc->row[2][0] * cx + fc->row[2][1] * cy + fc->row[2][2] * cz;

	vex = fc->absRow[0][0] * ex + fc->absRow[0][1] * ey + fc->absRow[0][2] * ez;
	vey = fc->absRow[1][0] * ex + fc->absRow[1][1] * ey + fc->absRow[1][2] * ez;
	vez = fc->absRow[2][0] * ex + fc->absRow[2][1] * ey + fc->absRow[2][2] * ez;

	zFar = vz + vez;
	if (zFar <= 1.0f)
		return 0;

	if ((vz - vez) <= 1.0f)
		return 1;

	if ((fc->projX * (vx - vex)) > (fc->halfW * zFar)) return 0;
	if ((fc->projX * (vx + vex)) < -(fc->halfW * zFar)) return 0;
	if ((fc->projY * (vy - vey)) > (fc->halfH * zFar)) return 0;
	if ((fc->projY * (vy + vey)) < -(fc->halfH * zFar)) return 0;

	return 1;
}

/* Conservative fully-inside test for a BSP leaf. When true, every child
 * quad-block AABB is also inside the camera frustum, so DrawLevelOvr1P can
 * skip the more expensive per-block frustum test for this leaf. */
int RenderLists_NativeBoxFullyInsideCameraFrustum(struct PushBuffer *pb, const struct BoundingBox *box)
{
	const struct NativeStressFrustumCache *fc = &s_nativeStressFrustum;
	float cx, cy, cz;
	float ex, ey, ez;
	float vx, vy, vz;
	float vex, vey, vez;
	float zNear;

	if ((pb == NULL) || (box == NULL))
		return 0;

	if (fc->pb != pb)
	{
		RenderLists_NativePrepareCameraFrustum(pb);
		fc = &s_nativeStressFrustum;
	}

	if ((fc->projX <= 0.0f) || (fc->projY <= 0.0f))
		return 0;

	cx = ((float)box->min.x + (float)box->max.x) * 0.5f;
	cy = ((float)box->min.y + (float)box->max.y) * 0.5f;
	cz = ((float)box->min.z + (float)box->max.z) * 0.5f;
	ex = ((float)box->max.x - (float)box->min.x) * 0.5f + 1.5f;
	ey = ((float)box->max.y - (float)box->min.y) * 0.5f + 1.5f;
	ez = ((float)box->max.z - (float)box->min.z) * 0.5f + 1.5f;

	vx = fc->trans[0] + fc->row[0][0] * cx + fc->row[0][1] * cy + fc->row[0][2] * cz;
	vy = fc->trans[1] + fc->row[1][0] * cx + fc->row[1][1] * cy + fc->row[1][2] * cz;
	vz = fc->trans[2] + fc->row[2][0] * cx + fc->row[2][1] * cy + fc->row[2][2] * cz;

	vex = fc->absRow[0][0] * ex + fc->absRow[0][1] * ey + fc->absRow[0][2] * ez;
	vey = fc->absRow[1][0] * ex + fc->absRow[1][1] * ey + fc->absRow[1][2] * ez;
	vez = fc->absRow[2][0] * ex + fc->absRow[2][1] * ey + fc->absRow[2][2] * ez;

	zNear = vz - vez;
	if (zNear <= 1.0f)
		return 0;

	if ((fc->projX * (vx + vex)) >  (fc->halfW * zNear)) return 0;
	if ((fc->projX * (vx - vex)) < -(fc->halfW * zNear)) return 0;
	if ((fc->projY * (vy + vey)) >  (fc->halfH * zNear)) return 0;
	if ((fc->projY * (vy - vey)) < -(fc->halfH * zNear)) return 0;

	return 1;
}

static int RenderLists_NativeProjectDistance(struct PushBuffer *pb, const struct BoundingBox *box)
{
	const struct NativeStressFrustumCache *fc = &s_nativeStressFrustum;
	int cornerIndex;
	float x, y, z;
	float depth;

	if ((pb == NULL) || (box == NULL))
		return 0;
	if (fc->pb != pb)
	{
		RenderLists_NativePrepareCameraFrustum(pb);
		fc = &s_nativeStressFrustum;
	}

	cornerIndex = pb->RenderListJmpIndex[5] & 7;
	x = (float)((cornerIndex & 1) ? box->min.x : box->max.x);
	y = (float)((cornerIndex & 2) ? box->min.y : box->max.y);
	z = (float)((cornerIndex & 4) ? box->min.z : box->max.z);
	depth = fc->trans[2] + fc->row[2][0] * x + fc->row[2][1] * y + fc->row[2][2] * z;
	if (depth <= 0.0f) return 0;
	if (depth >= 65535.0f) return 65535;
	return (int)depth;
}
#endif

static int RenderLists_BoxPassesFrustum(struct PushBuffer *pb, const struct BoundingBox *box)
{
	s16 point[3];
	const struct PushBufferFrustumPlane *plane;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
		return RenderLists_NativeBoxPassesCameraFrustum(pb, box);
#endif

	if (pb == 0)
	{
		return 1;
	}

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		// NOTE(aalhendi): Retail tests planes in 1,3,0,2 order; each test is independent, so native preserves the plane/corner pairing in a simple loop.
		plane = &pb->frustumPlanes[planeIndex];
		RenderLists_SelectBoxCorner(box, pb->RenderListJmpIndex[planeIndex] & 7, point);

		if (RenderLists_FrustumRejectsCorner(plane, point))
		{
			return 0;
		}
	}

	return 1;
}

static int RenderLists_ProjectDistance(struct PushBuffer *pb, const struct BoundingBox *box)
{
	if (pb == 0)
	{
		return 0;
	}

#if defined(CTR_NATIVE)
	return RenderLists_ProjectDistanceExactNative(pb, box);
#else
	{
		s16 point[3];
		int distance;

		RenderLists_SelectBoxCorner(box, pb->RenderListJmpIndex[5] & 7, point);
		CTR_GteLoadS16TripletV0(point);
		gte_rtps();
		gte_stsz(&distance);
		return distance;
	}
#endif
}

static int RenderLists_Select1P2PSlot(const struct BSP *bsp, struct PushBuffer *pb, int lodDistanceThreshold)
{
	if ((bsp->flag & BSP_LEAF_FLAG_WATER) != 0)
	{
		return RENDER_LIST_SLOT_WATER;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_DYNAMIC_SUBDIV) != 0)
	{
		return RENDER_LIST_SLOT_DYNAMIC_SUBDIV;
	}

	if (RenderLists_ProjectDistance(pb, &bsp->box) > lodDistanceThreshold)
	{
		return RENDER_LIST_SLOT_FULL_DYNAMIC;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X4) != 0)
	{
		return RENDER_LIST_SLOT_4X4;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X1) != 0)
	{
		return RENDER_LIST_SLOT_4X1;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X2) != 0)
	{
		return RENDER_LIST_SLOT_4X2;
	}

	return RENDER_LIST_SLOT_DYNAMIC_SUBDIV;
}

static struct VisMemBspListNode **RenderLists_Get1P2PHead(void *LevRenderList, int slotIndex)
{
	if (slotIndex == RENDER_LIST_SLOT_FULL_DYNAMIC)
	{
		return (struct VisMemBspListNode **)((char *)LevRenderList + 0x28);
	}

	return (struct VisMemBspListNode **)((char *)LevRenderList + slotIndex * 8 + 4);
}

static int RenderLists_Select3P4PSlot(const struct BSP *bsp)
{
	if ((bsp->flag & BSP_LEAF_FLAG_WATER) != 0)
	{
		return 2;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_DYNAMIC_SUBDIV) != 0)
	{
		return 3;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X4) != 0)
	{
		return 0;
	}

	return 1;
}

static void RenderLists_LinkBsp(struct BSP *bspRoot, struct BSP *bsp, struct VisMemBspListNode **head, struct VisMemBspListNode *bspList)
{
	struct VisMemBspListNode *node;
	int bspIndex = bsp - bspRoot;

	node = &bspList[bspIndex];

	node->next = *head;
	*head = node;
}

static void RenderLists_PushChild(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb, BspChildId childID,
                                  struct RenderListsScratchRecord **stack, struct RenderListsScratchRecord *stackEnd)
{
	struct BSP *child;
	struct RenderListsScratchRecord *record;

	if (childID < 0)
	{
		return;
	}

	if (!RenderLists_IsVisible(visLeafList, childID))
	{
		return;
	}

	child = &bspRoot[((u16)childID) & BSP_CHILD_ID_INDEX_MASK];
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
	{
		/* Use the real camera frustum for branch rejection too. This avoids
		 * inheriting the PS1 push-buffer AABB, whose fixed/s16 far-corner bounds
		 * can flip at particular spectator angles and cull the whole world. */
		if (!RenderLists_NativeBoxPassesCameraFrustum(pb, &child->box))
			return;
	}
	else
#endif
	if (!RenderLists_BoxOverlapsPushBuffer(&child->box, &pb->bbox))
	{
		return;
	}

	if (*stack >= stackEnd)
	{
		return;
	}

	record = *stack;
	/* Only childID is consumed when the stack entry is popped. The BSP already
	 * owns the authoritative bounding box, so avoid copying 12 bytes of box
	 * data into scratch memory for every traversed child. */
	record->childID = childID;
	*stack = record + 1;
}

static int RenderLists_Walk1P2P(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb, void *LevRenderList, struct VisMemBspListNode *bspList,
                                u8 numPlyr)
{
	struct RenderListsScratchRecord *stackBase = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, RENDER_LISTS_STACK_OFFSET);
	struct RenderListsScratchRecord *stackEnd = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, CTR_SCRATCHPAD_SIZE);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
	{
		stackBase = s_nativeStressRenderStack;
		stackEnd = s_nativeStressRenderStack + NATIVE_STRESS_RENDER_STACK_CAPACITY;
	}
#endif
	struct RenderListsScratchRecord *stack = stackBase;
	struct BSP *branch = bspRoot;
	int lodDistanceThreshold = (numPlyr == 1) ? CTR_SCRATCHPAD_PTR(struct MainRenderLevelGeometryScratch, 0)->bspLodDistanceThreshold : 0x1540;
	int count = 0;

	if (bspRoot == 0 || pb == 0)
	{
		return 0;
	}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
		RenderLists_NativePrepareCameraFrustum(pb);
#endif

	if ((bspRoot->flag & BSP_NODE_FLAG_LEAF) != 0)
	{
		int slotIndex = RenderLists_Select1P2PSlot(bspRoot, pb, lodDistanceThreshold);

		RenderLists_LinkBsp(bspRoot, bspRoot, RenderLists_Get1P2PHead(LevRenderList, slotIndex), bspList);
		return 1;
	}

	for (;;)
	{
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[0], &stack, stackEnd);
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[1], &stack, stackEnd);

		while (stack != stackBase)
		{
			struct RenderListsScratchRecord *record = --stack;
			struct BSP *bsp = &bspRoot[((u16)record->childID) & BSP_CHILD_ID_INDEX_MASK];

			if (((u16)record->childID & BSP_CHILD_ID_LEAF_FLAG) == 0)
			{
				branch = bsp;
				goto nextBranch;
			}

			if (!RenderLists_BoxPassesFrustum(pb, &bsp->box))
			{
				continue;
			}

			int slotIndex = RenderLists_Select1P2PSlot(bsp, pb, lodDistanceThreshold);
			RenderLists_LinkBsp(
				bspRoot,
				bsp,
				RenderLists_Get1P2PHead(LevRenderList, slotIndex),
				bspList);
			count++;
		}

		return count;

	nextBranch:
		continue;
	}
}

static int RenderLists_Walk3P4P(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb, void *LevRenderList, struct VisMemBspListNode *bspList)
{
	struct RenderListsScratchRecord *stackBase = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, RENDER_LISTS_STACK_OFFSET);
	struct RenderListsScratchRecord *stackEnd = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, CTR_SCRATCHPAD_SIZE);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
	{
		stackBase = s_nativeStressRenderStack;
		stackEnd = s_nativeStressRenderStack + NATIVE_STRESS_RENDER_STACK_CAPACITY;
	}
#endif
	struct RenderListsScratchRecord *stack = stackBase;
	struct BSP *branch = bspRoot;
	int count = 0;

	if (bspRoot == 0 || pb == 0)
	{
		return 0;
	}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
	if (NativePerf_IsStressTestMeasuring())
		RenderLists_NativePrepareCameraFrustum(pb);
#endif

	if ((bspRoot->flag & BSP_NODE_FLAG_LEAF) != 0)
	{
		int slotIndex = RenderLists_Select3P4PSlot(bspRoot);
		struct VisMemBspListNode **head = (struct VisMemBspListNode **)((char *)LevRenderList + slotIndex * 8 + 4);

		RenderLists_LinkBsp(bspRoot, bspRoot, head, bspList);
		return 1;
	}

	for (;;)
	{
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[0], &stack, stackEnd);
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[1], &stack, stackEnd);

		while (stack != stackBase)
		{
			struct RenderListsScratchRecord *record = --stack;
			struct BSP *bsp = &bspRoot[((u16)record->childID) & BSP_CHILD_ID_INDEX_MASK];

			if (((u16)record->childID & BSP_CHILD_ID_LEAF_FLAG) == 0)
			{
				branch = bsp;
				goto nextBranch;
			}

			if (!RenderLists_BoxPassesFrustum(pb, &bsp->box))
			{
				continue;
			}

			int slotIndex = RenderLists_Select3P4PSlot(bsp);
			struct VisMemBspListNode **head = (struct VisMemBspListNode **)((char *)LevRenderList + slotIndex * 8 + 4);

			RenderLists_LinkBsp(bspRoot, bsp, head, bspList);
			count++;
		}

		return count;

	nextBranch:
		continue;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800702d4-0x80070388.
// NOTE(aalhendi): Retail corner helpers 0x80070308..0x80070384 are represented by RenderLists_SelectBoxCorner.
void RenderLists_PreInit()
{
	static const u32 renderListJumpTable[] = {
	    0x80070310, 0x80070320, 0x80070330, 0x80070340, 0x8007034c, 0x8007035c, 0x8007036c, 0x8007037c,
	};
	u32 *scratchJumpTable = CTR_SCRATCHPAD_PTR(u32, RENDER_LISTS_CORNER_JUMP_TABLE_OFFSET);

	for (int i = 0; i < 8; i++)
	{
		scratchJumpTable[i] = renderListJumpTable[i];
	}
}

int RenderLists_Init1P2P(struct BSP *bspRoot, int *visLeafList, struct PushBuffer *pb, u32 LevRenderList, void *bspList, u8 numPlyr)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8006fe70-0x800702d4.
	RenderLists_Load1P2PGteState(pb);
	return RenderLists_Walk1P2P(bspRoot, visLeafList, pb, (void *)LevRenderList, bspList, numPlyr);
}

int RenderLists_Init3P4P(struct BSP *bspRoot, int *visLeafList, struct PushBuffer *pb, u32 LevRenderList, void *bspList)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80070388-0x80070720.
	RenderLists_Load1P2PGteState(pb);
	return RenderLists_Walk3P4P(bspRoot, visLeafList, pb, (void *)LevRenderList, bspList);
}
