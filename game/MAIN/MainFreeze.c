#include <common.h>

#ifdef CTR_NATIVE
#include "platform/native_log.h"
static int s_titleOptionsOpen;
static s16 s_titleOptionsReturnRow;
static u32 s_titleOptionsSavedState;
static struct RectMenu *s_titleOptionsOwner; // CTR_OPTIONS_V43
static int s_titleOptionsNeedsCursorReset;
static int s_titleVoicePreviewSound;
static int s_titleVoicePreviewDelay;

enum NativeOptionsPage
{
	NATIVE_OPTIONS_PAGE_ROOT = 0,
	NATIVE_OPTIONS_PAGE_AUDIO = 1,
	NATIVE_OPTIONS_PAGE_GRAPHICS = 2,
	NATIVE_OPTIONS_PAGE_CONTROLS = 3,
};

enum
{
	NATIVE_OPTIONS_TRANSITION_FRAMES = 12,
	NATIVE_OPTIONS_ROOT_ROW_COUNT = 4,
	NATIVE_OPTIONS_AUDIO_ROW_COUNT = 5,
	NATIVE_OPTIONS_GRAPHICS_ROW_COUNT = 7,
};

static int s_nativeOptionsSessionOpen;
static int s_nativeOptionsPage;
static int s_nativeOptionsTargetPage;
static int s_nativeOptionsTransitionActive;
static int s_nativeOptionsTransitionDirection;
static int s_nativeOptionsTransitionFrame;
static int s_nativeOptionsEntryActive;
static int s_nativeOptionsEntryFrame;
static int s_nativeOptionsExitActive;
static int s_nativeOptionsExitFrame;
static s16 s_nativeOptionsRows[4];
static struct TransitionMeta s_nativeOptionsSlideMeta[2] = {
	{-512, 0, 0, 0, 0},
	{0, 0, -1, 0, 0},
};

static void MainFreeze_TitleOptionsStopVoicePreview(void)
{
	if (s_titleVoicePreviewSound)
	{
		OtherFX_Stop1(s_titleVoicePreviewSound);
		s_titleVoicePreviewSound = 0;
	}
	s_titleVoicePreviewDelay = 0;
}

void MainFreeze_OpenTitleOptions(struct RectMenu *menu)
{
	s_titleOptionsReturnRow = menu->rowSelected;
	s_titleOptionsSavedState = menu->state;
	s_titleOptionsOwner = menu;
	s_titleOptionsOpen = 1;
	s_nativeOptionsSessionOpen = 0;
	// The main-menu input handler writes rowSelected again after this callback.
	// Reset the Options cursor at the start of the following update instead.
	s_titleOptionsNeedsCursorReset = 1;
	MainFreeze_TitleOptionsStopVoicePreview();
	OptionsMenu_TestSound(0, 0);
	menu->state |= ONLY_DRAW_TITLE | INVISIBLE;
	sdata->gGT->demoCountdownTimer = TITLE_DEMO_IDLE_FRAMES;
	RECTMENU_ClearInput();
}

