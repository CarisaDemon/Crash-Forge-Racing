/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gte/PsyX_GTE.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include <psx/gtereg.h>
#include <psx/libgte.h>
#include <stdint.h>
#if defined(CTR_NATIVE)
#include <platform.h>
#include <ctr_scratchpad.h>
#include <platform/native_gte_projection.h>
#endif

GTERegisters gteRegs;

#define GTE_SF(op)    ((op >> 19) & 1)
#define GTE_MX(op)    ((op >> 17) & 3)
#define GTE_V(op)     ((op >> 15) & 3)
#define GTE_CV(op)    ((op >> 13) & 3)
#define GTE_LM(op)    ((op >> 10) & 1)
#define GTE_FUNCT(op) (op & 63)

#define gteop(code)   (code & 0x1ffffff)

#define VX(n)         (n < 3 ? gteRegs.CP2D.p[n << 1].sw.l : C2_IR1)
#define VY(n)         (n < 3 ? gteRegs.CP2D.p[n << 1].sw.h : C2_IR2)
#define VZ(n)         (n < 3 ? gteRegs.CP2D.p[(n << 1) + 1].sw.l : C2_IR3)
#define MX11(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3)].sw.l : -C2_R << 4)
#define MX12(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3)].sw.h : C2_R << 4)
#define MX13(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 1].sw.l : C2_IR0)
#define MX21(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 1].sw.h : C2_R13)
#define MX22(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 2].sw.l : C2_R13)
#define MX23(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 2].sw.h : C2_R13)
#define MX31(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 3].sw.l : C2_R22)
#define MX32(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 3].sw.h : C2_R22)
#define MX33(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 4].sw.l : C2_R22)
#define CV1(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 5].sd : 0)
#define CV2(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 6].sd : 0)
#define CV3(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 7].sd : 0)

global_variable int m_sf;
global_variable s64 m_mac0;
global_variable s64 m_mac1_raw;
global_variable s64 m_mac2_raw;
global_variable s64 m_mac3;

#if defined(CTR_NATIVE)
#define NATIVE_GTE_SUBPIXEL_BUCKETS (1u << 16)
#define NATIVE_GTE_SUBPIXEL_WAYS 8u
#define NATIVE_GTE_SUBPIXEL_MAP_SIZE (NATIVE_GTE_SUBPIXEL_BUCKETS * NATIVE_GTE_SUBPIXEL_WAYS)
#define NATIVE_GTE_SUBPIXEL_L1_SIZE (1u << 11)
#define NATIVE_GTE_SUBPIXEL_VALUE_BUCKETS (1u << 16)
#define NATIVE_GTE_SUBPIXEL_VALUE_WAYS 8u
#define NATIVE_GTE_SUBPIXEL_VALUE_MAP_SIZE (NATIVE_GTE_SUBPIXEL_VALUE_BUCKETS * NATIVE_GTE_SUBPIXEL_VALUE_WAYS)
#define NATIVE_GTE_SUBPIXEL_VALUE_L1_SIZE (1u << 11)
#define NATIVE_GTE_SUBPIXEL_VALUE_TOLERANCE 1024
#define NATIVE_GTE_DEPTH_AMBIGUITY_TOLERANCE 16u
struct NativeGTESubpixelEntry
{
	const void *address;
	u32 packedSxy;
	s32 x16;
	s32 y16;
	u16 depth;
	u8 depthValid;
	u8 _depthPad;
	u32 generation;
};
struct NativeGTESubpixelValueEntry
{
	u32 packedSxy;
	s32 x16;
	s32 y16;
	u16 depth;
	u8 depthValid;
	u8 depthAmbiguous;
	u32 generation;
	u8 ambiguous;
};

global_variable struct NativeGTESubpixelEntry s_nativeGTESubpixelMap[NATIVE_GTE_SUBPIXEL_MAP_SIZE];
global_variable struct NativeGTESubpixelEntry *s_nativeGTESubpixelL1[NATIVE_GTE_SUBPIXEL_L1_SIZE];
global_variable struct NativeGTESubpixelValueEntry s_nativeGTESubpixelValueMap[NATIVE_GTE_SUBPIXEL_VALUE_MAP_SIZE];
/* Tiny direct-mapped front cache for the value-keyed table. Adjacent PS1
 * polygons commonly reuse projected edge coordinates; a pointer cache turns
 * those repeats into one indexed load instead of rescanning an 8-way bucket.
 * Generation/key checks preserve exactly the same validity rules. */
global_variable struct NativeGTESubpixelValueEntry *s_nativeGTEValueL1[NATIVE_GTE_SUBPIXEL_VALUE_L1_SIZE];
global_variable struct NativeGTESubpixelEntry *s_nativeGTELastSubpixelEntry;
global_variable struct NativeGTESubpixelValueEntry *s_nativeGTELastValueEntry;

/* Native scratchpad addresses are only 0x400 bytes wide and are reused
 * constantly by level/model renderers. Keep an exact direct slot per byte
 * offset so scratch SXY stores/reads bypass the generic associative hash.
 * Non-scratch addresses still use the original hash path unchanged. */
global_variable struct NativeGTESubpixelEntry
	s_nativeGTEScratchSubpixel[CTR_SCRATCHPAD_SIZE];

internal struct NativeGTESubpixelEntry *NativeGTE_ScratchEntryForAddress(
	const void *address)
{
	uintptr_t base;
	uintptr_t ptr;
	uintptr_t offset;

	if ((address == NULL) || (gCTRNativeScratchpadBase == NULL))
		return NULL;

	base = (uintptr_t)gCTRNativeScratchpadBase;
	ptr = (uintptr_t)address;
	if ((ptr < base) || (ptr >= base + CTR_SCRATCHPAD_SIZE))
		return NULL;

	offset = ptr - base;
	return &s_nativeGTEScratchSubpixel[offset];
}
global_variable int s_nativeGTEWideModeCached = -1;
global_variable s32 s_nativeGTESxyX16[3];
global_variable s32 s_nativeGTESxyY16[3];
global_variable u16 s_nativeGTESxyDepth[3];
global_variable u8 s_nativeGTESxyDepthValid[3];
global_variable u8 s_nativeGTESxyValueSaved[3];
global_variable u32 s_nativeGTESubpixelGeneration = 1;

typedef struct NativeGTEPresentationMatrix
{
	const MATRIX *matrix;
	s64 mQ16[3][3];
	s64 tQ16[3];
	int valid;
} NativeGTEPresentationMatrix;

global_variable NativeGTEPresentationMatrix s_nativeGTEPresentationMatrix[4];
global_variable NativeGTEPresentationMatrix *s_nativeGTEActivePresentationMatrix;
global_variable int s_nativeGTEPresentationMatchDirty = 1;
global_variable int s_nativeGTEPresentationMatchCached = 0;

void NativeGTE_InvalidatePresentationMatrixMatch(void)
{
	s_nativeGTEPresentationMatchDirty = 1;
}

void NativeGTE_RegisterPresentationMatrix(const MATRIX *matrix, const s64 *mQ16, const s64 *tQ16)
{
	NativeGTEPresentationMatrix *slot = NULL;
	if ((matrix == NULL) || (mQ16 == NULL) || (tQ16 == NULL)) return;
	for (int i = 0; i < 4; i++)
	{
		if (s_nativeGTEPresentationMatrix[i].valid && s_nativeGTEPresentationMatrix[i].matrix == matrix)
		{
			slot = &s_nativeGTEPresentationMatrix[i];
			break;
		}
		if ((slot == NULL) && !s_nativeGTEPresentationMatrix[i].valid)
			slot = &s_nativeGTEPresentationMatrix[i];
	}
	if (slot == NULL) slot = &s_nativeGTEPresentationMatrix[0];
	slot->matrix = matrix;
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++) slot->mQ16[r][c] = mQ16[r * 3 + c];
		slot->tQ16[r] = tQ16[r];
	}
	slot->valid = 1;
	if (s_nativeGTEActivePresentationMatrix == slot)
	{
		NativeGTE_InvalidatePresentationMatrixMatch();
	}
}

void NativeGTE_ClearPresentationMatrix(const MATRIX *matrix)
{
	if (matrix == NULL) return;
	for (int i = 0; i < 4; i++)
	{
		NativeGTEPresentationMatrix *slot = &s_nativeGTEPresentationMatrix[i];
		if (slot->valid && slot->matrix == matrix)
		{
			if (s_nativeGTEActivePresentationMatrix == slot)
			{
				s_nativeGTEActivePresentationMatrix = NULL;
				NativeGTE_InvalidatePresentationMatrixMatch();
			}
			memset(slot, 0, sizeof(*slot));
		}
	}
}

void NativeGTE_ClearAllPresentationMatrices(void)
{
	memset(s_nativeGTEPresentationMatrix, 0, sizeof(s_nativeGTEPresentationMatrix));
	s_nativeGTEActivePresentationMatrix = NULL;
	NativeGTE_InvalidatePresentationMatrixMatch();
}

void NativeGTE_ActivatePresentationMatrix(const MATRIX *matrix)
{
	s_nativeGTEActivePresentationMatrix = NULL;
	NativeGTE_InvalidatePresentationMatrixMatch();
	if (matrix == NULL) return;
	for (int i = 0; i < 4; i++)
	{
		NativeGTEPresentationMatrix *slot = &s_nativeGTEPresentationMatrix[i];
		if (slot->valid && slot->matrix == matrix)
		{
			s_nativeGTEActivePresentationMatrix = slot;
			break;
		}
	}
}

internal int NativeGTE_PresentationMatrixMatchesGte(void)
{
	const MATRIX *m;

	if (!s_nativeGTEPresentationMatchDirty)
	{
		return s_nativeGTEPresentationMatchCached;
	}

	s_nativeGTEPresentationMatchDirty = 0;
	s_nativeGTEPresentationMatchCached = 0;

	if ((s_nativeGTEActivePresentationMatrix == NULL) || !s_nativeGTEActivePresentationMatrix->valid)
	{
		return 0;
	}

	m = s_nativeGTEActivePresentationMatrix->matrix;
	if (m == NULL)
	{
		return 0;
	}

	s_nativeGTEPresentationMatchCached =
		(C2_R11 == m->m[0][0]) && (C2_R12 == m->m[0][1]) && (C2_R13 == m->m[0][2]) &&
		(C2_R21 == m->m[1][0]) && (C2_R22 == m->m[1][1]) && (C2_R23 == m->m[1][2]) &&
		(C2_R31 == m->m[2][0]) && (C2_R32 == m->m[2][1]) && (C2_R33 == m->m[2][2]) &&
		(C2_TRX == m->t[0]) && (C2_TRY == m->t[1]) && (C2_TRZ == m->t[2]);

	return s_nativeGTEPresentationMatchCached;
}

