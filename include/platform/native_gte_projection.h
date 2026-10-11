#pragma once

#if defined(CTR_NATIVE)

#include <stdint.h>

typedef struct NativeGTEProjectionContext
{
	int16_t r[3][3];
	int32_t tr[3];
	int32_t ofx;
	int32_t ofy;
	int16_t h;
	int wideMode;
	int presentationValid;
	int64_t presentationM_Q16[3][3];
	int64_t presentationT_Q16[3];
} NativeGTEProjectionContext;

typedef struct NativeGTEProjectionResult
{
	uint32_t packedSxy;
	int32_t x16;
	int32_t y16;
	uint16_t depth;
	uint32_t flags;
} NativeGTEProjectionResult;

/*
 * Capture the projection-only GTE state on the main thread. The resulting
 * context is immutable and may be shared by native worker threads.
 */
void NativeGTE_CaptureProjectionContext(
	NativeGTEProjectionContext *context);

/*
 * Thread-safe, side-effect-free RTPS-equivalent projection for one vertex.
 * It preserves the retail integer SXY/depth and the native subpixel
 * presentation coordinates, but does not mutate the emulated GTE registers.
 */
void NativeGTE_ProjectVertexPure(
	const NativeGTEProjectionContext *context,
	int16_t vx,
	int16_t vy,
	int16_t vz,
	NativeGTEProjectionResult *result);

#endif
