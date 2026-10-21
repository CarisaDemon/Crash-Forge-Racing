#include <common.h>

#if defined(CTR_NATIVE)

#include <NativeChat.h>
#include <NativeHangout.h>
#include <NativeOnlineSfx.h>
#include <NativeSession.h>
#include <platform/native_audio.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE = 0,
	NATIVE_HANGOUT_CUSTOM_TRANSITION_ENTER_COVER,
	NATIVE_HANGOUT_CUSTOM_TRANSITION_ENTER_REVEAL,
	NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_COVER,
	NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_REVEAL,
};

enum
{
	NATIVE_HANGOUT_CHAT_VISIBLE = 4,
	NATIVE_HANGOUT_MAX_SPAWNS = 16,
	/* HUD chat appears on activity, then gets out of the way automatically. */
	NATIVE_HANGOUT_HUD_CHAT_TICKS = 30 * 5,
	NATIVE_HANGOUT_HUD_CHAT_SCALE = 0xB00,
	NATIVE_HANGOUT_FLOATING_CHAT_TICKS = 30 * 4,
	NATIVE_HANGOUT_FLOATING_Y_OFFSET = 0xC0,
	/* STATIC_SCAN's decoded surface is XY, Z=5, about 1806 x 1209 units.
	 * Keep a padded rectangle inside its curved bezel, just ahead of the glass. */
	NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET = 10,
	NATIVE_HANGOUT_TERMINAL_TEXT_LEFT = -700,
	NATIVE_HANGOUT_TERMINAL_TEXT_TOP = -400,
	NATIVE_HANGOUT_TERMINAL_TEXT_MAX_WIDTH = 1400,
	NATIVE_HANGOUT_TERMINAL_LINE_STEP = 32,
	NATIVE_HANGOUT_TERMINAL_TEXT_ROWS = 25,
	NATIVE_HANGOUT_TERMINAL_TEXT_SCALE = 0x2800,
	NATIVE_HANGOUT_RACE_TEST_COUNTDOWN_MS = 30000,
	NATIVE_HANGOUT_RACE_TEST_ZERO_HOLD_MS = 1000,
	/* Legacy prototype helper kept until the old renderer is removed. */
	NATIVE_HANGOUT_TERMINAL_MAX_DIST = 0x1800,
};

static int s_hangoutActive;
static int s_hangoutLaunchRequested;
static int s_hangoutExitRequested;
static int s_hangoutSpawnApplied;
static int s_hangoutSpawnCount;
static int s_hangoutSpawnLevelIDs[NATIVE_HANGOUT_MAX_SPAWNS];
static unsigned int s_hangoutRngState;
static int s_hangoutChatOpen;
static int s_hangoutMenuOpen;
static int s_hangoutCustomizationOpen;
static int s_hangoutCustomizationTransition;
static int s_hangoutCustomizationExitApply;
static struct NativeOnlineSfxState s_hangoutOnlineSfx;
static int s_hangoutRaceTestActive;
static unsigned int s_hangoutRaceTestStartMs;
static unsigned int s_hangoutRaceTestZeroMs;
static int s_hangoutPlayerDisconnected;
static int s_hangoutIgnoreOpenKeyText;
static struct Instance *s_hangoutSaveScreen;
static char s_hangoutInput[NATIVE_CHAT_TEXT_MAX];
static int s_hangoutInputLen;
static unsigned int s_hangoutHudVisibleUntil;
static int s_hangoutLastChatCount;
static struct AdvProgress s_hangoutAdvBackup;
/* Progress counters are cached separately from AdvProgress in GameTracker.
 * Keep both snapshots intact while the social sandbox runs. */
static unsigned char s_hangoutAdvCountersBackup[
	sizeof(((struct GameTracker *)0)->currAdvProfile)];
static int s_hangoutAdvBackupValid;

static void NativeHangout_SuppressAdventureMask(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int hadMaskState =
		sdata->AkuAkuHintState != 0 ||
		sdata->AkuHint_RequestedHint != -1 ||
		sdata->boolDraw3D_AdvMask != 0 ||
		sdata->boolIsMaskThreadAlive != 0;

	if (sdata->boolIsMaskThreadAlive != 0)
		VehTalkMask_End();

	sdata->AkuAkuHintState = 0;
	sdata->AkuHint_RequestedHint = -1;
	sdata->AkuHint_boolInterruptWarppad = 0;
	sdata->boolDraw3D_AdvMask = 0;

	/* If a hint managed to start on the same frame Hangout was requested,
	 * undo only the mask-owned driver freeze. Do not disturb unrelated hub
	 * state or normal Adventure behaviour outside Hangout. */
	if (hadMaskState &&
	    gGT != NULL &&
	    gGT->drivers[0] != NULL &&
	    gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] ==
		    VehPhysProc_FreezeEndEvent_Init)
	{
		gGT->drivers[0]->funcPtrs[DRIVER_FUNC_INIT] =
			VehPhysProc_Driving_Init;
	}
}

static unsigned int NativeHangout_Tick(void)
{
	return (unsigned int)sdata->frameCounter;
}

static unsigned int NativeHangout_RngNext(void)
{
	unsigned int x = s_hangoutRngState;

	if (x == 0)
		x = 0x8f31a5c7u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_hangoutRngState = x;
	return x;
}

static void NativeHangout_ShowHudChat(void)
{
	s_hangoutHudVisibleUntil =
		NativeHangout_Tick() + NATIVE_HANGOUT_HUD_CHAT_TICKS;
}

static int NativeHangout_HudChatIsVisible(void)
{
	if (s_hangoutChatOpen)
		return 1;

	return (s32)(s_hangoutHudVisibleUntil - NativeHangout_Tick()) >= 0;
}

static int NativeHangout_ChatScaleForChar(unsigned char c, int baseScale)
{
	if (c >= 'a' && c <= 'z')
		return (baseScale * 3) / 4;

	return baseScale;
}

static int NativeHangout_ChatCharAdvance(unsigned char c, int baseScale)
{
	int width = data.font_charPixWidth[FONT_SMALL];
	int scale = NativeHangout_ChatScaleForChar(c, baseScale);

	if (c == ':' || c == '.')
		width = data.font_puncPixWidth[FONT_SMALL];

	if (c == ' ')
		scale = baseScale;

	return FP_Mult(width, scale);
}

static int NativeHangout_ChatLineWidthScaled(const char *text, int baseScale)
{
	int width = 0;

	if (text == NULL)
		return 0;

	for (; *text != 0; text++)
		width += NativeHangout_ChatCharAdvance((unsigned char)*text, baseScale);

	return width;
}

static void NativeHangout_FitChatLine(
	char *text,
	int maxWidth,
	int baseScale)
{
	int len;

	if (text == NULL || maxWidth < 8)
		return;

	if (NativeHangout_ChatLineWidthScaled(text, baseScale) <= maxWidth)
		return;

	len = (int)strlen(text);
	while (len > 3)
	{
		text[len - 3] = '.';
		text[len - 2] = '.';
		text[len - 1] = '.';
		text[len] = 0;

		if (NativeHangout_ChatLineWidthScaled(text, baseScale) <= maxWidth)
			return;

		len--;
		text[len] = 0;
	}
}

/* Retail CTR deliberately maps lowercase ASCII to the uppercase glyphs.
 * Chat still preserves the original bytes; for Hangout only, make lowercase
 * visually distinct by drawing those same glyphs smaller and lower. */
static void NativeHangout_DrawChatLine(
	const char *text,
	int posX,
	int posY,
	int baseScale,
	int colorIndex,
	int centered)
{
	struct GameTracker *gGT = sdata->gGT;
	s16 iconGroupID;
	struct Icon **icons;
	u32 *color;
	int cursorX;

	if (text == NULL || gGT == NULL)
		return;

	iconGroupID = data.font_IconGroupID[FONT_SMALL];
	if (iconGroupID < 0 || gGT->iconGroup[iconGroupID] == NULL)
		return;

	icons = ICONGROUP_GETICONS(gGT->iconGroup[iconGroupID]);
	color = data.ptrColor[colorIndex];
	cursorX = posX;

	if (centered)
		cursorX -= NativeHangout_ChatLineWidthScaled(text, baseScale) / 2;

	for (; *text != 0; text++)
	{
		unsigned char c = (unsigned char)*text;
		int drawScale = NativeHangout_ChatScaleForChar(c, baseScale);
		int advance = NativeHangout_ChatCharAdvance(c, baseScale);
		int yOffset = (c >= 'a' && c <= 'z') ? 2 : 0;
		u16 iconID = 0xff;

		if (c >= 0x21)
			iconID = data.font_characterIconID[c - 0x21];

		if (iconID != 0xff &&
		    iconID < gGT->iconGroup[iconGroupID]->numIcons)
		{
			DecalHUD_DrawPolyGT4(
				icons[iconID],
				(s16)cursorX,
				(s16)(posY + yOffset),
				&gGT->backBuffer->primMem,
				gGT->pushBuffer_UI.ptrOT,
				color[0], color[1], color[2], color[3],
				0,
				(s16)drawScale);
		}

		cursorX += advance;
	}
}

