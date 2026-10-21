#include <common.h>

#ifdef CTR_NATIVE
#include <NativeHangout.h>
#include <NativeHUD.h>
#include "platform/native_log.h"
#include "platform/native_input.h"
#include "platform/native_input_glyphs.h"
#include "platform/native_music_selection.h"
#include <platform/native_custom_tracks.h>
#include <platform/native_custom_music.h>
#include <platform/native_menu_music_clock.h>
#if defined(CTR_CSEQ_LIBERATION)
#include <NativeMusicRegistry.h>
#endif
#include <SDL3/SDL.h>
static int s_titleOptionsOpen;
static s16 s_titleOptionsReturnRow;
static u32 s_titleOptionsSavedState;
static struct RectMenu *s_titleOptionsOwner; // CTR_OPTIONS_V43
static int s_titleOptionsNeedsCursorReset;
static int s_titleVoicePreviewSound;
static int s_titleVoicePreviewDelay;

/* Hangout has no Adventure progression/hints in its pause hierarchy.
 * Keep a dedicated menu so retail Adventure can remain untouched. */
static struct MenuRow s_hangoutPauseRows[] =
{
	{0x2, 5, 1, 0, 0},                                      /* RESUME        */
	{NATIVE_HANGOUT_MENU_CUSTOMIZATION_STRING, 0, 2, 1, 1}, /* CUSTOMIZATION */
	{NATIVE_HANGOUT_MENU_DISCONNECT_STRING, 1, 3, 2, 2},    /* DISCONNECT P1 */
	{NATIVE_HANGOUT_MENU_CONNECT_STRING, 2, 4, 3, 3},       /* CONNECT P1    */
	{0x3, 3, 5, 4, 4},                                      /* QUIT          */
	{0xE, 4, 0, 5, 5},                                      /* OPTIONS       */
	{-1, 0, 0, 0, 0}
};

static struct RectMenu s_hangoutPauseMenu =
{
	.stringIndexTitle = RECTMENU_STRING_NONE,
	.posX_curr = 0x100,
	.posY_curr = 0xAF,
	.unk1 = 0,
	.state = ALL_PLAYERS_USE_MENU | RECTMENU_STATE_SMALL_EXEC_CENTERED,
	.rows = &s_hangoutPauseRows[0],
	.funcPtr = MainFreeze_MenuPtrDefault,
	.drawStyle = 4,
};

enum NativeOptionsPage
{
	NATIVE_OPTIONS_PAGE_ROOT = 0,
	NATIVE_OPTIONS_PAGE_AUDIO = 1,
	NATIVE_OPTIONS_PAGE_GRAPHICS = 2,
	NATIVE_OPTIONS_PAGE_GAMEPLAY = 3,
	NATIVE_OPTIONS_PAGE_CONTROLS = 4,
	NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS = 5,
	NATIVE_OPTIONS_PAGE_MUSIC = 6,
	NATIVE_OPTIONS_PAGE_MUSIC_LEVELS = 7,
	NATIVE_OPTIONS_PAGE_MUSIC_SONGS = 8,
	NATIVE_OPTIONS_PAGE_MUSIC_TARGETS = 9,
    NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS = 10,
    NATIVE_OPTIONS_PAGE_LANGUAGE = 11,
};

enum
{
	NATIVE_OPTIONS_TRANSITION_FRAMES = 12,
	NATIVE_OPTIONS_ROOT_ROW_COUNT = 6,
	NATIVE_OPTIONS_AUDIO_ROW_COUNT = 7,
	NATIVE_OPTIONS_MUSIC_ROOT_ROWS = 5,
	NATIVE_OPTIONS_MUSIC_CATEGORY_ROWS = 9,
	NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE = 6,
	NATIVE_OPTIONS_GRAPHICS_ROW_COUNT = 8,
	NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT = 14,
    NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS = 8,
    NATIVE_ADVANCED_GRAPHICS_FONT_PERCENT = 75,
    NATIVE_ADVANCED_GRAPHICS_TEXT_Y = 63,
    NATIVE_ADVANCED_GRAPHICS_ROW_TOP_Y = 60,
    NATIVE_ADVANCED_GRAPHICS_ROW_STEP = 17,
    NATIVE_ADVANCED_GRAPHICS_ROW_HEIGHT = 14,
    NATIVE_ADVANCED_GRAPHICS_FRAME_HEIGHT = 197,
	NATIVE_OPTIONS_GAMEPLAY_ROW_COUNT = 4, /* 5 for CNK (conditional height row) */
	NATIVE_CONTROLS_PLAYER_LIST_ROW_COUNT = 5,
	NATIVE_CONTROLS_PLAYER_CONFIG_ROW_COUNT = 7,
	NATIVE_CONTROLS_REMAP_VISIBLE_ROWS = 10,
};

enum NativeControlsView
{
	NATIVE_CONTROLS_VIEW_PLAYERS = 0,
	NATIVE_CONTROLS_VIEW_PLAYER,
	NATIVE_CONTROLS_VIEW_REMAP,
};

enum
{
	NATIVE_CONTROLS_REMAP_POWER_SLIDE = -2,
};

static const int s_nativeControlsRemapRows[] = {
	/* Face buttons first: these use the supplied PS/Xbox/Generic glyph atlas. */
	PLATFORM_INPUT_ACTION_CROSS,
	PLATFORM_INPUT_ACTION_SQUARE,
	PLATFORM_INPUT_ACTION_CIRCLE,
	PLATFORM_INPUT_ACTION_TRIANGLE,

	/* Keep every D-pad direction together. */
	PLATFORM_INPUT_ACTION_DPAD_UP,
	PLATFORM_INPUT_ACTION_DPAD_DOWN,
	PLATFORM_INPUT_ACTION_DPAD_LEFT,
	PLATFORM_INPUT_ACTION_DPAD_RIGHT,

	/* Shoulders/stick clicks stay grouped by side/type. */
	PLATFORM_INPUT_ACTION_L1,
	PLATFORM_INPUT_ACTION_R1,
	PLATFORM_INPUT_ACTION_L2,
	PLATFORM_INPUT_ACTION_R2,
	PLATFORM_INPUT_ACTION_L3,
	PLATFORM_INPUT_ACTION_R3,
	PLATFORM_INPUT_ACTION_START,
	PLATFORM_INPUT_ACTION_SELECT,

	/* Split both sticks into four independently remappable directions. */
	PLATFORM_INPUT_ACTION_LEFT_STICK_LEFT,
	PLATFORM_INPUT_ACTION_LEFT_STICK_RIGHT,
	PLATFORM_INPUT_ACTION_LEFT_STICK_UP,
	PLATFORM_INPUT_ACTION_LEFT_STICK_DOWN,
	PLATFORM_INPUT_ACTION_RIGHT_STICK_LEFT,
	PLATFORM_INPUT_ACTION_RIGHT_STICK_RIGHT,
	PLATFORM_INPUT_ACTION_RIGHT_STICK_UP,
	PLATFORM_INPUT_ACTION_RIGHT_STICK_DOWN,
};

static int s_nativeOptionsSessionOpen;
static int s_nativeLanguageFirstRun;
static int s_nativeOptionsPage;
static int s_nativeOptionsTargetPage;
static int s_nativeOptionsTransitionActive;
static int s_nativeOptionsTransitionDirection;
static int s_nativeOptionsTransitionFrame;
static int s_nativeOptionsEntryActive;
static int s_nativeOptionsEntryFrame;
static int s_nativeOptionsExitActive;
static int s_nativeOptionsExitFrame;
static s16 s_nativeOptionsRows[12];
/* Unlike volume sliders, custom music choices persist per level and need
 * pagination when hundreds of community songs are installed. */
static int s_nativeMusicLevelPage;
static int s_nativeMusicSongPage;
static int s_nativeMusicPackSongPage;
static int s_nativeMusicSelectedPack;
static int s_nativeMusicSelectedLevel;
static int s_nativeMusicCategory;

static int s_nativeControlsView;
static int s_nativeControlsPlayer;
static int s_nativeControlsRow;
static int s_nativeControlsScroll;
static int s_nativeAdvancedGraphicsScroll;
/* Don't let stationary mouse hover undo wheel/keyboard/controller scrolling. */
static int s_nativeAdvancedMouseX = -1;
static int s_nativeAdvancedMouseY = -1;
static int s_nativeControlsPowerSlideSide;
static int s_nativeControlsCaptureAction = -1;
static int s_nativeControlsCaptureDeviceIndex = -1;
static int s_nativeControlsWaitForNeutral;
static int s_nativeControlsNameEditing;
static int s_nativeControlsNamePlayer = -1;
static int s_nativeControlsNameLen;
static char s_nativeControlsNameBuffer[32];
/* A single graphics transaction spans the regular AND advanced pages.
 * Selecting values only edits this draft. Neither the live renderer nor
 * ctr_settings.cfg changes until APPLY CHANGES is explicitly selected.
 * Going Back without applying discards the draft. */
typedef struct NativeGraphicsDraft
{
	int valid;
	int resolutionIndex;
	int resolutionWidth,resolutionHeight;
	int fullscreen,aspectMode,fpsLimit,characterDetail;
	int subpixel,textureFilter,dithering,perfOverlay,gpuTiming;
	int compactVRAM,zbuffer;
} NativeGraphicsDraft;
static NativeGraphicsDraft s_nativeGraphicsDraft;

static void MainFreeze_NativeGraphicsEnsureDraft(void)
{
	NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
	if(p->valid)return;
	memset(p,0,sizeof(*p));
	p->resolutionIndex=Platform_GetResolutionPresetIndex();
	Platform_GetWindowSize(&p->resolutionWidth,&p->resolutionHeight);
	p->fullscreen=Platform_GetFullscreenMode();
	p->aspectMode=Platform_GetAspectRatioMode();
	p->fpsLimit=Platform_GetFPSLimitMode();
	p->characterDetail=Platform_GetCharacterDetailMode();
	p->subpixel=Platform_GetSubpixelMode();
	p->textureFilter=Platform_GetTextureFilteringMode();
	p->dithering=Platform_GetDitheringMode();
	p->perfOverlay=Platform_GetPerfOverlayMode();
	p->gpuTiming=Platform_GetGpuTimingMode();
	p->compactVRAM=Platform_GetExperimentalCompactVRAMMode();
	p->zbuffer=Platform_GetExperimentalZBufferMode();
	p->valid=1;
}

static int MainFreeze_NativeGraphicsIsDirty(void)
{
	const NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
	if(!p->valid)return 0;
	return p->resolutionIndex!=Platform_GetResolutionPresetIndex() ||
	       p->fullscreen!=Platform_GetFullscreenMode() ||
	       p->aspectMode!=Platform_GetAspectRatioMode() ||
	       p->fpsLimit!=Platform_GetFPSLimitMode() ||
	       p->characterDetail!=Platform_GetCharacterDetailMode() ||
	       p->subpixel!=Platform_GetSubpixelMode() ||
	       p->textureFilter!=Platform_GetTextureFilteringMode() ||
	       p->dithering!=Platform_GetDitheringMode() ||
	       p->perfOverlay!=Platform_GetPerfOverlayMode() ||
	       p->gpuTiming!=Platform_GetGpuTimingMode() ||
	       p->compactVRAM!=Platform_GetExperimentalCompactVRAMMode() ||
	       p->zbuffer!=Platform_GetExperimentalZBufferMode();
}

static void MainFreeze_NativeGraphicsApply(void)
{
	NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
	MainFreeze_NativeGraphicsEnsureDraft();
	if(!MainFreeze_NativeGraphicsIsDirty())return;

	/* Apply at the most stable order: resolution, fullscreen, aspect,
	 * frame rate, detail, and finally any advanced renderer toggles. */
	if(p->resolutionIndex>=0 &&
	   p->resolutionIndex!=Platform_GetResolutionPresetIndex())
		Platform_SetResolutionPreset(p->resolutionIndex);
	if(p->fullscreen!=Platform_GetFullscreenMode())
		Platform_SetFullscreenMode(p->fullscreen);
	if(p->aspectMode!=Platform_GetAspectRatioMode())
		Platform_SetAspectRatioMode(p->aspectMode);
	if(p->fpsLimit!=Platform_GetFPSLimitMode())
		Platform_SetFPSLimitMode(p->fpsLimit);
	if(p->characterDetail!=Platform_GetCharacterDetailMode())
		Platform_SetCharacterDetailMode(p->characterDetail);
	if(p->subpixel!=Platform_GetSubpixelMode())
		Platform_SetSubpixelMode(p->subpixel);
	if(p->textureFilter!=Platform_GetTextureFilteringMode())
		Platform_SetTextureFilteringMode(p->textureFilter);
	if(p->dithering!=Platform_GetDitheringMode())
		Platform_SetDitheringMode(p->dithering);
	if(p->perfOverlay!=Platform_GetPerfOverlayMode())
		Platform_SetPerfOverlayMode(p->perfOverlay);
	if(p->gpuTiming!=Platform_GetGpuTimingMode())
		Platform_SetGpuTimingMode(p->gpuTiming);
	if(p->compactVRAM!=Platform_GetExperimentalCompactVRAMMode())
		Platform_SetExperimentalCompactVRAMMode(p->compactVRAM);
	if(p->zbuffer!=Platform_GetExperimentalZBufferMode())
		Platform_SetExperimentalZBufferMode(p->zbuffer);

	Platform_Log("[GRAPHICS APPLY] pending graphics settings committed\n");
	p->valid=0;
	MainFreeze_NativeGraphicsEnsureDraft();
}


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

/* First-launch request must enter exactly the same native Options UI
 * as the manually selectable LANGUAGE item. Title state stays intact. */
void MainFreeze_OpenLanguageSetup(struct RectMenu *menu)
{
    s_nativeLanguageFirstRun=1;
    MainFreeze_OpenTitleOptions(menu);
    Platform_Log("[LANGUAGE] first-run chooser opened\n");
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
	if (s_nativeControlsNameEditing)
		Platform_StopTextInput();

	s_nativeControlsNameEditing = 0;
	s_nativeControlsNamePlayer = -1;
	s_nativeControlsNameLen = 0;
	s_nativeControlsNameBuffer[0] = 0;

	s_nativeGraphicsDraft.valid=0;
	s_nativeOptionsSessionOpen = 1;
    s_nativeOptionsPage = s_nativeLanguageFirstRun ?
                           NATIVE_OPTIONS_PAGE_LANGUAGE : NATIVE_OPTIONS_PAGE_ROOT;
    s_nativeOptionsTargetPage = s_nativeOptionsPage;
	s_nativeOptionsTransitionActive = 0;
	s_nativeOptionsTransitionDirection = 1;
	s_nativeOptionsTransitionFrame = 0;
    /* Display the first-launch language picker immediately, not an
     * unrelated Options root screen that can be dismissed by mistake. */
    s_nativeOptionsEntryActive = s_nativeLanguageFirstRun ? 0 : 1;
	s_nativeOptionsEntryFrame = 0;
	s_nativeOptionsExitActive = 0;
	s_nativeOptionsExitFrame = 0;
	memset(s_nativeOptionsRows, 0, sizeof(s_nativeOptionsRows));
	s_nativeMusicLevelPage=0;
	s_nativeMusicSongPage=0;
    s_nativeMusicPackSongPage=0;
    s_nativeMusicSelectedPack=0;
	s_nativeMusicSelectedLevel=0;
	s_nativeMusicCategory=0;

	s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYERS;
	s_nativeControlsPlayer = 0;
	s_nativeControlsRow = 0;
	s_nativeControlsScroll = 0;
	s_nativeAdvancedGraphicsScroll = 0;
	s_nativeAdvancedMouseX = -1;
	s_nativeAdvancedMouseY = -1;
	s_nativeControlsPowerSlideSide = 0;
	s_nativeControlsCaptureAction = -1;
	s_nativeControlsCaptureDeviceIndex = -1;
	s_nativeControlsWaitForNeutral = 0;
	Platform_InputCancelRebindCapture();

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
	/* Explicit Back from Graphics discards unconfirmed choices. */
	if(targetPage==NATIVE_OPTIONS_PAGE_ROOT &&
	   (s_nativeOptionsPage==NATIVE_OPTIONS_PAGE_GRAPHICS ||
	    s_nativeOptionsPage==NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS))
		s_nativeGraphicsDraft.valid=0;
	if (targetPage == NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS)
	{
		s_nativeAdvancedMouseX = -1;
		s_nativeAdvancedMouseY = -1;
	}
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

	s_nativeGraphicsDraft.valid=0;
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

static void MainFreeze_NativeControlsDrawFrame(char *title, int offsetX, int height)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	Color color;
	RECT separator = {.x = 112 + offsetX, .y = 48, .w = 326, .h = 2};
	RECT panel = {.x = 100 + offsetX, .y = 20, .w = 350, .h = height};

	/* Controls submenus use the compact font so the title never collides
	   with page counters or the top-left mouse BACK button. */
	DecalFont_DrawLine(title, 275 + offsetX, 32, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	color.self = sdata->battleSetup_Color_UI_1;
	RECTMENU_DrawOuterRect_Edge(&separator, color, 0x20, ot);
	RECTMENU_DrawInnerRect(&panel, 4, ot);
}

static void MainFreeze_NativeControlsDrawCursor(int offsetX, int y, int height)
{
	RECT cursor = {.x = 108 + offsetX, .y = y, .w = 332, .h = height};
	CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal,
		TRANS_50_DECAL, sdata->gGT->backBuffer->otMem.uiOT);
}

