#include <common.h>

#if defined(CTR_NATIVE)
/* Retail only knows whether level2 is non-null. On PC, keep the requested hub
   explicit so a swap cannot accidentally consume a stale secondary level. */
static int s_nativeHubRequestedLevel = -1;
static CollStepFlags s_nativeHubPrevStepFlags[4];
#endif

// NOTE(aalhendi): ASM-audited NTSC-U 926 0x80032ffc-0x80033108.
// packID will always be 3-gGT->activeMempackIndex
void LOAD_Hub_ReadFile(struct BigHeader *bigfile, int levID, int packID)
{
	struct GameTracker *gGT = sdata->gGT;

#if defined(CTR_NATIVE)
	s_nativeHubRequestedLevel = levID;
#endif

	// if level is already loaded, quit
	if (gGT->levID_in_each_mempack[packID] == levID)
	{
		return;
	}

	sdata->modelMaskHints3D = 0;

	// Swap to pack of hub you're NOT on,
	// wipe the pack to reload the new hub
	MEMPACK_SwapPacks(packID);
	MEMPACK_ClearLowMem();

	sdata->PatchMem_Size = LOAD_HUB_PATCH_MEM_ACTIVE;
	gGT->level2 = 0;
	gGT->levID_in_each_mempack[packID] = levID;

#if defined(CTR_NATIVE)
	/* Native PC path: the full disc image is already resident in host RAM after
	   first boot, so do not preserve the retail asynchronous CD queue here.
	   Build the inactive hub mempack atomically from RAM. This guarantees that
	   level2 and levID_in_each_mempack describe the same destination before the
	   swap trigger can observe them. */
	{
		u32 fileSize = 0;
		void *levFile;
		void *ptrFile;
		struct LoadQueueSlot lqs = {0};
		int vramIndex = LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_VRAM);
		int levIndex = LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_LEV);
		int ptrIndex = LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_PTR);

		Platform_Log("[HubResident] sync prepare begin: lev=%d pack=%d active=%d\n",
		             levID, packID, gGT->activeMempackIndex);

		if (LOAD_VramFile(bigfile, vramIndex, NULL, &fileSize, -1) == NULL)
			goto nativeHubLoadFail;

		fileSize = 0;
		levFile = LOAD_ReadFile_ex(bigfile, LT_GETADDR, levIndex, NULL, &fileSize, NULL);
		if (levFile == NULL)
			goto nativeHubLoadFail;

		/* LEV files are DRAM containers, not raw Level structs. Retail routes
		   LT_GETADDR through LOAD_DramFileCallback(), which applies the LEV's
		   embedded pointer map, shrinks away that map, skips the 4-byte header,
		   and only then invokes LOAD_Callback_LEV(). Skipping this step leaves
		   fields such as LevInstDef->model as raw offsets and crashes on swap. */
		memset(&lqs, 0, sizeof(lqs));
		lqs.flags = LT_MEMPACK;
		lqs.type_UNUSED = LT_DRAM;
		lqs.subfileIndex = levIndex;
		lqs.ptrDestination = levFile;
		lqs.size_UNUSED = fileSize;
		lqs.callbackFuncPtr = LOAD_Callback_LEV;
		LOAD_DramFileCallback(&lqs);
		if (sdata->ptrLevelFile == NULL)
			goto nativeHubLoadFail;

		/* Do not dereference Level internals yet. Hub LEVs still receive an
		   external PTR map below, and several fields can legitimately remain
		   raw offsets until LOAD_HubCallback applies that map. */
		Platform_Log("[HubResident] LEV container ready: lev=%d level=%p; applying external PTR...\n",
		             levID, (void *)sdata->ptrLevelFile);

		fileSize = 0;
		ptrFile = LOAD_ReadFile_ex(bigfile, LT_SETADDR, ptrIndex, sdata->PatchMem_Ptr, &fileSize, NULL);
		if (ptrFile == NULL)
			goto nativeHubLoadFail;
		lqs.flags = LT_SETADDR;
		lqs.subfileIndex = ptrIndex;
		lqs.ptrDestination = ptrFile;
		lqs.size_UNUSED = fileSize;
		LOAD_HubCallback(&lqs);

		Platform_Log("[HubResident] sync prepare ready: lev=%d pack=%d level2=%p\n",
		             levID, packID, (void *)gGT->level2);
		return;

	nativeHubLoadFail:
		Platform_Log("[HubResident] sync prepare FAILED: lev=%d pack=%d\n", levID, packID);
		gGT->levID_in_each_mempack[packID] = -1;
		gGT->level2 = 0;
		s_nativeHubRequestedLevel = -1;
		MEMPACK_SwapPacks(gGT->activeMempackIndex);
		return;
	}
