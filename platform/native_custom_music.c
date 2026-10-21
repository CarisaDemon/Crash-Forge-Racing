#include <platform/native_custom_music.h>
#include <NativeSession.h>
#include <platform/native_assets.h>
#include <platform/native_custom_tracks.h>
#include <platform/native_music_selection.h>

#ifdef internal
#undef internal
#endif
#include <vorbis/vorbisfile.h>
#define internal static

#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#define CUSTOM_MUSIC_OUTPUT_RATE 44100
#define CUSTOM_MUSIC_FP_SHIFT 16
#define CUSTOM_MUSIC_FP_ONE (1u << CUSTOM_MUSIC_FP_SHIFT)

/* Final Lap: 28/25 = 1.12x */
#define FINAL_LAP_SPEED_NUM 28
#define FINAL_LAP_SPEED_DEN 25

/* Lightweight pitch-preserving OLA settings. */
#define TS_WINDOW 1024
#define TS_HOP 256

struct NativeCustomMusicState
{
	s16 *pcm;
	int frameCount;

	s16 *finalPcm;
	int finalFrameCount;
	int finalLapActive;
	int finalLapRequested;

	int channels;
	int sampleRate;
	u64 positionFp;
	u64 timelineOutputFrames;
	u32 stepFp;
	int active;
	int paused;
	int maskMuted;
	int raceEndSeen;
	int hubFallback;
	int volume;
	int levelID;
	int ownsPcm;
	int ownsFinalPcm;
};

#define CUSTOM_MUSIC_CACHE_LEVELS 100

struct NativeCustomMusicCacheEntry
{
	s16 *pcm;
	int frameCount;
	int channels;
	int sampleRate;
	s16 *finalPcm;
	int finalFrameCount;
	SDL_AtomicInt loaded;
};

global_variable struct NativeCustomMusicCacheEntry s_customMusicCache[CUSTOM_MUSIC_CACHE_LEVELS];
global_variable SDL_AtomicInt s_customMusicCacheReady;
global_variable SDL_AtomicInt s_customMusicCacheLoading;

global_variable struct NativeCustomMusicState s_customMusic = {0};
/* Path identity prevents reuse of a different song on the same level. */
static char s_customMusicActivePath[1024];

/* Native OGG-only mask melody overlay. Retail Aku/Uka are CSEQ song IDs
 * 1 and 2 in a per-track pack; an independent PCM voice avoids destroying
 * the playing level's CSEQ/songset or restarting the level OGG clock. */
struct NativeMaskMusicState
{
    s16 *pcm;
    int frameCount;
    int channels;
    int songID;
    u64 positionFp;
    u32 stepFp;
    int active;
    int paused;
};
static struct NativeMaskMusicState s_maskMusic;
static void NativeCustomMusic_MaskStopNoLock(void)
{
    free(s_maskMusic.pcm);
    memset(&s_maskMusic,0,sizeof(s_maskMusic));
}
global_variable u64 s_hubClockFrames = 0;
global_variable int s_hubClockActive = 0;
global_variable int s_hubClockPaused = 0;

internal void NativeCustomMusic_StopNoLock(void)
{
    NativeCustomMusic_MaskStopNoLock();
	if (s_customMusic.pcm != NULL && s_customMusic.ownsPcm)
	{
		free(s_customMusic.pcm);
	}
	if (s_customMusic.finalPcm != NULL && s_customMusic.ownsFinalPcm)
	{
		free(s_customMusic.finalPcm);
	}

	memset(&s_customMusic, 0, sizeof(s_customMusic));
	s_customMusic.levelID = -1;
	s_customMusic.volume = 190;
	s_customMusicActivePath[0] = 0;
}

void NativeCustomMusic_BeginHubClock(void)
{
	NativeAudio_LockOutput();
	if (!s_hubClockActive)
	{
		s_hubClockFrames = 0;
		s_hubClockPaused = 0;
		s_hubClockActive = 1;
	}
	NativeAudio_UnlockOutput();
}

internal void NativeCustomMusic_StopTrackKeepHub(void)
{
	NativeAudio_LockOutput();
	NativeCustomMusic_StopNoLock();
	NativeAudio_UnlockOutput();
}