static void MainFreeze_NativeOptionsDrawRoot(int offsetX)
{
	static char *labels[NATIVE_OPTIONS_ROOT_ROW_COUNT] = {
		"AUDIO",
		"GRAPHICS",
		"GAMEPLAY",
		"CONTROLS",
        "LANGUAGE",
		"EXIT",
	};

	for (int i = 0; i < NATIVE_OPTIONS_ROOT_ROW_COUNT; i++)
	{
		const int y = 67 + (i * 22);
		DecalFont_DrawLine(labels[i], 256 + offsetX, y, FONT_BIG, JUSTIFY_CENTER | ORANGE);
	}

	MainFreeze_NativeOptionsDrawCursor(offsetX, 65 + (s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ROOT] * 22), 19);
	MainFreeze_NativeOptionsDrawFrame("OPTIONS", offsetX, 191);
}

static void MainFreeze_NativeOptionsDrawActionBinding(
	int playerIndex,
	int action,
	int offsetX,
	int y)
{
	char binding[64];
	const int kind = Platform_InputGetPlayerDeviceKind(playerIndex);
	int drewGlyph = 0;

	/* In mouse mode, navigation rows stay text-only so controller/keyboard
	   prompts do not compete with the pointer. Remap-content glyphs use a
	   different path and intentionally remain visible. */
	if (RECTMENU_NativeTouchGet(NULL, NULL))
		return;

	if (kind != PLATFORM_INPUT_DEVICE_NONE &&
	    Platform_InputResolvePlayerGlyphStyle(playerIndex) !=
		    PLATFORM_INPUT_GLYPH_KEYBOARD)
	{
		/* Prompt visuals are independent from the physical device. This lets
		   a keyboard player force PlayStation/Xbox/Generic presentation while
		   keeping the actual keyboard bindings untouched. */
		const int rawBinding =
			Platform_InputGetGamepadBindingForPlayer(playerIndex, action);

		drewGlyph = Platform_InputGlyphsDrawBinding(
			playerIndex,
			rawBinding,
			401 + offsetX, y - 3, 30, 14,
			&sdata->gGT->backBuffer->primMem,
			sdata->gGT->backBuffer->otMem.uiOT);
	}

	if (drewGlyph)
		return;

	Platform_InputFormatPlayerBindingName(
		playerIndex, action, binding, sizeof(binding));

	DecalFont_DrawLine(
		binding,
		434 + offsetX, y,
		FONT_SMALL,
		JUSTIFY_RIGHT |
			(kind == PLATFORM_INPUT_DEVICE_NONE ? GRAY : ORANGE));
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

/* Layout guard for every native settings language. Fonts used to overlap
 * their right-column value when PAL captions were longer than US English.
 * Scale ONLY this left label, keeping live values and button prompts legible.
 * Uses the actual bitmap-font width, not a rough strlen approximation. */
static void MainFreeze_NativeOptionsDrawFittedLabel(
    const char *label,const char *rightValue,int offsetX,int y)
{
    const int leftX=78,rightX=434,gap=14;
    int labelWidth;
    int rightWidth;
    int available;
    s16 scale=FP(1.0);
    if (!label) return;
    labelWidth=DecalFont_GetLineWidth((char *)label,FONT_SMALL);
    rightWidth=rightValue ?
        DecalFont_GetLineWidth((char *)rightValue,FONT_SMALL):0;
    available=rightX-leftX-gap-rightWidth;
    if (available<10) available=10;
    if (labelWidth>available)
    {
        int scaled=(available*FP(1.0))/labelWidth;
        if (scaled<1) scaled=1;
        scale=(s16)scaled;
    }
    DecalFont_DrawLineScaledNative((char *)label,leftX+offsetX,y,
                                   FONT_SMALL,ORANGE,scale);
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

	MainFreeze_NativeOptionsDrawFittedLabel(sdata->lngStrings[LNG_MODE],
       sdata->lngStrings[333 + howl_ModeGet()],offsetX,116);
	DecalFont_DrawLine(sdata->lngStrings[333 + howl_ModeGet()], 434 + offsetX, 116, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	MainFreeze_NativeOptionsDrawFittedLabel("HEAR OTHER RACERS",
        Platform_GetHearOtherRacers() ? "YES" : "NO",offsetX,134);
    DecalFont_DrawLine(Platform_GetHearOtherRacers() ? "YES" : "NO",
                       434 + offsetX, 134, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
    MainFreeze_NativeOptionsDrawFittedLabel("MUSIC","CHOOSE SONGS >",offsetX,152);
    DecalFont_DrawLine("CHOOSE SONGS >", 434 + offsetX, 152,
                       FONT_SMALL, JUSTIFY_RIGHT | WHITE);
    DecalFont_DrawLine("BACK", 78 + offsetX, 170, FONT_SMALL, ORANGE);
    MainFreeze_NativeOptionsDrawActionBinding(
        0, PLATFORM_INPUT_ACTION_TRIANGLE, offsetX, 170);

    MainFreeze_NativeOptionsDrawCursor(offsetX, 59 + (row * 18), 14);
    MainFreeze_NativeOptionsDrawFrame("AUDIO", offsetX, 191);
}

static void MainFreeze_NativeFormatFPSLimit(char *text, int textSize)
{
	const int mode = Platform_GetFPSLimitMode();

	if ((text == NULL) || (textSize <= 0))
		return;

	if (mode == PLATFORM_FPS_LIMIT_UNLIMITED)
		snprintf(text, (size_t)textSize, "Unlimited");
	else if (mode == PLATFORM_FPS_LIMIT_DISPLAY_HZ)
		snprintf(text, (size_t)textSize, "%d HZ", Platform_GetDisplayRefreshFPS());
	else
		snprintf(text, (size_t)textSize, "%d FPS", Platform_GetHighRefreshTargetFPS());
}

static void MainFreeze_NativeOptionsDrawGraphics(int offsetX)
{
	MainFreeze_NativeGraphicsEnsureDraft();
	const NativeGraphicsDraft *draft=&s_nativeGraphicsDraft;
	const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GRAPHICS];
	const int dirty=MainFreeze_NativeGraphicsIsDirty();
	char refreshText[24],resolutionText[32];
	int resolutionW=draft->resolutionWidth;
	int resolutionH=draft->resolutionHeight;
	if(draft->resolutionIndex>=0)
		Platform_GetResolutionPresetSize(draft->resolutionIndex,
			&resolutionW,&resolutionH);
	snprintf(resolutionText,sizeof(resolutionText),"%dx%d",resolutionW,resolutionH);

	MainFreeze_NativeOptionsDrawFittedLabel("RESOLUTION",resolutionText,offsetX,54);
	DecalFont_DrawLine(resolutionText,434+offsetX,54,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	const char *display=draft->fullscreen ? "FULLSCREEN" : "WINDOWED";
	MainFreeze_NativeOptionsDrawFittedLabel("DISPLAY MODE",display,offsetX,72);
	DecalFont_DrawLine((char *)display,434+offsetX,72,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	const char *aspect=Platform_GetAspectRatioLabel(draft->aspectMode);
	MainFreeze_NativeOptionsDrawFittedLabel("ASPECT",aspect,offsetX,90);
	DecalFont_DrawLine((char *)aspect,434+offsetX,90,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	if(draft->fpsLimit==PLATFORM_FPS_LIMIT_UNLIMITED)
		snprintf(refreshText,sizeof(refreshText),"Unlimited");
	else if(draft->fpsLimit==PLATFORM_FPS_LIMIT_DISPLAY_HZ)
		snprintf(refreshText,sizeof(refreshText),"%d HZ",Platform_GetDisplayRefreshFPS());
	else
	{
		static const int fps[]={30,60,120,144,200,240};
		const int mode=draft->fpsLimit;
		snprintf(refreshText,sizeof(refreshText),"%d FPS",
			(mode>=0 && mode<6)?fps[mode]:60);
	}
	MainFreeze_NativeOptionsDrawFittedLabel("FPS LIMIT",refreshText,offsetX,108);
	DecalFont_DrawLine(refreshText,434+offsetX,108,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	const int detail=draft->characterDetail;
	const char *detailText=detail==0?"LOW":
		(detail==1?"SMART":(detail==2?"HIGH":"ULTRA"));
	MainFreeze_NativeOptionsDrawFittedLabel("CHARACTER DETAIL",detailText,offsetX,126);
	DecalFont_DrawLine((char *)detailText,434+offsetX,126,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	MainFreeze_NativeOptionsDrawFittedLabel("ADVANCED GRAPHICS",">",offsetX,144);
	DecalFont_DrawLine(">",434+offsetX,144,FONT_SMALL,JUSTIFY_RIGHT|WHITE);

	MainFreeze_NativeOptionsDrawFittedLabel("APPLY CHANGES",
		dirty ? "PENDING" : "SAVED",offsetX,162);
	DecalFont_DrawLine(dirty?"PENDING":"SAVED",
		434+offsetX,162,FONT_SMALL,JUSTIFY_RIGHT|(dirty?ORANGE:WHITE));

	DecalFont_DrawLine("BACK",78+offsetX,180,FONT_SMALL,ORANGE);
	MainFreeze_NativeOptionsDrawActionBinding(
		0,PLATFORM_INPUT_ACTION_TRIANGLE,offsetX,180);

	MainFreeze_NativeOptionsDrawCursor(offsetX,51+(row*18),14);
	MainFreeze_NativeOptionsDrawFrame("GRAPHICS",offsetX,199);
}

/* Scoped uniform font. The retail line renderer computes justification
 * before it scales glyphs: offset its anchor to preserve proper right align. */
static void MainFreeze_NativeAdvancedCompactText(
    const char *text,int x,int y,int flags)
{
    const s16 scale=(s16)(((int)FP(1.0)*
        NATIVE_ADVANCED_GRAPHICS_FONT_PERCENT+50)/100);
    int width=DecalFont_GetLineWidth((char *)text,FONT_SMALL);
    int drawn=(int)FP_Mult(width,scale);
    if(flags&JUSTIFY_RIGHT)
        x+=width-drawn;
    else if(flags&JUSTIFY_CENTER)
        x+=(width-drawn)/2;
    DecalFont_DrawLineScaledNative((char *)text,x,y,FONT_SMALL,flags,scale);
}

/* Same selected-row to viewport logic as Controls / Remap, including wrap. */
static void MainFreeze_NativeAdvancedClampScroll(void)
{
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS];
    const int maxScroll=NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT-
                        NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS;
    if(row<s_nativeAdvancedGraphicsScroll)
        s_nativeAdvancedGraphicsScroll=row;
    else if(row>=s_nativeAdvancedGraphicsScroll+
                 NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS)
        s_nativeAdvancedGraphicsScroll=row-
            NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS+1;
    if(s_nativeAdvancedGraphicsScroll<0)
        s_nativeAdvancedGraphicsScroll=0;
    if(s_nativeAdvancedGraphicsScroll>maxScroll)
        s_nativeAdvancedGraphicsScroll=maxScroll;
}

static void MainFreeze_NativeOptionsDrawAdvancedGraphics(int offsetX)
{
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS];
    char rangeText[32];
    MainFreeze_NativeGraphicsEnsureDraft();
    const NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
    static const char *labels[NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT]={
        "INTERNAL RESOLUTION","SUBPIXEL","TEXTURE FILTER",
        "ANTI-ALIASING","WORLD DETAIL","PRESENT FILTER",
        "DITHER","CRT FILTER","PERF OVERLAY","GPU TIMING [EXP]",
        "VRAM STORAGE [EXP]","Z-BUFFER [EXP]","APPLY CHANGES","BACK"
    };
    const char *rightValues[NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT]={
        "NATIVE",
        p->subpixel ? "ON" : "OFF",
        p->textureFilter ? "BILINEAR" : "NEAREST",
        "OFF","RETAIL","DIRECT",
        p->dithering ? "PS1" : "OFF",
        "OFF",
        p->perfOverlay ? "ON" : "OFF",
        p->gpuTiming ? "ON EXP" : "OFF",
        p->compactVRAM ? "RG8 EXP" : "RG32F",
        p->zbuffer ? "CUSTOM EXP" : "OFF",
        MainFreeze_NativeGraphicsIsDirty() ? "PENDING" : "SAVED",
        ""
    };

    MainFreeze_NativeAdvancedClampScroll();
    for(int visible=0;visible<NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS;visible++)
    {
        const int absolute=s_nativeAdvancedGraphicsScroll+visible;
        const int y=NATIVE_ADVANCED_GRAPHICS_TEXT_Y+
                    visible*NATIVE_ADVANCED_GRAPHICS_ROW_STEP;
        const int readOnly=absolute==0 || absolute==3 || absolute==4 ||
                           absolute==5 || absolute==7;
        if(absolute>=NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT)
            break;

        /* Both columns are 75% of FONT_SMALL: no individually scaled rows. */
        MainFreeze_NativeAdvancedCompactText(
            labels[absolute],78+offsetX,y,ORANGE);
        if(absolute==NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT-1)
            MainFreeze_NativeOptionsDrawActionBinding(
                0,PLATFORM_INPUT_ACTION_TRIANGLE,offsetX,y);
        else
            MainFreeze_NativeAdvancedCompactText(
                rightValues[absolute],434+offsetX,y,
                JUSTIFY_RIGHT|(readOnly ? GRAY : WHITE));
    }

    MainFreeze_NativeOptionsDrawCursor(
        offsetX,NATIVE_ADVANCED_GRAPHICS_ROW_TOP_Y+
            (row-s_nativeAdvancedGraphicsScroll)*
            NATIVE_ADVANCED_GRAPHICS_ROW_STEP,
        NATIVE_ADVANCED_GRAPHICS_ROW_HEIGHT);
    snprintf(rangeText,sizeof(rangeText),"%d-%d / %d",
        s_nativeAdvancedGraphicsScroll+1,
        s_nativeAdvancedGraphicsScroll+NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS,
        NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT);
    MainFreeze_NativeAdvancedCompactText(
        rangeText,432+offsetX,202,JUSTIFY_RIGHT|ORANGE);
    MainFreeze_NativeOptionsDrawFrame(
        "ADVANCED GRAPHICS",offsetX,NATIVE_ADVANCED_GRAPHICS_FRAME_HEIGHT);
}

static int MainFreeze_NativeOptionsGameplayHasHeight(void)
{
	const int driftMode = NativeHUD_GetDriftMeterMode();
	return driftMode == NATIVE_HUD_DRIFT_CNK_CENTERED ||
	       driftMode == NATIVE_HUD_DRIFT_CNK_3D;
}

static int MainFreeze_NativeOptionsGameplayRowCount(void)
{
	return NATIVE_OPTIONS_GAMEPLAY_ROW_COUNT +
	       MainFreeze_NativeOptionsGameplayHasHeight();
}

static void MainFreeze_NativeOptionsDrawGameplay(int offsetX)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GAMEPLAY];
	const int driftMode = NativeHUD_GetDriftMeterMode();
	const char *driftText =
		driftMode == NATIVE_HUD_DRIFT_CTR_CENTERED ? "CTR CENTERED" :
		(driftMode == NATIVE_HUD_DRIFT_CNK_CENTERED ? "CNK CENTERED" :
		(driftMode == NATIVE_HUD_DRIFT_CNK_3D ? "CNK 3D" : "OFF"));

	MainFreeze_NativeOptionsDrawFittedLabel("SAFFI HUD",
       NativeHUD_GetSaffiEnabled() ? "ON" : "OFF",offsetX,62);
	DecalFont_DrawLine(
		NativeHUD_GetSaffiEnabled() ? "ON" : "OFF",
		434 + offsetX, 62, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	MainFreeze_NativeOptionsDrawFittedLabel("RESERVES BAR",
       NativeHUD_GetReservesBarEnabled() ? "ON" : "OFF",offsetX,84);
	DecalFont_DrawLine(
		NativeHUD_GetReservesBarEnabled() ? "ON" : "OFF",
		434 + offsetX, 84, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	MainFreeze_NativeOptionsDrawFittedLabel("DRIFT METER",driftText,offsetX,106);
	DecalFont_DrawLine(
		driftText,
		434 + offsetX, 106, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	const int hasHeight = MainFreeze_NativeOptionsGameplayHasHeight();
	const int backY = hasHeight ? 154 : 132;
	if (hasHeight)
	{
		MainFreeze_NativeOptionsDrawFittedLabel("METER HEIGHT",
       NativeHUD_GetCNKMeterHeight()==NATIVE_HUD_CNK_HEIGHT_HIGH ? "HIGH" : "LOW",offsetX,128);
		DecalFont_DrawLine(
			NativeHUD_GetCNKMeterHeight() == NATIVE_HUD_CNK_HEIGHT_HIGH
				? "HIGH" : "LOW",
			434 + offsetX, 128, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	}
	DecalFont_DrawLine("BACK", 78 + offsetX, backY, FONT_SMALL, ORANGE);
	MainFreeze_NativeOptionsDrawActionBinding(
		0, PLATFORM_INPUT_ACTION_TRIANGLE, offsetX, backY);

	MainFreeze_NativeOptionsDrawCursor(
		offsetX,
		59 + (row * 22),
		16);
	MainFreeze_NativeOptionsDrawFrame("GAMEPLAY", offsetX,
		hasHeight ? 172 : 150);
}

static int MainFreeze_NativeControlsRemapActionCount(void)
{
	const int kind = Platform_InputGetPlayerDeviceKind(s_nativeControlsPlayer);

	/* Keyboard exposes every digital action separately, including L1 and R1. */
	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
		return 16;
	if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
		return (int)(sizeof(s_nativeControlsRemapRows) /
		             sizeof(s_nativeControlsRemapRows[0]));

	return 0;
}

static int MainFreeze_NativeControlsRemapRowIsPowerSlide(int row)
{
	(void)row;
	return 0;
}

static int MainFreeze_NativeControlsRemapRowAction(int row)
{
	const int actionCount = MainFreeze_NativeControlsRemapActionCount();
	int action;

	if (row < 0 || row >= actionCount)
		return -1;

	action = s_nativeControlsRemapRows[row];
	return action;
}

static const char *MainFreeze_NativeControlsRemapLabel(int row, int action)
{
	/* Gamepad-only stick rows begin after the 16 digital rows. */
	if (row >= 16)
	{
		switch (row)
		{
		/* Compact labels fit the PS1-width menu without ellipses. */
		case 16: return "L-STICK LEFT";
		case 17: return "L-STICK RIGHT";
		case 18: return "L-STICK UP";
		case 19: return "L-STICK DOWN";
		case 20: return "R-STICK LEFT";
		case 21: return "R-STICK RIGHT";
		case 22: return "R-STICK UP";
		case 23: return "R-STICK DOWN";
		default: break;
		}
	}

	switch (action)
	{
	case PLATFORM_INPUT_ACTION_CROSS: return "ACCELERATE";
	case PLATFORM_INPUT_ACTION_SQUARE: return "BRAKE / REVERSE";
	case PLATFORM_INPUT_ACTION_CIRCLE: return "ITEM";
	/* Keep the visible label as requested even though the retail logical
	   action behind this slot is the Triangle/HUD-lookback channel. */
	case PLATFORM_INPUT_ACTION_TRIANGLE: return "TRIANGLE";

	case PLATFORM_INPUT_ACTION_DPAD_UP: return "D-PAD UP";
	case PLATFORM_INPUT_ACTION_DPAD_DOWN: return "D-PAD DOWN";
	case PLATFORM_INPUT_ACTION_DPAD_LEFT: return "D-PAD LEFT";
	case PLATFORM_INPUT_ACTION_DPAD_RIGHT: return "D-PAD RIGHT";

	case PLATFORM_INPUT_ACTION_L1: return "L1 - POWERSLIDE";
	case PLATFORM_INPUT_ACTION_R1: return "R1 - POWERSLIDE";
	case PLATFORM_INPUT_ACTION_L2: return "L2";
	case PLATFORM_INPUT_ACTION_R2: return "R2";
	case PLATFORM_INPUT_ACTION_L3: return "L3";
	case PLATFORM_INPUT_ACTION_R3: return "R3";
	case PLATFORM_INPUT_ACTION_START: return "START / PAUSE";
	case PLATFORM_INPUT_ACTION_SELECT: return "SELECT";

	case PLATFORM_INPUT_ACTION_LEFT_X: return "L-STICK LEFT / RIGHT";
	case PLATFORM_INPUT_ACTION_LEFT_Y: return "L-STICK UP / DOWN";
	case PLATFORM_INPUT_ACTION_RIGHT_X: return "R-STICK LEFT / RIGHT";
	case PLATFORM_INPUT_ACTION_RIGHT_Y: return "R-STICK UP / DOWN";
	default: return Platform_InputGetActionDisplayName(action);
	}
}

static int MainFreeze_NativeOptionsControlsRowCount(void)
{
	switch (s_nativeControlsView)
	{
	case NATIVE_CONTROLS_VIEW_PLAYER:
		return NATIVE_CONTROLS_PLAYER_CONFIG_ROW_COUNT;

	case NATIVE_CONTROLS_VIEW_REMAP:
		return MainFreeze_NativeControlsRemapActionCount() + 1;

	default:
		return NATIVE_CONTROLS_PLAYER_LIST_ROW_COUNT;
	}
}

static void MainFreeze_NativeControlsFitText(char *text, int maxWidth)
{
	int len;

	if (text == NULL || maxWidth < 16)
		return;

	if (DecalFont_GetLineWidth(text, FONT_SMALL) <= maxWidth)
		return;

	len = (int)strlen(text);
	while (len > 3)
	{
		text[len - 3] = '.';
		text[len - 2] = '.';
		text[len - 1] = '.';
		text[len] = 0;
		if (DecalFont_GetLineWidth(text, FONT_SMALL) <= maxWidth)
			return;
		len--;
		text[len] = 0;
	}

	snprintf(text, 4, "...");
}

static void MainFreeze_NativeControlsGetDeviceText(
	int playerIndex,
	char *out,
	int outSize)
{
	int kind;
	int deviceIndex;
	struct PlatformInputDeviceInfo info;

	if (out == NULL || outSize <= 0)
		return;

	out[0] = 0;
	kind = Platform_InputGetPlayerDeviceKind(playerIndex);

	if (kind == PLATFORM_INPUT_DEVICE_NONE)
	{
		snprintf(out, (size_t)outSize, "UNASSIGNED");
		return;
	}

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		snprintf(out, (size_t)outSize, "KEYBOARD");
		return;
	}

	deviceIndex = Platform_InputGetPlayerDeviceIndex(playerIndex);
	if (deviceIndex >= 1 &&
	    Platform_InputGetDeviceInfo(deviceIndex, &info))
	{
		snprintf(out, (size_t)outSize, "%s", info.name);
		MainFreeze_NativeControlsFitText(out, 150);
		return;
	}

	snprintf(out, (size_t)outSize, "GAMEPAD (DISCONNECTED)");
}

static void MainFreeze_NativeControlsGetGlyphText(
	int playerIndex,
	char *out,
	int outSize)
{
	int requested;
	int resolved;

	if (out == NULL || outSize <= 0)
		return;

	requested = Platform_InputGetPlayerGlyphStyle(playerIndex);
	resolved = Platform_InputResolvePlayerGlyphStyle(playerIndex);

	if (requested == PLATFORM_INPUT_GLYPH_AUTO)
	{
		snprintf(out, (size_t)outSize, "AUTO (%s)",
		         Platform_InputGetGlyphStyleName(resolved));
	}
	else
	{
		snprintf(out, (size_t)outSize, "%s",
		         Platform_InputGetGlyphStyleName(requested));
	}
}

static void MainFreeze_NativeControlsDrawStateSlotAt(
	int offsetX,
	int x,
	int y,
	int waiting)
{
	struct GameTracker *gGT = sdata->gGT;
	RECT slot = {
		.x = (s16)(x + offsetX),
		.y = (s16)(y - 2),
		.w = 24,
		.h = 11,
	};
	Color color;

	if (Platform_InputGlyphsDrawState(
			waiting,
			slot.x, slot.y, slot.w, slot.h,
			&gGT->backBuffer->primMem,
			gGT->backBuffer->otMem.uiOT))
	{
		return;
	}

	/* Asset missing/renderer unavailable: retain a safe retail-style fallback. */
	color.self = waiting ?
		sdata->menuRowHighlight_Normal.self :
		sdata->battleSetup_Color_UI_1;

	if (waiting)
		CTR_Box_DrawClearBox(&slot, &color, TRANS_50_DECAL,
		                     gGT->backBuffer->otMem.uiOT);
	else
		RECTMENU_DrawOuterRect_Edge(
			&slot, color, 0x20, gGT->backBuffer->otMem.uiOT);

	if (waiting)
		DecalFont_DrawLine("...", x + 12 + offsetX, y - 1,
		                   FONT_SMALL, JUSTIFY_CENTER | WHITE);
}

static void MainFreeze_NativeControlsDrawEmptySlot(
	int offsetX,
	int y,
	int waiting)
{
	MainFreeze_NativeControlsDrawStateSlotAt(
		offsetX, 405, y, waiting);
}

static void MainFreeze_NativeControlsDrawPlayers(int offsetX)
{
	char label[24];
	char device[PLATFORM_INPUT_DEVICE_NAME_MAX];
	int playerIndex;

	for (playerIndex = 0; playerIndex < PLATFORM_INPUT_PAD_COUNT; playerIndex++)
	{
		const int y = 57 + playerIndex * 22;
		const int hasDevice =
			Platform_InputGetPlayerDeviceKind(playerIndex) !=
			PLATFORM_INPUT_DEVICE_NONE;
		const int textColor = hasDevice ? ORANGE : GRAY;

		snprintf(label, sizeof(label), "%s %d",
		         NativeLocalization_Translate("PLAYER"), playerIndex + 1);
		MainFreeze_NativeControlsGetDeviceText(
			playerIndex, device, sizeof(device));

		DecalFont_DrawLine(label, 112 + offsetX, y,
		                   FONT_SMALL, textColor);
		DecalFont_DrawLine(device, 432 + offsetX, y,
		                   FONT_SMALL, JUSTIFY_RIGHT | textColor);
	}

	DecalFont_DrawLine("BACK", 112 + offsetX, 145,
	                   FONT_SMALL, ORANGE);
	MainFreeze_NativeOptionsDrawActionBinding(
		0, PLATFORM_INPUT_ACTION_TRIANGLE, offsetX, 145);

	MainFreeze_NativeControlsDrawCursor(
		offsetX, 54 + s_nativeControlsRow * 22, 15);
	MainFreeze_NativeControlsDrawFrame("CONTROLS", offsetX, 170);
}

static void MainFreeze_NativeControlsBeginNameEdit(void)
{
	const char *current;

	if (s_nativeControlsPlayer < 0 ||
	    s_nativeControlsPlayer >= PLATFORM_INPUT_PAD_COUNT)
		return;

	current = Platform_GetPlayerName(s_nativeControlsPlayer);
	snprintf(
		s_nativeControlsNameBuffer,
		sizeof(s_nativeControlsNameBuffer),
		"%s",
		current != NULL ? current : "");

	s_nativeControlsNameLen =
		(int)strlen(s_nativeControlsNameBuffer);
	s_nativeControlsNamePlayer = s_nativeControlsPlayer;
	s_nativeControlsNameEditing = 1;
	Platform_StartTextInput();
	OtherFX_Play(1, 1);
}

static void MainFreeze_NativeControlsEndNameEdit(int commit)
{
	if (!s_nativeControlsNameEditing)
		return;

	if (commit)
	{
		Platform_SetPlayerName(
			s_nativeControlsNamePlayer,
			s_nativeControlsNameBuffer);
		OtherFX_Play(1, 1);
	}
	else
	{
		OtherFX_Play(0, 1);
	}

	s_nativeControlsNameEditing = 0;
	s_nativeControlsNamePlayer = -1;
	s_nativeControlsNameLen = 0;
	s_nativeControlsNameBuffer[0] = 0;
	Platform_StopTextInput();
}

int MainFreeze_NativeControlsHandleTextInput(const char *text)
{
	int i;

	if (!s_nativeControlsNameEditing)
		return 0;

	if (text == NULL)
		return 1;

	for (i = 0;
	     text[i] != 0 &&
	     s_nativeControlsNameLen <
		     (int)sizeof(s_nativeControlsNameBuffer) - 1;
	     i++)
	{
		unsigned char c = (unsigned char)text[i];

		if (c < 0x20 || c == 0x7f)
			continue;

		s_nativeControlsNameBuffer[s_nativeControlsNameLen++] =
			(char)c;
		s_nativeControlsNameBuffer[s_nativeControlsNameLen] = 0;
	}

	return 1;
}

int MainFreeze_NativeControlsHandleHostKey(
	int key,
	int down,
	int repeat)
{
	if (!s_nativeControlsNameEditing)
		return 0;

	if (!down)
		return 1;

	if (key == SDL_SCANCODE_RETURN ||
	    key == SDL_SCANCODE_KP_ENTER)
	{
		if (!repeat)
			MainFreeze_NativeControlsEndNameEdit(1);
		return 1;
	}

	if (key == SDL_SCANCODE_ESCAPE)
	{
		if (!repeat)
			MainFreeze_NativeControlsEndNameEdit(0);
		return 1;
	}

	if (key == SDL_SCANCODE_BACKSPACE)
	{
		if (s_nativeControlsNameLen > 0)
		{
			s_nativeControlsNameLen--;
			s_nativeControlsNameBuffer[
				s_nativeControlsNameLen] = 0;
		}
		return 1;
	}

	/* While editing, keyboard input belongs exclusively to the name field. */
	return 1;
}

static void MainFreeze_NativeControlsDrawPlayer(int offsetX)
{
	struct GameTracker *gGT = sdata->gGT;
	char title[32];
	char device[PLATFORM_INPUT_DEVICE_NAME_MAX];
	char glyph[48];
	const int playerIndex = s_nativeControlsPlayer;
	const int kind = Platform_InputGetPlayerDeviceKind(playerIndex);
	const int deviceTextColor =
		kind == PLATFORM_INPUT_DEVICE_NONE ? GRAY : ORANGE;
	const b32 vibrationOff =
		(gGT->gameMode1 &
		 data.gGT_gameMode1_VibPerPlayer[playerIndex]) != 0;

	snprintf(title, sizeof(title), "%s %d %s",
	         NativeLocalization_Translate("PLAYER"), playerIndex + 1,
	         NativeLocalization_Translate("CONTROLS"));
	MainFreeze_NativeControlsGetDeviceText(playerIndex, device, sizeof(device));
	MainFreeze_NativeControlsGetGlyphText(playerIndex, glyph, sizeof(glyph));

	DecalFont_DrawLine("DEVICE", 112 + offsetX, 52,
	                   FONT_SMALL, deviceTextColor);
	DecalFont_DrawLine(device, 432 + offsetX, 52,
	                   FONT_SMALL, JUSTIFY_RIGHT | deviceTextColor);

	DecalFont_DrawLine("PLAYER NAME", 112 + offsetX, 70,
	                   FONT_SMALL, ORANGE);
	{
		char playerName[40];
		const char *storedName =
			Platform_GetPlayerName(playerIndex);

		if (s_nativeControlsNameEditing &&
		    s_nativeControlsNamePlayer == playerIndex)
		{
			snprintf(
				playerName,
				sizeof(playerName),
				"%s_",
				s_nativeControlsNameBuffer);
		}
		else
		{
			snprintf(
				playerName,
				sizeof(playerName),
				"%s",
				storedName != NULL ? storedName : "PLAYER");
		}

		MainFreeze_NativeControlsFitText(playerName, 150);
		DecalFont_DrawLine(
			playerName,
			432 + offsetX,
			70,
			FONT_SMALL,
			JUSTIFY_RIGHT |
			(s_nativeControlsNameEditing &&
			 s_nativeControlsNamePlayer == playerIndex ?
				PERIWINKLE : ORANGE));
	}

	DecalFont_DrawLine("BUTTON ICONS", 112 + offsetX, 88,
	                   FONT_SMALL, deviceTextColor);
	MainFreeze_NativeControlsFitText(glyph, 150);
	DecalFont_DrawLine(glyph, 432 + offsetX, 88,
	                   FONT_SMALL, JUSTIFY_RIGHT | deviceTextColor);

	DecalFont_DrawLine("VIBRATION", 112 + offsetX, 106,
	                   FONT_SMALL,
	                   kind == PLATFORM_INPUT_DEVICE_GAMEPAD ? ORANGE : GRAY);
	DecalFont_DrawLine(
		kind == PLATFORM_INPUT_DEVICE_GAMEPAD ?
			(vibrationOff ? "OFF" : "ON") : "N/A",
		432 + offsetX, 106, FONT_SMALL,
		JUSTIFY_RIGHT |
		(kind == PLATFORM_INPUT_DEVICE_GAMEPAD ? ORANGE : GRAY));

	DecalFont_DrawLine("REMAP CONTROLS", 112 + offsetX, 124,
	                   FONT_SMALL,
	                   kind == PLATFORM_INPUT_DEVICE_NONE ? GRAY : ORANGE);
	DecalFont_DrawLine("RESET DEFAULTS", 112 + offsetX, 142,
	                   FONT_SMALL,
	                   kind == PLATFORM_INPUT_DEVICE_NONE ? GRAY : ORANGE);
	DecalFont_DrawLine("BACK", 112 + offsetX, 160,
	                   FONT_SMALL, ORANGE);
	MainFreeze_NativeOptionsDrawActionBinding(
		0, PLATFORM_INPUT_ACTION_TRIANGLE, offsetX, 160);

	MainFreeze_NativeControlsDrawCursor(
		offsetX, 49 + s_nativeControlsRow * 18, 15);
	MainFreeze_NativeControlsDrawFrame(title, offsetX, 184);
}

static void MainFreeze_NativeControlsClampScroll(void)
{
	const int rowCount = MainFreeze_NativeOptionsControlsRowCount();
	const int maxScroll =
		rowCount > NATIVE_CONTROLS_REMAP_VISIBLE_ROWS ?
		rowCount - NATIVE_CONTROLS_REMAP_VISIBLE_ROWS : 0;

	if (s_nativeControlsRow < s_nativeControlsScroll)
		s_nativeControlsScroll = s_nativeControlsRow;
	if (s_nativeControlsRow >=
	    s_nativeControlsScroll + NATIVE_CONTROLS_REMAP_VISIBLE_ROWS)
	{
		s_nativeControlsScroll =
			s_nativeControlsRow -
			NATIVE_CONTROLS_REMAP_VISIBLE_ROWS + 1;
	}

	if (s_nativeControlsScroll < 0)
		s_nativeControlsScroll = 0;
	if (s_nativeControlsScroll > maxScroll)
		s_nativeControlsScroll = maxScroll;
}

static void MainFreeze_NativeControlsFormatRemapBinding(
	int action,
	char *out,
	int outSize)
{
	const int kind =
		Platform_InputGetPlayerDeviceKind(s_nativeControlsPlayer);

	if (out == NULL || outSize <= 0)
		return;

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		const int binding =
			Platform_InputGetKeyboardBindingForPlayer(
				s_nativeControlsPlayer, action);

		if (binding < 0)
		{
			snprintf(out, (size_t)outSize, "--");
			return;
		}

		/* Remapping always shows the physical keyboard key, even when the
		   player forced PlayStation/Xbox/Generic prompt visuals. */
		if (!Platform_InputFormatBindingName(
				PLATFORM_INPUT_DEVICE_KEYBOARD,
				binding,
				out,
				outSize))
		{
			snprintf(out, (size_t)outSize, "--");
		}
		return;
	}

	Platform_InputFormatPlayerBindingName(
		s_nativeControlsPlayer, action, out, outSize);
}

static void MainFreeze_NativeControlsDrawRemap(int offsetX)
{
	char title[32];
	char pageText[24];
	char binding[128];
	const int actionCount = MainFreeze_NativeControlsRemapActionCount();
	const int rowCount = actionCount + 1;
	int visibleIndex;

	/* Short title leaves room for the row counter on small screens. */
	snprintf(title, sizeof(title), "P%d - %s",
	         s_nativeControlsPlayer + 1,
	         NativeLocalization_Translate("REMAP"));

	MainFreeze_NativeControlsClampScroll();

	snprintf(pageText, sizeof(pageText), "%d/%d",
	         s_nativeControlsRow + 1, rowCount);
	DecalFont_DrawLine(pageText, 432 + offsetX, 34,
	                   FONT_SMALL, JUSTIFY_RIGHT | ORANGE);

	for (visibleIndex = 0;
	     visibleIndex < NATIVE_CONTROLS_REMAP_VISIBLE_ROWS;
	     visibleIndex++)
	{
		const int row = s_nativeControlsScroll + visibleIndex;
		const int y = 54 + visibleIndex * 14;

		if (row >= rowCount)
			break;

		if (row == actionCount)
		{
			DecalFont_DrawLine("BACK", 112 + offsetX, y,
			                   FONT_SMALL, ORANGE);
			continue;
		}

		{
			const int action = MainFreeze_NativeControlsRemapRowAction(row);

			if (MainFreeze_NativeControlsRemapRowIsPowerSlide(row))
			{
				char leftBinding[48];
				char rightBinding[48];

				DecalFont_DrawLine(
					"POWER SLIDE / HOP",
					78 + offsetX, y, FONT_SMALL, ORANGE);

				const int leftCenter = 330;
				const int rightCenter = 410;
				const int leftSelected =
					(s_nativeControlsRow == row) &&
					(s_nativeControlsPowerSlideSide == 0);
				const int rightSelected =
					(s_nativeControlsRow == row) &&
					(s_nativeControlsPowerSlideSide != 0);
				char leftDisplay[56];
				char rightDisplay[56];

				MainFreeze_NativeControlsFormatRemapBinding(
					PLATFORM_INPUT_ACTION_L1,
					leftBinding,
					sizeof(leftBinding));
				MainFreeze_NativeControlsFormatRemapBinding(
					PLATFORM_INPUT_ACTION_R1,
					rightBinding,
					sizeof(rightBinding));

				DecalFont_DrawLine(
					"|",
					370 + offsetX, y,
					FONT_SMALL,
					JUSTIFY_CENTER | ORANGE);

				if (s_nativeControlsCaptureAction ==
				    PLATFORM_INPUT_ACTION_L1)
				{
					MainFreeze_NativeControlsDrawStateSlotAt(
						offsetX, leftCenter - 15, y, 1);
				}
				else if (strcmp(leftBinding, "--") == 0)
				{
					MainFreeze_NativeControlsDrawStateSlotAt(
						offsetX, leftCenter - 15, y, 0);
				}
				else
				{
					snprintf(
						leftDisplay,
						sizeof(leftDisplay),
						leftSelected ? "[%s]" : "%s",
						leftBinding);
					DecalFont_DrawLine(
						leftDisplay,
						leftCenter + offsetX, y,
						FONT_SMALL,
						JUSTIFY_CENTER | ORANGE);
				}

				if (s_nativeControlsCaptureAction ==
				    PLATFORM_INPUT_ACTION_R1)
				{
					MainFreeze_NativeControlsDrawStateSlotAt(
						offsetX, rightCenter - 15, y, 1);
				}
				else if (strcmp(rightBinding, "--") == 0)
				{
					MainFreeze_NativeControlsDrawStateSlotAt(
						offsetX, rightCenter - 15, y, 0);
				}
				else
				{
					snprintf(
						rightDisplay,
						sizeof(rightDisplay),
						rightSelected ? "[%s]" : "%s",
						rightBinding);
					DecalFont_DrawLine(
						rightDisplay,
						rightCenter + offsetX, y,
						FONT_SMALL,
						JUSTIFY_CENTER | ORANGE);
				}
			}
			else
			{
				char actionLabel[64];
				snprintf(actionLabel, sizeof(actionLabel), "%s",
				         NativeLocalization_Translate(
				             MainFreeze_NativeControlsRemapLabel(row, action)));
				MainFreeze_NativeControlsFitText(actionLabel, 198);
				DecalFont_DrawLine(
					actionLabel,
					112 + offsetX, y, FONT_SMALL, ORANGE);

				if (s_nativeControlsCaptureAction == action)
				{
					MainFreeze_NativeControlsDrawEmptySlot(
						offsetX, y, 1);
				}
				else
				{
					MainFreeze_NativeControlsFormatRemapBinding(
						action,
						binding,
						sizeof(binding));
					MainFreeze_NativeControlsFitText(binding, 92);

					if (strcmp(binding, "--") == 0)
					{
						MainFreeze_NativeControlsDrawEmptySlot(
							offsetX, y, 0);
					}
					else
					{
						int drewGlyph = 0;

						if (Platform_InputGetPlayerDeviceKind(
								s_nativeControlsPlayer) ==
						    PLATFORM_INPUT_DEVICE_GAMEPAD)
						{
							const int rawBinding =
								Platform_InputGetGamepadBindingForPlayer(
									s_nativeControlsPlayer,
									action);

							drewGlyph = Platform_InputGlyphsDrawBinding(
								s_nativeControlsPlayer,
								rawBinding,
								405 + offsetX, y - 2, 24, 11,
								&sdata->gGT->backBuffer->primMem,
								sdata->gGT->backBuffer->otMem.uiOT);
						}

						if (!drewGlyph)
						{
							DecalFont_DrawLine(
								binding,
								432 + offsetX, y,
								FONT_SMALL,
								JUSTIFY_RIGHT | ORANGE);
						}
					}
				}
			}
		}
	}

	{
		const int cursorVisible =
			s_nativeControlsRow - s_nativeControlsScroll;
		MainFreeze_NativeControlsDrawCursor(
			offsetX, 51 + cursorVisible * 14, 12);
	}

	MainFreeze_NativeControlsDrawFrame(title, offsetX, 190);
}

static void MainFreeze_NativeOptionsDrawControls(
	int offsetX,
	const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	(void)gamepad;

	switch (s_nativeControlsView)
	{
	case NATIVE_CONTROLS_VIEW_PLAYER:
		MainFreeze_NativeControlsDrawPlayer(offsetX);
		break;

	case NATIVE_CONTROLS_VIEW_REMAP:
		MainFreeze_NativeControlsDrawRemap(offsetX);
		break;

	default:
		MainFreeze_NativeControlsDrawPlayers(offsetX);
		break;
	}
}

/* Music UI reuses the main Options renderer and per-player input.
 * Source EXISTING preserves existing imported CSEQ/legacy OGG by default;
 * ORIGINAL bypasses both; the two PACK modes resolve indexed community
 * songs in the existing CSEQ / OGG playback backends. */
static int MainFreeze_MusicMin(int a,int b) { return a<b?a:b; }

static void MainFreeze_MusicDrawFit(const char *raw,int x,int y,
                                    int width,u32 flags)
{
    char buf[180];
    size_t length;
    if(!raw) raw="";
    snprintf(buf,sizeof(buf),"%s",raw);
    length=strlen(buf);
    while(length>0 &&
          DecalFont_GetLineWidth(buf,FONT_SMALL)>width)
        buf[--length]=0;
    DecalFont_DrawLine(buf,x,y,FONT_SMALL,flags);
}

static int MainFreeze_MusicPackKind(void)
{
    int source=NativeMusicSelection_GetDefaultSource();
    return source==NATIVE_MUSIC_SOURCE_CSEQ ?
           NATIVE_MUSIC_SOURCE_CSEQ : NATIVE_MUSIC_SOURCE_OGG;
}

static const char *MainFreeze_MusicPackLabel(void)
{
    int kind=MainFreeze_MusicPackKind();
    int count;
    if (NativeMusicSelection_GetDefaultSource() <
        NATIVE_MUSIC_SOURCE_OGG) return "SELECT PACK MODE";
    count=NativeMusicSelection_GetPackCount(kind);
    if(count<=0) return "NONE INSTALLED";
    return NativeMusicSelection_GetPackName(
        kind,NativeMusicSelection_GetDefaultPackIndex(kind));
}

/* Clear, destination-first music navigation. Retail tracks use their
 * real LEV slot. Boss music has its own synthetic cue, independent of
 * the map it is raced on. Custom tracks use ID-based override slots. */
enum
{
    NATIVE_MUSIC_CAT_RACES=0,
    NATIVE_MUSIC_CAT_BATTLES,
    NATIVE_MUSIC_CAT_HUBS,
    NATIVE_MUSIC_CAT_MASKS,
    NATIVE_MUSIC_CAT_BOSSES,
    NATIVE_MUSIC_CAT_MENU,
    NATIVE_MUSIC_CAT_CUTSCENES,
    NATIVE_MUSIC_CAT_CUSTOM,
    NATIVE_MUSIC_CAT_BACK
};
static const char *s_nativeMusicCategories[]={
    "RACE TRACKS",
    "BATTLE ARENAS",
    "ADVENTURE HUBS",
    "AKU / UKA MASK MUSIC",
    "BOSS RACES",
    "MENU & CREDITS",
    "CUTSCENES",
    "CUSTOM TRACKS",
    "BACK"
};
static const int s_nativeMusicMenuSlots[]={MAIN_MENU_LEVEL,CREDITS_CRASH};
static const int s_nativeMusicCutsceneSlots[]={
    INTRO_RACE_TODAY,OXIDE_ENDING,OXIDE_TRUE_ENDING
};
static int MainFreeze_MusicTargetCount(void)
{
    switch(s_nativeMusicCategory)
    {
    case NATIVE_MUSIC_CAT_RACES:return 18;
    case NATIVE_MUSIC_CAT_BATTLES:return 7;
    case NATIVE_MUSIC_CAT_HUBS:return 6;
    case NATIVE_MUSIC_CAT_MASKS:return 2;
    case NATIVE_MUSIC_CAT_BOSSES:return 1;
    case NATIVE_MUSIC_CAT_MENU:
        return sizeof(s_nativeMusicMenuSlots)/sizeof(s_nativeMusicMenuSlots[0]);
    case NATIVE_MUSIC_CAT_CUTSCENES:
        return sizeof(s_nativeMusicCutsceneSlots)/sizeof(s_nativeMusicCutsceneSlots[0]);
    case NATIVE_MUSIC_CAT_CUSTOM:
        return NativeCustomTracks_Count();
    default:return 0;
    }
}
static int MainFreeze_MusicTargetAt(int pos)
{
    switch(s_nativeMusicCategory)
    {
    case NATIVE_MUSIC_CAT_RACES:return pos;
    case NATIVE_MUSIC_CAT_BATTLES:return NITRO_COURT+pos;
    case NATIVE_MUSIC_CAT_HUBS:
        if(pos<5)return GEM_STONE_VALLEY+pos;
        return ADVENTURE_GARAGE;
    case NATIVE_MUSIC_CAT_MASKS:
        return pos ? NATIVE_MUSIC_MASK_UKA_SLOT : NATIVE_MUSIC_MASK_AKU_SLOT;
    case NATIVE_MUSIC_CAT_BOSSES:return NATIVE_MUSIC_BOSS_SLOT;
    case NATIVE_MUSIC_CAT_MENU:return s_nativeMusicMenuSlots[pos];
    case NATIVE_MUSIC_CAT_CUTSCENES:return s_nativeMusicCutsceneSlots[pos];
    case NATIVE_MUSIC_CAT_CUSTOM:return NATIVE_MUSIC_CUSTOM_BASE+pos;
    default:return -1;
    }
}
static const char *MainFreeze_MusicTargetName(int slot)
{
    if(slot==NATIVE_MUSIC_MASK_AKU_SLOT)return "AKU AKU (MASK)";
    if(slot==NATIVE_MUSIC_MASK_UKA_SLOT)return "UKA UKA (MASK)";
    if(slot==NATIVE_MUSIC_BOSS_SLOT)return "BOSS RACES";
    /* The private Hangout soundtrack is not listed in public music choices. */
    if(slot==MAIN_MENU_LEVEL)return "MAIN MENU";
    if(slot==CREDITS_CRASH)return "END CREDITS";
    if(slot==INTRO_RACE_TODAY)return "RACE INTRO";
    /* Studio-logo intro is no longer available in public soundtrack menus. */
    if(slot==OXIDE_ENDING)return "OXIDE ENDING";
    if(slot==OXIDE_TRUE_ENDING)return "TRUE ENDING";
    if(slot==ADVENTURE_GARAGE)return "CHARACTER SELECT";
    if(slot>=NATIVE_MUSIC_CUSTOM_BASE)
    {
        const char *name=NativeCustomTracks_Name(slot-NATIVE_MUSIC_CUSTOM_BASE);
        return name ? name : "CUSTOM TRACK";
    }
    if(slot>=0 && slot<INTRO_RACE_TODAY)
    {
        return sdata->lngStrings[data.metaDataLEV[slot].name_LNG];
    }
    return "UNKNOWN";
}
static int MainFreeze_MusicVisibleLevels(void)
{
    int left=MainFreeze_MusicTargetCount() -
        s_nativeMusicLevelPage*NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    return MainFreeze_MusicMin(left>0?left:0,
                              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE);
}
static int MainFreeze_MusicVisibleChoices(void)
{
    /* Root of music browser: 3 legacy choices + one row per PACK.
     * The individual songs are on a second screen for the chosen pack. */
    int left=3+NativeMusicSelection_GetAvailablePackCount(
                 s_nativeMusicSelectedLevel) -
        s_nativeMusicSongPage*NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    return MainFreeze_MusicMin(left>0?left:0,
                               NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE);
}
static int MainFreeze_MusicVisiblePackSongs(void)
{
    int left=NativeMusicSelection_GetAvailablePackSongCount(
                  s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack) -
        s_nativeMusicPackSongPage*NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    return MainFreeze_MusicMin(left>0?left:0,
                               NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE);
}
static void MainFreeze_NativeOptionsDrawMusic(int offsetX)
{
    static const char *names[]={
        "DEFAULT MUSIC","MUSIC PACK","CHOOSE MUSIC",
        "RELOAD PACKS","BACK"
    };
    int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC];
    const int kind=NativeMusicSelection_GetDefaultSource();
    for(int i=0;i<NATIVE_OPTIONS_MUSIC_ROOT_ROWS;i++)
    {
        int y=62+i*23;
        DecalFont_DrawLine(names[i],78+offsetX,y,FONT_SMALL,ORANGE);
        if(i==0)
            MainFreeze_MusicDrawFit(
                NativeMusicSelection_SourceName(kind),
                432+offsetX,y,177,JUSTIFY_RIGHT|WHITE);
        if(i==1)
            MainFreeze_MusicDrawFit(MainFreeze_MusicPackLabel(),
                432+offsetX,y,190,JUSTIFY_RIGHT|WHITE);
        if(i==2)
            DecalFont_DrawLine("BY LOCATION >",432+offsetX,y,
                               FONT_SMALL,JUSTIFY_RIGHT|WHITE);
        if(i==3)
            DecalFont_DrawLine("SCAN FILES",432+offsetX,y,
                               FONT_SMALL,JUSTIFY_RIGHT|WHITE);
        if(i==4)
            MainFreeze_NativeOptionsDrawActionBinding(
                0,PLATFORM_INPUT_ACTION_TRIANGLE,offsetX,y);
    }
    MainFreeze_NativeOptionsDrawCursor(offsetX,59+row*23,18);
    MainFreeze_NativeOptionsDrawFrame("MUSIC",offsetX,181);
}
static void MainFreeze_NativeOptionsDrawMusicLevels(int offsetX)
{
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_LEVELS];
    for(int i=0;i<NATIVE_OPTIONS_MUSIC_CATEGORY_ROWS;i++)
    {
        int y=51+i*17;
        MainFreeze_MusicDrawFit(s_nativeMusicCategories[i],
            88+offsetX,y,325,i==NATIVE_MUSIC_CAT_CUSTOM &&
            NativeCustomTracks_Count()==0 ? GRAY : ORANGE);
        if(i!=NATIVE_MUSIC_CAT_BACK)
            DecalFont_DrawLine(">",425+offsetX,y,
                               FONT_SMALL,JUSTIFY_RIGHT|WHITE);
    }
    MainFreeze_NativeOptionsDrawCursor(offsetX,48+row*17,14);
    MainFreeze_NativeOptionsDrawFrame("WHERE TO PLAY?",offsetX,194);
}
static void MainFreeze_NativeOptionsDrawMusicTargets(int offsetX)
{
    const int shown=MainFreeze_MusicVisibleLevels();
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_TARGETS];
    char note[128];
    if(shown==0)
        MainFreeze_MusicDrawFit("NO TRACK MODS INSTALLED",
                               78+offsetX,83,344,WHITE);
    for(int i=0;i<shown;i++)
    {
        int slot=MainFreeze_MusicTargetAt(s_nativeMusicLevelPage*
                    NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+i);
        int y=59+i*21;
        MainFreeze_MusicDrawFit(MainFreeze_MusicTargetName(slot),
                                78+offsetX,y,185,ORANGE);
        MainFreeze_MusicDrawFit(
            NativeMusicSelection_GetLevelSelectionName(slot),
            430+offsetX,y,174,JUSTIFY_RIGHT|WHITE);
    }
    DecalFont_DrawLine("BACK",78+offsetX,59+shown*21,
                       FONT_SMALL,ORANGE);
    snprintf(note,sizeof(note),"%s %d/%d  LEFT / RIGHT",
             NativeLocalization_Translate("PAGE"),
             s_nativeMusicLevelPage+1,
             (MainFreeze_MusicTargetCount()+
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE > 0 ?
             (MainFreeze_MusicTargetCount()+
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE : 1);
    DecalFont_DrawLine(note,256+offsetX,203,
                       FONT_SMALL,JUSTIFY_CENTER|WHITE);
    MainFreeze_NativeOptionsDrawCursor(offsetX,56+row*21,16);
    MainFreeze_NativeOptionsDrawFrame(
        (char *)s_nativeMusicCategories[s_nativeMusicCategory],
        offsetX,197);
}
static void MainFreeze_NativeOptionsDrawMusicSongs(int offsetX)
{
    const int shown=MainFreeze_MusicVisibleChoices();
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_SONGS];
    const int current=NativeMusicSelection_GetLevelChoice(
                          s_nativeMusicSelectedLevel);
    const int selectedPack=NativeMusicSelection_GetAvailablePackPositionForChoice(
                               s_nativeMusicSelectedLevel,current);
    const char *targetName=MainFreeze_MusicTargetName(
        s_nativeMusicSelectedLevel);
    char caption[100];
    for(int i=0;i<shown;i++)
    {
        int pos=s_nativeMusicSongPage*
                NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+i;
        int y=59+i*21;
        if(pos<3)
        {
            const char *label=NativeMusicSelection_GetLevelChoiceName(pos);
            if(s_nativeMusicSelectedLevel>=NATIVE_MUSIC_CUSTOM_BASE)
            {
                if(pos==1)label="TRACK DEFAULT";
                else if(pos==2)label="TRACK IMPORTS";
            }
            MainFreeze_MusicDrawFit(label,78+offsetX,y,340,
                                     current==pos ? ORANGE : WHITE);
        }
        else
        {
            const int pack=pos-3;
            const int count=NativeMusicSelection_GetAvailablePackSongCount(
                                  s_nativeMusicSelectedLevel,pack);
            const char *name=NativeMusicSelection_GetAvailablePackName(
                                  s_nativeMusicSelectedLevel,pack);
            char label[190],tail[30];
            snprintf(label,sizeof(label),"%s >",name);
            snprintf(tail,sizeof(tail),"%d SONG%s",count,count==1?"":"S");
            MainFreeze_MusicDrawFit(label,78+offsetX,y,275,
                                     selectedPack==pack ? ORANGE : WHITE);
            DecalFont_DrawLine(tail,432+offsetX,y,FONT_SMALL,
                               JUSTIFY_RIGHT|WHITE);
        }
    }
    DecalFont_DrawLine("BACK",78+offsetX,59+shown*21,FONT_SMALL,ORANGE);
    snprintf(caption,sizeof(caption),"%s %d/%d",
             NativeLocalization_Translate("PACKS"),
             s_nativeMusicSongPage+1,
             (3+NativeMusicSelection_GetAvailablePackCount(
                  s_nativeMusicSelectedLevel)+
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE);
    DecalFont_DrawLine(caption,78+offsetX,203,FONT_SMALL,WHITE);
    MainFreeze_MusicDrawFit(targetName,432+offsetX,203,230,
                             JUSTIFY_RIGHT|ORANGE);
    MainFreeze_NativeOptionsDrawCursor(offsetX,56+row*21,16);
    MainFreeze_NativeOptionsDrawFrame("SELECT MUSIC PACK",offsetX,197);
}
static void MainFreeze_NativeOptionsDrawMusicPackSongs(int offsetX)
{
    const int shown=MainFreeze_MusicVisiblePackSongs();
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS];
    const int current=NativeMusicSelection_GetLevelChoice(
                          s_nativeMusicSelectedLevel);
    const char *packName=NativeMusicSelection_GetAvailablePackName(
                             s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack);
    char caption[100];
    for(int i=0;i<shown;i++)
    {
        int pos=s_nativeMusicPackSongPage*
                NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+i;
        int choice=NativeMusicSelection_GetAvailablePackSongChoice(
            s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack,pos);
        const char *songName=NativeMusicSelection_GetAvailablePackSongName(choice);
        MainFreeze_MusicDrawFit(songName,78+offsetX,59+i*21,342,
                                 choice==current ? ORANGE : WHITE);
    }
    DecalFont_DrawLine("BACK",78+offsetX,59+shown*21,FONT_SMALL,ORANGE);
    snprintf(caption,sizeof(caption),"%s %d/%d",
             NativeLocalization_Translate("SONGS"),
             s_nativeMusicPackSongPage+1,
             (NativeMusicSelection_GetAvailablePackSongCount(
                  s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack)+
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
              NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE);
    DecalFont_DrawLine(caption,78+offsetX,203,FONT_SMALL,WHITE);
    MainFreeze_MusicDrawFit(packName,432+offsetX,203,240,
                             JUSTIFY_RIGHT|ORANGE);
    MainFreeze_NativeOptionsDrawCursor(offsetX,56+row*21,16);
    MainFreeze_NativeOptionsDrawFrame("PACK SONGS",offsetX,197);
}

/* Translations warning is ALWAYS at the top, including initial setup.
 * Render via the same DecalFont pipeline as the rest of the PC port.
 * Strings are plain ASCII on this US font atlas; accented Latin glyphs
 * remain a future atlas extension, never emit corrupt UTF-8 bytes. */
static void MainFreeze_NativeOptionsDrawLanguage(int offsetX)
{
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_LANGUAGE];
    const int chosen=Platform_GetGameLanguage();
    DecalFont_DrawLine("LANGUAGE",256+offsetX,32,
                       FONT_SMALL,JUSTIFY_CENTER|ORANGE);
    DecalFont_DrawLine("TRANSLATIONS MAY HAVE ERRORS",256+offsetX,56,
                       FONT_SMALL,JUSTIFY_CENTER|WHITE);
    DecalFont_DrawLine("PLEASE REPORT ANY MISTAKES",256+offsetX,69,
                       FONT_SMALL,JUSTIFY_CENTER|ORANGE);
    for(int i=0;i<6;i++)
    {
        int y=91+i*16;
        char *name=(char *)NativeLocalization_GetLanguageName(i);
        DecalFont_DrawLine(name,256+offsetX,y,
                          FONT_SMALL,JUSTIFY_CENTER|
                          (Platform_HasGameLanguage() && chosen==i ? ORANGE : WHITE));
    }
    if(!s_nativeLanguageFirstRun)
        DecalFont_DrawLine("BACK",256+offsetX,190,
                           FONT_SMALL,JUSTIFY_CENTER|ORANGE);
    MainFreeze_NativeOptionsDrawCursor(offsetX,88+row*16,15);
    MainFreeze_NativeOptionsDrawFrame("LANGUAGE",offsetX,210);
}

static void MainFreeze_NativeOptionsDrawPage(int page, int offsetX, const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	switch (page)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		MainFreeze_NativeOptionsDrawAudio(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC:
		MainFreeze_NativeOptionsDrawMusic(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_LEVELS:
		MainFreeze_NativeOptionsDrawMusicLevels(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_TARGETS:
		MainFreeze_NativeOptionsDrawMusicTargets(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_SONGS:
		MainFreeze_NativeOptionsDrawMusicSongs(offsetX);
		break;
    case NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS:
        MainFreeze_NativeOptionsDrawMusicPackSongs(offsetX);
        break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		MainFreeze_NativeOptionsDrawGraphics(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS:
		MainFreeze_NativeOptionsDrawAdvancedGraphics(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_GAMEPLAY:
		MainFreeze_NativeOptionsDrawGameplay(offsetX);
		break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		MainFreeze_NativeOptionsDrawControls(offsetX, gamepad);
		break;
    case NATIVE_OPTIONS_PAGE_LANGUAGE:
        MainFreeze_NativeOptionsDrawLanguage(offsetX);
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

static void MainFreeze_NativeOptionsInputLanguage(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    const int rows=s_nativeLanguageFirstRun ? 6 : 7;
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_LANGUAGE];
    if(tap&BTN_UP) {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_LANGUAGE,rows,-1);
        return;
    }
    if(tap&BTN_DOWN) {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_LANGUAGE,rows,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one)))return;
    if(row>=0 && row<6)
    {
        /* Persist immediately. Switching language in-game refreshes every
         * translated text draw without restarting music, game or mods. */
        Platform_SetGameLanguage(row);
        OtherFX_Play(1,1);
        if(s_nativeLanguageFirstRun)
        {
            s_nativeLanguageFirstRun=0;
            MainFreeze_NativeOptionsBeginExit();
        }
        else MainFreeze_NativeOptionsBeginTransition(
            NATIVE_OPTIONS_PAGE_ROOT,-1);
    }
    else if(!s_nativeLanguageFirstRun)
    {
        OtherFX_Play(1,1);
        MainFreeze_NativeOptionsBeginTransition(
            NATIVE_OPTIONS_PAGE_ROOT,-1);
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

	if ((row == 4) &&
        (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one)))
    {
        OtherFX_Play(1, 1);
        Platform_SetHearOtherRacers(!Platform_GetHearOtherRacers());
        return;
    }

    if ((row == 5) && (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one)))
    {
		/* Audio and track metadata are indexed before HOWL starts.
		 * Avoid blocking the live CSEQ sequencer on menu navigation. */
		if (!NativeCustomTracks_IsActiveRace())
			NativeCustomTracks_EnsureScanned();
		NativeMusicSelection_EnsureReady();
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_MUSIC, 1);
	}
	if ((row == 6) && (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one)))
	{
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT, -1);
	}
}

/* Music choices are saved for *all* locations, but only the current
 * audible location should be restarted. Use precisely the same slot routing
 * as CseqMusic_Start, including custom tracks, bosses and Hangout.
 * A mask cue is a temporary song; handle it without killing its backing
 * racing soundtrack. */
static int MainFreeze_MusicLiveSlot(void)
{
    struct GameTracker *gGT;
    if(sdata==NULL || (gGT=sdata->gGT)==NULL)
        return -1;
    if(NativeHangout_IsActive())
        return NATIVE_MUSIC_HANGOUT_SLOT;
    if(NativeCustomTracks_IsActiveRace() &&
       NativeCustomTracks_SelectedIndex()>=0)
        return NATIVE_MUSIC_CUSTOM_BASE+
               NativeCustomTracks_SelectedIndex();
    if(IS_BOSS_RACE(gGT->gameMode1))
        return NATIVE_MUSIC_BOSS_SLOT;
    return gGT->levelID;
}

static void MainFreeze_MusicLiveApplyChoice(int requestedSlot)
{
    struct GameTracker *gGT;
    struct NativeMusicSelectionResolved resolved;
    const struct CseqHeader *retail;
    struct SongSet *hubSet=NULL;
    int hubBits=0;
    int activeSlot;
    int source;
    int maskID;
    int activeMask;
    int restartClock;
    int wasPaused;
    int started;

    if(sdata==NULL || (gGT=sdata->gGT)==NULL ||
       sdata->boolAudioEnabled==0 ||
       sdata->Loading.stage!=LOAD_IDLE ||
       (gGT->gameMode1&END_OF_RACE)!=0)
        return;

    activeSlot=MainFreeze_MusicLiveSlot();
    if(activeSlot<0)
        return;

    activeMask=(sdata->cseqBoolPlay!=0 &&
                (sdata->cseqHighestIndex==CSEQ_SONG_AKU ||
                 sdata->cseqHighestIndex==CSEQ_SONG_UKA)) ?
                 sdata->cseqHighestIndex : 0;
    maskID=(requestedSlot==NATIVE_MUSIC_MASK_AKU_SLOT) ?
            CSEQ_SONG_AKU :
            (requestedSlot==NATIVE_MUSIC_MASK_UKA_SLOT) ?
            CSEQ_SONG_UKA : 0;

    /* Changing a specific inactive track must NOT interrupt the music
       that's already playing somewhere else. */
    if(requestedSlot>=0 && requestedSlot!=activeSlot &&
       (!maskID || maskID!=activeMask))
        return;

    if(activeMask)
    {
        if(requestedSlot>=0 && maskID!=activeMask)
        {
            Platform_Log("[MUSIC LIVE] saved other slot=%d during mask; no interrupt\n",
                         requestedSlot);
            return;
        }
        /* Restore the current cue without touching song-0's OGG/CSEQ
           timeline. Its normal end-of-mask transition will pick up the
           newly selected background song later. */
        restartClock=NativeMenuMusicClock_IsRunning();
        if(restartClock) NativeMenuMusicClock_Stop();
        CseqMusic_Stop(activeMask);
        started=CseqMusic_Start(activeMask,0,NULL,0,1);
        if(restartClock) NativeMenuMusicClock_Start();
        Platform_Log("[MUSIC LIVE] restarted mask song=%d result=%d\n",
                     activeMask,started);
        return;
    }

    /* Never start race audio over the title/garage transition or over a
       cutscene's XA. The selected choice is still saved for its next load. */
    if((gGT->gameMode1 & GAME_CUTSCENE)!=0 ||
       ((gGT->gameMode1 & MAIN_MENU)!=0 &&
        gGT->levelID!=MAIN_MENU_LEVEL &&
        gGT->levelID!=ADVENTURE_GARAGE))
        return;

    /* A freshly selected song can start even if the previous location's
       source was intentionally silent. Use the already-loaded retail HOWL
       CSEQ as the reference; a custom external CSEQ points elsewhere. */
    retail=(const struct CseqHeader *)(const void *)sdata->sampleBlock1;
    if(retail->songSize<(int)sizeof(*retail) ||
       retail->songSize>0x40000 ||
       retail->numSongs==0 || retail->numSongs>128)
    {
        Platform_LogWarn("[MUSIC LIVE] retail CSEQ bank not ready; saved for next load\n");
        return;
    }

    /* Menu HOWL ticks run on a separate 60-Hz thread. Waiting for that
       thread to stop is required before freeing/replacing a CSEQ pack.
       Never hold SDL audio output lock while changing these resources. */
    restartClock=NativeMenuMusicClock_IsRunning();
    if(restartClock) NativeMenuMusicClock_Stop();
    wasPaused=(sdata->boolSoundPaused!=0) ||
        (((gGT->gameMode1&PAUSE_ALL)!=0) &&
         ((gGT->gameMode1&MAIN_MENU)==0));

    CseqMusic_StopAll();
    Music_End();
#if defined(CTR_CSEQ_LIBERATION)
    NativeMusicRegistry_Shutdown();
#endif
    /* Restores the retail *bank* when transitioning away from an external
       CSEQ, so 'ORIGINAL CTR' does not follow dangling pointers. It also
       restores the source for OGG mode, whose CSEQ pool is silent. */
    howl_ParseCseqHeader((struct CseqHeader *)(void *)sdata->sampleBlock1);

    if((gGT->gameMode1 & ADVENTURE_ARENA)!=0 &&
       gGT->levelID>=GEM_STONE_VALLEY &&
       gGT->levelID<GEM_STONE_VALLEY+5)
    {
        hubSet=&sdata->advHubSongSet;
        hubBits=1<<(gGT->levelID-GEM_STONE_VALLEY);
    }

    source=NativeMusicSelection_Resolve(activeSlot,&resolved);
    started=CseqMusic_Start(0,0,hubSet,hubBits,1);
    if(started)
    {
        sdata->cseqBoolPlay=1;
        sdata->cseqHighestIndex=0;
        sdata->cseqTempo=0;
        if(wasPaused) CseqMusic_Pause();
    }
    else
        Music_End();

    if(restartClock) NativeMenuMusicClock_Start();
    Platform_Log("[MUSIC LIVE] slot=%d requested=%d source=%d song=%s started=%d (clock=%d)\n",
                 activeSlot,requestedSlot,source,
                 resolved.path[0]?resolved.path:"RETAIL/EXISTING",
                 started,restartClock);
}

static void MainFreeze_NativeOptionsInputMusic(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC];
    if(tap & BTN_UP)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC,
             NATIVE_OPTIONS_MUSIC_ROOT_ROWS,-1);
        return;
    }
    if(tap & BTN_DOWN)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC,
             NATIVE_OPTIONS_MUSIC_ROOT_ROWS,1);
        return;
    }

    if(row==0 && (tap&(BTN_LEFT|BTN_RIGHT|BTN_CIRCLE|BTN_CROSS_one)))
    {
        int kind=NativeMusicSelection_GetDefaultSource();
        int step=(tap&BTN_LEFT)?-1:1;
        NativeMusicSelection_SetDefaultSource((kind+step+4)%4);
        MainFreeze_MusicLiveApplyChoice(-1);
        OtherFX_Play(1,1);
        return;
    }
    if(row==1 && (tap&(BTN_LEFT|BTN_RIGHT|BTN_CIRCLE|BTN_CROSS_one)))
    {
        if (NativeMusicSelection_GetDefaultSource() <
            NATIVE_MUSIC_SOURCE_OGG) return;
        int kind=MainFreeze_MusicPackKind();
        NativeMusicSelection_CycleDefaultPack(
             kind,(tap&BTN_LEFT)?-1:1);
        MainFreeze_MusicLiveApplyChoice(-1);
        OtherFX_Play(1,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one))) return;

    if(row==2)
    {
        s_nativeMusicCategory=0;
        s_nativeMusicLevelPage=0;
        s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_LEVELS]=0;
        MainFreeze_NativeOptionsBeginTransition(
             NATIVE_OPTIONS_PAGE_MUSIC_LEVELS,1);
    }
    else if(row==3)
    {
        if(!NativeCustomTracks_IsActiveRace())
            NativeCustomTracks_Scan();
        NativeMusicSelection_Reload();
        MainFreeze_MusicLiveApplyChoice(-1);
        OtherFX_Play(1,1);
    }
    else if(row==4)
        MainFreeze_NativeOptionsBeginTransition(
             NATIVE_OPTIONS_PAGE_AUDIO,-1);
}

