#include <common.h>
#if defined(CTR_NATIVE)
#include <NativeHangout.h>
#include <platform/native_custom_tracks.h>
#include <platform/native_music_selection.h>
#include <platform/native_menu_music_clock.h>
#include <platform/native_custom_music.h>
/* Keep sub-millisecond time from Unlimited loading frames instead of
 * rounding every host frame up to one millisecond. */
static double s_nativeLoadingTimeCarryMS = 0.0;
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_perf.h>
#include <platform/native_replay_scheduler.h>
#include <platform/native_savestate.h>
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
static struct NativePerfFrameInfo MainPerf_FrameInfo(struct GameTracker *gGT)
{
	struct NativePerfFrameInfo info;

	info.frameCounter = sdata->frameCounter;
	info.timer = gGT->timer;
	info.levelID = gGT->levelID;
	info.gameMode1 = gGT->gameMode1;
	info.loadingStage = sdata->Loading.stage;
	info.boolDemoMode = gGT->boolDemoMode;
	info.numPlyrCurrGame = gGT->numPlyrCurrGame;
	info.elapsedTimeMS = gGT->elapsedTimeMS;
	info.vsyncTillFlip = sdata->vsyncTillFlip;
	info.vSync_between_drawSync = gGT->vSync_between_drawSync;
	info.frameTimer_VsyncCallback = gGT->frameTimer_VsyncCallback;
	info.targetFPS = Platform_GetHighRefreshMode() ? Platform_GetHighRefreshTargetFPS() : 30;
	info.fullscreen = Platform_GetFullscreenMode();

	return info;
}