int MainFreeze_TitleOptionsIsOpen(void)
{
	return s_titleOptionsOpen;
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800379f4-0x80037bc0.
void MainFreeze_ConfigDrawNPC105(s16 startX, s16 startY, s16 radius, int angleStep, s16 angle, char *color, uint32_t *otMem, struct PrimMem *primMem)
{
	s16 pos[6];
	char colors[0xc];

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			colors[(i * 4) + j] = color[j];
		}
	}

	pos[0] = startX;
	pos[1] = startY;

	int scaledRadiusX = (radius << 3) / 5;
	int currAngleStep = 0;

	while (true)
	{
		u32 currAngle = (u16)(currAngleStep + angle);

		pos[4] = startX + (s16)((scaledRadiusX * MATH_Cos(currAngle)) >> 0xc);
		pos[5] = startY + (s16)((radius * MATH_Sin(currAngle)) >> 0xc);

		if ((s16)currAngleStep != 0)
		{
			RECTMENU_DrawRwdTriangle(pos, colors, otMem, primMem);
		}

		currAngleStep = (s16)(currAngleStep + angleStep);

		pos[2] = pos[4];
		pos[3] = pos[5];

		if ((s16)currAngleStep > 0x1000)
		{
			return;
		}
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80037bc0-0x80037da0.
void MainFreeze_ConfigDrawArrows(s16 offsetX, s16 offsetY, char *str)
{
	int lineWidth;
	int color;
	u32 *colorPtr;
	struct GameTracker *gGT = sdata->gGT;

	// orange color
	color = 0;

	if ((sdata->frameCounter & 4) == 0)
	{
		// red color
		color = 3;
	}

	lineWidth = DecalFont_GetLineWidth(str, 1) >> 1;

	// get color data
	colorPtr = data.ptrColor[color];

	struct Icon **iconPtrArray = ICONGROUP_GETICONS(gGT->iconGroup[4]);

	// Draw left arrow
	DecalHUD_Arrow2D(
	    // largeFont
	    iconPtrArray[0x38],

	    (offsetX - lineWidth) - 0x14, (int)offsetY + 7,

	    // pointer to PrimMem struct
	    &gGT->backBuffer->primMem,

	    // pointer to OT memory
	    gGT->pushBuffer_UI.ptrOT,

	    // color data
	    colorPtr[0], colorPtr[1], colorPtr[2], colorPtr[3],

	    0, FP(1.0), 0x800);

	// Draw right arrow
	DecalHUD_Arrow2D(
	    // largeFont
	    iconPtrArray[0x38],

	    (offsetX + lineWidth) + 0x12, (int)offsetY + 7,

	    // pointer to PrimMem struct
	    &gGT->backBuffer->primMem,

	    // pointer to OT memory
	    gGT->pushBuffer_UI.ptrOT,

	    // color data
	    colorPtr[0], colorPtr[1], colorPtr[2], colorPtr[3],

	    0, FP(1.0), 0);

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80037da0-0x80038b5c.
static inline void MainFreeze_ConfigDrawWire(s16 x1, s16 y1, s16 x2, s16 y2, u8 r, u8 g, u8 b, void *ot)
{
	CTR_Box_DrawWirePrims(MakePoint(x1, y1), MakePoint(x2, y2), MakeColor(r, g, b), ot);
}

static inline void MainFreeze_ConfigDrawRaceWheel(int value, struct GameTracker *gGT)
{
	s16 triangle[8];
	RECT rect;

	for (int i = 0; i < 3; i++)
	{
		int sin = MATH_Sin(value);
		void *ot = gGT->pushBuffer_UI.ptrOT;

		if ((i != 1) && (value == 0x600))
		{
			ot = (void *)((intptr_t)ot + 0xc);
		}

		s16 y = sdata->analogConfigY[0] + ((sin * (i - 1) * 0x20) >> 0xc) + 0x20;
		MainFreeze_ConfigDrawWire(0xe2, y, 0x11e, y, 0, 0xff, 0, ot);
	}

	for (int tri = 0; tri < 2; tri++)
	{
		u32 wave = ((u32)sdata->frameCounter << 6) + (tri * 0x800);
		int sin = MATH_Sin(wave);
		int angle = (value * sin) >> 0xc;
		int angleSin = MATH_Sin(angle);

		for (int point = 0; point < 3; point++)
		{
			int base = tri * 6 + point * 2;
			triangle[point * 2] = data.raceConfig_unk80084290[base + 2] + ((tri == 0) ? 0x114 : 0xec);
			triangle[(point * 2) + 1] = sdata->analogConfigY[0] + ((angleSin << 5) >> 0xc) + 0x20 + data.raceConfig_unk80084290[base + 3];
		}

		RECTMENU_DrawRwdTriangle(triangle, (char *)data.raceConfig_colors_arrows, gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);
	}

	rect.x = 0xec;
	rect.y = sdata->analogConfigY[0];
	rect.w = 0x28;
	rect.h = 0x41;
	RECTMENU_DrawRwdBlueRect(&rect, (char *)data.raceConfig_colors_blueRect, gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);

	rect.x = -0x14;
	rect.y = sdata->analogConfigY[0] - 0x14;
	rect.w = 0x228;
	rect.h = 0x91;
	RECTMENU_DrawInnerRect(&rect, 4, gGT->pushBuffer_UI.ptrOT);
}

static inline void MainFreeze_ConfigDrawNamco(int value, struct GameTracker *gGT)
{
	int mirrorValue = -value;
	RECT rect;

	for (int i = 0; i < 2; i++)
	{
		int currValue = (i == 0) ? mirrorValue : value;
		u32 angle = (currValue - 0x400) & 0xfff;
		int sin = MATH_Sin(angle);
		int cos = MATH_Cos(angle);

		MainFreeze_ConfigDrawWire(0x100 + ((cos * 400) / 0x5000), sdata->analogConfigY[1] + ((sin * 0x32) >> 0xc), 0x100 + ((cos * 0x118) / 0x5000),
		                          sdata->analogConfigY[1] + ((sin * 0x23) >> 0xc), 0, 0xff, 0, gGT->pushBuffer_UI.ptrOT);
	}

	u32 frameAngle = (u32)sdata->frameCounter << 6;
	u32 baseAngle = (((MATH_Sin(frameAngle) * value) >> 0xc) - 0x400) & 0xfff;
	int baseSin = MATH_Sin(baseAngle);
	int baseCos = MATH_Cos(baseAngle);

	for (int point = 0; point < 3; point++)
	{
		int offset = point * 2;
		int colorOffset = point * 4;
		MainFreeze_ConfigDrawNPC105(data.unkNamcoGamepad_800842DC[offset] + ((baseCos * 200) / 0x5000) + 0x100,
		                            data.unkNamcoGamepad_800842DC[offset + 1] + sdata->analogConfigY[1] + ((baseSin * 0x19) >> 0xc), 10, 0x80, baseAngle,
		                            (char *)&data.unkNamcoGamepadRwdTriangleColors[colorOffset], gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);
	}

	for (int row = 0; row < 0x400; row += 0xaa)
	{
		for (int angle = 0; angle < 0x1000; angle += 0x400)
		{
			u8 color = (row != 0) ? 0x50 : 0x32;
			u32 currAngle = (baseAngle + angle + row) & 0xfff;
			int sin = MATH_Sin(currAngle);
			int cos = MATH_Cos(currAngle);

			MainFreeze_ConfigDrawWire(0x100 + ((cos < 0 ? cos + 0x3f : cos) >> 6), sdata->analogConfigY[1] + ((sin * 0x28) >> 0xc),
			                          0x100 + ((cos * 0x120) / 0x5000), sdata->analogConfigY[1] + ((sin * 0x24) >> 0xc), color, color, color,
			                          gGT->pushBuffer_UI.ptrOT);
		}
	}

	for (u16 row = 0; row < 3; row++)
	{
		int rowOffset = row * 2;
		for (int point = 0; point < 3; point++)
		{
			int pointOffset = point * 2;
			s16 scale = data.unkNamcoGamepad_800842DC[rowOffset + 7];
			MainFreeze_ConfigDrawNPC105(data.unkNamcoGamepad_800842DC[pointOffset + 18] * scale + 0x100,
			                            sdata->analogConfigY[1] + (data.unkNamcoGamepad_800842DC[pointOffset + 19] * scale),
			                            data.unkNamcoGamepad_800842DC[rowOffset + 6], 0x80, baseAngle, (char *)&data.unkNamcoGamepad_800842DC[pointOffset + 12],
			                            gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);
		}
	}

	rect.x = -0x14;
	rect.y = sdata->analogConfigY[1] - 0x3c;
	rect.w = 0x228;
	rect.h = 0xa0;
	RECTMENU_DrawInnerRect(&rect, 4, gGT->pushBuffer_UI.ptrOT);
}

void MainFreeze_ConfigSetupEntry(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if ((sdata->AnyPlayerTap & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0)
	{
		sdata->boolOpenWheelConfig = false;
		return;
	}

	int gamepadID = sdata->gamepadID_OwnerRaceWheelConfig;
	struct GamepadBuffer *gamepad = &sdata->gGamepads->gamepad[gamepadID];
	struct ControllerPacket *controller = gamepad->ptrControllerPacket;

	if ((controller == NULL) || (controller->plugged != PLUGGED))
	{
		sdata->boolOpenWheelConfig = false;
		return;
	}

	int isNamco = controller->controllerData == ((PAD_ID_JOGCON << 4) | 3);
	int posIndex = isNamco * 2;

	if (sdata->raceWheelConfigPageIndex == 1)
	{
		u32 tap = sdata->buttonTapPerPlayer[gamepadID];

		if ((tap & (BTN_UP | BTN_LEFT)) != 0)
		{
			sdata->WheelConfigOption--;
			if ((s16)sdata->WheelConfigOption < 0)
			{
				sdata->WheelConfigOption = 3;
			}
		}
		else if ((tap & (BTN_DOWN | BTN_RIGHT)) != 0)
		{
			sdata->WheelConfigOption++;
			if ((s16)sdata->WheelConfigOption > 3)
			{
				sdata->WheelConfigOption = 0;
			}
		}
		else if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) != 0)
		{
			sdata->raceWheelConfigPageIndex = 2;
			data.rwd[gamepadID].deadZone = data.raceConfig_DeadZone[(s16)sdata->WheelConfigOption].hi1;
		}

		DecalFont_DrawMultiLine(sdata->lngStrings[LNG_SELECT_DEAD_ZONE_AND_PRESS_BUTTON], 0x100, sdata->posY_MultiLine[posIndex], 0x1cc, FONT_BIG,
		                        JUSTIFY_CENTER);
		DecalFont_DrawLine(sdata->lngStrings[data.raceConfig_DeadZone[(s16)sdata->WheelConfigOption].lngIndex], 0x100, sdata->posY_Arrows[posIndex], FONT_BIG,
		                   JUSTIFY_CENTER);
		MainFreeze_ConfigDrawArrows(0x100, sdata->posY_Arrows[posIndex], sdata->lngStrings[data.raceConfig_DeadZone[(s16)sdata->WheelConfigOption].lngIndex]);
		sdata->unk_RaceWheelConfig[0] = data.raceConfig_DeadZone[(s16)sdata->WheelConfigOption].lo16;
	}
	else if (sdata->raceWheelConfigPageIndex < 2)
	{
		if (sdata->raceWheelConfigPageIndex == 0)
		{
			DecalFont_DrawMultiLine(sdata->lngStrings[LNG_CENTER_THE_CONTROLLER_AND_PRESS_BUTTON], 0x100, sdata->posY_MultiLine[posIndex], 0x1cc, FONT_BIG,
			                        JUSTIFY_CENTER);

			if ((sdata->buttonTapPerPlayer[gamepadID] & (BTN_CIRCLE | BTN_CROSS_one)) != 0)
			{
				sdata->raceWheelConfigPageIndex++;
				if (!isNamco)
				{
					data.rwd[gamepadID].gamepadCenter = controller->analog.rightX;
				}
				else
				{
					gamepad->unk44 = 4;
					data.rwd[sdata->gamepadID_OwnerRaceWheelConfig].gamepadCenter = 0x80;
				}
				RECTMENU_ClearInput();
			}

			sdata->unk_RaceWheelConfig[0] = 0;
		}
	}
	else if (sdata->raceWheelConfigPageIndex == 2)
	{
		u32 tap = sdata->buttonTapPerPlayer[gamepadID];

		if ((tap & (BTN_UP | BTN_LEFT)) != 0)
		{
			sdata->raceWheelConfigOptionIndex--;
			if ((s16)sdata->raceWheelConfigOptionIndex < 0)
			{
				sdata->raceWheelConfigOptionIndex = data.raceConfig_unk80084290[isNamco];
			}
		}
		else if ((tap & (BTN_DOWN | BTN_RIGHT)) != 0)
		{
			sdata->raceWheelConfigOptionIndex++;
			if ((s16)data.raceConfig_unk80084290[isNamco] < (s16)sdata->raceWheelConfigOptionIndex)
			{
				sdata->raceWheelConfigOptionIndex = 0;
			}
		}
		else if ((tap & (BTN_CIRCLE | BTN_CROSS_one)) != 0)
		{
			sdata->boolOpenWheelConfig = false;
			data.rwd[gamepadID].range = data.raceConfig_Range[(s16)sdata->raceWheelConfigOptionIndex].hi1;
			RECTMENU_ClearInput();
		}

		sdata->unk_RaceWheelConfig[0] = data.raceConfig_Range[(s16)sdata->raceWheelConfigOptionIndex].lo16;
		DecalFont_DrawMultiLine(sdata->lngStrings[LNG_SELECT_RANGE_AND_PRESS_BUTTON], 0x100, sdata->posY_MultiLine[posIndex], 0x1cc, FONT_BIG, JUSTIFY_CENTER);
		DecalFont_DrawLine(sdata->lngStrings[data.raceConfig_Range[(s16)sdata->raceWheelConfigOptionIndex].lngIndex], 0x100, sdata->posY_Arrows[posIndex],
		                   FONT_BIG, JUSTIFY_CENTER);
		MainFreeze_ConfigDrawArrows(0x100, sdata->posY_Arrows[posIndex],
		                            sdata->lngStrings[data.raceConfig_Range[(s16)sdata->raceWheelConfigOptionIndex].lngIndex]);
	}

	if (!isNamco)
	{
		MainFreeze_ConfigDrawRaceWheel(sdata->unk_RaceWheelConfig[0], gGT);
	}
	else
	{
		MainFreeze_ConfigDrawNamco(sdata->unk_RaceWheelConfig[0], gGT);
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80038b5c-0x80039908.

#ifdef CTR_NATIVE
enum
{
	MAINFREEZE_NATIVE_ROW_SCREEN = 8,
	MAINFREEZE_NATIVE_ROW_HIGH_REFRESH = 9,
	MAINFREEZE_NATIVE_ROW_SUBPIXEL = 10,
	MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL = 11,
	MAINFREEZE_NATIVE_ROW_EXIT = 12,
};
#endif

typedef struct
{
	int numGamepads;
	int numAnalogs;
	int gamepadId[4];
	int analogId[4];
	int isGamepadAnalog[4];
	int menuRowsToRemove;
} GAMEPAD_MainFreeze_MenuPtrOptions;

force_inline void IDENTIFYGAMEPADS_MainFreeze_MenuPtrOptions(struct RectMenu *menu, GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	(void)menu;
	struct GameTracker *gGT = sdata->gGT;

	// get number of ordinary gamepads and/or "analog controllers" connected, and which players are using which

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		struct ControllerPacket *ptrControllerPacket = sdata->gGamepads->gamepad[i].ptrControllerPacket;

		// if gamepad is not an "analog controller", as CTR uses to refer to jogcons and negcons
		if (
		    // this function needs to display menu graphics for digital controllers at all times,
		    // even if they're unplugged; this is simply an additional check for if
		    // an "analog controller" is connected or not
		    ((ptrControllerPacket == 0) || (ptrControllerPacket->plugged != PLUGGED)) ||

		    ((ptrControllerPacket->controllerData != ((PAD_ID_JOGCON << 4) | 3)) && (ptrControllerPacket->controllerData != ((PAD_ID_NEGCON << 4) | 3))))
		{
			gamepad->gamepadId[gamepad->numGamepads] = i;
			gamepad->isGamepadAnalog[i] = false;
			gamepad->numGamepads++;
		}
		else
		{
			gamepad->analogId[gamepad->numAnalogs] = i;
			gamepad->isGamepadAnalog[i] = true;
			gamepad->numAnalogs++;
		}
	}

	// the menu buttons for configuring dualshocks and analog controllers are accompanied by labels each
	// these labels can appear at once
	b32 areBothControllerLabelsNecessary = false;
	if (gamepad->numGamepads != 0)
	{
		areBothControllerLabelsNecessary = (gamepad->numAnalogs != 0);
	}

	// set amount of menu rows to hide/remove
	// used for the dualshock and/or "analog" rows which are variable

	// in singleplayer, regardless of gamepad, 2 of these rows are always visible
	// (dualshock label + 1 gamepad)

	// with 4 regular gamepads connected, in multiplayer, there's 5 rows visible
	// (dualshock label + 4 gamepads)

	// maximum amount of rows is 6, which happens if there's 4 controllers and one of them is analog
	// (dualshock label + analog label + 4 gamepads)

	// in the last scenario, menuRowsToRemove will equal -1
	gamepad->menuRowsToRemove = (4 - areBothControllerLabelsNecessary) - gGT->numPlyrCurrGame;
}

#ifdef CTR_NATIVE
static void MainFreeze_NativeOptionsReset(void)
{
	s_nativeOptionsSessionOpen = 1;
	s_nativeOptionsPage = NATIVE_OPTIONS_PAGE_ROOT;
	s_nativeOptionsTargetPage = NATIVE_OPTIONS_PAGE_ROOT;
	s_nativeOptionsTransitionActive = 0;
	s_nativeOptionsTransitionDirection = 1;
	s_nativeOptionsTransitionFrame = 0;
	s_nativeOptionsEntryActive = 1;
	s_nativeOptionsEntryFrame = 0;
	s_nativeOptionsExitActive = 0;
	s_nativeOptionsExitFrame = 0;
	memset(s_nativeOptionsRows, 0, sizeof(s_nativeOptionsRows));
	s_nativeOptionsSlideMeta[0].distX = -512;
	s_nativeOptionsSlideMeta[0].distY = 0;
	s_nativeOptionsSlideMeta[0].headStart = 0;
	s_nativeOptionsSlideMeta[0].currX = 0;
	s_nativeOptionsSlideMeta[0].currY = 0;
}

static void MainFreeze_NativeOptionsBeginTransition(int targetPage, int direction)
{
	if (s_nativeOptionsTransitionActive || targetPage == s_nativeOptionsPage)
		return;

	MainFreeze_TitleOptionsStopVoicePreview();
	OptionsMenu_TestSound(0, 0);
	RECTMENU_ClearInput();

	s_nativeOptionsTargetPage = targetPage;
	s_nativeOptionsTransitionDirection = direction < 0 ? -1 : 1;
	s_nativeOptionsTransitionFrame = 0;
	s_nativeOptionsTransitionActive = 1;
	s_nativeOptionsSlideMeta[0].distX = (s16)(-512 * s_nativeOptionsTransitionDirection);
	s_nativeOptionsSlideMeta[0].currX = 0;
	s_nativeOptionsSlideMeta[0].currY = 0;
}

static void MainFreeze_NativeOptionsBeginExit(void)
{
	if (s_nativeOptionsExitActive)
		return;

	MainFreeze_TitleOptionsStopVoicePreview();
	OptionsMenu_TestSound(0, 0);
	RECTMENU_ClearInput();

	s_nativeOptionsExitActive = 1;
	s_nativeOptionsExitFrame = 0;
	s_nativeOptionsSlideMeta[0].distX = 512;
	s_nativeOptionsSlideMeta[0].currX = 0;
	s_nativeOptionsSlideMeta[0].currY = 0;
}

static void MainFreeze_NativeOptionsMoveRow(int page, int rowCount, int direction)
{
	int row = s_nativeOptionsRows[page] + direction;
	if (row < 0) row = rowCount - 1;
	if (row >= rowCount) row = 0;
	s_nativeOptionsRows[page] = (s16)row;
	MainFreeze_TitleOptionsStopVoicePreview();
	OptionsMenu_TestSound(0, 0);
	OtherFX_Play(0, 1);
}

static void MainFreeze_NativeOptionsDrawFrame(char *title, int offsetX, int height)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	Color color;

	DecalFont_DrawLine(title, 256 + offsetX, 30, FONT_BIG, JUSTIFY_CENTER | ORANGE);

	RECT separator = {.x = 66 + offsetX, .y = 48, .w = 380, .h = 2};
	color.self = sdata->battleSetup_Color_UI_1;
	RECTMENU_DrawOuterRect_Edge(&separator, color, 0x20, ot);

	RECT panel = {.x = 56 + offsetX, .y = 20, .w = 400, .h = height};
	RECTMENU_DrawInnerRect(&panel, 4, ot);
}

static void MainFreeze_NativeOptionsDrawCursor(int offsetX, int y, int height)
{
	RECT cursor = {.x = 74 + offsetX, .y = y, .w = 364, .h = height};
	CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, sdata->gGT->backBuffer->otMem.uiOT);
}

