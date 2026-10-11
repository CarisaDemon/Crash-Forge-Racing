#include <common.h>
#include <NativeHUD.h>

#if defined(CTR_NATIVE)

#include <platform/native_assets.h>
#include <platform/native_renderer.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_HUD_SETTINGS_FILE "ctr_hud.cfg"
#define NATIVE_HUD_RESERVES_W 49
#define NATIVE_HUD_RESERVES_H 4
#define NATIVE_HUD_RESERVES_NORMAL_MAX 3840
#define NATIVE_HUD_RESERVES_COUNTER_GAP 5
#define NATIVE_HUD_CNK_SIDE_OFFSET 52
#define NATIVE_HUD_CNK_SIDE_OFFSET_SPLIT 36
#define NATIVE_HUD_CNK_GAUGE_ATLAS_W 252
#define NATIVE_HUD_CNK_GAUGE_ATLAS_H 64
#define NATIVE_HUD_CNK_GAUGE_CELL_W 28
#define NATIVE_HUD_CNK_GAUGE_CELL_H 32
#define NATIVE_HUD_CNK_GAUGE_COLUMNS 9
#define NATIVE_HUD_CNK_GAUGE_FRAMES 18

/* Gasmoxian/Saffi speedometer geometry. The original online HUD builds the
 * gauge from primitives rather than a texture. Keep those exact 4:3 offsets
 * here and apply Gasmoxian's WIDE_34 3/4 X correction in widescreen. */
#define NATIVE_HUD_SAFFI_POINT_COUNT 22
#define NATIVE_HUD_SAFFI_SCALE_ONE 256
/* Keep Saffi in the retail speedometer corner, slightly farther left
 * and noticeably lower so it clears the drift/jump HUD cluster. */
#define NATIVE_HUD_SAFFI_CENTER_RIGHT 72
#define NATIVE_HUD_SAFFI_CENTER_BOTTOM 28
#define NATIVE_HUD_SAFFI_SPEED_MAX 100
#define NATIVE_HUD_SAFFI_NEEDLE_HEIGHT 60
#define NATIVE_HUD_SAFFI_NEEDLE_WIDTH 6

static const s16 s_nativeHUDSaffiSpeedometerData[NATIVE_HUD_SAFFI_POINT_COUNT][2] = {
	{-65, 16}, {-54, 13},
	{-70, 0}, {-58, 0},
	{-65, -16}, {-54, -13},
	{-50, -29}, {-42, -24},
	{-25, -39}, {-21, -32},
	{0, -42}, {0, -35},
	{25, -39}, {21, -32},
	{50, -29}, {42, -24},
	{65, -16}, {54, -13},
	{70, 0}, {58, 0},
	{65, 16}, {54, 13},
};

struct NativeHUDSettings
{
	int loaded;
	int saffiHUD;
	int reservesBar;
	int driftMeter;
};

static struct NativeHUDSettings s_nativeHUDSettings = {0, 0, 0, NATIVE_HUD_DRIFT_OFF};
static TextureID s_nativeHUDCNKGaugeTexture;
static int s_nativeHUDCNKGaugeLoadAttempted;

static int NativeHUD_Clamp(int value, int minValue, int maxValue)
{
	if (value < minValue) return minValue;
	if (value > maxValue) return maxValue;
	return value;
}

static void NativeHUD_SanitizeSettings(void)
{
	s_nativeHUDSettings.saffiHUD = s_nativeHUDSettings.saffiHUD != 0;
	s_nativeHUDSettings.reservesBar = s_nativeHUDSettings.reservesBar != 0;
	s_nativeHUDSettings.driftMeter =
		NativeHUD_Clamp(s_nativeHUDSettings.driftMeter,
		               NATIVE_HUD_DRIFT_OFF,
		               NATIVE_HUD_DRIFT_CNK_CENTERED);
}

static void NativeHUD_LoadSettings(void)
{
	FILE *f;
	char line[128];

	if (s_nativeHUDSettings.loaded)
		return;

	f = fopen(NATIVE_HUD_SETTINGS_FILE, "rb");
	if (f != NULL)
	{
		while (fgets(line, sizeof(line), f) != NULL)
		{
			char key[64];
			int value;
			if (sscanf(line, "%63[^=]=%d", key, &value) != 2)
				continue;

			if (strcmp(key, "saffi_hud") == 0)
				s_nativeHUDSettings.saffiHUD = value;
			else if (strcmp(key, "reserves_bar") == 0)
				s_nativeHUDSettings.reservesBar = value;
			else if (strcmp(key, "drift_meter") == 0)
				s_nativeHUDSettings.driftMeter = value;
		}
		fclose(f);
	}

	s_nativeHUDSettings.loaded = 1;
	NativeHUD_SanitizeSettings();
}

