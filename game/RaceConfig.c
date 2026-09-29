#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80043c10-0x80043d24.
void RaceConfig_LoadGameOptions(void)
{
	if (sdata->boolHasLoadedOptions != 0)
	{
		return;
	}

	sdata->boolHasLoadedOptions = 1;
	s16 *volumes = &sdata->gameOptions.volFx;

	for (s32 i = 0; i < GAME_OPTIONS_VOLUME_COUNT; i++)
	{
#ifdef CTR_NATIVE
		/* Native preferences are available before the memory-card profile is
		   loaded, so keep them authoritative and mirror them into GameOptions.
		   This prevents the menu from restoring stale/default retail volumes. */
		int savedVol = Platform_GetSavedAudioVolume(i, (u8)volumes[i]);
		volumes[i] = (s16)savedVol;
		howl_VolumeSet(i, (u8)savedVol);
#else
		howl_VolumeSet(i, (u8)volumes[i]);
#endif
		memcpy(&data.rwd[0], &sdata->gameOptions.rwd[0], sizeof(data.rwd));
	}

#ifdef CTR_NATIVE
	{
		int vibrationMask = Platform_GetSavedVibrationMask();
		int stereo = Platform_GetSavedAudioMode((u8)sdata->gameOptions.audioMode & 1);
		sdata->gameOptions.gameMode1_vibrationFlags = vibrationMask;
		CTR_WriteU16LE(&sdata->gameOptions.audioMode, (u16)stereo);
		sdata->gGT->gameMode1 = (sdata->gGT->gameMode1 & ~GAME_MODE_VIBRATION_MASK) | vibrationMask;
		howl_ModeSet((u8)stereo);
	}
#else
	sdata->gGT->gameMode1 |= sdata->gameOptions.gameMode1_vibrationFlags & GAME_MODE_VIBRATION_MASK;
	howl_ModeSet((u8)sdata->gameOptions.audioMode & 1);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80043d24-0x80043e34.
void RaceConfig_SaveGameOptions(void)
{
	s16 *volumes = &sdata->gameOptions.volFx;

	for (s32 i = 0; i < GAME_OPTIONS_VOLUME_COUNT; i++)
	{
		volumes[i] = howl_VolumeGet(i) & 0xff;
#ifdef CTR_NATIVE
		Platform_SaveAudioVolume(i, volumes[i]);
#endif
	}

	memcpy(&sdata->gameOptions.rwd[0], &data.rwd[0], sizeof(data.rwd));
	sdata->gameOptions.gameMode1_vibrationFlags = sdata->gGT->gameMode1 & GAME_MODE_VIBRATION_MASK;
	CTR_WriteU16LE(&sdata->gameOptions.audioMode, (u16)(howl_ModeGet() != 0));
#ifdef CTR_NATIVE
	Platform_SaveVibrationMask(sdata->gameOptions.gameMode1_vibrationFlags);
	Platform_SaveAudioMode((u16)sdata->gameOptions.audioMode != 0);
#endif
}
