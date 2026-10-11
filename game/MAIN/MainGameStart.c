#include <common.h>
#if defined(CTR_NATIVE)
#include <platform/native_custom_tracks.h>
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003aee8-0x8003af84.
void MainGameStart_Initialize(struct GameTracker *gGT, b32 boolStopAudio)
{
	u32 gameModeFlag;

	// DotLights_AudioAndVideo wont execute
	// if (gGT & 0x20102000 == 0), but if it did execute,
	// traffic lights would instantly hit green in cutscene
	// and main menu, while starting from top-position (0xf00)
	// when spawning in the adventure arena

	// if you're not in cutscene and not in main menu
	if ((sdata->gGT->gameMode1 & (GAME_CUTSCENE | MAIN_MENU)) == 0)
	{
		// traffic light countdown
		gGT->trafficLightsTimer = 0xf00;

		// fly-in camera animation
		gameModeFlag = gGT->gameMode1 | START_OF_RACE;
	}
	// if you are:
	else
	{
		// disable traffic lights
		gGT->trafficLightsTimer = 0;

		// no fly-in camera
		gameModeFlag = gGT->gameMode1 & ~(START_OF_RACE);
	}

#if defined(CTR_NATIVE)
	/*
	 * Community LEV/VRM packs may omit retail fly-in camera paths.
	 * The same tested practice fast-start timer below starts normal
	 * gameplay without ever entering the missing camera animation.
	 * Keep this limited to an explicitly selected custom track.
	 */
	if (NativeCustomTracks_IsSelected())
	{
		gGT->trafficLightsTimer = (s32)0xfffffc40;
		gameModeFlag &= ~(START_OF_RACE);
		gGT->hudFlags |= HUD_FLAG_RACE_HUD | HUD_FLAG_RENDER_LESS;
		Platform_Log("[CustomTracks] Fast start: skipping optional fly-in and traffic lights\n");
	}
#endif

#if defined(CTR_PRACTICE_FASTSTART)
	/*
	 * Special practice fast-start: keep normal gameplay/HUD, but begin the
	 * loaded track already in racing state. No fly-in camera, title bars or
	 * traffic-light countdown.
	 */
	gGT->trafficLightsTimer = (s32)0xfffffc40;
	gameModeFlag &= ~(START_OF_RACE);
#endif

	// save new game mode
#ifdef CTR_NATIVE
	gGT->gameMode1 = (gameModeFlag & ~GAME_MODE_VIBRATION_MASK) | (u32)Platform_GetSavedVibrationMask();
#else
	gGT->gameMode1 = gameModeFlag;
#endif
	gGT->gameMode1 &= ~(END_OF_RACE); // Remove end-of-race flag

	// this never happens in normal gameplay
	if (boolStopAudio == 0)
	{
		Music_Stop();

		// stops menu sounds
		// keep backup,
		// keep music (no music to stop),
		// stop all fx
		howl_StopAudio(0, 0, 1);
	}

	VehBirth_TeleportAll(gGT, VEH_BIRTH_SPAWN_INIT_RACE_STATE);
}