static void NativeHUD_SaveSettings(void)
{
	FILE *f;

	NativeHUD_LoadSettings();
	NativeHUD_SanitizeSettings();

	f = fopen(NATIVE_HUD_SETTINGS_FILE, "wb");
	if (f == NULL)
	{
		Platform_LogWarn("[NativeHUD] could not save %s\n", NATIVE_HUD_SETTINGS_FILE);
		return;
	}

	fprintf(f, "version=1\n");
	fprintf(f, "saffi_hud=%d\n", s_nativeHUDSettings.saffiHUD);
	fprintf(f, "reserves_bar=%d\n", s_nativeHUDSettings.reservesBar);
	fprintf(f, "drift_meter=%d\n", s_nativeHUDSettings.driftMeter);
	fclose(f);
}

int NativeHUD_GetSaffiEnabled(void)
{
	NativeHUD_LoadSettings();
	return s_nativeHUDSettings.saffiHUD;
}

void NativeHUD_SetSaffiEnabled(int enabled)
{
	NativeHUD_LoadSettings();
	s_nativeHUDSettings.saffiHUD = enabled != 0;
	NativeHUD_SaveSettings();
}

void NativeHUD_HandleGameplayToggle(void)
{
	/* Triangle belongs to the original game: skip intros and look behind.
	 * Change the optional Saffi HUD from Options instead of a gameplay hotkey.
	 * Retain this entry point for existing render/HUD callers. */
}

int NativeHUD_GetReservesBarEnabled(void)
{
	NativeHUD_LoadSettings();
	return s_nativeHUDSettings.reservesBar;
}

void NativeHUD_SetReservesBarEnabled(int enabled)
{
	NativeHUD_LoadSettings();
	s_nativeHUDSettings.reservesBar = enabled != 0;
	NativeHUD_SaveSettings();
}

int NativeHUD_GetDriftMeterMode(void)
{
	NativeHUD_LoadSettings();
	return s_nativeHUDSettings.driftMeter;
}

void NativeHUD_SetDriftMeterMode(int mode)
{
	NativeHUD_LoadSettings();
	s_nativeHUDSettings.driftMeter =
		NativeHUD_Clamp(mode, NATIVE_HUD_DRIFT_OFF, NATIVE_HUD_DRIFT_CNK_CENTERED);
	NativeHUD_SaveSettings();
}

void NativeHUD_GetPlayerData(const struct Driver *driver, struct NativeHUDPlayerData *out)
{
	int room;

	if (out == NULL)
		return;

	memset(out, 0, sizeof(*out));
	if (driver == NULL)
		return;

	room = driver->turbo_MeterRoomLeft;
	if (NativeWorld_IsActive())
		room = NativeWorld_HudRoom((struct Driver *)driver);

	out->reserves = NativeReserves_Get(driver);
	out->slideRoom = room;
	out->slideMaxRoom = driver->const_turboMaxRoom * ELAPSED_MS;
	out->slideReady =
		room <= (driver->const_turboLowRoomWarning * ELAPSED_MS);
	out->isDrifting = driver->kartState == KS_DRIFTING;
	out->fireSpeedCap = driver->fireSpeedCap;
	out->turboOutsideTimer = driver->turbo_outsideTimer;
}

static void NativeHUD_GetAnchor(
	const struct Driver *driver,
	int *centerX,
	int *meterY,
	int *reservesY)
{
	const struct GameTracker *gGT = sdata->gGT;
	int playerIndex = driver->driverID;
	const RECT *viewport;

	if (playerIndex < 0 || playerIndex >= 4)
		playerIndex = 0;

	viewport = &gGT->pushBuffer[playerIndex].rect;
	*centerX = viewport->x + viewport->w / 2;

	if (gGT->numPlyrCurrGame >= 3)
	{
		*meterY = viewport->y + viewport->h - 10;
		*reservesY = *meterY + 3;
	}
	else
	{
		*meterY = viewport->y + viewport->h - 20;
		*reservesY = *meterY + 5;
	}
}

static void NativeHUD_DrawCenteredCTRSlideMeter(
	struct Driver *driver,
	const struct NativeHUDPlayerData *hud)
{
	int centerX;
	int meterY;
	int reservesY;

	if (!hud->isDrifting)
		return;

	NativeHUD_GetAnchor(driver, &centerX, &meterY, &reservesY);
	(void)reservesY;

	/* UI_DrawSlideMeter draws 49 px to the LEFT of posX, so +24 keeps
	   the retail bar centered without changing its renderer or thresholds. */
	UI_DrawSlideMeter((s16)(centerX + 24), (s16)meterY, driver);
}