/*
 * The smooth-presentation path needs floor((H << 32) / viewZQ16), clamped
 * to the GTE's 17-bit reciprocal range. On 32-bit x86, a signed 64/64 divide
 * is substantially more expensive than the hardware floating-point divide.
 *
 * Use double only to guess the quotient, then verify/correct it against the
 * original integer numerator/denominator. The returned integer is therefore
 * bit-identical to the old division; the integer fallback remains for any
 * unexpected range/rounding case.
 */
internal inline s64 NativeGTE_PresentationRatioQ16Fast(s64 viewZQ16)
{
	const s64 maxRatio = 0x1ffff;
	const s64 numerator = (s64)C2_H << 32;
	s64 q;
	s64 product;

	if ((viewZQ16 <= 0) || (numerator <= 0))
		return 0;

	/* This range guard is far above any CTR camera-space depth, but keeps the
	   corrective multiplication provably inside signed 64-bit. */
	if (viewZQ16 > (s64)0x3fffffffffffLL)
	{
		q = numerator / viewZQ16;
		return (q > maxRatio) ? maxRatio : q;
	}

	q = (s64)((double)numerator / (double)viewZQ16);
	if (q < 0)
		q = 0;
	if (q > maxRatio)
		q = maxRatio;

	product = q * viewZQ16;

	if (product > numerator)
	{
		if (q > 0)
		{
			q--;
			product -= viewZQ16;
		}
	}
	else if ((q < maxRatio) && ((numerator - product) >= viewZQ16))
	{
		q++;
		product += viewZQ16;
	}

	/* A correctly rounded double estimate is at most one away here for CTR's
	   numeric range. If that ever stops being true, preserve exact behavior. */
	if ((product > numerator) ||
	    ((q < maxRatio) && ((numerator - product) >= viewZQ16)))
	{
		q = numerator / viewZQ16;
		if (q > maxRatio)
			q = maxRatio;
	}

	return q;
}

void NativeGTE_SubpixelBeginFrame(void)
{
	s_nativeGTESxyValueSaved[0] = 0;
	s_nativeGTESxyValueSaved[1] = 0;
	s_nativeGTESxyValueSaved[2] = 0;
	s_nativeGTELastSubpixelEntry = NULL;
	s_nativeGTELastValueEntry = NULL;
	s_nativeGTEWideModeCached = Platform_GetAspectRatioMode();
	s_nativeGTESubpixelGeneration++;
	if (s_nativeGTESubpixelGeneration == 0)
	{
		memset(s_nativeGTESubpixelMap, 0, sizeof(s_nativeGTESubpixelMap));
		memset(s_nativeGTESubpixelValueMap, 0, sizeof(s_nativeGTESubpixelValueMap));
		memset(s_nativeGTEScratchSubpixel, 0, sizeof(s_nativeGTEScratchSubpixel));
		s_nativeGTESubpixelGeneration = 1;
	}
}

internal u32 NativeGTE_SubpixelHash(const void *address)
{
	const u32 key = (u32)(uintptr_t)address;
	const u32 bucket = ((key >> 2) ^ (key >> 11) ^ (key >> 19)) & (NATIVE_GTE_SUBPIXEL_BUCKETS - 1);
	return bucket * NATIVE_GTE_SUBPIXEL_WAYS;
}

internal u32 NativeGTE_SubpixelL1Index(const void *address)
{
	const u32 key = (u32)(uintptr_t)address;
	return ((key >> 2) ^ (key >> 13) ^ (key >> 21)) &
	       (NATIVE_GTE_SUBPIXEL_L1_SIZE - 1);
}

internal u32 NativeGTE_SubpixelValueHash(u32 packedSxy)
{
	const u32 mixed = packedSxy * 2654435761u;
	return ((mixed ^ (mixed >> 16)) & (NATIVE_GTE_SUBPIXEL_VALUE_BUCKETS - 1)) * NATIVE_GTE_SUBPIXEL_VALUE_WAYS;
}

internal struct NativeGTESubpixelValueEntry *NativeGTE_FindSXYValueEntry(u32 packedSxy)
{
	struct NativeGTESubpixelValueEntry *entry = s_nativeGTELastValueEntry;
	if ((entry != NULL) &&
	    (entry->generation == s_nativeGTESubpixelGeneration) &&
	    (entry->packedSxy == packedSxy))
	{
		return entry;
	}

	const u32 base = NativeGTE_SubpixelValueHash(packedSxy);
	const u32 l1Index =
		(base >> 3) & (NATIVE_GTE_SUBPIXEL_VALUE_L1_SIZE - 1);

	entry = s_nativeGTEValueL1[l1Index];
	if ((entry != NULL) &&
	    (entry->generation == s_nativeGTESubpixelGeneration) &&
	    (entry->packedSxy == packedSxy))
	{
		s_nativeGTELastValueEntry = entry;
		return entry;
	}

	for (u32 way = 0; way < NATIVE_GTE_SUBPIXEL_VALUE_WAYS; way++)
	{
		entry = &s_nativeGTESubpixelValueMap[base + way];
		if ((entry->generation == s_nativeGTESubpixelGeneration) &&
		    (entry->packedSxy == packedSxy))
		{
			s_nativeGTEValueL1[l1Index] = entry;
			s_nativeGTELastValueEntry = entry;
			return entry;
		}
	}

	return NULL;
}

internal struct NativeGTESubpixelEntry *NativeGTE_FindStoredSXYEntry(const void *address, u32 packedSxy)
{
	struct NativeGTESubpixelEntry *entry =
		NativeGTE_ScratchEntryForAddress(address);

	if ((entry != NULL) &&
	    (entry->generation == s_nativeGTESubpixelGeneration) &&
	    (entry->address == address) &&
	    (entry->packedSxy == packedSxy))
	{
		s_nativeGTELastSubpixelEntry = entry;
		return entry;
	}

	entry = s_nativeGTELastSubpixelEntry;
	if ((entry != NULL) &&
	    (entry->generation == s_nativeGTESubpixelGeneration) &&
	    (entry->address == address) &&
	    (entry->packedSxy == packedSxy))
	{
		return entry;
	}

	if (address == NULL)
	{
		return NULL;
	}

	const u32 l1Index = NativeGTE_SubpixelL1Index(address);
	entry = s_nativeGTESubpixelL1[l1Index];
	if ((entry != NULL) &&
	    (entry->generation == s_nativeGTESubpixelGeneration) &&
	    (entry->address == address) &&
	    (entry->packedSxy == packedSxy))
	{
		s_nativeGTELastSubpixelEntry = entry;
		return entry;
	}

	const u32 base = NativeGTE_SubpixelHash(address);
	for (u32 way = 0; way < NATIVE_GTE_SUBPIXEL_WAYS; way++)
	{
		entry = &s_nativeGTESubpixelMap[base + way];
		if ((entry->generation == s_nativeGTESubpixelGeneration) &&
		    (entry->address == address) &&
		    (entry->packedSxy == packedSxy))
		{
			s_nativeGTESubpixelL1[l1Index] = entry;
			s_nativeGTELastSubpixelEntry = entry;
			return entry;
		}
	}

	return NULL;
}

internal int NativeGTE_GetWideModeCached(void)
{
	if (s_nativeGTEWideModeCached < 0)
	{
		s_nativeGTEWideModeCached = Platform_GetAspectRatioMode();
	}
	return s_nativeGTEWideModeCached;
}

internal s64 NativeGTE_SubpixelAbsDiff(s32 a, s32 b)
{
	s64 d = (s64)a - (s64)b;
	return (d < 0) ? -d : d;
}

internal void NativeGTE_SaveSXYValue(u32 packedSxy, s32 x16, s32 y16, u16 depth, int depthValid)
{
	const u32 base = NativeGTE_SubpixelValueHash(packedSxy);
	const u32 l1Index =
		(base >> 3) & (NATIVE_GTE_SUBPIXEL_VALUE_L1_SIZE - 1);
	struct NativeGTESubpixelValueEntry *entry = s_nativeGTELastValueEntry;

	/* Shared level edges frequently project to the same packed PS1 SXY.
	   Probe the exact same last/L1 front caches used by the read path before
	   rescanning the 8-way backing bucket. Key/generation checks keep the
	   backing-table semantics unchanged. */
	if (!((entry != NULL) &&
	      (entry->generation == s_nativeGTESubpixelGeneration) &&
	      (entry->packedSxy == packedSxy)))
	{
		entry = s_nativeGTEValueL1[l1Index];
		if (!((entry != NULL) &&
		      (entry->generation == s_nativeGTESubpixelGeneration) &&
		      (entry->packedSxy == packedSxy)))
		{
			entry = NULL;
		}
	}

	if (entry == NULL)
	{
		for (u32 way = 0; way < NATIVE_GTE_SUBPIXEL_VALUE_WAYS; way++)
		{
			struct NativeGTESubpixelValueEntry *candidate =
				&s_nativeGTESubpixelValueMap[base + way];
			if ((candidate->generation == s_nativeGTESubpixelGeneration) &&
			    (candidate->packedSxy == packedSxy))
			{
				entry = candidate;
				break;
			}
			if ((entry == NULL) &&
			    (candidate->generation != s_nativeGTESubpixelGeneration))
			{
				entry = candidate;
			}
		}
	}

	if (entry == NULL)
	{
		entry = &s_nativeGTESubpixelValueMap[base + ((packedSxy >> 5) & (NATIVE_GTE_SUBPIXEL_VALUE_WAYS - 1))];
		entry->generation = 0;
	}

	if ((entry->generation == s_nativeGTESubpixelGeneration) && (entry->packedSxy == packedSxy))
	{
		s_nativeGTEValueL1[l1Index] = entry;
		s_nativeGTELastValueEntry = entry;
		/* Keep the first fractional projection for this packed PS1 coordinate.
		   For perspective data, detect when the same packed SXY represents
		   meaningfully different depths and disable the value-keyed fallback. */
		if (depthValid)
		{
			if (entry->depthValid)
			{
				u32 a = entry->depth;
				u32 b = depth;
				u32 diff = a > b ? a - b : b - a;
				if (diff > NATIVE_GTE_DEPTH_AMBIGUITY_TOLERANCE)
					entry->depthAmbiguous = 1;
			}
			else
			{
				entry->depth = depth;
				entry->depthValid = 1;
			}
		}
		return;
	}

	entry->packedSxy = packedSxy;
	entry->x16 = x16;
	entry->y16 = y16;
	entry->depth = depth;
	entry->depthValid = depthValid ? 1 : 0;
	entry->depthAmbiguous = 0;
	entry->generation = s_nativeGTESubpixelGeneration;
	entry->ambiguous = 0;
	s_nativeGTEValueL1[l1Index] = entry;
	s_nativeGTELastValueEntry = entry;
}