internal int NativeCustomMusic_LoadOgg(const char *path, s16 **pcmOut, int *frameCountOut, int *channelsOut, int *sampleRateOut)
{
	OggVorbis_File vf;
	vorbis_info *info;
	ogg_int64_t totalFrames64;
	size_t totalBytes;
	size_t bytesRead = 0;
	s16 *pcm = NULL;
	int bitstream = 0;
	int result = 0;

	*pcmOut = NULL;
	*frameCountOut = 0;
	*channelsOut = 0;
	*sampleRateOut = 0;

	if (ov_fopen(path, &vf) < 0)
	{
		return 0;
	}

	info = ov_info(&vf, -1);
	if ((info == NULL) || (info->rate <= 0) || ((info->channels != 1) && (info->channels != 2)))
	{
		fprintf(stderr, "[CustomMusic] OGG invalido: se requiere mono o stereo.\n");
		goto done;
	}

	totalFrames64 = ov_pcm_total(&vf, -1);
	if ((totalFrames64 <= 0) || ((uint64_t)totalFrames64 > (uint64_t)INT_MAX))
	{
		fprintf(stderr, "[CustomMusic] OGG demasiado grande o sin frames validos.\n");
		goto done;
	}

	if ((uint64_t)totalFrames64 > (uint64_t)SIZE_MAX / ((size_t)info->channels * sizeof(s16)))
	{
		fprintf(stderr, "[CustomMusic] OGG demasiado grande para memoria.\n");
		goto done;
	}

	totalBytes = (size_t)totalFrames64 * (size_t)info->channels * sizeof(s16);
	pcm = (s16 *)malloc(totalBytes);
	if (pcm == NULL)
	{
		fprintf(stderr, "[CustomMusic] Sin memoria para decodificar OGG.\n");
		goto done;
	}

	while (bytesRead < totalBytes)
	{
		size_t remaining = totalBytes - bytesRead;
		int request = (remaining > 65536u) ? 65536 : (int)remaining;
		long got = ov_read(&vf, (char *)pcm + bytesRead, request, 0, 2, 1, &bitstream);

		if (got == 0)
		{
			break;
		}
		if (got < 0)
		{
			fprintf(stderr, "[CustomMusic] Error decodificando OGG (%ld).\n", got);
			free(pcm);
			pcm = NULL;
			goto done;
		}

		bytesRead += (size_t)got;
	}

	if (bytesRead < (size_t)info->channels * sizeof(s16))
	{
		free(pcm);
		pcm = NULL;
		goto done;
	}

	*pcmOut = pcm;
	*frameCountOut = (int)(bytesRead / ((size_t)info->channels * sizeof(s16)));
	*channelsOut = info->channels;
	*sampleRateOut = (int)info->rate;

	pcm = NULL;
	result = 1;

done:
	if (pcm != NULL)
	{
		free(pcm);
	}

	ov_clear(&vf);
	return result;
}

/* Soundtrack package replacement for the two mask CSEQ melodies.
 * Missing or unusable files fall back to normal retail CSEQ masks. */
int NativeCustomMusic_TryStartMaskCue(int songID)
{
    if (NativeSession_NoModsActive()) return 0;
    struct NativeMusicSelectionResolved selected;
    char path[1536];
    struct stat st;
    s16 *pcm=NULL;
    int frames=0,channels=0,rate=0;
    int slot=songID==1 ? NATIVE_MUSIC_MASK_AKU_SLOT :
             songID==2 ? NATIVE_MUSIC_MASK_UKA_SLOT : -1;
    if(slot<0 ||
       NativeMusicSelection_Resolve(slot,&selected)!=NATIVE_MUSIC_SOURCE_OGG ||
       !selected.path[0] ||
       !NativeAssets_BuildPath(selected.path,path,sizeof(path)) ||
       stat(path,&st)!=0 || st.st_size<=0 ||
       st.st_size>24*1024*1024)
        return 0;

    if(!NativeCustomMusic_LoadOgg(path,&pcm,&frames,&channels,&rate))
        return 0;
    if(frames<=0 || rate<=0 || (s64)frames>(s64)rate*240)
    {
        free(pcm);
        return 0;
    }
    NativeAudio_LockOutput();
    NativeCustomMusic_MaskStopNoLock();
    s_maskMusic.pcm=pcm;
    s_maskMusic.frameCount=frames;
    s_maskMusic.channels=channels;
    s_maskMusic.songID=songID;
    s_maskMusic.stepFp=(u32)(((u64)rate<<CUSTOM_MUSIC_FP_SHIFT)/
                            CUSTOM_MUSIC_OUTPUT_RATE);
    if(!s_maskMusic.stepFp)s_maskMusic.stepFp=1;
    s_maskMusic.active=1;
    if(s_customMusic.active)s_customMusic.maskMuted=1;
    NativeAudio_UnlockOutput();
    Platform_Log("[Music] %s mask OGG override started: %s\n",
                 songID==1?"Aku":"Uka",selected.path);
    return 1;
}
void NativeCustomMusic_StopMaskCue(int songID)
{
    NativeAudio_LockOutput();
    if(s_maskMusic.active &&
       (!songID || s_maskMusic.songID==songID))
    {
        NativeCustomMusic_MaskStopNoLock();
        s_customMusic.maskMuted=0;
    }
    NativeAudio_UnlockOutput();
}
int NativeCustomMusic_RestartMaskCue(int songID)
{
    int found=0;
    NativeAudio_LockOutput();
    if(s_maskMusic.active && s_maskMusic.songID==songID)
    {
        s_maskMusic.positionFp=0;
        s_maskMusic.paused=0;
        found=1;
    }
    NativeAudio_UnlockOutput();
    return found;
}
internal void NativeCustomMusic_MixMaskCueNoLock(
    int *mixLeft,int *mixRight,s16 masterLeft,s16 masterRight)
{
    const struct NativeMaskMusicState *m=&s_maskMusic;
    u64 a,b;
    u32 frac;
    int left,right,vol;
    if(!m->active || m->paused || !m->pcm || m->frameCount<=0)
        return;
    a=(m->positionFp>>CUSTOM_MUSIC_FP_SHIFT)%(u64)m->frameCount;
    b=(a+1)%(u64)m->frameCount;
    frac=(u32)(m->positionFp & (CUSTOM_MUSIC_FP_ONE-1));
    if(m->channels==1)
    {
        left=m->pcm[a]+(int)(((s64)(m->pcm[b]-m->pcm[a])*frac)>>
                              CUSTOM_MUSIC_FP_SHIFT);
        right=left;
    }
    else
    {
        left=m->pcm[a*2]+(int)(((s64)(m->pcm[b*2]-m->pcm[a*2])*frac)>>
                              CUSTOM_MUSIC_FP_SHIFT);
        right=m->pcm[a*2+1]+
            (int)(((s64)(m->pcm[b*2+1]-m->pcm[a*2+1])*frac)>>
                   CUSTOM_MUSIC_FP_SHIFT);
    }
    vol=(sdata->vol_Music*128)/255;
    *mixLeft+=(int)(((s64)left*vol*masterLeft)/(255*0x3fff));
    *mixRight+=(int)(((s64)right*vol*masterRight)/(255*0x3fff));
    s_maskMusic.positionFp+=s_maskMusic.stepFp;
    if((s_maskMusic.positionFp>>CUSTOM_MUSIC_FP_SHIFT)>=(u64)m->frameCount)
        s_maskMusic.positionFp%=
            ((u64)m->frameCount<<CUSTOM_MUSIC_FP_SHIFT);
}
internal int NativeCustomMusic_FileExists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
	{
		return 0;
	}

	fclose(f);
	return 1;
}