static Color NativeHUD_ReservesLayerColor(int layer)
{
	switch (layer % 6)
	{
	case 0:
		return MakeColor(32, 255, 80);
	case 1:
		return MakeColor(32, 220, 255);
	case 2:
		return MakeColor(72, 120, 255);
	case 3:
		return MakeColor(190, 72, 255);
	case 4:
		return MakeColor(255, 200, 32);
	default:
		return MakeColor(255, 72, 48);
	}
}

static void NativeHUD_DrawReservesBar(
	struct Driver *driver,
	const struct NativeHUDPlayerData *hud,
	int underDriftMeter)
{
	struct GameTracker *gGT = sdata->gGT;
	int centerX;
	int meterY;
	int reservesY;
	int value;
	int completedBars;
	int activeLayer;
	int layerValue;
	int fillWidth;
	char counterText[16];
	RECT box;
	Color color;

	NativeHUD_GetAnchor(driver, &centerX, &meterY, &reservesY);

	if (!underDriftMeter)
		reservesY = meterY;

	value = hud->reserves;
	if (value < 0)
		value = 0;

	/*
	 * One logical reserve bar is 3840 reserve units. Overflow no longer
	 * leaves the meter permanently full: completed bars are counted, then
	 * the visible bar starts filling the next colored layer.
	 */
	completedBars = value / NATIVE_HUD_RESERVES_NORMAL_MAX;
	activeLayer = completedBars;
	layerValue = value % NATIVE_HUD_RESERVES_NORMAL_MAX;

	/* At an exact multiple, keep the just-completed layer visibly full. */
	if (value > 0 && layerValue == 0)
	{
		activeLayer = completedBars - 1;
		layerValue = NATIVE_HUD_RESERVES_NORMAL_MAX;
	}

	fillWidth =
		(layerValue * NATIVE_HUD_RESERVES_W) /
		NATIVE_HUD_RESERVES_NORMAL_MAX;
	fillWidth =
		NativeHUD_Clamp(
			fillWidth,
			0,
			NATIVE_HUD_RESERVES_W);

	if (value > 0 && fillWidth == 0)
		fillWidth = 1;

	box.x =
		(s16)(centerX -
		      (NATIVE_HUD_RESERVES_W / 2));
	box.y = (s16)reservesY;
	box.w = NATIVE_HUD_RESERVES_W;
	box.h = NATIVE_HUD_RESERVES_H;

	/*
	 * OT order is reversed: submit border, fill, then background so the
	 * final picture is background -> colored reserve layer -> border.
	 */
	color = MakeColor(0, 0, 0);
	CTR_Box_DrawWireBox(
		&box,
		&color,
		gGT->pushBuffer_UI.ptrOT,
		&gGT->backBuffer->primMem);

	if (fillWidth > 0)
	{
		RECT fill = box;
		fill.w = (s16)fillWidth;
		color = NativeHUD_ReservesLayerColor(activeLayer);
		CTR_Box_DrawSolidBox(
			&fill,
			color,
			gGT->pushBuffer_UI.ptrOT);
	}

	color = MakeColor(36, 36, 36);
	CTR_Box_DrawSolidBox(
		&box,
		color,
		gGT->pushBuffer_UI.ptrOT);

	/* Number of completely filled reserve bars, kept next to the meter. */
	snprintf(
		counterText,
		sizeof(counterText),
		"%d",
		completedBars);
	DecalFont_DrawLine(
		counterText,
		centerX + (NATIVE_HUD_RESERVES_W / 2) +
			NATIVE_HUD_RESERVES_COUNTER_GAP,
		reservesY - 4,
		FONT_SMALL,
		WHITE);
}

static int NativeHUD_LoadCNKGaugeTexture(void)
{
	char path[1024];
	SDL_Surface *source;
	SDL_Surface *rgba;
	unsigned char *pixels;

	if (s_nativeHUDCNKGaugeTexture != 0)
		return 1;
	if (s_nativeHUDCNKGaugeLoadAttempted)
		return 0;

	s_nativeHUDCNKGaugeLoadAttempted = 1;

	if (!NativeAssets_ResolvePath(
		    "UI_NATIVE/cnk_turbo_gauge.png",
		    path,
		    sizeof(path)))
		return 0;

	source = SDL_LoadPNG(path);
	if (source == NULL)
		return 0;

	rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(source);
	if (rgba == NULL)
		return 0;

	if (rgba->w != NATIVE_HUD_CNK_GAUGE_ATLAS_W ||
	    rgba->h != NATIVE_HUD_CNK_GAUGE_ATLAS_H)
	{
		SDL_DestroySurface(rgba);
		return 0;
	}

	pixels = (unsigned char *)malloc(
		(size_t)rgba->w * (size_t)rgba->h * 4u);
	if (pixels == NULL)
	{
		SDL_DestroySurface(rgba);
		return 0;
	}

	for (int y = 0; y < rgba->h; y++)
	{
		memcpy(
			pixels + (size_t)y * (size_t)rgba->w * 4u,
			(const unsigned char *)rgba->pixels +
				(size_t)y * rgba->pitch,
			(size_t)rgba->w * 4u);
	}

	s_nativeHUDCNKGaugeTexture =
		NativeRenderer_CreateImageTexture(
			pixels,
			rgba->w,
			rgba->h);

	free(pixels);
	SDL_DestroySurface(rgba);

	return s_nativeHUDCNKGaugeTexture != 0;
}