static void MainFreeze_NativeOptionsDrawRoot(int offsetX)
{
	static char *labels[NATIVE_OPTIONS_ROOT_ROW_COUNT] = {
		"AUDIO",
		"GRAPHICS",
		"CONTROLS",
		"EXIT",
	};

	for (int i = 0; i < NATIVE_OPTIONS_ROOT_ROW_COUNT; i++)
	{
		const int y = 67 + (i * 22);
		DecalFont_DrawLine(labels[i], 256 + offsetX, y, FONT_BIG, JUSTIFY_CENTER | ORANGE);
	}

	MainFreeze_NativeOptionsDrawCursor(offsetX, 65 + (s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ROOT] * 22), 19);
	MainFreeze_NativeOptionsDrawFrame("OPTIONS", offsetX, 145);
}

static void MainFreeze_NativeOptionsDrawSlider(int offsetX, int y, int volume)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	struct PrimMem *primMem = &gGT->backBuffer->primMem;
	const int left = 190 + offsetX;
	const int width = 220;
	const int marker = left + ((volume * (width - 5)) / 255);

	/* Keep the exact retail primitive order. The PS1 ordering table renders
	   these in reverse insertion order: marker fill/outline are queued first,
	   then the wedge, so the small cursor remains visible on top of the bar. */
	RECT fill = {.x = marker + 1, .y = y - 2, .w = 3, .h = 10};
	Color fillColor = *(Color *)(data.Options_VolumeSlider_Colors + 0xc);
	CTR_Box_DrawSolidBox(&fill, fillColor, ot);

	RECT outline = {.x = marker, .y = y - 3, .w = 5, .h = 12};
	Color outlineColor = *(Color *)(data.Options_VolumeSlider_Colors + 0x10);
	CTR_Box_DrawSolidBox(&outline, outlineColor, ot);

	s16 triangle[8] = {
		(s16)left, (s16)(y + 8),
		(s16)(left + width), (s16)(y - 2),
		(s16)(left + width), (s16)(y + 8),
		0, 0
	};
	RECTMENU_DrawRwdTriangle(triangle, data.Options_VolumeSlider_Colors, ot, primMem);
}