static void MainFreeze_NativeOptionsInputMusicLevels(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_LEVELS];
    if(tap&BTN_UP)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_LEVELS,
                                       NATIVE_OPTIONS_MUSIC_CATEGORY_ROWS,-1);
        return;
    }
    if(tap&BTN_DOWN)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_LEVELS,
                                       NATIVE_OPTIONS_MUSIC_CATEGORY_ROWS,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one)))return;
    if(row==NATIVE_MUSIC_CAT_BACK)
    {
        MainFreeze_NativeOptionsBeginTransition(
            NATIVE_OPTIONS_PAGE_MUSIC,-1);
        return;
    }
    s_nativeMusicCategory=row;
    if(MainFreeze_MusicTargetCount()==0)
    {
        OtherFX_Play(5,1);
        return;
    }
    s_nativeMusicLevelPage=0;
    s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_TARGETS]=0;
    MainFreeze_NativeOptionsBeginTransition(
        NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,1);
}
static void MainFreeze_NativeOptionsInputMusicTargets(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    const int visible=MainFreeze_MusicVisibleLevels();
    const int pageCount=(MainFreeze_MusicTargetCount()+
        NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
        NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_TARGETS];

    if(tap&(BTN_LEFT|BTN_RIGHT))
    {
        int next=s_nativeMusicLevelPage+((tap&BTN_LEFT)?-1:1);
        if(next>=0 && next<pageCount)
        {
            s_nativeMusicLevelPage=next;
            if(row>MainFreeze_MusicVisibleLevels())
                s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_TARGETS]=0;
            OtherFX_Play(0,1);
        }
        return;
    }
    if(tap&BTN_UP)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,
                                       visible+1,-1);
        return;
    }
    if(tap&BTN_DOWN)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,
                                       visible+1,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one)))return;
    if(row>=visible)
    {
        MainFreeze_NativeOptionsBeginTransition(
            NATIVE_OPTIONS_PAGE_MUSIC_LEVELS,-1);
        return;
    }
    s_nativeMusicSelectedLevel=MainFreeze_MusicTargetAt(
        s_nativeMusicLevelPage*NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+row);
    {
        int selected=NativeMusicSelection_GetLevelChoice(
                         s_nativeMusicSelectedLevel);
        int pack=NativeMusicSelection_GetAvailablePackPositionForChoice(
                         s_nativeMusicSelectedLevel,selected);
        int position=selected<3 ? selected : (pack>=0 ? 3+pack : 0);
        s_nativeMusicSongPage=position/NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
        s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_SONGS]=
            position%NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    }
    MainFreeze_NativeOptionsBeginTransition(
        NATIVE_OPTIONS_PAGE_MUSIC_SONGS,1);
}
static void MainFreeze_NativeOptionsInputMusicSongs(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    const int pageCount=(3+NativeMusicSelection_GetAvailablePackCount(
                             s_nativeMusicSelectedLevel)+
        NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
        NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    const int visible=MainFreeze_MusicVisibleChoices();
    int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_SONGS];

    if(tap&(BTN_LEFT|BTN_RIGHT))
    {
        int next=s_nativeMusicSongPage+((tap&BTN_LEFT)?-1:1);
        if(next>=0 && next<pageCount)
        {
            s_nativeMusicSongPage=next;
            if(row>MainFreeze_MusicVisibleChoices())
                s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_SONGS]=0;
            OtherFX_Play(0,1);
        }
        return;
    }
    if(tap&BTN_UP)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_SONGS,
                                       visible+1,-1);
        return;
    }
    if(tap&BTN_DOWN)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_SONGS,
                                       visible+1,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one)))return;
    if(row<visible)
    {
        int pos=s_nativeMusicSongPage*NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+row;
        if(pos<3)
        {
            NativeMusicSelection_SetLevelChoice(s_nativeMusicSelectedLevel,pos);
            MainFreeze_MusicLiveApplyChoice(s_nativeMusicSelectedLevel);
            OtherFX_Play(1,1);
            MainFreeze_NativeOptionsBeginTransition(
                NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,-1);
            return;
        }
        /* A soundtrack pack is a group, not a song. Drill into its
         * actual CSEQ/OGG song list without setting music prematurely. */
        s_nativeMusicSelectedPack=pos-3;
        int current=NativeMusicSelection_GetLevelChoice(
                        s_nativeMusicSelectedLevel);
        int index=NativeMusicSelection_GetAvailablePackSongPosition(
                      s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack,
                      current);
        s_nativeMusicPackSongPage=index/NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
        s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS]=
            index%NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
        OtherFX_Play(1,1);
        MainFreeze_NativeOptionsBeginTransition(
            NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS,1);
        return;
    }
    MainFreeze_NativeOptionsBeginTransition(
        NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,-1);
}
static void MainFreeze_NativeOptionsInputMusicPackSongs(void)
{
    const u32 tap=sdata->AnyPlayerTap;
    const int count=NativeMusicSelection_GetAvailablePackSongCount(
                        s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack);
    const int pageCount=(count+NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE-1)/
                         NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE;
    const int visible=MainFreeze_MusicVisiblePackSongs();
    const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS];
    if(tap&(BTN_LEFT|BTN_RIGHT))
    {
        int next=s_nativeMusicPackSongPage+((tap&BTN_LEFT)?-1:1);
        if(next>=0 && next<pageCount)
        {
            s_nativeMusicPackSongPage=next;
            if(row>MainFreeze_MusicVisiblePackSongs())
                s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS]=0;
            OtherFX_Play(0,1);
        }
        return;
    }
    if(tap&BTN_UP)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS,
                                        visible+1,-1);
        return;
    }
    if(tap&BTN_DOWN)
    {
        MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS,
                                        visible+1,1);
        return;
    }
    if(!(tap&(BTN_CIRCLE|BTN_CROSS_one)))return;
    if(row<visible)
    {
        int position=s_nativeMusicPackSongPage*
                        NATIVE_OPTIONS_MUSIC_LIST_PAGE_SIZE+row;
        int choice=NativeMusicSelection_GetAvailablePackSongChoice(
            s_nativeMusicSelectedLevel,s_nativeMusicSelectedPack,position);
        if(choice>=3)
        {
            NativeMusicSelection_SetLevelChoice(s_nativeMusicSelectedLevel,choice);
            MainFreeze_MusicLiveApplyChoice(s_nativeMusicSelectedLevel);
            OtherFX_Play(1,1);
            MainFreeze_NativeOptionsBeginTransition(
                NATIVE_OPTIONS_PAGE_MUSIC_TARGETS,-1);
            return;
        }
    }
    MainFreeze_NativeOptionsBeginTransition(
        NATIVE_OPTIONS_PAGE_MUSIC_SONGS,-1);
}