static int NativeHUD_DrawCNKGaugeFrame(
	int frame,
	int x,
	int y,
	int w,
	int h,
	int mirrorX)
{
	struct GameTracker *gGT = sdata->gGT;
	struct PrimMem *primMem;
	uint32_t *ot;
	DR_PSYX_TEX_FT4 *packet;
	POLY_FT4 *poly;
	const size_t bytes = sizeof(DR_PSYX_TEX_FT4);
	int column;
	int row;
	int u0;
	int v0;
	int u1;
	int v1;

	if (gGT == NULL ||
	    frame < 0 ||
	    frame >= NATIVE_HUD_CNK_GAUGE_FRAMES ||
	    w <= 0 ||
	    h <= 0 ||
	    !NativeHUD_LoadCNKGaugeTexture())
		return 0;

	primMem = &gGT->backBuffer->primMem;
	ot = gGT->pushBuffer_UI.ptrOT;

	if ((char *)primMem->cursor + bytes >=
	    (char *)primMem->guardEnd)
		return 0;

	column = frame % NATIVE_HUD_CNK_GAUGE_COLUMNS;
	row = frame / NATIVE_HUD_CNK_GAUGE_COLUMNS;
	u0 = column * NATIVE_HUD_CNK_GAUGE_CELL_W;
	v0 = row * NATIVE_HUD_CNK_GAUGE_CELL_H;
	u1 = u0 + NATIVE_HUD_CNK_GAUGE_CELL_W - 1;
	v1 = v0 + NATIVE_HUD_CNK_GAUGE_CELL_H - 1;

	packet = (DR_PSYX_TEX_FT4 *)primMem->cursor;
	memset(packet, 0, bytes);
	setlen(
		packet,
		(int)(bytes / sizeof(uint32_t)) - 1);

	packet->code[0] =
		0xb4000000u |
		((uint32_t)s_nativeHUDCNKGaugeTexture &
		 0x00ffffffu);
	packet->code[1] =
		NATIVE_HUD_CNK_GAUGE_ATLAS_W |
		(NATIVE_HUD_CNK_GAUGE_ATLAS_H << 16);

	poly = &packet->poly;
	setPolyFT4(poly);

	/* 128 is neutral modulation for the original CNK sprite colors. */
	setRGB0(poly, 128, 128, 128);

	poly->x0 = (s16)x;
	poly->y0 = (s16)y;
	poly->x1 = (s16)(x + w);
	poly->y1 = (s16)y;
	poly->x2 = (s16)x;
	poly->y2 = (s16)(y + h);
	poly->x3 = (s16)(x + w);
	poly->y3 = (s16)(y + h);

	if (mirrorX)
	{
		poly->u0 = (u8)u1;
		poly->u1 = (u8)u0;
		poly->u2 = (u8)u1;
		poly->u3 = (u8)u0;
	}
	else
	{
		poly->u0 = (u8)u0;
		poly->u1 = (u8)u1;
		poly->u2 = (u8)u0;
		poly->u3 = (u8)u1;
	}
	poly->v0 = (u8)v0;
	poly->v1 = (u8)v0;
	poly->v2 = (u8)v1;
	poly->v3 = (u8)v1;
	poly->tpage = 0;
	poly->clut = 0;

	addPrim(ot, packet);
	primMem->cursor = (char *)packet + bytes;
	return 1;
}