internal int NativeGTE_GetSXYValueFixed(u32 packedSxy, s32 *x16, s32 *y16)
{
	const struct NativeGTESubpixelValueEntry *entry = NativeGTE_FindSXYValueEntry(packedSxy);
	if (entry == NULL)
	{
		return 0;
	}
	if (x16) *x16 = entry->x16;
	if (y16) *y16 = entry->y16;
	return 1;
}

internal int NativeGTE_GetSXYValueDepth(u32 packedSxy, u16 *depth)
{
	const struct NativeGTESubpixelValueEntry *entry = NativeGTE_FindSXYValueEntry(packedSxy);
	if ((entry == NULL) || !entry->depthValid || entry->depthAmbiguous)
	{
		return 0;
	}
	if (depth) *depth = entry->depth;
	return 1;
}

internal int NativeGTE_GetCanonicalSXYFixed(u32 packedSxy, u16 depth, int depthValid, s32 *x16, s32 *y16)
{
	const struct NativeGTESubpixelValueEntry *entry = NativeGTE_FindSXYValueEntry(packedSxy);
	if (entry == NULL)
	{
		return 0;
	}

	/* A packed PS1 screen coordinate can legitimately be reused by unrelated
	   geometry. Only canonicalize when camera-space depth agrees too. Shared
	   level edges then resolve to one identical fractional SXY regardless of
	   which packet copied/stored the vertex. */
	if (depthValid)
	{
		u32 a, b, diff;
		if (!entry->depthValid) return 0;
		a = entry->depth;
		b = depth;
		diff = a > b ? a - b : b - a;
		if (diff > NATIVE_GTE_DEPTH_AMBIGUITY_TOLERANCE) return 0;
	}
	else if (entry->depthValid)
	{
		return 0;
	}

	if (x16) *x16 = entry->x16;
	if (y16) *y16 = entry->y16;
	return 1;
}

internal int NativeGTE_GetSXYValue(u32 packedSxy, float *x, float *y)
{
	s32 x16, y16;
	if (!x || !y || !NativeGTE_GetSXYValueFixed(packedSxy, &x16, &y16)) return 0;
	*x = (float)x16 / 65536.0f;
	*y = (float)y16 / 65536.0f;
	return 1;
}

internal u32 NativeGTE_PackFixedSxy(s32 x16, s32 y16)
{
	const s16 x = (s16)(x16 >> 16);
	const s16 y = (s16)(y16 >> 16);
	return (u32)(u16)x | ((u32)(u16)y << 16);
}

static void NativeGTE_SaveSXY(void *address, u32 packedSxy, s32 x16, s32 y16, u16 depth, int depthValid)
{
	struct NativeGTESubpixelEntry *entry;
	if (!address) return;

	entry = NativeGTE_ScratchEntryForAddress(address);
	if (entry == NULL)
	{
		const u32 base = NativeGTE_SubpixelHash(address);
		entry = NULL;

		// Four-way set association prevents unrelated packet addresses from
		// evicting each other's fractional SXY within the same frame. The old
		// direct-mapped table could leave only some vertices of a polygon with
		// subpixel data, which visibly increased wobble.
		for (u32 way = 0; way < NATIVE_GTE_SUBPIXEL_WAYS; way++)
		{
			struct NativeGTESubpixelEntry *candidate = &s_nativeGTESubpixelMap[base + way];
			if ((candidate->generation == s_nativeGTESubpixelGeneration) && (candidate->address == address))
			{
				entry = candidate;
				break;
			}
			if ((entry == NULL) && (candidate->generation != s_nativeGTESubpixelGeneration))
			{
				entry = candidate;
			}
		}
		if (entry == NULL)
		{
			// Extremely rare 5th collision in one bucket: deterministically replace
			// one way rather than corrupting a neighbouring bucket.
			entry = &s_nativeGTESubpixelMap[base + ((u32)(uintptr_t)address >> 4 & (NATIVE_GTE_SUBPIXEL_WAYS - 1))];
		}
	}

	entry->address = address;
	entry->packedSxy = packedSxy;
	entry->x16 = x16;
	entry->y16 = y16;
	entry->depth = depth;
	entry->depthValid = depthValid ? 1 : 0;
	entry->generation = s_nativeGTESubpixelGeneration;
	s_nativeGTESubpixelL1[NativeGTE_SubpixelL1Index(address)] = entry;
	s_nativeGTELastSubpixelEntry = entry;
}

void NativeGTE_RecordSXYStore(void *address, int fifoIndex)
{
	u32 packedSxy;
	if (fifoIndex<0 || fifoIndex>2) return;
	packedSxy = gteRegs.CP2D.p[12+fifoIndex].d;
	NativeGTE_SaveSXY(address, packedSxy,
	 s_nativeGTESxyX16[fifoIndex],s_nativeGTESxyY16[fifoIndex],
	 s_nativeGTESxyDepth[fifoIndex],s_nativeGTESxyDepthValid[fifoIndex]);
	// RTPS/RTPT already inserted the address-independent projection key.
	// Only add it here for values that entered SXY through a manual load/copy.
	if (!s_nativeGTESxyValueSaved[fifoIndex])
	{
		NativeGTE_SaveSXYValue(packedSxy,
		 s_nativeGTESxyX16[fifoIndex],s_nativeGTESxyY16[fifoIndex],
		 s_nativeGTESxyDepth[fifoIndex],s_nativeGTESxyDepthValid[fifoIndex]);
		s_nativeGTESxyValueSaved[fifoIndex] = 1;
	}
}

int NativeGTE_GetCurrentSubpixelFixed(int fifoIndex, s32 *x16, s32 *y16)
{
	if ((fifoIndex < 0) || (fifoIndex > 2) || (x16 == NULL) || (y16 == NULL))
		return 0;

	*x16 = s_nativeGTESxyX16[fifoIndex];
	*y16 = s_nativeGTESxyY16[fifoIndex];
	return 1;
}

int NativeGTE_GetStoredSubpixelFixed(const void *address, u32 packedSxy, s32 *x16, s32 *y16)
{
	const struct NativeGTESubpixelEntry *entry;

	if ((address == NULL) || (x16 == NULL) || (y16 == NULL))
	{
		return 0;
	}

	entry = NativeGTE_FindStoredSXYEntry(address, packedSxy);
	if (entry == NULL)
	{
		return NativeGTE_GetSXYValueFixed(packedSxy, x16, y16);
	}

	/* A propagated packet may explicitly carry only the integer PS1 position.
	   Prefer an address-independent fractional projection if one exists. */
	{
		const s32 integerX16 = (s32)(s16)packedSxy * 65536;
		const s32 integerY16 = (s32)(s16)(packedSxy >> 16) * 65536;
		if ((entry->x16 == integerX16) && (entry->y16 == integerY16) &&
		    NativeGTE_GetSXYValueFixed(packedSxy, x16, y16))
		{
			return 1;
		}
	}

	/* Prefer one canonical fractional coordinate for shared geometry edges.
	   Address-local metadata can differ slightly after packet copies/subdivide;
	   using it independently opens visible cracks between adjacent polygons. */
	if (NativeGTE_GetCanonicalSXYFixed(packedSxy, entry->depth, entry->depthValid, x16, y16))
		return 1;

	*x16 = entry->x16;
	*y16 = entry->y16;
	return 1;
}

/* Native OBJ culling needs the exact projection that belongs to this
   specific vertex store. The canonical value-keyed path above intentionally
   merges shared level edges, but unrelated small OBJ vertices can land on the
   same integer SXY/depth and must not borrow each other's fractional position. */
int NativeGTE_GetStoredSubpixelFixedLocal(const void *address, u32 packedSxy, s32 *x16, s32 *y16)
{
	const struct NativeGTESubpixelEntry *entry;

	if ((address == NULL) || (x16 == NULL) || (y16 == NULL))
		return 0;

	entry = NativeGTE_FindStoredSXYEntry(address, packedSxy);
	if (entry != NULL)
	{
		*x16 = entry->x16;
		*y16 = entry->y16;
		return 1;
	}

	/* If local metadata was lost, fall back to the literal PS1 coordinate,
	   never to another vertex's canonical fractional projection. */
	*x16 = (s32)(s16)packedSxy * 65536;
	*y16 = (s32)(s16)(packedSxy >> 16) * 65536;
	return 1;
}

int NativeGTE_GetStoredSubpixel(const void *address, u32 packedSxy, float *x, float *y)
{
	s32 x16, y16;
	if (!x || !y || !NativeGTE_GetStoredSubpixelFixed(address, packedSxy, &x16, &y16)) return 0;
	*x = (float)x16 / 65536.0f;
	*y = (float)y16 / 65536.0f;
	return 1;
}

int NativeGTE_GetStoredDepth(const void *address, u32 packedSxy, u16 *depth)
{
	const struct NativeGTESubpixelEntry *entry;

	if (depth == NULL) return 0;

	entry = NativeGTE_FindStoredSXYEntry(address, packedSxy);
	if ((entry != NULL) && entry->depthValid)
	{
		*depth = entry->depth;
		return 1;
	}

	return NativeGTE_GetSXYValueDepth(packedSxy, depth);
}