static int MainPerf_StressTestAllBotsFinished(struct GameTracker *gGT)
{
	int botCount = 0;

	if ((gGT == NULL) ||
	    (sdata->Loading.stage != LOAD_IDLE) ||
	    ((gGT->gameMode1 & (START_OF_RACE | LOADING | MAIN_MENU)) != 0))
	{
		return 0;
	}

	for (int driverID = 0; driverID < 8; ++driverID)
	{
		struct Driver *driver = gGT->drivers[driverID];

		if ((driver == NULL) || ((driver->actionsFlagSet & ACTION_BOT) == 0))
			continue;

		botCount++;

		if ((driver->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
			return 0;
	}

	return botCount == 8;
}

static struct NativeReplaySchedulerFrameInfo MainReplayScheduler_FrameInfo(struct GameTracker *gGT)
{
	struct NativeReplaySchedulerFrameInfo info;

	info.frameTimer = gGT->frameTimer_VsyncCallback;
	info.frameCounter = sdata->frameCounter;
	info.timer = gGT->timer;
	info.framesInThisLEV = gGT->framesInThisLEV;
	info.elapsedTimeMS = gGT->elapsedTimeMS;
	info.msInThisLEV = gGT->msInThisLEV;
	info.elapsedEventTime = gGT->elapsedEventTime;
	info.mainGameState = sdata->mainGameState;
	info.loadingStage = sdata->Loading.stage;
	info.levelID = gGT->levelID;
	info.mixRandomNumber = (u32)sdata->randomNumber;
	info.audioRNG = sdata->audioRNG;
	info.deadcoed0 = (u32)gGT->deadcoed_struct.state0;
	info.deadcoed1 = (u32)gGT->deadcoed_struct.state1;
	info.advRng0 = (u32)sdata->const_0x30215400;
	info.advRng1 = (u32)sdata->const_0x493583fe;

	return info;
}
#endif

// NOTE(aalhendi): PSX path ASM-verified NTSC-U 926 0x8003c58c-0x8003cf7c.
#ifdef CTR_NATIVE
u32 CTR_Main(void)
#else
u32 main(void)
#endif
{
	u32 AddBitsConfig0;
	u32 RemBitsConfig0;
	u32 AddBitsConfig8;
	u32 RemBitsConfig8;
	int iVar8;
	u32 gameMode1;
	u32 gameMode2;
	u32 uVar12;

	struct GameTracker *gGT;
	gGT = sdata->gGT;

	struct GamepadSystem *gGS;
	gGS = sdata->gGamepads;

	// NOTE(aalhendi): Retail main calls __main before the state loop. Native has
	// no linked __main body, so keep this as a CTR_NATIVE-only divergence.
#ifndef CTR_NATIVE
	__main();
#endif

	do
	{
#ifndef CTR_NATIVE
		// wont happen under normal conditions
		if (sdata->mainGameState == 5)
		{
			MainKillGame_StopCTR();
			return 0;
		}
#endif

		LOAD_NextQueuedFile();
#if defined(CTR_NATIVE)
		if (NativeHangout_ConsumeExitRequest())
		{
			NativeHangout_SetActive(0);
			gGT->numPlyrNextGame = 1;
			gGT->boolDemoMode = 0;
			sdata->mainMenuState = MAIN_MENU_TITLE;
			MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
			Platform_Log("[Hangout] clean exit requested -> main menu\n");
		}
		else if (NativeHangout_ConsumeLaunchRequest())
		{
			/* A hidden shortcut can launch Hangout while the title menu is still
			 * alive. Kill the title scene and menu stack BEFORE clearing
			 * MAIN_MENU, otherwise MM_Title_KillThread() refuses to destroy the
			 * title objects and the old menu can survive over the hub. */
			if ((gGT->gameMode1 & MAIN_MENU) != 0)
			{
				if (sdata->ptrActiveMenu != NULL)
					RECTMENU_Hide(sdata->ptrActiveMenu);

				MM_Title_KillThread();
			}

			sdata->ptrActiveMenu = NULL;
			sdata->ptrDesiredMenu = NULL;
			sdata->activeSubMenu = NULL;
			RECTMENU_ClearInput();

			/* Snapshot Adventure progress before changing the temporary hub state.
			 * Hangout restores it on exit so the social sandbox cannot modify a
			 * real Adventure profile or memcard progression. */
			NativeHangout_SetActive(1);

			gGT->gameMode1 &= ~(BATTLE_MODE | TIME_TRIAL | ARCADE_MODE |
			                   ADVENTURE_CUP | RELIC_RACE | CRYSTAL_CHALLENGE |
			                   END_OF_RACE | GAME_CUTSCENE | MAIN_MENU);
			gGT->gameMode1 |= ADVENTURE_MODE;
			gGT->gameMode2 &= ~(CUP_ANY_KIND);
			gGT->numPlyrNextGame = 1;
			gGT->podiumRewardID = NOFUNC;
			gGT->boolDemoMode = 0;
			sdata->advProgress.HubLevYouSavedOn = N_SANITY_BEACH;
			MainRaceTrack_RequestLoad(N_SANITY_BEACH);
			Platform_Log("[Hangout] loading hidden N. Sanity Beach social session\n");
		}
#endif
		// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003c5d0-0x8003c5dc for per-frame XA pause handling.
		CDSYS_XAPauseAtEnd();

		switch (sdata->mainGameState)
		{
		// Initialize Game (happens once)
		case 0:
			StateZero();
			break;

		// Happens on first frame that loading ends
		case 1:

			ElimBG_Deactivate(gGT);

			MainStats_RestartRaceCountLoss();
			// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003c9f8-0x8003ca04 for load-complete voiceline reset.
			Voiceline_ClearTimeStamp();

			// Disable End-Of-Race menu
			gGT->gameMode1 &= ~END_OF_RACE;

			if (gGT->levelID == MAIN_MENU_LEVEL)
			{
				if (RaceFlag_IsFullyOffScreen())
				{
					RaceFlag_SetFullyOnScreen();
				}
			}

			else
			{
				if (RaceFlag_IsFullyOnScreen())
				{
					RaceFlag_BeginTransition(2);
				}
			}

			DropRain_Reset(gGT);
			GAMEPROG_GetPtrHighScoreTrack();
			MainInit_FinalizeInit(gGT);
			GAMEPAD_GetNumConnected(gGS);

			sdata->boolSoundPaused = 0;
			// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003caa4-0x8003cab4 for load-complete engine audio init.
			VehBirth_EngineAudio_AllPlayers();

			// 9 = intro cutscene
			// 10 = traffic lights
			// 11 = racing

#if defined(CTR_PRACTICE_FASTSTART)
			/* Practice fast-start: skip race intro XA and traffic-light audio state. */
			Audio_SetState_Safe(AUDIO_RACING);
#else
			// Arcade-Style track starts with intro cutscene
			uVar12 = 9;

			if (
			    // If Level ID is less than 18, it's one of the race tracks
			    (gGT->levelID < NITRO_COURT) || (
			                                        // Battle-Style track starts with traffic lights
			                                        uVar12 = 10,
			                                        // Level ID >= 18 and < 23
			                                        // Battle tracks
			                                        gGT->levelID - NITRO_COURT < 7))
			{
				Audio_SetState_Safe(uVar12);
			}
#endif
#ifdef CTR_NATIVE
			if (gGT->levelID == MAIN_MENU_LEVEL)
			{
				/* The title's bank, VLC, driver models and menu resources are
				 * now ready. Only now schedule note zero and start playback.
				 * This removes the drift in the first menu music loop. */
				CseqMusic_StopAll();
				Music_End();
				NativeCustomMusic_TryStartLevel(MAIN_MENU_LEVEL);
				Audio_SetState(AUDIO_GARAGE_ENTRY);
				sdata->audioState = AUDIO_GARAGE_ENTRY;
				Platform_Log("[MENU MUSIC] title theme started AFTER level initialization\n");
			}
			/* Only menus use the independent HOWL clock. Race physics,
			 * TAS, VBlank and the game renderer remain on their original
			 * main-thread timing model. */
			if ((gGT->gameMode1 & MAIN_MENU) != 0 &&
			    sdata->Loading.stage == LOAD_IDLE)
				NativeMenuMusicClock_Start();
#endif
			sdata->mainGameState = 3;
			gGT->clockEffectEnabled &= 0xfffe;
			break;

		// Reset stage, reset music
		case 2:
			Audio_SetState_Safe(1);
			MEMPACK_PopState();

			// ignore threads, because we PopState,
			// so the threadpool will reset anyway
			LevInstDef_RePack(gGT->level1->ptr_mesh_info, 0);

			sdata->mainGameState = 1;
			break;

		// Main Gameplay Update
		// Makes up all normal interaction with the game
		case 3:
#if defined(CTR_NATIVE)
			if (sdata->Loading.stage == LOAD_IDLE)
				s_nativeLoadingTimeCarryMS = 0.0;
#endif

			// if loading, or gameplay interrupted
			if (sdata->Loading.stage != LOAD_IDLE)
			{
				if (RaceFlag_IsFullyOnScreen() || (gGT->levelID == NAUGHTY_DOG_CRATE) || (sdata->pause_state != 0))
				{
					gGT->gameMode1 |= LOADING;
				}

				iVar8 = sdata->Loading.stage;

				// Loading used to force a 32 ms content step every host frame.
				// At 200 Hz that made the checkered flag animate ~6.4x too fast.
#if defined(CTR_NATIVE)
				if (Platform_GetHighRefreshMode())
				{
					int loadingFPS = Platform_GetHighRefreshTargetFPS();
					int loadingElapsed;
					if (loadingFPS > 0)
					{
						s_nativeLoadingTimeCarryMS = 0.0;
						loadingElapsed = (1000 + loadingFPS / 2) / loadingFPS;
					}
					else
					{
						/* At 2000+ FPS a frame lasts less than 1 ms. Rounding
						 * every frame up to 1 ms speeds up the loading animation.
						 * Carry the fractional milliseconds into later frames. */
						double hostMs = Platform_GetLastFrameTimeMS();
						double totalMs;
						if (hostMs < 0.0) hostMs = 0.0;
						if (hostMs > 64.0) hostMs = 64.0;
						totalMs = hostMs + s_nativeLoadingTimeCarryMS;
						loadingElapsed = (int)totalMs;
						s_nativeLoadingTimeCarryMS = totalMs - (double)loadingElapsed;
					}
					/* Zero is valid: no whole millisecond passed this host frame. */
					gGT->elapsedTimeMS = loadingElapsed;
				}
				else
#endif
				{
					gGT->elapsedTimeMS = 32;
				}

				// if loading VLC
				if (iVar8 == LOAD_VLC)
				{
					// if VLC is not loaded, quit
					// we know when it's done from a load callback
					if (sdata->bool_IsLoaded_VlcTable != 1)
					{
						break;
					}

					// if == 1, finish the loading
					goto FinishLoading;
				}

				// if restarting race
				if (iVar8 == LOAD_RESTART)
				{
					if (RaceFlag_IsFullyOnScreen())
					{
						// reinitialize world,
						// does not reinitialize pools
						sdata->mainGameState = 2;

						// no loading, and no interruption
						sdata->Loading.stage = LOAD_IDLE;

						// Turn off the "Loading..." flag
						gGT->gameMode1 &= ~LOADING;
						break;
					}

					// if not fully on-screen, do not BREAK,
					// keep rendering the scene
				}

				// if waiting for checkered flag to cover screen,
				// right before loading the next requested level
				else if (iVar8 == -4)
				{
					RemBitsConfig8 = sdata->Loading.OnBegin.RemBitsConfig8;
					AddBitsConfig8 = sdata->Loading.OnBegin.AddBitsConfig8;
					RemBitsConfig0 = sdata->Loading.OnBegin.RemBitsConfig0;
					AddBitsConfig0 = sdata->Loading.OnBegin.AddBitsConfig0;

					if (RaceFlag_IsFullyOnScreen())
					{
						sdata->Loading.OnBegin.AddBitsConfig0 = 0;
						sdata->Loading.OnBegin.RemBitsConfig0 = 0;
						sdata->Loading.OnBegin.AddBitsConfig8 = 0;
						sdata->Loading.OnBegin.RemBitsConfig8 = 0;

						gameMode2 = gGT->gameMode2;

						gGT->hudFlags &= HUD_FLAG_CLEAR_INTRO_RACE_TITLE_BARS_MASK;

						gameMode1 = gGT->gameMode1;
						gGT->gameMode2 = gameMode2 | AddBitsConfig8;
						gGT->gameMode1 = gameMode1 | AddBitsConfig0;
						gGT->gameMode1 = (gameMode1 | AddBitsConfig0) & ~RemBitsConfig0;
						gGT->gameMode2 = (gameMode2 | AddBitsConfig8) & ~RemBitsConfig8;

						MainRaceTrack_StartLoad(sdata->Loading.Lev_ID_To_Load);
					}

					else if (RaceFlag_IsFullyOffScreen())
					{
						RaceFlag_BeginTransition(1);
					}

					// do not BREAK,
					// keep rendering the scene
				}

				// if something is being loaded
				else
				{
					sdata->Loading.stage = LOAD_TenStages(gGT, iVar8, sdata->ptrBigfile1);

					// If just finished loading stage 9
					if (sdata->Loading.stage == LOAD_FINISHED)
					{
						if ((gGT->levelID == MAIN_MENU_LEVEL) || (gGT->levelID == SCRAPBOOK))
						{
							MainLoadVLC();

							// start loading VLC (scroll up to iVar8 == LOAD_VLC)
							sdata->Loading.stage = LOAD_VLC;
							break;
						}

					FinishLoading:
						// loading is finished,
						// initialize world and pools,
						// remove LOADING... flag from gGT
						sdata->Loading.stage = LOAD_IDLE;
						sdata->mainGameState = 1;
						gGT->gameMode1 &= ~LOADING;
						break;
					}

					// else, do not BREAK,
					// keep rendering the scene
					// which is the checkered flag
				}
			}

			// =========== Main Game Loop ======================

			{
				int trafficElapsed = gGT->elapsedTimeMS;
#if defined(CTR_NATIVE)
				if (Platform_GetHighRefreshMode() &&
				    LOAD_IsOpen_RacingOrBattle() &&
				    ((gGT->gameMode1 & (MAIN_MENU | ADVENTURE_ARENA | GAME_CUTSCENE)) == 0))
				{
					// MainMain runs before MainFrame_GameLogic samples this host frame.
					// Use the completed retail tick from the previous frame instead of
					// stale gGT->elapsedTimeMS, which otherwise double-counts time.
					trafficElapsed = 0; /* advanced once inside each coherent world tick */
				}
#endif
				if (((-960 < gGT->trafficLightsTimer) &&
				     ((gGT->gameMode1 & (START_OF_RACE | PAUSE_ALL)) == 0)) &&
				    ((iVar8 = gGT->trafficLightsTimer - trafficElapsed,
				      gGT->trafficLightsTimer = iVar8,
				      iVar8 < -960)))
				{
					gGT->trafficLightsTimer = 0xfffffc40;
				}
			}

			// Retail frameCounter is a 30 Hz content clock, not the number of host
			// updates. Native high-refresh advances it after the real-time clock is
			// sampled in MainFrame_GameLogic.
#if !defined(CTR_NATIVE)
			sdata->frameCounter++;
#endif

#ifdef CTR_NATIVE
			// Begin native per-frame sidecars before any game logic/render work.
			// Some systems project/build primitives during logic, so starting the
			// subpixel generation only immediately before RenderFrame discarded
			// valid GTE fractional coordinates from earlier in this same frame.
			Platform_BeginFrame();
#endif

			// Process all gamepad input
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			{
				struct NativeReplaySchedulerFrameInfo replayFrameInfo = MainReplayScheduler_FrameInfo(gGT);

				if (NativeReplayScheduler_BeginFrame(&replayFrameInfo) != 0)
				{
					return 0;
				}
				NativeSaveState_BeginFrame();
				gGT = sdata->gGT;
				gGS = sdata->gGamepads;
			}
			{
				struct NativePerfFrameInfo perfFrameInfo = MainPerf_FrameInfo(gGT);

				NativePerf_BeginFrame(&perfFrameInfo);
			}
#endif
			GAMEPAD_ProcessAnyoneVars(gGS);

			// Start new frame (ClearOTagR)
			MainFrame_ResetDB(gGT);

			if (
			    // If you're in Demo Mode
			    (gGT->boolDemoMode != 0) &&

			    (
			        // Turn off HUD
			        gGT->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK,
			        // if game is not loading
			        sdata->Loading.stage == LOAD_IDLE))
			{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
				if (NativePerf_IsStressTestActive())
				{
					/* Measure only active racing, not loading or the start lights. */
					if ((gGT->gameMode1 & START_OF_RACE) == 0)
						NativePerf_StressTestBeginRace();

					const int limitReached = NativePerf_StressTestReachedFrameLimit();
					if (limitReached || MainPerf_StressTestAllBotsFinished(gGT))
					{
						Platform_Log("[PERF TEST] 8-bot run done (frameLimit=%d)\n", limitReached);
						NativePerf_SetStressTestActive(0);
						{
							/* One-shot clean process exit for no-focus unattended profiling.
							 * Never affect normal stress tests without an explicit marker. */
							FILE *exit = fopen("debug/perf.exit_after_stress", "rb");
							if (exit != NULL)
							{
								fclose(exit);
								remove("debug/perf.exit_after_stress");
								Platform_Log("[PERF TEST] completed, requesting clean exit\n");
#if defined(CTR_PGO_GENERATE)
								{
									extern void __gcov_dump(void);
									Platform_Log("[PERF TEST] saving GCC PGO counters\n");
									__gcov_dump();
								}
#endif
								Platform_RequestExit();
							}
						}
#if defined(CTR_PGO_TRAINING)
						{
							FILE *pgoExit =
								fopen("debug/pgo.exit_after_stress", "rb");
							if (pgoExit != NULL)
							{
								fclose(pgoExit);
								remove("debug/pgo.exit_after_stress");
								Platform_Log("[PERF TEST] PGO training complete; clean exit\n");
								Platform_RequestExit();
							}
						}
#endif
						gGT->boolDemoMode = 0;
						gGT->numPlyrNextGame = 1;
						sdata->mainMenuState = MAIN_MENU_TITLE;
						MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
					}
				}
				else
#endif
				{
					// Retail demo behavior: timed preview, any button exits.
#if defined(CTR_NATIVE)
					{
						int value = gGT->demoCountdownTimer - Platform_GetLegacy30HzTicks();
						gGT->demoCountdownTimer = (value > 0) ? value : 0;
					}
#else
					gGT->demoCountdownTimer--;
#endif

					if (gGT->demoCountdownTimer < 1)
					{
						gGT->boolDemoMode = 0;
						gGT->numPlyrNextGame = 1;
						sdata->mainMenuState = MAIN_MENU_TITLE;

					LAB_8003ce08:
						MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
					}
					else if (gGS->anyoneHeldCurr != 0)
					{
						gGT->boolDemoMode = 0;
						goto LAB_8003ce08;
					}

					if (gGT->numPlyrCurrGame == 1)
						uVar12 = 0x23;
					else
						uVar12 = 100;

					DecalFont_DrawMultiLine(
						sdata->lngStrings[LNG_DEMO_MODE_PRESS_ANY_BUTTON_TO_EXIT],
						0x100, uVar12, 0x200, 2, 0xffff8000);
				}
			}

			if ((gGT->gameMode1 & LOADING) == 0)
			{
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
				NativePerf_BeginScope(NATIVE_PERF_BUCKET_GAME_LOGIC);
#endif
				MainFrame_GameLogic(gGT, gGS);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
				NativePerf_EndScope(NATIVE_PERF_BUCKET_GAME_LOGIC);
#endif
			}
#if defined(CTR_NATIVE)
			NativeHangout_Update(gGT);
			if (!NativeWorld_IsActive()) sdata->frameCounter += Platform_GetLegacy30HzTicks();
#endif

			// If you are in demo mode
			if (gGT->boolDemoMode != '\0')
			{
				// Turn off HUD
				gGT->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;
			}

			// reset vsync calls between drawsync
			gGT->vSync_between_drawSync = 0;


#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDER_FRAME);
#endif
#if defined(CTR_NATIVE)
            NativeWorld_BeginRender(gGT);
#endif
            MainFrame_RenderFrame(gGT, gGS);
#if defined(CTR_NATIVE)
            NativeWorld_EndRender(gGT);
#endif
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDER_FRAME);
#endif
#ifdef CTR_NATIVE
			Platform_EndFrame();
#endif


			// if mask is talking in Adventure Hub
#if defined(CTR_NATIVE)
			if (NativeHangout_IsActive())
			{
				/* NativeHangout_Update() already clears the state; keep this
				 * guard here too so the retail updater can never run in Hangout. */
				sdata->boolDraw3D_AdvMask = 0;
				sdata->AkuAkuHintState = 0;
			}
			else
#endif
			if (sdata->boolDraw3D_AdvMask != 0)
			{
				AH_MaskHint_Update();
			}
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
			{
				struct NativeReplaySchedulerFrameInfo replayFrameInfo = MainReplayScheduler_FrameInfo(gGT);

				if (NativeReplayScheduler_EndFrame(&replayFrameInfo) != 0)
				{
					return 0;
				}
			}
			{
				struct NativePerfFrameInfo perfFrameInfo = MainPerf_FrameInfo(gGT);

				NativePerf_EndFrame(&perfFrameInfo);
			}
#endif
			break;

#ifndef CTR_NATIVE
		// In theory, this is left over from the demos,
		// which would "timeout" and restart after sitting idle
		case 4:

			// erase all data past the
			// last 3 bookmarks, if there
			// that many exist
			MEMPACK_PopState();
			MEMPACK_PopState();
			MEMPACK_PopState();

			CTR_ErrorScreen(0, 0, 0);
			Music_Stop();

			// clear backup, destroy music, destroy all fx
			howl_StopAudio(1, 1, 1);
			Bank_DestroyAll();
			howl_Disable();

			GAMEPAD_SetMainMode();

			// Set vsync to 2 FPS
			VSync(30);

			// reboot game
			sdata->mainGameState = 0;
#endif
		}
	} while (true);
}

// NOTE(aalhendi): Source split of retail main's state-0 body
// 0x8003c614-0x8003c984.
// By separating this, it can be overwritten dynamically (oxide fix).
void StateZero()
{
	u16 *clockEffect;
	u32 vramSize;

	struct GameTracker *gGT;
	gGT = sdata->gGT;

	struct GamepadSystem *gGS;
	gGS = sdata->gGamepads;

// already zero, part of BSS
#if 0
	memset(gGT, 0, sizeof(struct GameTracker));
#endif

	// Set Video Mode to NTSC
	SetVideoMode(0);
	ResetCallback();

#define MEMPACK_SIZE 0x200000 // 2mb

	MEMPACK_Init(MEMPACK_SIZE);
	LOAD_InitCD();
	RaceFlag_SetFullyOffScreen();

	ResetGraph(0);
	SetGraphDebug(0);

	MainInit_VRAMClear();

	SetDispMask(1);

	SetDefDrawEnv(&gGT->db[0].drawEnv, 0, 0, 0x200, 0xd8);
	SetDefDrawEnv(&gGT->db[1].drawEnv, 0, 0x128, 0x200, 0xd8);
	SetDefDispEnv(&gGT->db[0].dispEnv, 0, 0x128, 0x200, 0xd8);
	SetDefDispEnv(&gGT->db[1].dispEnv, 0, 0, 0x200, 0xd8);

	gGT->db[0].dispEnv.screen.x = 0;
	gGT->db[0].dispEnv.screen.y = 0xc;
	gGT->db[0].dispEnv.screen.w = 0x100;
	gGT->db[0].dispEnv.screen.h = 0xd8;

	gGT->db[1].dispEnv.screen.x = 0;
	gGT->db[1].dispEnv.screen.y = 0xc;
	gGT->db[1].dispEnv.screen.w = 0x100;
	gGT->db[1].dispEnv.screen.h = 0xd8;

	gGT->db[0].drawEnv.isbg = 1;
	gGT->db[0].drawEnv.r0 = 0;
	gGT->db[0].drawEnv.g0 = 0;
	gGT->db[0].drawEnv.b0 = 0;

	gGT->db[1].drawEnv.isbg = 1;
	gGT->db[1].drawEnv.r0 = 0;
	gGT->db[1].drawEnv.g0 = 0;
	gGT->db[1].drawEnv.b0 = 0;

	// default number of lives in battle
	// this is left over from prototypes, useless in retail
	gGT->battleLifeLimit = 5;

	// 30 second counter?
	gGT->constVal_9000 = 9000;

	// set lap count to 3
	gGT->numLaps = 3;

	gGT->battleSetup.enabledWeapons |= BATTLE_DEFAULT_WEAPON_FLAGS;
	gGT->numPlyrCurrGame = 1;
	gGT->numPlyrNextGame = 1;
	*(u32 *)&gGT->battleSetup.teamOfEachPlayer = 0x3020100;

	// traffic light countdown timer, set to negative one second
	gGT->trafficLightsTimer = 0xfffffc40;

	Timer_Init();
	DrawSyncCallback(&MainDrawCb_DrawSync);

	MEMCARD_InitCard();
	VSync(0);
	GAMEPAD_Init(gGS);
	VSync(0);
	GAMEPAD_GetNumConnected(gGS);

#ifdef CTR_NATIVE
#define BIGPATH "\\BIGFILE.BIG;1"
#else
#define BIGPATH rdata.s_PathTo_Bigfile
#endif

	// Get CD Position fo BIGFILE
	sdata->ptrBigfile1 = LOAD_ReadDirectory(BIGPATH);

// Defrag to save heap space,
// required because MEMPACK_Init moves heap
#if 0
	// NOTE(aalhendi): Retail main does not rewrite BIGFILE overlay sizes here.
	extern char RB_NewEndFile[4];

	// Dont load full overlay file, cut off the end
	struct BigEntry *firstEntry = BIG_GETENTRY(sdata->ptrBigfile1);
	firstEntry[231].size = 28 * 0x800;
	// firstEntry[231].size = (u32)RB_NewEndFile - (u32)OVR_Region3;
	// printf("Size: %08x\n", firstEntry[231].size);

	// Cut off Region1 overlays at 2 sectors (not 3),
	// This protects Region2 RAM so it is not overwritten
	// during Region1 disc streaming, saves loading time
	for (int i = 221; i <= 225; i++)
		firstEntry[i].size = 2 * 0x800;
#endif

	// English=1
	// PAL SCES02105 calls it multiple times
	LOAD_LangFile((int)sdata->ptrBigfile1, 1);
	GAMEPROG_NewGame_OnBoot();
	gGT->overlayIndex_null_notUsed = 0;

#if defined(CTR_PRACTICE_FASTSTART)
	/*
	 * Stored special practice mode: direct-load Crash Cove with the normal
	 * 1P Arcade gameplay/HUD, but without front-end intro or race countdown.
	 * This is intentionally OFF in normal builds and kept separate from CSEQ.
	 */
	gGT->levelID = CRASH_COVE;
	gGT->gameMode1 = (gGT->gameMode1 & GAME_MODE_VIBRATION_MASK) | ARCADE_MODE;
	gGT->gameMode2 = 0;
	gGT->boolDemoMode = 0;
	gGT->numPlyrCurrGame = 1;
	gGT->numPlyrNextGame = 1;
	Platform_Log("[PRACTICE] fast-start target: level_03 CRASH_COVE (HUD enabled)\n");
#elif defined(CTR_NATIVE)
	/* Native Crash Forge Racing boot: go straight to the normal title
	 * level instead of entering the licensed studio-logo cutscene.
	 * Keep menu/Adventure/race routing identical after the title loads. */
	gGT->levelID = MAIN_MENU_LEVEL;
	sdata->mainMenuState = MAIN_MENU_TITLE;
	Platform_Log("[BOOT] direct Crash Forge title; Sony/ND startup intros disabled\n");
#else
	gGT->levelID = NAUGHTY_DOG_CRATE;
	// gGT->levelID = OXIDE_TRUE_ENDING;
#endif

	InitGeom();
	SetGeomOffset(0x100, 0x78); // width/2, height/2
	SetGeomScreen(0x140);       // "distance" to screen, alters FOV

	// NOTE(aalhendi): Retail calls 0x8006ae74 here; keep the verified depth
	// scale setup on every target.
	RenderBucket_InitDepthGTE();
	// NOTE(aalhendi): Retail bakes these authored matrix tables in place.
	Vector_BakeMatrixTable();

	gGT->swapchainIndex = 0;
	gGT->backBuffer = &gGT->db[0];

	gGT->overlayIndex_EndOfRace = 0xff;
	gGT->overlayIndex_LOD = OVERLAY_INDEX_NONE;
	gGT->overlayIndex_Threads = OVERLAY_INDEX_NONE;

	PutDispEnv(&gGT->db[1].dispEnv);
	PutDrawEnv(&gGT->db[1].drawEnv);
	DrawSync(0);

	/* Retail starts with the Sony/SCEA bitmap. The native port starts
	 * with a blank loading frame and skips this copyrighted studio card. */
#if !defined(CTR_NATIVE) && !defined(CTR_PRACTICE_FASTSTART)
	LOAD_VramFile(sdata->ptrBigfile1, 0x1fd, NULL, &vramSize, -1);
#endif
	MainInit_VRAMDisplay();

	/* Full LEV/VRM validation and soundtrack-pack directory scanning belong
	 * to startup, before the HOWL sequencer/audio stream becomes active.
	 * Never repeat these synchronous file reads on ordinary menu transitions. */
#ifdef CTR_NATIVE
	NativeCustomTracks_EnsureScanned();
	NativeMusicSelection_EnsureReady();
#endif

	// \SOUNDS\KART.HWL;1
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003c8e0-0x8003c928 for startup HOWL/music/XA setup.
	howl_InitGlobals(data.kartHwlPath);

	VSyncCallback(MainDrawCb_Vsync);

#if defined(CTR_NATIVE)
	/* No Sony voice-over XA, Naughty Dog song or splash-screen loop.
	 * Normal title soundtrack initializes through LOAD_TenStages
	 * when the main-menu bank is ready. */
	CseqMusic_StopAll();
#elif !defined(CTR_PRACTICE_FASTSTART)
	Music_SetIntro();
	CseqMusic_StopAll();
	CseqMusic_Start(CSEQ_SONG_LEVEL, 0, NULL, 0, 0);
	Music_Start(0);

	// "Start your engines, for Sony Computer..."
	CDSYS_XAPlay(CDSYS_XA_TYPE_EXTRA, 0x50);

	while (sdata->XA_State != 0)
	{
		// WARNING: Read-only address (ram, 0x8008d888) is written
		// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003c940-0x8003c948 for startup XA pause polling.
#ifdef CTR_NATIVE
		// NOTE(aalhendi): Retail hardware interrupts keep XA/audio moving while
		// this loop spins. Native owns VBlank in VSync(), so pump it here.
		VSync(0);
#endif
		CDSYS_XAPauseAtEnd();
	}
#else
	/*
	 * No boot logo music/XA in the isolated CSEQ test build. Level loading
	 * immediately follows HOWL init and the shared UI asset load below.
	 */
	CseqMusic_StopAll();
#endif

	DecalGlobal_Clear(gGT);

	// This loads UI textures (shared.vrm)
	// This includes traffic lights, font, and more
	LOAD_VramFile(sdata->ptrBigfile1, 0x102, NULL, &vramSize, -1);

	sdata->mainGameState = 3;

	// start loading
	sdata->Loading.stage = LOAD_TEN_STAGES_0;

	clockEffect = &gGT->clockEffectEnabled;
	gGT->gameMode1 |= LOADING;
	gGT->clockEffectEnabled = *clockEffect & 0xfffe;
}