static void MainFreeze_NativeOptionsCycleResolution(int direction)
{
	const int count = Platform_GetResolutionPresetCount();
	MainFreeze_NativeGraphicsEnsureDraft();
	int current = s_nativeGraphicsDraft.resolutionIndex;

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

	s_nativeGraphicsDraft.resolutionIndex=current;
}

static void MainFreeze_NativeOptionsInputGraphics(void)
{
	MainFreeze_NativeGraphicsEnsureDraft();
	NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
	const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GRAPHICS];
	const u32 tap=sdata->AnyPlayerTap;
	const u32 adjust=tap&(BTN_LEFT|BTN_RIGHT|BTN_CIRCLE|BTN_CROSS_one);
	const int direction=(tap&BTN_LEFT)?-1:1;

	if(tap&BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_GRAPHICS,NATIVE_OPTIONS_GRAPHICS_ROW_COUNT,-1);
		return;
	}
	if(tap&BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_GRAPHICS,NATIVE_OPTIONS_GRAPHICS_ROW_COUNT,1);
		return;
	}
	if(row==7 && (tap&(BTN_CIRCLE|BTN_CROSS_one)))
	{
		OtherFX_Play(1,1);
		MainFreeze_NativeOptionsBeginTransition(NATIVE_OPTIONS_PAGE_ROOT,-1);
		return;
	}
	if(row==6 && (tap&(BTN_CIRCLE|BTN_CROSS_one)))
	{
		OtherFX_Play(1,1);
		MainFreeze_NativeGraphicsApply();
		return;
	}
	if(row==5 && (tap&(BTN_CIRCLE|BTN_CROSS_one)))
	{
		OtherFX_Play(1,1);
		MainFreeze_NativeOptionsBeginTransition(
			NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS,1);
		return;
	}
	if(!adjust)return;
	OtherFX_Play(1,1);
	switch(row)
	{
	case 0:
		MainFreeze_NativeOptionsCycleResolution(direction);
		break;
	case 1:
		p->fullscreen=!p->fullscreen;
		break;
	case 2:
		p->aspectMode=(p->aspectMode+PLATFORM_ASPECT_RATIO_COUNT+
			direction)%PLATFORM_ASPECT_RATIO_COUNT;
		break;
	case 3:
		p->fpsLimit=(p->fpsLimit+PLATFORM_FPS_LIMIT_COUNT+
			direction)%PLATFORM_FPS_LIMIT_COUNT;
		break;
	case 4:
		p->characterDetail=(p->characterDetail+4+direction)%4;
		break;
	}
}