/* Combined renderer lookup. Bit 0 means x16/y16 are valid, bit 1 means depth
 * is valid. The legacy helpers above intentionally remain for other callers.
 *
 * The native GPU builder used to look up the same address/packed-SXY twice per
 * vertex: once for subpixel coordinates and again for perspective depth. Keep
 * exactly the same canonical/fallback rules while sharing the local and value
 * hash lookups. */
int NativeGTE_GetStoredVertexMetadata(
	const void *address, u32 packedSxy,
	s32 *x16, s32 *y16, u16 *depth)
{
	const struct NativeGTESubpixelEntry *localEntry;
	const struct NativeGTESubpixelValueEntry *valueEntry = NULL;
	const int wantSubpixel = (x16 != NULL) && (y16 != NULL);
	const int wantDepth = (depth != NULL);
	int result = 0;

	if (!wantSubpixel && !wantDepth)
		return 0;

	localEntry = NativeGTE_FindStoredSXYEntry(address, packedSxy);

	if (wantSubpixel ||
	    (wantDepth && ((localEntry == NULL) || !localEntry->depthValid)))
	{
		valueEntry = NativeGTE_FindSXYValueEntry(packedSxy);
	}

	if (wantSubpixel && (address != NULL))
	{
		if (localEntry == NULL)
		{
			if (valueEntry != NULL)
			{
				*x16 = valueEntry->x16;
				*y16 = valueEntry->y16;
				result |= 1;
			}
		}
		else
		{
			const s32 integerX16 = (s32)(s16)packedSxy * 65536;
			const s32 integerY16 = (s32)(s16)(packedSxy >> 16) * 65536;
			int useValue = 0;

			/* Match NativeGTE_GetStoredSubpixelFixed: an explicitly integer
			 * local store prefers any value-keyed fractional projection. */
			if ((localEntry->x16 == integerX16) &&
			    (localEntry->y16 == integerY16) &&
			    (valueEntry != NULL))
			{
				useValue = 1;
			}
			else if (valueEntry != NULL)
			{
				/* Match NativeGTE_GetCanonicalSXYFixed without performing a
				 * second value-table lookup. */
				if (localEntry->depthValid)
				{
					if (valueEntry->depthValid)
					{
						const u32 a = localEntry->depth;
						const u32 b = valueEntry->depth;
						const u32 diff = (a > b) ? (a - b) : (b - a);
						if (diff <= NATIVE_GTE_DEPTH_AMBIGUITY_TOLERANCE)
							useValue = 1;
					}
				}
				else if (!valueEntry->depthValid)
				{
					useValue = 1;
				}
			}

			if (useValue)
			{
				*x16 = valueEntry->x16;
				*y16 = valueEntry->y16;
			}
			else
			{
				*x16 = localEntry->x16;
				*y16 = localEntry->y16;
			}
			result |= 1;
		}
	}

	if (wantDepth)
	{
		if ((localEntry != NULL) && localEntry->depthValid)
		{
			*depth = localEntry->depth;
			result |= 2;
		}
		else if ((valueEntry != NULL) &&
		         valueEntry->depthValid &&
		         !valueEntry->depthAmbiguous)
		{
			*depth = valueEntry->depth;
			result |= 2;
		}
	}

	return result;
}

void NativeGTE_CopySXYStore(void *dst, const void *src, u32 packed)
{
	s32 x16, y16;
	u16 depth = 0;
	const int metadata =
		NativeGTE_GetStoredVertexMetadata(
			src, packed, &x16, &y16, &depth);
	const int depthValid = (metadata & 2) != 0;

	if ((metadata & 1) == 0)
	{
		x16 = (s32)(s16)packed * 65536;
		y16 = (s32)(s16)(packed >> 16) * 65536;
	}

	NativeGTE_SaveSXY(dst, packed, x16, y16, depth, depthValid);
}

void NativeGTE_SaveSXYStoreExact(
	void *dst, u32 packed, s32 x16, s32 y16, u16 depth)
{
	/* Native OBJ rigid projection caches already own the exact local subpixel
	   coordinate and GTE depth. Avoid re-querying source-address/value hash
	   tables merely to copy metadata into the emitted packet. */
	NativeGTE_SaveSXY(dst, packed, x16, y16, depth, 1);
}

void NativeGTE_LoadSXYStore(const void *src, u32 packed, int index)
{
	s32 x16, y16;
	u16 depth = 0;
	const int metadata =
		NativeGTE_GetStoredVertexMetadata(
			src, packed, &x16, &y16, &depth);
	const int depthValid = (metadata & 2) != 0;

	if ((metadata & 1) == 0)
	{
		x16 = (s32)(s16)packed * 65536;
		y16 = (s32)(s16)(packed >> 16) * 65536;
	}

	s_nativeGTESxyX16[index] = x16;
	s_nativeGTESxyY16[index] = y16;
	s_nativeGTESxyDepth[index] = depth;
	s_nativeGTESxyDepthValid[index] = depthValid ? 1 : 0;
	s_nativeGTESxyValueSaved[index] = 0;
}
void NativeGTE_CopySXYFIFO(int src, int dst)
{
	s_nativeGTESxyX16[dst]=s_nativeGTESxyX16[src];
	s_nativeGTESxyY16[dst]=s_nativeGTESxyY16[src];
	s_nativeGTESxyDepth[dst]=s_nativeGTESxyDepth[src];
	s_nativeGTESxyDepthValid[dst]=s_nativeGTESxyDepthValid[src];
	s_nativeGTESxyValueSaved[dst]=s_nativeGTESxyValueSaved[src];
}
void NativeGTE_WriteSXYRegister(u32 packed, int reg)
{
	if (reg<12 || reg>15) return;
	if (reg==15)
	{
		NativeGTE_CopySXYFIFO(1,0); NativeGTE_CopySXYFIFO(2,1); reg=14;
	}
	s_nativeGTESxyX16[reg-12]=(s32)(s16)packed*65536;
	s_nativeGTESxyY16[reg-12]=(s32)(s16)(packed>>16)*65536;
	s_nativeGTESxyDepth[reg-12]=0;
	s_nativeGTESxyDepthValid[reg-12]=0;
	s_nativeGTESxyValueSaved[reg-12]=0;
}

#endif

u32 gte_leadingzerocount(u32 lzcs)
{
	u32 lzcr = ((s32)lzcs < 0) ? ~lzcs : lzcs;
	local_persist const char debruijn32[32] = {0, 31, 9, 30, 3, 8,  13, 29, 2,  5,  7,  21, 12, 24, 28, 19,
	                                           1, 10, 4, 14, 6, 22, 25, 20, 11, 15, 23, 26, 16, 27, 17, 18};

	if (!lzcr)
	{
		return 32;
	}

#if defined(CTR_NATIVE) && defined(__GNUC__)
	/* Exact GTE LZCR semantics, native single-instruction leading-zero.
	 * Signed input has already been complemented above. */
	return (u32)__builtin_clz(lzcr);
#endif

	lzcr |= lzcr >> 1;
	lzcr |= lzcr >> 2;
	lzcr |= lzcr >> 4;
	lzcr |= lzcr >> 8;
	lzcr |= lzcr >> 16;
	lzcr++;

	return debruijn32[lzcr * 0x076be629 >> 27];
}

int LIM(int value, int max, int min, unsigned int flag)
{
	if (value > max)
	{
		C2_FLAG |= flag;
		return max;
	}
	else if (value < min)
	{
		C2_FLAG |= flag;
		return min;
	}

	return value;
}

internal inline s64 gte_shift(s64 a, int sf)
{
	if (sf > 0)
	{
		return a >> 12;
	}
	else if (sf < 0)
	{
		return a << 12;
	}

	return a;
}

internal int BOUNDS(/*int44*/ s64 value, int max_flag, int min_flag)
{
	if (value /*.positive_overflow()*/ > (s64)0x7ffffffffff)
	{
		C2_FLAG |= max_flag;
	}

	if (value /*.negative_overflow()*/ < (s64)-0x8000000000)
	{
		C2_FLAG |= min_flag;
	}

	return (int)(gte_shift(value /*.value()*/, m_sf));
}

internal u32 gte_divide(u16 numerator, u16 denominator)
{
	if (numerator < (denominator * 2))
	{
		local_persist const u8 table[] = {
		    0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3, 0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5,
		    0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8, 0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
		    0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a, 0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90,
		    0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
		    0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d,
		    0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
		    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35,
		    0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f, 0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
		    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15,
		    0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11, 0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
		    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00};

		int shift = gte_leadingzerocount(denominator) - 16;

		int r1 = (denominator << shift) & 0x7fff;
		int r2 = table[((r1 + 0x40) >> 7)] + 0x101;
		int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
		u32 reciprocal = ((r2 * r3) + 0x80) >> 8;

		return (u32)((((u64)reciprocal * (numerator << shift)) + 0x8000) >> 16);
	}

	return 0xffffffff;
}

#if defined(CTR_NATIVE)
/* Exact GTE reciprocal LUT for the high-volume level RTPS/RTPT path.
 * CTR often projects thousands of vertices with the same screen distance
 * and recurring 16-bit SZ. Memoize the bit-exact retail divider; when the
 * numerator changes or the 16K-entry low-SZ set collides, recompute exactly.
 * The C2_FLAG saturation remains in Lm_E, after this function.
 * A/B: place debug/gte_division_cache.disable before launch. */
#define NATIVE_GTE_DIV_CACHE_ENTRIES 16384u
struct NativeGTEDivisionCacheEntry
{
	u32 result;
	u16 h;
	u16 sz;
	u8 valid;
};
static struct NativeGTEDivisionCacheEntry
	s_nativeGTEDivisionCache[NATIVE_GTE_DIV_CACHE_ENTRIES];

/* A/B candidate: exact 16-bit direct-index GTE reciprocal cache.
 * No hash collisions; 512 KiB resident table, indexed by depth.
 * Does not alter any PS1 flags, saturation or reciprocal bits.
 * Enable only if debug/gte_dense_cache.enable exists at launch.
 * Leaving the marker absent gives the exact previous path. */
struct NativeGTEDenseDivisionEntry
{
	u32 result;
	u16 h;
	u8 valid;
};
static struct NativeGTEDenseDivisionEntry
	s_nativeGTEDenseDivisionCache[65536];
static int s_nativeGTEDenseChecked;
static int s_nativeGTEDenseEnabled;