static void NativeHangout_CloseChat(void)
{
	if (!s_hangoutChatOpen)
		return;

	s_hangoutChatOpen = 0;
	s_hangoutIgnoreOpenKeyText = 0;
	Platform_StopTextInput();
}

static void NativeHangout_OpenChat(void)
{
	if (s_hangoutChatOpen)
		return;

	s_hangoutChatOpen = 1;
	s_hangoutInputLen = 0;
	s_hangoutInput[0] = 0;
	s_hangoutIgnoreOpenKeyText = 1;
	NativeHangout_ShowHudChat();
	Platform_StartTextInput();
}

static void NativeHangout_SendInput(void)
{
	const char *playerName;
	int playerID;

	if (s_hangoutInputLen <= 0)
	{
		NativeHangout_CloseChat();
		return;
	}

	playerName = NativeSession_GetLocalPlayerName();
	playerID = NativeSession_GetLocalPlayerID();

	NativeChat_AddLocal(
		playerID,
		playerName,
		s_hangoutInput,
		NativeHangout_Tick());
	s_hangoutLastChatCount = NativeChat_GetCount();
	NativeHangout_ShowHudChat();

	Platform_Log(
		"[HangoutChat] %s: %s\n",
		playerName,
		s_hangoutInput);

	s_hangoutInputLen = 0;
	s_hangoutInput[0] = 0;
	NativeHangout_CloseChat();
}

void NativeHangout_RequestLaunch(void)
{
	NativeSession_SetMode(NATIVE_SESSION_HANGOUT_LOCAL);
	NativeSession_SetLocalPlayerID(0);
	NativeSession_SetLocalPlayerName(
		Platform_GetPlayerName(0));
	s_hangoutLaunchRequested = 1;
}

int NativeHangout_ConsumeLaunchRequest(void)
{
	int requested = s_hangoutLaunchRequested;
	s_hangoutLaunchRequested = 0;
	return requested;
}

void NativeHangout_RequestExit(void)
{
	if (s_hangoutActive)
		s_hangoutExitRequested = 1;
}

int NativeHangout_ConsumeExitRequest(void)
{
	int requested = s_hangoutExitRequested;
	s_hangoutExitRequested = 0;
	return requested;
}

static void NativeHangout_PlayOnlineSfx(
	enum NativeOnlineSfxEvent eventId,
	void *user)
{
	(void)user;
	NativeAudio_OnlineSfxPlay((int)eventId);
	Platform_Log("[HANGOUT ONLINE SFX] event=%d asset=%s\n",
	             (int)eventId,
	             NativeOnlineSfx_GetAssetName(eventId));
}

int NativeHangout_ShouldSuppressRetailWarpSfx(void)
{
	return NativeHangout_IsActive() &&
	       s_hangoutRaceTestActive &&
	       s_hangoutRaceTestZeroMs != 0;
}

void NativeHangout_RequestRaceTest(void)
{
	unsigned int now;

	if (!NativeHangout_IsActive() ||
	    sdata->gGT == NULL ||
	    sdata->Loading.stage != LOAD_IDLE ||
	    NativeHangout_IsMenuOpen() ||
	    NativeHangout_IsCustomizationOpen() ||
	    NativeHangout_IsCustomizationTransitionActive() ||
	    NativeHangout_IsPlayerDisconnected() ||
	    s_hangoutRaceTestActive)
	{
		return;
	}

	now = (unsigned int)SDL_GetTicks();
	NativeOnlineSfx_Reset(&s_hangoutOnlineSfx);
	NativeOnlineSfx_SetMapPickCountdown(
		&s_hangoutOnlineSfx,
		1,
		now);
	NativeOnlineSfx_SetRaceWarpCountdown(
		&s_hangoutOnlineSfx,
		1);
	/* The 30-second mark is itself one of the 10-second reminder beats. */
	NativeHangout_PlayOnlineSfx(
		NATIVE_ONLINE_SFX_MAP_PICK_REMINDER,
		NULL);

	s_hangoutRaceTestActive = 1;
	s_hangoutRaceTestStartMs = now;
	s_hangoutRaceTestZeroMs = 0;

	NativeChat_AddSystem(
		"NEXT RACE: CRASH COVE",
		NativeHangout_Tick());
	NativeHangout_ShowHudChat();
	Platform_LogWarn(
		"[HANGOUT RACE TEST] started 30s countdown -> Crash Cove (Ctrl+Alt+Shift+U)\n");
}

static void NativeHangout_LoadRaceTestCrashCove(struct GameTracker *gGT)
{
	u32 clearMode1;

	if (gGT == NULL)
		return;

	/* Leave the social sandbox first so Adventure progress is restored before
	 * switching modes. The race itself then starts through CTR's ordinary
	 * level-loader path, not through an Adventure WarpPad. */
	NativeHangout_SetActive(0);

	clearMode1 =
		BATTLE_MODE |
		ADVENTURE_MODE |
		TIME_TRIAL |
		ADVENTURE_ARENA |
		ADVENTURE_CUP |
		ADVENTURE_BOSS |
		RELIC_RACE |
		CRYSTAL_CHALLENGE |
		POINT_LIMIT |
		LIFE_LIMIT |
		TIME_LIMIT;

	gGT->gameMode1 &= ~clearMode1;
	gGT->gameMode1 |= ARCADE_MODE;
	gGT->gameMode2 &= ~(CUP_ANY_KIND);
	gGT->numPlyrNextGame = 1;
	gGT->numLaps = MM_DEFAULT_LAP_COUNT;
	gGT->originalEventTime = TITLE_INITIAL_EVENT_TIME;
	gGT->currLEV = CRASH_COVE;

	/* Apply the same mode cleanup again at the load gate, after the checkered
	 * flag covers the screen, so no hub mode bit can leak into Crash Cove. */
	sdata->Loading.OnBegin.AddBitsConfig0 |= ARCADE_MODE;
	sdata->Loading.OnBegin.RemBitsConfig0 |= clearMode1;

	Platform_LogWarn(
		"[HANGOUT RACE TEST] zero hold complete -> loading Crash Cove as 1P Arcade\n");
	MainRaceTrack_RequestLoad(CRASH_COVE);
}

static void NativeHangout_UpdateRaceTest(struct GameTracker *gGT)
{
	unsigned int now;
	unsigned int elapsed;
	int remainingMs;

	if (!s_hangoutRaceTestActive || gGT == NULL)
		return;

	now = (unsigned int)SDL_GetTicks();
	elapsed = now - s_hangoutRaceTestStartMs;

	if (s_hangoutRaceTestZeroMs == 0)
	{
		if (elapsed < NATIVE_HANGOUT_RACE_TEST_COUNTDOWN_MS)
		{
			remainingMs =
				NATIVE_HANGOUT_RACE_TEST_COUNTDOWN_MS -
				(int)elapsed;

			NativeOnlineSfx_UpdateMapPickCountdown(
				&s_hangoutOnlineSfx,
				now,
				NativeHangout_PlayOnlineSfx,
				NULL);
			NativeOnlineSfx_UpdateRaceWarpCountdown(
				&s_hangoutOnlineSfx,
				remainingMs,
				NativeHangout_PlayOnlineSfx,
				NULL);
			return;
		}

		/* At zero, stop the 10-second reminder before updating the warp SFX so
		 * clktick2 can never overlap warpaway on the same instant. Start CTR's
		 * real kart-warp animation immediately as the synchronized cue plays. */
		NativeOnlineSfx_SetMapPickCountdown(
			&s_hangoutOnlineSfx,
			0,
			now);
		s_hangoutRaceTestZeroMs = now;
		NativeOnlineSfx_UpdateRaceWarpCountdown(
			&s_hangoutOnlineSfx,
			0,
			NativeHangout_PlayOnlineSfx,
			NULL);

		if (gGT->drivers[0] != NULL &&
		    gGT->drivers[0]->instSelf != NULL)
		{
			/* th is unused by retail Warp_Init, so start now instead of waiting
			 * one more frame for DRIVER_FUNC_INIT dispatch. */
			VehStuckProc_Warp_Init(NULL, gGT->drivers[0]);
		}

		Platform_LogWarn(
			"[HANGOUT RACE TEST] countdown reached 0; warpaway + retail warp animation started\n");
		return;
	}

	/* Preserve the requested one-second audible hold, but never cut the warp
	 * animation in half. Retail marks the kart hidden when the shrink/flare
	 * phase is complete; a 4-second guard prevents a broken state from hanging. */
	if ((unsigned int)(now - s_hangoutRaceTestZeroMs) <
	    NATIVE_HANGOUT_RACE_TEST_ZERO_HOLD_MS)
	{
		return;
	}

	if (gGT->drivers[0] != NULL &&
	    gGT->drivers[0]->instSelf != NULL &&
	    (gGT->drivers[0]->instSelf->flags & HIDE_MODEL) == 0 &&
	    (unsigned int)(now - s_hangoutRaceTestZeroMs) < 4000u)
	{
		return;
	}

	NativeOnlineSfx_SetRaceWarpCountdown(
		&s_hangoutOnlineSfx,
		0);
	s_hangoutRaceTestActive = 0;
	NativeHangout_LoadRaceTestCrashCove(gGT);
}

