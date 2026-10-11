#include <platform/native_jobs.h>

#if defined(CTR_NATIVE)

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define NATIVE_JOBS_MAX_WORKERS 12

struct NativeJobsWorker
{
	SDL_Thread *thread;
	int workerIndex;
};

struct NativeJobsState
{
	SDL_Mutex *mutex;
	SDL_Condition *startCond;
	SDL_Condition *doneCond;
	struct NativeJobsWorker workers[NATIVE_JOBS_MAX_WORKERS];
	int initialized;
	int shutdown;
	int logicalCoreCount;
	int workerCount;
	int generation;
	int pendingWorkers;
	int itemCount;
	int chunkSize;
	NativeJobsRangeFn fn;
	void *userData;
	SDL_AtomicInt nextIndex;
};

static struct NativeJobsState s_nativeJobs;

static void NativeJobs_RunAvailableWork(void)
{
	for (;;)
	{
		const int begin =
			SDL_AddAtomicInt(&s_nativeJobs.nextIndex, s_nativeJobs.chunkSize);
		int end;

		if (begin >= s_nativeJobs.itemCount)
			return;

		end = begin + s_nativeJobs.chunkSize;
		if (end > s_nativeJobs.itemCount)
			end = s_nativeJobs.itemCount;

		s_nativeJobs.fn(begin, end, s_nativeJobs.userData);
	}
}

static int SDLCALL NativeJobs_WorkerMain(void *opaque)
{
	struct NativeJobsWorker *worker = (struct NativeJobsWorker *)opaque;
	int seenGeneration = 0;

	(void)worker;
	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_NORMAL);

	for (;;)
	{
		SDL_LockMutex(s_nativeJobs.mutex);
		while (!s_nativeJobs.shutdown &&
		       (seenGeneration == s_nativeJobs.generation))
		{
			SDL_WaitCondition(s_nativeJobs.startCond, s_nativeJobs.mutex);
		}

		if (s_nativeJobs.shutdown)
		{
			SDL_UnlockMutex(s_nativeJobs.mutex);
			break;
		}

		seenGeneration = s_nativeJobs.generation;
		SDL_UnlockMutex(s_nativeJobs.mutex);

		NativeJobs_RunAvailableWork();

		SDL_LockMutex(s_nativeJobs.mutex);
		s_nativeJobs.pendingWorkers--;
		if (s_nativeJobs.pendingWorkers == 0)
			SDL_SignalCondition(s_nativeJobs.doneCond);
		SDL_UnlockMutex(s_nativeJobs.mutex);
	}

	return 0;
}

int NativeJobs_Init(void)
{
	int wantedWorkers;

	if (s_nativeJobs.initialized)
		return 1;

	memset(&s_nativeJobs, 0, sizeof(s_nativeJobs));
	s_nativeJobs.logicalCoreCount = SDL_GetNumLogicalCPUCores();
	if (s_nativeJobs.logicalCoreCount < 1)
		s_nativeJobs.logicalCoreCount = 1;

	/*
	 * Main thread participates in jobs. Reserve one additional logical core
	 * for audio/OS/background work:
	 *   2 cores -> 0 workers + main
	 *   4 cores -> 2 workers + main
	 *   6 cores -> 4 workers + main
	 *   8 cores -> 6 workers + main
	 */
	wantedWorkers = s_nativeJobs.logicalCoreCount - 2;
	if (wantedWorkers < 0)
		wantedWorkers = 0;
	if (wantedWorkers > NATIVE_JOBS_MAX_WORKERS)
		wantedWorkers = NATIVE_JOBS_MAX_WORKERS;

	s_nativeJobs.mutex = SDL_CreateMutex();
	s_nativeJobs.startCond = SDL_CreateCondition();
	s_nativeJobs.doneCond = SDL_CreateCondition();
	if ((s_nativeJobs.mutex == NULL) ||
	    (s_nativeJobs.startCond == NULL) ||
	    (s_nativeJobs.doneCond == NULL))
	{
		NativeJobs_Shutdown();
		fprintf(stderr, "[Jobs] worker pool disabled: %s\n", SDL_GetError());
		return 0;
	}

	for (int i = 0; i < wantedWorkers; i++)
	{
		char name[32];
		SDL_snprintf(name, sizeof(name), "CTR render worker %d", i);
		s_nativeJobs.workers[i].workerIndex = i;
		s_nativeJobs.workers[i].thread =
			SDL_CreateThread(
				NativeJobs_WorkerMain,
				name,
				&s_nativeJobs.workers[i]);
		if (s_nativeJobs.workers[i].thread == NULL)
			break;
		s_nativeJobs.workerCount++;
	}

	s_nativeJobs.initialized = 1;
	printf(
		"[Jobs] logical cores=%d, render workers=%d (+ main thread)\n",
		s_nativeJobs.logicalCoreCount,
		s_nativeJobs.workerCount);
	fflush(stdout);
	return 1;
}