int NativeCustomMusic_HasLevelTrack(int levelID)
{
	if (NativeSession_NoModsActive()) return 0;
	char relativePath[128];
	char path[1024];
	struct NativeMusicSelectionResolved selected;
	int source;

	/* A community track never inherits the host retail level_03.ogg. */
	if (NativeCustomTracks_IsActiveRace())
	{
		struct NativeMusicSelectionResolved trackSelection;
		int slot=NATIVE_MUSIC_CUSTOM_BASE+
		         NativeCustomTracks_SelectedIndex();
		int chosen=NativeMusicSelection_Resolve(slot,&trackSelection);
		if(chosen==NATIVE_MUSIC_SOURCE_OGG)
		{
			if (!NativeAssets_BuildPath(trackSelection.path,path,sizeof(path)))
				return 0;
			return NativeCustomMusic_FileExists(path);
		}
		return chosen==NATIVE_MUSIC_SOURCE_EXISTING &&
		       NativeCustomTracks_SelectedMusicKind() ==
		       NATIVE_CUSTOM_TRACK_MUSIC_OGG;
	}

	source=NativeMusicSelection_Resolve(
	    (sdata->gGT != NULL && IS_BOSS_RACE(sdata->gGT->gameMode1)) ?
	    NATIVE_MUSIC_BOSS_SLOT : levelID,&selected);
	if (source==NATIVE_MUSIC_SOURCE_OGG)
	{
		if (!NativeAssets_BuildPath(selected.path,path,sizeof(path)))
			return 0;
		return NativeCustomMusic_FileExists(path);
	}
	if (source!=NATIVE_MUSIC_SOURCE_EXISTING)
		return 0;

	/* The public resolver is intentionally separate from TryStartLevel:
	 * callers such as Adventure Garage can preserve the currently playing
	 * retail/menu music when no override exists. Today this checks OGG;
	 * future native CSEQ overrides can be added here without changing
	 * the gameplay trigger sites. */
	if (levelID >= 0 && levelID < CUSTOM_MUSIC_CACHE_LEVELS &&
		SDL_GetAtomicInt(&s_customMusicCache[levelID].loaded) &&
		s_customMusicCache[levelID].pcm != NULL)
	{
		return 1;
	}

	snprintf(relativePath, sizeof(relativePath),
		"MUSIC_CUSTOM/level_%02d.ogg", levelID);

	if (!NativeAssets_BuildPath(relativePath, path, sizeof(path)))
		return 0;

	return NativeCustomMusic_FileExists(path);
}

internal int NativeCustomMusic_GetCachedLevel(
	int levelID,
	s16 **pcmOut,
	int *frameCountOut,
	int *channelsOut,
	int *sampleRateOut,
	s16 **finalPcmOut,
	int *finalFrameCountOut)
{
	struct NativeCustomMusicCacheEntry *entry;

	if (levelID < 0 || levelID >= CUSTOM_MUSIC_CACHE_LEVELS)
		return 0;

	entry = &s_customMusicCache[levelID];
	if (!SDL_GetAtomicInt(&entry->loaded) || entry->pcm == NULL || entry->frameCount <= 0)
		return 0;

	*pcmOut = entry->pcm;
	*frameCountOut = entry->frameCount;
	*channelsOut = entry->channels;
	*sampleRateOut = entry->sampleRate;
	*finalPcmOut = entry->finalPcm;
	*finalFrameCountOut = entry->finalFrameCount;
	return 1;
}