static u32 NativeGTE_DivideCached(u16 h, u16 sz)
{
	static int checked = 0;
	static int enabled = 1;
	struct NativeGTEDivisionCacheEntry *entry;
	u32 result;

	if (!s_nativeGTEDenseChecked)
	{
		FILE *marker = fopen("debug/gte_dense_cache.enable", "rb");
		s_nativeGTEDenseEnabled = marker != NULL;
		if (marker) fclose(marker);
		s_nativeGTEDenseChecked = 1;
	}
	if (s_nativeGTEDenseEnabled)
	{
		struct NativeGTEDenseDivisionEntry *dense =
			&s_nativeGTEDenseDivisionCache[sz];
		if (dense->valid && dense->h == h)
			return dense->result;
		result = gte_divide(h, sz);
		dense->result = result;
		dense->h = h;
		dense->valid = 1;
		return result;
	}

	if (!checked)
	{
		FILE *disableMarker = fopen("debug/gte_division_cache.disable", "rb");
		enabled = disableMarker == NULL;
		if (disableMarker) fclose(disableMarker);
		checked = 1;
	}

	if (!enabled)
		return gte_divide(h, sz);

	entry = &s_nativeGTEDivisionCache[(u32)sz & (NATIVE_GTE_DIV_CACHE_ENTRIES - 1)];
	if (entry->valid && entry->sz == sz && entry->h == h)
		return entry->result;

	result = gte_divide(h, sz);
	entry->result = result;
	entry->h = h;
	entry->sz = sz;
	entry->valid = 1;
	return result;
}
#endif

/* Setting bits 12 & 19-22 in FLAG does not set bit 31 */

internal int A1(/*int44*/ s64 a)
{
	return BOUNDS(a, (1 << 31) | (1 << 30), (1 << 31) | (1 << 27));
}
internal int A2(/*int44*/ s64 a)
{
	return BOUNDS(a, (1 << 31) | (1 << 29), (1 << 31) | (1 << 26));
}
internal int A3(/*int44*/ s64 a)
{
	m_mac3 = a;
	return BOUNDS(a, (1 << 31) | (1 << 28), (1 << 31) | (1 << 25));
}
internal int Lm_B1(int a, int lm)
{
	return LIM(a, 0x7fff, -0x8000 * !lm, (1 << 31) | (1 << 24));
}
internal int Lm_B2(int a, int lm)
{
	return LIM(a, 0x7fff, -0x8000 * !lm, (1 << 31) | (1 << 23));
}
internal int Lm_B3(int a, int lm)
{
	return LIM(a, 0x7fff, -0x8000 * !lm, (1 << 22));
}

internal int Lm_B3_sf(s64 value, int sf, int lm)
{
	int value_sf = (int)(gte_shift(value, sf));
	int value_12 = (int)(gte_shift(value, 1));
	int max = 0x7fff;
	int min = 0;
	if (lm == 0)
	{
		min = -0x8000;
	}

	if (value_12 < -0x8000 || value_12 > 0x7fff)
	{
		C2_FLAG |= (1 << 22);
	}

	if (value_sf > max)
	{
		return max;
	}
	else if (value_sf < min)
	{
		return min;
	}

	return value_sf;
}

internal int Lm_C1(int a)
{
	return LIM(a, 0x00ff, 0x0000, (1 << 21));
}
internal int Lm_C2(int a)
{
	return LIM(a, 0x00ff, 0x0000, (1 << 20));
}
internal int Lm_C3(int a)
{
	return LIM(a, 0x00ff, 0x0000, (1 << 19));
}
internal int Lm_D(s64 a, int sf)
{
	return LIM((int)(gte_shift(a, sf)), 0xffff, 0x0000, (1 << 31) | (1 << 18));
}

internal u32 Lm_E(u32 result)
{
	if (result == 0xffffffff)
	{
		C2_FLAG |= (1 << 31) | (1 << 17);
		return 0x1ffff;
	}

	if (result > 0x1ffff)
	{
		return 0x1ffff;
	}

	return result;
}

internal s64 F(s64 a)
{
	m_mac0 = a;

	if (a > 0x7fffffffLL)
	{
		C2_FLAG |= (1 << 31) | (1 << 16);
	}

	if (a < -0x80000000LL)
	{
		C2_FLAG |= (1 << 31) | (1 << 15);
	}

	return a;
}

internal int Lm_G1(s64 a)
{
	if (a > 0x3ff)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		return 0x3ff;
	}
	if (a < -0x400)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		return -0x400;
	}

	return (int)(a);
}

internal int Lm_G2(s64 a)
{
	if (a > 0x3ff)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		return 0x3ff;
	}

	if (a < -0x400)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		return -0x400;
	}

	return (int)(a);
}

internal int Lm_H(s64 value, int sf)
{
	s64 value_sf = gte_shift(value, sf);
	int value_12 = (int)(gte_shift(value, 1));
	int max = 0x1000;
	int min = 0x0000;

	if (value_sf < min || value_sf > max)
	{
		C2_FLAG |= (1 << 12);
	}

	if (value_12 > max)
	{
		return max;
	}

	if (value_12 < min)
	{
		return min;
	}

	return value_12;
}

#if defined(CTR_NATIVE)
static inline int NativeGTE_PureBounds(
	s64 value,
	u32 maxFlag,
	u32 minFlag,
	u32 *flags)
{
	if (value > (s64)0x7ffffffffff)
		*flags |= maxFlag;
	if (value < (s64)-0x8000000000)
		*flags |= minFlag;
	return (int)(value >> 12);
}

static inline int NativeGTE_PureLim(
	int value,
	int maxValue,
	int minValue,
	u32 flag,
	u32 *flags)
{
	if (value > maxValue)
	{
		*flags |= flag;
		return maxValue;
	}
	if (value < minValue)
	{
		*flags |= flag;
		return minValue;
	}
	return value;
}

static inline int NativeGTE_PureLmB3Sf(
	s64 value,
	u32 *flags)
{
	const int shifted = (int)(value >> 12);
	if ((shifted < -0x8000) || (shifted > 0x7fff))
		*flags |= (1u << 22);
	if (shifted > 0x7fff)
		return 0x7fff;
	if (shifted < -0x8000)
		return -0x8000;
	return shifted;
}

static inline u32 NativeGTE_PureLmE(
	u32 value,
	u32 *flags)
{
	if (value == 0xffffffffu)
	{
		*flags |= (1u << 31) | (1u << 17);
		return 0x1ffffu;
	}
	return value > 0x1ffffu ? 0x1ffffu : value;
}

static inline s64 NativeGTE_PureF(
	s64 value,
	u32 *flags)
{
	if (value > 0x7fffffffLL)
		*flags |= (1u << 31) | (1u << 16);
	if (value < -0x80000000LL)
		*flags |= (1u << 31) | (1u << 15);
	return value;
}

static inline int NativeGTE_PureLmG1(
	s64 value,
	u32 *flags)
{
	if (value > 0x3ff)
	{
		*flags |= (1u << 31) | (1u << 14);
		return 0x3ff;
	}
	if (value < -0x400)
	{
		*flags |= (1u << 31) | (1u << 14);
		return -0x400;
	}
	return (int)value;
}

static inline int NativeGTE_PureLmG2(
	s64 value,
	u32 *flags)
{
	if (value > 0x3ff)
	{
		*flags |= (1u << 31) | (1u << 13);
		return 0x3ff;
	}
	if (value < -0x400)
	{
		*flags |= (1u << 31) | (1u << 13);
		return -0x400;
	}
	return (int)value;
}

static s64 NativeGTE_PresentationRatioQ16Pure(
	s16 h,
	s64 viewZQ16)
{
	const s64 maxRatio = 0x1ffff;
	const s64 numerator = (s64)h << 32;
	s64 q;
	s64 product;

	if ((viewZQ16 <= 0) || (numerator <= 0))
		return 0;

	if (viewZQ16 > (s64)0x3fffffffffffLL)
	{
		q = numerator / viewZQ16;
		return (q > maxRatio) ? maxRatio : q;
	}

	q = (s64)((double)numerator / (double)viewZQ16);
	if (q < 0)
		q = 0;
	if (q > maxRatio)
		q = maxRatio;

	product = q * viewZQ16;
	if (product > numerator)
	{
		if (q > 0)
		{
			q--;
			product -= viewZQ16;
		}
	}
	else if ((q < maxRatio) &&
	         ((numerator - product) >= viewZQ16))
	{
		q++;
		product += viewZQ16;
	}

	if ((product > numerator) ||
	    ((q < maxRatio) &&
	     ((numerator - product) >= viewZQ16)))
	{
		q = numerator / viewZQ16;
		if (q > maxRatio)
			q = maxRatio;
	}

	return q;
}

void NativeGTE_CaptureProjectionContext(
	NativeGTEProjectionContext *context)
{
	if (context == NULL)
		return;

	memset(context, 0, sizeof(*context));

	context->r[0][0] = C2_R11;
	context->r[0][1] = C2_R12;
	context->r[0][2] = C2_R13;
	context->r[1][0] = C2_R21;
	context->r[1][1] = C2_R22;
	context->r[1][2] = C2_R23;
	context->r[2][0] = C2_R31;
	context->r[2][1] = C2_R32;
	context->r[2][2] = C2_R33;
	context->tr[0] = C2_TRX;
	context->tr[1] = C2_TRY;
	context->tr[2] = C2_TRZ;
	context->ofx = C2_OFX;
	context->ofy = C2_OFY;
	context->h = C2_H;
	context->wideMode = NativeGTE_GetWideModeCached();

	if (NativeGTE_PresentationMatrixMatchesGte() &&
	    (s_nativeGTEActivePresentationMatrix != NULL))
	{
		NativeGTEPresentationMatrix *pm =
			s_nativeGTEActivePresentationMatrix;
		context->presentationValid = 1;
		for (int row = 0; row < 3; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				context->presentationM_Q16[row][column] =
					pm->mQ16[row][column];
			}
			context->presentationT_Q16[row] =
				pm->tQ16[row];
		}
	}
}