void NativeJobs_Shutdown(void)
{
	if (s_nativeJobs.mutex != NULL)
	{
		SDL_LockMutex(s_nativeJobs.mutex);
		s_nativeJobs.shutdown = 1;
		if (s_nativeJobs.startCond != NULL)
			SDL_BroadcastCondition(s_nativeJobs.startCond);
		SDL_UnlockMutex(s_nativeJobs.mutex);
	}

	for (int i = 0; i < s_nativeJobs.workerCount; i++)
	{
		if (s_nativeJobs.workers[i].thread != NULL)
		{
			SDL_WaitThread(s_nativeJobs.workers[i].thread, NULL);
			s_nativeJobs.workers[i].thread = NULL;
		}
	}

	if (s_nativeJobs.doneCond != NULL)
		SDL_DestroyCondition(s_nativeJobs.doneCond);
	if (s_nativeJobs.startCond != NULL)
		SDL_DestroyCondition(s_nativeJobs.startCond);
	if (s_nativeJobs.mutex != NULL)
		SDL_DestroyMutex(s_nativeJobs.mutex);

	memset(&s_nativeJobs, 0, sizeof(s_nativeJobs));
}

int NativeJobs_GetLogicalCoreCount(void)
{
	return s_nativeJobs.initialized ? s_nativeJobs.logicalCoreCount : 1;
}

int NativeJobs_GetWorkerCount(void)
{
	return s_nativeJobs.initialized ? s_nativeJobs.workerCount : 0;
}

int NativeJobs_ParallelFor(
	int itemCount,
	int minItemsPerChunk,
	NativeJobsRangeFn fn,
	void *userData)
{
	int participants;
	int chunkSize;

	if ((itemCount <= 0) || (fn == NULL))
		return 0;

	if (!s_nativeJobs.initialized)
		NativeJobs_Init();

	if (minItemsPerChunk < 1)
		minItemsPerChunk = 1;

	participants = s_nativeJobs.workerCount + 1;
	if ((s_nativeJobs.workerCount <= 0) ||
	    (itemCount < minItemsPerChunk * 2))
	{
		fn(0, itemCount, userData);
		return 1;
	}

	/*
	 * Aim for roughly four chunks per participating thread. This is enough
	 * load balancing for uneven BSP/model work without turning the atomic
	 * allocator itself into overhead.
	 */
	chunkSize = (itemCount + participants * 4 - 1) / (participants * 4);
	if (chunkSize < minItemsPerChunk)
		chunkSize = minItemsPerChunk;

	SDL_LockMutex(s_nativeJobs.mutex);
	s_nativeJobs.fn = fn;
	s_nativeJobs.userData = userData;
	s_nativeJobs.itemCount = itemCount;
	s_nativeJobs.chunkSize = chunkSize;
	SDL_SetAtomicInt(&s_nativeJobs.nextIndex, 0);
	s_nativeJobs.pendingWorkers = s_nativeJobs.workerCount;
	s_nativeJobs.generation++;
	SDL_BroadcastCondition(s_nativeJobs.startCond);
	SDL_UnlockMutex(s_nativeJobs.mutex);

	/* Main thread does useful work instead of only waiting. */
	NativeJobs_RunAvailableWork();

	SDL_LockMutex(s_nativeJobs.mutex);
	while (s_nativeJobs.pendingWorkers > 0)
		SDL_WaitCondition(s_nativeJobs.doneCond, s_nativeJobs.mutex);
	SDL_UnlockMutex(s_nativeJobs.mutex);

	return 1;
}

#endif
