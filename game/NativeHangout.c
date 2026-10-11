#include <common.h>

#if defined(CTR_NATIVE)

#include <NativeChat.h>
#include <NativeHangout.h>
#include <NativeSession.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	NATIVE_HANGOUT_CHAT_VISIBLE = 4,
	NATIVE_HANGOUT_MAX_SPAWNS = 16,
	/* HUD chat appears on activity, then gets out of the way automatically. */
	NATIVE_HANGOUT_HUD_CHAT_TICKS = 30 * 5,
	NATIVE_HANGOUT_HUD_CHAT_SCALE = 0xB00,
	NATIVE_HANGOUT_FLOATING_CHAT_TICKS = 30 * 4,
	NATIVE_HANGOUT_FLOATING_Y_OFFSET = 0xC0,
	NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET = 6,
	NATIVE_HANGOUT_TERMINAL_TEXT_MAX_WIDTH = 300,
	NATIVE_HANGOUT_TERMINAL_LINE_STEP = 46,
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
static int s_hangoutIgnoreOpenKeyText;
static struct Instance *s_hangoutSaveScreen;
static char s_hangoutInput[NATIVE_CHAT_TEXT_MAX];
static int s_hangoutInputLen;
static unsigned int s_hangoutHudVisibleUntil;
static int s_hangoutLastChatCount;
static struct AdvProgress s_hangoutAdvBackup;
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
			s_hangoutAdvBackupValid = 0;
		}

		s_hangoutActive = 0;
		s_hangoutSpawnApplied = 0;
		s_hangoutSpawnCount = 0;
		s_hangoutSaveScreen = NULL;
		s_hangoutMenuOpen = 0;
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
	s_hangoutAdvBackupValid = 1;

	s_hangoutActive = 1;
	NativeHangout_SuppressAdventureMask();
	s_hangoutSpawnApplied = 0;
	s_hangoutSpawnCount = 0;
	s_hangoutSaveScreen = NULL;
	s_hangoutMenuOpen = 0;
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

int NativeHangout_ShouldBlockGameplayInput(void)
{
	return NativeHangout_IsMenuOpen();
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

	NativeHangout_ApplyPortalSpawnIndex(
		gGT,
		spawnIndex);
}