void NativeHangout_SetActive(int active)
{
	active = active != 0;

	if (s_hangoutActive == active)
	{
		if (active)
			s_hangoutSpawnApplied = 0;
		return;
	}

	if (!active)
	{
		NativeHangout_CloseChat();

		if (s_hangoutAdvBackupValid)
		{
			memcpy(
				&sdata->advProgress,
				&s_hangoutAdvBackup,
				sizeof(s_hangoutAdvBackup));
			memcpy(
				&sdata->gGT->currAdvProfile,
				s_hangoutAdvCountersBackup,
				sizeof(s_hangoutAdvCountersBackup));
			s_hangoutAdvBackupValid = 0;
		}

		s_hangoutActive = 0;
		s_hangoutSpawnApplied = 0;
		s_hangoutSpawnCount = 0;
		s_hangoutSaveScreen = NULL;
		s_hangoutMenuOpen = 0;
		s_hangoutCustomizationOpen = 0;
		s_hangoutCustomizationTransition = NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE;
		s_hangoutCustomizationExitApply = 0;
		NativeOnlineSfx_Reset(&s_hangoutOnlineSfx);
		s_hangoutRaceTestActive = 0;
		s_hangoutRaceTestStartMs = 0;
		s_hangoutRaceTestZeroMs = 0;
		if (s_hangoutPlayerDisconnected)
			Platform_SetDebugFreecamEnabled(0);
		s_hangoutPlayerDisconnected = 0;
		s_hangoutInputLen = 0;
		s_hangoutInput[0] = 0;
		s_hangoutHudVisibleUntil = 0;
		s_hangoutLastChatCount = 0;
		s_hangoutExitRequested = 0;
		NativeChat_Reset();
		NativeSession_Reset();
		return;
	}

	memcpy(
		&s_hangoutAdvBackup,
		&sdata->advProgress,
		sizeof(s_hangoutAdvBackup));
	memcpy(
		s_hangoutAdvCountersBackup,
		&sdata->gGT->currAdvProfile,
		sizeof(s_hangoutAdvCountersBackup));
	s_hangoutAdvBackupValid = 1;

	/* The Adventure hub geometry is shared, but its story state is not.
	 * Never expose the selected save's rewards, gates or 101% counters to
	 * Hangout; this transient state is restored byte-for-byte on exit. */
	memset(&sdata->advProgress, 0, sizeof(sdata->advProgress));
	sdata->advProgress.HubLevYouSavedOn = N_SANITY_BEACH;
	memset(&sdata->gGT->currAdvProfile, 0,
	       sizeof(sdata->gGT->currAdvProfile));

	s_hangoutActive = 1;
	NativeHangout_SuppressAdventureMask();
	s_hangoutSpawnApplied = 0;
	s_hangoutSpawnCount = 0;
	s_hangoutSaveScreen = NULL;
	s_hangoutMenuOpen = 0;
	s_hangoutCustomizationOpen = 0;
	s_hangoutCustomizationTransition = NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE;
	s_hangoutCustomizationExitApply = 0;
	NativeOnlineSfx_Reset(&s_hangoutOnlineSfx);
	s_hangoutRaceTestActive = 0;
	s_hangoutRaceTestStartMs = 0;
	s_hangoutRaceTestZeroMs = 0;
	s_hangoutPlayerDisconnected = 0;
	s_hangoutInputLen = 0;
	s_hangoutInput[0] = 0;
	s_hangoutExitRequested = 0;
	s_hangoutRngState = (unsigned int)SDL_GetTicks() ^ 0xa57c31e9u;

	NativeChat_Reset();
	NativeChat_AddSystem("N. SANITY HANGOUT - LOCAL", NativeHangout_Tick());
	NativeChat_AddSystem("T OPENS CHAT", NativeHangout_Tick());
	s_hangoutLastChatCount = NativeChat_GetCount();
	NativeHangout_ShowHudChat();
}

int NativeHangout_IsActive(void)
{
	return s_hangoutActive &&
	       NativeSession_GetMode() == NATIVE_SESSION_HANGOUT_LOCAL;
}

int NativeHangout_ChatIsOpen(void)
{
	return s_hangoutChatOpen;
}

void NativeHangout_SetMenuOpen(int open)
{
	if (!NativeHangout_IsActive())
	{
		s_hangoutMenuOpen = 0;
		return;
	}

	open = open != 0;
	if (open)
		NativeHangout_CloseChat();

	s_hangoutMenuOpen = open;
}

int NativeHangout_IsMenuOpen(void)
{
	return NativeHangout_IsActive() && s_hangoutMenuOpen;
}

void NativeHangout_SetCustomizationOpen(int open)
{
	if (!NativeHangout_IsActive())
	{
		s_hangoutCustomizationOpen = 0;
		return;
	}

	s_hangoutCustomizationOpen = open != 0;
	if (s_hangoutCustomizationOpen)
		NativeHangout_SetMenuOpen(1);
}

int NativeHangout_IsCustomizationOpen(void)
{
	return NativeHangout_IsActive() && s_hangoutCustomizationOpen;
}

int NativeHangout_IsCustomizationTransitionActive(void)
{
	return NativeHangout_IsActive() &&
	       s_hangoutCustomizationTransition !=
		       NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE;
}

int NativeHangout_ShouldHideGameplayHud(void)
{
	return NativeHangout_IsCustomizationOpen() ||
	       NativeHangout_IsCustomizationTransitionActive();
}

static void NativeHangout_BeginFlagCover(void)
{
	RaceFlag_ResetTextAnim();
	RaceFlag_SetCanDraw(1);

	if (RaceFlag_IsFullyOnScreen())
		return;

	if (!RaceFlag_IsFullyOffScreen() && !RaceFlag_IsTransitioning())
		RaceFlag_SetFullyOffScreen();

	if (RaceFlag_IsFullyOffScreen())
		RaceFlag_BeginTransition(1);
}

static void NativeHangout_BeginFlagReveal(void)
{
	RaceFlag_ResetTextAnim();
	RaceFlag_SetCanDraw(1);

	if (RaceFlag_IsFullyOffScreen())
		return;

	RaceFlag_BeginTransition(2);
}

void NativeHangout_RequestCustomizationTransition(void)
{
	if (!NativeHangout_IsActive() ||
	    NativeHangout_IsCustomizationOpen() ||
	    NativeHangout_IsCustomizationTransitionActive())
		return;

	NativeHangout_CloseChat();
	RECTMENU_ClearInput();

	if (sdata->ptrActiveMenu != NULL)
		RECTMENU_Hide(sdata->ptrActiveMenu);
	sdata->ptrDesiredMenu = NULL;

	s_hangoutCustomizationExitApply = 0;
	s_hangoutCustomizationTransition =
		NATIVE_HANGOUT_CUSTOM_TRANSITION_ENTER_COVER;
	NativeHangout_BeginFlagCover();

	Platform_Log("[HANGOUT CUSTOMIZATION] flag enter begin\n");
}

void NativeHangout_RequestCustomizationExitTransition(int apply)
{
	if (!NativeHangout_IsCustomizationOpen() ||
	    NativeHangout_IsCustomizationTransitionActive())
		return;

	s_hangoutCustomizationExitApply = apply != 0;
	s_hangoutCustomizationTransition =
		NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_COVER;
	RECTMENU_ClearInput();
	NativeHangout_BeginFlagCover();

	Platform_Log(
		"[HANGOUT CUSTOMIZATION] flag exit begin apply=%d\n",
		s_hangoutCustomizationExitApply);
}

int NativeHangout_ShouldBlockGameplayInput(void)
{
	return NativeHangout_IsMenuOpen() ||
	       NativeHangout_IsCustomizationOpen() ||
	       NativeHangout_IsCustomizationTransitionActive() ||
	       NativeHangout_IsPlayerDisconnected();
}

void NativeHangout_BindSaveScreen(struct Instance *inst)
{
	s_hangoutSaveScreen = inst;
}

static int NativeHangout_LevelIDAlreadyAdded(int levelID)
{
	int i;

	for (i = 0; i < s_hangoutSpawnCount; i++)
	{
		if (s_hangoutSpawnLevelIDs[i] == levelID)
			return 1;
	}

	return 0;
}