static void NativeHUD_DrawCNKSlideMeter(
	struct Driver *driver,
	const struct NativeHUDPlayerData *hud)
{
	struct GameTracker *gGT = sdata->gGT;
	int centerX;
	int meterY;
	int reservesY;
	int maxRoom;
	int progress;
	int frame;
	int drawW;
	int drawH;
	int sideOffset;
	int gaugeCenterX;
	int gaugeY;
	int mirrorX;
	int playerIndex;
	const RECT *viewport;

	if (!hud->isDrifting || gGT == NULL)
		return;

	NativeHUD_GetAnchor(
		driver,
		&centerX,
		&meterY,
		&reservesY);
	(void)meterY;
	(void)reservesY;

	playerIndex = driver->driverID;
	if (playerIndex < 0 || playerIndex >= 4)
		playerIndex = 0;
	viewport = &gGT->pushBuffer[playerIndex].rect;

	maxRoom = hud->slideMaxRoom;
	if (maxRoom <= 0)
		return;

	progress = maxRoom - hud->slideRoom;
	progress = NativeHUD_Clamp(
		progress,
		0,
		maxRoom);

	/*
	 * The source sheet contains 18 successive CNK Turbo Charge Gauge
	 * sprites. Use them directly as a discrete animation driven by CTR's
	 * existing powerslide charge value. No extra sparks/flags/signals from
	 * the source sheet are used.
	 */
	frame =
		(progress * (NATIVE_HUD_CNK_GAUGE_FRAMES - 1)) /
		maxRoom;
	frame = NativeHUD_Clamp(
		frame,
		0,
		NATIVE_HUD_CNK_GAUGE_FRAMES - 1);

	drawW = NATIVE_HUD_CNK_GAUGE_CELL_W;
	drawH = NATIVE_HUD_CNK_GAUGE_CELL_H;

	/* Keep the vertical gauge readable without crowding 3P/4P viewports. */
	if (gGT->numPlyrCurrGame >= 3)
	{
		drawW = 21;
		drawH = 24;
		sideOffset = NATIVE_HUD_CNK_SIDE_OFFSET_SPLIT;
	}
	else
	{
		sideOffset = NATIVE_HUD_CNK_SIDE_OFFSET;
	}

	/*
	 * CNK places the turbo gauge on the opposite side of the screen-center
	 * from the direction of the powerslide. CTR locks multDrift when the
	 * powerslide begins, so this follows the actual drift side rather than
	 * momentary steering corrections during the slide:
	 *   multDrift < 0 = drifting right  -> gauge on the left
	 *   multDrift > 0 = drifting left   -> gauge on the right
	 */
	gaugeCenterX = centerX;
	mirrorX = 0;
	if (driver->multDrift < 0)
	{
		gaugeCenterX -= sideOffset;
		mirrorX = 1;
	}
	else if (driver->multDrift > 0)
	{
		gaugeCenterX += sideOffset;
	}

	/* Y is centered in the player's viewport; X behavior stays unchanged. */
	gaugeY =
		viewport->y +
		(viewport->h / 2) -
		(drawH / 2);

	NativeHUD_DrawCNKGaugeFrame(
		frame,
		gaugeCenterX - drawW / 2,
		gaugeY,
		drawW,
		drawH,
		mirrorX);
}

static int NativeHUD_SaffiScaleValue(int value, int scale)
{
	return (value * scale) / NATIVE_HUD_SAFFI_SCALE_ONE;
}

static int NativeHUD_SaffiScaleX(int value, int scale)
{
	/* Gasmoxian's WIDE_34 macro multiplies X offsets by 0.75 in 16:9. */
	if (Platform_GetWideMode())
		value = (value * 3) / 4;
	return NativeHUD_SaffiScaleValue(value, scale);
}

static int NativeHUD_SaffiViewportScale(void)
{
	const int players = sdata->gGT->numPlyrCurrGame;
	if (players >= 3)
		return 128;
	if (players == 2)
		return 192;
	return NATIVE_HUD_SAFFI_SCALE_ONE;
}

static void NativeHUD_SaffiGetAnchor(
	const struct Driver *driver,
	int scale,
	int *centerX,
	int *centerY)
{
	const struct GameTracker *gGT = sdata->gGT;
	int playerIndex = driver->driverID;
	const RECT *viewport;

	if (playerIndex < 0 || playerIndex >= 4)
		playerIndex = 0;
	viewport = &gGT->pushBuffer[playerIndex].rect;

	/* 512x240 reference: Gasmoxian uses center (453,190). */
	*centerX = viewport->x + viewport->w -
		NativeHUD_SaffiScaleValue(NATIVE_HUD_SAFFI_CENTER_RIGHT, scale);
	*centerY = viewport->y + viewport->h -
		NativeHUD_SaffiScaleValue(NATIVE_HUD_SAFFI_CENTER_BOTTOM, scale);
}

static Point NativeHUD_SaffiPoint(
	int centerX,
	int centerY,
	int index,
	int scale)
{
	return MakePoint(
		(s16)(centerX + NativeHUD_SaffiScaleX(
			s_nativeHUDSaffiSpeedometerData[index][0], scale)),
		(s16)(centerY + NativeHUD_SaffiScaleValue(
			s_nativeHUDSaffiSpeedometerData[index][1], scale)));
}