static void MainFreeze_NativeOptionsDrawAudio(int offsetX)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_AUDIO];

	for (int i = 0; i < 3; i++)
	{
		const int y = 62 + (i * 18);
		DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Audio[i]], 78 + offsetX, y, FONT_SMALL, ORANGE);
		MainFreeze_NativeOptionsDrawSlider(offsetX, y, howl_VolumeGet(i) & 0xff);
	}

	DecalFont_DrawLine(sdata->lngStrings[LNG_MODE], 78 + offsetX, 116, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(sdata->lngStrings[333 + howl_ModeGet()], 434 + offsetX, 116, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	DecalFont_DrawLine("BACK", 78 + offsetX, 134, FONT_SMALL, ORANGE);

	MainFreeze_NativeOptionsDrawCursor(offsetX, 59 + (row * 18), 14);
	MainFreeze_NativeOptionsDrawFrame("AUDIO", offsetX, 145);
}

static void MainFreeze_NativeOptionsDrawGraphics(int offsetX)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GRAPHICS];
	char refreshText[24];
	char resolutionText[32];
	char *detailText;
	int resolutionIndex = Platform_GetResolutionPresetIndex();
	int resolutionW = 0;
	int resolutionH = 0;

	if (resolutionIndex >= 0)
		Platform_GetResolutionPresetSize(resolutionIndex, &resolutionW, &resolutionH);
	else
		Platform_GetWindowSize(&resolutionW, &resolutionH);

	snprintf(resolutionText, sizeof(resolutionText), "%dx%d", resolutionW, resolutionH);

	DecalFont_DrawLine("RESOLUTION", 78 + offsetX, 54, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(resolutionText, 434 + offsetX, 54, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("DISPLAY MODE", 78 + offsetX, 72, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(Platform_GetFullscreenMode() ? "FULLSCREEN" : "WINDOWED", 434 + offsetX, 72, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("ASPECT", 78 + offsetX, 90, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(Platform_GetWideMode() ? "16:9" : "4:3", 434 + offsetX, 90, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("HIGH REFRESH", 78 + offsetX, 108, FONT_SMALL, ORANGE);
	if (Platform_GetHighRefreshMode())
		snprintf(refreshText, sizeof(refreshText), "%d FPS", Platform_GetHighRefreshTargetFPS());
	else
		snprintf(refreshText, sizeof(refreshText), "OFF");
	DecalFont_DrawLine(refreshText, 434 + offsetX, 108, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("SUBPIXEL", 78 + offsetX, 126, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(Platform_GetSubpixelMode() ? "ON" : "OFF", 434 + offsetX, 126, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("CHARACTER DETAIL", 78 + offsetX, 144, FONT_SMALL, ORANGE);
	{
		const int detailMode = Platform_GetCharacterDetailMode();
		detailText = detailMode == 0 ? "LOW" : (detailMode == 1 ? "SMART" : (detailMode == 2 ? "HIGH" : "ULTRA"));
	}
	DecalFont_DrawLine(detailText, 434 + offsetX, 144, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("BACK", 78 + offsetX, 162, FONT_SMALL, ORANGE);

	MainFreeze_NativeOptionsDrawCursor(offsetX, 51 + (row * 18), 14);
	MainFreeze_NativeOptionsDrawFrame("GRAPHICS", offsetX, 177);
}

static int MainFreeze_NativeOptionsControlsRowCount(const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	return gamepad->numGamepads + gamepad->numAnalogs + 1;
}

static void MainFreeze_NativeOptionsDrawControls(int offsetX, const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	struct GameTracker *gGT = sdata->gGT;
	int rowY = 62;
	int visibleRow = 0;
	int multitapStringOffset = (sdata->gGamepads->slotBuffer[0].controllerData == (PAD_ID_MULTITAP << 4)) ? 2 : 0;

	for (int i = 0; i < gamepad->numGamepads; i++, visibleRow++)
	{
		const int currPad = gamepad->gamepadId[i];
		int color = ORANGE;
		struct ControllerPacket *packet = sdata->gGamepads->gamepad[currPad].ptrControllerPacket;
		if ((packet == NULL) || (packet->plugged != PLUGGED))
			color = GRAY;

		DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Gamepads[currPad + multitapStringOffset]],
		                   78 + offsetX, rowY + visibleRow * 18, FONT_SMALL, color);

		const b32 vibrationOff = (gGT->gameMode1 & data.gGT_gameMode1_VibPerPlayer[currPad]) != 0;
		DecalFont_DrawLine(sdata->lngStrings[325 + vibrationOff], 434 + offsetX,
		                   rowY + visibleRow * 18, FONT_SMALL, JUSTIFY_RIGHT | (color == GRAY ? GRAY : WHITE));
	}

	for (int i = 0; i < gamepad->numAnalogs; i++, visibleRow++)
	{
		const int currPad = gamepad->analogId[i];
		DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Gamepads[currPad + multitapStringOffset]],
		                   78 + offsetX, rowY + visibleRow * 18, FONT_SMALL, ORANGE);
		DecalFont_DrawLine("CONFIGURE", 434 + offsetX, rowY + visibleRow * 18,
		                   FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	}

	DecalFont_DrawLine("BACK", 78 + offsetX, rowY + visibleRow * 18, FONT_SMALL, ORANGE);

	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_CONTROLS];
	MainFreeze_NativeOptionsDrawCursor(offsetX, 59 + row * 18, 14);
	MainFreeze_NativeOptionsDrawFrame("CONTROLS", offsetX, 145);
}

static void MainFreeze_NativeOptionsDrawPage(int page, int offsetX, const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	switch (page)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		MainFreeze_NativeOptionsDrawAudio(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		MainFreeze_NativeOptionsDrawGraphics(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		MainFreeze_NativeOptionsDrawControls(offsetX, gamepad);
		break;
	default:
		MainFreeze_NativeOptionsDrawRoot(offsetX);
		break;
	}
}

static void MainFreeze_NativeOptionsAdjustVolume(int row)
{
	static int sliderAccum[3] = {0, 0, 0};
	int oldVolume = howl_VolumeGet(row) & 0xff;
	int sliderStep = 4;

	if (Platform_GetHighRefreshMode())
	{
		int targetFPS = Platform_GetHighRefreshTargetFPS();
		if (targetFPS < 30) targetFPS = 30;

		if (sdata->AnyPlayerHold & (BTN_LEFT | BTN_RIGHT))
		{
			sliderAccum[row] += 30;
			sliderStep = 4 * (sliderAccum[row] / targetFPS);
			sliderAccum[row] %= targetFPS;
			if ((sliderStep == 0) && (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT)))
				sliderStep = 4;
		}
		else
		{
			sliderAccum[row] = 0;
			sliderStep = 0;
		}
	}

	if ((sliderStep <= 0) || ((sdata->AnyPlayerHold & (BTN_LEFT | BTN_RIGHT)) == 0))
		return;

	int volume = oldVolume;
	volume += (sdata->AnyPlayerHold & BTN_LEFT) ? -sliderStep : sliderStep;
	if (volume < 0) volume = 0;
	if (volume > 0xff) volume = 0xff;
	howl_VolumeSet(row, volume);
	Platform_SaveAudioVolume(row, volume);

	if (!s_titleOptionsOpen)
	{
		OptionsMenu_TestSound(row, 1);
		return;
	}

	if (volume == oldVolume)
		return;

	if (row == HOWL_VOLUME_TYPE_FX)
	{
		OtherFX_Play(0, 1);
	}
	else if (row == HOWL_VOLUME_TYPE_VOICE)
	{
		if ((s_titleVoicePreviewDelay == 0) || (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT)))
		{
			MainFreeze_TitleOptionsStopVoicePreview();
			s_titleVoicePreviewSound = OtherFX_Play(0x1c + CRASH_BANDICOOT, 0);
			s_titleVoicePreviewDelay = 12;
		}
		else
		{
			s_titleVoicePreviewDelay--;
		}
	}
}

static void MainFreeze_NativeOptionsInputAudio(void)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_AUDIO];

	if (sdata->AnyPlayerTap & BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_AUDIO, NATIVE_OPTIONS_AUDIO_ROW_COUNT, -1);
		return;
	}
	if (sdata->AnyPlayerTap & BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_AUDIO, NATIVE_OPTIONS_AUDIO_ROW_COUNT, 1);
		return;
	}

	if (row < 3)
	{
		MainFreeze_NativeOptionsAdjustVolume(row);
		return;
	}

	if (row == 3)
	{
		if (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one))
		{
			OptionsMenu_TestSound(0, 0);
			OtherFX_Play(1, 1);
			const int newMode = howl_ModeGet() == 0;
			howl_ModeSet(newMode);
			Platform_SaveAudioMode(newMode);
		}
		return;
	}

	if ((row == 4) && (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one)))
	{
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT, -1);
	}
}

static void MainFreeze_NativeOptionsCycleResolution(int direction)
{
	const int count = Platform_GetResolutionPresetCount();
	int current = Platform_GetResolutionPresetIndex();

	if (count <= 0)
		return;

	if (current < 0)
	{
		int windowW = 0;
		int windowH = 0;
		long long currentArea;
		int best = direction > 0 ? count - 1 : 0;
		long long bestDelta = 0x7fffffffffffffffLL;

		Platform_GetWindowSize(&windowW, &windowH);
		currentArea = (long long)windowW * (long long)windowH;

		for (int i = 0; i < count; i++)
		{
			int presetW = 0;
			int presetH = 0;
			long long area;
			long long delta;

			Platform_GetResolutionPresetSize(i, &presetW, &presetH);
			area = (long long)presetW * (long long)presetH;

			if ((direction > 0 && area <= currentArea) ||
			    (direction < 0 && area >= currentArea))
				continue;

			delta = area > currentArea ? area - currentArea : currentArea - area;
			if (delta < bestDelta)
			{
				bestDelta = delta;
				best = i;
			}
		}

		current = best;
	}
	else
	{
		current += direction;
		if (current < 0) current = count - 1;
		if (current >= count) current = 0;
	}

	Platform_SetResolutionPreset(current);
}

static void MainFreeze_NativeOptionsInputGraphics(void)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GRAPHICS];
	u32 adjust = sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one);

	if (sdata->AnyPlayerTap & BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_GRAPHICS, NATIVE_OPTIONS_GRAPHICS_ROW_COUNT, -1);
		return;
	}
	if (sdata->AnyPlayerTap & BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_GRAPHICS, NATIVE_OPTIONS_GRAPHICS_ROW_COUNT, 1);
		return;
	}

	if ((row == 6) && (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one)))
	{
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT, -1);
		return;
	}

	if (!adjust)
		return;

	OtherFX_Play(1, 1);
	switch (row)
	{
	case 0:
		MainFreeze_NativeOptionsCycleResolution((sdata->AnyPlayerTap & BTN_LEFT) ? -1 : 1);
		break;
	case 1:
		Platform_SetFullscreenMode(!Platform_GetFullscreenMode());
		break;
	case 2:
		Platform_SetWideMode(!Platform_GetWideMode());
		break;
	case 3:
		Platform_SetHighRefreshMode(!Platform_GetHighRefreshMode());
		break;
	case 4:
		Platform_SetSubpixelMode(!Platform_GetSubpixelMode());
		break;
	case 5:
	{
		int mode = Platform_GetCharacterDetailMode();
		if (sdata->AnyPlayerTap & BTN_LEFT)
			mode = (mode + 3) % 4;
		else
			mode = (mode + 1) % 4;
		Platform_SetCharacterDetailMode(mode);
		break;
	}
	}
}

static void MainFreeze_NativeOptionsInputControls(const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	struct GameTracker *gGT = sdata->gGT;
	const int rowCount = MainFreeze_NativeOptionsControlsRowCount(gamepad);
	int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_CONTROLS];

	if (row >= rowCount)
	{
		row = rowCount - 1;
		s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_CONTROLS] = (s16)row;
	}

	if (sdata->AnyPlayerTap & BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_CONTROLS, rowCount, -1);
		return;
	}
	if (sdata->AnyPlayerTap & BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_CONTROLS, rowCount, 1);
		return;
	}

	if ((sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one)) == 0)
		return;

	if (row < gamepad->numGamepads)
	{
		const int currPad = gamepad->gamepadId[row];
		struct ControllerPacket *packet = sdata->gGamepads->gamepad[currPad].ptrControllerPacket;
		if ((packet != NULL) && (packet->plugged == PLUGGED))
		{
			OtherFX_Play(1, 1);
			gGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[currPad];
			Platform_SaveVibrationMask(gGT->gameMode1 & GAME_MODE_VIBRATION_MASK);
		}
		return;
	}

	if (row < gamepad->numGamepads + gamepad->numAnalogs)
	{
		const int analogIndex = row - gamepad->numGamepads;
		OtherFX_Play(1, 1);
		sdata->gamepadID_OwnerRaceWheelConfig = gamepad->analogId[analogIndex];
		sdata->boolOpenWheelConfig = true;
		sdata->raceWheelConfigPageIndex = 0;
		return;
	}

	OtherFX_Play(1, 1);
	MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT, -1);
}