int NativeCustomMusic_PreloadAll(void)
{
	if (NativeSession_NoModsActive())
	{
		SDL_SetAtomicInt(&s_customMusicCacheReady, 1);
		SDL_SetAtomicInt(&s_customMusicCacheLoading, 0);
		return 1;
	}
	int loadedCount = 0;
	size_t totalBytes = 0;

	if (SDL_GetAtomicInt(&s_customMusicCacheReady))
		return 1;
	if (!SDL_CompareAndSwapAtomicInt(&s_customMusicCacheLoading, 0, 1))
		return 0;
	printf("[CustomMusic] Precache: decodificando todos los OGG residentes...\n");
	fflush(stdout);

	for (int levelID = 0; levelID < CUSTOM_MUSIC_CACHE_LEVELS; levelID++)
	{
		struct NativeCustomMusicCacheEntry *entry = &s_customMusicCache[levelID];
		char relativePath[128];
		char finalRelativePath[128];
		char path[1024];
		char finalPath[1024];
		s16 *pcm = NULL;
		s16 *finalPcm = NULL;
		int frameCount = 0;
		int finalFrameCount = 0;
		int channels = 0;
		int sampleRate = 0;
		int finalChannels = 0;
		int finalSampleRate = 0;

		if (SDL_GetAtomicInt(&entry->loaded))
		{
			loadedCount++;
			totalBytes += (size_t)entry->frameCount * (size_t)entry->channels * sizeof(s16);
			if (entry->finalPcm != NULL)
				totalBytes += (size_t)entry->finalFrameCount * (size_t)entry->channels * sizeof(s16);
			continue;
		}

		snprintf(relativePath, sizeof(relativePath), "MUSIC_CUSTOM/level_%02d.ogg", levelID);
		if (!NativeAssets_BuildPath(relativePath, path, sizeof(path)) || !NativeCustomMusic_FileExists(path))
			continue;

		if (!NativeCustomMusic_LoadOgg(path, &pcm, &frameCount, &channels, &sampleRate))
		{
			printf("[CustomMusic] Precache level %d: fallo; se cargara bajo demanda.\n", levelID);
			fflush(stdout);
			continue;
		}

		snprintf(finalRelativePath, sizeof(finalRelativePath), "MUSIC_CUSTOM/level_%02d_final.ogg", levelID);
		if (NativeAssets_BuildPath(finalRelativePath, finalPath, sizeof(finalPath)) && NativeCustomMusic_FileExists(finalPath))
		{
			if (!NativeCustomMusic_LoadOgg(finalPath, &finalPcm, &finalFrameCount, &finalChannels, &finalSampleRate) ||
				finalChannels != channels || finalSampleRate != sampleRate)
			{
				if (finalPcm != NULL) free(finalPcm);
				finalPcm = NULL;
				finalFrameCount = 0;
			}
		}

		entry->pcm = pcm;
		entry->frameCount = frameCount;
		entry->channels = channels;
		entry->sampleRate = sampleRate;
		entry->finalPcm = finalPcm;
		entry->finalFrameCount = finalFrameCount;
		/* Publish the completed entry only after every pointer/count is valid. */
		SDL_SetAtomicInt(&entry->loaded, 1);

		loadedCount++;
		totalBytes += (size_t)frameCount * (size_t)channels * sizeof(s16);
		if (finalPcm != NULL)
			totalBytes += (size_t)finalFrameCount * (size_t)channels * sizeof(s16);

		printf("[CustomMusic] Precache level %02d: %.2f MiB%s\n",
			levelID,
			(double)((size_t)frameCount * (size_t)channels * sizeof(s16) +
				(finalPcm ? (size_t)finalFrameCount * (size_t)channels * sizeof(s16) : 0)) / (1024.0 * 1024.0),
			finalPcm ? " + Final Lap" : "");
		fflush(stdout);
	}

	SDL_SetAtomicInt(&s_customMusicCacheReady, 1);
	SDL_SetAtomicInt(&s_customMusicCacheLoading, 0);
	printf("[CustomMusic] Precache listo: %d niveles, %.2f MiB PCM residentes.\n",
		loadedCount, (double)totalBytes / (1024.0 * 1024.0));
	fflush(stdout);
	return 1;
}

internal int SDLCALL NativeCustomMusic_PreloadThread(void *unused)
{
	(void)unused;
	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
	NativeCustomMusic_PreloadAll();
	return 0;
}

int NativeCustomMusic_PreloadAllAsync(void)
{
	if (NativeSession_NoModsActive())
	{
		/* Main-menu load waits for IsPreloadComplete(), even when
		 * there is intentionally no custom music to decode. */
		SDL_SetAtomicInt(&s_customMusicCacheReady, 1);
		return 1;
	}
	SDL_Thread *thread;

	if (SDL_GetAtomicInt(&s_customMusicCacheReady) ||
	    SDL_GetAtomicInt(&s_customMusicCacheLoading))
		return 1;

	thread = SDL_CreateThread(
		NativeCustomMusic_PreloadThread,
		"CTR OGG precache",
		NULL);
	if (thread == NULL)
	{
		fprintf(stderr, "[CustomMusic] No pude iniciar precache async: %s\n", SDL_GetError());
		return 0;
	}

	SDL_DetachThread(thread);
	return 1;
}

int NativeCustomMusic_IsPreloadComplete(void)
{
	return SDL_GetAtomicInt(&s_customMusicCacheReady) != 0;
}

/*
 * Simple overlap-add time compression.
 *
 * We advance faster through the source than through the output, while each
 * grain itself is reproduced at its original sample rate. That changes tempo
 * without intentionally changing pitch.
 *
 * This is deliberately dependency-free and precomputed when the OGG loads,
 * so the real-time audio callback remains cheap.
 */

