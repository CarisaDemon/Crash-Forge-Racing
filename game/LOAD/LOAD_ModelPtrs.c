#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031b50-0x80031bdc.
void LOAD_GlobalModelPtrs_MPK()
{
	struct GameTracker *gGT = sdata->gGT;

	for (int i = 0; i < LOAD_DRIVER_MODEL_EXTRA_COUNT; i++)
	{
		struct Model *m = data.driverModelExtras[i].model;

		if (m == NULL)
		{
			continue;
		}

		if (m->id == -1)
		{
			continue;
		}

#if defined(CTR_NATIVE)
		if ((m->id < 0) || (m->id >= (s32)len(gGT->modelPtr)))
		{
			continue;
		}
#endif

		gGT->modelPtr[m->id] = m;
	}

	if (sdata->PLYROBJECTLIST != 0)
	{
		LibraryOfModels_Store(gGT, -1, (struct Model **)sdata->PLYROBJECTLIST);
	}
#if defined(CTR_NATIVE)
	/* The retail 4P pack is still loaded, but its low-detail fourth
	 * kart/racer is overridden after storing the pack pointers. */
	if (Platform_GetCharacterDetailMode()==3)
	{
		struct Model *high=LOAD_NativeUltraFourthModel();
		if(high && high->id>=0 && high->id<(s32)len(gGT->modelPtr))
			gGT->modelPtr[high->id]=high;
		for(int ai=0;ai<4;ai++)
		{
			high=LOAD_NativeUltra2PAiModel(ai);
			if(high && high->id>=0 && high->id<(s32)len(gGT->modelPtr))
				gGT->modelPtr[high->id]=high;
		}
	}
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031bdc-0x80031c1c.
void LOAD_HubSwapPtrs(struct GameTracker *gGT)
{
	struct Level *oldLev1;
	struct VisMem *oldVisMem1;
	struct VisMem *oldVisMem2;

	// if no secondary lev exists, quit
	if (gGT->level2 == 0)
	{
		return;
	}

	oldLev1 = gGT->level1;
	oldVisMem1 = gGT->visMem1;
	oldVisMem2 = gGT->visMem2;

	gGT->level1 = gGT->level2;
	gGT->boolHubSwapped = 1;

	gGT->level2 = oldLev1;
	gGT->visMem1 = oldVisMem2;
	gGT->visMem2 = oldVisMem1;
}