static Color NativeHUD_SaffiColor(int index)
{
	if (index <= 2)
		return MakeColor(0x00, 0xb5, 0x00);
	if (index == 3)
		return MakeColor(0xdb, 0xb5, 0x00);
	if (index <= 6)
		return MakeColor(0xdb, 0x00, 0x00);
	if (index == 7)
		return MakeColor(0xd8, 0x00, 0xcb);
	if (index <= 9)
		return MakeColor(0xa4, 0x21, 0xff);
	return MakeColor(0x18, 0x18, 0xf3);
}

static void NativeHUD_DrawSaffiBackground(
	int centerX,
	int centerY,
	int scale)
{
	struct GameTracker *gGT = sdata->gGT;

	/* White tick/ring lines plus the one-pixel black Gasmoxian shadow. */
	for (int i = 0; i < NATIVE_HUD_SAFFI_POINT_COUNT; i += 2)
	{
		Point p0 = NativeHUD_SaffiPoint(centerX, centerY, i, scale);
		Point p1 = NativeHUD_SaffiPoint(centerX, centerY, i + 1, scale);
		CTR_Box_DrawWirePrims(p0, p1, MakeColor(0xff, 0xff, 0xff), gGT->pushBuffer_UI.ptrOT);
		CTR_Box_DrawWirePrims(
			MakePoint(p0.x + 1, p0.y + 1),
			MakePoint(p1.x + 1, p1.y + 1),
			MakeColor(0, 0, 0),
			gGT->pushBuffer_UI.ptrOT);
	}

	/* Colored gauge segments and their white/black dividers. */
	for (int i = 0, colorIndex = 0;
	     i < NATIVE_HUD_SAFFI_POINT_COUNT - 2;
	     i += 2, colorIndex++)
	{
		Point pt[4];
		for (int j = 0; j < 4; j++)
			pt[j] = NativeHUD_SaffiPoint(centerX, centerY, i + j, scale);

		CTR_Box_DrawWirePrims(pt[0], pt[2], MakeColor(0xff, 0xff, 0xff), gGT->pushBuffer_UI.ptrOT);
		CTR_Box_DrawWirePrims(pt[1], pt[3], MakeColor(0xff, 0xff, 0xff), gGT->pushBuffer_UI.ptrOT);
		CTR_Box_DrawWirePrims(MakePoint(pt[0].x + 1, pt[0].y + 1), MakePoint(pt[2].x + 1, pt[2].y + 1), MakeColor(0, 0, 0), gGT->pushBuffer_UI.ptrOT);
		CTR_Box_DrawWirePrims(MakePoint(pt[1].x + 1, pt[1].y + 1), MakePoint(pt[3].x + 1, pt[3].y + 1), MakeColor(0, 0, 0), gGT->pushBuffer_UI.ptrOT);

		PolyG4 *poly;
		GetPrimMem(poly);
		if (poly == nullptr)
			return;

		const PrimCode primCode = {.poly = {.renderCode = RenderCode_Polygon, .gouraud = 1, .quad = 1}};
		ColorCode colorBottom = NativeHUD_SaffiColor(colorIndex);
		ColorCode colorTop = NativeHUD_SaffiColor(colorIndex + 1);
		colorBottom.code = primCode;
		colorTop.code = primCode;

		for (int j = 0; j < 2; j++)
		{
			poly->v[j].pos = pt[j];
			poly->v[j].color = colorBottom;
			poly->v[j + 2].pos = pt[j + 2];
			poly->v[j + 2].color = colorTop;
		}
		poly->gPolyCode = primCode;
		AddPrimitive(poly, gGT->pushBuffer_UI.ptrOT);
	}

	/* Same translucent black fan used by the Gasmoxian speedometer. */
	typedef struct NativeHUDSaffiTPagePolyG3
	{
		TPage t;
		PolyG3 p;
	} NativeHUDSaffiTPagePolyG3;

	Point centerEdge = NativeHUD_SaffiPoint(
		centerX, centerY, NATIVE_HUD_SAFFI_POINT_COUNT - 1, scale);
	Point firstInner = NativeHUD_SaffiPoint(centerX, centerY, 1, scale);

	for (int i = 0; i < NATIVE_HUD_SAFFI_POINT_COUNT - 2; i += 2)
	{
		NativeHUDSaffiTPagePolyG3 *packet;
		GetPrimMem(packet);
		if (packet == nullptr)
			return;

		packet->t.texpage = (Texpage){.code = 0xE1, .dither = 1};
		packet->t.texpage.drawDisplayArea = 1;
		packet->p.tag.self = 0;

		const PrimCode primCode = {.poly = {.renderCode = RenderCode_Polygon, .gouraud = 1, .semiTransparency = 1}};
		ColorCode black = MakeColor(0, 0, 0);
		black.code = primCode;
		for (int j = 0; j < 3; j++)
			packet->p.v[j].color = black;
		packet->p.gPolyCode = primCode;

		packet->p.v[0].pos = NativeHUD_SaffiPoint(centerX, centerY, i + 1, scale);
		packet->p.v[1].pos = NativeHUD_SaffiPoint(centerX, centerY, i + 3, scale);
		packet->p.v[2].pos = MakePoint(centerEdge.x, firstInner.y);

		AddPrimitive(packet, gGT->pushBuffer_UI.ptrOT);
	}
}