internal int NativeCustomMusic_TryStartLevelEx(int levelID, int preservePosition)
{
	if (NativeSession_NoModsActive()) return 0;
	char relativePath[1024];
	char finalRelativePath[1024];
	char path[1536];
	char finalPath[1536];
	int customTrack = NativeCustomTracks_IsActiveRace();
	struct NativeMusicSelectionResolved selected;
	int resolvedSlot=customTrack ?
		NATIVE_MUSIC_CUSTOM_BASE+NativeCustomTracks_SelectedIndex() :
		(sdata->gGT != NULL && IS_BOSS_RACE(sdata->gGT->gameMode1)) ?
		NATIVE_MUSIC_BOSS_SLOT : levelID;
	int source = NativeMusicSelection_Resolve(resolvedSlot, &selected);
	int musicKey = customTrack ?
		NativeCustomTracks_SelectedMusicKey() : resolvedSlot;

	s16 *newPcm = NULL;
	s16 *newFinalPcm = NULL;

	int newFrameCount = 0;
	int newFinalFrameCount = 0;
	int newChannels = 0;
	int newSampleRate = 0;

	int finalChannels = 0;
	int finalSampleRate = 0;
	int newPcmOwned = 0;
	int newFinalPcmOwned = 0;
	u64 startPositionFp = 0;
	u64 startTimeline = 0;

	static int bannerPrinted = 0;

	if (customTrack &&
	    (source == NATIVE_MUSIC_SOURCE_CSEQ ||
	     source == NATIVE_MUSIC_SOURCE_RETAIL ||
	     (source == NATIVE_MUSIC_SOURCE_EXISTING &&
	      NativeCustomTracks_SelectedMusicKind() != NATIVE_CUSTOM_TRACK_MUSIC_OGG)))
		return 0;
	if (!customTrack && source != NATIVE_MUSIC_SOURCE_EXISTING &&
	    source != NATIVE_MUSIC_SOURCE_OGG)
		return 0;

	if (!bannerPrinted)
	{
		printf("[CustomMusic] CTR Music Core v0.4.9.3 - LOCK NORMAL TRACK\n");
		fflush(stdout);
		bannerPrinted = 1;
	}

	/* CSEQ 1/2 masks leave the OGG playing silently. Restore its audible
	 * output without decoding or seeking when CSEQ 0 starts again. */
	NativeAudio_LockOutput();
	if (s_customMusic.active && (s_customMusic.levelID == musicKey) &&
	    (source == NATIVE_MUSIC_SOURCE_EXISTING ||
	     (source == NATIVE_MUSIC_SOURCE_OGG &&
	      !strcmp(s_customMusicActivePath, selected.path))))
	{
		if (!preservePosition) s_customMusic.maskMuted = 0;
		if (preservePosition) s_customMusic.hubFallback = 0;
		NativeAudio_UnlockOutput();
		return 1;
	}
	NativeAudio_UnlockOutput();

	if (!customTrack && source == NATIVE_MUSIC_SOURCE_EXISTING &&
	    NativeCustomMusic_GetCachedLevel(
		levelID,
		&newPcm,
		&newFrameCount,
		&newChannels,
		&newSampleRate,
		&newFinalPcm,
		&newFinalFrameCount))
	{
		printf("[CustomMusic] Level %d: usando PCM precargado.\n", levelID);
		fflush(stdout);
		snprintf(relativePath, sizeof(relativePath),
		         "MUSIC_CUSTOM/level_%02d.ogg", levelID);
		goto track_ready;
	}

	if (customTrack && source != NATIVE_MUSIC_SOURCE_OGG)
	{
		const char *normal=NativeCustomTracks_SelectedMusicPath();
		const char *final=NativeCustomTracks_SelectedFinalMusicPath();
		snprintf(relativePath,sizeof(relativePath),"%s",normal?normal:"");
		snprintf(finalRelativePath,sizeof(finalRelativePath),"%s",final?final:"");
	}
	else if (source == NATIVE_MUSIC_SOURCE_OGG)
	{
		const char *dot;
		snprintf(relativePath,sizeof(relativePath),"%s",selected.path);
		dot=strrchr(relativePath,'.');
		finalRelativePath[0]=0;
		if (dot && (size_t)(dot-relativePath)<sizeof(finalRelativePath)-12)
			snprintf(finalRelativePath,sizeof(finalRelativePath),
			         "%.*s_final.ogg",(int)(dot-relativePath),relativePath);
	}
	else
	{
		snprintf(relativePath, sizeof(relativePath),
			"MUSIC_CUSTOM/level_%02d.ogg", levelID);
		snprintf(finalRelativePath, sizeof(finalRelativePath),
			"MUSIC_CUSTOM/level_%02d_final.ogg", levelID);
	}

	if (!NativeAssets_BuildPath(relativePath, path, sizeof(path)))
	{
		fprintf(stderr,
			"[CustomMusic] Level %d: no pude construir la ruta.\n",
			levelID);
		if (s_hubClockActive) NativeCustomMusic_StopTrackKeepHub();
		else NativeCustomMusic_Stop();
		return 0;
	}

	if (!NativeCustomMusic_FileExists(path))
	{
		printf("[CustomMusic] Level %d: sin %s\n",
			levelID, relativePath);
		fflush(stdout);
		if (s_hubClockActive) NativeCustomMusic_StopTrackKeepHub();
		else NativeCustomMusic_Stop();
		return 0;
	}

	printf("[CustomMusic] Level %d: cargando %s\n",
		levelID, relativePath);
	fflush(stdout);

	if (!NativeCustomMusic_LoadOgg(
		path,
		&newPcm,
		&newFrameCount,
		&newChannels,
		&newSampleRate))
	{
		fprintf(stderr,
			"[CustomMusic] Level %d: no pude cargar %s; uso CSEQ original.\n",
			levelID, relativePath);
		if (s_hubClockActive) NativeCustomMusic_StopTrackKeepHub();
		else NativeCustomMusic_Stop();
		return 0;
	}
	newPcmOwned = 1;

	/* Optional pre-rendered Final Lap OGG. */
	if (finalRelativePath[0] &&
	    NativeAssets_BuildPath(
		finalRelativePath,
		finalPath,
		sizeof(finalPath)) &&
		NativeCustomMusic_FileExists(finalPath))
	{
		printf("[CustomMusic] Level %d: cargando Final Lap %s\n",
			levelID, finalRelativePath);
		fflush(stdout);

		if (!NativeCustomMusic_LoadOgg(
			finalPath,
			&newFinalPcm,
			&newFinalFrameCount,
			&finalChannels,
			&finalSampleRate))
		{
			printf("[CustomMusic] Final Lap invalido; uso tema normal.\n");
			fflush(stdout);
			newFinalPcm = NULL;
			newFinalFrameCount = 0;
		}
		else
		{
			newFinalPcmOwned = 1;
		}
		if (newFinalPcm != NULL && ((finalChannels != newChannels) ||
				 (finalSampleRate != newSampleRate)))
		{
			printf("[CustomMusic] Final Lap debe tener mismos canales y sample rate; uso tema normal.\n");
			fflush(stdout);
			free(newFinalPcm);
			newFinalPcm = NULL;
			newFinalFrameCount = 0;
			newFinalPcmOwned = 0;
		}
	}
	else
	{
		printf("[CustomMusic] Level %d: sin Final Lap custom.\n", levelID);
		fflush(stdout);
	}

track_ready:
	NativeAudio_LockOutput();
	if (preservePosition && s_hubClockActive && newFrameCount > 0)
	{
		/* The hub clock runs even when no OGG is installed in a zone. */
		u64 mappedFrame;
		startTimeline = s_hubClockFrames;
		mappedFrame = (startTimeline * (u64)newSampleRate) /
			CUSTOM_MUSIC_OUTPUT_RATE;
		startPositionFp = (mappedFrame % (u64)newFrameCount) <<
			CUSTOM_MUSIC_FP_SHIFT;
	}
	NativeCustomMusic_StopNoLock();

	s_customMusic.pcm = newPcm;
	s_customMusic.frameCount = newFrameCount;
	s_customMusic.finalPcm = newFinalPcm;
	s_customMusic.finalFrameCount = newFinalFrameCount;
	s_customMusic.ownsPcm = newPcmOwned;
	s_customMusic.ownsFinalPcm = newFinalPcmOwned;
	s_customMusic.finalLapActive = 0;

	s_customMusic.channels = newChannels;
	s_customMusic.sampleRate = newSampleRate;
	s_customMusic.positionFp = startPositionFp;
	s_customMusic.timelineOutputFrames = startTimeline;
	s_customMusic.stepFp =
		(u32)(((u64)newSampleRate << CUSTOM_MUSIC_FP_SHIFT) /
			CUSTOM_MUSIC_OUTPUT_RATE);

	if (s_customMusic.stepFp == 0)
	{
		s_customMusic.stepFp = 1;
	}

	s_customMusic.active = 1;
	s_customMusic.paused = 0;
	s_customMusic.maskMuted = 0;
	s_customMusic.hubFallback = 0;

	/*
	 * Ceiling lower than the old fixed 190.
	 * ChangeVolume will scale this from CTR's music volume.
	 */
	s_customMusic.volume = (sdata->vol_Music * 128) / 255;
	s_customMusic.levelID = musicKey;
	snprintf(s_customMusicActivePath,sizeof(s_customMusicActivePath),
	         "%s",relativePath);

	NativeAudio_UnlockOutput();

	printf("[CustomMusic] Level %d: OK - %d Hz, %d canal(es), %d frames.\n",
		levelID, newSampleRate, newChannels, newFrameCount);
	fflush(stdout);

	return 1;
}