void NativeGTE_ProjectVertexPure(
	const NativeGTEProjectionContext *context,
	s16 vx,
	s16 vy,
	s16 vz,
	NativeGTEProjectionResult *result)
{
	u32 flags = 0;
	s64 mac1Raw;
	s64 mac2Raw;
	s64 mac3Raw;
	int mac1;
	int mac2;
	int ir1;
	int ir2;
	int depth;
	u32 hOverSz;
	s64 projectedX;
	s64 projectedY;
	s64 presentationProjectedX;
	s64 presentationProjectedY;
	s64 screenX16;
	s64 screenY16;
	s64 presentationX16;
	s64 presentationY16;
	int sx;
	int sy;

	if ((context == NULL) || (result == NULL))
		return;

	mac1Raw =
		((s64)context->tr[0] << 12) +
		((s64)context->r[0][0] * vx) +
		((s64)context->r[0][1] * vy) +
		((s64)context->r[0][2] * vz);
	mac2Raw =
		((s64)context->tr[1] << 12) +
		((s64)context->r[1][0] * vx) +
		((s64)context->r[1][1] * vy) +
		((s64)context->r[1][2] * vz);
	mac3Raw =
		((s64)context->tr[2] << 12) +
		((s64)context->r[2][0] * vx) +
		((s64)context->r[2][1] * vy) +
		((s64)context->r[2][2] * vz);

	mac1 = NativeGTE_PureBounds(
		mac1Raw,
		(1u << 31) | (1u << 30),
		(1u << 31) | (1u << 27),
		&flags);
	mac2 = NativeGTE_PureBounds(
		mac2Raw,
		(1u << 31) | (1u << 29),
		(1u << 31) | (1u << 26),
		&flags);
	(void)NativeGTE_PureBounds(
		mac3Raw,
		(1u << 31) | (1u << 28),
		(1u << 31) | (1u << 25),
		&flags);

	ir1 = NativeGTE_PureLim(
		mac1,
		0x7fff,
		-0x8000,
		(1u << 31) | (1u << 24),
		&flags);
	ir2 = NativeGTE_PureLim(
		mac2,
		0x7fff,
		-0x8000,
		(1u << 31) | (1u << 23),
		&flags);
	(void)NativeGTE_PureLmB3Sf(mac3Raw, &flags);

	depth = NativeGTE_PureLim(
		(int)(mac3Raw >> 12),
		0xffff,
		0,
		(1u << 31) | (1u << 18),
		&flags);
	hOverSz = NativeGTE_PureLmE(
		gte_divide((u16)context->h, (u16)depth),
		&flags);

	projectedX = (s64)ir1 * (s64)hOverSz;
	projectedY = (s64)ir2 * (s64)hOverSz;
	presentationProjectedX = projectedX;
	presentationProjectedY = projectedY;

	if ((mac1Raw >> 12) == (s64)ir1)
	{
		presentationProjectedX =
			(mac1Raw * (s64)hOverSz) >> 12;
	}
	if ((mac2Raw >> 12) == (s64)ir2)
	{
		presentationProjectedY =
			(mac2Raw * (s64)hOverSz) >> 12;
	}

	if (context->presentationValid)
	{
		const s64 viewXQ16 =
			context->presentationT_Q16[0] +
			((context->presentationM_Q16[0][0] * vx +
			  context->presentationM_Q16[0][1] * vy +
			  context->presentationM_Q16[0][2] * vz) >>
			 12);
		const s64 viewYQ16 =
			context->presentationT_Q16[1] +
			((context->presentationM_Q16[1][0] * vx +
			  context->presentationM_Q16[1][1] * vy +
			  context->presentationM_Q16[1][2] * vz) >>
			 12);
		const s64 viewZQ16 =
			context->presentationT_Q16[2] +
			((context->presentationM_Q16[2][0] * vx +
			  context->presentationM_Q16[2][1] * vy +
			  context->presentationM_Q16[2][2] * vz) >>
			 12);

		if (viewZQ16 > 0)
		{
			const s64 ratioQ16 =
				NativeGTE_PresentationRatioQ16Pure(
					context->h,
					viewZQ16);
			presentationProjectedX =
				(viewXQ16 * ratioQ16) >> 16;
			presentationProjectedY =
				(viewYQ16 * ratioQ16) >> 16;
		}
	}

	if (context->wideMode)
	{
		int aspectW=4,aspectH=3;
		Platform_GetAspectRatioSize(context->wideMode,&aspectW,&aspectH);
		const s64 numerator=(s64)4*aspectH;
		const s64 divisor=(s64)3*aspectW;
		projectedX=(projectedX*numerator)/divisor;
		presentationProjectedX=(presentationProjectedX*numerator)/divisor;
	}

	screenX16 = NativeGTE_PureF(
		(s64)context->ofx + projectedX,
		&flags);
	sx = NativeGTE_PureLmG1(screenX16 >> 16, &flags);
	presentationX16 =
		(s64)context->ofx + presentationProjectedX;
	if ((screenX16 >> 16) != sx)
		presentationX16 = (s64)sx << 16;

	screenY16 = NativeGTE_PureF(
		(s64)context->ofy + projectedY,
		&flags);
	sy = NativeGTE_PureLmG2(screenY16 >> 16, &flags);
	presentationY16 =
		(s64)context->ofy + presentationProjectedY;
	if ((screenY16 >> 16) != sy)
		presentationY16 = (s64)sy << 16;

	result->packedSxy =
		(u32)(u16)(s16)sx |
		((u32)(u16)(s16)sy << 16);
	result->x16 = (s32)presentationX16;
	result->y16 = (s32)presentationY16;
	result->depth = (u16)depth;
	result->flags = flags;
}

static int s_nativeGTEPureValidateChecked;
static int s_nativeGTEPureValidateEnabled;
static u64 s_nativeGTEPureValidateVertices;
static u64 s_nativeGTEPureValidateMismatches;
static u64 s_nativeGTEPureValidatePackedMismatch;
static u64 s_nativeGTEPureValidateDepthMismatch;
static u64 s_nativeGTEPureValidateSubpixelMismatch;
static u64 s_nativeGTEPureValidateOverflowMismatch;

static int NativeGTE_PureValidateEnabled(void)
{
	if (!s_nativeGTEPureValidateChecked)
	{
		FILE *marker;
		s_nativeGTEPureValidateChecked = 1;
		marker = fopen("debug/gte_pure_validate.enable", "rb");
		s_nativeGTEPureValidateEnabled = marker != NULL;
		if (marker != NULL)
			fclose(marker);
	}
	return s_nativeGTEPureValidateEnabled;
}

static void NativeGTE_ValidatePureVertex(
	s16 vx,
	s16 vy,
	s16 vz,
	const NativeGTEProjectionResult *pure)
{
	const u32 actualPacked = C2_SXY2;
	const u16 actualDepth = (u16)C2_SZ3;
	const s32 actualX16 = s_nativeGTESxyX16[2];
	const s32 actualY16 = s_nativeGTESxyY16[2];
	int mismatch = 0;

	s_nativeGTEPureValidateVertices++;

	if (pure->packedSxy != actualPacked)
	{
		s_nativeGTEPureValidatePackedMismatch++;
		mismatch = 1;
	}
	if (pure->depth != actualDepth)
	{
		s_nativeGTEPureValidateDepthMismatch++;
		mismatch = 1;
	}
	if ((pure->x16 != actualX16) ||
	    (pure->y16 != actualY16))
	{
		s_nativeGTEPureValidateSubpixelMismatch++;
		mismatch = 1;
	}

	if (mismatch)
	{
		s_nativeGTEPureValidateMismatches++;
		if (s_nativeGTEPureValidateMismatches <= 12)
		{
			Platform_Log(
				"[GTE PURE] mismatch v=(%d,%d,%d) "
				"sxy pure=%08x actual=%08x "
				"depth pure=%u actual=%u "
				"sub pure=(%d,%d) actual=(%d,%d)\n",
				(int)vx,
				(int)vy,
				(int)vz,
				pure->packedSxy,
				actualPacked,
				(unsigned)pure->depth,
				(unsigned)actualDepth,
				pure->x16,
				pure->y16,
				actualX16,
				actualY16);
		}
	}

	if ((s_nativeGTEPureValidateVertices % 1000000ULL) == 0)
	{
		Platform_Log(
			"[GTE PURE] vertices=%llu mismatches=%llu "
			"packed=%llu depth=%llu subpixel=%llu overflow=%llu\n",
			(unsigned long long)s_nativeGTEPureValidateVertices,
			(unsigned long long)s_nativeGTEPureValidateMismatches,
			(unsigned long long)s_nativeGTEPureValidatePackedMismatch,
			(unsigned long long)s_nativeGTEPureValidateDepthMismatch,
			(unsigned long long)s_nativeGTEPureValidateSubpixelMismatch,
			(unsigned long long)s_nativeGTEPureValidateOverflowMismatch);
	}
}

static void NativeGTE_ValidatePureOverflow(
	u32 pureFlags,
	u32 actualFlags)
{
	const u32 mask = 1u << 17;
	if ((pureFlags & mask) != (actualFlags & mask))
	{
		s_nativeGTEPureValidateOverflowMismatch++;
		if (s_nativeGTEPureValidateOverflowMismatch <= 12)
		{
			Platform_Log(
				"[GTE PURE] overflow mismatch pure=%08x actual=%08x\n",
				pureFlags,
				actualFlags);
		}
	}
}
#endif

internal int GTE_RotTransPers(int idx, int lm)
{
	int h_over_sz3;

	m_mac1_raw = /*int44*/ (s64)((s64)C2_TRX << 12) + (C2_R11 * VX(idx)) + (C2_R12 * VY(idx)) + (C2_R13 * VZ(idx));
	m_mac2_raw = /*int44*/ (s64)((s64)C2_TRY << 12) + (C2_R21 * VX(idx)) + (C2_R22 * VY(idx)) + (C2_R23 * VZ(idx));
	C2_MAC1 = A1(m_mac1_raw);
	C2_MAC2 = A2(m_mac2_raw);
	C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_TRZ << 12) + (C2_R31 * VX(idx)) + (C2_R32 * VY(idx)) + (C2_R33 * VZ(idx)));
	C2_IR1 = Lm_B1(C2_MAC1, lm);
	C2_IR2 = Lm_B2(C2_MAC2, lm);
	C2_IR3 = Lm_B3_sf(m_mac3, m_sf, lm);
	C2_SZ0 = C2_SZ1;
	C2_SZ1 = C2_SZ2;
	C2_SZ2 = C2_SZ3;
	C2_SZ3 = Lm_D(m_mac3, 1);
	#if defined(CTR_NATIVE)
	h_over_sz3 = Lm_E(NativeGTE_DivideCached(C2_H, C2_SZ3));