static void NativeHUD_DrawSaffiHUD(
	struct Driver *driver,
	const struct NativeHUDPlayerData *hud)
{
	int centerX;
	int centerY;
	int scale;
	int speed;
	int displaySpeed;
	int minScale = 0;
	int maxScale = NATIVE_HUD_SAFFI_SPEED_MAX;
	int minAngle;
	int maxAngle;
	int accelSpeedInt;
	int speedScale;
	int angle1;
	int angle2;
	int cosAngle[2];
	int sinAngle[2];
	int needleHeight;
	int needleWidth;
	int needlePadding;
	int yLen;
	char speedText[16];
	PolyG3 *poly;
	const PrimCode primCode = {.poly = {.gouraud = 1, .renderCode = RenderCode_Polygon}};

	(void)hud;
	if (driver == NULL || sdata->gGT == NULL)
		return;

	scale = NativeHUD_SaffiViewportScale();
	NativeHUD_SaffiGetAnchor(driver, scale, &centerX, &centerY);

	/* Gasmoxian reads planar kart speed for its numeric display. The needle
	 * uses the native interpolated retail value so it stays smooth at 200 Hz. */
	displaySpeed = MATH_FastSqrt(
		(u32)((driver->xSpeed * driver->xSpeed) +
		      (driver->zSpeed * driver->zSpeed)),
		0);
	speed = driver->speedometerNeedleValue;
	if (NativeWorld_IsActive())
		speed = NativeWorld_HudSpeedometer(driver);
	if (driver->kartState == KS_ENGINE_REVVING)
		displaySpeed = speed;

	accelSpeedInt = FP8_INT(driver->const_AccelSpeed_ClassStat);
	if (driver->const_AccelSpeed_ClassStat < speed)
	{
		/* Gasmoxian extends the upper sweep through the USF range. */
		maxAngle = ANG(67.5);
		minAngle = ANG(213.75);
		minScale = (accelSpeedInt * 108000) / 64000;
	}
	else
	{
		maxAngle = ANG(213.75);
		minAngle = ANG(305.20);
		maxScale = accelSpeedInt;
	}

	speedScale = (FP8_INT(speed) * 108000) / 64000;
	maxScale = (maxScale * 108000) / 64000;
	angle1 = VehCalc_MapToRange(
		speedScale, minScale, maxScale, minAngle, maxAngle);
	angle2 = angle1 + ANG(90);

	cosAngle[0] = MATH_Cos(angle1);
	cosAngle[1] = MATH_Cos(angle2);
	sinAngle[0] = MATH_Sin(angle1);
	sinAngle[1] = MATH_Sin(angle2);

	needleHeight = NativeHUD_SaffiScaleValue(
		NATIVE_HUD_SAFFI_NEEDLE_HEIGHT, scale);
	needleWidth = NativeHUD_SaffiScaleValue(
		NATIVE_HUD_SAFFI_NEEDLE_WIDTH, scale);
	needlePadding = NativeHUD_SaffiScaleValue(2, scale);
	if (needleWidth < 2) needleWidth = 2;
	if (needlePadding < 1) needlePadding = 1;

	/* First half of the original two-triangle yellow needle. */
	GetPrimMem(poly);
	if (poly == nullptr)
		return;
	poly->v[0].color = MakeColorCode(91, 91, 0, primCode);
	poly->v[1].color = MakeColorCode(50, 43, 1, primCode);
	poly->v[2].color = MakeColorCode(255, 187, 0, primCode);

	yLen = FP_INT(cosAngle[1] * (needleWidth + needlePadding)) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[2].pos.x = (s16)(centerX + NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[1] * needleWidth), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[2].pos.y = (s16)(centerY + yLen);

	yLen = FP_INT(cosAngle[0] * needleWidth) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[1].pos.x = (s16)(centerX - NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[0] * needleWidth), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[1].pos.y = (s16)(centerY - yLen);

	yLen = FP_INT(cosAngle[0] * needleHeight) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[0].pos.x = (s16)(centerX + NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[0] * needleHeight), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[0].pos.y = (s16)(centerY + yLen);
	AddPrimitive(poly, sdata->gGT->pushBuffer_UI.ptrOT);

	/* Second half/highlight, matching Gasmoxian's original colors. */
	GetPrimMem(poly);
	if (poly == nullptr)
		return;
	poly->v[0].color = MakeColorCode(255, 255, 255, primCode);
	poly->v[1].color = MakeColorCode(156, 105, 0, primCode);
	poly->v[2].color = MakeColorCode(255, 255, 0, primCode);

	yLen = FP_INT(cosAngle[1] * (needleWidth + needlePadding)) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[2].pos.x = (s16)(centerX - NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[1] * needleWidth), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[2].pos.y = (s16)(centerY - yLen);

	yLen = FP_INT(cosAngle[0] * needleWidth) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[1].pos.x = (s16)(centerX - NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[0] * needleWidth), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[1].pos.y = (s16)(centerY - yLen);

	yLen = FP_INT(cosAngle[0] * needleHeight) * FP8(1.25);
	if (yLen < 0) yLen += FP8(2) - 1;
	yLen /= FP8(2);
	poly->v[0].pos.x = (s16)(centerX + NativeHUD_SaffiScaleX(
		FP_INT(sinAngle[0] * needleHeight), NATIVE_HUD_SAFFI_SCALE_ONE));
	poly->v[0].pos.y = (s16)(centerY + yLen);
	AddPrimitive(poly, sdata->gGT->pushBuffer_UI.ptrOT);

	/* Numeric speed is part of the Gasmoxian/Saffi presentation. */
	snprintf(speedText, sizeof(speedText), "%d", FP8_INT(displaySpeed));
	DecalFont_DrawLine(
		speedText,
		centerX - NativeHUD_SaffiScaleValue(5, scale),
		centerY + NativeHUD_SaffiScaleValue(5, scale),
		FONT_SMALL,
		PAPU_YELLOW);

	/* Submit the translucent fan last, as in Gasmoxian, so OT ordering keeps
	 * the gauge behind the needle/text. */
	NativeHUD_DrawSaffiBackground(centerX, centerY, scale);
}