int NativeCustomMusic_TryStartLevel(int levelID)
{
	return NativeCustomMusic_TryStartLevelEx(levelID, 0);
}

int NativeCustomMusic_TrySwapHub(int levelID)
{
	if (NativeSession_NoModsActive()) return 0;
	char relativePath[128];
	char path[1024];

	if (levelID >= 0 && levelID < CUSTOM_MUSIC_CACHE_LEVELS &&
	    SDL_GetAtomicInt(&s_customMusicCache[levelID].loaded))
		return NativeCustomMusic_TryStartLevelEx(levelID, 1);

	snprintf(relativePath, sizeof(relativePath),
		"MUSIC_CUSTOM/level_%02d.ogg", levelID);
	if (!NativeAssets_BuildPath(relativePath, path, sizeof(path)) ||
		!NativeCustomMusic_FileExists(path))
	{
		/* Preserve the timeline while the original CSEQ plays in this hub. */
		NativeAudio_LockOutput();
		if (s_customMusic.active) s_customMusic.hubFallback = 1;
		NativeAudio_UnlockOutput();
		return 0;
	}
	return NativeCustomMusic_TryStartLevelEx(levelID, 1);
}

int NativeCustomMusic_ShouldMuteLevelCseq(void)
{
	int mute;
	NativeAudio_LockOutput();
	mute = s_customMusic.active && !s_customMusic.hubFallback;
	NativeAudio_UnlockOutput();
	return mute;
}

