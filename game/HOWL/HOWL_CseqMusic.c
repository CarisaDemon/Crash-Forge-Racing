#include <common.h>
#include <platform/native_custom_music.h>
#include <platform/native_custom_tracks.h>
#if defined(CTR_NATIVE)
#include <NativeHangout.h>
#endif
#if defined(CTR_CSEQ_LIBERATION)
#include <NativeMusicRegistry.h>
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028c78-0x80028d64
int CseqMusic_Start(u16 songID, int p2, struct SongSet *p3, int p4, int p5)
{
	int i;
	struct Song *song;
#if defined(CTR_NATIVE)
	int musicLevelID =
		(sdata->gGT != NULL && NativeHangout_IsActive())
			? NATIVE_HANGOUT_MUSIC_LEVEL_ID
			: ((sdata->gGT != NULL) ? sdata->gGT->levelID : -1);
#endif
#if defined(CTR_CSEQ_LIBERATION)
	int externalCseq = 0;
	struct SongSet *effectiveSongSet = p3;
	int effectiveSongSetBits = p4;
#endif

	if (sdata->boolAudioEnabled == 0)
	{
		return 0;
	}
#if defined(CTR_CSEQ_LIBERATION)
	if (songID == 0 && sdata->gGT != NULL)
	{
		int customTrack = NativeCustomTracks_IsActiveRace();
		if (customTrack &&
		    NativeCustomTracks_SelectedMusicKind()!=NATIVE_CUSTOM_TRACK_MUSIC_CSEQ)
		{
			/* Custom maps share Crash Cove's level shell, never its music.
			 * OGG is loaded from this track's own mod folder only.
			 * Missing music is intentionally SILENT; race SFX still work. */
			CseqMusic_StopAll();
			if (NativeCustomTracks_SelectedMusicKind()==NATIVE_CUSTOM_TRACK_MUSIC_OGG)
			{
				if(!NativeCustomMusic_TryStartLevel(musicLevelID))
					Platform_Log("[CustomTracks] OGG failed; track remains silent\n");
			}
			else
				Platform_Log("[CustomTracks] no mod music; race is silent\n");
			return 1;
		}
		externalCseq = customTrack ?
			NativeMusicRegistry_TryApplyCustomTrack() :
			NativeMusicRegistry_TryApplyLevel(musicLevelID);
		if(customTrack && !externalCseq)
		{
			/* A missing/invalid mod CSEQ must not play retail Crash Cove. */
			CseqMusic_StopAll();
			Platform_Log("[CustomTracks] CSEQ unavailable; race is silent\n");
			return 1;
		}

		/*
		 * External CSEQ packs are complete songs. Adventure hubs normally
		 * pass a retail SongSet with 20 layer bits, but a custom song can
		 * have any number of sequences. Passing the retail SongSet would
		 * make SongPool_Start reject the pack when the counts differ.
		 */
		if (externalCseq)
		{
			int stalePoolCount = 0;
			int poolIndex;

			/*
			 * The high-level Music state can already say "stopped" while one
			 * of the two low-level CSEQ pools is still flagged active. That
			 * leaves no free slot for the external song. Clear those stale
			 * pools explicitly before starting the replacement theme.
			 */
			for (poolIndex = 0; poolIndex < 2; poolIndex++)
			{
				struct Song *staleSong = &sdata->songPool[poolIndex];

				if ((staleSong->flags & 1) != 0)
				{
					Platform_Log(
						"[CSEQ] clearing stale pool %d before external level_%02d: id=%d flags=0x%02x seqs=%d\n",
						poolIndex,
						musicLevelID,
						staleSong->id,
						staleSong->flags,
						staleSong->numSequences);
					stalePoolCount++;
				}
			}

			if (stalePoolCount != 0)
				CseqMusic_StopAll();

			if (effectiveSongSet != NULL)
			{
				Platform_Log(
					"[CSEQ] external level_%02d bypassing retail hub SongSet (%d layers)\n",
					musicLevelID,
					effectiveSongSet->numSeqs);
			}

			effectiveSongSet = NULL;
			effectiveSongSetBits = 0;
		}
	}
#endif

	if (sdata->ptrCseqHeader == 0)
	{
		return 0;
	}

	if (sdata->ptrCseqHeader->numSongs <= songID)
	{
		return 0;
	}

#ifdef CTR_NATIVE
	if (songID == 0)
	{
#if defined(CTR_CSEQ_LIBERATION)
		if (!externalCseq)
		{
#endif
			if (sdata->gGT->levelID >= GEM_STONE_VALLEY &&
				sdata->gGT->levelID < INTRO_RACE_TODAY)
				NativeCustomMusic_BeginHubClock();
			if (NativeCustomMusic_TryStartLevel(musicLevelID))
			{
				/* Keep the original Adventure CSEQ running as a silent clock. */
				if (sdata->gGT->levelID < GEM_STONE_VALLEY ||
					sdata->gGT->levelID >= INTRO_RACE_TODAY)
					return 1;
			}
#if defined(CTR_CSEQ_LIBERATION)
		}
#endif
	}
#endif
	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is free
		if ((song->flags & 1) == 0)
		{
			// start song in this pool
#if defined(CTR_CSEQ_LIBERATION)
			SongPool_Start(
				song,
				songID,
				p2,
				p5,
				effectiveSongSet,
				effectiveSongSetBits);

			if (externalCseq)
			{
				Platform_Log(
					"[CSEQ] external level_%02d started: %d sequences, bpm=%d, tpqn=%d\n",
					musicLevelID,
					song->numSequences,
					song->bpm,
					song->tpqn);
			}
#else
			SongPool_Start(song, songID, p2, p5, p3, p4);
#endif
#ifdef CTR_NATIVE
			if (songID == 0 && sdata->gGT->levelID >= GEM_STONE_VALLEY &&
				sdata->gGT->levelID < INTRO_RACE_TODAY &&
				NativeCustomMusic_ShouldMuteLevelCseq())
				SongPool_Volume(song, 0, 0, 1);
#endif
#ifdef CTR_NATIVE
			/* The original Aku/Uka CSEQ plays while the OGG advances silently. */
			if ((songID == 1 || songID == 2) && NativeCustomMusic_IsActive())
			{
				NativeCustomMusic_SetMaskMuted(1);
			}
#endif

			Smart_ExitCriticalSection();
			return 1;
		}
	}

	Smart_ExitCriticalSection();
	return 0;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028d64-0x80028de0