void NativeHUD_RenderPlayerExtras(struct Driver *driver)
{
	struct NativeHUDPlayerData hud;
	int driftMode;
	int drawReserves;
	int drawSaffi;

	if (driver == NULL)
		return;

	driftMode = NativeHUD_GetDriftMeterMode();
	drawReserves = NativeHUD_GetReservesBarEnabled();
	drawSaffi = NativeHUD_GetSaffiEnabled();

	if (driftMode == NATIVE_HUD_DRIFT_OFF && !drawReserves && !drawSaffi)
		return;

	NativeHUD_GetPlayerData(driver, &hud);

	/* The UI OT is prepend-based: primitives submitted earlier render later,
	 * therefore visually on top. Submit meters/reserves before Saffi so any
	 * overlap keeps the gameplay gauges readable over the speedometer instead
	 * of moving their screen positions. */
	if (driftMode == NATIVE_HUD_DRIFT_CTR_CENTERED)
		NativeHUD_DrawCenteredCTRSlideMeter(driver, &hud);
	else if (driftMode == NATIVE_HUD_DRIFT_CNK_CENTERED)
		NativeHUD_DrawCNKSlideMeter(driver, &hud);

	if (drawReserves)
		NativeHUD_DrawReservesBar(
			driver,
			&hud,
			(driftMode != NATIVE_HUD_DRIFT_OFF) && hud.isDrifting);

	if (drawSaffi)
		NativeHUD_DrawSaffiHUD(driver, &hud);

}

#else

int NativeHUD_GetSaffiEnabled(void) { return 0; }
void NativeHUD_SetSaffiEnabled(int enabled) { (void)enabled; }
int NativeHUD_GetReservesBarEnabled(void) { return 0; }
void NativeHUD_SetReservesBarEnabled(int enabled) { (void)enabled; }
int NativeHUD_GetDriftMeterMode(void) { return NATIVE_HUD_DRIFT_OFF; }
void NativeHUD_SetDriftMeterMode(int mode) { (void)mode; }
void NativeHUD_HandleGameplayToggle(void) {}
void NativeHUD_GetPlayerData(const struct Driver *driver, struct NativeHUDPlayerData *out)
{
	(void)driver;
	if (out != NULL) memset(out, 0, sizeof(*out));
}
void NativeHUD_RenderPlayerExtras(struct Driver *driver) { (void)driver; }

#endif