void NativeHangout_Update(struct GameTracker *gGT)
{
	if (!NativeHangout_IsActive() || gGT == NULL)
		return;

	/* Hangout never owns Adventure tutorial/hint masks. Keep this enforced
	 * continuously so a retail trigger cannot re-arm Aku/Uka later. */
	NativeHangout_SuppressAdventureMask();

	if (NativeChat_GetCount() != s_hangoutLastChatCount)
	{
		s_hangoutLastChatCount = NativeChat_GetCount();
		NativeHangout_ShowHudChat();
	}

	if (sdata->Loading.stage != LOAD_IDLE)
		return;

	/* Defensive repair: menu transitions normally clear this explicitly, but
	 * if a native/retail menu disappears through an alternate path, never leave
	 * the kart input locked forever. Keep ownership during active/desired/submenu
	 * transitions and release it only when the whole menu stack is gone. */
	if (s_hangoutMenuOpen &&
	    sdata->ptrActiveMenu == NULL &&
	    sdata->ptrDesiredMenu == NULL &&
	    sdata->activeSubMenu == NULL)
	{
		s_hangoutMenuOpen = 0;
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

static int NativeHangout_TerminalFrontVisible(
	struct GameTracker *gGT)
{
	struct Driver *driver;
	s64 dx;
	s64 dy;
	s64 dz;
	s64 facing;

	if (gGT == NULL || s_hangoutSaveScreen == NULL)
		return 0;

	driver = gGT->drivers[0];
	if (driver == NULL || driver->instSelf == NULL)
		return 0;

	dx =
		(s64)driver->instSelf->matrix.t[0] -
		s_hangoutSaveScreen->matrix.t[0];
	dy =
		(s64)driver->instSelf->matrix.t[1] -
		s_hangoutSaveScreen->matrix.t[1];
	dz =
		(s64)driver->instSelf->matrix.t[2] -
		s_hangoutSaveScreen->matrix.t[2];

	/* STATIC_SCAN faces local +X. Text only exists on that physical side. */
	facing =
		dx * s_hangoutSaveScreen->matrix.m[0][0] +
		dy * s_hangoutSaveScreen->matrix.m[1][0] +
		dz * s_hangoutSaveScreen->matrix.m[2][0];

	return facing > 0;
}

static int NativeHangout_GetTerminalTextAxes(
	struct GameTracker *gGT,
	int *rightY,
	int *rightZ,
	int *downY,
	int *downZ)
{
	SVec3 worldCenter;
	SVec3 worldY;
	SVec3 worldZ;
	SVec2 screenCenter;
	SVec2 screenY;
	SVec2 screenZ;
	int yDY;
	int zDX;

	if (gGT == NULL ||
	    s_hangoutSaveScreen == NULL ||
	    rightY == NULL ||
	    rightZ == NULL ||
	    downY == NULL ||
	    downZ == NULL)
		return 0;

	NativeHangout_TransformTerminalPoint(
		s_hangoutSaveScreen,
		NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET,
		0,
		0,
		&worldCenter);
	NativeHangout_TransformTerminalPoint(
		s_hangoutSaveScreen,
		NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET,
		64,
		0,
		&worldY);
	NativeHangout_TransformTerminalPoint(
		s_hangoutSaveScreen,
		NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET,
		0,
		64,
		&worldZ);

	if (!NativeHangout_ProjectWorldPointRaw(
		    gGT,
		    worldCenter.x,
		    worldCenter.y,
		    worldCenter.z,
		    &screenCenter) ||
	    !NativeHangout_ProjectWorldPointRaw(
		    gGT,
		    worldY.x,
		    worldY.y,
		    worldY.z,
		    &screenY) ||
	    !NativeHangout_ProjectWorldPointRaw(
		    gGT,
		    worldZ.x,
		    worldZ.y,
		    worldZ.z,
		    &screenZ))
		return 0;

	yDY = (int)screenY.y - (int)screenCenter.y;
	zDX = (int)screenZ.x - (int)screenCenter.x;

	/* Keep the chat glued to STATIC_SCAN's real panel plane, but rotate the
	 * entire text basis 90 degrees clockwise relative to the previous mapping.
	 * The old basis rendered the glyphs sideways (90 degrees left) in-game.
	 * Start from its signed axes, then apply: newRight = oldDown,
	 * newDown = -oldRight. */
	{
		const int oldRightZ = (zDX >= 0) ? 1 : -1;
		const int oldDownY = (yDY >= 0) ? 1 : -1;

		*rightY = oldDownY;
		*rightZ = 0;
		*downY = 0;
		*downZ = -oldRightZ;
	}

	return 1;
}

static void NativeHangout_DrawTerminalGlyph(
	struct GameTracker *gGT,
	struct Icon *icon,
	int localRight,
	int localDown,
	int localWidth,
	int localHeight,
	int colorIndex,
	int rightY,
	int rightZ,
	int downY,
	int downZ)
{
	SVec3 world[4];
	SVec2 screen[4];
	POLY_GT4 *p;
	u32 *color;
	int localX = NATIVE_HANGOUT_TERMINAL_FRONT_OFFSET;
	int right0 = localRight;
	int right1 = localRight + localWidth;
	int down0 = localDown;
	int down1 = localDown + localHeight;
	int localY[4];
	int localZ[4];

	if (gGT == NULL ||
	    s_hangoutSaveScreen == NULL ||
	    icon == NULL ||
	    localWidth <= 0 ||
	    localHeight <= 0)
		return;

	localY[0] = rightY * right0 + downY * down0;
	localZ[0] = rightZ * right0 + downZ * down0;
	localY[1] = rightY * right1 + downY * down0;
	localZ[1] = rightZ * right1 + downZ * down0;
	localY[2] = rightY * right0 + downY * down1;
	localZ[2] = rightZ * right0 + downZ * down1;
	localY[3] = rightY * right1 + downY * down1;
	localZ[3] = rightZ * right1 + downZ * down1;

	for (int i = 0; i < 4; i++)
	{
		NativeHangout_TransformTerminalPoint(
			s_hangoutSaveScreen,
			localX,
			localY[i],
			localZ[i],
			&world[i]);
	}

	for (int i = 0; i < 4; i++)
	{
		if (!NativeHangout_ProjectWorldPointRaw(
			    gGT,
			    world[i].x,
			    world[i].y,
			    world[i].z,
			    &screen[i]))
			return;
	}

	color = data.ptrColor[colorIndex];
	p = (POLY_GT4 *)gGT->backBuffer->primMem.cursor;
	setInt32RGB4(
		p,
		color[0],
		color[1],
		color[2],
		color[3]);
	addPolyGT4(gGT->pushBuffer_UI.ptrOT, p);
	setXY4CompilerHack(
		p,
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
	int colorIndex,
	int rightY,
	int rightZ,
	int downY,
	int downZ)
{
	s16 iconGroupID;
	struct Icon **icons;
	int lineWidth;
	int cursorRight;

	if (gGT == NULL || text == NULL)
		return;

	iconGroupID = data.font_IconGroupID[FONT_SMALL];
	if (iconGroupID < 0 ||
	    gGT->iconGroup[iconGroupID] == NULL)
		return;

	icons = ICONGROUP_GETICONS(
		gGT->iconGroup[iconGroupID]);
	lineWidth =
		NativeHangout_ChatLineWidthScaled(
			text,
			baseScale);
	cursorRight = -(lineWidth / 2);

	for (; *text != 0; text++)
	{
		unsigned char c = (unsigned char)*text;
		int drawScale =
			NativeHangout_ChatScaleForChar(
				c,
				baseScale);
		int advance =
			NativeHangout_ChatCharAdvance(
				c,
				baseScale);
		int lower =
			(c >= 'a' && c <= 'z') ? 2 : 0;
		u16 iconID = 0xff;

		if (c >= 0x21)
			iconID =
				data.font_characterIconID[
					c - 0x21];

		if (iconID != 0xff &&
		    iconID <
			    gGT->iconGroup[
				    iconGroupID]->numIcons)
		{
			struct Icon *icon = icons[iconID];
			int glyphWidth =
				FP_Mult(
					icon->texLayout.u1 -
						icon->texLayout.u0,
					drawScale);
			int glyphHeight =
				FP_Mult(
					icon->texLayout.v2 -
						icon->texLayout.v0,
					drawScale);

			NativeHangout_DrawTerminalGlyph(
				gGT,
				icon,
				cursorRight,
				localDown + lower,
				glyphWidth,
				glyphHeight,
				colorIndex,
				rightY,
				rightZ,
				downY,
				downZ);
		}

		cursorRight += advance;
	}
}

static void NativeHangout_RenderLinkedTerminal(
	struct GameTracker *gGT)
{
	/* This is a large physical display, not HUD text. Make the glyphs roughly
	 * twice the previous size so the chat is readable from normal Hangout
	 * driving distance. */
	const int textScale = 0x4800;
	int count;
	int first;
	int i;
	int lineIndex;
	int visibleCount;
	int rightY;
	int rightZ;
	int downY;
	int downZ;

	if (!NativeHangout_TerminalFrontVisible(gGT))
		return;

	if (!NativeHangout_GetTerminalTextAxes(
		    gGT,
		    &rightY,
		    &rightZ,
		    &downY,
		    &downZ))
		return;

	count = NativeChat_GetCount();
	first = count - NATIVE_HANGOUT_CHAT_VISIBLE;
	if (first < 0)
		first = 0;
	visibleCount = count - first;

	lineIndex = 0;
	for (i = first; i < count; i++, lineIndex++)
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
			NATIVE_HANGOUT_TERMINAL_TEXT_MAX_WIDTH,
			textScale);

		NativeHangout_DrawTerminalLine(
			gGT,
			line,
			lineIndex * NATIVE_HANGOUT_TERMINAL_LINE_STEP -
				((visibleCount - 1) * NATIVE_HANGOUT_TERMINAL_LINE_STEP) / 2,
			textScale,
			WHITE,
			rightY,
			rightZ,
			downY,
			downZ);
	}
}

static void NativeHangout_RenderFloatingChat(struct GameTracker *gGT)
{
	struct Driver *driver;
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

	/* Pause/options owns the screen while open; do not stack chat behind it. */
	if (NativeHangout_IsMenuOpen())
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

void NativeHangout_Render(struct GameTracker *gGT)
{
	if (!NativeHangout_IsActive() || gGT == NULL)
		return;

	if (gGT->levelID != N_SANITY_BEACH ||
	    (gGT->gameMode1 & ADVENTURE_ARENA) == 0)
		return;

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

	/* While the live pause/menu overlay is open, keyboard input belongs to
	 * RECTMENU/Options. Hangout chat must not steal keys from menu navigation. */
	if (NativeHangout_IsMenuOpen())
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