// pause all songs
void CseqMusic_Pause()
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
#ifdef CTR_NATIVE
	NativeCustomMusic_SetPaused(1);
#endif


	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if ((song->flags & 1) != 0)
		{
			// pause song
			song->flags |= 2;
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028de0-0x80028e5c
// resume all songs
void CseqMusic_Resume()
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
#ifdef CTR_NATIVE
	NativeCustomMusic_SetPaused(0);
#endif


	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if ((song->flags & 1) != 0)
		{
			// unpause song
			song->flags &= ~(2);
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028e5c-0x80028f34
void CseqMusic_ChangeVolume(u16 songID, int p2, int p3)
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader->numSongs <= songID)
	{
		return;
	}

	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if (((song->flags & 1) != 0) && (song->id == songID))
		{
			SongPool_Volume(song, p2 & 0xff, p3 & 0xff, 0);
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80028f34-0x80029008
void CseqMusic_Restart(u16 songID, int p2)
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader->numSongs <= songID)
	{
		return;
	}

#ifdef CTR_NATIVE
	if ((songID == 0) && NativeCustomMusic_IsActive())
	{
		NativeCustomMusic_Restart();
		return;
	}
#endif
	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if (((song->flags & 1) != 0) && (song->id == songID))
		{
			song->flags |= 4;
			SongPool_Volume(song, 0, p2 & 0xff, 0);
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029008-0x800290cc
void CseqMusic_ChangeTempo(u16 songID, int p2)
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader->numSongs <= songID)
	{
		return;
	}

#ifdef CTR_NATIVE
	if ((songID == 0) && NativeCustomMusic_IsActive())
	{
		NativeCustomMusic_EnableFinalLap();
		return;
	}
#endif
	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if (((song->flags & 1) != 0) && (song->id == songID))
		{
			SongPool_ChangeTempo(song, p2);
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800290cc-0x800291a0
void CseqMusic_AdvHubSwap(u16 songId, struct SongSet *songSet, int songSetActiveBits)
{
	struct Song *song;
	int i;
#ifdef CTR_NATIVE
	int muteLevelSong = 0;
#endif

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader->numSongs <= songId)
	{
		return;
	}

#ifdef CTR_NATIVE
#if defined(CTR_CSEQ_LIBERATION)
	/* A native external CSEQ is a complete replacement theme, not a
	 * 20-layer Adventure Hub arrangement. Ignore retail layer swaps. */
	if (songId == 0 && NativeMusicRegistry_IsExternalCseqActive())
	{
		return;
	}
#endif

	if (songId == 0 && songSet != NULL && sdata->gGT != NULL)
	{
		NativeCustomMusic_TrySwapHub(sdata->gGT->levelID);
		muteLevelSong = NativeCustomMusic_ShouldMuteLevelCseq();
	}
#endif
	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if song is playing
		if (song->flags & 1)
		{
			if (song->id == songId)
			{
				SongPool_AdvHub2(song, songSet, songSetActiveBits);
#ifdef CTR_NATIVE
				/* CSEQ keeps its time even while its OGG variant is audible. */
				SongPool_Volume(song, muteLevelSong ? 0 : 190, 0, 1);
#endif
			}
		}
	}

	Smart_ExitCriticalSection();
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800291a0-0x80029258
void CseqMusic_Stop(u16 songID)
{
	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader->numSongs <= songID)
	{
		return;
	}

	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if (((song->flags & 1) != 0) && (song->id == songID))
		{
			SongPool_StopAllCseq(song);
		}
	}

	Smart_ExitCriticalSection();
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029258-0x800292e0
void CseqMusic_StopAll()
{
#ifdef CTR_NATIVE
	NativeCustomMusic_Stop();
#endif

	int i;
	struct Song *song;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}
	if (sdata->ptrCseqHeader == 0)
	{
		return;
	}

	Smart_EnterCriticalSection();

	for (i = 0; i < 2; i++)
	{
		song = &sdata->songPool[i];

		// if pool is taken
		if ((song->flags & 1) != 0)
		{
			SongPool_StopAllCseq(song);
		}
	}

	Smart_ExitCriticalSection();
}