static void MainFreeze_NativeOptionsApplyTouch(const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	int mouseX;
	int mouseY;
	int rowCount;
	int rowStartY;
	int rowStep;
	int rowHeight;
	int row = -1;

	if (!RECTMENU_NativeTouchGet(&mouseX, &mouseY))
		return;

	switch (s_nativeOptionsPage)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		rowCount = NATIVE_OPTIONS_AUDIO_ROW_COUNT;
		rowStartY = 59;
		rowStep = 18;
		rowHeight = 14;
		break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		rowCount = NATIVE_OPTIONS_GRAPHICS_ROW_COUNT;
		rowStartY = 51;
		rowStep = 18;
		rowHeight = 14;
		break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		rowCount = MainFreeze_NativeOptionsControlsRowCount(gamepad);
		rowStartY = 59;
		rowStep = 18;
		rowHeight = 14;
		break;
	default:
		rowCount = NATIVE_OPTIONS_ROOT_ROW_COUNT;
		rowStartY = 65;
		rowStep = 22;
		rowHeight = 19;
		break;
	}

	for (int i = 0; i < rowCount; i++)
	{
		RECT rowRect = {
			.x = 74,
			.y = (s16)(rowStartY + i * rowStep),
			.w = 364,
			.h = (s16)rowHeight,
		};

		if ((mouseX >= rowRect.x) &&
		    (mouseX < rowRect.x + rowRect.w) &&
		    (mouseY >= rowRect.y) &&
		    (mouseY < rowRect.y + rowRect.h))
		{
			row = i;
			s_nativeOptionsRows[s_nativeOptionsPage] = (s16)i;
			break;
		}
	}

	if (row < 0)
		return;

	/* Audio sliders are genuinely touchable: click where the marker should go. */
	if ((s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_AUDIO) && (row < 3))
	{
		RECT sliderRect = {
			.x = 190,
			.y = (s16)(59 + row * 18),
			.w = 220,
			.h = 14,
		};

		if (RECTMENU_NativeTouchConsumeRect(&sliderRect))
		{
			int volume = ((mouseX - sliderRect.x) * 255) / (sliderRect.w - 1);
			if (volume < 0) volume = 0;
			if (volume > 255) volume = 255;

			howl_VolumeSet(row, volume);
			Platform_SaveAudioVolume(row, volume);
			MainFreeze_TitleOptionsStopVoicePreview();
			OptionsMenu_TestSound(row, 1);
		}
		return;
	}

	{
		RECT rowRect = {
			.x = 74,
			.y = (s16)(rowStartY + row * rowStep),
			.w = 364,
			.h = (s16)rowHeight,
		};

		if (RECTMENU_NativeTouchConsumeRect(&rowRect))
		{
			sdata->AnyPlayerTap |= BTN_CROSS_one;
			sdata->buttonTapPerPlayer[0] |= BTN_CROSS_one;
		}
	}
}

static b32 MainFreeze_NativeOptionsUpdate(struct RectMenu *menu, GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	(void)menu;

	if (!s_nativeOptionsSessionOpen)
		MainFreeze_NativeOptionsReset();

	if (s_nativeOptionsEntryActive)
	{
		int ticks = Platform_GetLegacy30HzTicks();

		/* Opening Options should use the same native CTR slide as the
		   Audio/Graphics/Controls pages instead of popping in instantly. */
		MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsEntryFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);
		MainFreeze_NativeOptionsDrawPage(NATIVE_OPTIONS_PAGE_ROOT,
		                                 512 + s_nativeOptionsSlideMeta[0].currX,
		                                 gamepad);

		if (ticks > 0)
		{
			s_nativeOptionsEntryFrame += ticks;
			if (s_nativeOptionsEntryFrame >= NATIVE_OPTIONS_TRANSITION_FRAMES)
			{
				s_nativeOptionsEntryFrame = NATIVE_OPTIONS_TRANSITION_FRAMES;
				MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsEntryFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);
				s_nativeOptionsEntryActive = 0;
			}
		}
		return false;
	}

	if (s_nativeOptionsExitActive)
	{
		int ticks = Platform_GetLegacy30HzTicks();

		MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsExitFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage,
		                                 s_nativeOptionsSlideMeta[0].currX,
		                                 gamepad);

		if (ticks > 0)
		{
			s_nativeOptionsExitFrame += ticks;
			if (s_nativeOptionsExitFrame >= NATIVE_OPTIONS_TRANSITION_FRAMES)
			{
				s_nativeOptionsExitFrame = NATIVE_OPTIONS_TRANSITION_FRAMES;
				MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsExitFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);
				s_nativeOptionsExitActive = 0;
				return true;
			}
		}
		return false;
	}

	if (s_nativeOptionsTransitionActive)
	{
		int ticks = Platform_GetLegacy30HzTicks();
		MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsTransitionFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);

		const int outgoingX = s_nativeOptionsSlideMeta[0].currX;
		const int incomingX = (512 * s_nativeOptionsTransitionDirection) + outgoingX;
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, outgoingX, gamepad);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsTargetPage, incomingX, gamepad);

		if (ticks > 0)
		{
			s_nativeOptionsTransitionFrame += ticks;
			if (s_nativeOptionsTransitionFrame >= NATIVE_OPTIONS_TRANSITION_FRAMES)
			{
				s_nativeOptionsTransitionFrame = NATIVE_OPTIONS_TRANSITION_FRAMES;
				MM_TransitionInOut(s_nativeOptionsSlideMeta, s_nativeOptionsTransitionFrame, NATIVE_OPTIONS_TRANSITION_FRAMES);
				s_nativeOptionsPage = s_nativeOptionsTargetPage;
				s_nativeOptionsTransitionActive = 0;
			}
		}
		return false;
	}

	/* Stable custom page: mouse hover selects the row; click activates it. */
	MainFreeze_NativeOptionsApplyTouch(gamepad);

	if (sdata->AnyPlayerTap & BTN_START)
	{
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginExit();
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

	if (sdata->AnyPlayerTap & (BTN_TRIANGLE | BTN_SQUARE_one))
	{
		if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_ROOT)
		{
			OtherFX_Play(1, 1);
			MainFreeze_NativeOptionsBeginExit();
			MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
			return false;
		}

		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT, -1);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

	switch (s_nativeOptionsPage)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		MainFreeze_NativeOptionsInputAudio();
		break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		MainFreeze_NativeOptionsInputGraphics();
		break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		MainFreeze_NativeOptionsInputControls(gamepad);
		break;
	default:
		if (sdata->AnyPlayerTap & BTN_UP)
			MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_ROOT, NATIVE_OPTIONS_ROOT_ROW_COUNT, -1);
		else if (sdata->AnyPlayerTap & BTN_DOWN)
			MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_ROOT, NATIVE_OPTIONS_ROOT_ROW_COUNT, 1);
		else if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
		{
			const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ROOT];
			if (row == 3)
			{
				OtherFX_Play(1, 1);
				MainFreeze_NativeOptionsBeginExit();
				MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
				return false;
			}

			OtherFX_Play(1, 1);
			MainFreeze_NativeOptionsBeginTransition(row + 1, 1);
		}
		break;
	}

	MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
	return false;
}
#endif

force_inline b32 PROCESSINPUTS_MainFreeze_MenuPtrOptions(struct RectMenu *menu, GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	struct GameTracker *gGT = sdata->gGT;
	b32 exitMenu = false;

	if (sdata->AnyPlayerTap & (BTN_UP | BTN_DOWN))
	{
#ifdef CTR_NATIVE
		if (s_titleOptionsOpen)
		{
			MainFreeze_TitleOptionsStopVoicePreview();
		}
#endif
		// play sound for when you're moving around in the menu
		OtherFX_Play(0, 1);

		// Native appends five PC-specific rows after the controller rows.
		// Skip controller rows that do not exist for the current player count.
		int lastControllerRow = gGT->numPlyrCurrGame + 3;
		if (sdata->AnyPlayerTap & BTN_UP)
		{
			if (menu->rowSelected == 0)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_EXIT;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_EXIT)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_SUBPIXEL;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_SUBPIXEL)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_HIGH_REFRESH;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_HIGH_REFRESH)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_SCREEN;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_SCREEN)
				menu->rowSelected = lastControllerRow;
			else
				menu->rowSelected--;
		}
		else if (sdata->AnyPlayerTap & BTN_DOWN)
		{
			if (menu->rowSelected == lastControllerRow)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_SCREEN;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_SCREEN)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_HIGH_REFRESH;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_HIGH_REFRESH)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_SUBPIXEL;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_SUBPIXEL)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL)
				menu->rowSelected = MAINFREEZE_NATIVE_ROW_EXIT;
			else if (menu->rowSelected == MAINFREEZE_NATIVE_ROW_EXIT)
				menu->rowSelected = 0;
			else
				menu->rowSelected++;
		}
	}
	else
	{
		switch (menu->rowSelected)
		{
		// 0: FX slider
		// 1: Music slider
		// 2: Voice slider
		case 0:
		case 1:
		case 2:
#ifdef CTR_NATIVE
			if (!s_titleOptionsOpen)
#endif
			OptionsMenu_TestSound(menu->rowSelected, 1);
			int oldVolume = howl_VolumeGet(menu->rowSelected) & 0xff;
			int sliderStep = 4;
#ifdef CTR_NATIVE
			static int s_nativeSliderAccum[3] = {0, 0, 0};
			if (Platform_GetHighRefreshMode())
			{
				int targetFPS = Platform_GetHighRefreshTargetFPS();
				if (targetFPS < 30) targetFPS = 30;

				if (sdata->AnyPlayerHold & (BTN_LEFT | BTN_RIGHT))
				{
					// Keep continuous slider speed equal to retail while polling
					// input every host frame. Do not require the button hold to
					// coincide with a discrete 30 Hz content tick.
					s_nativeSliderAccum[menu->rowSelected] += 30;
					sliderStep = 4 * (s_nativeSliderAccum[menu->rowSelected] / targetFPS);
					s_nativeSliderAccum[menu->rowSelected] %= targetFPS;

					// A fresh tap must always feel immediate.
					if ((sliderStep == 0) && (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT)))
					{
						sliderStep = 4;
					}
				}
				else
				{
					s_nativeSliderAccum[menu->rowSelected] = 0;
					sliderStep = 0;
				}
			}
#endif
			if ((sliderStep > 0) && (sdata->AnyPlayerHold & (BTN_LEFT | BTN_RIGHT)))
			{
				int volume = oldVolume;

				if (sdata->AnyPlayerHold & BTN_LEFT)
				{
					volume -= sliderStep;
				}
				else if (sdata->AnyPlayerHold & BTN_RIGHT)
				{
					volume += sliderStep;
				}

				if (volume < 0)
				{
					volume = 0;
				}
				if (volume > 0xff)
				{
					volume = 0xff;
				}

				howl_VolumeSet(menu->rowSelected, volume);
#ifdef CTR_NATIVE
				Platform_SaveAudioVolume(menu->rowSelected, volume);
				if (s_titleOptionsOpen && volume != oldVolume)
				{
					if (menu->rowSelected == HOWL_VOLUME_TYPE_FX)
					{
						OtherFX_Play(0, 1);
					}
					else if (menu->rowSelected == HOWL_VOLUME_TYPE_VOICE)
					{
						if (s_titleVoicePreviewDelay == 0 || (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT)))
						{
							MainFreeze_TitleOptionsStopVoicePreview();
							s_titleVoicePreviewSound = OtherFX_Play(0x1c + CRASH_BANDICOOT, 0);
							s_titleVoicePreviewDelay = 12;
						}
						else
						{
							s_titleVoicePreviewDelay--;
						}
					}
				}
#endif
			}
			break;

		// Mode	(Stereo/Mono)
		case 3:
			// clear test sound
			OptionsMenu_TestSound(0, 0);

			if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
			{
				OtherFX_Play(1, 1);
				int mode = howl_ModeGet();
				howl_ModeSet(mode == 0);
#ifdef CTR_NATIVE
				Platform_SaveAudioMode(mode == 0);
#endif
			}
			break;

		// DualShock/"Analog controller" settings
		case 4:
		case 5:
		case 6:
		case 7:
			// clear test sound
			OptionsMenu_TestSound(0, 0);

			if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
			{
				OtherFX_Play(1, 1);

				int gamepadRow = menu->rowSelected - 4;

				if (gamepadRow < gamepad->numGamepads)
				{
					// selecting dualshock row
					// toggle gamepad vibration
					gGT->gameMode1 ^= data.gGT_gameMode1_VibPerPlayer[gamepad->gamepadId[gamepadRow]];
#ifdef CTR_NATIVE
					Platform_SaveVibrationMask(gGT->gameMode1 & GAME_MODE_VIBRATION_MASK);
#endif
				}
				else
				{
					// selecting "analog controller" row
					// this will open the analog controller config menu
					sdata->gamepadID_OwnerRaceWheelConfig = gamepad->analogId[gamepadRow - gamepad->numGamepads];
					sdata->boolOpenWheelConfig = true;
					sdata->raceWheelConfigPageIndex = 0;
				}
			}
			break;

		// Native PC display options.
		case MAINFREEZE_NATIVE_ROW_SCREEN:
#ifdef CTR_NATIVE
			OptionsMenu_TestSound(0, 0);
			if (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one))
			{
				OtherFX_Play(1, 1);
				Platform_SetWideMode(!Platform_GetWideMode());
			}
#endif
			break;
		case MAINFREEZE_NATIVE_ROW_HIGH_REFRESH:
#ifdef CTR_NATIVE
			OptionsMenu_TestSound(0, 0);
			if (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one))
			{
				const int oldMode = Platform_GetHighRefreshMode();
				const int newMode = !oldMode;
				Platform_Log("[CTR Native] V70 MENU HIGH REFRESH row=%d tap=%08x old=%d new=%d\n",
				             menu->rowSelected, (unsigned)sdata->AnyPlayerTap, oldMode, newMode);
				OtherFX_Play(1, 1);
				Platform_SetHighRefreshMode(newMode);
			}
#endif
			break;
		case MAINFREEZE_NATIVE_ROW_SUBPIXEL:
#ifdef CTR_NATIVE
			OptionsMenu_TestSound(0, 0);
			if (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one))
			{
				const int oldMode = Platform_GetSubpixelMode();
				const int newMode = !oldMode;
				Platform_Log("[CTR Native] V70 MENU SUBPIXEL row=%d tap=%08x old=%d new=%d\n",
				             menu->rowSelected, (unsigned)sdata->AnyPlayerTap, oldMode, newMode);
				OtherFX_Play(1, 1);
				Platform_SetSubpixelMode(newMode);
			}
#endif
			break;
		case MAINFREEZE_NATIVE_ROW_CHARACTER_DETAIL:
#ifdef CTR_NATIVE
			OptionsMenu_TestSound(0, 0);
			if (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one))
			{
				int mode = Platform_GetCharacterDetailMode();
				if (sdata->AnyPlayerTap & BTN_LEFT)
					mode = (mode + 3) % 4;
				else
					mode = (mode + 1) % 4;
				OtherFX_Play(1, 1);
				Platform_SetCharacterDetailMode(mode);
			}
#endif
			break;
		case MAINFREEZE_NATIVE_ROW_EXIT:
			OptionsMenu_TestSound(0, 0);
			if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
			{
				OtherFX_Play(1, 1);
				exitMenu = true;
			}
			break;
		}
	}

	return exitMenu;
}

