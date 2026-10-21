#include <common.h>

#if defined(CTR_NATIVE)
static DriverModelExtraSlot s_nativeUltraFourthHiModel;
/* Four AI characters are selected only inside LOAD_Robots2P; they
 * cannot use the fixed three-element retail player-extra array. */
static DriverModelExtraSlot s_nativeUltra2PAiHigh[4];
struct Model *LOAD_NativeUltraFourthModel(void)
{
	return s_nativeUltraFourthHiModel.model;
}
struct Model *LOAD_NativeUltra2PAiModel(int index)
{
	return (index>=0 && index<4)?s_nativeUltra2PAiHigh[index].model:NULL;
}
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif

#if defined(CTR_NATIVE)
static b32 LOAD_DuplicateCharactersAllowed(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 storyMask =
		ADVENTURE_MODE | ADVENTURE_BOSS | ADVENTURE_CUP | ADVENTURE_ARENA;

	return (gGT != NULL) && ((gGT->gameMode1 & storyMask) == 0);
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800326b4-0x80032700.
void LOAD_RunPtrMap(char *origin, int *patchArr, int numPtrs)
{
	int *ptrCurrOffset = patchArr;

	for (ptrCurrOffset = &patchArr[0]; ptrCurrOffset < &patchArr[numPtrs]; ptrCurrOffset++)
	{
		int offset = (*ptrCurrOffset >> 2) << 2;
		*(int *)&origin[offset] = *(int *)&origin[offset] + (int)origin;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		NativeCheckpoint_RegisterPointerSlot(&origin[offset]);
#endif
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032700-0x800327dc.
void LOAD_Robots2P(struct BigHeader *bigfile, int p1, int p2, void (*callback)(struct LoadQueueSlot *))
{
	int setIndex;
	u8 *robotSet;
	b32 boolFoundRepeat = false;

#if defined(CTR_NATIVE)
	if (LOAD_DuplicateCharactersAllowed())
	{
		/* Native non-story modes do not reserve character identities.
		 * Pick a deterministic AI pack without filtering P1/P2 out, so
		 * humans and bots are free to share the same racer. */
		int p1Pack = CharacterRegistry_GetDriverPackID(p1);
		int p2Pack = CharacterRegistry_GetDriverPackID(p2);
		setIndex = (p1Pack * 3 + p2Pack) % LOAD_2P_AI_SET_COUNT;
		robotSet = data.characterIDs_2P_AIs[setIndex];
	}
	else
#endif
	{
		// Retail/story behavior: choose a set that does not repeat P1/P2.
		for (setIndex = 0; setIndex < LOAD_2P_AI_SET_COUNT; setIndex++)
		{
			robotSet = data.characterIDs_2P_AIs[setIndex];

			boolFoundRepeat = false;
			for (int racerIndex = 0; racerIndex < LOAD_2P_AI_SET_RACER_COUNT; racerIndex++)
			{
				if ((robotSet[racerIndex] == p1) || (robotSet[racerIndex] == p2))
				{
					boolFoundRepeat = true;
					break;
				}
			}

			if (!boolFoundRepeat)
			{
				break;
			}
		}
	}

	if (setIndex >= LOAD_2P_AI_SET_COUNT)
	{
		return;
	}

	data.characterIDs[2] = robotSet[0];
	data.characterIDs[3] = robotSet[1];
	data.characterIDs[4] = robotSet[2];
	data.characterIDs[5] = robotSet[3];

#if defined(CTR_NATIVE)
	/* The pack below contains the four original low-detail bot models.
	 * ULTRA loads all four HI models BEFORE that pack, so its existing
	 * completion callback remains last. Keep Battle 2P lightweight:
	 * it has no AI kart threads and never needs these extra assets. */
	if(sdata->gGT->numPlyrCurrGame==2 &&
	   (sdata->gGT->gameMode1&BATTLE_MODE)==0 &&
	   Platform_GetCharacterDetailMode()==3)
	{
		for(int bot=0;bot<4;bot++)
			LOAD_AppendQueue(bigfile,LT_GETADDR,
				BI_RACERMODELHI+CharacterRegistry_GetDriverPackID(data.characterIDs[2+bot]),
				&s_nativeUltra2PAiHigh[bot].fileBase,
				LOAD_QUEUE_CALLBACK_SET_POINTER);
	}
#endif
	LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + setIndex, NULL, callback);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800327dc-0x8003282c.
void LOAD_Robots1P(int characterID)
{
	int newCharacterID = 0;

	data.characterIDs[0] = characterID;

	for (int i = 1; i < LOAD_CHARACTER_ID_COUNT; i++, newCharacterID++)
	{
#if defined(CTR_NATIVE)
		/* Outside story mode, do not skip the human's character. The
		 * automatic roster remains deterministic, but duplicate identities
		 * are legal just like duplicate human selections. */
		if (!LOAD_DuplicateCharactersAllowed() &&
			(newCharacterID == characterID))
#else
		if (newCharacterID == characterID)
#endif
		{
			newCharacterID++;
		}

		data.characterIDs[i] = newCharacterID;
	}
}

static void (*const LOAD_DriverMPK_SetPointer)(struct LoadQueueSlot *) = LOAD_QUEUE_CALLBACK_SET_POINTER;

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003282c-0x80032b50.
int LOAD_DriverMPK(struct BigHeader *bigfile, int levelLOD, void (*callback)(struct LoadQueueSlot *))
{
	int i;
	int gameMode1;

	struct GameTracker *gGT = sdata->gGT;
	gameMode1 = gGT->gameMode1;

#if defined(CTR_NATIVE)
	const int nativeUltraMulti = Platform_GetCharacterDetailMode()==3 &&
		(gGT->numPlyrCurrGame>=2 && gGT->numPlyrCurrGame<=4) &&
		((gameMode1 & (MAIN_MENU|ADVENTURE_ARENA|GAME_CUTSCENE))==0);
	s_nativeUltraFourthHiModel.fileBase=NULL;
	memset(s_nativeUltra2PAiHigh,0,sizeof(s_nativeUltra2PAiHigh));
#endif
	int lastFileIndexMPK;

	// 3P/4P
	if ((u32)(levelLOD - LOAD_LEVEL_LOD_3P) < LOAD_LEVEL_LOD_3P4P_COUNT)
	{
		for (i = 0; i < LOAD_DRIVER_MODEL_EXTRA_COUNT; i++)
		{
			/* PC ULTRA actually loads the HIGH model asset for each
			 * player instead of high-detail walking a LOW-only model. */
			LOAD_AppendQueue(bigfile, LT_GETADDR,
#if defined(CTR_NATIVE)
				(nativeUltraMulti?BI_RACERMODELHI:BI_RACERMODELLOW) +
#else
				BI_RACERMODELLOW +
#endif
				CharacterRegistry_GetDriverPackID(data.characterIDs[i]),
				&data.driverModelExtras[i].fileBase,LOAD_DriverMPK_SetPointer);
		}
#if defined(CTR_NATIVE)
		if(nativeUltraMulti)
			LOAD_AppendQueue(bigfile,LT_GETADDR,
				BI_RACERMODELHI+CharacterRegistry_GetDriverPackID(data.characterIDs[3]),
				&s_nativeUltraFourthHiModel.fileBase,LOAD_DriverMPK_SetPointer);
#endif

		// load 4P MPK of fourth player
		lastFileIndexMPK = BI_4PARCADEPACK + CharacterRegistry_GetDriverPackID(data.characterIDs[3]);
	}

	else if (levelLOD == LOAD_LEVEL_LOD_1P)
	{
		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) == TIME_TRIAL)
		{
			goto LoadHighAndPack;
		}

		if (
		    // adv/cutscene mpk when we just need text from MPK
		    ((gameMode1 & (GAME_CUTSCENE | ADVENTURE_ARENA)) != 0) ||

		    // credits
		    ((gGT->gameMode2 & CREDITS) != 0) ||

		    // adventure character select
		    (gGT->levelID == ADVENTURE_GARAGE))
		{
			lastFileIndexMPK = BI_ADVENTUREPACK + CharacterRegistry_GetDriverPackID(data.characterIDs[0]);
			goto QueueLastPack;
		}

		if ((gameMode1 & ADVENTURE_BOSS) != 0)
		{
			goto LoadHighAndPack;
		}

		if (
		    // If you are in Adventure cup
		    ((gameMode1 & ADVENTURE_CUP) != 0) &&

		    // purple gem cup
		    (gGT->cup.cupID == 4))
		{
			// high lod model
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + CharacterRegistry_GetDriverPackID(data.characterIDs[0]), &data.driverModelExtras[0].fileBase, LOAD_DriverMPK_SetPointer);

			// pack of four AIs with bosses
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + LOAD_PURPLE_GEM_CUP_AI_SET_INDEX, NULL, callback);

			data.characterIDs[1] = RIPPER_ROO;
			data.characterIDs[2] = PAPU_PAPU;
			data.characterIDs[3] = KOMODO_JOE;
			data.characterIDs[4] = PINSTRIPE;

			return sdata->ptrMPK;
		}

		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) != MAIN_MENU)
		{
			LOAD_Robots1P(data.characterIDs[0]);
		}

		// arcade mpk
		lastFileIndexMPK = BI_1PARCADEPACK + CharacterRegistry_GetDriverPackID(data.characterIDs[0]);
	}

	else if ((levelLOD == LOAD_LEVEL_LOD_RELIC) || ((gameMode1 & TIME_TRIAL) != 0))
	{
	LoadHighAndPack:
		// Do NOT switch the order to optimize Relic,
		// if HI+IDs[1] and PACK+IDs[0] is loaded,
		// then mask-grab breaks for all characters
		// on Hot Air Skyway (except Crash Bandicoot)

		// Load Player 1 [0]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + CharacterRegistry_GetDriverPackID(data.characterIDs[0]), &data.driverModelExtras[0].fileBase, LOAD_DriverMPK_SetPointer);

		// Load boss or ghost [1]
		lastFileIndexMPK = BI_TIMETRIALPACK + CharacterRegistry_GetDriverPackID(data.characterIDs[1]);
	}

	// else if (levelLOD == LOAD_LEVEL_LOD_2P)
	else
	{
		// med models
		for (i = 0; i < LOAD_MED_LOD_DRIVER_MODEL_EXTRA_COUNT; i++)
		{
			/* Match ULTRA in two-player races/battles as well. */
			LOAD_AppendQueue(bigfile, LT_GETADDR,
#if defined(CTR_NATIVE)
				(nativeUltraMulti?BI_RACERMODELHI:BI_RACERMODELMED) +
#else
				BI_RACERMODELMED +
#endif
				CharacterRegistry_GetDriverPackID(data.characterIDs[i]),
				&data.driverModelExtras[i].fileBase,LOAD_DriverMPK_SetPointer);
		}

		LOAD_Robots2P(bigfile, data.characterIDs[0], data.characterIDs[1], callback);
		return sdata->ptrMPK;
	}