static void NativeHangout_BuildSpawnList(struct GameTracker *gGT)
{
	struct Thread *thread;
	int i;
	int j;

	s_hangoutSpawnCount = 0;

	if (gGT == NULL)
		return;

	for (thread = gGT->threadBuckets[WARPPAD].thread;
	     thread != NULL &&
	     s_hangoutSpawnCount < NATIVE_HANGOUT_MAX_SPAWNS;
	     thread = thread->siblingThread)
	{
		struct WarpPad *warpPad;

		if (thread->object == NULL)
			continue;

		warpPad = (struct WarpPad *)thread->object;
		if (NativeHangout_LevelIDAlreadyAdded(warpPad->levelID))
			continue;

		s_hangoutSpawnLevelIDs[s_hangoutSpawnCount++] = warpPad->levelID;
	}

	/* Stable order prevents spawn choice from depending on thread-list order. */
	for (i = 1; i < s_hangoutSpawnCount; i++)
	{
		int value = s_hangoutSpawnLevelIDs[i];
		j = i - 1;

		while (j >= 0 && s_hangoutSpawnLevelIDs[j] > value)
		{
			s_hangoutSpawnLevelIDs[j + 1] = s_hangoutSpawnLevelIDs[j];
			j--;
		}

		s_hangoutSpawnLevelIDs[j + 1] = value;
	}

	Platform_Log(
		"[Hangout] stable spawn list: %d portals\n",
		s_hangoutSpawnCount);
}

static struct Instance *NativeHangout_FindPortalInstance(
	struct GameTracker *gGT,
	int levelID)
{
	struct Thread *thread;

	if (gGT == NULL)
		return NULL;

	for (thread = gGT->threadBuckets[WARPPAD].thread;
	     thread != NULL;
	     thread = thread->siblingThread)
	{
		struct WarpPad *warpPad;

		if (thread->object == NULL || thread->inst == NULL)
			continue;

		warpPad = (struct WarpPad *)thread->object;
		if (warpPad->levelID == levelID)
			return thread->inst;
	}

	return NULL;
}

static int NativeHangout_ApplyPortalSpawnIndex(
	struct GameTracker *gGT,
	int spawnIndex)
{
	struct Driver *driver;
	struct Instance *portalInst;
	int oldPrevLEV;
	int oldPodiumRewardID;
	int levelID;
	char message[96];

	if (gGT == NULL || gGT->levelID != N_SANITY_BEACH)
		return 0;

	driver = gGT->drivers[0];
	if (driver == NULL || driver->instSelf == NULL)
		return 0;

	if (s_hangoutSpawnCount <= 0)
		NativeHangout_BuildSpawnList(gGT);

	if (s_hangoutSpawnCount <= 0)
		return 0;

	if (spawnIndex < 0)
		spawnIndex = 0;

	spawnIndex %= s_hangoutSpawnCount;
	levelID = s_hangoutSpawnLevelIDs[spawnIndex];

	oldPrevLEV = gGT->prevLEV;
	oldPodiumRewardID = gGT->podiumRewardID;

	gGT->prevLEV = levelID;
	gGT->podiumRewardID = NOFUNC;
	VehBirth_TeleportSelf(
		driver,
		VEH_BIRTH_SPAWN_USE_LEVEL_POSITION,
		0);

	/* Retail return spawns are intentionally in front of a portal. Hangout
	 * wants the portal itself as the social spawn point. Snap to the actual
	 * WarpPad center, then run the ordinary ground probe once more at that
	 * X/Z so the kart lands on the local floor instead of keeping the height
	 * from the retail return point. */
	portalInst = NativeHangout_FindPortalInstance(gGT, levelID);
	if (portalInst != NULL)
	{
		driver->posCurr.x =
			CTR_MipsSll(portalInst->matrix.t[0], FRACTIONAL_BITS_8);
		driver->posCurr.y =
			CTR_MipsSll(portalInst->matrix.t[1], FRACTIONAL_BITS_8);
		driver->posCurr.z =
			CTR_MipsSll(portalInst->matrix.t[2], FRACTIONAL_BITS_8);
		driver->posPrev = driver->posCurr;

		VehBirth_TeleportSelf(driver, 0, 0);

		driver->instSelf->matrix.t[0] =
			CTR_MipsSra(driver->posCurr.x, FRACTIONAL_BITS_8);
		driver->instSelf->matrix.t[1] =
			CTR_MipsSra(driver->posCurr.y, FRACTIONAL_BITS_8);
		driver->instSelf->matrix.t[2] =
			CTR_MipsSra(driver->posCurr.z, FRACTIONAL_BITS_8);
	}

	gGT->prevLEV = oldPrevLEV;
	gGT->podiumRewardID = oldPodiumRewardID;

	s_hangoutSpawnApplied = 1;

	snprintf(
		message,
		sizeof(message),
		"SPAWNED AT PORTAL %d",
		levelID);
	NativeChat_AddSystem(
		message,
		NativeHangout_Tick());

	Platform_Log(
		"[Hangout] spawn index=%d levelID=%d count=%d\n",
		spawnIndex,
		levelID,
		s_hangoutSpawnCount);
	return 1;
}

static void NativeHangout_ChooseLocalSpawn(struct GameTracker *gGT)
{
	int spawnIndex;

	if (s_hangoutSpawnCount <= 0)
		NativeHangout_BuildSpawnList(gGT);

	if (s_hangoutSpawnCount <= 0)
		return;

	spawnIndex =
		(int)(NativeHangout_RngNext() %
		      (unsigned int)s_hangoutSpawnCount);

	if (NativeHangout_ApplyPortalSpawnIndex(
			gGT,
			spawnIndex))
	{
		/* The online join cue has two retail CNK variants. Pick the variant
		 * here; a future authoritative network join can send this same bit so
		 * every client hears the identical cue. */
		NativeOnlineSfx_PlayerJoined(
			&s_hangoutOnlineSfx,
			(int)(NativeHangout_RngNext() & 1u),
			NativeHangout_PlayOnlineSfx,
			NULL);
	}
}

static void NativeHangout_StopPlayerEngines(struct Driver *d)
{
	int engineClass;

	if (d == NULL)
		return;

	NativeAudio_EngineStop((int)d->driverID);
	for (engineClass = 0; engineClass < NUM_CLASSES; engineClass++)
		EngineAudio_Stop((engineClass * 4) + d->driverID);

	d->engineSoundVolumeState = 0;
	d->engineSoundPitchState = 0;
}

static void NativeHangout_KeepPlayerDisconnected(struct GameTracker *gGT)
{
	struct Driver *d;

	if (!s_hangoutPlayerDisconnected || gGT == NULL)
		return;

	d = gGT->drivers[0];
	if (d == NULL || d->instSelf == NULL)
		return;

	d->instSelf->flags |= HIDE_MODEL;
	memset(&d->velocity, 0, sizeof(d->velocity));
	d->speed = 0;
	d->speedApprox = 0;
	d->fireSpeed = 0;
	d->xSpeed = 0;
	d->ySpeed = 0;
	d->zSpeed = 0;
	d->kartState = KS_NORMAL;
	d->wheelRotation = 0;
	NativeHangout_StopPlayerEngines(d);
}

int NativeHangout_IsPlayerDisconnected(void)
{
	return NativeHangout_IsActive() && s_hangoutPlayerDisconnected;
}

void NativeHangout_DisconnectPlayer(void)
{
	struct GameTracker *gGT;

	if (!NativeHangout_IsActive() || s_hangoutPlayerDisconnected)
		return;

	gGT = sdata->gGT;
	if (gGT == NULL || gGT->drivers[0] == NULL ||
	    gGT->drivers[0]->instSelf == NULL)
		return;

	NativeHangout_CloseChat();
	s_hangoutPlayerDisconnected = 1;
	NativeHangout_KeepPlayerDisconnected(gGT);
	Platform_SetDebugFreecamEnabled(1);
	NativeOnlineSfx_PlayerDisconnected(
		&s_hangoutOnlineSfx,
		NativeHangout_PlayOnlineSfx,
		NULL);

	NativeChat_AddSystem("PLAYER 1 DISCONNECTED", NativeHangout_Tick());
	NativeHangout_ShowHudChat();
	Platform_LogWarn(
		"[HANGOUT DISCONNECT] P1 disconnected; kart hidden, engines stopped, freecam locked\n");
}

void NativeHangout_PlayCustomizationDoneSfx(void)
{
	if (!NativeHangout_IsActive() || NativeHangout_IsPlayerDisconnected())
		return;

	NativeHangout_PlayOnlineSfx(
		NATIVE_ONLINE_SFX_CUSTOMIZATION_DONE,
		NULL);
	Platform_Log("[HANGOUT CUSTOMIZATION] connected-player finish sfx=magtran2.wav\n");
}