#else
	h_over_sz3 = Lm_E(gte_divide(C2_H, C2_SZ3));
#endif
	C2_SXY0 = C2_SXY1;
	C2_SXY1 = C2_SXY2;
#if defined(CTR_NATIVE)
	s_nativeGTESxyX16[0] = s_nativeGTESxyX16[1];
	s_nativeGTESxyY16[0] = s_nativeGTESxyY16[1];
	s_nativeGTESxyDepth[0] = s_nativeGTESxyDepth[1];
	s_nativeGTESxyDepthValid[0] = s_nativeGTESxyDepthValid[1];
	s_nativeGTESxyValueSaved[0] = s_nativeGTESxyValueSaved[1];
	s_nativeGTESxyX16[1] = s_nativeGTESxyX16[2];
	s_nativeGTESxyY16[1] = s_nativeGTESxyY16[2];
	s_nativeGTESxyDepth[1] = s_nativeGTESxyDepth[2];
	s_nativeGTESxyDepthValid[1] = s_nativeGTESxyDepthValid[2];
	s_nativeGTESxyValueSaved[1] = s_nativeGTESxyValueSaved[2];
	s_nativeGTESxyDepth[2] = (u16)C2_SZ3;
	s_nativeGTESxyDepthValid[2] = C2_SZ3 > 0;
	s_nativeGTESxyValueSaved[2] = 0;
#endif
	// Correct the perspective term before screen clipping, around OFX (the
	// current viewport center). 4:3 -> 16:9 needs 3/4 horizontal scale:
	// the display's 4/3 expansion cancels it while revealing more scene.
	// Keep logical IR/SZ/flags fully PS1-quantized. Native level presentation
	// may carry a higher-precision interpolated view matrix into SXY only.
	s64 projectedX = (s64)C2_IR1 * h_over_sz3;
	s64 projectedY = (s64)C2_IR2 * h_over_sz3;
#if defined(CTR_NATIVE)
	/* Preserve the 12 fractional transform bits that retail discards when MAC1/2
	   are shifted into IR1/2. Keep the original quantized GTE divide/depth so
	   shared edges and clipping remain PS1-coherent. Only use this when IR was
	   not clamped; otherwise presentation falls back to the exact retail value. */
	s64 presentationProjectedX = projectedX;
	s64 presentationProjectedY = projectedY;
	if ((m_sf > 0) && ((m_mac1_raw >> 12) == (s64)C2_IR1))
		presentationProjectedX = (m_mac1_raw * (s64)h_over_sz3) >> 12;
	if ((m_sf > 0) && ((m_mac2_raw >> 12) == (s64)C2_IR2))
		presentationProjectedY = (m_mac2_raw * (s64)h_over_sz3) >> 12;

	if (NativeGTE_PresentationMatrixMatchesGte())
	{
		NativeGTEPresentationMatrix *pm = s_nativeGTEActivePresentationMatrix;
		s64 vx = VX(idx), vy = VY(idx), vz = VZ(idx);
		s64 viewXQ16 = pm->tQ16[0] +
			((pm->mQ16[0][0] * vx + pm->mQ16[0][1] * vy + pm->mQ16[0][2] * vz) >> 12);
		s64 viewYQ16 = pm->tQ16[1] +
			((pm->mQ16[1][0] * vx + pm->mQ16[1][1] * vy + pm->mQ16[1][2] * vz) >> 12);
		s64 viewZQ16 = pm->tQ16[2] +
			((pm->mQ16[2][0] * vx + pm->mQ16[2][1] * vy + pm->mQ16[2][2] * vz) >> 12);

		if (viewZQ16 > 0)
		{
			s64 ratioQ16 = NativeGTE_PresentationRatioQ16Fast(viewZQ16);
			presentationProjectedX = (viewXQ16 * ratioQ16) >> 16;
			presentationProjectedY = (viewYQ16 * ratioQ16) >> 16;
		}
	}

	int aspectMode=NativeGTE_GetWideModeCached();
	if(aspectMode!=PLATFORM_ASPECT_4_3)
	{
		int aspectW=4,aspectH=3;
		Platform_GetAspectRatioSize(aspectMode,&aspectW,&aspectH);
		const s64 numerator=(s64)4*aspectH;
		const s64 divisor=(s64)3*aspectW;
		projectedX=(projectedX*numerator)/divisor;
		presentationProjectedX=(presentationProjectedX*numerator)/divisor;
	}
#endif
	{
		s64 screenX16 = F((s64)C2_OFX + projectedX);
		C2_SX2 = Lm_G1(screenX16 >> 16);
#if defined(CTR_NATIVE)
		s64 presentationX16 = (s64)C2_OFX + presentationProjectedX;
		if ((screenX16 >> 16) != C2_SX2)
		{
			screenX16 = (s64)C2_SX2 << 16;
			presentationX16 = screenX16;
		}
		s_nativeGTESxyX16[2] = (s32)presentationX16;
#endif
	}
	{
		s64 screenY16 = F((s64)C2_OFY + projectedY);
		C2_SY2 = Lm_G2(screenY16 >> 16);
#if defined(CTR_NATIVE)
		s64 presentationY16 = (s64)C2_OFY + presentationProjectedY;
		if ((screenY16 >> 16) != C2_SY2)
		{
			screenY16 = (s64)C2_SY2 << 16;
			presentationY16 = screenY16;
		}
		s_nativeGTESxyY16[2] = (s32)presentationY16;
#endif
	}

#if defined(CTR_NATIVE)
	// Use the fractional result produced by the same quantized GTE divide that
	// generated the retail integer SXY. This keeps shared edges topologically
	// identical to PS1 while retaining the low 16 bits for smooth presentation.
	// A separate floating-point px/pz projection looked "more precise" but was
	// not the same projection and caused shimmer and cracks.
	NativeGTE_SaveSXYValue(C2_SXY2, s_nativeGTESxyX16[2], s_nativeGTESxyY16[2],
	                       s_nativeGTESxyDepth[2], s_nativeGTESxyDepthValid[2]);
	s_nativeGTESxyValueSaved[2] = 1;
#endif
	return h_over_sz3;
}