QueueLastPack:
	LOAD_AppendQueue(bigfile, LT_GETADDR, lastFileIndexMPK, NULL, callback);
	return sdata->ptrMPK;
}

struct LngFile
{
	int numStrings;
	int offsetToPtrArr;
	char strings[1];
};

// param_1 - Pointer to "cd position of bigfile"
// param_2 - language index - 0 ja, 1 en, 2 en2, 3 fr, 4 de, 5 it, 6 es, 7 ne
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032b50-0x80032c24
void LOAD_LangFile(int bigfilePtr, int lang)
{
	struct LngFile *lngFile;
	u32 size;

	int i;
	int numStrings;
	char * P32 *strArray;

#if BUILD == EurRetail
	// This is to turn the screen black for a bit (optional)
	CTR_ErrorScreen(0, 0, 0);
	VSync(0);
#endif

	if (sdata->lngFile == 0)
	{
		sdata->lngFile = MEMPACK_AllocMem(sdata->langBufferSize /* "lang buffer" */);
	}

	lngFile = sdata->lngFile;

	lngFile = LOAD_ReadFile_ex((struct BigHeader *)bigfilePtr, LT_SETADDR, BI_LANGUAGEFILE + lang, lngFile, &size, NULL);
	if (lngFile == NULL)
	{
		return;
	}

	numStrings = lngFile->numStrings;
	strArray = (char * P32 *)((uintptr_t)lngFile + lngFile->offsetToPtrArr);

	sdata->numLngStrings = numStrings;
	sdata->lngStrings = strArray;

	for (i = 0; i < numStrings; i++)
	{
		strArray[i] = (char *)((u32)strArray[i] + (u32)lngFile);
	}
#if BUILD == EurRetail
	// set voicelines to new lang
	CDSYS_SetXAToLang(lang);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80032c24-0x80032d30.
int LOAD_GetBigfileIndex(u32 levelID, int lod, int fileIndexInGroup)
{
	if (levelID < NITRO_COURT)
	{
		return BI_ARCADETRACKS + levelID * LOAD_TRACK_FILES_PER_LOD_GROUP + sdata->levBigLodIndex[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - NITRO_COURT) < LOAD_BATTLE_TRACK_COUNT)
	{
		return BI_BATTLETRACKS + (levelID - NITRO_COURT) * LOAD_TRACK_FILES_PER_LOD_GROUP + sdata->levBigLodIndex[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - INTRO_RACE_TODAY) < LOAD_INTRO_CUTSCENE_COUNT)
	{
		return BI_CUTSCENES_INTRO + (levelID - INTRO_RACE_TODAY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup;
	}

	if ((u32)(levelID - OXIDE_ENDING) < LOAD_OUTRO_CUTSCENE_COUNT)
	{
		return BI_CUTSCENES_OUTRO + (levelID - OXIDE_ENDING) * LOAD_OUTRO_FILES_PER_LEVEL + fileIndexInGroup;
	}

	if (levelID == ADVENTURE_GARAGE)
	{
		return BI_MAINMENUFILE + LOAD_MAIN_MENU_GARAGE_FILE_OFFSET + fileIndexInGroup;
	}

	if (levelID == NAUGHTY_DOG_CRATE)
	{
		return BI_NDBOX + fileIndexInGroup;
	}

	if ((u32)(levelID - CREDITS_CRASH) < LOAD_CREDIT_LEVEL_COUNT)
	{
		return BI_CREDITS + (levelID - CREDITS_CRASH) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup;
	}

	if (levelID == MAIN_MENU_LEVEL)
	{
		return BI_MAINMENUFILE + fileIndexInGroup;
	}

	if (levelID == SCRAPBOOK)
	{
		return BI_SCRAPBOOK + fileIndexInGroup;
	}

	return BI_ADVENTUREHUB + (levelID - GEM_STONE_VALLEY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup;
}