void NativeHangout_ConnectPlayer(void)
{
	struct GameTracker *gGT;
	struct Driver *d;

	if (!NativeHangout_IsPlayerDisconnected())
		return;

	gGT = sdata->gGT;
	if (gGT == NULL || sdata->Loading.stage != LOAD_IDLE ||
	    gGT->drivers[0] == NULL || gGT->drivers[0]->instSelf == NULL)
		return;

	d = gGT->drivers[0];
	s_hangoutPlayerDisconnected = 0;
	Platform_SetDebugFreecamEnabled(0);
	d->instSelf->flags &= ~HIDE_MODEL;
	memset(&d->velocity, 0, sizeof(d->velocity));
	d->speed = 0;
	d->speedApprox = 0;
	d->fireSpeed = 0;
	d->xSpeed = 0;
	d->ySpeed = 0;
	d->zSpeed = 0;
	d->kartState = KS_NORMAL;
	d->wheelRotation = 0;
	d->funcPtrs[DRIVER_FUNC_INIT] = VehPhysProc_Driving_Init;
	VehBirth_SetConsts(d);

	/* Reuse the current loadout exactly as-is and only move the existing
	 * driver to a random Hangout portal. ChooseLocalSpawn also emits one of
	 * the two randomized join sounds (gatwarp2/gatwarp3). */
	s_hangoutSpawnApplied = 0;
	NativeHangout_ChooseLocalSpawn(gGT);

	d->instSelf->flags &= ~HIDE_MODEL;
	d->engineSoundMode = ENGINE_SOUND_DYNAMIC;
	d->engineSoundVolumeState = 0;
	d->engineSoundPitchState = 0;
	VehBirth_EngineAudio_AllPlayers();

	NativeChat_AddSystem("PLAYER 1 CONNECTED", NativeHangout_Tick());
	NativeHangout_ShowHudChat();
	Platform_LogWarn(
		"[HANGOUT CONNECT] P1 connected with current loadout at random portal\n");
}

void NativeHangout_PrepareDisconnectedCustomization(void)
{
	if (!NativeHangout_IsPlayerDisconnected())
		return;

	/* Character Select owns the camera while it is open. The disconnected
	 * freecam resumes on cancel, or is permanently released on reconnect. */
	Platform_SetDebugFreecamEnabled(0);
}

void NativeHangout_FinishDisconnectedCustomization(int apply)
{
	struct GameTracker *gGT;

	if (!NativeHangout_IsPlayerDisconnected())
		return;

	gGT = sdata->gGT;
	if (gGT == NULL || gGT->drivers[0] == NULL ||
	    gGT->drivers[0]->instSelf == NULL)
		return;

	if (!apply)
	{
		NativeHangout_KeepPlayerDisconnected(gGT);
		Platform_SetDebugFreecamEnabled(1);
		Platform_LogWarn(
			"[HANGOUT DISCONNECT] customization cancelled; P1 remains disconnected\n");
		return;
	}

	/* Personalization has already applied the selected loadout to the same
	 * Driver. Reuse the exact direct-connect path so test and customization
	 * reconnects cannot drift apart. */
	NativeHangout_ConnectPlayer();
	Platform_LogWarn(
		"[HANGOUT DISCONNECT] P1 reconnect requested from customization\n");
}

void NativeHangout_Update(struct GameTracker *gGT)
{
	if (!NativeHangout_IsActive() || gGT == NULL)
		return;

	/* Hangout never owns Adventure tutorial/hint masks. Keep this enforced
	 * continuously so a retail trigger cannot re-arm Aku/Uka later. */
	NativeHangout_SuppressAdventureMask();

	/* drivers[0] may already be the freed previous scene while loading. */
	if (s_hangoutPlayerDisconnected &&
	    sdata->Loading.stage == LOAD_IDLE &&
	    !NativeHangout_IsCustomizationOpen())
		NativeHangout_KeepPlayerDisconnected(gGT);

	if (NativeChat_GetCount() != s_hangoutLastChatCount)
	{
		s_hangoutLastChatCount = NativeChat_GetCount();
		NativeHangout_ShowHudChat();
	}

	NativeHangout_UpdateRaceTest(gGT);
	if (!NativeHangout_IsActive())
		return;

	if (sdata->Loading.stage != LOAD_IDLE)
		return;

	/* Customization uses the retail checkered flag only as a visual wipe.
	 * No loading state is entered: cover the current view, swap menu/world
	 * state underneath the flag, then reveal the destination immediately. */
	switch (s_hangoutCustomizationTransition)
	{
	case NATIVE_HANGOUT_CUSTOM_TRANSITION_ENTER_COVER:
		if (RaceFlag_IsFullyOnScreen())
		{
			MM_Characters_BeginHangoutCustomization();
			/* The checkered flag is the persistent background for Hangout
			 * Customization. Do not reveal the world again until the selector
			 * closes. */
			RaceFlag_SetFullyOnScreen();
			s_hangoutCustomizationTransition =
				NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE;
			Platform_Log("[HANGOUT CUSTOMIZATION] menu active, flag held onscreen\n");
		}
		break;

	case NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_COVER:
		if (RaceFlag_IsFullyOnScreen())
		{
			MM_Characters_FinishHangoutCustomization(
				s_hangoutCustomizationExitApply);
			s_hangoutCustomizationTransition =
				NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_REVEAL;
			NativeHangout_BeginFlagReveal();
			Platform_Log("[HANGOUT CUSTOMIZATION] hangout restored under flag\n");
		}
		break;

	case NATIVE_HANGOUT_CUSTOM_TRANSITION_EXIT_REVEAL:
		if (RaceFlag_IsFullyOffScreen())
		{
			s_hangoutCustomizationTransition =
				NATIVE_HANGOUT_CUSTOM_TRANSITION_NONE;
			s_hangoutCustomizationExitApply = 0;
			Platform_Log("[HANGOUT CUSTOMIZATION] exit transition complete\n");
		}
		break;

	default:
		break;
	}

	/* Defensive repair: menu transitions normally clear this explicitly, but
	 * if a native/retail menu disappears through an alternate path, never leave
	 * the kart input locked forever. Keep ownership during active/desired/submenu
	 * transitions and release it only when the whole menu stack is gone. */
	if (s_hangoutMenuOpen &&
	    !NativeHangout_IsCustomizationTransitionActive() &&
	    sdata->ptrActiveMenu == NULL &&
	    sdata->ptrDesiredMenu == NULL &&
	    sdata->activeSubMenu == NULL)
	{
		s_hangoutMenuOpen = 0;
		s_hangoutCustomizationOpen = 0;
	}

	if (gGT->levelID != N_SANITY_BEACH ||
	    (gGT->gameMode1 & ADVENTURE_ARENA) == 0)
	{
		/* Any normal exit also clears the native social-session state. */
		NativeHangout_SetActive(0);
		return;
	}

	if (s_hangoutSpawnCount <= 0)
		NativeHangout_BuildSpawnList(gGT);

	if (!s_hangoutSpawnApplied)
		NativeHangout_ChooseLocalSpawn(gGT);
}

static void NativeHangout_FormatMessage(
	const struct NativeChatMessage *message,
	char *out,
	int outSize)
{
	if (message == NULL || out == NULL || outSize <= 0)
		return;

	if (message->kind == NATIVE_CHAT_SYSTEM)
	{
		snprintf(
			out,
			(size_t)outSize,
			"SYSTEM: %s",
			message->text);
	}
	else
	{
		snprintf(
			out,
			(size_t)outSize,
			"%s: %s",
			message->playerName[0] ?
				message->playerName : "PLAYER",
			message->text);
	}
}

static int NativeHangout_ProjectWorldPoint(
	struct GameTracker *gGT,
	int worldX,
	int worldY,
	int worldZ,
	SVec2 *out)
{
	struct PushBuffer *pb;
	MATRIX *viewProj;
	SVECTOR pos;
	u32 gteFlags;
	u32 depth;

	if (gGT == NULL || out == NULL)
		return 0;

	pb = &gGT->pushBuffer[0];
	PushBuffer_SetPsyqGeom(pb);

	viewProj = &pb->matrix_ViewProj;
	gte_SetRotMatrix(viewProj);
	gte_SetTransMatrix(viewProj);

	pos.vx = (short)worldX;
	pos.vy = (short)worldY;
	pos.vz = (short)worldZ;
	pos.pad = 0;

	CTR_GteLoadSV0(&pos);
	gte_rtps();
	CTR_GteStoreSXY(out->v);
	gte_stflg(&gteFlags);
	gte_stsz(&depth);

	if ((gteFlags & 0x40000u) != 0 || depth == 0)
		return 0;

	out->x += pb->rect.x;
	out->y += pb->rect.y;

	if (out->x < pb->rect.x - 96 ||
	    out->x > pb->rect.x + pb->rect.w + 96 ||
	    out->y < pb->rect.y - 64 ||
	    out->y > pb->rect.y + pb->rect.h + 64)
		return 0;

	return 1;
}

static int NativeHangout_ProjectInstance(
	struct GameTracker *gGT,
	struct Instance *inst,
	int yOffset,
	SVec2 *out)
{
	if (inst == NULL)
		return 0;

	return NativeHangout_ProjectWorldPoint(
		gGT,
		inst->matrix.t[0],
		inst->matrix.t[1] + yOffset,
		inst->matrix.t[2],
		out);
}