static void MainFreeze_NativeOptionsInputAdvancedGraphics(void)
{
	MainFreeze_NativeGraphicsEnsureDraft();
	NativeGraphicsDraft *p=&s_nativeGraphicsDraft;
	const int row=s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS];
	const u32 tap=sdata->AnyPlayerTap;
	const u32 adjust=tap&(BTN_LEFT|BTN_RIGHT|BTN_CIRCLE|BTN_CROSS_one);
	if(tap&BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS,
			NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT,-1);
		return;
	}
	if(tap&BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS,
			NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT,1);
		return;
	}
	if(row==13 && (tap&(BTN_CIRCLE|BTN_CROSS_one)))
	{
		OtherFX_Play(1,1);
		MainFreeze_NativeOptionsBeginTransition(
			NATIVE_OPTIONS_PAGE_GRAPHICS,-1);
		return;
	}
	if(row==12 && (tap&(BTN_CIRCLE|BTN_CROSS_one)))
	{
		OtherFX_Play(1,1);
		MainFreeze_NativeGraphicsApply();
		return;
	}
	if(!adjust)return;
	switch(row)
	{
	case 1:p->subpixel=!p->subpixel;break;
	case 2:p->textureFilter=!p->textureFilter;break;
	case 6:p->dithering=!p->dithering;break;
	case 8:p->perfOverlay=!p->perfOverlay;break;
	case 9:p->gpuTiming=!p->gpuTiming;break;
	case 10:p->compactVRAM=!p->compactVRAM;break;
	case 11:p->zbuffer=!p->zbuffer;break;
	default:
		OtherFX_Play(0,1);
		return;
	}
	OtherFX_Play(1,1);
}