// stuff is drawn last to first
force_inline void DISPLAYRECTMENU_MainFreeze_MenuPtrOptions(struct RectMenu *menu, GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	struct GameTracker *gGT = sdata->gGT;

	// note: multitap only works if it's connected to the P1 slot
	int multitapStringOffset = (sdata->gGamepads->slotBuffer[0].controllerData == (PAD_ID_MULTITAP << 4)) ? 2 : 0;

	// a menu row is 10 pixels
	int menuRowsNegativePadding = gamepad->menuRowsToRemove * 10;

	int analogRowPosY = 0;
	if (gamepad->numGamepads != 0)
	{
		analogRowPosY = (gamepad->numGamepads + 1) * 10;
	}

	// cursor location for exit button, which will change depending on how many dualshock rows there need to be
	data.Options_HighlightBar[8].posY = gamepad->menuRowsToRemove * -10 + 119;

	// cursor location for dualshock rows
	if (gamepad->numGamepads > 0)
	{
		for (int i = 0; i < gamepad->numGamepads; i++)
		{
			data.Options_HighlightBar[i + 4].posY = (i * 10) + 79;
		}
	}

	// cursor location for "analog" rows
	if (gamepad->numAnalogs > 0)
	{
		for (int i = 0; i < gamepad->numAnalogs; i++)
		{
			b32 areBothControllerLabelsNecessary = false;
			if (gamepad->numGamepads != 0)
			{
				areBothControllerLabelsNecessary = (gamepad->numAnalogs != 0);
			}

			data.Options_HighlightBar[gamepad->numGamepads + i + 4].posY = ((gamepad->numGamepads + i + areBothControllerLabelsNecessary) * 10) + 79;
		}
	}

	menu->drawStyle &= ~RECTMENU_DRAW_STYLE_3P4P_LAYOUT;
	if (gGT->numPlyrCurrGame > 2)
	{
		menu->drawStyle |= RECTMENU_DRAW_STYLE_3P4P_LAYOUT;
	}

	int volumeSliderTriangleLeftMargin = 0;

	for (int i = 0; i < 3; i++)
	{
		//"FX:", "MUSIC:", "VOICE:"
		int lineWidth = DecalFont_GetLineWidth(sdata->lngStrings[data.Options_StringIDs_Audio[i]], FONT_SMALL);
		if (volumeSliderTriangleLeftMargin < lineWidth)
		{
			volumeSliderTriangleLeftMargin = lineWidth;
		}
	}

	DecalFont_DrawLine(sdata->lngStrings[LNG_OPTIONS_TITLE], 256, 26 + (menuRowsNegativePadding / 2), FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	int volumeSliderWidth = 380 - (30 + volumeSliderTriangleLeftMargin);

	uint32_t *ot = gGT->backBuffer->otMem.uiOT;
	struct PrimMem *primMem = &gGT->backBuffer->primMem;
	Color color;

	// draw volume sliders
	for (int i = 0; i < 3; i++)
	{
		int volume = howl_VolumeGet(i) & 0xff;
		int volumeSliderValue = volume * (volumeSliderWidth - 5);

		s16 volumeSliderPosY = (menuRowsNegativePadding / 2) + (i * 10);

		if (volumeSliderValue < 0)
		{
			volumeSliderValue += 0xff;
		}

		int volumeSliderTriangleLeftPosX = 30 + volumeSliderTriangleLeftMargin;
		int volumeSliderBarPosX = 0x38 + volumeSliderTriangleLeftPosX + (s16)((u32)volumeSliderValue >> 8);

		s16 volumeSliderTriangle[8] = {// 0, 1
		                               volumeSliderTriangleLeftPosX + 56, volumeSliderPosY + 58,

		                               // 2, 3
		                               volumeSliderTriangleLeftPosX + volumeSliderWidth + 56, volumeSliderPosY + 48,

		                               // 4, 5
		                               0, 0};

		volumeSliderTriangle[4] = volumeSliderTriangle[2];
		volumeSliderTriangle[5] = volumeSliderTriangle[1];

		RECT volumeSliderBar = {.x = volumeSliderBarPosX + 1, .y = volumeSliderPosY + 48, .w = 3, .h = 10};
		color = *(Color *)(data.Options_VolumeSlider_Colors + 0xc);
		CTR_Box_DrawSolidBox(&volumeSliderBar, color, ot);

		RECT volumeSliderBarOutline = {.x = volumeSliderBarPosX, .y = volumeSliderPosY + 47, .w = 5, .h = 12};

		color = *(Color *)(data.Options_VolumeSlider_Colors + 0x10);
		CTR_Box_DrawSolidBox(&volumeSliderBarOutline, color, ot);

		RECTMENU_DrawRwdTriangle(volumeSliderTriangle, data.Options_VolumeSlider_Colors, ot, primMem);

		// "FX:" "MUSIC:" "VOICE:"
		DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Audio[i]], 76, 50 + (menuRowsNegativePadding / 2) + (i * 10), FONT_SMALL, ORANGE);
	}

	DecalFont_DrawLine(sdata->lngStrings[LNG_MODE], 76, 80 + (menuRowsNegativePadding / 2), FONT_SMALL, ORANGE);

	// 333: MONO
	// 334: STEREO
	int mode = howl_ModeGet();

	// "MONO", "STEREO"
	DecalFont_DrawLine(sdata->lngStrings[333 + mode], 436, 80 + (menuRowsNegativePadding / 2), FONT_SMALL, (JUSTIFY_RIGHT | WHITE));

	if (gamepad->numGamepads != 0)
	{
		DecalFont_DrawLine(sdata->lngStrings[LNG_DUAL_SHOCK], 76, 90 + (menuRowsNegativePadding / 2), FONT_SMALL, ORANGE);

		int lineWidth_controller1A = DecalFont_GetLineWidth(sdata->lngStrings[data.Options_StringIDs_Gamepads[2]], FONT_SMALL);

		// width can change depending on language
		int lineWidth_vibrateOff = DecalFont_GetLineWidth(sdata->lngStrings[LNG_VIBRATE_OFF], FONT_SMALL);
		int lineWidth_vibrateOn = DecalFont_GetLineWidth(sdata->lngStrings[LNG_VIBRATE_ON], FONT_SMALL);
		if (lineWidth_vibrateOn < lineWidth_vibrateOff)
		{
			lineWidth_vibrateOn = lineWidth_vibrateOff;
		}

		lineWidth_vibrateOn = (lineWidth_controller1A + lineWidth_vibrateOn + 10);
		lineWidth_vibrateOn = 256 - (lineWidth_vibrateOn >> 1);

		for (int i = 0; i < gamepad->numGamepads; i++)
		{
			int dualShockRowColor = ORANGE;
			int currPad = gamepad->gamepadId[i];

			struct ControllerPacket *ptrControllerPacket = sdata->gGamepads->gamepad[currPad].ptrControllerPacket;

			if (ptrControllerPacket == 0 || ptrControllerPacket->plugged != PLUGGED)
			{
				dualShockRowColor = GRAY;
			}

			int rowY = 100 + (menuRowsNegativePadding / 2) + (i * 10);

			// "CONTROLLER 1", "CONTROLLER 2",
			// "CONTROLLER 1A", "CONTROLLER 1B",
			// "CONTROLLER 1C", "CONTROLLER 1D"
			DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Gamepads[currPad + multitapStringOffset]], lineWidth_vibrateOn, rowY, FONT_SMALL,
			                   dualShockRowColor);

			b32 boolDisabled = (gGT->gameMode1 & data.gGT_gameMode1_VibPerPlayer[currPad]) != 0;

			if (dualShockRowColor != GRAY)
			{
				dualShockRowColor = boolDisabled ? RED : WHITE;
			}

			// 325: "VIBRATE ON"
			// 326: "VIBRATE OFF"

			DecalFont_DrawLine(sdata->lngStrings[325 + boolDisabled], lineWidth_vibrateOn + lineWidth_controller1A + 10, rowY, FONT_SMALL, dualShockRowColor);
		}
	}

	if (gamepad->numAnalogs != 0)
	{
		DecalFont_DrawLine(sdata->lngStrings[LNG_CONFIGURE_ANALOG], 76, 90 + (menuRowsNegativePadding / 2) + analogRowPosY, FONT_SMALL, ORANGE);

		for (int i = 0; i < gamepad->numAnalogs; i++)
		{
			DecalFont_DrawLine(sdata->lngStrings[data.Options_StringIDs_Gamepads[gamepad->analogId[i] + multitapStringOffset]], 256,
			                   100 + (menuRowsNegativePadding / 2) + analogRowPosY + (i * 10), FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
		}
	}

#ifdef CTR_NATIVE
	const int nativeOptionsY = 140 - (menuRowsNegativePadding / 2);
	DecalFont_DrawLine("SCREEN", 76, nativeOptionsY, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(Platform_GetWideMode() ? "16:9" : "4:3", 436, nativeOptionsY, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("HIGH REFRESH", 76, nativeOptionsY + 10, FONT_SMALL, ORANGE);
	{
		char refreshText[24];
		if (Platform_GetHighRefreshMode())
			snprintf(refreshText, sizeof(refreshText), "%d FPS", Platform_GetHighRefreshTargetFPS());
		else
			snprintf(refreshText, sizeof(refreshText), "OFF");
		DecalFont_DrawLine(refreshText, 436, nativeOptionsY + 10, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	}

	DecalFont_DrawLine("SUBPIXEL", 76, nativeOptionsY + 20, FONT_SMALL, ORANGE);
	DecalFont_DrawLine(Platform_GetSubpixelMode() ? "ON" : "OFF", 436, nativeOptionsY + 20, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("CHARACTER DETAIL", 76, nativeOptionsY + 30, FONT_SMALL, ORANGE);
	{
		const int detailMode = Platform_GetCharacterDetailMode();
		char *detailText = detailMode == 0 ? "LOW" : (detailMode == 1 ? "SMART" : (detailMode == 2 ? "HIGH" : "ULTRA"));
		DecalFont_DrawLine(detailText, 436, nativeOptionsY + 30, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	}

	DecalFont_DrawLine(sdata->lngStrings[LNG_OPTIONS_EXIT], 76, nativeOptionsY + 40, FONT_SMALL, ORANGE);
#endif

	// Native PC rows reuse the final retail highlight style and extend it down.
	int highlightRow = menu->rowSelected;
	int nativeHighlightOffset = 0;
#ifdef CTR_NATIVE
	if (menu->rowSelected >= MAINFREEZE_NATIVE_ROW_SCREEN)
	{
		highlightRow = MAINFREEZE_NATIVE_ROW_SCREEN;
		nativeHighlightOffset = (menu->rowSelected - MAINFREEZE_NATIVE_ROW_SCREEN) * 10;
	}
#endif
	RECT cursor = {.x = 74,
	               .y = data.Options_HighlightBar[highlightRow].posY + nativeHighlightOffset + (menuRowsNegativePadding / 2) + 20,
	               .w = 364,
	               .h = data.Options_HighlightBar[highlightRow].sizeY};

	CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);

	RECT titleSeparatorLine = {.x = 66, .y = (menuRowsNegativePadding / 2) + 43, .w = 380, .h = 2};

	color.self = sdata->battleSetup_Color_UI_1;
	RECTMENU_DrawOuterRect_Edge(&titleSeparatorLine, color, 0x20, ot);

	RECT menuBG = {.x = 56, .y = (menuRowsNegativePadding / 2) + 20, .w = 400, .h = 175 - menuRowsNegativePadding};

#ifdef CTR_NATIVE
	// Hide the title logo behind this translucent pause-menu panel.
	RECTMENU_DrawInnerRect(&menuBG, 4, ot);
#else
	RECTMENU_DrawInnerRect(&menuBG, 4, ot);
#endif
}

void MainFreeze_MenuPtrOptions(struct RectMenu *menu)
{
#ifdef CTR_NATIVE
	if (s_titleOptionsOpen && s_titleOptionsNeedsCursorReset)
	{
		menu->rowSelected = 0;
		s_titleOptionsNeedsCursorReset = 0;
	}
#endif
	MainFreeze_SafeAdvDestroy();

	// open racing wheel config menu instead
	if (sdata->boolOpenWheelConfig != 0)
	{
		MainFreeze_ConfigSetupEntry();
		return;
	}

	// the options menu has gamepad-related settings
	// specificially for DualShock controllers
	// as well as what CTR refers to as "analog controllers", which are jogcons and negcons
	// this struct will be filled out in IDENTIFYGAMEPADS
	GAMEPAD_MainFreeze_MenuPtrOptions gamepad = {
	    .numGamepads = 0,
	    .numAnalogs = 0,
	    .gamepadId = {-1, -1, -1, -1},
	    .analogId = {-1, -1, -1, -1},
	    .isGamepadAnalog = {false, false, false, false},
	    .menuRowsToRemove = 0,
	};

	IDENTIFYGAMEPADS_MainFreeze_MenuPtrOptions(menu, &gamepad);
#ifdef CTR_NATIVE
	b32 exitMenu = MainFreeze_NativeOptionsUpdate(menu, &gamepad);
	if (exitMenu)
	{
		OtherFX_Play(1, 1);
		OptionsMenu_TestSound(0, 0);
		MainFreeze_TitleOptionsStopVoicePreview();
		RECTMENU_ClearInput();
		s_nativeOptionsSessionOpen = 0;
		s_nativeOptionsTransitionActive = 0;

		if (s_titleOptionsOpen)
		{
			s_titleOptionsOpen = 0;
			if (s_titleOptionsOwner != NULL)
			{
				s_titleOptionsOwner->rowSelected = s_titleOptionsReturnRow;
				s_titleOptionsOwner->state = s_titleOptionsSavedState;
				s_titleOptionsOwner = NULL;
			}
			return;
		}

		sdata->ptrDesiredMenu = MainFreeze_GetMenuPtr();
	}
	return;
#else
	b32 exitMenu = PROCESSINPUTS_MainFreeze_MenuPtrOptions(menu, &gamepad);
	if (exitMenu || (sdata->AnyPlayerTap & (BTN_TRIANGLE | BTN_START | BTN_SQUARE_one)))
	{
		OtherFX_Play(1, 1);
		OptionsMenu_TestSound(0, 0);
		RECTMENU_ClearInput();
		sdata->ptrDesiredMenu = MainFreeze_GetMenuPtr();
	}
	DISPLAYRECTMENU_MainFreeze_MenuPtrOptions(menu, &gamepad);
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80039908-0x800399fc.
void MainFreeze_MenuPtrQuit(struct RectMenu *menu)
{
	s16 row;
	struct GameTracker *gGT = sdata->gGT;

	if (menu->funcState == RECTMENU_FUNC_STATE_INPUT)
	{
		row = menu->rowSelected;
		if (row == 0)
		{
			// Erase ghost of previous race from RAM
			GhostTape_Destroy();

			// Add bit for "in menu" when loading is done
			sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

			// Go to main menu
			sdata->mainMenuState = MAIN_MENU_TITLE;

			// Remove bit for "In Adventure Arena" when loading is done
			sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;

			// Unpause game
			gGT->gameMode1 &= ~PAUSE_1;

			// Level ID for main menu (39)
			MainRaceTrack_RequestLoad(0x27);
			return;
		}

		if ((row == 1) || (row == -1))
		{
			sdata->ptrActiveMenu = MainFreeze_GetMenuPtr();
		}
	}
	else
	{
		menu->drawStyle &= ~RECTMENU_DRAW_STYLE_3P4P_LAYOUT;

		// if more than 2 screens
		if (gGT->numPlyrCurrGame > 2)
		{
			menu->drawStyle |= RECTMENU_DRAW_STYLE_3P4P_LAYOUT;
		}

		MainFreeze_SafeAdvDestroy();
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800399fc-0x80039a44.
void MainFreeze_SafeAdvDestroy(void)
{
	// If you're in Adventure Arena
	if ((sdata->gGT->gameMode1 & ADVENTURE_ARENA) == 0)
	{
		return;
	}

	// check if Adv Hub is loaded
	if (!LOAD_IsOpen_AdvHub())
	{
		return;
	}

	AH_Pause_Destroy();
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80039a44-0x80039dcc.
void MainFreeze_MenuPtrDefault(struct RectMenu *menu)
{
	int levID = 0; // dingo canyon
	u16 stringID;
	u32 gameMode;

	struct GameTracker *gGT = sdata->gGT;
	gameMode = gGT->gameMode1;

	// if you have not waited 5 frames since the game was paused then quit
	if (gGT->cooldownfromPauseUntilUnpause != 0)
	{
		return;
	}

	// assume 5 frames have passed since paused

	if (menu->funcState != RECTMENU_FUNC_STATE_INPUT)
	{
		menu->drawStyle &= ~RECTMENU_DRAW_STYLE_3P4P_LAYOUT;

		// if more than 2 screens
		if (2 < gGT->numPlyrCurrGame)
		{
			menu->drawStyle |= RECTMENU_DRAW_STYLE_3P4P_LAYOUT;
		}

		if (((gameMode & ADVENTURE_ARENA) == 0) || (menu->state & NEEDS_TO_CLOSE))
		{
			return;
		}

		// quit adv hub if it's not loaded
		if (!LOAD_IsOpen_AdvHub())
		{
			return;
		}

		AH_Pause_Update();
		return;
	}

	if (menu->rowSelected < 0)
	{
		return;
	}

	// get stringID from selected row
	stringID = menu->rows[menu->rowSelected].stringIndex;

	// stringID 14: "OPTIONS"
	if (stringID == 14)
	{
		// Set Menu to Options
		sdata->ptrDesiredMenu = &data.menuRacingWheelConfig;

		data.menuRacingWheelConfig.rowSelected = MAINFREEZE_NATIVE_ROW_EXIT;
		return;
	}

	// stringID 11: "AKU AKU HINTS"
	// stringID 12: "UKA UKA HINTS"
	if (stringID == 11 || stringID == 12)
	{
		// Set Menu to Hints
		sdata->ptrDesiredMenu = &D232.menuHintMenu; // in 232
		return;
	}

	// stringID 3: "QUIT"
	if (stringID == 3)
	{
		// Set Menu to Quit
		sdata->ptrDesiredMenu = &data.menuQuit;
		data.menuQuit.rowSelected = 1;
		return;
	}

	// must wait 5 frames until next pause
	gGT->cooldownFromUnpauseUntilPause = 5;

	// hide Menu
	RECTMENU_Hide(menu);

	MainFreeze_SafeAdvDestroy();

	// careful, it's stringID MINUS one
	switch (stringID)
	{
	// stringID 1: "RESTART"
	case 1:

	// stringID 4: "RETRY"
	case 4:

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;

		if (RaceFlag_IsFullyOffScreen())
		{
			// checkered flag, begin transition on-screen
			RaceFlag_BeginTransition(1);
		}

		// restart race
		sdata->Loading.stage = LOAD_RESTART;

		// if you are not showing a ghost during a race
		if (sdata->boolReplayHumanGhost == 0)
		{
			return;
		}

		// If the ghost playing buffer is nullptr
		if (sdata->ptrGhostTapePlaying == 0)
		{
			return;
		}

		// Make P2 the character that is saved in the header of the
		// ghost that you will see in the race
		data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;
		return;

	// stringID 2: "RESUME"
	case 2:

		// unpause game
		ElimBG_Deactivate(gGT);

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;

		// unpause audio
		MainFrame_TogglePauseAudio(0);

		// play pause/unpause sound
		OtherFX_Play(1, 1);
		return;

	// stringID 5: "CHANGE CHARACTER"
	case 5:

		// erase ghost of previous race from RAM
		GhostTape_Destroy();

		// set level ID to main menu
		levID = MAIN_MENU_LEVEL;

		// return to character selection
		sdata->mainMenuState = MAIN_MENU_CHARACTERS;

		// when loading is done, add bit for "in mb"
		sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;
		break;

	// stringID 6: "CHANGE LEVEL"
	case 6:

		// erase ghost of previous race from RAM
		GhostTape_Destroy();

		// level ID of main mb
		levID = MAIN_MENU_LEVEL;

		// return to track selection
		sdata->mainMenuState = MAIN_MENU_TRACK_SELECT;

		// when loading is done
		// add bit for "in mb"
		sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;
		break;

	// stringID 10: "CHANGE SETUP"
	case 10:

		// set level ID to main menu
		levID = MAIN_MENU_LEVEL;

		// return to battle setup
		sdata->mainMenuState = MAIN_MENU_BATTLE_SETUP;

		// when loading is done
		// add bit for "in mb"
		sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;
		break;

	// stringID 13: "EXIT TO MAP"
	case 13:

		// when loading is done
		// add this bit for In Adventure Arena
		sdata->Loading.OnBegin.AddBitsConfig0 |= ADVENTURE_ARENA;

		// when loading is done
		// remove bits for Relic Race or Crystal Challenge
		sdata->Loading.OnBegin.RemBitsConfig0 |= RELIC_RACE | CRYSTAL_CHALLENGE;

		// when loading is done
		// remove bit for CTR Token Challenge
		sdata->Loading.OnBegin.RemBitsConfig8 |= TOKEN_RACE;

		// get rid of pause flag
		gGT->gameMode1 &= ~PAUSE_1;

		// If you are not in Adventure cup
		if ((gameMode & ADVENTURE_CUP) == 0)
		{
			// 0x80000000
			// If you're in Boss Mode
			if ((int)gameMode < 0)
			{
				// when loading is done remove bit for Boss Race, relic, and crystal challenge
				sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_BOSS | RELIC_RACE | CRYSTAL_CHALLENGE;

				// When loading is done add bit to spawn driver near boss door
				sdata->Loading.OnBegin.AddBitsConfig8 |= SPAWN_AT_BOSS;
			}

			// set levID to level you were in previously
			levID = gGT->prevLEV;
		}

		// If you're in Adventure Cup
		else
		{
			levID = GEM_STONE_VALLEY;

			// when loading is done remove bits for Adventure Cup, relic, and crystal challenge
			sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_CUP | RELIC_RACE | CRYSTAL_CHALLENGE;

			// Level ID
			gGT->levelID = gGT->cup.cupID + ADVENTURE_CUP_SYNTHETIC_LEVEL_ID_BASE;
		}
		break;
	default:
		return;
	}

	MainRaceTrack_RequestLoad(levID);
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80039dcc-0x80039e98.
struct RectMenu *MainFreeze_GetMenuPtr(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 gameMode = gGT->gameMode1;

	if ((gameMode & ADVENTURE_ARENA) != 0)
	{
		s32 hintString = LNG_UKA_UKA_HINTS;
		if (VehPickupItem_MaskBoolGoodGuy(gGT->drivers[0]) != 0)
		{
			hintString = LNG_AKU_AKU_HINTS;
		}

		data.rowsAdvHub[1].stringIndex = hintString;
		return &data.menuAdvHub;
	}

	if ((gameMode & ADVENTURE_MODE) != 0)
	{
		if ((gameMode & ADVENTURE_CUP) != 0)
		{
			return &data.menuAdvCup;
		}

		return &data.menuAdvRace;
	}

	if ((gameMode & BATTLE_MODE) != 0)
	{
		return &data.menuBattle;
	}

	if ((gGT->gameMode2 & CUP_ANY_KIND) != 0)
	{
		return &data.menuArcadeCup;
	}

	return &data.menuArcadeRace;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80039e98-0x80039fa8.
void MainFreeze_IfPressStart(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 gameMode1;
	struct RectMenu *menu;

	if (RaceFlag_IsFullyOnScreen())
	{
		return;
	}

	if ((gGT->renderFlags & RENDER_FLAG_CHECKERED_FLAG) != 0)
	{
		return;
	}

	if (sdata->AkuAkuHintState != 0)
	{
		return;
	}

	if (sdata->ptrActiveMenu != NULL)
	{
		return;
	}

	gameMode1 = gGT->gameMode1;

	if ((gameMode1 & (END_OF_RACE | PAUSE_ALL)) != 0)
	{
		return;
	}

	if (gGT->levelID == MAIN_MENU_LEVEL)
	{
		return;
	}

	if ((gameMode1 & GAME_CUTSCENE) != 0)
	{
		return;
	}

	if (gGT->boolDemoMode != 0)
	{
		return;
	}

	if ((u32)(gGT->levelID - OXIDE_ENDING) < 2)
	{
		return;
	}

	if (sdata->load_inProgress != 0)
	{
		return;
	}

	if ((gGT->gameMode2 & VEH_FREEZE_PODIUM) != 0)
	{
		return;
	}

	gGT->gameMode1 = gameMode1 | PAUSE_1;

	menu = MainFreeze_GetMenuPtr();
	menu->rowSelected = 0;

	RECTMENU_Show(menu);
	MainFrame_TogglePauseAudio(1);
	OtherFX_Play(1, 1);
	ElimBG_Activate(gGT);
}
