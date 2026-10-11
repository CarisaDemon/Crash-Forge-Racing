#pragma once

#if defined(CTR_NATIVE)

typedef void (*NativeJobsRangeFn)(int beginIndex, int endIndex, void *userData);

/*
 * Persistent native worker pool.
 *
 * The main thread always participates in ParallelFor. Worker count is chosen
 * from the host logical-core count while reserving one logical core for
 * audio/OS work. Small jobs automatically stay single-threaded.
 */
int NativeJobs_Init(void);
void NativeJobs_Shutdown(void);
int NativeJobs_GetLogicalCoreCount(void);
int NativeJobs_GetWorkerCount(void);
int NativeJobs_ParallelFor(
	int itemCount,
	int minItemsPerChunk,
	NativeJobsRangeFn fn,
	void *userData);

#endif