static void MainFreeze_NativeOptionsInputGameplay(void)
{
	const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_GAMEPLAY];
	const int hasHeight = MainFreeze_NativeOptionsGameplayHasHeight();
	const int backRow = hasHeight ? 4 : 3;
	const u32 adjust =
		sdata->AnyPlayerTap &
		(BTN_LEFT | BTN_RIGHT | BTN_CIRCLE | BTN_CROSS_one);

	if (sdata->AnyPlayerTap & BTN_UP)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_GAMEPLAY,
			MainFreeze_NativeOptionsGameplayRowCount(),
			-1);
		return;
	}

	if (sdata->AnyPlayerTap & BTN_DOWN)
	{
		MainFreeze_NativeOptionsMoveRow(
			NATIVE_OPTIONS_PAGE_GAMEPLAY,
			MainFreeze_NativeOptionsGameplayRowCount(),
			1);
		return;
	}

	if (row == backRow)
	{
		if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
		{
			OtherFX_Play(1, 1);
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_ROOT, -1);
		}
		return;
	}

	if (!adjust)
		return;

	OtherFX_Play(1, 1);

	if (row == 0)
	{
		NativeHUD_SetSaffiEnabled(
			!NativeHUD_GetSaffiEnabled());
		return;
	}

	if (row == 1)
	{
		NativeHUD_SetReservesBarEnabled(
			!NativeHUD_GetReservesBarEnabled());
		return;
	}

	if (row == 2)
	{
		int mode = NativeHUD_GetDriftMeterMode();

		if (sdata->AnyPlayerTap & BTN_LEFT)
		{
			mode--;
			if (mode < NATIVE_HUD_DRIFT_OFF)
				mode = NATIVE_HUD_DRIFT_CNK_3D;
		}
		else
		{
			mode++;
			if (mode > NATIVE_HUD_DRIFT_CNK_3D)
				mode = NATIVE_HUD_DRIFT_OFF;
		}

		NativeHUD_SetDriftMeterMode(mode);
		return;
	}
	if (hasHeight && row == 3)
	{
		NativeHUD_SetCNKMeterHeight(
			NativeHUD_GetCNKMeterHeight() == NATIVE_HUD_CNK_HEIGHT_HIGH
			? NATIVE_HUD_CNK_HEIGHT_LOW : NATIVE_HUD_CNK_HEIGHT_HIGH);
	}
}