#else
	LOAD_AppendQueue(bigfile, LT_VRAM, LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_VRAM), NULL, NULL);
	LOAD_AppendQueue(bigfile, LT_GETADDR, LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_LEV), NULL, LOAD_Callback_LEV);
	LOAD_AppendQueue(bigfile, LT_SETADDR, LOAD_GetBigfileIndex(levID, LOAD_LEVEL_LOD_1P, LVI_PTR), sdata->PatchMem_Ptr, LOAD_HubCallback);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80033108-0x80033318.
void LOAD_Hub_SwapNow()
{
	struct Level *level1;
	struct VisMem *visMem;
	struct CameraDC *cDC;
	struct GameTracker *gGT = sdata->gGT;

	// stall until load is done
#if defined(CTR_NATIVE)
	{
		int inactivePack = LOAD_HUB_MEMPACK_PAIR_INDEX_SUM - gGT->activeMempackIndex;
		if (gGT->level2 == 0)
		{
			Platform_Log("[HubResident] swap deferred: level2 not ready, requested=%d\n",
			             s_nativeHubRequestedLevel);
			return;
		}
		if ((s_nativeHubRequestedLevel >= 0) &&
		    (gGT->levID_in_each_mempack[inactivePack] != s_nativeHubRequestedLevel))
		{
			Platform_Log("[HubResident] swap refused: requested=%d inactive=%d\n",
			             s_nativeHubRequestedLevel, gGT->levID_in_each_mempack[inactivePack]);
			return;
		}
	}
#else
	while (gGT->level2 == 0)
	{
		LOAD_NextQueuedFile();
		VSync(0);
	}
#endif

#if defined(CTR_NATIVE)
	Platform_Log("[HubResident] SWAP begin: requested=%d activeLevel=%d activePack=%d inactiveLevel=%d level1=%p level2=%p\n",
	             s_nativeHubRequestedLevel, gGT->levelID, gGT->activeMempackIndex,
	             gGT->levID_in_each_mempack[LOAD_HUB_MEMPACK_PAIR_INDEX_SUM - gGT->activeMempackIndex],
	             (void *)gGT->level1, (void *)gGT->level2);
#endif

	// Aug 5
	// ptrintf("gGT->level2 = 0x%08x\n",gGT->level2);
	// ptrintf("SWAPPING 1...\n");

	LevInstDef_RePack(gGT->level1->ptr_mesh_info, 1);

	// Aug 5
	// ptrintf("SWAPPING 2...\n");

	LOAD_HubSwapPtrs(gGT);

	// 0,1,2
	gGT->activeMempackIndex = LOAD_HUB_MEMPACK_PAIR_INDEX_SUM - gGT->activeMempackIndex;

	gGT->prevLEV = gGT->levelID;
	gGT->levelID = gGT->levID_in_each_mempack[gGT->activeMempackIndex];
#if defined(CTR_NATIVE)
	if ((s_nativeHubRequestedLevel >= 0) && (gGT->levelID != s_nativeHubRequestedLevel))
	{
		Platform_Log("[HubResident] refused stale swap: requested=%d active=%d\n",
		             s_nativeHubRequestedLevel, gGT->levelID);
	}
	s_nativeHubRequestedLevel = -1;
#endif

	Audio_AdvHub_SwapSong(gGT->levelID);

	// Aug 5
	// ptrintf("SWAPPING 3...\n");

	LibraryOfModels_Clear(gGT);

	/*
	In Aug 5
	if (sdata->PLYROBJECTLIST == 0)
	{
	    printf("ERROR: No PLYROBJECTLIST!\n");
	}
	*/

	if (sdata->PLYROBJECTLIST != 0)
	{
		LOAD_GlobalModelPtrs_MPK();
	}

	level1 = gGT->level1;

	/*
	In Aug 5
	if (level1 == 0)
	{
	    printf("ERROR: No LEVEL!\n");
	}
	*/

	if (level1 != 0)
	{
		LibraryOfModels_Store(gGT, level1->numModels, level1->ptrModelsPtrArray);

		INSTANCE_LevInitAll(level1->ptrInstDefs, level1->numInstances);

		LevInstDef_UnPack(level1->ptr_mesh_info);

		DecalGlobal_Store(gGT, level1->levTexLookup);
	}

	MEMPACK_SwapPacks(gGT->activeMempackIndex);
	MainInit_VisMem(gGT);

	cDC = &gGT->cameraDC[0];
	cDC->ptrQuadBlock = 0;
	cDC->visLeafSrc = 0;
	cDC->visFaceSrc = 0;
	cDC->visInstSrc = 0;
	cDC->visOVertSrc = 0;
	cDC->visSCVertSrc = 0;

	visMem = gGT->visMem1;
	visMem->visLeafSrc[0] = 0;
	visMem->visFaceSrc[0] = 0;
	visMem->visOVertSrc[0] = 0;
	visMem->visSCVertSrc[0] = 0;

	gGT->drivers[0]->underDriver = 0;

	gGT->framesInThisLEV = 0;
	gGT->msInThisLEV = 0;
#if defined(CTR_NATIVE)
	/* V151: MainFrame_ResetDB polls the hub at display rate, but collision
	   publishes stepFlagSet only on a driver simulation step. A successful
	   swap must consume that event; otherwise the extra render frames swap
	   the same two resident hubs back and forth using the old collision.
	   Clear only hub bits, for every driver, after the swap has succeeded.
	   Fresh collision events can still request an immediate genuine return. */
	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		struct Driver *driver = gGT->drivers[i];
		if (driver != NULL)
			driver->stepFlagSet &= (CollStepFlags)~(COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK |
			                                      COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK);
	}
	gGT->bool_AdvHub_NeedToSwapLEV = 0;
	Platform_Log("[HubResident] SWAP done: activeLevel=%d prev=%d activePack=%d level1=%p level2=%p\n",
	             gGT->levelID, gGT->prevLEV, gGT->activeMempackIndex,
	             (void *)gGT->level1, (void *)gGT->level2);