int GTE_operator(int op)
{
	int v;
	int lm;
	int cv;
	int mx;
	int h_over_sz3 = 0;

	lm = GTE_LM(gteop(op));
	m_sf = GTE_SF(gteop(op));

	C2_FLAG = 0;

	switch (GTE_FUNCT(gteop(op)))
	{
	case 0x00:
	case 0x01:
		h_over_sz3 = GTE_RotTransPers(0, lm);

		C2_MAC0 = (int)(F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
		C2_IR0 = Lm_H(m_mac0, 1);

		return 1;

	case 0x06:
		C2_MAC0 = (int)(F((s64)(C2_SX0 * C2_SY1) + (C2_SX1 * C2_SY2) + (C2_SX2 * C2_SY0) - (C2_SX0 * C2_SY2) - (C2_SX1 * C2_SY0) - (C2_SX2 * C2_SY1)));
		C2_FLAG = 0;
		return 1;

	case 0x0c:

		C2_MAC1 = A1((s64)(C2_R22 * C2_IR3) - (C2_R33 * C2_IR2));
		C2_MAC2 = A2((s64)(C2_R33 * C2_IR1) - (C2_R11 * C2_IR3));
		C2_MAC3 = A3((s64)(C2_R11 * C2_IR2) - (C2_R22 * C2_IR1));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		return 1;

	case 0x10:

		C2_MAC1 = A1((C2_R << 16) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - (C2_R << 16)), 0)));
		C2_MAC2 = A2((C2_G << 16) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - (C2_G << 16)), 0)));
		C2_MAC3 = A3((C2_B << 16) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - (C2_B << 16)), 0)));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x11:

		C2_MAC1 = A1((C2_IR1 << 12) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - (C2_IR1 << 12)), 0)));
		C2_MAC2 = A2((C2_IR2 << 12) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - (C2_IR2 << 12)), 0)));
		C2_MAC3 = A3((C2_IR3 << 12) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - (C2_IR3 << 12)), 0)));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x12:

		mx = GTE_MX(gteop(op));
		v = GTE_V(gteop(op));
		cv = GTE_CV(gteop(op));

		switch (cv)
		{
		case 2:
			C2_MAC1 = A1((s64)(MX12(mx) * VY(v)) + (MX13(mx) * VZ(v)));
			C2_MAC2 = A2((s64)(MX22(mx) * VY(v)) + (MX23(mx) * VZ(v)));
			C2_MAC3 = A3((s64)(MX32(mx) * VY(v)) + (MX33(mx) * VZ(v)));
			Lm_B1(A1(((s64)CV1(cv) << 12) + (MX11(mx) * VX(v))), 0);
			Lm_B2(A2(((s64)CV2(cv) << 12) + (MX21(mx) * VX(v))), 0);
			Lm_B3(A3(((s64)CV3(cv) << 12) + (MX31(mx) * VX(v))), 0);
			break;

		default:
			C2_MAC1 = A1(/*int44*/ (s64)((s64)CV1(cv) << 12) + (MX11(mx) * VX(v)) + (MX12(mx) * VY(v)) + (MX13(mx) * VZ(v)));
			C2_MAC2 = A2(/*int44*/ (s64)((s64)CV2(cv) << 12) + (MX21(mx) * VX(v)) + (MX22(mx) * VY(v)) + (MX23(mx) * VZ(v)));
			C2_MAC3 = A3(/*int44*/ (s64)((s64)CV3(cv) << 12) + (MX31(mx) * VX(v)) + (MX32(mx) * VY(v)) + (MX33(mx) * VZ(v)));
			break;
		}

		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		return 1;

	case 0x13:

		C2_MAC1 = A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x14:

		C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x16:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = A1((s64)(C2_L11 * VX(v)) + (C2_L12 * VY(v)) + (C2_L13 * VZ(v)));
			C2_MAC2 = A2((s64)(C2_L21 * VX(v)) + (C2_L22 * VY(v)) + (C2_L23 * VZ(v)));
			C2_MAC3 = A3((s64)(C2_L31 * VX(v)) + (C2_L32 * VY(v)) + (C2_L33 * VZ(v)));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_MAC1 = A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
			C2_MAC2 = A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
			C2_MAC3 = A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = Lm_C1(C2_MAC1 >> 4);
			C2_G2 = Lm_C2(C2_MAC2 >> 4);
			C2_B2 = Lm_C3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x1b:

		C2_MAC1 = A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1((C2_R << 4) * C2_IR1);
		C2_MAC2 = A2((C2_G << 4) * C2_IR2);
		C2_MAC3 = A3((C2_B << 4) * C2_IR3);
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x1c:

		C2_MAC1 = A1(/*int44*/ (s64)(((s64)C2_RBK) << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = A2(/*int44*/ (s64)(((s64)C2_GBK) << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = A3(/*int44*/ (s64)(((s64)C2_BBK) << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1((C2_R << 4) * C2_IR1);
		C2_MAC2 = A2((C2_G << 4) * C2_IR2);
		C2_MAC3 = A3((C2_B << 4) * C2_IR3);
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x1e:

		C2_MAC1 = A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x20:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = A1((s64)(C2_L11 * VX(v)) + (C2_L12 * VY(v)) + (C2_L13 * VZ(v)));
			C2_MAC2 = A2((s64)(C2_L21 * VX(v)) + (C2_L22 * VY(v)) + (C2_L23 * VZ(v)));
			C2_MAC3 = A3((s64)(C2_L31 * VX(v)) + (C2_L32 * VY(v)) + (C2_L33 * VZ(v)));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = Lm_C1(C2_MAC1 >> 4);
			C2_G2 = Lm_C2(C2_MAC2 >> 4);
			C2_B2 = Lm_C3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x28:

		C2_MAC1 = A1(C2_IR1 * C2_IR1);
		C2_MAC2 = A2(C2_IR2 * C2_IR2);
		C2_MAC3 = A3(C2_IR3 * C2_IR3);
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		return 1;

	case 0x29:

		C2_MAC1 = A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x2a:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = A1((C2_R0 << 16) + (C2_IR0 * Lm_B1(A1(((s64)C2_RFC << 12) - (C2_R0 << 16)), 0)));
			C2_MAC2 = A2((C2_G0 << 16) + (C2_IR0 * Lm_B2(A2(((s64)C2_GFC << 12) - (C2_G0 << 16)), 0)));
			C2_MAC3 = A3((C2_B0 << 16) + (C2_IR0 * Lm_B3(A3(((s64)C2_BFC << 12) - (C2_B0 << 16)), 0)));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = Lm_C1(C2_MAC1 >> 4);
			C2_G2 = Lm_C2(C2_MAC2 >> 4);
			C2_B2 = Lm_C3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x2d:

		C2_MAC0 = (int)(F((s64)(C2_ZSF3 * C2_SZ1) + (C2_ZSF3 * C2_SZ2) + (C2_ZSF3 * C2_SZ3)));
		C2_OTZ = Lm_D(m_mac0, 1);
		return 1;

	case 0x2e:

		C2_MAC0 = (int)(F((s64)(C2_ZSF4 * C2_SZ0) + (C2_ZSF4 * C2_SZ1) + (C2_ZSF4 * C2_SZ2) + (C2_ZSF4 * C2_SZ3)));
		C2_OTZ = Lm_D(m_mac0, 1);
		return 1;

	case 0x30:

		for (v = 0; v < 3; v++)
		{
			h_over_sz3 = GTE_RotTransPers(v, lm);
		}

		C2_MAC0 = (int)(F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
		C2_IR0 = Lm_H(m_mac0, 1);
		return 1;

	case 0x3d:

		C2_MAC1 = A1(C2_IR0 * C2_IR1);
		C2_MAC2 = A2(C2_IR0 * C2_IR2);
		C2_MAC3 = A3(C2_IR0 * C2_IR3);
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x3e:

		C2_MAC1 = A1(gte_shift(C2_MAC1, -m_sf) + (C2_IR0 * C2_IR1));
		C2_MAC2 = A2(gte_shift(C2_MAC2, -m_sf) + (C2_IR0 * C2_IR2));
		C2_MAC3 = A3(gte_shift(C2_MAC3, -m_sf) + (C2_IR0 * C2_IR3));
		C2_IR1 = Lm_B1(C2_MAC1, lm);
		C2_IR2 = Lm_B2(C2_MAC2, lm);
		C2_IR3 = Lm_B3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = Lm_C1(C2_MAC1 >> 4);
		C2_G2 = Lm_C2(C2_MAC2 >> 4);
		C2_B2 = Lm_C3(C2_MAC3 >> 4);
		return 1;

	case 0x3f:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = A1((s64)(C2_L11 * VX(v)) + (C2_L12 * VY(v)) + (C2_L13 * VZ(v)));
			C2_MAC2 = A2((s64)(C2_L21 * VX(v)) + (C2_L22 * VY(v)) + (C2_L23 * VZ(v)));
			C2_MAC3 = A3((s64)(C2_L31 * VX(v)) + (C2_L32 * VY(v)) + (C2_L33 * VZ(v)));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_MAC1 = A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_MAC1 = A1((C2_R << 4) * C2_IR1);
			C2_MAC2 = A2((C2_G << 4) * C2_IR2);
			C2_MAC3 = A3((C2_B << 4) * C2_IR3);
			C2_IR1 = Lm_B1(C2_MAC1, lm);
			C2_IR2 = Lm_B2(C2_MAC2, lm);
			C2_IR3 = Lm_B3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = Lm_C1(C2_MAC1 >> 4);
			C2_G2 = Lm_C2(C2_MAC2 >> 4);
			C2_B2 = Lm_C3(C2_MAC3 >> 4);
		}
		return 1;
	}

	return 0;
}

#if defined(CTR_NATIVE)
/* Native RTPS/RTPT entry points used by the PSX inline macros. These are the
   two hottest GTE operations in the renderer and their opcode is already known
   at compile time, so bypass the generic GTE_operator decode/switch while
   preserving the exact same sf/lm, FLAG, projection and depth-cue updates. */
int NativeGTE_RTPS(void)
{
	int h_over_sz3;

	m_sf = 1;
	C2_FLAG = 0;
	h_over_sz3 = GTE_RotTransPers(0, 0);
	C2_MAC0 = (int)(F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
	C2_IR0 = Lm_H(m_mac0, 1);
	return 1;
}

int NativeGTE_RTPT(void)
{
	int h_over_sz3 = 0;

	m_sf = 1;
	C2_FLAG = 0;
	/* Fixed 3-vertex command: allow the x86 optimizer to specialize each
	 * vertex load and remove loop/index tests. GTE FIFO and flags still update
	 * in the original 0 -> 1 -> 2 order. */
	GTE_RotTransPers(0, 0);
	GTE_RotTransPers(1, 0);
	h_over_sz3 = GTE_RotTransPers(2, 0);
	C2_MAC0 = (int)(F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
	C2_IR0 = Lm_H(m_mac0, 1);
	return 1;
}

/* Packed triple path used by the opt-in native level polygon test. No GTE
 * state is skipped: writing registers 12..14 must invalidate the stored
 * subpixel metadata exactly as MTC2 does, NCLIP must set MAC0/FLAG, and the
 * returned MAC0 matches MFC2(24). Avoid three indirect MTC2 dispatch calls
 * while retaining signed 16-bit packed-register semantics. */
int NativeGTE_NCLIPLoadedTriple(u32 sxy0, u32 sxy1, u32 sxy2)
{
    NativeGTE_WriteSXYRegister(sxy0, 12);
    NativeGTE_WriteSXYRegister(sxy1, 13);
    NativeGTE_WriteSXYRegister(sxy2, 14);
    gteRegs.CP2D.p[12].d=sxy0;
    gteRegs.CP2D.p[13].d=sxy1;
    gteRegs.CP2D.p[14].d=sxy2;
    NativeGTE_NCLIP();
    return (s32)C2_MAC0;
}

/* NCLIP is another extremely hot fixed-opcode path in level/model rendering.
   Keep the exact retail arithmetic/FLAG behavior but bypass GTE_operator's
   opcode decode, switch and sf/lm setup. */
int NativeGTE_NCLIP(void)
{
	C2_FLAG = 0;
	C2_MAC0 = (int)(F(
		(s64)(C2_SX0 * C2_SY1) +
		(s64)(C2_SX1 * C2_SY2) +
		(s64)(C2_SX2 * C2_SY0) -
		(s64)(C2_SX0 * C2_SY2) -
		(s64)(C2_SX1 * C2_SY0) -
		(s64)(C2_SX2 * C2_SY1)));
	/* GTE_operator case 0x06 clears FLAG after F(), so preserve that exact
	   final architectural state even if MAC0 overflowed. */
	C2_FLAG = 0;
	return 1;
}
#endif

#if defined(CTR_NATIVE)
/* Read-only camera snapshot for the experimental resident-PC world renderer.
 * The native static-world preview is drawn after retail OT processing, but
 * its matrices must be captured at the level's own GTE projection phase. */
void NativeGTE_GetLevelPCProjectionOrigin(s32 *x16,s32 *y16)
{
    if(x16)*x16=(s32)C2_OFX;
    if(y16)*y16=(s32)C2_OFY;
}
void NativeGTE_GetLevelPCView(s16 outM[3][3], s32 outT[3], int *screen, int *wide)
{
	if (outM != NULL)
	{
		outM[0][0] = (s16)C2_R11; outM[0][1] = (s16)C2_R12; outM[0][2] = (s16)C2_R13;
		outM[1][0] = (s16)C2_R21; outM[1][1] = (s16)C2_R22; outM[1][2] = (s16)C2_R23;
		outM[2][0] = (s16)C2_R31; outM[2][1] = (s16)C2_R32; outM[2][2] = (s16)C2_R33;
	}
	if (outT != NULL)
	{
		outT[0] = (s32)C2_TRX;
		outT[1] = (s32)C2_TRY;
		outT[2] = (s32)C2_TRZ;
	}
	if (screen != NULL) *screen = (int)C2_H;
	if (wide != NULL) *wide = NativeGTE_GetWideModeCached();
}
#endif