int NativeCustomMusic_IsActive(void)
{
	int active;

	NativeAudio_LockOutput();
	active = s_customMusic.active;
	NativeAudio_UnlockOutput();

	return active;
}

void NativeCustomMusic_Stop(void)
{
	NativeAudio_LockOutput();
	NativeCustomMusic_StopNoLock();
	s_hubClockActive = 0;
	s_hubClockFrames = 0;
	s_hubClockPaused = 0;
	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_SetPaused(int paused)
{
	NativeAudio_LockOutput();
	s_hubClockPaused = paused != 0;

	if (s_customMusic.active)
	{
		s_customMusic.paused = paused != 0;
	}
    if(s_maskMusic.active)
        s_maskMusic.paused=paused!=0;

	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_SetMaskMuted(int muted)
{
	NativeAudio_LockOutput();
	if (s_customMusic.active) s_customMusic.maskMuted = muted != 0;
	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_SetVolume(int volume)
{
	int scaled;

	if (volume < 0)
	{
		volume = 0;
	}
	else if (volume > 255)
	{
		volume = 255;
	}

	/* Keep custom OGG below the previous overly-loud fixed level. */
	scaled = (volume * 128) / 255;

	NativeAudio_LockOutput();

	if (s_customMusic.active)
	{
		s_customMusic.volume = scaled;
	}

	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_Restart(void)
{
	NativeAudio_LockOutput();

	if (s_customMusic.active)
	{
		s_customMusic.positionFp = 0;
		s_customMusic.timelineOutputFrames = 0;
		if (s_hubClockActive) s_hubClockFrames = 0;
		s_customMusic.paused = 0;
		s_customMusic.maskMuted = 0;
		s_customMusic.finalLapActive = 0;
		s_customMusic.finalLapRequested = 0;
	}

	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_EndRace(void)
{
	NativeAudio_LockOutput();
	if (s_customMusic.active)
	{
		if (s_customMusic.finalLapActive && s_customMusic.frameCount > 0 &&
			s_customMusic.finalFrameCount > 0)
		{
			u64 finalFrame = s_customMusic.positionFp >> CUSTOM_MUSIC_FP_SHIFT;
			u64 normalFrame =
				((finalFrame % (u64)s_customMusic.finalFrameCount) *
				 (u64)s_customMusic.frameCount) /
				(u64)s_customMusic.finalFrameCount;
			s_customMusic.positionFp =
				(normalFrame << CUSTOM_MUSIC_FP_SHIFT) |
				(s_customMusic.positionFp & (CUSTOM_MUSIC_FP_ONE - 1));
		}
		s_customMusic.finalLapActive = 0;
		s_customMusic.finalLapRequested = 0;
		s_customMusic.raceEndSeen = 1;
	}
	NativeAudio_UnlockOutput();
}

void NativeCustomMusic_EnableFinalLap(void)
{
	int changed = 0;
	int hasFinal = 0;
	int firstRequest = 0;

	NativeAudio_LockOutput();

	if (s_customMusic.active && sdata->audioState != AUDIO_RACE_END)
	{
		if (!s_customMusic.finalLapRequested)
		{
			s_customMusic.finalLapRequested = 1;
			firstRequest = 1;
		}
		/*
		 * IMPORTANT:
		 * If there is no _final.ogg, do NOT touch position, pause state,
		 * volume or active state. The normal OGG must continue uninterrupted.
		 */
		if (!s_customMusic.finalLapActive &&
			(s_customMusic.finalPcm != NULL) &&
			(s_customMusic.finalFrameCount > 0) &&
			(s_customMusic.frameCount > 0))
		{
			u64 normalFrame =
				s_customMusic.positionFp >> CUSTOM_MUSIC_FP_SHIFT;
			u64 finalFrame;

			normalFrame %= (u64)s_customMusic.frameCount;

			finalFrame =
				(normalFrame * (u64)s_customMusic.finalFrameCount) /
				(u64)s_customMusic.frameCount;

			s_customMusic.positionFp =
				finalFrame << CUSTOM_MUSIC_FP_SHIFT;

			s_customMusic.finalLapActive = 1;
			hasFinal = 1;
			changed = 1;
		}
		else if (s_customMusic.finalLapActive)
		{
			hasFinal = 1;
		}
	}

	NativeAudio_UnlockOutput();

	if (changed)
	{
		printf("[CustomMusic] FINAL LAP -> usando _final.ogg\n");
	}
	else if (!hasFinal && firstRequest)
	{
		printf("[CustomMusic] FINAL LAP -> sin _final.ogg; OGG normal continua\n");
	}

	fflush(stdout);
}

internal s16 *NativeCustomMusic_GetCurrentPcm(void)
{
	if (s_customMusic.finalLapActive &&
		(s_customMusic.finalPcm != NULL) &&
		(s_customMusic.finalFrameCount > 0))
	{
		return s_customMusic.finalPcm;
	}

	return s_customMusic.pcm;
}

internal int NativeCustomMusic_GetCurrentFrameCount(void)
{
	if (s_customMusic.finalLapActive &&
		(s_customMusic.finalPcm != NULL) &&
		(s_customMusic.finalFrameCount > 0))
	{
		return s_customMusic.finalFrameCount;
	}

	return s_customMusic.frameCount;
}

internal int NativeCustomMusic_ReadSample(int channel)
{
	s16 *pcm;
	int frameCount;
	u64 frame0;
	u64 frame1;
	u32 frac;
	int sample0;
	int sample1;

	if (!s_customMusic.active || s_customMusic.paused)
	{
		return 0;
	}

	pcm = NativeCustomMusic_GetCurrentPcm();
	frameCount = NativeCustomMusic_GetCurrentFrameCount();

	if ((pcm == NULL) || (frameCount <= 0))
	{
		return 0;
	}

	frame0 = s_customMusic.positionFp >> CUSTOM_MUSIC_FP_SHIFT;

	if (frame0 >= (u64)frameCount)
	{
		frame0 %= (u64)frameCount;
		s_customMusic.positionFp = frame0 << CUSTOM_MUSIC_FP_SHIFT;
	}

	frame1 = frame0 + 1;

	if (frame1 >= (u64)frameCount)
	{
		frame1 = 0;
	}

	if (s_customMusic.channels == 1)
	{
		sample0 = pcm[frame0];
		sample1 = pcm[frame1];
	}
	else
	{
		sample0 = pcm[frame0 * 2 + channel];
		sample1 = pcm[frame1 * 2 + channel];
	}

	frac = (u32)(s_customMusic.positionFp & (CUSTOM_MUSIC_FP_ONE - 1));

	return sample0 +
		(int)(((s64)(sample1 - sample0) * frac) >> CUSTOM_MUSIC_FP_SHIFT);
}

void NativeCustomMusic_MixFrameNoLock(
	int *mixLeft,
	int *mixRight,
	s16 masterLeft,
	s16 masterRight)
{
	int left;
	int right;
	int frameCount;
	int maskPlaying = 0;
	int i;
	s64 scaledLeft;
	s64 scaledRight;

	if (s_hubClockActive && !s_hubClockPaused) s_hubClockFrames++;

    NativeCustomMusic_MixMaskCueNoLock(
        mixLeft,mixRight,masterLeft,masterRight);
	if (!s_customMusic.active || s_customMusic.paused)
	{
		return;
	}

	frameCount = NativeCustomMusic_GetCurrentFrameCount();

	if (frameCount <= 0)
	{
		return;
	}

	left = NativeCustomMusic_ReadSample(0);
	right = NativeCustomMusic_ReadSample(
		s_customMusic.channels == 1 ? 0 : 1);

	if (sdata->audioState != AUDIO_RACE_END)
		s_customMusic.raceEndSeen = 0;
	else if (sdata->XA_State != XA_IDLE && !s_customMusic.raceEndSeen)
	{
		/* Return to the regular race OGG after the victory/defeat XA.
		 * Map the current phase back from the Final Lap arrangement. */
		if (s_customMusic.finalLapActive && s_customMusic.frameCount > 0 &&
			s_customMusic.finalFrameCount > 0)
		{
			u64 finalFrame = s_customMusic.positionFp >> CUSTOM_MUSIC_FP_SHIFT;
			u64 normalFrame =
				((finalFrame % (u64)s_customMusic.finalFrameCount) *
				 (u64)s_customMusic.frameCount) /
				(u64)s_customMusic.finalFrameCount;
			s_customMusic.positionFp =
				(normalFrame << CUSTOM_MUSIC_FP_SHIFT) |
				(s_customMusic.positionFp & (CUSTOM_MUSIC_FP_ONE - 1));
		}
		s_customMusic.finalLapActive = 0;
		s_customMusic.finalLapRequested = 0;
		s_customMusic.raceEndSeen = 1;
		left = NativeCustomMusic_ReadSample(0);
		right = NativeCustomMusic_ReadSample(
			s_customMusic.channels == 1 ? 0 : 1);
	}

	/* Pause/resume can call CSEQ 0 again before the mask has finished.
	 * Check the actual playing pools, so OGG cannot overlap Aku/Uka. */
	if (sdata->ptrCseqHeader != NULL)
	{
		for (i = 0; i < 2; i++)
		{
			struct Song *song = &sdata->songPool[i];
			if ((song->flags & 1) && (song->id == 1 || song->id == 2))
			{
				maskPlaying = 1;
				break;
			}
		}
	}
    if(s_maskMusic.active)maskPlaying=1;
	/* Release the mute automatically when the original mask CSEQ ends. */
	if (!maskPlaying) s_customMusic.maskMuted = 0;
	/* Win/lose tracks play through XA. Keep the OGG clock advancing
	 * silently until CTR finishes that track. */
	if (maskPlaying || s_customMusic.maskMuted || s_customMusic.hubFallback ||
		(sdata->audioState == AUDIO_RACE_END && sdata->XA_State != XA_IDLE))
		left = right = 0;

	scaledLeft = (s64)left * s_customMusic.volume * masterLeft;
	scaledRight = (s64)right * s_customMusic.volume * masterRight;

	scaledLeft /= (255 * 0x3fff);
	scaledRight /= (255 * 0x3fff);

	*mixLeft += (int)scaledLeft;
	*mixRight += (int)scaledRight;

	s_customMusic.positionFp += s_customMusic.stepFp;
	s_customMusic.timelineOutputFrames++;

	while ((s_customMusic.positionFp >> CUSTOM_MUSIC_FP_SHIFT) >=
		(u64)frameCount)
	{
		s_customMusic.positionFp -=
			((u64)frameCount << CUSTOM_MUSIC_FP_SHIFT);
	}
}