static void NativeHangout_TransformTerminalPoint(
	struct Instance *inst,
	int localX,
	int localY,
	int localZ,
	SVec3 *out)
{
	out->x =
		inst->matrix.t[0] +
		((inst->matrix.m[0][0] * localX +
		  inst->matrix.m[0][1] * localY +
		  inst->matrix.m[0][2] * localZ) >> FRACTIONAL_BITS);
	out->y =
		inst->matrix.t[1] +
		((inst->matrix.m[1][0] * localX +
		  inst->matrix.m[1][1] * localY +
		  inst->matrix.m[1][2] * localZ) >> FRACTIONAL_BITS);
	out->z =
		inst->matrix.t[2] +
		((inst->matrix.m[2][0] * localX +
		  inst->matrix.m[2][1] * localY +
		  inst->matrix.m[2][2] * localZ) >> FRACTIONAL_BITS);
}

static int NativeHangout_GetTerminalBasis(
	struct GameTracker *gGT,
	SVec2 *center,
	double *rightX,
	double *rightY,
	double *upX,
	double *upY,
	int *textScale,
	u16 *rotation)
{
	struct Driver *driver;
	SVec3 worldCenter;
	SVec3 worldRight;
	SVec3 worldUp;
	SVec2 screenRight;
	SVec2 screenUp;
	s64 dx;
	s64 dy;
	s64 dz;
	s64 distSq;
	s64 facing;
	double rx;
	double ry;
	double ux;
	double uy;
	double rLen;
	double uLen;
	double angle;
	int scale;

	if (gGT == NULL ||
	    s_hangoutSaveScreen == NULL ||
	    center == NULL ||
	    rightX == NULL ||
	    rightY == NULL ||
	    upX == NULL ||
	    upY == NULL ||
	    textScale == NULL ||
	    rotation == NULL)
		return 0;

	driver = gGT->drivers[0];
	if (driver == NULL || driver->instSelf == NULL)
		return 0;

	worldCenter.x = (s16)s_hangoutSaveScreen->matrix.t[0];
	worldCenter.y = (s16)s_hangoutSaveScreen->matrix.t[1];
	worldCenter.z = (s16)s_hangoutSaveScreen->matrix.t[2];

	dx = (s64)driver->instSelf->matrix.t[0] - worldCenter.x;
	dy = (s64)driver->instSelf->matrix.t[1] - worldCenter.y;
	dz = (s64)driver->instSelf->matrix.t[2] - worldCenter.z;
	distSq = dx * dx + dy * dy + dz * dz;

	if (distSq >
	    (s64)NATIVE_HANGOUT_TERMINAL_MAX_DIST *
	    NATIVE_HANGOUT_TERMINAL_MAX_DIST)
		return 0;

	/* SaveObj places its interaction camera along local +X, so +X is the
	 * physical front of the terminal. Never draw the chat through its back. */
	facing =
		dx * s_hangoutSaveScreen->matrix.m[0][0] +
		dy * s_hangoutSaveScreen->matrix.m[1][0] +
		dz * s_hangoutSaveScreen->matrix.m[2][0];

	if (facing <= 0)
		return 0;

	/* Do not billboard the terminal chat around steep side angles. Only show
	 * it when the player is looking at the physical front reasonably head-on. */
	if (facing * facing <
	    distSq * (s64)(FP_ONE / 3) * (FP_ONE / 3))
		return 0;

	/* Local Y is vertical and local Z is horizontal on STATIC_SCAN. */
	NativeHangout_TransformTerminalPoint(
		s_hangoutSaveScreen, 0, 0, 128, &worldRight);
	NativeHangout_TransformTerminalPoint(
		s_hangoutSaveScreen, 0, -64, 0, &worldUp);

	if (!NativeHangout_ProjectWorldPoint(
		    gGT,
		    worldCenter.x,
		    worldCenter.y,
		    worldCenter.z,
		    center) ||
	    !NativeHangout_ProjectWorldPoint(
		    gGT,
		    worldRight.x,
		    worldRight.y,
		    worldRight.z,
		    &screenRight) ||
	    !NativeHangout_ProjectWorldPoint(
		    gGT,
		    worldUp.x,
		    worldUp.y,
		    worldUp.z,
		    &screenUp))
		return 0;

	rx = (double)screenRight.x - center->x;
	ry = (double)screenRight.y - center->y;
	ux = (double)screenUp.x - center->x;
	uy = (double)screenUp.y - center->y;
	rLen = sqrt(rx * rx + ry * ry);
	uLen = sqrt(ux * ux + uy * uy);

	if (rLen < 3.0 || uLen < 2.0)
		return 0;

	*rightX = rx / rLen;
	*rightY = ry / rLen;
	*upX = ux / uLen;
	*upY = uy / uLen;

	/* 128 local units approximate half the screen width. Fit roughly a
	 * 120-pixel chat line to that physical width and clamp at retail size. */
	scale = (int)((rLen * 2.0 * FP_ONE) / 120.0);
	if (scale < 0x400)
		scale = 0x400;
	if (scale > FP_ONE)
		scale = FP_ONE;
	*textScale = scale;

	angle = atan2(ry, rx);
	*rotation = (u16)(((int)(angle * 651.8986469044033)) & 0xfff);
	return 1;
}

static void NativeHangout_DrawRotatedChatLine(
	struct GameTracker *gGT,
	const char *text,
	double centerX,
	double centerY,
	double rightX,
	double rightY,
	double upX,
	double upY,
	double lineOffset,
	int baseScale,
	int colorIndex,
	u16 rotation)
{
	s16 iconGroupID;
	struct Icon **icons;
	u32 *color;
	double cursor;
	double start;
	int lineWidth;

	if (gGT == NULL || text == NULL)
		return;

	iconGroupID = data.font_IconGroupID[FONT_SMALL];
	if (iconGroupID < 0 || gGT->iconGroup[iconGroupID] == NULL)
		return;

	icons = ICONGROUP_GETICONS(gGT->iconGroup[iconGroupID]);
	color = data.ptrColor[colorIndex];
	lineWidth = NativeHangout_ChatLineWidthScaled(text, baseScale);
	start = -(double)lineWidth * 0.5;
	cursor = 0.0;

	for (; *text != 0; text++)
	{
		unsigned char c = (unsigned char)*text;
		int drawScale = NativeHangout_ChatScaleForChar(c, baseScale);
		int advance = NativeHangout_ChatCharAdvance(c, baseScale);
		double lower =
			(c >= 'a' && c <= 'z') ? -1.5 : 0.0;
		u16 iconID = 0xff;
		double along = start + cursor + advance * 0.5;
		double px =
			centerX +
			rightX * along +
			upX * (lineOffset + lower);
		double py =
			centerY +
			rightY * along +
			upY * (lineOffset + lower);

		if (c >= 0x21)
			iconID = data.font_characterIconID[c - 0x21];

		if (iconID != 0xff &&
		    iconID < gGT->iconGroup[iconGroupID]->numIcons)
		{
			DecalHUD_Arrow2D(
				icons[iconID],
				(s16)px,
				(s16)py,
				&gGT->backBuffer->primMem,
				gGT->pushBuffer_UI.ptrOT,
				color[0], color[1], color[2], color[3],
				0,
				drawScale,
				rotation);
		}

		cursor += advance;
	}
}

static void NativeHangout_RenderLinkedTerminalLegacy(struct GameTracker *gGT)
{
	SVec2 center;
	double rightX;
	double rightY;
	double upX;
	double upY;
	u16 rotation;
	int textScale;
	int count;
	int first;
	int i;
	int lineIndex;
	int lineStep;
	int maxWidth;

	if (!NativeHangout_GetTerminalBasis(
		    gGT,
		    &center,
		    &rightX,
		    &rightY,
		    &upX,
		    &upY,
		    &textScale,
		    &rotation))
		return;

	/* The first perspective attempt rotated every glyph independently and
	 * produced a diagonal chain. Keep the text anchored to the real terminal
	 * center and distance-scaled, but draw it upright. Side/back views are
	 * culled above, so it no longer behaves like an always-facing billboard. */
	(void)rightX;
	(void)rightY;
	(void)upX;
	(void)upY;
	(void)rotation;

	count = NativeChat_GetCount();
	first = count - 3;
	if (first < 0)
		first = 0;

	lineStep = FP_Mult(9, textScale);
	if (lineStep < 5)
		lineStep = 5;

	maxWidth = NativeHangout_ChatLineWidthScaled(
		"MMMMMMMMMMMMMMMMMMMM",
		textScale);
	lineIndex = 0;

	for (i = first; i < count; i++, lineIndex++)
	{
		const struct NativeChatMessage *message =
			NativeChat_Get(i);
		char line[
			NATIVE_CHAT_NAME_MAX +
			NATIVE_CHAT_TEXT_MAX +
			4];
		int offset =
			(1 - lineIndex) * lineStep;

		NativeHangout_FormatMessage(
			message,
			line,
			sizeof(line));

		NativeHangout_FitChatLine(
			line,
			maxWidth,
			textScale);

		NativeHangout_DrawChatLine(
			line,
			center.x,
			center.y + offset,
			textScale,
			WHITE,
			1);
	}
}