static void MainFreeze_NativeControlsMoveRow(int direction)
{
	const int rowCount = MainFreeze_NativeOptionsControlsRowCount();

	if (rowCount <= 0)
		return;

	s_nativeControlsRow += direction;
	if (s_nativeControlsRow < 0)
		s_nativeControlsRow = rowCount - 1;
	if (s_nativeControlsRow >= rowCount)
		s_nativeControlsRow = 0;

	if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP)
		MainFreeze_NativeControlsClampScroll();

	MainFreeze_TitleOptionsStopVoicePreview();
	OptionsMenu_TestSound(0, 0);
	OtherFX_Play(0, 1);
}

static void MainFreeze_NativeControlsCycleDevice(
	int playerIndex,
	int direction)
{
	enum { DEVICE_NONE = -2 };
	int candidates[PLATFORM_INPUT_PAD_COUNT + 2];
	int candidateCount = 0;
	int current;
	int currentPos = -1;
	int deviceCount;
	int i;

	candidates[candidateCount++] = DEVICE_NONE;
	candidates[candidateCount++] = 0; /* keyboard */

	deviceCount = Platform_InputGetDeviceCount();
	for (i = 1;
	     i < deviceCount &&
	     candidateCount < (int)(sizeof(candidates) / sizeof(candidates[0]));
	     i++)
	{
		candidates[candidateCount++] = i;
	}

	if (candidateCount <= 0)
		return;

	switch (Platform_InputGetPlayerDeviceKind(playerIndex))
	{
	case PLATFORM_INPUT_DEVICE_NONE:
		current = DEVICE_NONE;
		break;
	case PLATFORM_INPUT_DEVICE_KEYBOARD:
		current = 0;
		break;
	default:
		current = Platform_InputGetPlayerDeviceIndex(playerIndex);
		break;
	}

	for (i = 0; i < candidateCount; i++)
	{
		if (candidates[i] == current)
		{
			currentPos = i;
			break;
		}
	}

	if (currentPos < 0)
		currentPos = direction > 0 ? -1 : 0;

	currentPos += direction;
	if (currentPos < 0)
		currentPos = candidateCount - 1;
	if (currentPos >= candidateCount)
		currentPos = 0;

	if (candidates[currentPos] == DEVICE_NONE)
	{
		Platform_InputSetPlayerDeviceKind(
			playerIndex, PLATFORM_INPUT_DEVICE_NONE);
	}
	else
	{
		Platform_InputAssignDeviceToPlayer(
			candidates[currentPos], playerIndex);
	}

	OtherFX_Play(1, 1);
}

static void MainFreeze_NativeControlsCycleGlyph(
	int playerIndex,
	int direction)
{
	static const int keyboardStyles[] = {
		PLATFORM_INPUT_GLYPH_AUTO,
		PLATFORM_INPUT_GLYPH_KEYBOARD,
		PLATFORM_INPUT_GLYPH_PLAYSTATION,
		PLATFORM_INPUT_GLYPH_XBOX,
		PLATFORM_INPUT_GLYPH_NINTENDO,
		PLATFORM_INPUT_GLYPH_GENERIC,
	};
	static const int gamepadStyles[] = {
		PLATFORM_INPUT_GLYPH_AUTO,
		PLATFORM_INPUT_GLYPH_PLAYSTATION,
		PLATFORM_INPUT_GLYPH_XBOX,
		PLATFORM_INPUT_GLYPH_NINTENDO,
		PLATFORM_INPUT_GLYPH_GENERIC,
	};
	const int kind = Platform_InputGetPlayerDeviceKind(playerIndex);
	const int *styles =
		kind == PLATFORM_INPUT_DEVICE_KEYBOARD ?
		keyboardStyles : gamepadStyles;
	const int styleCount =
		kind == PLATFORM_INPUT_DEVICE_KEYBOARD ?
		(int)(sizeof(keyboardStyles) / sizeof(keyboardStyles[0])) :
		(int)(sizeof(gamepadStyles) / sizeof(gamepadStyles[0]));
	const int current = Platform_InputGetPlayerGlyphStyle(playerIndex);
	int position = 0;

	for (int i = 0; i < styleCount; i++)
	{
		if (styles[i] == current)
		{
			position = i;
			break;
		}
	}

	position += direction;
	if (position < 0)
		position = styleCount - 1;
	if (position >= styleCount)
		position = 0;

	Platform_InputSetPlayerGlyphStyle(playerIndex, styles[position]);
	OtherFX_Play(1, 1);
}

static int MainFreeze_NativeControlsBeginCapture(int action)
{
	const int deviceIndex =
		Platform_InputGetPlayerDeviceIndex(s_nativeControlsPlayer);

	if (deviceIndex < 0 ||
	    !Platform_InputBeginRebindCapture(deviceIndex))
	{
		OtherFX_Play(0, 1);
		return 0;
	}

	s_nativeControlsCaptureAction = action;
	s_nativeControlsCaptureDeviceIndex = deviceIndex;
	RECTMENU_ClearInput();
	OtherFX_Play(1, 1);
	return 1;
}

static int MainFreeze_NativeControlsPollCapture(void)
{
	int binding = -1;
	const int result = Platform_InputPollRebindCapture(&binding);

	if (result == 0)
		return 1;

	if (result > 0 && s_nativeControlsCaptureAction >= 0)
	{
		const int kind =
			Platform_InputGetPlayerDeviceKind(s_nativeControlsPlayer);

		if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
		{
			Platform_InputSetKeyboardBindingForPlayer(
				s_nativeControlsPlayer,
				s_nativeControlsCaptureAction,
				binding);
		}
		else if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
		{
			Platform_InputSetGamepadBindingForPlayer(
				s_nativeControlsPlayer,
				s_nativeControlsCaptureAction,
				binding);
		}

		OtherFX_Play(1, 1);
	}

	Platform_InputCancelRebindCapture();
	s_nativeControlsCaptureAction = -1;
	s_nativeControlsCaptureDeviceIndex = -1;
	/* A freshly rebound Accelerate/Brake key can immediately become a menu
	   Cross/Square input on the next host frame while the key is still held.
	   Hold menu ownership until every mapped control returns to neutral. */
	s_nativeControlsWaitForNeutral = 1;
	RECTMENU_ClearInput();
	return 1;
}

static void MainFreeze_NativeControlsClearCurrentBinding(void)
{
	const int actionCount = MainFreeze_NativeControlsRemapActionCount();
	int action;
	int kind;

	if (s_nativeControlsView != NATIVE_CONTROLS_VIEW_REMAP ||
	    s_nativeControlsRow < 0 ||
	    s_nativeControlsRow >= actionCount)
	{
		return;
	}

	action = MainFreeze_NativeControlsRemapRowAction(
		s_nativeControlsRow);
	kind = Platform_InputGetPlayerDeviceKind(s_nativeControlsPlayer);

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		Platform_InputSetKeyboardBindingForPlayer(
			s_nativeControlsPlayer, action, -1);
	}
	else if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
	{
		Platform_InputSetGamepadBindingForPlayer(
			s_nativeControlsPlayer, action, -1);
	}
	else
	{
		return;
	}

	OtherFX_Play(1, 1);
}

static void MainFreeze_NativeOptionsInputControls(
	const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 confirm = BTN_CIRCLE | BTN_CROSS_one;
	int rowCount;

	(void)gamepad;

	if (s_nativeControlsNameEditing)
	{
		RECTMENU_ClearInput();
		return;
	}

	if (s_nativeControlsCaptureAction >= 0)
	{
		MainFreeze_NativeControlsPollCapture();
		return;
	}

	if (s_nativeControlsWaitForNeutral)
	{
		if (sdata->AnyPlayerHold == 0)
			s_nativeControlsWaitForNeutral = 0;
		RECTMENU_ClearInput();
		return;
	}

	rowCount = MainFreeze_NativeOptionsControlsRowCount();
	if (rowCount <= 0)
		return;

	if (s_nativeControlsRow >= rowCount)
		s_nativeControlsRow = rowCount - 1;

	if (sdata->AnyPlayerTap & BTN_UP)
	{
		MainFreeze_NativeControlsMoveRow(-1);
		return;
	}

	if (sdata->AnyPlayerTap & BTN_DOWN)
	{
		MainFreeze_NativeControlsMoveRow(1);
		return;
	}

	if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP)
	{
		const int actionCount = MainFreeze_NativeControlsRemapActionCount();

		if (MainFreeze_NativeControlsRemapRowIsPowerSlide(
		        s_nativeControlsRow) &&
		    (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT)))
		{
			s_nativeControlsPowerSlideSide =
				(sdata->AnyPlayerTap & BTN_RIGHT) ? 1 : 0;
			OtherFX_Play(1, 1);
			return;
		}

		if ((sdata->AnyPlayerTap & BTN_SELECT) &&
		    s_nativeControlsRow < actionCount)
		{
			MainFreeze_NativeControlsClearCurrentBinding();
			return;
		}

		if ((sdata->AnyPlayerTap & confirm) == 0)
			return;

		if (s_nativeControlsRow == actionCount)
		{
			s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYER;
			s_nativeControlsRow = 4;
			s_nativeControlsScroll = 0;
			OtherFX_Play(1, 1);
			return;
		}

		MainFreeze_NativeControlsBeginCapture(
			MainFreeze_NativeControlsRemapRowAction(
				s_nativeControlsRow));
		return;
	}

	if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_PLAYER)
	{
		const int kind =
			Platform_InputGetPlayerDeviceKind(s_nativeControlsPlayer);

		if (s_nativeControlsRow == 0 &&
		    (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | confirm)))
		{
			MainFreeze_NativeControlsCycleDevice(
				s_nativeControlsPlayer,
				(sdata->AnyPlayerTap & BTN_LEFT) ? -1 : 1);
			return;
		}

		if (s_nativeControlsRow == 2 &&
		    (sdata->AnyPlayerTap & (BTN_LEFT | BTN_RIGHT | confirm)))
		{
			MainFreeze_NativeControlsCycleGlyph(
				s_nativeControlsPlayer,
				(sdata->AnyPlayerTap & BTN_LEFT) ? -1 : 1);
			return;
		}

		if ((sdata->AnyPlayerTap & confirm) == 0)
			return;

		switch (s_nativeControlsRow)
		{
		case 1:
			MainFreeze_NativeControlsBeginNameEdit();
			break;

		case 3:
			if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
			{
				gGT->gameMode1 ^=
					data.gGT_gameMode1_VibPerPlayer[
						s_nativeControlsPlayer];
				Platform_SaveVibrationMask(
					gGT->gameMode1 & GAME_MODE_VIBRATION_MASK);
				{
					const int enabled =
						(gGT->gameMode1 & data.gGT_gameMode1_VibPerPlayer[
							s_nativeControlsPlayer]) == 0;
					if (!Platform_InputPreviewVibration(s_nativeControlsPlayer, enabled) &&
					    enabled)
					{
						Platform_LogWarn("[CONTROLS] Controller P%d has no available rumble\n",
						                 s_nativeControlsPlayer + 1);
					}
				}
				OtherFX_Play(1, 1);
			}
			break;

		case 4:
			if (kind != PLATFORM_INPUT_DEVICE_NONE)
			{
				s_nativeControlsView = NATIVE_CONTROLS_VIEW_REMAP;
				s_nativeControlsRow = 0;
				s_nativeControlsScroll = 0;
				s_nativeControlsPowerSlideSide = 0;
				OtherFX_Play(1, 1);
			}
			break;

		case 5:
			if (kind != PLATFORM_INPUT_DEVICE_NONE)
			{
				Platform_InputResetBindingsForPlayer(
					s_nativeControlsPlayer);
				OtherFX_Play(1, 1);
			}
			break;

		case 6:
			s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYERS;
			s_nativeControlsRow = s_nativeControlsPlayer;
			OtherFX_Play(1, 1);
			break;
		}

		return;
	}

	/* Player list. */
	if ((sdata->AnyPlayerTap & confirm) == 0)
		return;

	if (s_nativeControlsRow >= PLATFORM_INPUT_PAD_COUNT)
	{
		OtherFX_Play(1, 1);
		MainFreeze_NativeOptionsBeginTransition(
			NATIVE_OPTIONS_PAGE_ROOT, -1);
		return;
	}

	s_nativeControlsPlayer = s_nativeControlsRow;
	s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYER;
	s_nativeControlsRow = 0;
	s_nativeControlsScroll = 0;
	OtherFX_Play(1, 1);
}