#endif
}

// NOTE(aalhendi): Native mirrors retail rdata 0x80011180 because CTR_NATIVE
// does not expose the retail rdata object.
#if defined(CTR_NATIVE)
static const int s_advHubConnectedLevID[LOAD_ADV_HUB_COUNT][LOAD_ADV_HUB_CONNECTION_COUNT] = {
    {N_SANITY_BEACH, THE_LOST_RUINS, -1},
    {GEM_STONE_VALLEY, GLACIER_PARK, -1},
    {GEM_STONE_VALLEY, GLACIER_PARK, -1},
    {N_SANITY_BEACH, THE_LOST_RUINS, CITADEL_CITY},
    {GLACIER_PARK, -1, -1},
};
#define LOAD_HUB_CONNECTED_LEV(hub, index) s_advHubConnectedLevID[(hub)][(index)]
#else
#define LOAD_HUB_CONNECTED_LEV(hub, index) rdata.MetaDataHubs[(hub)].connectedHub_LevID[(index)]
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80033318-0x80033474.
void LOAD_Hub_Main(struct BigHeader *bigfilePtr)
{
	struct GameTracker *gGT;

	// quit if already loading
	if (sdata->Loading.stage != LOAD_IDLE)
	{
		return;
	}

	gGT = sdata->gGT;

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		CollStepFlags stepFlagSet = gGT->drivers[i]->stepFlagSet;
		int nextLevelID = (stepFlagSet & COLL_STEP_TRIGGER_HUB_LEVEL_ID_MASK) >> COLL_STEP_TRIGGER_HUB_LEVEL_ID_SHIFT;
		int needSwapNow = (stepFlagSet & COLL_STEP_TRIGGER_HUB_SWAP_NOW_MASK) >> COLL_STEP_TRIGGER_HUB_SWAP_NOW_SHIFT;
#if defined(CTR_NATIVE)
		if (i < 4 && stepFlagSet != s_nativeHubPrevStepFlags[i])
		{
			Platform_Log("[HubResident] trigger change: drv=%d level=%d flags=0x%04x next=%d swap=%d requested=%d activePack=%d level2=%p\n",
			             i, gGT->levelID, (unsigned)stepFlagSet, nextLevelID, needSwapNow,
			             s_nativeHubRequestedLevel, gGT->activeMempackIndex, (void *)gGT->level2);
			s_nativeHubPrevStepFlags[i] = stepFlagSet;
		}
#endif

		// if new level does not need to load
		if (nextLevelID == LOAD_HUB_TRIGGER_NONE)
		{
			if ((needSwapNow != 0) || (gGT->bool_AdvHub_NeedToSwapLEV != 0))
			{
#if defined(CTR_NATIVE)
				int previousPack = gGT->activeMempackIndex;
				LOAD_Hub_SwapNow();
				/* Keep pending events on a deferred/refused swap. After success,
				   no other driver's old flags may be decoded in the new hub. */
				if (gGT->activeMempackIndex != previousPack)
					return;
#else
				gGT->bool_AdvHub_NeedToSwapLEV = 0;
				LOAD_Hub_SwapNow();
#endif
			}
		}

		// if new level needs to load
		else
		{
			// only in AdvHub, or else the game
			// crashes in 4P Nitro Court Life Limit
			u32 currLevelID = gGT->levelID - GEM_STONE_VALLEY;

			// ctr hubs are 0-4
			if (currLevelID >= LOAD_ADV_HUB_COUNT)
			{
				return;
			}

			LOAD_Hub_ReadFile(bigfilePtr, LOAD_HUB_CONNECTED_LEV(currLevelID, nextLevelID - LOAD_HUB_TRIGGER_ID_BIAS),
			                  LOAD_HUB_MEMPACK_PAIR_INDEX_SUM - gGT->activeMempackIndex);
		}
	}
}