static int NativeHangout_ProjectWorldPointRaw(
	struct GameTracker *gGT,
	int worldX,
	int worldY,
	int worldZ,
	SVec2 *out)
{
	struct PushBuffer *pb;
	MATRIX *viewProj;
	SVECTOR pos;
	u32 gteFlags;
	u32 depth;

	if (gGT == NULL || out == NULL)
		return 0;

	pb = &gGT->pushBuffer[0];
	PushBuffer_SetPsyqGeom(pb);

	viewProj = &pb->matrix_ViewProj;
	gte_SetRotMatrix(viewProj);
	gte_SetTransMatrix(viewProj);

	pos.vx = (short)worldX;
	pos.vy = (short)worldY;
	pos.vz = (short)worldZ;
	pos.pad = 0;

	CTR_GteLoadSV0(&pos);
	gte_rtps();
	CTR_GteStoreSXY(out->v);
	gte_stflg(&gteFlags);
	gte_stsz(&depth);

	if ((gteFlags & 0x40000u) != 0 || depth == 0)
		return 0;

	out->x += pb->rect.x;
	out->y += pb->rect.y;
	return 1;
}

static int NativeHangout_TerminalFrontVisible(struct GameTracker *gGT)
{
	SVec3 world[3];
	SVec2 screen[3];
	s64 area;

	if (gGT == NULL || s_hangoutSaveScreen == NULL)
		return 0;

	/* STATIC_SCAN is an XY surface, not the save object's interaction-camera
	 * axis. Keep its text fixed to the glass: right=+X, down=-Y, front=+Z.
	 * Projected winding tests the actual camera side, including free camera. */
	NativeHangout_TransformTerminalPoint(s_hangoutSaveScreen,
		0, 0, NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET, &world[0]);
	NativeHangout_TransformTerminalPoint(s_hangoutSaveScreen,
		128, 0, NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET, &world[1]);
	NativeHangout_TransformTerminalPoint(s_hangoutSaveScreen,
		0, -128, NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET, &world[2]);
	for (int i = 0; i < 3; i++)
	{
		if (!NativeHangout_ProjectWorldPointRaw(gGT,
			world[i].x, world[i].y, world[i].z, &screen[i]))
			return 0;
	}
	area = (s64)(screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
		(s64)(screen[1].y - screen[0].y) * (screen[2].x - screen[0].x);
	return area > 0;
}

static void NativeHangout_DrawTerminalGlyph(
	struct GameTracker *gGT,
	struct Icon *icon,
	int localRight,
	int localDown,
	int localWidth,
	int localHeight,
	int colorIndex)
{
	SVec3 world[4];
	SVec2 screen[4];
	POLY_GT4 *p;
	u32 *color;
	int right[4] = {localRight, localRight + localWidth,
		localRight, localRight + localWidth};
	int down[4] = {localDown, localDown,
		localDown + localHeight, localDown + localHeight};

	if (gGT == NULL || s_hangoutSaveScreen == NULL || icon == NULL ||
		localWidth <= 0 || localHeight <= 0)
		return;

	for (int i = 0; i < 4; i++)
	{
		NativeHangout_TransformTerminalPoint(s_hangoutSaveScreen,
			right[i], -down[i], NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET, &world[i]);
		if (!NativeHangout_ProjectWorldPointRaw(gGT,
			world[i].x, world[i].y, world[i].z, &screen[i]))
			return;
	}

	p = (POLY_GT4 *)gGT->backBuffer->primMem.cursor;
	if ((unsigned char *)(p + 1) > (unsigned char *)gGT->backBuffer->primMem.end)
		return;
	color = data.ptrColor[colorIndex];
	setInt32RGB4(p, color[0], color[1], color[2], color[3]);
	addPolyGT4(gGT->pushBuffer_UI.ptrOT, p);
	setXY4CompilerHack(p,
		(u16)screen[0].x, screen[0].y,
		(u16)screen[1].x, screen[1].y,
		(u16)screen[2].x, screen[2].y,
		(u16)screen[3].x, screen[3].y);
	setIconUV(p, icon);
	gGT->backBuffer->primMem.cursor = p + 1;
}

static void NativeHangout_DrawTerminalLine(
	struct GameTracker *gGT,
	const char *text,
	int localDown,
	int baseScale,
	int colorIndex)
{
	s16 iconGroupID;
	struct Icon **icons;
	int cursorRight = NATIVE_HANGOUT_TERMINAL_TEXT_LEFT;

	if (gGT == NULL || text == NULL)
		return;
	iconGroupID = data.font_IconGroupID[FONT_SMALL];
	if (iconGroupID < 0 || gGT->iconGroup[iconGroupID] == NULL)
		return;
	icons = ICONGROUP_GETICONS(gGT->iconGroup[iconGroupID]);
	for (; *text != 0; text++)
	{
		unsigned char c = (unsigned char)*text;
		int drawScale = NativeHangout_ChatScaleForChar(c, baseScale);
		int advance = NativeHangout_ChatCharAdvance(c, baseScale);
		int lower = (c >= 'a' && c <= 'z') ? FP_Mult(2, baseScale) : 0;
		u16 iconID = 0xff;

		if (c >= 0x21)
			iconID = data.font_characterIconID[c - 0x21];
		if (iconID != 0xff && iconID < gGT->iconGroup[iconGroupID]->numIcons)
		{
			struct Icon *icon = icons[iconID];
			int glyphWidth = FP_Mult(icon->texLayout.u1 - icon->texLayout.u0, drawScale);
			int glyphHeight = FP_Mult(icon->texLayout.v2 - icon->texLayout.v0, drawScale);
			if (cursorRight + glyphWidth <= NATIVE_HANGOUT_TERMINAL_TEXT_LEFT +
				NATIVE_HANGOUT_TERMINAL_TEXT_MAX_WIDTH)
				NativeHangout_DrawTerminalGlyph(gGT, icon, cursorRight,
					localDown + lower, glyphWidth, glyphHeight, colorIndex);
		}
		cursorRight += advance;
	}
}

/* Ring of display rows: wrap complete messages instead of truncating them to
 * five letters, and keep the most recent rows when the physical panel fills. */
static int NativeHangout_WrapTerminalMessage(
	const char *text,
	char rows[NATIVE_HANGOUT_TERMINAL_TEXT_ROWS][NATIVE_CHAT_NAME_MAX + NATIVE_CHAT_TEXT_MAX + 4],
	int rowCount)
{
	while (text != NULL && *text != 0)
	{
		const char *start;
		const char *end;
		const char *lastSpace = NULL;
		const char *next;
		int width = 0;
		int length;
		char *row;

		while (*text == ' ' || *text == '\n' || *text == '\r') text++;
		if (*text == 0) break;
		start = end = text;
		while (*end != 0 && *end != '\n' && *end != '\r')
		{
			int advance = NativeHangout_ChatCharAdvance((unsigned char)*end,
				NATIVE_HANGOUT_TERMINAL_TEXT_SCALE);
			if (width + advance > NATIVE_HANGOUT_TERMINAL_TEXT_MAX_WIDTH) break;
			if (*end == ' ') lastSpace = end;
			width += advance;
			end++;
		}
		next = end;
		if (*end != 0 && *end != '\n' && *end != '\r' && lastSpace != NULL)
		{
			end = lastSpace;
			next = lastSpace + 1;
		}
		if (end == start) end = next = start + 1;
		while (end > start && end[-1] == ' ') end--;
		length = (int)(end - start);
		row = rows[rowCount % NATIVE_HANGOUT_TERMINAL_TEXT_ROWS];
		if (length >= NATIVE_CHAT_NAME_MAX + NATIVE_CHAT_TEXT_MAX + 4)
			length = NATIVE_CHAT_NAME_MAX + NATIVE_CHAT_TEXT_MAX + 3;
		memcpy(row, start, (size_t)length);
		row[length] = 0;
		rowCount++;
		text = next;
	}
	return rowCount;
}

static void NativeHangout_RenderLinkedTerminal(struct GameTracker *gGT)
{
	char rows[NATIVE_HANGOUT_TERMINAL_TEXT_ROWS][NATIVE_CHAT_NAME_MAX + NATIVE_CHAT_TEXT_MAX + 4];
	int rowCount = 0;
	int firstRow;
	int count;

	if (!NativeHangout_TerminalFrontVisible(gGT)) return;
	count = NativeChat_GetCount();
	for (int i = 0; i < count; i++)
	{
		char line[NATIVE_CHAT_NAME_MAX + NATIVE_CHAT_TEXT_MAX + 4];
		NativeHangout_FormatMessage(NativeChat_Get(i), line, sizeof(line));
		rowCount = NativeHangout_WrapTerminalMessage(line, rows, rowCount);
	}
	firstRow = rowCount - NATIVE_HANGOUT_TERMINAL_TEXT_ROWS;
	if (firstRow < 0) firstRow = 0;
	for (int row = firstRow; row < rowCount; row++)
		NativeHangout_DrawTerminalLine(gGT,
			rows[row % NATIVE_HANGOUT_TERMINAL_TEXT_ROWS],
			NATIVE_HANGOUT_TERMINAL_TEXT_TOP +
				(row - firstRow) * NATIVE_HANGOUT_TERMINAL_LINE_STEP,
			NATIVE_HANGOUT_TERMINAL_TEXT_SCALE, WHITE);
}

static void NativeHangout_RenderFloatingChat(struct GameTracker *gGT)
{
	struct Driver *driver;

	if (NativeHangout_ShouldHideGameplayHud())
		return;
	const struct NativeChatMessage *message;
	SVec2 screen;
	char line[NATIVE_CHAT_TEXT_MAX];
	char nameLine[NATIVE_CHAT_NAME_MAX + 4];

	driver = gGT->drivers[0];
	if (driver == NULL || driver->instSelf == NULL)
		return;

	if (!NativeHangout_ProjectInstance(
		gGT,
		driver->instSelf,
		NATIVE_HANGOUT_FLOATING_Y_OFFSET,
		&screen))
		return;

	snprintf(
		nameLine,
		sizeof(nameLine),
		"%s",
		NativeSession_GetLocalPlayerName());

	NativeHangout_FitChatLine(
		nameLine,
		112,
		0xD00);

	/* The player name is the persistent label directly above the kart. */
	NativeHangout_DrawChatLine(
		nameLine,
		screen.x,
		screen.y,
		0xD00,
		PERIWINKLE,
		1);

	message = NativeChat_GetLatestFromPlayer(
		NativeSession_GetLocalPlayerID(),
		NativeHangout_Tick(),
		NATIVE_HANGOUT_FLOATING_CHAT_TICKS);

	if (message == NULL)
		return;

	snprintf(
		line,
		sizeof(line),
		"%s",
		message->text);

	NativeHangout_FitChatLine(
		line,
		132,
		0xC00);

	/* Latest speech sits above the name and remains a camera-facing 2D label. */
	NativeHangout_DrawChatLine(
		line,
		screen.x,
		screen.y - 10,
		0xC00,
		WHITE,
		1);
}

static void NativeHangout_RenderHud(struct GameTracker *gGT)
{
	struct PushBuffer *pb = &gGT->pushBuffer[0];
	int count;
	int visibleCount;
	int first;
	int i;
	int y;
	int x;
	int maxLineWidth;
	int lineHeight = 7;

	/* Pause/options/customization owns the screen while open; do not stack
	 * Hangout chat behind it or behind the checkered-flag wipe. */
	if (NativeHangout_IsMenuOpen() || NativeHangout_ShouldHideGameplayHud())
		return;

	if (!NativeHangout_HudChatIsVisible())
		return;

	count = NativeChat_GetCount();
	visibleCount = count;
	if (visibleCount > NATIVE_HANGOUT_CHAT_VISIBLE)
		visibleCount = NATIVE_HANGOUT_CHAT_VISIBLE;

	first = count - visibleCount;
	x = pb->rect.x + 8;
	maxLineWidth = pb->rect.w - 16;
	if (maxLineWidth > 280)
		maxLineWidth = 280;

	y =
		pb->rect.y +
		pb->rect.h -
		8 -
		visibleCount * lineHeight -
		(s_hangoutChatOpen ? 9 : 0);

	if (y < pb->rect.y + 8)
		y = pb->rect.y + 8;

	/* Intentionally no RectMenu panel: HUD chat is only the text itself. */
	for (i = first; i < count; i++)
	{
		const struct NativeChatMessage *message =
			NativeChat_Get(i);
		char line[
			NATIVE_CHAT_NAME_MAX +
			NATIVE_CHAT_TEXT_MAX +
			4];

		NativeHangout_FormatMessage(
			message,
			line,
			sizeof(line));

		NativeHangout_FitChatLine(
			line,
			maxLineWidth,
			NATIVE_HANGOUT_HUD_CHAT_SCALE);

		NativeHangout_DrawChatLine(
			line,
			x,
			y,
			NATIVE_HANGOUT_HUD_CHAT_SCALE,
			WHITE,
			0);

		y += lineHeight;
	}

	if (s_hangoutChatOpen)
	{
		char inputLine[NATIVE_CHAT_TEXT_MAX + 4];

		snprintf(
			inputLine,
			sizeof(inputLine),
			"> %s_",
			s_hangoutInput);

		NativeHangout_FitChatLine(
			inputLine,
			maxLineWidth,
			NATIVE_HANGOUT_HUD_CHAT_SCALE);

		NativeHangout_DrawChatLine(
			inputLine,
			x,
			y + 1,
			NATIVE_HANGOUT_HUD_CHAT_SCALE,
			PERIWINKLE,
			0);
	}
}

static void NativeHangout_RenderRaceTestCountdown(struct GameTracker *gGT)
{
	struct PushBuffer *pb;
	unsigned int now;
	unsigned int elapsed;
	int seconds;
	char text[8];

	if (!s_hangoutRaceTestActive || gGT == NULL)
		return;

	pb = &gGT->pushBuffer[0];
	if (s_hangoutRaceTestZeroMs != 0)
	{
		seconds = 0;
	}
	else
	{
		now = (unsigned int)SDL_GetTicks();
		elapsed = now - s_hangoutRaceTestStartMs;
		if (elapsed >= NATIVE_HANGOUT_RACE_TEST_COUNTDOWN_MS)
			seconds = 0;
		else
			seconds =
				(NATIVE_HANGOUT_RACE_TEST_COUNTDOWN_MS - (int)elapsed + 999) /
				1000;
	}

	snprintf(text, sizeof(text), "%d", seconds);
	DecalFont_DrawLine(
		text,
		pb->rect.x + pb->rect.w / 2,
		pb->rect.y + 12,
		FONT_BIG,
		JUSTIFY_CENTER | ORANGE);
}

void NativeHangout_Render(struct GameTracker *gGT)
{
	if (!NativeHangout_IsActive() || gGT == NULL)
		return;

	if (gGT->levelID != N_SANITY_BEACH ||
	    (gGT->gameMode1 & ADVENTURE_ARENA) == 0)
		return;

	NativeHangout_RenderRaceTestCountdown(gGT);
	NativeHangout_RenderHud(gGT);
	NativeHangout_RenderLinkedTerminal(gGT);
	NativeHangout_RenderFloatingChat(gGT);
}

int NativeHangout_HandleHostKey(
	int scancode,
	int down,
	int repeat)
{
	if (!NativeHangout_IsActive())
		return 0;

	/* While a menu or its flag wipe owns the screen, Hangout chat must not
	 * steal keyboard input from navigation or leak input into gameplay. */
	if (NativeHangout_IsMenuOpen() ||
	    NativeHangout_IsCustomizationTransitionActive())
		return 0;

	if (!s_hangoutChatOpen)
	{
		if (scancode == SDL_SCANCODE_T &&
		    down &&
		    !repeat)
		{
			NativeHangout_OpenChat();
			return 1;
		}

		return 0;
	}

	/* Keep fullscreen available while text entry owns keyboard gameplay. */
	if (scancode == SDL_SCANCODE_F11)
		return 0;

	if (scancode == SDL_SCANCODE_ESCAPE)
	{
		if (down && !repeat)
			NativeHangout_CloseChat();

		return 1;
	}

	if (scancode == SDL_SCANCODE_RETURN ||
	    scancode == SDL_SCANCODE_KP_ENTER)
	{
		if (down && !repeat)
			NativeHangout_SendInput();

		return 1;
	}

	if (scancode == SDL_SCANCODE_BACKSPACE)
	{
		if (down && s_hangoutInputLen > 0)
		{
			s_hangoutInputLen--;

			while (
				s_hangoutInputLen > 0 &&
				(((unsigned char)s_hangoutInput[
					s_hangoutInputLen] & 0xc0) == 0x80))
			{
				s_hangoutInputLen--;
			}

			s_hangoutInput[
				s_hangoutInputLen] = 0;
		}

		return 1;
	}

	/* Only keyboard gameplay is blocked. Gamepads keep working normally. */
	return 1;
}

void NativeHangout_HandleTextInput(const char *text)
{
	size_t textLen;
	size_t room;

	if (!NativeHangout_IsActive() ||
	    !s_hangoutChatOpen ||
	    text == NULL ||
	    text[0] == 0)
		return;

	if (s_hangoutIgnoreOpenKeyText)
	{
		s_hangoutIgnoreOpenKeyText = 0;

		if ((text[0] == 't' ||
		     text[0] == 'T') &&
		    text[1] == 0)
		{
			return;
		}
	}

	textLen = strlen(text);
	room =
		sizeof(s_hangoutInput) -
		1 -
		(size_t)s_hangoutInputLen;

	if (textLen > room)
		textLen = room;

	if (textLen == 0)
		return;

	memcpy(
		&s_hangoutInput[s_hangoutInputLen],
		text,
		textLen);

	s_hangoutInputLen += (int)textLen;
	s_hangoutInput[s_hangoutInputLen] = 0;
}

#endif