static void MainFreeze_NativeOptionsApplyTouch(const GAMEPAD_MainFreeze_MenuPtrOptions *gamepad)
{
	int mouseX;
	int mouseY;
	int advancedMouseMoved = 1;
	int rowCount;
	int rowStartY;
	int rowStep;
	int rowHeight;
	int rowX = 74;
	int rowWidth = 364;
	int row = -1;

	if (!RECTMENU_NativeTouchGet(&mouseX, &mouseY))
		return;

	/* A stationary mouse must never steal the selection back after a
	   keyboard, controller or mouse-wheel scroll. It can still click. */
	if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS)
	{
		advancedMouseMoved = (mouseX != s_nativeAdvancedMouseX) ||
		                     (mouseY != s_nativeAdvancedMouseY);
		s_nativeAdvancedMouseX = mouseX;
		s_nativeAdvancedMouseY = mouseY;
	}

	/* In the remap list, the only exits are the explicit BACK row below
	   and the visible top-left BACK button when clicked with the mouse. */
	if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS &&
	    s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP)
	{
		RECT topBackRect = {.x = 8, .y = 4, .w = 88, .h = 28};

		if ((mouseX >= topBackRect.x) &&
		    (mouseX < topBackRect.x + topBackRect.w) &&
		    (mouseY >= topBackRect.y) &&
		    (mouseY < topBackRect.y + topBackRect.h) &&
		    RECTMENU_NativeTouchConsumeRect(&topBackRect))
		{
			s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYER;
			s_nativeControlsRow = 4;
			s_nativeControlsScroll = 0;
			RECTMENU_ClearInput();
			OtherFX_Play(1, 1);
			return;
		}
	}

	switch (s_nativeOptionsPage)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		rowCount = NATIVE_OPTIONS_AUDIO_ROW_COUNT;
		rowStartY = 59;
		rowStep = 18;
		rowHeight = 14;
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC:
		rowCount=NATIVE_OPTIONS_MUSIC_ROOT_ROWS;
		rowStartY=59;
		rowStep=23;
		rowHeight=18;
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_LEVELS:
		rowCount=NATIVE_OPTIONS_MUSIC_CATEGORY_ROWS;
		rowStartY=48;
		rowStep=17;
		rowHeight=14;
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_TARGETS:
		rowCount=MainFreeze_MusicVisibleLevels()+1;
		rowStartY=56;
		rowStep=21;
		rowHeight=16;
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_SONGS:
		rowCount=MainFreeze_MusicVisibleChoices()+1;
		rowStartY=56;
		rowStep=21;
		rowHeight=16;
		break;
    case NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS:
        rowCount=MainFreeze_MusicVisiblePackSongs()+1;
        rowStartY=56;
        rowStep=21;
        rowHeight=16;
        break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		rowCount = NATIVE_OPTIONS_GRAPHICS_ROW_COUNT;
		rowStartY = 51;
		rowStep = 18;
		rowHeight = 14;
		break;
	case NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS:
		MainFreeze_NativeAdvancedClampScroll();
		rowCount=NATIVE_OPTIONS_ADVANCED_GRAPHICS_ROW_COUNT-
			s_nativeAdvancedGraphicsScroll;
		if(rowCount>NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS)
			rowCount=NATIVE_ADVANCED_GRAPHICS_VISIBLE_ROWS;
		rowStartY=NATIVE_ADVANCED_GRAPHICS_ROW_TOP_Y;
		rowStep=NATIVE_ADVANCED_GRAPHICS_ROW_STEP;
		rowHeight=NATIVE_ADVANCED_GRAPHICS_ROW_HEIGHT;
		break;
	case NATIVE_OPTIONS_PAGE_GAMEPLAY:
		rowCount = MainFreeze_NativeOptionsGameplayRowCount();
		rowStartY = 59;
		rowStep = 22;
		rowHeight = 16;
		break;
    case NATIVE_OPTIONS_PAGE_LANGUAGE:
        rowCount=s_nativeLanguageFirstRun ? 6 : 7;
        rowStartY=88;
        rowStep=16;
        rowHeight=15;
        break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		rowCount = MainFreeze_NativeOptionsControlsRowCount();
		rowX = 108;
		rowWidth = 332;
		if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_PLAYERS)
		{
			rowStartY = 54;
			rowStep = 22;
			rowHeight = 15;
		}
		else if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_PLAYER)
		{
			rowStartY = 49;
			rowStep = 18;
			rowHeight = 15;
		}
		else
		{
			int remaining = rowCount - s_nativeControlsScroll;
			if (remaining > NATIVE_CONTROLS_REMAP_VISIBLE_ROWS)
				remaining = NATIVE_CONTROLS_REMAP_VISIBLE_ROWS;
			rowCount = remaining;
			rowStartY = 51;
			rowStep = 14;
			rowHeight = 12;
		}
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
			.x = (s16)rowX,
			.y = (s16)(rowStartY + i * rowStep),
			.w = (s16)rowWidth,
			.h = (s16)rowHeight,
		};

		if ((mouseX >= rowRect.x) &&
		    (mouseX < rowRect.x + rowRect.w) &&
		    (mouseY >= rowRect.y) &&
		    (mouseY < rowRect.y + rowRect.h))
		{
			row = i;
			if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS)
			{
				s_nativeControlsRow =
					(s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP) ?
					(s_nativeControlsScroll + i) : i;
			}
			else if(s_nativeOptionsPage==
			        NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS)
			{
				/* Hover selects only after mouse movement. Stationary
				   hover must not override D-pad or wheel navigation. */
				if (advancedMouseMoved)
					s_nativeOptionsRows[s_nativeOptionsPage]=
						(s16)(s_nativeAdvancedGraphicsScroll+i);
			}
			else
			{
				s_nativeOptionsRows[s_nativeOptionsPage] = (s16)i;
			}
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
			.x = (s16)rowX,
			.y = (s16)(rowStartY + row * rowStep),
			.w = (s16)rowWidth,
			.h = (s16)rowHeight,
		};

		if (RECTMENU_NativeTouchConsumeRect(&rowRect))
		{
			/* A click must work even when the pointer hasn't moved. */
			if (s_nativeOptionsPage ==
			    NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS)
				s_nativeOptionsRows[s_nativeOptionsPage] =
					(s16)(s_nativeAdvancedGraphicsScroll + row);
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

	/* While waiting for a raw key/button, the rebind capture owns input.
	   This must happen before START/BACK handling so those buttons can
	   themselves be assigned without closing Options. */
	if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS &&
	    s_nativeControlsCaptureAction >= 0)
	{
		MainFreeze_NativeOptionsInputControls(gamepad);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

	if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS &&
	    s_nativeControlsWaitForNeutral)
	{
		MainFreeze_NativeOptionsInputControls(gamepad);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

	/* Remap is intentionally modal: controller/keyboard shortcuts must not
	   leave it. START, Triangle and Square are ignored here. The BACK row
	   and the mouse-only top-left BACK hit target are the only exits. */
	if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS &&
	    s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP &&
	    (sdata->AnyPlayerTap & (BTN_START | BTN_TRIANGLE | BTN_SQUARE_one)))
	{
		RECTMENU_ClearInput();
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

    /* A first-run install must choose a language before leaving.
     * Never let START/triangle accidentally close the selector. */
    if (s_nativeLanguageFirstRun &&
        s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_LANGUAGE &&
        (sdata->AnyPlayerTap & (BTN_START|BTN_TRIANGLE|BTN_SQUARE_one)))
    {
        RECTMENU_ClearInput();
        MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage,0,gamepad);
        return false;
    }

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

		if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_CONTROLS)
		{
			if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_REMAP)
			{
				s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYER;
				s_nativeControlsRow = 4;
				s_nativeControlsScroll = 0;
				OtherFX_Play(1, 1);
				MainFreeze_NativeOptionsDrawPage(
					s_nativeOptionsPage, 0, gamepad);
				return false;
			}

			if (s_nativeControlsView == NATIVE_CONTROLS_VIEW_PLAYER)
			{
				s_nativeControlsView = NATIVE_CONTROLS_VIEW_PLAYERS;
				s_nativeControlsRow = s_nativeControlsPlayer;
				OtherFX_Play(1, 1);
				MainFreeze_NativeOptionsDrawPage(
					s_nativeOptionsPage, 0, gamepad);
				return false;
			}
		}

		OtherFX_Play(1, 1);
        if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_LANGUAGE)
            MainFreeze_NativeOptionsBeginTransition(
                NATIVE_OPTIONS_PAGE_ROOT,-1);
		else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS)
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_GRAPHICS, -1);
        else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS)
            MainFreeze_NativeOptionsBeginTransition(
                NATIVE_OPTIONS_PAGE_MUSIC_SONGS, -1);
		else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_MUSIC_SONGS)
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_MUSIC_TARGETS, -1);
		else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_MUSIC_TARGETS)
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_MUSIC_LEVELS, -1);
		else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_MUSIC_LEVELS)
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_MUSIC, -1);
		else if (s_nativeOptionsPage == NATIVE_OPTIONS_PAGE_MUSIC)
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_AUDIO, -1);
		else
			MainFreeze_NativeOptionsBeginTransition(
				NATIVE_OPTIONS_PAGE_ROOT, -1);
		MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
		return false;
	}

	switch (s_nativeOptionsPage)
	{
	case NATIVE_OPTIONS_PAGE_AUDIO:
		MainFreeze_NativeOptionsInputAudio();
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC:
		MainFreeze_NativeOptionsInputMusic();
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_LEVELS:
		MainFreeze_NativeOptionsInputMusicLevels();
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_TARGETS:
		MainFreeze_NativeOptionsInputMusicTargets();
		break;
	case NATIVE_OPTIONS_PAGE_MUSIC_SONGS:
		MainFreeze_NativeOptionsInputMusicSongs();
		break;
    case NATIVE_OPTIONS_PAGE_MUSIC_PACK_SONGS:
        MainFreeze_NativeOptionsInputMusicPackSongs();
        break;
	case NATIVE_OPTIONS_PAGE_GRAPHICS:
		MainFreeze_NativeOptionsInputGraphics();
		break;
	case NATIVE_OPTIONS_PAGE_ADVANCED_GRAPHICS:
		MainFreeze_NativeOptionsInputAdvancedGraphics();
		break;
	case NATIVE_OPTIONS_PAGE_GAMEPLAY:
		MainFreeze_NativeOptionsInputGameplay();
		break;
	case NATIVE_OPTIONS_PAGE_CONTROLS:
		MainFreeze_NativeOptionsInputControls(gamepad);
		break;
    case NATIVE_OPTIONS_PAGE_LANGUAGE:
        MainFreeze_NativeOptionsInputLanguage();
        break;
	default:
		if (sdata->AnyPlayerTap & BTN_UP)
			MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_ROOT, NATIVE_OPTIONS_ROOT_ROW_COUNT, -1);
		else if (sdata->AnyPlayerTap & BTN_DOWN)
			MainFreeze_NativeOptionsMoveRow(NATIVE_OPTIONS_PAGE_ROOT, NATIVE_OPTIONS_ROOT_ROW_COUNT, 1);
		else if (sdata->AnyPlayerTap & (BTN_CIRCLE | BTN_CROSS_one))
		{
			const int row = s_nativeOptionsRows[NATIVE_OPTIONS_PAGE_ROOT];
			if (row == 5)
			{
				OtherFX_Play(1, 1);
				MainFreeze_NativeOptionsBeginExit();
				MainFreeze_NativeOptionsDrawPage(s_nativeOptionsPage, 0, gamepad);
				return false;
			}

			OtherFX_Play(1, 1);
            MainFreeze_NativeOptionsBeginTransition(
                row==4 ? NATIVE_OPTIONS_PAGE_LANGUAGE : row+1,1);
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
					{
						const int player = gamepad->gamepadId[gamepadRow];
						const int enabled =
							(gGT->gameMode1 & data.gGT_gameMode1_VibPerPlayer[player]) == 0;
						Platform_InputPreviewVibration(player, enabled);
					}
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
				int mode = Platform_GetFPSLimitMode();
				if (sdata->AnyPlayerTap & BTN_LEFT)
					mode = (mode + PLATFORM_FPS_LIMIT_COUNT - 1) % PLATFORM_FPS_LIMIT_COUNT;
				else
					mode = (mode + 1) % PLATFORM_FPS_LIMIT_COUNT;
				OtherFX_Play(1, 1);
				Platform_SetFPSLimitMode(mode);
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
	DecalFont_DrawLine((char *)Platform_GetAspectRatioLabel(Platform_GetAspectRatioMode()), 436, nativeOptionsY, FONT_SMALL, JUSTIFY_RIGHT | WHITE);

	DecalFont_DrawLine("FPS LIMIT", 76, nativeOptionsY + 10, FONT_SMALL, ORANGE);
	{
		char refreshText[24];
		MainFreeze_NativeFormatFPSLimit(refreshText, (int)sizeof(refreshText));
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
		if (s_nativeControlsNameEditing)
			Platform_StopTextInput();
		s_nativeControlsNameEditing = 0;
		s_nativeControlsNamePlayer = -1;
		s_nativeControlsNameLen = 0;
		s_nativeControlsNameBuffer[0] = 0;
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
#if defined(CTR_NATIVE)
			if (NativeHangout_IsActive())
			{
				/* Restore the temporary Adventure snapshot before leaving the
				 * social hub. The normal quit path below performs the actual load. */
				NativeHangout_SetActive(0);
			}
#endif
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
#if defined(CTR_NATIVE)
	if ((gGT->cooldownfromPauseUntilUnpause != 0) && !NativeHangout_IsMenuOpen())
#else
	if (gGT->cooldownfromPauseUntilUnpause != 0)
#endif
	{
		return;
	}

	// assume 5 frames have passed since paused

	if (menu->funcState != RECTMENU_FUNC_STATE_INPUT)
	{
		menu->drawStyle &= ~RECTMENU_DRAW_STYLE_3P4P_LAYOUT;

#if defined(CTR_NATIVE)
		/* The retail Adventure pause renderer draws hub name, story progress
		 * and hint-related decoration. Hangout is a live social overlay, so
		 * none of that belongs behind its three-row menu. */
		if (NativeHangout_IsActive() &&
		    menu == &s_hangoutPauseMenu)
		{
			return;
		}
#endif

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

#if defined(CTR_NATIVE)
	if (NativeHangout_IsActive() &&
	    menu == &s_hangoutPauseMenu &&
	    stringID == NATIVE_HANGOUT_MENU_DISCONNECT_STRING)
	{
		RECTMENU_Hide(menu);
		NativeHangout_SetMenuOpen(0);
		NativeHangout_DisconnectPlayer();
		gGT->cooldownFromUnpauseUntilPause = 5;
		RECTMENU_ClearInput();
		OtherFX_Play(1, 1);
		return;
	}

	if (NativeHangout_IsActive() &&
	    menu == &s_hangoutPauseMenu &&
	    stringID == NATIVE_HANGOUT_MENU_CONNECT_STRING)
	{
		RECTMENU_Hide(menu);
		NativeHangout_SetMenuOpen(0);
		NativeHangout_ConnectPlayer();
		gGT->cooldownFromUnpauseUntilPause = 5;
		RECTMENU_ClearInput();
		OtherFX_Play(1, 1);
		return;
	}

	if (NativeHangout_IsActive() &&
	    menu == &s_hangoutPauseMenu &&
	    stringID == NATIVE_HANGOUT_MENU_CUSTOMIZATION_STRING)
	{
		NativeHangout_RequestCustomizationTransition();
		return;
	}
#endif

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
#if defined(CTR_NATIVE)
		if (NativeHangout_IsMenuOpen())
		{
			/* Hangout uses the retail pause menu as a live overlay. Gameplay and
			 * audio were never paused and ElimBG never captured a screenshot. */
			NativeHangout_SetMenuOpen(0);
			OtherFX_Play(1, 1);
			return;
		}
#endif

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

#if defined(CTR_NATIVE)
	if (NativeHangout_IsActive())
	{
		return &s_hangoutPauseMenu;
	}
#endif

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

	menu = MainFreeze_GetMenuPtr();
	menu->rowSelected = 0;

#if defined(CTR_NATIVE)
	if (NativeHangout_IsActive())
	{
		/* Hangout pause is a live menu overlay: keep world/audio ticking and
		 * never enter the retail ElimBG screenshot/frozen-game path. Gameplay
		 * controls are suppressed separately while this menu hierarchy is open. */
		NativeHangout_SetMenuOpen(1);
		RECTMENU_Show(menu);
		OtherFX_Play(1, 1);
		return;
	}
#endif

	gGT->gameMode1 = gameMode1 | PAUSE_1;
	RECTMENU_Show(menu);
	MainFrame_TogglePauseAudio(1);
	OtherFX_Play(1, 1);
	ElimBG_Activate(gGT);
}
