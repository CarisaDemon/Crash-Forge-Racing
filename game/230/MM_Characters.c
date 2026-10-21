#include <common.h>
#include <EngineSoundRegistry.h>
#if defined(CTR_NATIVE)
#include <NativeHangout.h>
#include <NativeRacerVisual.h>
#include <platform/native_gltf.h>
#include <platform/native_obj.h>
#include <platform/native_input.h>
#include <platform/native_assets.h>
#include <platform/native_renderer.h>
#include <SDL3/SDL.h>
#include <RacerLoadout.h>
#include <VehiclePartRegistry.h>
#endif

enum
{
	MM_CHARACTER_SELECT_SCREEN_W = 0x200,
	MM_CHARACTER_SELECT_SCREEN_H = 0xd8,
	MM_CHARACTER_SELECT_DISTANCE_TO_SCREEN = 0x100,
	MM_CHARACTER_SELECT_MODEL_MOVE_FP = 0x1000,
	MM_CHARACTER_SELECT_MODEL_MOVE_FP_SHIFT = 0xc,
	MM_CHARACTER_SELECT_MODEL_MOVE_NEXT = 1,
	MM_CHARACTER_SELECT_MODEL_MOVE_PREV = -1,
	MM_CHARACTER_SELECT_ICON_COUNT = 0xf,
	MM_CHARACTER_SELECT_EXPANSION_ICON_FIRST = 0xc,
	MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT = 8,
	MM_CHARACTER_SELECT_MAX_PLAYERS = 4,
	MM_CHARACTER_SELECT_LIMITED_LAYOUT_OFFSET = 4,
	MM_CHARACTER_SELECT_FULL_LAYOUT_COUNT = 2,
	MM_CHARACTER_SELECT_TRANSITION_FRAMES = 0xc,
	MM_CHARACTER_SELECT_TRANSITION_STEP = 8,
	MM_CHARACTER_SELECT_ANGLE_STEP = 0x400,
	MM_CHARACTER_SELECT_ANGLE_OFFSET = 400,
	MM_CHARACTER_SELECT_SPIN_STEP = 0x40,
	MM_CHARACTER_SELECT_LAYOUT_3P = 2,
	MM_CHARACTER_SELECT_LAYOUT_4P = 3,
	MM_CHARACTER_SELECT_LAYOUT_1P_LIMITED = 4,
	MM_CHARACTER_SELECT_LAYOUT_2P_LIMITED = 5,
	MM_CHARACTER_SELECT_TITLE_TRANSITION_INDEX = 15,
	MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST = 0x10,
	MM_CHARACTER_SELECT_3P_TITLE_X = 0x9c,
	MM_CHARACTER_SELECT_3P_SELECT_Y = 0x14,
	MM_CHARACTER_SELECT_3P_CHARACTER_Y = 0x26,
	MM_CHARACTER_SELECT_4P_TITLE_X = 0xfc,
	MM_CHARACTER_SELECT_4P_SELECT_Y = 8,
	MM_CHARACTER_SELECT_4P_CHARACTER_Y = 0x18,
	MM_CHARACTER_SELECT_LIMITED_TITLE_X = 0xfc,
	MM_CHARACTER_SELECT_LIMITED_TITLE_Y = 10,
	MM_CHARACTER_SELECT_INPUT_DPAD = BTN_RIGHT | BTN_LEFT | BTN_DOWN | BTN_UP,
	MM_CHARACTER_SELECT_INPUT_MENU = BTN_TRIANGLE | BTN_CIRCLE | BTN_SQUARE_one | BTN_CROSS_one,
	MM_CHARACTER_SELECT_INPUT_CONFIRM = BTN_CIRCLE | BTN_CROSS_one,
	MM_CHARACTER_SELECT_INPUT_BACK = BTN_TRIANGLE | BTN_SQUARE_one,
	MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_X = 6,
	MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_Y = 4,
	MM_CHARACTER_SELECT_ICON_RECT_W = 0x34,
	MM_CHARACTER_SELECT_ICON_RECT_H = 0x21,
	/* Retail driver portraits occupy about 40x25 logical pixels inside the
	 * 52x33 selector frame. Community icon PNGs are commonly square (128x128),
	 * so drawing them with source aspect in CTR's non-square UI space makes
	 * faces look unnaturally narrow. Fit custom icons to the retail portrait
	 * aspect first, then scale that frame down only when a paged slot is tighter. */
	MM_CHARACTER_SELECT_NATIVE_ICON_W = 0x28,
	MM_CHARACTER_SELECT_NATIVE_ICON_H = 0x19,
	MM_CHARACTER_SELECT_CURSOR_LABEL_OFFSET_X = -6,
	MM_CHARACTER_SELECT_CURSOR_LABEL_OFFSET_Y = -3,
	MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_X = 3,
	MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_Y = 2,
	MM_CHARACTER_SELECT_HIGHLIGHT_W = 0x2e,
	MM_CHARACTER_SELECT_HIGHLIGHT_H = 0x1d,
	MM_CHARACTER_SELECT_SELECTED_BORDER_COUNT = 2,
	MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_X = 3,
	MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_Y = 2,
	MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_W = 6,
	MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_H = 4,
	MM_CHARACTER_SELECT_4P_NAME_BOTTOM_OFFSET = -6,
	MM_CHARACTER_SELECT_COLOR_PHASE_FRAME_STEP = 0x100,
	MM_CHARACTER_SELECT_COLOR_PHASE_PLAYER_STEP = 0x400,
	MM_CHARACTER_SELECT_COLOR_TRIG_MASK = 0x3ff,
	MM_CHARACTER_SELECT_COLOR_TRIG_HIGH_HALF_BIT = 0x400,
	MM_CHARACTER_SELECT_COLOR_TRIG_NEGATE_BIT = 0x800,
	MM_CHARACTER_SELECT_COLOR_PULSE_THRESHOLD = 0xc00,
	MM_CHARACTER_SELECT_COLOR_PULSE_SCALE_SHIFT = 7,
	MM_CHARACTER_SELECT_COLOR_PULSE_FP_SHIFT = 0xc,
	/* Native 4:3 roster layout (logical 512x216 menu space). */
	MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE = 20,
	MM_CHARACTER_SELECT_ROSTER_COLUMNS = 4,
	MM_CHARACTER_SELECT_ROSTER_ROWS = 5,
	MM_CHARACTER_SELECT_ROSTER_X = 0x11c,
	MM_CHARACTER_SELECT_ROSTER_Y = 0x28,
	MM_CHARACTER_SELECT_ROSTER_SLOT_W = 0x2e,
	MM_CHARACTER_SELECT_ROSTER_SLOT_H = 0x1c,
	MM_CHARACTER_SELECT_ROSTER_COL_STEP = 0x38,
	MM_CHARACTER_SELECT_ROSTER_ROW_STEP = 0x23,
	/* Native 4P roster: same 4x5 geometry as the other paged selectors. */
	MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE = 20,
	MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS = 4,
	MM_CHARACTER_SELECT_ROSTER_4P_ROWS = 5,
	MM_CHARACTER_SELECT_ROSTER_4P_X = 0x11c,
	MM_CHARACTER_SELECT_ROSTER_4P_COL_STEP = 0x38,
	MM_CHARACTER_SELECT_PAGE_HEADER_Y = 0x12,
	MM_CHARACTER_SELECT_PAGE_L1_X = 0x132,
	MM_CHARACTER_SELECT_PAGE_TEXT_X = 0x187,
	MM_CHARACTER_SELECT_PAGE_R1_X = 0x1dc,
	MM_CHARACTER_SELECT_PREVIEW_X = 0x16,
	MM_CHARACTER_SELECT_PREVIEW_Y = 0x30,
	MM_CHARACTER_SELECT_PREVIEW_W = 0xe8,
	MM_CHARACTER_SELECT_PREVIEW_H = 0x8c,
	MM_CHARACTER_SELECT_PREVIEW_NAME_Y = 0xc4,
};

#if defined(CTR_NATIVE)
static s16 s_characterPage;
static s16 s_characterCursor;
static s16 s_characterCursor2P[2];
static s16 s_characterCursor3P[3];
static s16 s_characterCursor4P[4];

struct MMCharacterNativeIcon
{
	int characterID;
	TextureID texture;
	int width;
	int height;
	struct MMCharacterNativeIcon *next;
};

static struct MMCharacterNativeIcon *s_characterNativeIcons;

struct MMHangoutCustomizationSnapshot
{
	b32 valid;
	struct NativeRacerLoadout loadout;
	int numPlyrNextGame;
	u32 renderFlags;
	RECT pushBufferRect;
	SVec3 pushBufferPos;
	SVec3 pushBufferRot;
	int distanceToScreenPrev;
	int distanceToScreenCurr;
	int cameraMode;
	MATRIX instMatrix;
	struct Model *instModel;
	u32 instFlags;
	s16 instAnimFrame;
	s16 instVertSplit;
	u16 wheelSize;
	struct PushBuffer *idppPushBuffer[4];
	Vec3 posCurr;
	Vec3 posPrev;
	SVec3Slot rotCurr;
	SVec3Slot rotPrev;
	Vec3 velocity;
	s16 speed;
	s16 speedApprox;
	s16 fireSpeed;
	int xSpeed;
	int ySpeed;
	int zSpeed;
	u8 kartState;
	s16 wheelRotation;
	Actions actionsFlagSet;
	Actions actionsFlagSetPrevFrame;
	s16 engineSoundVolumeState;
	s16 engineSoundPitchState;
	EngineSoundMode engineSoundMode;
};

static struct MMHangoutCustomizationSnapshot s_hangoutCustomizationSnapshot;
static int s_hangoutPreviewEngineChoice = 0x7fffffff;
static int s_hangoutPreviewEnginePack = 0x7fffffff;
static int s_hangoutPreviewEngineClass = 0x7fffffff;
static int s_hangoutPreviewEngineKart = 0x7fffffff;

static struct MMCharacterNativeIcon *MM_Characters_RosterGetNativeIcon(int characterID)
{
	struct MMCharacterNativeIcon *entry;
	const struct CharacterDef *character;
	const char *iconPath;
	char relativePath[768];
	char resolvedPath[1024];
	SDL_Surface *source;
	SDL_Surface *rgba;
	unsigned char *pixels;
	int pathLen;

	if (characterID <= NITROS_OXIDE)
		return NULL;

	for (entry = s_characterNativeIcons; entry != NULL; entry = entry->next)
	{
		if (entry->characterID == characterID)
			return entry->texture != 0 ? entry : NULL;
	}

	character = CharacterRegistry_GetByID(characterID);
	iconPath = CharacterRegistry_GetIconPath(characterID);
	if (character == NULL || iconPath == NULL || !*iconPath)
		return NULL;

	entry = (struct MMCharacterNativeIcon *)calloc(1, sizeof(*entry));
	if (entry == NULL)
		return NULL;

	entry->characterID = characterID;
	entry->next = s_characterNativeIcons;
	s_characterNativeIcons = entry;

	pathLen = snprintf(relativePath, sizeof(relativePath),
		"mods/racers/%s/%s", character->slug, iconPath);
	if (pathLen <= 0 || (size_t)pathLen >= sizeof(relativePath) ||
		!NativeAssets_ResolvePath(relativePath, resolvedPath, sizeof(resolvedPath)))
	{
		SDL_Log("Character icon %s: could not resolve %s",
			character->slug, iconPath);
		return NULL;
	}

	source = SDL_LoadPNG(resolvedPath);
	if (source == NULL)
		source = SDL_LoadBMP(resolvedPath);
	if (source == NULL)
	{
		SDL_Log("Character icon %s: could not load %s",
			character->slug, resolvedPath);
		return NULL;
	}

	rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(source);
	if (rgba == NULL)
		return NULL;

	if (rgba->w <= 0 || rgba->h <= 0 || rgba->w > 256 || rgba->h > 256)
	{
		SDL_DestroySurface(rgba);
		return NULL;
	}

	pixels = (unsigned char *)malloc((size_t)rgba->w * (size_t)rgba->h * 4u);
	if (pixels == NULL)
	{
		SDL_DestroySurface(rgba);
		return NULL;
	}

	for (int y = 0; y < rgba->h; ++y)
	{
		memcpy(pixels + (size_t)y * (size_t)rgba->w * 4u,
			(const unsigned char *)rgba->pixels + (size_t)y * rgba->pitch,
			(size_t)rgba->w * 4u);
	}

	entry->texture = NativeRenderer_CreateImageTexture(pixels, rgba->w, rgba->h);
	entry->width = rgba->w;
	entry->height = rgba->h;
	free(pixels);
	SDL_DestroySurface(rgba);

	if (entry->texture == 0)
		return NULL;

	SDL_Log("Character icon %s: loaded %s (%dx%d)",
		character->slug, iconPath, entry->width, entry->height);
	return entry;
}

static b32 MM_Characters_RosterDrawNativeIcon(
	struct GameTracker *gGT,
	int characterID,
	const RECT *slotRect,
	Color iconColor)
{
	struct MMCharacterNativeIcon *nativeIcon;
	struct PrimMem *primMem;
	uint32_t *ot;
	DR_PSYX_TEX_FT4 *packet;
	POLY_FT4 *poly;
	const size_t bytes = sizeof(DR_PSYX_TEX_FT4);
	int maxW;
	int maxH;
	int drawW;
	int drawH;
	int x;
	int y;

	nativeIcon = MM_Characters_RosterGetNativeIcon(characterID);
	if (nativeIcon == NULL || nativeIcon->texture == 0 ||
		gGT == NULL || slotRect == NULL)
		return false;

	maxW = slotRect->w - 4;
	maxH = slotRect->h - 4;
	if (maxW <= 0 || maxH <= 0)
		return false;

	/* Match the retail selector's logical portrait aspect instead of the raw
	 * PNG aspect. This is intentional: community icons such as the 128x128
	 * Tron Bonne portrait are authored square, while CTR draws portraits in a
	 * wider logical rectangle. Preserving the PNG's 1:1 aspect here was the
	 * reason custom faces looked squeezed/narrow compared with retail icons. */
	drawW = MM_CHARACTER_SELECT_NATIVE_ICON_W;
	drawH = MM_CHARACTER_SELECT_NATIVE_ICON_H;
	if (drawW > maxW)
	{
		drawW = maxW;
		drawH = (MM_CHARACTER_SELECT_NATIVE_ICON_H * drawW) /
			MM_CHARACTER_SELECT_NATIVE_ICON_W;
	}
	if (drawH > maxH)
	{
		drawH = maxH;
		drawW = (MM_CHARACTER_SELECT_NATIVE_ICON_W * drawH) /
			MM_CHARACTER_SELECT_NATIVE_ICON_H;
	}
	if (drawW < 1) drawW = 1;
	if (drawH < 1) drawH = 1;

	x = slotRect->x + 2 + (maxW - drawW) / 2;
	y = slotRect->y + 2 + (maxH - drawH) / 2;

	primMem = &gGT->backBuffer->primMem;
	ot = gGT->pushBuffer_UI.ptrOT;
	if ((char *)primMem->cursor + bytes >= (char *)primMem->guardEnd)
		return false;

	packet = (DR_PSYX_TEX_FT4 *)primMem->cursor;
	memset(packet, 0, bytes);
	setlen(packet, (int)(bytes / sizeof(uint32_t)) - 1);
	packet->code[0] = 0xb4000000u |
		((uint32_t)nativeIcon->texture & 0x00ffffffu);
	packet->code[1] = (uint32_t)nativeIcon->width |
		((uint32_t)nativeIcon->height << 16);

	poly = &packet->poly;
	setPolyFT4(poly);
	setRGB0(poly, iconColor.r, iconColor.g, iconColor.b);

	poly->x0 = (s16)x;         poly->y0 = (s16)y;
	poly->x1 = (s16)(x+drawW); poly->y1 = (s16)y;
	poly->x2 = (s16)x;         poly->y2 = (s16)(y+drawH);
	poly->x3 = (s16)(x+drawW); poly->y3 = (s16)(y+drawH);

	poly->u0 = 0; poly->v0 = 0;
	poly->u1 = (u8)(nativeIcon->width - 1); poly->v1 = 0;
	poly->u2 = 0; poly->v2 = (u8)(nativeIcon->height - 1);
	poly->u3 = (u8)(nativeIcon->width - 1);
	poly->v3 = (u8)(nativeIcon->height - 1);
	poly->tpage = 0;
	poly->clut = 0;

	addPrim(ot, packet);
	primMem->cursor = (char *)packet + bytes;
	return true;
}

static b32 MM_Characters_DuplicateSelectionsAllowed(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 storyMask =
		ADVENTURE_MODE | ADVENTURE_BOSS | ADVENTURE_CUP | ADVENTURE_ARENA;

	return (gGT != NULL) && ((gGT->gameMode1 & storyMask) == 0);
}

enum NativeLoadoutInputResult
{
	NATIVE_LOADOUT_INPUT_NONE = 0,
	NATIVE_LOADOUT_INPUT_ACTIVE,
	NATIVE_LOADOUT_INPUT_READY,
};

/* Always show these retail engine sounds, including when zero mod packs exist.
 * Keep this menu independent from PHYSICS and from the selected kart body. */
enum { MM_CHARACTER_ENGINE_PRESET_COUNT = 6 };
static const int s_characterEnginePresets[MM_CHARACTER_ENGINE_PRESET_COUNT] = {
    ENGINE_SOUND_AUTO, ENGINE_SOUND_ACCEL, ENGINE_SOUND_SPEED,
    ENGINE_SOUND_TURN, ENGINE_SOUND_BALANCED, ENGINE_SOUND_RETAIL
};

static const char *MM_Characters_EngineSoundName(int sound)
{
    switch (sound)
    {
    case ENGINE_SOUND_AUTO: return "KART ENGINE";
    case ENGINE_SOUND_ACCEL: return "ACCEL";
    case ENGINE_SOUND_SPEED: return "SPEED";
    case ENGINE_SOUND_TURN: return "TURN";
    case ENGINE_SOUND_BALANCED: return "BALANCED";
    case ENGINE_SOUND_RETAIL: return "CHARACTER RETAIL";
    default: return EngineSoundRegistry_GetName(sound);
    }
}

static int MM_Characters_LoadoutFirstStage(void)
{
	if (RacerLoadout_IsAdventureLocked())
		return RACER_SELECT_READY;

    /* Character -> Physics -> Kart -> Kart Color -> Wheels -> Engine -> Ready.
     * Keep physics independent from which body/kart/wheels were chosen. */
    return RACER_SELECT_PHYSICS;
}

static int MM_Characters_LoadoutPreviousStage(int playerIndex, int stage)
{
    if (stage == RACER_SELECT_READY && !RacerLoadout_IsAdventureLocked())
        return RACER_SELECT_ENGINE;
    if (stage == RACER_SELECT_READY || stage == RACER_SELECT_ENGINE) {
        if (VehiclePartRegistry_GetCount(VEHICLE_PART_WHEELS) > 0 &&
            !VehiclePartRegistry_HidesWheels(
                RacerLoadout_GetEffectiveKart(playerIndex)))
            return RACER_SELECT_WHEELS;
        if (VehiclePartRegistry_GetColorCount(
                RacerLoadout_GetEffectiveKart(playerIndex)) > 1)
            return RACER_SELECT_KART_COLOR;
        if (VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0)
            return RACER_SELECT_KART;
    }
    if (stage == RACER_SELECT_WHEELS) {
        if (VehiclePartRegistry_GetColorCount(
                RacerLoadout_GetEffectiveKart(playerIndex)) > 1)
            return RACER_SELECT_KART_COLOR;
        if (VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0)
            return RACER_SELECT_KART;
    }
    if (stage == RACER_SELECT_KART_COLOR)
        return RACER_SELECT_KART;
    if (stage == RACER_SELECT_PHYSICS)
        return RACER_SELECT_CHARACTER;
    return RACER_SELECT_PHYSICS;
}

static int MM_Characters_LoadoutNextStage(int playerIndex, int stage)
{
    if (stage == RACER_SELECT_PHYSICS) {
        if (VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0)
            return RACER_SELECT_KART;
        if (VehiclePartRegistry_GetCount(VEHICLE_PART_WHEELS) > 0)
            return RACER_SELECT_WHEELS;
    }
    if (stage == RACER_SELECT_KART) {
        int kartID = RacerLoadout_GetEffectiveKart(playerIndex);
        if (VehiclePartRegistry_GetColorCount(kartID) > 1)
            return RACER_SELECT_KART_COLOR;
    }
    if ((stage == RACER_SELECT_KART || stage == RACER_SELECT_KART_COLOR) &&
        VehiclePartRegistry_GetCount(VEHICLE_PART_WHEELS) > 0 &&
        !VehiclePartRegistry_HidesWheels(
            RacerLoadout_GetEffectiveKart(playerIndex)))
        return RACER_SELECT_WHEELS;
    if (stage != RACER_SELECT_ENGINE && !RacerLoadout_IsAdventureLocked())
        return RACER_SELECT_ENGINE;
    return RACER_SELECT_READY;
}

static void MM_Characters_LoadoutCyclePart(int playerIndex, int stage, int direction)
{
	int kind = stage == RACER_SELECT_KART ?
		VEHICLE_PART_KART : VEHICLE_PART_WHEELS;
	int count = VehiclePartRegistry_GetCount(kind);
	int current = stage == RACER_SELECT_KART ?
		RacerLoadout_GetKart(playerIndex) :
		RacerLoadout_GetWheels(playerIndex);
	int slot;
	int next;

	if (count <= 0)
		return;

	/* Body-only custom characters have no embedded/original kart to fall back
	   to, so their kart stage contains only actual registered kart assets. */
	if (stage == RACER_SELECT_KART &&
		CharacterRegistry_IsModularBody(
			RacerLoadout_GetCharacter(playerIndex)))
	{
		int base = current >= 0 ? current : 0;
		next = (base + direction + count) % count;
	}
	else
	{
		/* Legacy/retail loadouts keep ORIGINAL as slot zero. */
		slot = current < 0 ? 0 : current + 1;
		slot = (slot + direction + count + 1) % (count + 1);
		next = slot - 1;
	}

	if (stage == RACER_SELECT_KART)
		RacerLoadout_SetKart(playerIndex, next);
	else
		RacerLoadout_SetWheels(playerIndex, next);

	OtherFX_Play(0, 1);
}

static int MM_Characters_LoadoutHandlePartInput(int playerIndex, u32 button)
{
	int stage = RacerLoadout_GetSelectionPart(playerIndex);

    if (stage != RACER_SELECT_PHYSICS &&
        stage != RACER_SELECT_KART &&
        stage != RACER_SELECT_KART_COLOR &&
        stage != RACER_SELECT_WHEELS &&
        stage != RACER_SELECT_ENGINE)
        return NATIVE_LOADOUT_INPUT_NONE;

    if (stage == RACER_SELECT_PHYSICS)
    {
        int current=RacerLoadout_GetPhysicsClass(playerIndex);
        if (button & BTN_LEFT)
        {
            current--;
            if (current < RACER_PHYSICS_DEFAULT)current=RACER_PHYSICS_LAST;
            RacerLoadout_SetPhysicsClass(playerIndex,current);
            OtherFX_Play(0,1);
        }
        else if (button & BTN_RIGHT)
        {
            current++;
            if (current > RACER_PHYSICS_LAST)current=RACER_PHYSICS_DEFAULT;
            RacerLoadout_SetPhysicsClass(playerIndex,current);
            OtherFX_Play(0,1);
        }

        if (button & MM_CHARACTER_SELECT_INPUT_CONFIRM)
        {
            const int nextStage=MM_Characters_LoadoutNextStage(playerIndex,stage);
            RacerLoadout_SetSelectionPart(playerIndex,nextStage);
            OtherFX_Play(1,1);
            Platform_Log("[PHYSICS SELECT] player=%d class=%s character=%d\n",
                playerIndex,
                RacerLoadout_PhysicsName(RacerLoadout_GetPhysicsClass(playerIndex)),
                RacerLoadout_GetCharacter(playerIndex));
            return nextStage==RACER_SELECT_READY ?
                   NATIVE_LOADOUT_INPUT_READY:NATIVE_LOADOUT_INPUT_ACTIVE;
        }

        if (button & MM_CHARACTER_SELECT_INPUT_BACK)
        {
            RacerLoadout_SetSelectionPart(playerIndex,RACER_SELECT_CHARACTER);
            OtherFX_Play(2,1);
        }
        return NATIVE_LOADOUT_INPUT_ACTIVE;
    }

    if (stage == RACER_SELECT_ENGINE)
    {
        int current = RacerLoadout_GetEngineSound(playerIndex);
        int packs = EngineSoundRegistry_GetCount();
        int total = MM_CHARACTER_ENGINE_PRESET_COUNT + packs;
        if (button & (BTN_LEFT | BTN_RIGHT))
        {
            int slot = 0;
            int direction = (button & BTN_LEFT) ? -1 : 1;
            for (int i = 0; i < MM_CHARACTER_ENGINE_PRESET_COUNT; ++i)
                if (s_characterEnginePresets[i] == current)
                    slot = i;
            if (current >= 0 && current < packs)
                slot = MM_CHARACTER_ENGINE_PRESET_COUNT + current;
            slot = (slot + direction + total) % total;
            RacerLoadout_SetEngineSound(playerIndex,
                slot < MM_CHARACTER_ENGINE_PRESET_COUNT ?
                    s_characterEnginePresets[slot] :
                    slot - MM_CHARACTER_ENGINE_PRESET_COUNT);
            OtherFX_Play(0, 1);
        }
        if (button & MM_CHARACTER_SELECT_INPUT_CONFIRM)
        {
            RacerLoadout_SetSelectionPart(playerIndex, RACER_SELECT_READY);
            OtherFX_Play(1, 1);
            Platform_Log("[ENGINE SELECT] player=%d choice=%d pack=%d retailClass=%d\n",
                  playerIndex, RacerLoadout_GetEngineSound(playerIndex),
                  RacerLoadout_GetEffectiveEngineSound(playerIndex),
                  RacerLoadout_GetEffectiveEngineSoundClass(playerIndex));
            return NATIVE_LOADOUT_INPUT_READY;
        }
        if (button & MM_CHARACTER_SELECT_INPUT_BACK)
        {
            RacerLoadout_SetSelectionPart(playerIndex,
                    MM_Characters_LoadoutPreviousStage(playerIndex, stage));
            OtherFX_Play(2, 1);
        }
        return NATIVE_LOADOUT_INPUT_ACTIVE;
    }

    if (stage == RACER_SELECT_KART_COLOR)
    {
        int kartID = RacerLoadout_GetEffectiveKart(playerIndex);
        int count = VehiclePartRegistry_GetColorCount(kartID);
        int index = RacerLoadout_GetKartColor(playerIndex);
        if (count <= 1)
            return NATIVE_LOADOUT_INPUT_ACTIVE;

        if (button & BTN_LEFT)
        {
            RacerLoadout_SetKartColor(
                playerIndex, (index + count - 1) % count);
            OtherFX_Play(0, 1);
        }
        else if (button & BTN_RIGHT)
        {
            RacerLoadout_SetKartColor(
                playerIndex, (index + 1) % count);
            OtherFX_Play(0, 1);
        }

        if (button & MM_CHARACTER_SELECT_INPUT_CONFIRM)
        {
            int nextStage =
                MM_Characters_LoadoutNextStage(playerIndex, stage);
            RacerLoadout_SetSelectionPart(playerIndex, nextStage);
            OtherFX_Play(1, 1);
            return nextStage == RACER_SELECT_READY ?
                NATIVE_LOADOUT_INPUT_READY :
                NATIVE_LOADOUT_INPUT_ACTIVE;
        }
        if (button & MM_CHARACTER_SELECT_INPUT_BACK)
        {
            RacerLoadout_SetSelectionPart(
                playerIndex,
                MM_Characters_LoadoutPreviousStage(playerIndex, stage));
            OtherFX_Play(2, 1);
        }
        return NATIVE_LOADOUT_INPUT_ACTIVE;
    }

    if ((button & BTN_LEFT) != 0)
		MM_Characters_LoadoutCyclePart(playerIndex, stage, -1);
	else if ((button & BTN_RIGHT) != 0)
		MM_Characters_LoadoutCyclePart(playerIndex, stage, 1);

	if ((button & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0)
	{
		int nextStage = MM_Characters_LoadoutNextStage(playerIndex, stage);
		RacerLoadout_SetSelectionPart(playerIndex, nextStage);
		OtherFX_Play(1, 1);
		return nextStage == RACER_SELECT_READY ?
			NATIVE_LOADOUT_INPUT_READY :
			NATIVE_LOADOUT_INPUT_ACTIVE;
	}

	if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
	{
		RacerLoadout_SetSelectionPart(
			playerIndex,
			MM_Characters_LoadoutPreviousStage(playerIndex, stage));
		OtherFX_Play(2, 1);
	}

	return NATIVE_LOADOUT_INPUT_ACTIVE;
}

static b32 MM_Characters_LoadoutConfirmCharacter(int playerIndex, int characterID)
{
	int nextStage;
	const char *defaultKartSlug;
	int defaultKartID;

	RacerLoadout_SetCharacter(playerIndex, characterID);
    RacerLoadout_SetPhysicsClass(playerIndex,RACER_PHYSICS_DEFAULT);
    RacerLoadout_SetEngineSound(playerIndex,ENGINE_SOUND_AUTO);

	defaultKartSlug = CharacterRegistry_GetKartSlug(characterID);
	defaultKartID = defaultKartSlug ?
		VehiclePartRegistry_GetIDBySlug(VEHICLE_PART_KART, defaultKartSlug) : -1;

	if (defaultKartID >= 0)
	{
		/* character.ini may provide an initial kart, but the player can still
		   change it in the independent kart-selection stage. */
		RacerLoadout_SetKart(playerIndex, defaultKartID);
	}
	else if (CharacterRegistry_IsModularBody(characterID) &&
		VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0)
	{
		/* A body-only character requires a separate kart. Start on the first
		   registered kart instead of presenting an invalid ORIGINAL slot. */
		RacerLoadout_SetKart(playerIndex, 0);
	}
	else
	{
		RacerLoadout_SetKart(playerIndex, RACER_LOADOUT_ORIGINAL);
	}

	RacerLoadout_SetWheels(playerIndex, RACER_LOADOUT_ORIGINAL);
	nextStage = MM_Characters_LoadoutFirstStage();
	RacerLoadout_SetSelectionPart(playerIndex, nextStage);

	return nextStage == RACER_SELECT_READY;
}

static void MM_Characters_LoadoutUndoReady(int playerIndex)
{
	RacerLoadout_SetSelectionPart(
		playerIndex,
		MM_Characters_LoadoutPreviousStage(playerIndex, RACER_SELECT_READY));
}

static void MM_Characters_LoadoutMarkReady(int playerIndex, int playerCount)
{
	u16 readyMask = (u16)((1U << playerCount) - 1U);

	sdata->characterSelectFlags |= (u16)(1U << playerIndex);

	if ((sdata->characterSelectFlags & readyMask) == readyMask)
	{
		D230.characterSelectExitsForward = 1;
		D230.characterSelectMenuState = EXITING_MENU;
	}
}

static const char *MM_Characters_LoadoutPartName(int playerIndex, int stage)
{
	int kind;
	int partID;

	if (stage == RACER_SELECT_KART)
	{
		kind = VEHICLE_PART_KART;
		partID = RacerLoadout_GetKart(playerIndex);
	}
	else
	{
		kind = VEHICLE_PART_WHEELS;
		partID = RacerLoadout_GetWheels(playerIndex);
	}

	if (partID < 0)
		return "ORIGINAL";

	return VehiclePartRegistry_GetDisplayName(kind, partID);
}

static void MM_Characters_LoadoutDrawStatus(int playerIndex,
	int centerX, int y, int maxNameChars)
{
	char line[96];
	int stage = RacerLoadout_GetSelectionPart(playerIndex);
    /* Selectors must never overflow even the 3P/4P preview windows. */
    const int maxTextWidth = D230.characterSelectWindowWidth > 50
                            ? D230.characterSelectWindowWidth - 12 : 140;

	if (stage == RACER_SELECT_CHARACTER)
		return;

	if (stage == RACER_SELECT_READY)
	{
		snprintf(line, sizeof(line), "READY");
	}
    else if (stage == RACER_SELECT_PHYSICS)
    {
        const int choice=RacerLoadout_GetPhysicsClass(playerIndex);
        const char *label=RacerLoadout_PhysicsName(choice);
        if (maxNameChars<=10)
        {
            /* 3P/4P: leave adequate space inside compact character previews. */
#if defined(CTR_CNK_PHYSICS_PREVIEW_QA)
            if (choice == RACER_PHYSICS_CNK_PREVIEW)label="CNK";
            else
#endif
            if (choice == RACER_PHYSICS_MAX)label="MAX";
            else if (choice == RACER_PHYSICS_BALANCED)label="BAL";
            else if (choice == RACER_PHYSICS_DEFAULT)label="AUTO";
            else if (choice == RACER_PHYSICS_ACCEL)label="ACC";
            else if (choice == RACER_PHYSICS_SPEED)label="SPD";
            snprintf(line,sizeof(line),"%s < %s >",NativeLocalization_Translate("PHYS"),
                      NativeLocalization_Translate(label));
        }
        else if (choice == RACER_PHYSICS_DEFAULT)
        {
            int base=CharacterRegistry_GetEngineClass(
                    RacerLoadout_GetCharacter(playerIndex));
            const char *baseName = base == BALANCED
                ? "BAL" : RacerLoadout_PhysicsName(base);
            snprintf(line,sizeof(line),"%s < %s:%s >",NativeLocalization_Translate("PHYS"),
                      NativeLocalization_Translate("DEFAULT"),
                      NativeLocalization_Translate(baseName));
        }
        else
            snprintf(line,sizeof(line),"%s < %s >",NativeLocalization_Translate("PHYSICS"),
                      NativeLocalization_Translate(label));
    }
    else if (stage == RACER_SELECT_KART_COLOR)
    {
        int kartID = RacerLoadout_GetEffectiveKart(playerIndex);
        const char *name = VehiclePartRegistry_GetColorName(
            kartID, RacerLoadout_GetKartColor(playerIndex));
        int visibleChars=maxNameChars;
        const char *displayName=name?name:"DEFAULT";
        do
        {
            snprintf(line, sizeof(line), "COLOR < %.*s >",
                     visibleChars, displayName);
            if (visibleChars <= 2 ||
                DecalFont_GetLineWidth(line,FONT_SMALL)*80 <= maxTextWidth*100)
                break;
            --visibleChars;
        } while (1);
    }
    else if (stage == RACER_SELECT_ENGINE)
    {
        int choice = RacerLoadout_GetEngineSound(playerIndex);
        const char *name = MM_Characters_EngineSoundName(choice);
        int visibleChars = maxNameChars;
        do {
            snprintf(line, sizeof(line), "ENGINE < %.*s >", visibleChars, name);
            if (visibleChars <= 2 ||
                DecalFont_GetLineWidth(line,FONT_SMALL)*80 <= maxTextWidth*100)
                break;
            --visibleChars;
        } while (1);
    }
    else
    {
        const char *kind = stage == RACER_SELECT_KART ? "KART" : "WHEELS";
        const char *name = MM_Characters_LoadoutPartName(playerIndex, stage);
        int visibleChars=maxNameChars;
        const char *displayName=name?name:"ORIGINAL";
        do
        {
            snprintf(line, sizeof(line), "%s < %.*s >",
                      NativeLocalization_Translate(kind), visibleChars, displayName);
            if (visibleChars <= 2 ||
                DecalFont_GetLineWidth(line,FONT_SMALL)*80 <= maxTextWidth*100)
                break;
            --visibleChars;
        } while (1);
    }

    /* Small orange (CTR font) selectors: physics/kart/color/wheels/engine/ready.
     * Only this caption is scaled; names and the rest of the menu are intact. */
    DecalFont_DrawLineNativeScaled(line, centerX, y + 1,
                                  FONT_SMALL, JUSTIFY_CENTER | ORANGE, 80);
}

static b32 MM_Characters_RosterCharacterUnlocked(int characterID)
{
	/* Mod characters are controlled by their own enabled flag/config. */
	if (characterID > NITROS_OXIDE)
		return CharacterRegistry_GetByID(characterID) != NULL;

	for (int i = 0; i < MM_CHARACTER_SELECT_ICON_COUNT; i++)
	{
		const struct CharacterSelectMeta *meta = &D230.characterSelectMeta1P2P[i];
		if (meta->characterID != characterID) continue;
		s16 unlockFlags = (s16)meta->unlockFlags;
		return (unlockFlags == MM_CHARACTER_UNLOCK_ALWAYS) ||
			CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlockFlags);
	}
	return false;
}

static int MM_Characters_RosterGetCount(void)
{
	int count = 0;
	int registryCount = CharacterRegistry_GetCount();

	for (int rosterIndex = 0; rosterIndex < registryCount; rosterIndex++)
	{
		const struct CharacterDef *character =
			CharacterRegistry_GetByRosterIndex(rosterIndex);
		if ((character != NULL) &&
			MM_Characters_RosterCharacterUnlocked(character->id))
		{
			count++;
		}
	}

	return count;
}

static int MM_Characters_RosterGetPageCount(void)
{
	int count = MM_Characters_RosterGetCount();
	return count > 0 ?
		(count + MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE - 1) /
		MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE : 1;
}

static int MM_Characters_RosterGetCharacterIDByIndex(int visibleIndex)
{
	if (visibleIndex < 0) return -1;

	int index = 0;
	int registryCount = CharacterRegistry_GetCount();
	for (int rosterIndex = 0; rosterIndex < registryCount; rosterIndex++)
	{
		const struct CharacterDef *character =
			CharacterRegistry_GetByRosterIndex(rosterIndex);
		if (character == NULL) continue;
		if (!MM_Characters_RosterCharacterUnlocked(character->id)) continue;
		if (index == visibleIndex) return character->id;
		index++;
	}
	return -1;
}

static int MM_Characters_RosterFindVisibleIndex(int characterID)
{
	int index = 0;
	int registryCount = CharacterRegistry_GetCount();
	for (int rosterIndex = 0; rosterIndex < registryCount; rosterIndex++)
	{
		const struct CharacterDef *candidate =
			CharacterRegistry_GetByRosterIndex(rosterIndex);
		if (candidate == NULL) continue;
		if (!MM_Characters_RosterCharacterUnlocked(candidate->id)) continue;
		if (candidate->id == characterID) return index;
		index++;
	}
	return -1;
}

static int MM_Characters_RosterGetPageCharacterCount(int page)
{
	int remaining = MM_Characters_RosterGetCount() -
		page * MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
	if (remaining <= 0) return 0;
	return remaining > MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE ?
		MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE : remaining;
}

static int MM_Characters_Roster4PGetPageCharacterCount(int page)
{
	int remaining = MM_Characters_RosterGetCount() -
		page * MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE;
	if (remaining <= 0) return 0;
	return remaining > MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE ?
		MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE : remaining;
}

static int MM_Characters_RosterGetFallbackRetailID(int characterID)
{
	if ((characterID >= CRASH_BANDICOOT) &&
		(characterID <= NITROS_OXIDE))
	{
		return characterID;
	}

	int fallback = CharacterRegistry_GetFallbackRetailID(characterID);
	if ((fallback < CRASH_BANDICOOT) || (fallback > NITROS_OXIDE))
		fallback = CRASH_BANDICOOT;
	return fallback;
}

static struct Icon *MM_Characters_RosterGetIcon(
	struct GameTracker *gGT,
	int characterID)
{
	int iconCharacterID = characterID;
	if (characterID > NITROS_OXIDE)
	{
		iconCharacterID =
			CharacterRegistry_GetIconReuseCharacterID(characterID);
		if ((iconCharacterID < CRASH_BANDICOOT) ||
			(iconCharacterID > NITROS_OXIDE))
		{
			iconCharacterID =
				MM_Characters_RosterGetFallbackRetailID(characterID);
		}
	}

	return gGT->ptrIcons[
		data.MetaDataCharacters[iconCharacterID].iconID];
}

static void MM_Characters_RosterDrawIcon(
	struct GameTracker *gGT,
	int characterID,
	const RECT *slotRect,
	Color iconColor)
{
	struct Icon *icon;

	if (MM_Characters_RosterDrawNativeIcon(
		gGT, characterID, slotRect, iconColor))
	{
		return;
	}

	icon = MM_Characters_RosterGetIcon(gGT, characterID);
	if (icon == NULL)
		return;

	RECTMENU_DrawPolyGT4(
		icon,
		slotRect->x + 2,
		slotRect->y + 2,
		&gGT->backBuffer->primMem,
		gGT->pushBuffer_UI.ptrOT,
		iconColor.self,
		iconColor.self,
		iconColor.self,
		iconColor.self,
		TRANS_50_DECAL,
		FP(1.0));
}

static const char *MM_Characters_RosterGetDisplayName(int characterID)
{
	if (characterID > NITROS_OXIDE)
		return CharacterRegistry_GetDisplayName(characterID);

	if ((characterID >= CRASH_BANDICOOT) &&
		(characterID <= NITROS_OXIDE))
	{
		return sdata->lngStrings[
			data.MetaDataCharacters[characterID].name_LNG_long];
	}

	return "CRASH BANDICOOT";
}

static const char *MM_Characters_RosterGetPreviewModelName(int characterID)
{
	if (characterID > NITROS_OXIDE)
		return CharacterRegistry_GetAssetName(characterID);

	int modelCharacterID =
		MM_Characters_RosterGetFallbackRetailID(characterID);

	return data.MetaDataCharacters[modelCharacterID].name_Debug;
}

static int MM_Characters_RosterGetSelectedCharacterID(void)
{
	return MM_Characters_RosterGetCharacterIDByIndex(
		s_characterPage * MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
		s_characterCursor);
}

static void MM_Characters_RosterApplySelection(int moveDirection)
{
	int characterID = MM_Characters_RosterGetSelectedCharacterID();
	if (characterID < 0) return;
	RacerLoadout_SetCharacter(0, characterID);
	D230.characterSelectPlayerState.modelMoveDir[0] = (s16)moveDirection;
}

static void MM_Characters_RosterClampCursor(void)
{
	if (s_characterPage < 0) s_characterPage = 0;
	int pageTotal = MM_Characters_RosterGetPageCount();
	if (s_characterPage >= pageTotal)
		s_characterPage = (s16)(pageTotal - 1);

	int pageCount = MM_Characters_RosterGetPageCharacterCount(s_characterPage);
	if (pageCount <= 0)
	{
		s_characterCursor = 0;
		return;
	}
	if (s_characterCursor >= pageCount) s_characterCursor = (s16)(pageCount - 1);
	if (s_characterCursor < 0) s_characterCursor = 0;
}

static void MM_Characters_RosterChangePage(int direction)
{
	int pageTotal = MM_Characters_RosterGetPageCount();
	s_characterPage = (s16)((s_characterPage + direction + pageTotal) % pageTotal);
	s_characterCursor = 0;
	MM_Characters_RosterClampCursor();

	if (MM_Characters_RosterGetPageCharacterCount(s_characterPage) > 0)
		MM_Characters_RosterApplySelection(direction > 0 ?
			MM_CHARACTER_SELECT_MODEL_MOVE_NEXT :
			MM_CHARACTER_SELECT_MODEL_MOVE_PREV);

	OtherFX_Play(0, 1);
}

static void MM_Characters_RosterMoveCursor(u32 button)
{
	int pageCount = MM_Characters_RosterGetPageCharacterCount(s_characterPage);
	if (pageCount <= 0) return;

	int oldCursor = s_characterCursor;
	int cursor = oldCursor;
	int column = cursor % MM_CHARACTER_SELECT_ROSTER_COLUMNS;

	if ((button & BTN_RIGHT) != 0)
	{
		if ((column + 1 < MM_CHARACTER_SELECT_ROSTER_COLUMNS) &&
			(cursor + 1 < pageCount)) cursor++;
	}
	else if ((button & BTN_LEFT) != 0)
	{
		if (column > 0) cursor--;
	}
	else if ((button & BTN_DOWN) != 0)
	{
		int candidate = cursor + MM_CHARACTER_SELECT_ROSTER_COLUMNS;
		if (candidate < pageCount) cursor = candidate;
	}
	else if ((button & BTN_UP) != 0)
	{
		int candidate = cursor - MM_CHARACTER_SELECT_ROSTER_COLUMNS;
		if (candidate >= 0) cursor = candidate;
	}

	if (cursor == oldCursor) return;

	s_characterCursor = (s16)cursor;
	MM_Characters_RosterApplySelection(cursor > oldCursor ?
		MM_CHARACTER_SELECT_MODEL_MOVE_NEXT :
		MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
	OtherFX_Play(0, 1);
}

static void MM_Characters_RosterRestoreSelection(void)
{
	s_characterPage = 0;
	s_characterCursor = 0;

	int currentCharacterID = data.characterIDs[0];
	int visibleIndex = MM_Characters_RosterFindVisibleIndex(currentCharacterID);
	if (visibleIndex >= 0)
	{
		s_characterPage = (s16)(visibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE);
		s_characterCursor = (s16)(visibleIndex % MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE);
	}
	MM_Characters_RosterClampCursor();
}


static void MM_Characters_HangoutResetEnginePreview(void)
{
	s_hangoutPreviewEngineChoice = 0x7fffffff;
	s_hangoutPreviewEnginePack = 0x7fffffff;
	s_hangoutPreviewEngineClass = 0x7fffffff;
	s_hangoutPreviewEngineKart = 0x7fffffff;
}

static void MM_Characters_HangoutFreezeDriver(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;

	if (!s_hangoutCustomizationSnapshot.valid ||
	    !NativeHangout_IsCustomizationOpen() ||
	    gGT == NULL ||
	    gGT->drivers[0] == NULL)
		return;

	d = gGT->drivers[0];

	/* The world keeps running in Hangout. Pin only the local racer to the exact
	 * pose it had when Customization opened, while the same Instance is borrowed
	 * by the menu renderer for the rotating preview. */
	d->posCurr = s_hangoutCustomizationSnapshot.posCurr;
	d->posPrev = s_hangoutCustomizationSnapshot.posPrev;
	d->rotCurr = s_hangoutCustomizationSnapshot.rotCurr;
	d->rotPrev = s_hangoutCustomizationSnapshot.rotPrev;
	memset(&d->velocity, 0, sizeof(d->velocity));
	d->speed = 0;
	d->speedApprox = 0;
	d->fireSpeed = 0;
	d->xSpeed = 0;
	d->ySpeed = 0;
	d->zSpeed = 0;
	d->kartState = KS_NORMAL;
	d->wheelRotation = 0;
	/* EngineSound_Player treats an airborne zero-speed kart as free-revving.
	 * Mark this temporary preview state grounded so it stays at true idle. */
	d->actionsFlagSetPrevFrame |= ACTION_TOUCH_GROUND;
}

static void MM_Characters_HangoutStopRetailEngines(struct Driver *d)
{
	int engineClass;

	if (d == NULL)
		return;

	for (engineClass = 0; engineClass < NUM_CLASSES; engineClass++)
		EngineAudio_Stop((engineClass * 4) + d->driverID);
}

static void MM_Characters_HangoutUpdateEnginePreview(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;
	int choice;
	int pack;
	int engineClass;
	int kart;

	if (!s_hangoutCustomizationSnapshot.valid ||
	    !NativeHangout_IsCustomizationOpen() ||
	    gGT == NULL ||
	    gGT->drivers[0] == NULL)
		return;

	if (RacerLoadout_GetSelectionPart(0) != RACER_SELECT_ENGINE)
	{
		MM_Characters_HangoutResetEnginePreview();
		return;
	}

	d = gGT->drivers[0];
	choice = RacerLoadout_GetEngineSound(0);
	pack = RacerLoadout_GetEffectiveEngineSound(0);
	engineClass = RacerLoadout_GetEffectiveEngineSoundClass(0);
	kart = RacerLoadout_GetEffectiveKart(0);

	if (choice == s_hangoutPreviewEngineChoice &&
	    pack == s_hangoutPreviewEnginePack &&
	    engineClass == s_hangoutPreviewEngineClass &&
	    kart == s_hangoutPreviewEngineKart)
		return;

	MM_Characters_HangoutStopRetailEngines(d);
	VehBirth_EngineAudio_AllPlayers();

	/* Start every newly highlighted engine from stationary RPM. The ordinary
	 * player-audio tick keeps updating it after this one immediate preview. */
	d->engineSoundMode = ENGINE_SOUND_DYNAMIC;
	d->engineSoundVolumeState = 0;
	d->engineSoundPitchState = 0;
	EngineSound_Player(d);

	s_hangoutPreviewEngineChoice = choice;
	s_hangoutPreviewEnginePack = pack;
	s_hangoutPreviewEngineClass = engineClass;
	s_hangoutPreviewEngineKart = kart;

	Platform_Log(
		"[HANGOUT CUSTOMIZATION] idle engine preview choice=%d pack=%d class=%d kart=%d\n",
		choice, pack, engineClass, kart);
}

static void MM_Characters_HangoutPreviewTick(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;

	MM_Characters_HangoutFreezeDriver();

	if (!s_hangoutCustomizationSnapshot.valid ||
	    !NativeHangout_IsCustomizationOpen() ||
	    gGT == NULL ||
	    gGT->drivers[0] == NULL)
		return;

	d = gGT->drivers[0];

	/* This calls the exact gameplay exhaust path, including mod exhaust mounts. */
	VehEmitter_PreviewExhaust(d);
	MM_Characters_HangoutUpdateEnginePreview();
}

static void MM_Characters_HangoutRestoreLoadout(void)
{
	const struct NativeRacerLoadout *saved =
		&s_hangoutCustomizationSnapshot.loadout;

	RacerLoadout_SetCharacter(0, saved->characterID);
	RacerLoadout_SetKart(0, saved->kartID);
	RacerLoadout_SetKartColor(0, saved->kartColor);
	RacerLoadout_SetWheels(0, saved->wheelID);
	RacerLoadout_SetPhysicsClass(0, saved->physicsClass);
	RacerLoadout_SetEngineSound(0, saved->engineSoundID);
	RacerLoadout_SetSelectionPart(0, saved->selectionPart);
}

void MM_Characters_FinishHangoutCustomization(int apply)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;
	struct Instance *inst;
	struct InstDrawPerPlayer *idpp;
	struct Model *model;
	int i;

	if (!s_hangoutCustomizationSnapshot.valid ||
	    gGT == NULL ||
	    gGT->drivers[0] == NULL ||
	    gGT->drivers[0]->instSelf == NULL)
	{
		NativeHangout_SetCustomizationOpen(0);
		NativeHangout_SetMenuOpen(0);
		return;
	}

	d = gGT->drivers[0];
	inst = d->instSelf;

	/* Stop the selector immediately before restoring any live-world state.
	 * RECTMENU_Hide() alone only marks NEEDS_TO_CLOSE; RECTMENU_ProcessState()
	 * would still run MM_Characters_MenuProc() once more later this frame and
	 * overwrite the restored Hangout viewport/camera with preview values. */
	if (sdata->ptrActiveMenu == &D230.menuCharacterSelect)
	{
		RECTMENU_Hide(&D230.menuCharacterSelect);
		sdata->ptrActiveMenu = NULL;
	}
	sdata->ptrDesiredMenu = NULL;

	if (!apply)
		MM_Characters_HangoutRestoreLoadout();
	else
		RacerLoadout_SetSelectionPart(0, RACER_SELECT_CHARACTER);

	/* Recalculate the live driver's physics constants from the final selection
	 * without respawning or reloading N. Sanity Beach. */
	VehBirth_SetConsts(d);

	gGT->numPlyrNextGame =
		(u8)s_hangoutCustomizationSnapshot.numPlyrNextGame;
	gGT->renderFlags = s_hangoutCustomizationSnapshot.renderFlags;

	gGT->pushBuffer[0].rect = s_hangoutCustomizationSnapshot.pushBufferRect;
	gGT->pushBuffer[0].pos = s_hangoutCustomizationSnapshot.pushBufferPos;
	gGT->pushBuffer[0].rot = s_hangoutCustomizationSnapshot.pushBufferRot;
	gGT->pushBuffer[0].distanceToScreen_PREV =
		s_hangoutCustomizationSnapshot.distanceToScreenPrev;
	gGT->pushBuffer[0].distanceToScreen_CURR =
		s_hangoutCustomizationSnapshot.distanceToScreenCurr;
	gGT->cameraDC[0].cameraMode =
		s_hangoutCustomizationSnapshot.cameraMode;

	inst->matrix = s_hangoutCustomizationSnapshot.instMatrix;
	inst->flags = s_hangoutCustomizationSnapshot.instFlags;
	inst->animFrame = s_hangoutCustomizationSnapshot.instAnimFrame;
	inst->vertSplit = s_hangoutCustomizationSnapshot.instVertSplit;

	idpp = INST_GETIDPP(inst);
	for (i = 0; i < 4; i++)
		idpp[i].pushBuffer =
			s_hangoutCustomizationSnapshot.idppPushBuffer[i];

	if (apply)
	{
		model = NativeRacerVisual_ResolveModel(data.characterIDs[0]);
		inst->model = model;
		if ((NativeObj_GetMesh(model) != NULL) &&
		    !CharacterRegistry_IsModularBody(data.characterIDs[0]) &&
		    !CharacterRegistry_HasWheels(data.characterIDs[0]))
			d->wheelSize = 0;
		else
			d->wheelSize = 0xccc;
	}
	else
	{
		inst->model = s_hangoutCustomizationSnapshot.instModel;
		d->wheelSize = s_hangoutCustomizationSnapshot.wheelSize;
	}

	/* Return the racer to the exact world state it had on entry. No distance is
	 * gained while browsing, but pre-existing momentum resumes naturally. */
	d->posCurr = s_hangoutCustomizationSnapshot.posCurr;
	d->posPrev = s_hangoutCustomizationSnapshot.posPrev;
	d->rotCurr = s_hangoutCustomizationSnapshot.rotCurr;
	d->rotPrev = s_hangoutCustomizationSnapshot.rotPrev;
	d->velocity = s_hangoutCustomizationSnapshot.velocity;
	d->speed = s_hangoutCustomizationSnapshot.speed;
	d->speedApprox = s_hangoutCustomizationSnapshot.speedApprox;
	d->fireSpeed = s_hangoutCustomizationSnapshot.fireSpeed;
	d->xSpeed = s_hangoutCustomizationSnapshot.xSpeed;
	d->ySpeed = s_hangoutCustomizationSnapshot.ySpeed;
	d->zSpeed = s_hangoutCustomizationSnapshot.zSpeed;
	d->kartState = s_hangoutCustomizationSnapshot.kartState;
	d->wheelRotation = s_hangoutCustomizationSnapshot.wheelRotation;
	d->actionsFlagSet = s_hangoutCustomizationSnapshot.actionsFlagSet;
	d->actionsFlagSetPrevFrame =
		s_hangoutCustomizationSnapshot.actionsFlagSetPrevFrame;

	if (apply)
	{
		d->engineSoundMode = ENGINE_SOUND_DYNAMIC;
		d->engineSoundVolumeState = 0;
		d->engineSoundPitchState = 0;
	}
	else
	{
		d->engineSoundMode =
			s_hangoutCustomizationSnapshot.engineSoundMode;
		d->engineSoundVolumeState =
			s_hangoutCustomizationSnapshot.engineSoundVolumeState;
		d->engineSoundPitchState =
			s_hangoutCustomizationSnapshot.engineSoundPitchState;
	}

	/* Drop whichever preview channel was active and bind the final selection. */
	MM_Characters_HangoutStopRetailEngines(d);
	VehBirth_EngineAudio_AllPlayers();

	sdata->characterSelectFlags = 0;
	RECTMENU_ClearInput();
	gGT->cooldownFromUnpauseUntilPause = 5;

	NativeHangout_SetCustomizationOpen(0);
	NativeHangout_SetMenuOpen(0);

	/* A player who was already spawned gets the short customization-complete
	 * cue. If customization is being used to reconnect a disconnected player,
	 * skip magtran2 so the spawn/join cue (gatwarp2/gatwarp3) remains the only
	 * connection sound. */
	if (apply && !NativeHangout_IsPlayerDisconnected())
		NativeHangout_PlayCustomizationDoneSfx();

	NativeHangout_FinishDisconnectedCustomization(apply);

	Platform_Log(
		"[HANGOUT CUSTOMIZATION] %s character=%d kart=%d wheels=%d physics=%d engine=%d\n",
		apply ? "apply" : "cancel",
		data.characterIDs[0],
		RacerLoadout_GetEffectiveKart(0),
		RacerLoadout_GetEffectiveWheels(0),
		RacerLoadout_GetPhysicsClass(0),
		RacerLoadout_GetEngineSound(0));

	memset(&s_hangoutCustomizationSnapshot, 0,
	       sizeof(s_hangoutCustomizationSnapshot));
	MM_Characters_HangoutResetEnginePreview();
}

void MM_Characters_BeginHangoutCustomization(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d;
	struct Instance *inst;
	struct InstDrawPerPlayer *idpp;
	const struct NativeRacerLoadout *loadout;
	int i;

	if (!NativeHangout_IsActive() ||
	    NativeHangout_IsCustomizationOpen() ||
	    gGT == NULL ||
	    gGT->drivers[0] == NULL ||
	    gGT->drivers[0]->instSelf == NULL)
		return;

	d = gGT->drivers[0];
	inst = d->instSelf;
	loadout = RacerLoadout_Get(0);
	if (loadout == NULL)
		return;

	NativeHangout_PrepareDisconnectedCustomization();

	memset(&s_hangoutCustomizationSnapshot, 0,
	       sizeof(s_hangoutCustomizationSnapshot));
	s_hangoutCustomizationSnapshot.valid = 1;
	s_hangoutCustomizationSnapshot.loadout = *loadout;
	s_hangoutCustomizationSnapshot.numPlyrNextGame = gGT->numPlyrNextGame;
	s_hangoutCustomizationSnapshot.renderFlags = gGT->renderFlags;
	s_hangoutCustomizationSnapshot.pushBufferRect = gGT->pushBuffer[0].rect;
	s_hangoutCustomizationSnapshot.pushBufferPos = gGT->pushBuffer[0].pos;
	s_hangoutCustomizationSnapshot.pushBufferRot = gGT->pushBuffer[0].rot;
	s_hangoutCustomizationSnapshot.distanceToScreenPrev =
		gGT->pushBuffer[0].distanceToScreen_PREV;
	s_hangoutCustomizationSnapshot.distanceToScreenCurr =
		gGT->pushBuffer[0].distanceToScreen_CURR;
	s_hangoutCustomizationSnapshot.cameraMode = gGT->cameraDC[0].cameraMode;
	s_hangoutCustomizationSnapshot.instMatrix = inst->matrix;
	s_hangoutCustomizationSnapshot.instModel = inst->model;
	s_hangoutCustomizationSnapshot.instFlags = inst->flags;
	s_hangoutCustomizationSnapshot.instAnimFrame = inst->animFrame;
	s_hangoutCustomizationSnapshot.instVertSplit = inst->vertSplit;
	s_hangoutCustomizationSnapshot.wheelSize = d->wheelSize;

	idpp = INST_GETIDPP(inst);
	for (i = 0; i < 4; i++)
		s_hangoutCustomizationSnapshot.idppPushBuffer[i] =
			idpp[i].pushBuffer;

	s_hangoutCustomizationSnapshot.posCurr = d->posCurr;
	s_hangoutCustomizationSnapshot.posPrev = d->posPrev;
	s_hangoutCustomizationSnapshot.rotCurr = d->rotCurr;
	s_hangoutCustomizationSnapshot.rotPrev = d->rotPrev;
	s_hangoutCustomizationSnapshot.velocity = d->velocity;
	s_hangoutCustomizationSnapshot.speed = d->speed;
	s_hangoutCustomizationSnapshot.speedApprox = d->speedApprox;
	s_hangoutCustomizationSnapshot.fireSpeed = d->fireSpeed;
	s_hangoutCustomizationSnapshot.xSpeed = d->xSpeed;
	s_hangoutCustomizationSnapshot.ySpeed = d->ySpeed;
	s_hangoutCustomizationSnapshot.zSpeed = d->zSpeed;
	s_hangoutCustomizationSnapshot.kartState = d->kartState;
	s_hangoutCustomizationSnapshot.wheelRotation = d->wheelRotation;
	s_hangoutCustomizationSnapshot.actionsFlagSet = d->actionsFlagSet;
	s_hangoutCustomizationSnapshot.actionsFlagSetPrevFrame =
		d->actionsFlagSetPrevFrame;
	s_hangoutCustomizationSnapshot.engineSoundMode = d->engineSoundMode;
	s_hangoutCustomizationSnapshot.engineSoundVolumeState =
		d->engineSoundVolumeState;
	s_hangoutCustomizationSnapshot.engineSoundPitchState =
		d->engineSoundPitchState;

	VehiclePartRegistry_Init();
	gGT->numPlyrNextGame = 1;
	sdata->characterSelectFlags = 0;
	D230.characterSelectTransitionFrame = MM_CHARACTER_SELECT_TRANSITION_FRAMES;
	D230.characterSelectMenuState = ENTERING_MENU;
	D230.characterSelectExitsForward = 0;

	RacerLoadout_SetSelectionPart(0, RACER_SELECT_CHARACTER);
	MM_Characters_SetMenuLayout();
	MM_Characters_RosterRestoreSelection();

	D230.characterSelectPlayerState.currentCharacterID[0] =
		data.characterIDs[0];
	D230.characterSelectPlayerState.desiredCharacterID[0] =
		data.characterIDs[0];
	D230.characterSelectModelMoveTimer[0] = 0;
	D230.characterSelectPlayerState.angle[0] =
		MM_CHARACTER_SELECT_ANGLE_OFFSET;

	MM_Characters_HangoutResetEnginePreview();
	NativeHangout_SetCustomizationOpen(1);
	RECTMENU_ClearInput();
	sdata->buttonTapPerPlayer[0] = 0;

	/* This path comes from an already-closed Hangout pause menu. Retail direct
	 * character-select jumps activate the menu immediately rather than routing
	 * through ptrDesiredMenu, whose promotion only runs when another menu is
	 * already active. */
	sdata->ptrDesiredMenu = NULL;
	RECTMENU_Show(&D230.menuCharacterSelect);
	D230.menuCharacterSelect.state &= ~ONLY_DRAW_TITLE;

	Platform_Log(
		"[HANGOUT CUSTOMIZATION] open character=%d kart=%d wheels=%d physics=%d engine=%d\n",
		data.characterIDs[0],
		RacerLoadout_GetEffectiveKart(0),
		RacerLoadout_GetEffectiveWheels(0),
		RacerLoadout_GetPhysicsClass(0),
		RacerLoadout_GetEngineSound(0));
}

static void MM_Characters_Roster2PRestoreSelection(void)
{
	int firstVisibleIndex =
		MM_Characters_RosterFindVisibleIndex(data.characterIDs[0]);
	s_characterPage = (s16)(firstVisibleIndex >= 0 ?
		(firstVisibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) : 0);

	for (int playerIndex = 0; playerIndex < 2; playerIndex++)
	{
		int visibleIndex =
			MM_Characters_RosterFindVisibleIndex(
				data.characterIDs[playerIndex]);

		if ((visibleIndex >= 0) &&
			((visibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) ==
				s_characterPage))
		{
			s_characterCursor2P[playerIndex] =
				(s16)(visibleIndex % MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE);
		}
		else
		{
			s_characterCursor2P[playerIndex] = (s16)playerIndex;
		}
	}

	if (!MM_Characters_DuplicateSelectionsAllowed() &&
		(s_characterCursor2P[0] == s_characterCursor2P[1]))
	{
		s_characterCursor2P[1] =
			(s16)((s_characterCursor2P[0] + 1) %
				MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE);
	}
}

static int MM_Characters_Roster2PGetSelectedCharacterID(int playerIndex)
{
	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	if ((playerIndex < 0) || (playerIndex >= 2) ||
		(s_characterCursor2P[playerIndex] < 0) ||
		(s_characterCursor2P[playerIndex] >= pageCount))
	{
		return -1;
	}

	return MM_Characters_RosterGetCharacterIDByIndex(
		s_characterPage * MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
		s_characterCursor2P[playerIndex]);
}

static void MM_Characters_Roster2PApplySelection(
	int playerIndex,
	int moveDirection)
{
	int characterID =
		MM_Characters_Roster2PGetSelectedCharacterID(playerIndex);

	if (characterID < 0)
		return;

	RacerLoadout_SetCharacter(playerIndex, characterID);
	D230.characterSelectPlayerState.modelMoveDir[playerIndex] =
		(s16)moveDirection;
}

static void MM_Characters_Roster2PChangePage(int direction)
{
	int pageTotal = MM_Characters_RosterGetPageCount();
	s_characterPage =
		(s16)((s_characterPage + direction + pageTotal) % pageTotal);

	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	if (pageCount > 0)
	{
		for (int playerIndex = 0; playerIndex < 2; playerIndex++)
		{
			int visibleIndex =
				MM_Characters_RosterFindVisibleIndex(
					data.characterIDs[playerIndex]);
			int cursor = -1;

			if ((visibleIndex >= 0) &&
				((visibleIndex /
					MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) ==
					s_characterPage))
			{
				cursor =
					visibleIndex %
					MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
			}

			if ((cursor < 0) || (cursor >= pageCount) ||
				(!MM_Characters_DuplicateSelectionsAllowed() &&
					(playerIndex == 1) &&
					(cursor == s_characterCursor2P[0])))
			{
				cursor = playerIndex;
				if (cursor >= pageCount)
					cursor = 0;

				if (!MM_Characters_DuplicateSelectionsAllowed() &&
					(playerIndex == 1) &&
					(cursor == s_characterCursor2P[0]) &&
					(pageCount > 1))
				{
					cursor =
						(cursor + 1) % pageCount;
				}
			}

			s_characterCursor2P[playerIndex] = (s16)cursor;
			MM_Characters_Roster2PApplySelection(
				playerIndex,
				direction > 0
					? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
					: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
		}
	}
	else
	{
		s_characterCursor2P[0] = 0;
		s_characterCursor2P[1] = 1;
	}

	OtherFX_Play(0, 1);
}

static void MM_Characters_Roster2PMoveCursor(
	int playerIndex,
	u32 button)
{
	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);
	if (pageCount <= 0)
		return;

	int oldCursor = s_characterCursor2P[playerIndex];
	int cursor = oldCursor;
	int otherCursor = s_characterCursor2P[playerIndex ^ 1];
	int column = cursor % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	int delta = 0;

	if ((button & BTN_RIGHT) != 0)
	{
		if ((column + 1 < MM_CHARACTER_SELECT_ROSTER_COLUMNS) &&
			(cursor + 1 < pageCount))
			delta = 1;
	}
	else if ((button & BTN_LEFT) != 0)
	{
		if (column > 0)
			delta = -1;
	}
	else if ((button & BTN_DOWN) != 0)
	{
		if (cursor + MM_CHARACTER_SELECT_ROSTER_COLUMNS < pageCount)
			delta = MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	}
	else if ((button & BTN_UP) != 0)
	{
		if (cursor - MM_CHARACTER_SELECT_ROSTER_COLUMNS >= 0)
			delta = -MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	}

	if (delta == 0)
		return;

	int candidate = cursor + delta;

	while (!MM_Characters_DuplicateSelectionsAllowed() &&
		(candidate == otherCursor))
	{
		int next = candidate + delta;

		if ((delta == 1) &&
			((next / MM_CHARACTER_SELECT_ROSTER_COLUMNS) !=
				(cursor / MM_CHARACTER_SELECT_ROSTER_COLUMNS)))
			return;
		if ((delta == -1) &&
			((next / MM_CHARACTER_SELECT_ROSTER_COLUMNS) !=
				(cursor / MM_CHARACTER_SELECT_ROSTER_COLUMNS)))
			return;
		if ((next < 0) || (next >= pageCount))
			return;

		candidate = next;
	}

	s_characterCursor2P[playerIndex] = (s16)candidate;
	MM_Characters_Roster2PApplySelection(
		playerIndex,
		candidate > oldCursor
			? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
			: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
	OtherFX_Play(0, 1);
}

static b32 MM_Characters_Roster3PCursorTaken(
	int playerIndex,
	int cursor)
{
	if (MM_Characters_DuplicateSelectionsAllowed())
		return false;

	for (int otherPlayer = 0; otherPlayer < 3; otherPlayer++)
	{
		if ((otherPlayer != playerIndex) &&
			(s_characterCursor3P[otherPlayer] == cursor))
		{
			return true;
		}
	}

	return false;
}

static void MM_Characters_Roster3PRestoreSelection(void)
{
	int firstVisibleIndex =
		MM_Characters_RosterFindVisibleIndex(data.characterIDs[0]);
	s_characterPage = (s16)(firstVisibleIndex >= 0 ?
		(firstVisibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) : 0);
	s_characterCursor3P[0] = -1;
	s_characterCursor3P[1] = -1;
	s_characterCursor3P[2] = -1;

	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	for (int playerIndex = 0; playerIndex < 3; playerIndex++)
	{
		int visibleIndex =
			MM_Characters_RosterFindVisibleIndex(
				data.characterIDs[playerIndex]);
		int cursor = -1;

		if ((visibleIndex >= 0) &&
			((visibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) ==
				s_characterPage))
		{
			cursor =
				visibleIndex % MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
		}

		if ((cursor < 0) || (cursor >= pageCount) ||
			MM_Characters_Roster3PCursorTaken(playerIndex, cursor))
		{
			cursor = playerIndex;
			if (cursor >= pageCount)
				cursor = 0;

			while ((pageCount > 1) &&
				MM_Characters_Roster3PCursorTaken(playerIndex, cursor))
			{
				cursor = (cursor + 1) % pageCount;
			}
		}

		s_characterCursor3P[playerIndex] = (s16)cursor;
	}
}

static int MM_Characters_Roster3PGetSelectedCharacterID(int playerIndex)
{
	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	if ((playerIndex < 0) || (playerIndex >= 3) ||
		(s_characterCursor3P[playerIndex] < 0) ||
		(s_characterCursor3P[playerIndex] >= pageCount))
	{
		return -1;
	}

	return MM_Characters_RosterGetCharacterIDByIndex(
		s_characterPage * MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
		s_characterCursor3P[playerIndex]);
}

static void MM_Characters_Roster3PApplySelection(
	int playerIndex,
	int moveDirection)
{
	int characterID =
		MM_Characters_Roster3PGetSelectedCharacterID(playerIndex);

	if (characterID < 0)
		return;

	RacerLoadout_SetCharacter(playerIndex, characterID);
	D230.characterSelectPlayerState.modelMoveDir[playerIndex] =
		(s16)moveDirection;
}

static void MM_Characters_Roster3PChangePage(int direction)
{
	int pageTotal = MM_Characters_RosterGetPageCount();
	s_characterPage =
		(s16)((s_characterPage + direction + pageTotal) % pageTotal);

	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	if (pageCount > 0)
	{
		/* Rebuild the page cursors from scratch so old-page slots do not collide. */
		s_characterCursor3P[0] = -1;
		s_characterCursor3P[1] = -1;
		s_characterCursor3P[2] = -1;

		for (int playerIndex = 0; playerIndex < 3; playerIndex++)
		{
			int visibleIndex =
				MM_Characters_RosterFindVisibleIndex(
					data.characterIDs[playerIndex]);
			int cursor = -1;

			if ((visibleIndex >= 0) &&
				((visibleIndex /
					MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE) ==
					s_characterPage))
			{
				cursor =
					visibleIndex %
					MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
			}

			if ((cursor < 0) || (cursor >= pageCount) ||
				MM_Characters_Roster3PCursorTaken(playerIndex, cursor))
			{
				cursor = playerIndex;
				if (cursor >= pageCount)
					cursor = 0;

				while ((pageCount > 1) &&
					MM_Characters_Roster3PCursorTaken(playerIndex, cursor))
				{
					cursor = (cursor + 1) % pageCount;
				}
			}

			s_characterCursor3P[playerIndex] = (s16)cursor;
			MM_Characters_Roster3PApplySelection(
				playerIndex,
				direction > 0
					? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
					: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
		}
	}
	else
	{
		s_characterCursor3P[0] = 0;
		s_characterCursor3P[1] = 1;
		s_characterCursor3P[2] = 2;
	}

	OtherFX_Play(0, 1);
}

static void MM_Characters_Roster3PMoveCursor(
	int playerIndex,
	u32 button)
{
	int pageCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);
	if (pageCount <= 0)
		return;

	int oldCursor = s_characterCursor3P[playerIndex];
	int cursor = oldCursor;
	int column = cursor % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	int delta = 0;

	if ((button & BTN_RIGHT) != 0)
	{
		if ((column + 1 < MM_CHARACTER_SELECT_ROSTER_COLUMNS) &&
			(cursor + 1 < pageCount))
			delta = 1;
	}
	else if ((button & BTN_LEFT) != 0)
	{
		if (column > 0)
			delta = -1;
	}
	else if ((button & BTN_DOWN) != 0)
	{
		if (cursor + MM_CHARACTER_SELECT_ROSTER_COLUMNS < pageCount)
			delta = MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	}
	else if ((button & BTN_UP) != 0)
	{
		if (cursor - MM_CHARACTER_SELECT_ROSTER_COLUMNS >= 0)
			delta = -MM_CHARACTER_SELECT_ROSTER_COLUMNS;
	}

	if (delta == 0)
		return;

	int candidate = cursor + delta;
	int startRow = cursor / MM_CHARACTER_SELECT_ROSTER_COLUMNS;

	while (MM_Characters_Roster3PCursorTaken(playerIndex, candidate))
	{
		int next = candidate + delta;

		if (((delta == 1) || (delta == -1)) &&
			((next / MM_CHARACTER_SELECT_ROSTER_COLUMNS) != startRow))
			return;
		if ((next < 0) || (next >= pageCount))
			return;

		candidate = next;
	}

	s_characterCursor3P[playerIndex] = (s16)candidate;
	MM_Characters_Roster3PApplySelection(
		playerIndex,
		candidate > oldCursor
			? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
			: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
	OtherFX_Play(0, 1);
}

static b32 MM_Characters_Roster4PCursorTaken(
	int playerIndex,
	int cursor)
{
	if (MM_Characters_DuplicateSelectionsAllowed())
		return false;

	for (int otherPlayer = 0; otherPlayer < 4; otherPlayer++)
	{
		if ((otherPlayer != playerIndex) &&
			(s_characterCursor4P[otherPlayer] == cursor))
		{
			return true;
		}
	}

	return false;
}

static void MM_Characters_Roster4PRestoreSelection(void)
{
	int firstVisibleIndex =
		MM_Characters_RosterFindVisibleIndex(data.characterIDs[0]);
	s_characterPage = (s16)(firstVisibleIndex >= 0 ?
		(firstVisibleIndex / MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE) : 0);

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
		s_characterCursor4P[playerIndex] = -1;

	int pageCount =
		MM_Characters_Roster4PGetPageCharacterCount(s_characterPage);

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		int visibleIndex =
			MM_Characters_RosterFindVisibleIndex(
				data.characterIDs[playerIndex]);
		int cursor = -1;

		if ((visibleIndex >= 0) &&
			((visibleIndex / MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE) ==
				s_characterPage))
		{
			cursor =
				visibleIndex % MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE;
		}

		if ((cursor < 0) || (cursor >= pageCount) ||
			MM_Characters_Roster4PCursorTaken(playerIndex, cursor))
		{
			cursor = playerIndex;
			if (cursor >= pageCount)
				cursor = 0;

			while ((pageCount > 1) &&
				MM_Characters_Roster4PCursorTaken(playerIndex, cursor))
			{
				cursor = (cursor + 1) % pageCount;
			}
		}

		s_characterCursor4P[playerIndex] = (s16)cursor;
	}
}

static int MM_Characters_Roster4PGetSelectedCharacterID(int playerIndex)
{
	int pageCount =
		MM_Characters_Roster4PGetPageCharacterCount(s_characterPage);

	if ((playerIndex < 0) || (playerIndex >= 4) ||
		(s_characterCursor4P[playerIndex] < 0) ||
		(s_characterCursor4P[playerIndex] >= pageCount))
	{
		return -1;
	}

	return MM_Characters_RosterGetCharacterIDByIndex(
		s_characterPage * MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE +
		s_characterCursor4P[playerIndex]);
}

static void MM_Characters_Roster4PApplySelection(
	int playerIndex,
	int moveDirection)
{
	int characterID =
		MM_Characters_Roster4PGetSelectedCharacterID(playerIndex);

	if (characterID < 0)
		return;

	RacerLoadout_SetCharacter(playerIndex, characterID);
	D230.characterSelectPlayerState.modelMoveDir[playerIndex] =
		(s16)moveDirection;
}

static void MM_Characters_Roster4PChangePage(int direction)
{
	int pageTotal = MM_Characters_RosterGetPageCount();
	s_characterPage =
		(s16)((s_characterPage + direction + pageTotal) % pageTotal);

	int pageCount =
		MM_Characters_Roster4PGetPageCharacterCount(s_characterPage);

	if (pageCount > 0)
	{
		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
			s_characterCursor4P[playerIndex] = -1;

		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
		{
			int visibleIndex =
				MM_Characters_RosterFindVisibleIndex(
					data.characterIDs[playerIndex]);
			int cursor = -1;

			if ((visibleIndex >= 0) &&
				((visibleIndex /
					MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE) ==
					s_characterPage))
			{
				cursor =
					visibleIndex %
					MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE;
			}

			if ((cursor < 0) || (cursor >= pageCount) ||
				MM_Characters_Roster4PCursorTaken(playerIndex, cursor))
			{
				cursor = playerIndex;
				if (cursor >= pageCount)
					cursor = 0;

				while ((pageCount > 1) &&
					MM_Characters_Roster4PCursorTaken(playerIndex, cursor))
				{
					cursor = (cursor + 1) % pageCount;
				}
			}

			s_characterCursor4P[playerIndex] = (s16)cursor;
			MM_Characters_Roster4PApplySelection(
				playerIndex,
				direction > 0
					? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
					: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
		}
	}
	else
	{
		s_characterCursor4P[0] = 0;
		s_characterCursor4P[1] = 1;
		s_characterCursor4P[2] = 2;
		s_characterCursor4P[3] = 3;
	}

	OtherFX_Play(0, 1);
}

static void MM_Characters_Roster4PMoveCursor(
	int playerIndex,
	u32 button)
{
	int pageCount =
		MM_Characters_Roster4PGetPageCharacterCount(s_characterPage);
	if (pageCount <= 0)
		return;

	int oldCursor = s_characterCursor4P[playerIndex];
	int cursor = oldCursor;
	int column = cursor % MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;
	int delta = 0;

	if ((button & BTN_RIGHT) != 0)
	{
		if ((column + 1 < MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS) &&
			(cursor + 1 < pageCount))
			delta = 1;
	}
	else if ((button & BTN_LEFT) != 0)
	{
		if (column > 0)
			delta = -1;
	}
	else if ((button & BTN_DOWN) != 0)
	{
		if (cursor + MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS < pageCount)
			delta = MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;
	}
	else if ((button & BTN_UP) != 0)
	{
		if (cursor - MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS >= 0)
			delta = -MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;
	}

	if (delta == 0)
		return;

	int candidate = cursor + delta;
	int startRow = cursor / MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;

	while (MM_Characters_Roster4PCursorTaken(playerIndex, candidate))
	{
		int next = candidate + delta;

		if (((delta == 1) || (delta == -1)) &&
			((next / MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS) != startRow))
			return;
		if ((next < 0) || (next >= pageCount))
			return;

		candidate = next;
	}

	s_characterCursor4P[playerIndex] = (s16)candidate;
	MM_Characters_Roster4PApplySelection(
		playerIndex,
		candidate > oldCursor
			? MM_CHARACTER_SELECT_MODEL_MOVE_NEXT
			: MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
	OtherFX_Play(0, 1);
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800ad98c-0x800ada4c.
void MM_Characters_AnimateColors(u8 *colorData, s16 playerID, s16 flag)
{
	u8 colorAdjustmentValue;
	u32 trigApproximationIndex;
	u32 trigApprox;

	// access int RGBA as a char array,
	// for editing components of color
	u8 *ptrColor = (u8 *)data.ptrColor[playerID + PLAYER_BLUE];

	trigApprox = 0;

	// if player has not selected character yet
	// see MM_Characters_MenuProc
	if (flag == 0)
	{
		trigApproximationIndex = sdata->frameCounter * MM_CHARACTER_SELECT_COLOR_PHASE_FRAME_STEP + playerID * MM_CHARACTER_SELECT_COLOR_PHASE_PLAYER_STEP;

		// approximate trigonometry
		trigApprox = CTR_ReadU32LE(&data.trigApprox[trigApproximationIndex & MM_CHARACTER_SELECT_COLOR_TRIG_MASK]);

		if ((trigApproximationIndex & MM_CHARACTER_SELECT_COLOR_TRIG_HIGH_HALF_BIT) == 0)
		{
			trigApprox = trigApprox << 0x10;
		}
		trigApprox = trigApprox >> 0x10;

		if ((trigApproximationIndex & MM_CHARACTER_SELECT_COLOR_TRIG_NEGATE_BIT) != 0)
		{
			trigApprox = -(int)trigApprox;
		}
	}

	colorAdjustmentValue = 0;
	if (MM_CHARACTER_SELECT_COLOR_PULSE_THRESHOLD < (int)trigApprox)
	{
		colorAdjustmentValue = ((trigApprox << MM_CHARACTER_SELECT_COLOR_PULSE_SCALE_SHIFT) >> MM_CHARACTER_SELECT_COLOR_PULSE_FP_SHIFT);
	}

	colorData[0] = ptrColor[0] | colorAdjustmentValue;
	colorData[1] = ptrColor[1] | colorAdjustmentValue;
	colorData[2] = ptrColor[2] | colorAdjustmentValue;
	colorData[3] = 0;

	return;
}

#if defined(CTR_NATIVE)
static void MM_Characters_RosterDrawDriverWindow(
	struct GameTracker *gGT,
	u32 *ot)
{
	RECT drawRect = {
		MM_CHARACTER_SELECT_PREVIEW_X,
		MM_CHARACTER_SELECT_PREVIEW_Y,
		MM_CHARACTER_SELECT_PREVIEW_W,
		MM_CHARACTER_SELECT_PREVIEW_H,
	};

	Color animatedColor;
	MM_Characters_AnimateColors(
		(u8 *)&animatedColor,
		0,
		(sdata->characterSelectFlags & 1) == 0);

	RECTMENU_DrawOuterRect_HighLevel(&drawRect, animatedColor, 0, ot);
	RECTMENU_DrawInnerRect(&drawRect, 9, &ot[3]);

	RECT viewportRect = {0, 0, drawRect.w, drawRect.h};
	RECTMENU_DrawRwdBlueRect(
		&viewportRect,
		&D230.characterSelect_BlueRectColors[0],
		&gGT->pushBuffer[0].ptrOT[0x3ff],
		&gGT->backBuffer->primMem);

	MM_Characters_LoadoutDrawStatus(
		0,
		drawRect.x + (drawRect.w >> 1),
		drawRect.y + drawRect.h - 14,
		18);
}

static void MM_Characters_RosterDrawHeader(u32 *ot)
{
	char pageText[32];
	char pagePrev[48] = "L1";
	char pageNext[48] = "R1";
	int shoulderFont = FONT_BIG;

#if defined(CTR_NATIVE)
	Platform_InputFormatPlayerBindingName(
		0, PLATFORM_INPUT_ACTION_L1, pagePrev, sizeof(pagePrev));
	Platform_InputFormatPlayerBindingName(
		0, PLATFORM_INPUT_ACTION_R1, pageNext, sizeof(pageNext));

	if ((int)strlen(pagePrev) > 4 || (int)strlen(pageNext) > 4)
		shoulderFont = FONT_SMALL;
#endif

	snprintf(
		pageText,
		sizeof(pageText),
		"%d/%d",
		(int)s_characterPage + 1,
		MM_Characters_RosterGetPageCount());

	/*
	 * Use CTR's own quip/menu-box helper so these controls inherit the
	 * same menu panel background, outline color, text vertical offset
	 * and centering rules as the retail UI.
	 */
	int headerY = MM_CHARACTER_SELECT_PAGE_HEADER_Y - 10;

	/* Symmetric spacing around the centered page counter.
	 * Wider shoulder boxes keep full keyboard names readable. */
	int l1Width = 72;
	int pageWidth = 54;
	int r1Width = 72;

	RECTMENU_DrawQuip(
		pagePrev,
		MM_CHARACTER_SELECT_PAGE_L1_X,
		headerY,
		l1Width,
		shoulderFont,
		JUSTIFY_CENTER | WHITE,
		0);

	RECTMENU_DrawQuip(
		pageText,
		MM_CHARACTER_SELECT_PAGE_TEXT_X,
		headerY,
		pageWidth,
		FONT_BIG,
		JUSTIFY_CENTER | ORANGE,
		0);

	RECTMENU_DrawQuip(
		pageNext,
		MM_CHARACTER_SELECT_PAGE_R1_X,
		headerY,
		r1Width,
		shoulderFont,
		JUSTIFY_CENTER | WHITE,
		0);

	(void)ot;
}

static void MM_Characters_RosterMenuProc(
	struct GameTracker *gGT,
	u32 *ot)
{
	u32 button = sdata->buttonTapPerPlayer[0];
	b32 playerSelected = (sdata->characterSelectFlags & 1) != 0;
#if defined(CTR_NATIVE)
	b32 hangoutTransition = NativeHangout_IsCustomizationTransitionActive();
	if (hangoutTransition)
		button = 0;
#endif

#if defined(CTR_NATIVE)
	if (!hangoutTransition &&
	    (D230.characterSelectMenuState == IN_MENU) &&
		!playerSelected &&
		(RacerLoadout_GetSelectionPart(0) == RACER_SELECT_CHARACTER))
	{
		int mouseX;
		int mouseY;
		int pageCount = MM_Characters_RosterGetPageCharacterCount(s_characterPage);

		if (RECTMENU_NativeTouchGet(&mouseX, &mouseY))
		{
			/* Page controls in the native expanded roster. */
			if (MM_Characters_RosterGetPageCount() > 1)
			{
				RECT prevPageRect = {
					MM_CHARACTER_SELECT_PAGE_L1_X - 12,
					MM_CHARACTER_SELECT_PAGE_HEADER_Y - 4,
					44,
					18,
				};
				RECT nextPageRect = {
					MM_CHARACTER_SELECT_PAGE_R1_X - 12,
					MM_CHARACTER_SELECT_PAGE_HEADER_Y - 4,
					44,
					18,
				};

				if (RECTMENU_NativeTouchConsumeRect(&prevPageRect))
				{
					MM_Characters_RosterChangePage(-1);
				}
				else if (RECTMENU_NativeTouchConsumeRect(&nextPageRect))
				{
					MM_Characters_RosterChangePage(1);
				}
			}

			/* Portraits are literal touch buttons. */
			for (int slot = 0; slot < pageCount; slot++)
			{
				int column = slot % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
				int row = slot / MM_CHARACTER_SELECT_ROSTER_COLUMNS;
				RECT touchRect = {
					MM_CHARACTER_SELECT_ROSTER_X +
						column * MM_CHARACTER_SELECT_ROSTER_COL_STEP,
					MM_CHARACTER_SELECT_ROSTER_Y +
						row * MM_CHARACTER_SELECT_ROSTER_ROW_STEP,
					MM_CHARACTER_SELECT_ROSTER_SLOT_W,
					MM_CHARACTER_SELECT_ROSTER_SLOT_H,
				};

				if ((mouseX >= touchRect.x) &&
				    (mouseX < touchRect.x + touchRect.w) &&
				    (mouseY >= touchRect.y) &&
				    (mouseY < touchRect.y + touchRect.h))
				{
					if (slot != s_characterCursor)
					{
						int oldCursor = s_characterCursor;
						s_characterCursor = (s16)slot;
						MM_Characters_RosterApplySelection(
							slot > oldCursor ?
								MM_CHARACTER_SELECT_MODEL_MOVE_NEXT :
								MM_CHARACTER_SELECT_MODEL_MOVE_PREV);
					}

					if (RECTMENU_NativeTouchConsumeRect(&touchRect))
					{
						button |= BTN_CROSS_one;
					}
					break;
				}
			}
		}
	}
#endif

	if (D230.characterSelectMenuState == IN_MENU)
	{
#if defined(CTR_NATIVE)
		if (hangoutTransition)
			goto hangout_customization_draw_only;
#endif
		if (!playerSelected)
		{
			int loadoutResult =
				MM_Characters_LoadoutHandlePartInput(0, button);

			if (loadoutResult == NATIVE_LOADOUT_INPUT_READY)
			{
				sdata->characterSelectFlags |= 1;
#if defined(CTR_NATIVE)
				if (NativeHangout_IsCustomizationOpen())
					NativeHangout_RequestCustomizationExitTransition(1);
				else
#endif
				{
					D230.characterSelectExitsForward = 1;
					D230.characterSelectMenuState = EXITING_MENU;
				}
			}
			else if (loadoutResult == NATIVE_LOADOUT_INPUT_NONE)
			{
				if ((button & BTN_L1) != 0)
					MM_Characters_RosterChangePage(-1);
				else if ((button & BTN_R1) != 0)
					MM_Characters_RosterChangePage(1);
				else if ((button & MM_CHARACTER_SELECT_INPUT_DPAD) != 0)
					MM_Characters_RosterMoveCursor(button);

				int selectedCharacterID =
					MM_Characters_RosterGetSelectedCharacterID();

				if (((button & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0) &&
					(selectedCharacterID >= 0))
				{
					b32 ready =
						MM_Characters_LoadoutConfirmCharacter(
							0, selectedCharacterID);
					OtherFX_Play(1, 1);

					if (ready)
					{
						sdata->characterSelectFlags |= 1;
#if defined(CTR_NATIVE)
						if (NativeHangout_IsCustomizationOpen())
							NativeHangout_RequestCustomizationExitTransition(1);
						else
#endif
						{
							D230.characterSelectExitsForward = 1;
							D230.characterSelectMenuState = EXITING_MENU;
						}
					}
				}

				if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
				{
#if defined(CTR_NATIVE)
					if (NativeHangout_IsCustomizationOpen())
						NativeHangout_RequestCustomizationExitTransition(0);
					else
#endif
					{
						D230.characterSelectExitsForward = 0;
						D230.characterSelectMenuState = EXITING_MENU;
					}
					OtherFX_Play(2, 1);
				}
			}
		}
		else if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
		{
			sdata->characterSelectFlags &= ~1;
			MM_Characters_LoadoutUndoReady(0);
			OtherFX_Play(2, 1);
		}
	}

#if defined(CTR_NATIVE)
hangout_customization_draw_only:
#endif
	sdata->buttonTapPerPlayer[0] = 0;

	int pageCharacterCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);
	int selectedCharacterID =
		MM_Characters_RosterGetSelectedCharacterID();

	Color playerColor;
	MM_Characters_AnimateColors(
		(u8 *)&playerColor,
		0,
		(sdata->characterSelectFlags & 1));

	for (int slot = 0;
		slot < MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
		slot++)
	{
		int column = slot % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
		int row = slot / MM_CHARACTER_SELECT_ROSTER_COLUMNS;

		RECT drawRect = {
			MM_CHARACTER_SELECT_ROSTER_X +
				column * MM_CHARACTER_SELECT_ROSTER_COL_STEP,
			MM_CHARACTER_SELECT_ROSTER_Y +
				row * MM_CHARACTER_SELECT_ROSTER_ROW_STEP,
			MM_CHARACTER_SELECT_ROSTER_SLOT_W,
			MM_CHARACTER_SELECT_ROSTER_SLOT_H,
		};

		int characterID = -1;
		if (slot < pageCharacterCount)
		{
			int visibleIndex =
				s_characterPage *
					MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
				slot;
			characterID =
				MM_Characters_RosterGetCharacterIDByIndex(
					visibleIndex);
		}

		b32 slotSelected =
			(slot == s_characterCursor) &&
			(characterID >= 0);

		/*
		 * Retail-style selection: the same thick player-color outline
		 * used by the original selector. The pulsing inner highlight is
		 * submitted after the portrait below so it stays behind it.
		 */
		if (slotSelected)
		{
			RECTMENU_DrawOuterRect_HighLevel(
				&drawRect,
				playerColor,
				0,
				ot);
		}

		if (characterID >= 0)
		{
			Color iconColor =
				D230.characterSelect_NeutralColor;

			MM_Characters_RosterDrawIcon(
				gGT,
				characterID,
				&drawRect,
				iconColor);
		}

		if (slotSelected)
		{
			Color highlightColor = playerColor;
			highlightColor.r =
				(u8)(((u32)highlightColor.r << 2) / 5);
			highlightColor.g =
				(u8)(((u32)highlightColor.g << 2) / 5);
			highlightColor.b =
				(u8)(((u32)highlightColor.b << 2) / 5);

			RECT highlightRect = {
				drawRect.x + 3,
				drawRect.y + 2,
				drawRect.w - 6,
				drawRect.h - 4,
			};

			/* Same flashing square behavior as the retail selector. */
			CTR_Box_DrawSolidBox(
				&highlightRect,
				highlightColor,
				ot);
		}

		/*
		 * Submit the retail panel LAST, just like the original selector.
		 * Use the compact retail shadow flags (0x80 | 0x40), so the
		 * shadow keeps the CTR look without spilling into neighboring
		 * cells or past the lower edge of the 4:3 menu.
		 */
		RECTMENU_DrawInnerRect(&drawRect, 0xC0, ot);

	}

	int previewCharacterID = selectedCharacterID;
	if (previewCharacterID < 0)
		previewCharacterID = data.characterIDs[0];

	if (previewCharacterID >= 0)
	{
		DecalFont_DrawLine(
			(char *)MM_Characters_RosterGetDisplayName(previewCharacterID),
			MM_CHARACTER_SELECT_PREVIEW_X +
				(MM_CHARACTER_SELECT_PREVIEW_W >> 1),
			MM_CHARACTER_SELECT_PREVIEW_NAME_Y,
			FONT_CREDITS,
			JUSTIFY_CENTER | ORANGE);
	}

	MM_Characters_RosterDrawHeader(ot);

	D230.characterSelectPlayerState.angle[0] +=
		MM_CHARACTER_SELECT_SPIN_STEP *
		Platform_GetLegacy30HzTicks();

	MM_Characters_RosterDrawDriverWindow(gGT, ot);
}

static void MM_Characters_RosterMPDrawDriverWindows(
	struct GameTracker *gGT,
	u32 *ot,
	int playerCount)
{
	for (int playerIndex = 0; playerIndex < playerCount; playerIndex++)
	{
		SVec2 *windowPos =
			&D230.activeCharacterSelectWindowPos[playerIndex];
		struct TransitionMeta *driverWindowTransition =
			&D230.characterSelectTransitionMeta[
				playerIndex +
				MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST];

		RECT drawRect = {
			driverWindowTransition->currX + windowPos->x,
			driverWindowTransition->currY + windowPos->y,
			D230.characterSelectWindowWidth,
			D230.characterSelectWindowHeight,
		};

		b32 playerSelected =
			((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

		Color animatedColor;
		MM_Characters_AnimateColors(
			(u8 *)&animatedColor,
			playerIndex,
			playerSelected ^ 1);

		RECTMENU_DrawOuterRect_HighLevel(
			&drawRect,
			animatedColor,
			0,
			ot);

		if (playerSelected)
		{
			RECT selectedBorder = drawRect;

			for (int borderIndex = 0;
				borderIndex < MM_CHARACTER_SELECT_SELECTED_BORDER_COUNT;
				borderIndex++)
			{
				selectedBorder.x +=
					MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_X;
				selectedBorder.y +=
					MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_Y;
				selectedBorder.w -=
					MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_W;
				selectedBorder.h -=
					MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_H;

				animatedColor.r =
					(u8)(((u32)animatedColor.r << 2) / 5);
				animatedColor.g =
					(u8)(((u32)animatedColor.g << 2) / 5);
				animatedColor.b =
					(u8)(((u32)animatedColor.b << 2) / 5);

				RECTMENU_DrawOuterRect_HighLevel(
					&selectedBorder,
					animatedColor,
					0,
					ot);
			}
		}

		RECTMENU_DrawInnerRect(&drawRect, 9, &ot[3]);

		RECT viewportRect = {
			0,
			0,
			drawRect.w,
			drawRect.h,
		};

		RECTMENU_DrawRwdBlueRect(
			&viewportRect,
			&D230.characterSelect_BlueRectColors[0],
			&gGT->pushBuffer[playerIndex].ptrOT[0x3ff],
			&gGT->backBuffer->primMem);

		if ((D230.characterSelectModelMoveTimer[playerIndex] == 0) &&
			(D230.characterSelectPlayerState.currentCharacterID[playerIndex] ==
				data.characterIDs[playerIndex]))
		{
			int characterID = data.characterIDs[playerIndex];
			int fontType =
				(playerCount >= 3) ? FONT_SMALL : FONT_CREDITS;
			int nameOffset = (playerCount >= 3) ? 4 : 0;

			DecalFont_DrawLine(
				(char *)MM_Characters_RosterGetDisplayName(characterID),
				drawRect.x + (drawRect.w >> 1),
				drawRect.y + D230.characterSelectNameTextY + nameOffset,
				fontType,
				JUSTIFY_CENTER | ORANGE);
		}

		MM_Characters_LoadoutDrawStatus(
			playerIndex,
			drawRect.x + (drawRect.w >> 1),
			drawRect.y + drawRect.h - 12,
			playerCount >= 3 ? 10 : 16);

		D230.characterSelectPlayerState.angle[playerIndex] +=
			MM_CHARACTER_SELECT_SPIN_STEP *
			Platform_GetLegacy30HzTicks();
	}
}

static void MM_Characters_Roster2PMenuProc(
	struct GameTracker *gGT,
	u32 *ot)
{
	if (D230.characterSelectMenuState == IN_MENU)
	{
		for (int playerIndex = 0; playerIndex < 2; playerIndex++)
		{
			u32 button = sdata->buttonTapPerPlayer[playerIndex];
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if (!playerSelected)
			{
				int loadoutResult =
					MM_Characters_LoadoutHandlePartInput(
						playerIndex, button);

				if (loadoutResult == NATIVE_LOADOUT_INPUT_READY)
				{
					MM_Characters_LoadoutMarkReady(playerIndex, 2);
				}
				else if (loadoutResult == NATIVE_LOADOUT_INPUT_NONE)
				{
					if ((button & BTN_L1) != 0)
					{
						MM_Characters_Roster2PChangePage(-1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & BTN_R1) != 0)
					{
						MM_Characters_Roster2PChangePage(1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & MM_CHARACTER_SELECT_INPUT_DPAD) != 0)
					{
						MM_Characters_Roster2PMoveCursor(
							playerIndex,
							button);
					}

					int selectedCharacterID =
						MM_Characters_Roster2PGetSelectedCharacterID(
							playerIndex);

					if (((button & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0) &&
						(selectedCharacterID >= 0))
					{
						b32 ready =
							MM_Characters_LoadoutConfirmCharacter(
								playerIndex, selectedCharacterID);
						OtherFX_Play(1, 1);
						if (ready)
							MM_Characters_LoadoutMarkReady(playerIndex, 2);
					}

					if ((playerIndex == 0) &&
						((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0))
					{
						D230.characterSelectExitsForward = 0;
						D230.characterSelectMenuState = EXITING_MENU;
						OtherFX_Play(2, 1);
					}
				}
			}
			else if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
			{
				sdata->characterSelectFlags &=
					(u16)~(1 << playerIndex);
				MM_Characters_LoadoutUndoReady(playerIndex);
				OtherFX_Play(2, 1);
			}

			sdata->buttonTapPerPlayer[playerIndex] = 0;
		}
	}
	else
	{
		sdata->buttonTapPerPlayer[0] = 0;
		sdata->buttonTapPerPlayer[1] = 0;
	}

	int pageCharacterCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	Color playerColors[2];
	for (int playerIndex = 0; playerIndex < 2; playerIndex++)
	{
		MM_Characters_AnimateColors(
			(u8 *)&playerColors[playerIndex],
			playerIndex,
			(sdata->characterSelectFlags >> playerIndex) & 1U);
	}

	for (int slot = 0;
		slot < MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
		slot++)
	{
		int column = slot % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
		int row = slot / MM_CHARACTER_SELECT_ROSTER_COLUMNS;

		RECT drawRect = {
			MM_CHARACTER_SELECT_ROSTER_X +
				column * MM_CHARACTER_SELECT_ROSTER_COL_STEP,
			MM_CHARACTER_SELECT_ROSTER_Y +
				row * MM_CHARACTER_SELECT_ROSTER_ROW_STEP,
			MM_CHARACTER_SELECT_ROSTER_SLOT_W,
			MM_CHARACTER_SELECT_ROSTER_SLOT_H,
		};

		int characterID = -1;
		if (slot < pageCharacterCount)
		{
			characterID =
				MM_Characters_RosterGetCharacterIDByIndex(
					s_characterPage *
						MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
					slot);
		}

		int cursorPlayer = -1;
		for (int playerIndex = 0; playerIndex < 2; playerIndex++)
		{
			if ((characterID >= 0) &&
				(s_characterCursor2P[playerIndex] == slot))
			{
				cursorPlayer = playerIndex;
				break;
			}
		}

		if (cursorPlayer >= 0)
		{
			/*
			 * Submit the player number first: this OT is prepend-based,
			 * so earlier submissions render later/on top of the cursor.
			 */
			DecalFont_DrawLine(
				D230.playerNumberStrings[cursorPlayer],
				drawRect.x - 6,
				drawRect.y - 3,
				FONT_BIG,
				WHITE);

			RECTMENU_DrawOuterRect_HighLevel(
				&drawRect,
				playerColors[cursorPlayer],
				0,
				ot);
		}

		if (characterID >= 0)
		{
			Color iconColor =
				D230.characterSelect_NeutralColor;

			for (int playerIndex = 0;
				playerIndex < 2;
				playerIndex++)
			{
				if ((s_characterCursor2P[playerIndex] == slot) &&
					(((sdata->characterSelectFlags >> playerIndex) & 1U) != 0))
				{
					iconColor =
						D230.characterSelect_ChosenColor;
				}
			}

			MM_Characters_RosterDrawIcon(
				gGT,
				characterID,
				&drawRect,
				iconColor);
		}

		for (int playerIndex = 0; playerIndex < 2; playerIndex++)
		{
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if ((characterID >= 0) &&
				!playerSelected &&
				(s_characterCursor2P[playerIndex] == slot))
			{
				Color highlightColor = playerColors[playerIndex];
				highlightColor.r =
					(u8)(((u32)highlightColor.r << 2) / 5);
				highlightColor.g =
					(u8)(((u32)highlightColor.g << 2) / 5);
				highlightColor.b =
					(u8)(((u32)highlightColor.b << 2) / 5);

				RECT highlightRect = {
					drawRect.x + 3,
					drawRect.y + 2,
					drawRect.w - 6,
					drawRect.h - 4,
				};

				CTR_Box_DrawSolidBox(
					&highlightRect,
					highlightColor,
					ot);
			}
		}

		RECTMENU_DrawInnerRect(&drawRect, 0xC0, ot);
	}

	MM_Characters_RosterDrawHeader(ot);
	MM_Characters_RosterMPDrawDriverWindows(gGT, ot, 2);
}

static void MM_Characters_Roster3PMenuProc(
	struct GameTracker *gGT,
	u32 *ot)
{
	if (D230.characterSelectMenuState == IN_MENU)
	{
		for (int playerIndex = 0; playerIndex < 3; playerIndex++)
		{
			u32 button = sdata->buttonTapPerPlayer[playerIndex];
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if (!playerSelected)
			{
				int loadoutResult =
					MM_Characters_LoadoutHandlePartInput(
						playerIndex, button);

				if (loadoutResult == NATIVE_LOADOUT_INPUT_READY)
				{
					MM_Characters_LoadoutMarkReady(playerIndex, 3);
				}
				else if (loadoutResult == NATIVE_LOADOUT_INPUT_NONE)
				{
					if ((button & BTN_L1) != 0)
					{
						MM_Characters_Roster3PChangePage(-1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & BTN_R1) != 0)
					{
						MM_Characters_Roster3PChangePage(1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & MM_CHARACTER_SELECT_INPUT_DPAD) != 0)
					{
						MM_Characters_Roster3PMoveCursor(
							playerIndex,
							button);
					}

					int selectedCharacterID =
						MM_Characters_Roster3PGetSelectedCharacterID(
							playerIndex);

					if (((button & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0) &&
						(selectedCharacterID >= 0))
					{
						b32 ready =
							MM_Characters_LoadoutConfirmCharacter(
								playerIndex, selectedCharacterID);
						OtherFX_Play(1, 1);
						if (ready)
							MM_Characters_LoadoutMarkReady(playerIndex, 3);
					}

					if ((playerIndex == 0) &&
						((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0))
					{
						D230.characterSelectExitsForward = 0;
						D230.characterSelectMenuState = EXITING_MENU;
						OtherFX_Play(2, 1);
					}
				}
			}
			else if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
			{
				sdata->characterSelectFlags &=
					(u16)~(1 << playerIndex);
				MM_Characters_LoadoutUndoReady(playerIndex);
				OtherFX_Play(2, 1);
			}

			sdata->buttonTapPerPlayer[playerIndex] = 0;
		}
	}
	else
	{
		for (int playerIndex = 0; playerIndex < 3; playerIndex++)
			sdata->buttonTapPerPlayer[playerIndex] = 0;
	}

	int pageCharacterCount =
		MM_Characters_RosterGetPageCharacterCount(s_characterPage);

	Color playerColors[3];
	for (int playerIndex = 0; playerIndex < 3; playerIndex++)
	{
		MM_Characters_AnimateColors(
			(u8 *)&playerColors[playerIndex],
			playerIndex,
			(sdata->characterSelectFlags >> playerIndex) & 1U);
	}

	for (int slot = 0;
		slot < MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE;
		slot++)
	{
		int column = slot % MM_CHARACTER_SELECT_ROSTER_COLUMNS;
		int row = slot / MM_CHARACTER_SELECT_ROSTER_COLUMNS;

		RECT drawRect = {
			MM_CHARACTER_SELECT_ROSTER_X +
				column * MM_CHARACTER_SELECT_ROSTER_COL_STEP,
			MM_CHARACTER_SELECT_ROSTER_Y +
				row * MM_CHARACTER_SELECT_ROSTER_ROW_STEP,
			MM_CHARACTER_SELECT_ROSTER_SLOT_W,
			MM_CHARACTER_SELECT_ROSTER_SLOT_H,
		};

		int characterID = -1;
		if (slot < pageCharacterCount)
		{
			characterID =
				MM_Characters_RosterGetCharacterIDByIndex(
					s_characterPage *
						MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE +
					slot);
		}

		int cursorPlayer = -1;
		for (int playerIndex = 0; playerIndex < 3; playerIndex++)
		{
			if ((characterID >= 0) &&
				(s_characterCursor3P[playerIndex] == slot))
			{
				cursorPlayer = playerIndex;
				break;
			}
		}

		if (cursorPlayer >= 0)
		{
			DecalFont_DrawLine(
				D230.playerNumberStrings[cursorPlayer],
				drawRect.x - 6,
				drawRect.y - 3,
				FONT_BIG,
				WHITE);

			RECTMENU_DrawOuterRect_HighLevel(
				&drawRect,
				playerColors[cursorPlayer],
				0,
				ot);
		}

		if (characterID >= 0)
		{
			Color iconColor =
				D230.characterSelect_NeutralColor;

			for (int playerIndex = 0;
				playerIndex < 3;
				playerIndex++)
			{
				if ((s_characterCursor3P[playerIndex] == slot) &&
					(((sdata->characterSelectFlags >> playerIndex) & 1U) != 0))
				{
					iconColor =
						D230.characterSelect_ChosenColor;
				}
			}

			MM_Characters_RosterDrawIcon(
				gGT,
				characterID,
				&drawRect,
				iconColor);
		}

		for (int playerIndex = 0; playerIndex < 3; playerIndex++)
		{
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if ((characterID >= 0) &&
				!playerSelected &&
				(s_characterCursor3P[playerIndex] == slot))
			{
				Color highlightColor = playerColors[playerIndex];
				highlightColor.r =
					(u8)(((u32)highlightColor.r << 2) / 5);
				highlightColor.g =
					(u8)(((u32)highlightColor.g << 2) / 5);
				highlightColor.b =
					(u8)(((u32)highlightColor.b << 2) / 5);

				RECT highlightRect = {
					drawRect.x + 3,
					drawRect.y + 2,
					drawRect.w - 6,
					drawRect.h - 4,
				};

				CTR_Box_DrawSolidBox(
					&highlightRect,
					highlightColor,
					ot);
			}
		}

		RECTMENU_DrawInnerRect(&drawRect, 0xC0, ot);
	}

	MM_Characters_RosterDrawHeader(ot);
	MM_Characters_RosterMPDrawDriverWindows(gGT, ot, 3);
}
static void MM_Characters_Roster4PMenuProc(
	struct GameTracker *gGT,
	u32 *ot)
{
	if (D230.characterSelectMenuState == IN_MENU)
	{
		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
		{
			u32 button = sdata->buttonTapPerPlayer[playerIndex];
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if (!playerSelected)
			{
				int loadoutResult =
					MM_Characters_LoadoutHandlePartInput(
						playerIndex, button);

				if (loadoutResult == NATIVE_LOADOUT_INPUT_READY)
				{
					MM_Characters_LoadoutMarkReady(playerIndex, 4);
				}
				else if (loadoutResult == NATIVE_LOADOUT_INPUT_NONE)
				{
					if ((button & BTN_L1) != 0)
					{
						MM_Characters_Roster4PChangePage(-1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & BTN_R1) != 0)
					{
						MM_Characters_Roster4PChangePage(1);
						button &= ~(BTN_L1 | BTN_R1);
					}
					else if ((button & MM_CHARACTER_SELECT_INPUT_DPAD) != 0)
					{
						MM_Characters_Roster4PMoveCursor(
							playerIndex,
							button);
					}

					int selectedCharacterID =
						MM_Characters_Roster4PGetSelectedCharacterID(
							playerIndex);

					if (((button & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0) &&
						(selectedCharacterID >= 0))
					{
						b32 ready =
							MM_Characters_LoadoutConfirmCharacter(
								playerIndex, selectedCharacterID);
						OtherFX_Play(1, 1);
						if (ready)
							MM_Characters_LoadoutMarkReady(playerIndex, 4);
					}

					if ((playerIndex == 0) &&
						((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0))
					{
						D230.characterSelectExitsForward = 0;
						D230.characterSelectMenuState = EXITING_MENU;
						OtherFX_Play(2, 1);
					}
				}
			}
			else if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
			{
				sdata->characterSelectFlags &=
					(u16)~(1 << playerIndex);
				MM_Characters_LoadoutUndoReady(playerIndex);
				OtherFX_Play(2, 1);
			}

			sdata->buttonTapPerPlayer[playerIndex] = 0;
		}
	}
	else
	{
		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
			sdata->buttonTapPerPlayer[playerIndex] = 0;
	}

	int pageCharacterCount =
		MM_Characters_Roster4PGetPageCharacterCount(s_characterPage);

	Color playerColors[4];
	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		MM_Characters_AnimateColors(
			(u8 *)&playerColors[playerIndex],
			playerIndex,
			(sdata->characterSelectFlags >> playerIndex) & 1U);
	}

	for (int slot = 0;
		slot < MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE;
		slot++)
	{
		int column = slot % MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;
		int row = slot / MM_CHARACTER_SELECT_ROSTER_4P_COLUMNS;

		RECT drawRect = {
			MM_CHARACTER_SELECT_ROSTER_4P_X +
				column * MM_CHARACTER_SELECT_ROSTER_4P_COL_STEP,
			MM_CHARACTER_SELECT_ROSTER_Y +
				row * MM_CHARACTER_SELECT_ROSTER_ROW_STEP,
			MM_CHARACTER_SELECT_ROSTER_SLOT_W,
			MM_CHARACTER_SELECT_ROSTER_SLOT_H,
		};

		int characterID = -1;
		if (slot < pageCharacterCount)
		{
			characterID =
				MM_Characters_RosterGetCharacterIDByIndex(
					s_characterPage *
						MM_CHARACTER_SELECT_ROSTER_4P_PAGE_SIZE +
					slot);
		}

		int cursorPlayer = -1;
		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
		{
			if ((characterID >= 0) &&
				(s_characterCursor4P[playerIndex] == slot))
			{
				cursorPlayer = playerIndex;
				break;
			}
		}

		if (cursorPlayer >= 0)
		{
			DecalFont_DrawLine(
				D230.playerNumberStrings[cursorPlayer],
				drawRect.x - 6,
				drawRect.y - 3,
				FONT_BIG,
				WHITE);

			RECTMENU_DrawOuterRect_HighLevel(
				&drawRect,
				playerColors[cursorPlayer],
				0,
				ot);
		}

		if (characterID >= 0)
		{
			Color iconColor =
				D230.characterSelect_NeutralColor;

			for (int playerIndex = 0;
				playerIndex < 4;
				playerIndex++)
			{
				if ((s_characterCursor4P[playerIndex] == slot) &&
					(((sdata->characterSelectFlags >> playerIndex) & 1U) != 0))
				{
					iconColor =
						D230.characterSelect_ChosenColor;
				}
			}

			MM_Characters_RosterDrawIcon(
				gGT,
				characterID,
				&drawRect,
				iconColor);
		}

		for (int playerIndex = 0; playerIndex < 4; playerIndex++)
		{
			b32 playerSelected =
				((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

			if ((characterID >= 0) &&
				!playerSelected &&
				(s_characterCursor4P[playerIndex] == slot))
			{
				Color highlightColor = playerColors[playerIndex];
				highlightColor.r =
					(u8)(((u32)highlightColor.r << 2) / 5);
				highlightColor.g =
					(u8)(((u32)highlightColor.g << 2) / 5);
				highlightColor.b =
					(u8)(((u32)highlightColor.b << 2) / 5);

				RECT highlightRect = {
					drawRect.x + 3,
					drawRect.y + 2,
					drawRect.w - 6,
					drawRect.h - 4,
				};

				CTR_Box_DrawSolidBox(
					&highlightRect,
					highlightColor,
					ot);
			}
		}

		RECTMENU_DrawInnerRect(&drawRect, 0xC0, ot);
	}

	MM_Characters_RosterDrawHeader(ot);
	MM_Characters_RosterMPDrawDriverWindows(gGT, ot, 4);
}
#endif
// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 230 0x800ada4c-0x800adae4.
int MM_Characters_GetNextDriver(s16 direction, s16 characterID)
{
	u8 nextIcon = D230.activeCharacterSelectMeta[(s32)characterID].nextIconByDirection[direction];
	s16 unlocked = D230.activeCharacterSelectMeta[(s32)nextIcon].unlockFlags;

	// set new driver to the driver
	// you'd get when pressing Up button
	s16 newDriver = nextIcon;

	if (
	    // if desired driver is not unlocked by default
	    (unlocked != MM_CHARACTER_UNLOCK_ALWAYS) &&

	    !CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlocked))
	{
		// set new driver to the driver you already have
		newDriver = characterID;
	}

	// return new driver
	return newDriver;
}

// used for preventing players highlighting the same character
// also for when you go left of komodo joe's icon
// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 230 0x800adae4-0x800adb64.
b32 MM_Characters_boolIsInvalid(s16 *iconPerPlayer, s16 characterID, s16 player)
{
	// if there are players
	if (sdata->gGT->numPlyrNextGame)
	{
		// loop through players
		for (s16 playerIndex = 0; playerIndex < sdata->gGT->numPlyrNextGame; playerIndex++)
		{
			// if driver is taken
			if ((playerIndex != player) && (characterID == iconPerPlayer[playerIndex]))
			{
				return 1;
			}
		}
	}

	// if driver is not taken
	return 0;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800adb64-0x800adc0c.
// Search for character model by string,
// specific to main menu lev, altered in oxide mod
struct Model *MM_Characters_GetModelByName(const char *name)
{
	struct Model **models;
	struct Model *model;
	struct Level *level1 = sdata->gGT->level1;

	// if LEV is invalid
	if (level1 == NULL)
	{
		return NULL;
	}

	models = level1->ptrModelsPtrArray;
	if (models == NULL)
	{
		return NULL;
	}

	// loop through all models in array
	// of model pointers, until nullptr
	for (model = models[0]; model != NULL; models++, model = models[0])
	{
		if ((ModelName_ReadWord(model->name, 0) == ModelName_ReadWord(name, 0)) && (ModelName_ReadWord(model->name, 1) == ModelName_ReadWord(name, 1)) &&
		    (ModelName_ReadWord(model->name, 2) == ModelName_ReadWord(name, 2)) && (ModelName_ReadWord(model->name, 3) == ModelName_ReadWord(name, 3)))
		{
			// found it
			return model;
		}
	}

#if defined(CTR_NATIVE)
	/* External racers live outside the menu LEV and are loaded on demand. */
	struct Model *objModel = NativeObj_LoadRacer(name);
	if (objModel != NULL)
		return objModel;
#endif

	return NULL;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800adc0c-0x800ae0bc PSX path.
void MM_Characters_DrawWindows(b32 boolShowDrivers)
{
	struct GameTracker *gGT = sdata->gGT;
	SVec3 rot;

	if (boolShowDrivers != 0)
	{
		// enable drawing wheels
		gGT->renderFlags |= RENDER_FLAG_TIRES;
	}

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		SVec2 *windowPos = &D230.activeCharacterSelectWindowPos[playerIndex];
		struct TransitionMeta *tMeta = &D230.characterSelectTransitionMeta[playerIndex];

		struct PushBuffer *pb = &gGT->pushBuffer[playerIndex];
		pb->rect.x = windowPos->x + tMeta[MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST].currX;
		pb->rect.y = windowPos->y + tMeta[MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST].currY;
		pb->rect.w = D230.characterSelectWindowWidth;
		pb->rect.h = D230.characterSelectWindowHeight;

		// negative StartX
		if ((s16)pb->rect.x < 0)
		{
			pb->rect.w -= pb->rect.x;
			pb->rect.x = 0;
			if ((s16)pb->rect.w < 0)
			{
				pb->rect.w = 0;
			}
		}

		// negative StartY
		if ((s16)pb->rect.y < 0)
		{
			pb->rect.h -= pb->rect.y;
			pb->rect.y = 0;
			if ((s16)pb->rect.h < 0)
			{
				pb->rect.h = 0;
			}
		}

		// startX + sizeX out of bounds
		if ((MM_CHARACTER_SELECT_SCREEN_W < pb->rect.x + pb->rect.w) && (pb->rect.w = MM_CHARACTER_SELECT_SCREEN_W - pb->rect.x, pb->rect.w < 0))
		{
			pb->rect.x = MM_CHARACTER_SELECT_SCREEN_W;
			pb->rect.w = 0;

#ifdef CTR_NATIVE
			// NOTE(aalhendi): Native renderer guard; retail leaves w at zero.
			pb->rect.w = 1;
#endif
		}

		// startY + sizeY out of bounds
		if ((MM_CHARACTER_SELECT_SCREEN_H < pb->rect.y + pb->rect.h) && (pb->rect.h = MM_CHARACTER_SELECT_SCREEN_H - pb->rect.y, pb->rect.h < 0))
		{
			pb->rect.y = MM_CHARACTER_SELECT_SCREEN_H;
			pb->rect.h = 0;

#ifdef CTR_NATIVE
			// NOTE(aalhendi): Native renderer guard; retail leaves h at zero.
			pb->rect.h = 1;
#endif
		}

		// distanceToScreen
		pb->distanceToScreen_CURR = MM_CHARACTER_SELECT_DISTANCE_TO_SCREEN;
		pb->distanceToScreen_PREV = MM_CHARACTER_SELECT_DISTANCE_TO_SCREEN;

		// pushBuffer pos and rot to all zero
		pb->pos.x = 0;
		pb->pos.y = 0;
		pb->pos.z = 0;
		pb->rot.x = 0;
		pb->rot.y = 0;
		pb->rot.z = 0;

		// player -> instance
		struct Instance *driverInst = gGT->drivers[playerIndex]->instSelf;

		// Make Visible
		driverInst->flags &= ~HIDE_MODEL;

		// if driver is off-screen
		if ((gGT->numPlyrNextGame <= playerIndex) || (boolShowDrivers == 0))
		{
			// invisible
			driverInst->flags |= HIDE_MODEL;
		}

		struct InstDrawPerPlayer *idpp = INST_GETIDPP(driverInst);

		// clear pushBuffer in every InstDrawPerPlayer
		idpp[0].pushBuffer = 0;
		idpp[1].pushBuffer = 0;
		idpp[2].pushBuffer = 0;
		idpp[3].pushBuffer = 0;

		// set pushBuffer in InstDrawPerPlayer,
		// so that each camera can only see one driver
		idpp[playerIndex].pushBuffer = pb;

		s16 *currCharacterID = &D230.characterSelectPlayerState.currentCharacterID[playerIndex];

		driverInst->animFrame = 0;
		driverInst->vertSplit = 0;

#if defined(CTR_NATIVE)
		struct Model *model =
			NativeRacerVisual_ResolveModel((int)*currCharacterID);
#else
		struct Model *model =
			MM_Characters_GetModelByName(
				MM_Characters_RosterGetPreviewModelName((int)*currCharacterID));
		if (model == NULL)
		{
			int fallbackID =
				MM_Characters_RosterGetFallbackRetailID((int)*currCharacterID);
			model =
				MM_Characters_GetModelByName(
					data.MetaDataCharacters[fallbackID].name_Debug);
		}
#endif

#if defined(CTR_NATIVE)
		/*
		 * Hide CTR's sprite wheels only when the selected model really is an
		 * external OBJ that supplies its own wheels. If OBJ loading failed and
		 * we fell back to retail, keep retail wheels visible.
		 */
		if ((NativeObj_GetMesh(model) != NULL) &&
			!CharacterRegistry_IsModularBody((int)*currCharacterID) &&
			!CharacterRegistry_HasWheels((int)*currCharacterID))
			gGT->drivers[playerIndex]->wheelSize = 0;
		else
			gGT->drivers[playerIndex]->wheelSize = 0xccc;
#endif

		// set modelPtr in Instance
		driverInst->model = model;

		// CameraDC, freecam mode
		gGT->cameraDC[playerIndex].cameraMode = CAMERA_MODE_FREECAM;

		// Set position of player
		driverInst->matrix.t[0] = D230.characterSelectDriverModel.pos.x;
		driverInst->matrix.t[1] = D230.characterSelectDriverModel.pos.y;
		driverInst->matrix.t[2] = D230.characterSelectDriverModel.pos.z;

		s16 *moveTimer = &D230.characterSelectModelMoveTimer[playerIndex];
		s16 nextMoveTimer = *moveTimer;
#if defined(CTR_NATIVE)
		{
			const int moveTicks = Platform_GetLegacy30HzTicks();
			if (moveTicks > 0)
			{
				nextMoveTimer = (s16)((*moveTimer > moveTicks) ? (*moveTimer - moveTicks) : 0);
			}
		}
#else
		nextMoveTimer = *moveTimer + -1;
#endif

		// If no transition between players
		if (*moveTimer == 0)
		{
			// compare to character ID
			if (*currCharacterID != data.characterIDs[playerIndex])
			{
				*moveTimer = D230.characterSelectDriverModel.moveFrames << 1;
				D230.characterSelectPlayerState.desiredCharacterID[playerIndex] = data.characterIDs[playerIndex];
			}
		}

		// if transition between players
		else
		{
			// get timer
			*moveTimer = nextMoveTimer;

			s32 slideDirection;
			s32 slideOffset;

			// if timer is before midpoint
			if ((int)nextMoveTimer < (int)D230.characterSelectDriverModel.moveFrames)
			{
				// make driver fly off screen
				*currCharacterID = D230.characterSelectPlayerState.desiredCharacterID[playerIndex];
				s32 moveFrameScale = RaceFlag_MoveModels((int)nextMoveTimer, (int)D230.characterSelectDriverModel.moveFrames);

				// direction moving
				slideDirection = -D230.characterSelectPlayerState.modelMoveDir[playerIndex];
				slideOffset = moveFrameScale * D230.characterSelectDriverModel.slideDistance >> MM_CHARACTER_SELECT_MODEL_MOVE_FP_SHIFT;
			}

			// if timer is after midpoint
			else
			{
				// make new driver fly on screen
				s32 moveFrameScale =
				    RaceFlag_MoveModels((int)nextMoveTimer - (int)D230.characterSelectDriverModel.moveFrames, (int)D230.characterSelectDriverModel.moveFrames);

				// direction moving
				slideDirection = D230.characterSelectPlayerState.modelMoveDir[playerIndex];
				slideOffset = (MM_CHARACTER_SELECT_MODEL_MOVE_FP - moveFrameScale) * (int)D230.characterSelectDriverModel.slideDistance >>
				              MM_CHARACTER_SELECT_MODEL_MOVE_FP_SHIFT;
			}

			driverInst->matrix.t[0] += slideDirection * slideOffset;
		}

		// driver rotation
		rot.x = D230.characterSelectDriverModel.rot.x;
		rot.y = D230.characterSelectDriverModel.rot.y + D230.characterSelectPlayerState.angle[playerIndex];
		rot.z = D230.characterSelectDriverModel.rot.z;

		ConvertRotToMatrix(&driverInst->matrix, &rot);
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800ae0bc-0x800ae274.
void MM_Characters_SetMenuLayout(void)
{
	b32 expandRoster = false;

	// By default, draw "Select character" in 3P menu
	D230.characterSelectRosterExpanded = 0;

	s32 numPlyrNextGame = sdata->gGT->numPlyrNextGame;
	s32 layoutIndex = numPlyrNextGame - 1;

	// Loop through bottom characters,
	// if any are unlocked, use expanded
	for (s32 iconIndex = MM_CHARACTER_SELECT_EXPANSION_ICON_FIRST; iconIndex < MM_CHARACTER_SELECT_ICON_COUNT; iconIndex++)
	{
		// OG game code
		u16 unlocked = D230.characterSelectMeta1P2P[iconIndex].unlockFlags;

		if (CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlocked))
		{
			expandRoster = true;
			break;
		}
	}

#if defined(CTR_NATIVE)
	if (numPlyrNextGame == 1)
	{
		/*
		 * Native character-mod menu prototype:
		 * dedicated 4:3 layout: large preview on the left,
		 * reusable 4x5 roster grid on the right.
		 */
		expandRoster = true;
		layoutIndex = 0;
	}
	else if (numPlyrNextGame == 2)
	{
		expandRoster = true;
		layoutIndex = 1;
	}
	else if (numPlyrNextGame == 3)
	{
		expandRoster = true;
		layoutIndex = 2;
	}
	else if (numPlyrNextGame == 4)
	{
		/* Native 4P: compact preview grid left, paged roster right. */
		expandRoster = true;
		layoutIndex = 3;
	}
	else
#endif
	if (
	    // if 1P2P (0 or 1)
	    (layoutIndex < MM_CHARACTER_SELECT_FULL_LAYOUT_COUNT) &&

	    // if very few characters are unlocked
	    (!expandRoster))
	{
		// layout [4] and [5] for 1P2P without expansion
		layoutIndex += MM_CHARACTER_SELECT_LIMITED_LAYOUT_OFFSET;
	}

	D230.characterSelectRosterExpanded = expandRoster;

	D230.characterSelectLayoutIndex = layoutIndex;

	D230.characterSelectDriverModel.pos.y = D230.characterSelectLayout.driverPosY[layoutIndex];
	D230.characterSelectDriverModel.pos.z = D230.characterSelectLayout.driverPosZ[layoutIndex];

	D230.characterSelectWindowWidth = D230.characterSelectLayout.windowW[layoutIndex];
	D230.characterSelectWindowHeight = D230.characterSelectLayout.windowH[layoutIndex];

	D230.activeCharacterSelectWindowPos = D230.characterSelectWindowPosByLayout[layoutIndex];

	D230.activeCharacterSelectMeta = D230.characterSelectMetaByLayout[layoutIndex];

	D230.characterSelectNameTextY = D230.characterSelectLayout.textY[layoutIndex];

	D230.characterSelectTransitionMeta = D230.characterSelectTransitionByPlayerCount[numPlyrNextGame - 1];

#if defined(CTR_NATIVE)
	if (numPlyrNextGame == 1)
	{
		D230.characterSelectWindowPos[0].x = MM_CHARACTER_SELECT_PREVIEW_X;
		D230.characterSelectWindowPos[0].y = MM_CHARACTER_SELECT_PREVIEW_Y;
		D230.activeCharacterSelectWindowPos = &D230.characterSelectWindowPos[0];
		D230.characterSelectWindowWidth = MM_CHARACTER_SELECT_PREVIEW_W;
		D230.characterSelectWindowHeight = MM_CHARACTER_SELECT_PREVIEW_H;

		/* Pull the retail model a little closer inside the larger preview. */
		D230.characterSelectDriverModel.pos.y = 0x28;
		D230.characterSelectDriverModel.pos.z = 0xb0;
	}
	else if (numPlyrNextGame == 2)
	{
		/*
		 * 2P 4:3 layout:
		 * two compact previews stacked on the left, shared roster right.
		 */
		D230.characterSelectWindowPos[2].x = 0x10;
		D230.characterSelectWindowPos[2].y = 0x0c;
		D230.characterSelectWindowPos[3].x = 0x10;
		D230.characterSelectWindowPos[3].y = 0x74;

		D230.activeCharacterSelectWindowPos =
			&D230.characterSelectWindowPos[2];

		D230.characterSelectWindowWidth = 0xe8;
		D230.characterSelectWindowHeight = 0x4c;
		D230.characterSelectNameTextY = 0x50;

		D230.characterSelectDriverModel.pos.y = 0x28;
		D230.characterSelectDriverModel.pos.z = 0xd0;
	}
	else if (numPlyrNextGame == 3)
	{
		/*
		 * Compact 3P stack on the left.
		 * Keep clear of the FPS counter and leave breathing room
		 * between all three previews.
		 */
		D230.characterSelectWindowPos[6].x = 0x42;
		D230.characterSelectWindowPos[6].y = 0x12;
		D230.characterSelectWindowPos[7].x = 0x42;
		D230.characterSelectWindowPos[7].y = 0x53;
		D230.characterSelectWindowPos[8].x = 0x42;
		D230.characterSelectWindowPos[8].y = 0x94;

		D230.activeCharacterSelectWindowPos =
			&D230.characterSelectWindowPos[6];

		D230.characterSelectWindowWidth = 0x98;
		D230.characterSelectWindowHeight = 0x34;
		D230.characterSelectNameTextY = 0x32;

		/* Pull the model farther back so kart + driver fit comfortably. */
		D230.characterSelectDriverModel.pos.y = 0x28;
		D230.characterSelectDriverModel.pos.z = 0x120;
	}
	else if (numPlyrNextGame == 4)
	{
		/*
		 * Four previews stacked on the left, following the same visual
		 * language as 2P/3P. The 4x5 roster owns the right side.
		 */
		D230.characterSelectWindowPos[9].x = 0x38;
		D230.characterSelectWindowPos[9].y = 0x0c;
		D230.characterSelectWindowPos[10].x = 0x38;
		D230.characterSelectWindowPos[10].y = 0x3e;
		D230.characterSelectWindowPos[11].x = 0x38;
		D230.characterSelectWindowPos[11].y = 0x70;
		D230.characterSelectWindowPos[12].x = 0x38;
		D230.characterSelectWindowPos[12].y = 0xa2;

		D230.activeCharacterSelectWindowPos =
			&D230.characterSelectWindowPos[9];

		/* Wider than the old 2x2 previews, but low enough for all four. */
		D230.characterSelectWindowWidth = 0xb8;
		D230.characterSelectWindowHeight = 0x28;
		D230.characterSelectNameTextY = 0x25;

		/* Pull kart + driver farther back rather than shrinking the window. */
		D230.characterSelectDriverModel.pos.y = 0x28;
		D230.characterSelectDriverModel.pos.z = 0x190;
	}
#endif

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae274-0x800ae2c0.
void MM_Characters_BackupIDs(void)
{
	for (s32 driverIndex = 0; driverIndex < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; driverIndex++)
	{
		// make a backup when you leave character selection,
		// backup is restored when you go back to selection
		sdata->characterIDs_backup[driverIndex] = data.characterIDs[driverIndex];
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800ae2c0-0x800ae464.
void MM_Characters_PreventOverlap(void)
{
	struct GameTracker *gGT = sdata->gGT;

#if defined(CTR_NATIVE)
	if (MM_Characters_DuplicateSelectionsAllowed())
		return;
#endif

	s8 availableDefaultCharacters[MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT];

	// default 0,1,2,3,4,5,6,7
	CTR_WriteU32LE((u8 *)&availableDefaultCharacters[0], R230.packedDefaultCharacterIDWords[0]);
	CTR_WriteU32LE((u8 *)&availableDefaultCharacters[4], R230.packedDefaultCharacterIDWords[1]);

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		// get character ID
		s32 characterID = data.characterIDs[playerIndex];

		// if not a secret character
		if (characterID < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT)
		{
			// character is taken
			availableDefaultCharacters[characterID] = -1;
		}
	}

	for (s32 playerIndex = 1; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		for (s32 previousPlayer = 0; previousPlayer < playerIndex; previousPlayer++)
		{
			// if two characters are the same
			if (data.characterIDs[playerIndex] == data.characterIDs[previousPlayer])
			{
				// look for a new character
				for (s32 defaultIndex = 0; defaultIndex < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; defaultIndex++)
				{
					// get default character
					s8 *defaultCharacter = &availableDefaultCharacters[defaultIndex];
					s8 freeCharacter = *defaultCharacter;

					// if character is not taken
					if (-1 < freeCharacter)
					{
						// assign free character
						data.characterIDs[playerIndex] = (s16)freeCharacter;

						// character is now taken
						*defaultCharacter = -1;

						break;
					}
				}
			}
		}
	}
	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 overlay 230 0x800ae464-0x800ae6b0.
void MM_Characters_RestoreIDs(void)
{
	struct GameTracker *gGT = sdata->gGT;

	// erase select bits
	sdata->characterSelectFlags = 0;
	D230.characterSelectTransitionFrame = MM_CHARACTER_SELECT_TRANSITION_FRAMES;
	D230.characterSelectMenuState = ENTERING_MENU;

	// This uses 80086e84, which controls character IDs
	for (s32 driverIndex = 0; driverIndex < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; driverIndex++)
	{
		// set character ID to the last ID you entered
		data.characterIDs[driverIndex] = sdata->characterIDs_backup[driverIndex];
	}

	MM_Characters_SetMenuLayout();

#if defined(CTR_NATIVE)
	VehiclePartRegistry_Init();
	RacerLoadout_ResetAll();

	if (gGT->numPlyrNextGame == 1)
	{
		MM_Characters_RosterRestoreSelection();
	}
#endif

	for (s32 iconIndex = 0; iconIndex < MM_CHARACTER_SELECT_ICON_COUNT; iconIndex++)
	{
		// would not need this if CSM was sorted
		// by order of character ID

		// Basically sets them to 0, 1, 2, 3, 4... up to 0xE,
		// setting Oxide's manually to 0xF is needed to make his icon appear

		D230.characterMenuID[(s32)D230.activeCharacterSelectMeta[iconIndex].characterID] = iconIndex;
	}

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		// Determine if this icon is unlocked (and drawing)

		// get character ID
		s16 *currID = &data.characterIDs[playerIndex];

#if defined(CTR_NATIVE)
		/* Dynamic IDs are not valid indices into the retail 0..15 menu table.
		 * If a mod disappeared since the backup was made, fall back safely.
		 * Valid mods use CharacterRegistry for availability instead of retail
		 * unlock flags. */
		if (CharacterRegistry_GetByID((int)*currID) == NULL)
			*currID = CRASH_BANDICOOT;
		if (*currID > NITROS_OXIDE)
			continue;
#endif

		// get unlock requirement for this character
		s16 unlocked = D230.activeCharacterSelectMeta[(s32)*currID].unlockFlags;

		if (
		    // If Icon has an unlock requirement
		    (unlocked != MM_CHARACTER_UNLOCK_ALWAYS) &&

		    // If Character is Locked
		    !CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlocked))
		{
			// change character to Crash
			*currID = CRASH_BANDICOOT;
		}
	}

	MM_Characters_PreventOverlap();

#if defined(CTR_NATIVE)
	if (gGT->numPlyrNextGame == 2)
	{
		MM_Characters_Roster2PRestoreSelection();
	}
	else if (gGT->numPlyrNextGame == 3)
	{
		MM_Characters_Roster3PRestoreSelection();
	}
	else if (gGT->numPlyrNextGame == 4)
	{
		MM_Characters_Roster4PRestoreSelection();
	}
#endif

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		// set name string ID to the character ID of each player.
		// The string will only draw if both these variables match
		D230.characterSelectPlayerState.currentCharacterID[playerIndex] = data.characterIDs[playerIndex];
		D230.characterSelectPlayerState.desiredCharacterID[playerIndex] = data.characterIDs[playerIndex];

		// something to do with transitioning between icons
		D230.characterSelectModelMoveTimer[playerIndex] = 0;

		// rotation of each driver, 90 degrees difference
		D230.characterSelectPlayerState.angle[playerIndex] = (playerIndex * MM_CHARACTER_SELECT_ANGLE_STEP) + MM_CHARACTER_SELECT_ANGLE_OFFSET;
	}

	MM_Characters_DrawWindows(0);
	return;
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 230 0x800ae6b0-0x800ae74c.
void MM_Characters_HideDrivers(void)
{
	struct GameTracker *gGT = sdata->gGT;

	for (s32 playerIndex = 0; playerIndex < MM_CHARACTER_SELECT_MAX_PLAYERS; playerIndex++)
	{
		PushBuffer_Init(&gGT->pushBuffer[playerIndex], 0, 1);

		gGT->drivers[playerIndex]->instSelf->flags |= HIDE_MODEL;
	}

	return;
}

void MM_Characters_MenuProc(struct RectMenu *unused)
{
	(void)unused;
	b32 candidateInUseByOtherPlayer;
	b32 deadEndCandidateAvailable;
	s16 nextIcon;
	int intermediateIcon;
	s16 previousCandidateIcon;
	int nextIconCopy;
	s16 alternateIcon;
	s16 iconPerPlayer[4];

	RECT drawRect;

	s16 hitNavigationDeadEnd;

	int direction;

	struct GameTracker *gGT = sdata->gGT;
	int legacyTicks = 1;
#if defined(CTR_NATIVE)
	legacyTicks = Platform_GetLegacy30HzTicks();
#endif

	u32 *ot = gGT->backBuffer->otMem.uiOT;

	for (s32 playerIndex = 0; playerIndex < MM_CHARACTER_SELECT_MAX_PLAYERS; playerIndex++)
	{
#if defined(CTR_NATIVE)
		/*
		 * characterMenuID is a retail 0..15 lookup. Native mod IDs are
		 * logical roster IDs, so never use them directly as table indices.
		 */
		int menuCharacterID =
			CharacterRegistry_GetDriverPackID(data.characterIDs[playerIndex]);
		iconPerPlayer[playerIndex] = D230.characterMenuID[menuCharacterID];
#else
		iconPerPlayer[playerIndex] = D230.characterMenuID[data.characterIDs[playerIndex]];
#endif
	}

	// if menu is not in focus
	if (D230.characterSelectMenuState != IN_MENU)
	{
		MM_TransitionInOut(D230.characterSelectTransitionMeta, (int)D230.characterSelectTransitionFrame, MM_CHARACTER_SELECT_TRANSITION_STEP);
	}

	MM_Characters_SetMenuLayout();
	MM_Characters_DrawWindows(1);

	// if transitioning in
	if (D230.characterSelectMenuState == ENTERING_MENU)
	{
		// if no more frames
		if (D230.characterSelectTransitionFrame == 0)
		{
			// menu is now in focus
			D230.characterSelectMenuState = IN_MENU;
		}
		else if (legacyTicks > 0)
		{
			D230.characterSelectTransitionFrame -= legacyTicks;
			if (D230.characterSelectTransitionFrame < 0) D230.characterSelectTransitionFrame = 0;
		}
	}

	// if transitioning out
	if (D230.characterSelectMenuState == EXITING_MENU)
	{
		// increase retail content frame
		D230.characterSelectTransitionFrame += legacyTicks;

		// if more than 12 frames
		if (D230.characterSelectTransitionFrame > MM_CHARACTER_SELECT_TRANSITION_FRAMES)
		{
#if defined(CTR_NATIVE)
			if (NativeHangout_IsCustomizationOpen())
			{
				NativeHangout_RequestCustomizationExitTransition(
					D230.characterSelectExitsForward != 0);
				return;
			}
#endif

			// Make a backup of the characters
			// you selected in character selection screen
			MM_Characters_BackupIDs();

			// if returning to main menu
			if (D230.characterSelectExitsForward == 0)
			{
				MM_JumpTo_Title_Returning();
				MM_Characters_HideDrivers();
				return;
			}

			MM_Characters_HideDrivers();

			// if you are in a cup
			if ((gGT->gameMode2 & CUP_ANY_KIND) != 0)
			{
				sdata->ptrDesiredMenu = &D230.menuCupSelect;
				MM_CupSelect_Init();
				return;
			}

			// if going to track selection
			sdata->ptrDesiredMenu = &D230.menuTrackSelect;
			MM_TrackSelect_Init();
			return;
		}
	}

#if defined(CTR_NATIVE)
	if (gGT->numPlyrNextGame == 1)
	{
		MM_Characters_RosterMenuProc(gGT, ot);
		if (NativeHangout_IsCustomizationOpen())
			MM_Characters_HangoutPreviewTick();
		return;
	}
	else if (gGT->numPlyrNextGame == 2)
	{
		MM_Characters_Roster2PMenuProc(gGT, ot);
		return;
	}
	else if (gGT->numPlyrNextGame == 3)
	{
		MM_Characters_Roster3PMenuProc(gGT, ot);
		return;
	}
	else if (gGT->numPlyrNextGame == 4)
	{
		MM_Characters_Roster4PMenuProc(gGT, ot);
		return;
	}
#endif

	int posX = D230.characterSelectTransitionMeta[MM_CHARACTER_SELECT_TITLE_TRANSITION_INDEX].currX;
	int posY = D230.characterSelectTransitionMeta[MM_CHARACTER_SELECT_TITLE_TRANSITION_INDEX].currY;

	u32 characterSelectType;
	char *characterSelectString;
	switch (D230.characterSelectLayoutIndex)
	{
	// 3P character selection
	case MM_CHARACTER_SELECT_LAYOUT_3P:

		// If you have a lot of characters unlocked, do not draw SELECT CHARACTER
		if (D230.characterSelectRosterExpanded)
		{
			goto dontDrawSelectCharacter;
		}

		DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_CHARACTER_SELECT], posX + MM_CHARACTER_SELECT_3P_TITLE_X, posY + MM_CHARACTER_SELECT_3P_SELECT_Y,
		                   FONT_BIG, (JUSTIFY_CENTER | ORANGE));
		characterSelectType = FONT_BIG;

		characterSelectString = sdata->lngStrings[LNG_CHARACTER];

		posX = posX + MM_CHARACTER_SELECT_3P_TITLE_X;
		posY = posY + MM_CHARACTER_SELECT_3P_CHARACTER_Y;
		break;

	// 4P character selection
	case MM_CHARACTER_SELECT_LAYOUT_4P:

		// If Fake Crash is unlocked, do not draw "Select Character"
		if (sdata->gameProgress.unlockFlags & UNLOCK_FAKE_CRASH)
		{
			goto dontDrawSelectCharacter;
		}

		DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_CHARACTER_SELECT], posX + MM_CHARACTER_SELECT_4P_TITLE_X, posY + MM_CHARACTER_SELECT_4P_SELECT_Y,
		                   FONT_CREDITS, (JUSTIFY_CENTER | ORANGE));
		characterSelectType = FONT_CREDITS;

		characterSelectString = sdata->lngStrings[LNG_CHARACTER];

		posX = posX + MM_CHARACTER_SELECT_4P_TITLE_X;
		posY = posY + MM_CHARACTER_SELECT_4P_CHARACTER_Y;
		break;

	// If you are in 1P or 2P character selection,
	// when you do NOT have a lot of characters selected
	case MM_CHARACTER_SELECT_LAYOUT_1P_LIMITED:
	case MM_CHARACTER_SELECT_LAYOUT_2P_LIMITED:
		characterSelectType = FONT_BIG;

		characterSelectString = sdata->lngStrings[LNG_SELECT_CHARACTER];

		posX = posX + MM_CHARACTER_SELECT_LIMITED_TITLE_X;
		posY = posY + MM_CHARACTER_SELECT_LIMITED_TITLE_Y;
		break;

	default:
		goto dontDrawSelectCharacter;
	}

	// Draw String
	DecalFont_DrawLine(characterSelectString, posX, posY, characterSelectType, (JUSTIFY_CENTER | ORANGE));

dontDrawSelectCharacter:

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		u16 playerSelectFlag = (u16)(1 << playerIndex);
		s16 currentIcon = iconPerPlayer[playerIndex];
		s16 candidateIcon = currentIcon;
		b32 playerSelectedBeforeInput = (((int)(s16)sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

		Color playerColor;
		MM_Characters_AnimateColors((u8 *)&playerColor, playerIndex, (int)(s16)(sdata->characterSelectFlags & playerSelectFlag));

		struct CharacterSelectMeta *preInputCharacterMeta = &D230.activeCharacterSelectMeta[currentIcon];
		u32 button = sdata->buttonTapPerPlayer[playerIndex];

		if ((D230.characterSelectMenuState == IN_MENU) &&
		    // If you press the D-Pad, or Cross, Square, Triangle, Circle
		    ((button & (MM_CHARACTER_SELECT_INPUT_DPAD | MM_CHARACTER_SELECT_INPUT_MENU)) != 0))
		{
			// if character has not been selected by this player
			if (!playerSelectedBeforeInput)
			{
				// If you pressed any of the D-pad buttons
				if ((button & MM_CHARACTER_SELECT_INPUT_DPAD) != 0)
				{
					hitNavigationDeadEnd = 0;

					// If you do not press Up
					if ((button & BTN_UP) == 0)
					{
						// If you do not press Down
						if ((button & BTN_DOWN) == 0)
						{
							// This must be if you press Left,
							// because the variable will change
							// if it is anything that isn't Left

							// Left
							direction = CHARACTER_SELECT_DIR_LEFT;

							// If you press Left
							if ((button & BTN_LEFT) != 0)
							{
								goto LAB_800aec08;
							}

							// At this point, you must have pressed Right

							// Right
							direction = CHARACTER_SELECT_DIR_RIGHT;

							// Move down character selection list
							D230.characterSelectPlayerState.modelMoveDir[playerIndex] = MM_CHARACTER_SELECT_MODEL_MOVE_NEXT;
						}

						// If you pressed Down
						else
						{
							// Down
							direction = CHARACTER_SELECT_DIR_DOWN;

							// Move down character selection list
							D230.characterSelectPlayerState.modelMoveDir[playerIndex] = MM_CHARACTER_SELECT_MODEL_MOVE_NEXT;
						}
					}

					// If you pressed Up
					else
					{
						// Up
						direction = CHARACTER_SELECT_DIR_UP;
					LAB_800aec08:
						// If you press Up or Left

						// Move up character selection list
						D230.characterSelectPlayerState.modelMoveDir[playerIndex] = MM_CHARACTER_SELECT_MODEL_MOVE_PREV;
					}

					previousCandidateIcon = candidateIcon;
					do
					{
						candidateIcon = MM_Characters_GetNextDriver(direction, previousCandidateIcon);
						alternateIcon = candidateIcon;

						if (candidateIcon == previousCandidateIcon)
						{
							hitNavigationDeadEnd = 1;
							nextIcon = MM_Characters_GetNextDriver(direction, (int)(s16)currentIcon);
							nextIconCopy = (int)nextIcon;
							candidateIcon = MM_Characters_GetNextDriver(D230.characterSelectFallbackDirection1[direction], nextIconCopy);
							intermediateIcon = (int)(s16)candidateIcon;

							if ((((intermediateIcon == alternateIcon) || (nextIconCopy == alternateIcon)) || (nextIconCopy == intermediateIcon)) ||
							    MM_Characters_boolIsInvalid(iconPerPlayer, intermediateIcon, playerIndex))
							{
								nextIcon = MM_Characters_GetNextDriver(D230.characterSelectFallbackDirection1[direction], (int)(s16)currentIcon);
								intermediateIcon = (int)nextIcon;
								candidateIcon = MM_Characters_GetNextDriver(direction, intermediateIcon);
								alternateIcon = (int)(s16)candidateIcon;

								if (((alternateIcon == previousCandidateIcon) || (intermediateIcon == previousCandidateIcon)) ||
								    ((intermediateIcon == alternateIcon || MM_Characters_boolIsInvalid(iconPerPlayer, alternateIcon, playerIndex))))
								{
									nextIcon = MM_Characters_GetNextDriver(direction, (int)(s16)currentIcon);
									intermediateIcon = (int)nextIcon;
									candidateIcon = MM_Characters_GetNextDriver(D230.characterSelectFallbackDirection2[direction], intermediateIcon);
									alternateIcon = (int)(s16)candidateIcon;

									if (((alternateIcon == previousCandidateIcon) || (intermediateIcon == previousCandidateIcon)) ||
									    ((intermediateIcon == alternateIcon || MM_Characters_boolIsInvalid(iconPerPlayer, alternateIcon, playerIndex))))
									{
										nextIcon = MM_Characters_GetNextDriver(D230.characterSelectFallbackDirection2[direction], (int)(s16)currentIcon);
										intermediateIcon = (int)nextIcon;
										candidateIcon = MM_Characters_GetNextDriver(direction, intermediateIcon);
										alternateIcon = (int)(s16)candidateIcon;

										if ((((alternateIcon == previousCandidateIcon) || (intermediateIcon == previousCandidateIcon)) ||
										     (intermediateIcon == alternateIcon)) ||
										    MM_Characters_boolIsInvalid(iconPerPlayer, alternateIcon, playerIndex))
										{
											candidateIcon = (u32)currentIcon;
										}
									}
								}
							}
						}
						candidateInUseByOtherPlayer = false;

						for (s32 otherPlayerIndex = 0; otherPlayerIndex < gGT->numPlyrNextGame; otherPlayerIndex++)
						{
							if ((otherPlayerIndex != playerIndex) && ((s16)candidateIcon == iconPerPlayer[otherPlayerIndex]))
							{
								candidateInUseByOtherPlayer = true;
								break;
							}
						}

						if (previousCandidateIcon << 0x10 != candidateIcon << 0x10)
						{
							// Play sound
							// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800aeeb8-0x800aeecc for character cursor-change SFX.
							OtherFX_Play(0, 1);
						}
						if (hitNavigationDeadEnd != 0)
						{
							deadEndCandidateAvailable = !candidateInUseByOtherPlayer;
							candidateInUseByOtherPlayer = false;
							if (deadEndCandidateAvailable)
							{
								break;
							}
							candidateIcon = (u32)currentIcon;
						}
						previousCandidateIcon = candidateIcon;
					} while (candidateInUseByOtherPlayer);
				}
				currentIcon = (u16)candidateIcon;

				for (s32 otherPlayerIndex = 0; otherPlayerIndex < gGT->numPlyrNextGame; otherPlayerIndex++)
				{
					if ((otherPlayerIndex != playerIndex) && ((s16)candidateIcon == iconPerPlayer[otherPlayerIndex]))
					{
						candidateIcon = (u32)(u16)iconPerPlayer[playerIndex];
					}
					currentIcon = (u16)candidateIcon;
				}

				// If this player pressed Cross or Circle
				if (((sdata->buttonTapPerPlayer)[playerIndex] & MM_CHARACTER_SELECT_INPUT_CONFIRM) != 0)
				{
					// this player has now selected a character
					sdata->characterSelectFlags = sdata->characterSelectFlags | (u16)(1 << playerIndex);

					u8 numPlyrNextGame = gGT->numPlyrNextGame;

					// Play sound
					// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800aefa4-0x800aefe4 for character confirm SFX.
					OtherFX_Play(1, 1);

					// if all players have selected their characters
					if ((int)(s16)sdata->characterSelectFlags == (1 << numPlyrNextGame) - 1)
					{
						// exit toward cup or track selection
						D230.characterSelectExitsForward = 1;
						D230.characterSelectMenuState = EXITING_MENU;
					}
				}

				if (
				    // if this is the first iteration of the loop
				    ((playerIndex & 0xffff) == 0) &&

				    // if you press Square or Triangle
				    ((sdata->buttonTapPerPlayer[0] & MM_CHARACTER_SELECT_INPUT_BACK) != 0))
				{
					// return to main menu
					D230.characterSelectExitsForward = 0;
					D230.characterSelectMenuState = EXITING_MENU;

					// Play sound
					// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800af01c-0x800af054 for character-select back SFX.
					OtherFX_Play(2, 1);
				}
			}
			else
			{
				// if you press Square or Triangle
				if ((button & MM_CHARACTER_SELECT_INPUT_BACK) != 0)
				{
					// Play sound
					// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800af060-0x800af074 for character deselect SFX.
					OtherFX_Play(2, 1);

					// this player has de-selected their character
					sdata->characterSelectFlags = sdata->characterSelectFlags & ~playerSelectFlag;
				}
			}

			// clear input
			sdata->buttonTapPerPlayer[playerIndex] = 0;
		}

		iconPerPlayer[playerIndex] = currentIcon;

		// transition of each icon
		struct TransitionMeta *currentIconTransition = &D230.characterSelectTransitionMeta[currentIcon];

		// if player has not selected a character
		b32 playerSelectedAfterInput = ((sdata->characterSelectFlags >> playerIndex) & 1U) != 0;
		Color outlineColor;
		if (!playerSelectedAfterInput)
		{
			// draw string
			// "1", "2", "3", "4", above the character icon
			DecalFont_DrawLine(D230.playerNumberStrings[playerIndex], currentIconTransition->currX + (u32)preInputCharacterMeta->posX - 6,
			                   currentIconTransition->currY + (u32)preInputCharacterMeta->posY - 3, FONT_BIG, WHITE);
			outlineColor = playerColor;
		}
		else
		{
			outlineColor = D230.characterSelect_Outline;
		}

		drawRect.x = currentIconTransition->currX + preInputCharacterMeta->posX;
		drawRect.y = currentIconTransition->currY + preInputCharacterMeta->posY;
		drawRect.w = MM_CHARACTER_SELECT_ICON_RECT_W;
		drawRect.h = MM_CHARACTER_SELECT_ICON_RECT_H;

		RECTMENU_DrawOuterRect_HighLevel(&drawRect, outlineColor, 0, ot);
	}

	MM_Characters_PreventOverlap();

	struct CharacterSelectMeta *iconDrawMeta = D230.activeCharacterSelectMeta;

	// loop through character icons
	for (s32 iconIndex = 0; iconIndex < MM_CHARACTER_SELECT_ICON_COUNT; iconIndex++)
	{
		s16 unlockRequirement = iconDrawMeta->unlockFlags;
		if (
		    // If Icon is unlocked by default,
		    (unlockRequirement == MM_CHARACTER_UNLOCK_ALWAYS) ||

		    // if character is unlocked
		    // from the global unlock bitfield
		    // also the variable written by cheats
		    CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlockRequirement))
		{
			Color iconColor = D230.characterSelect_NeutralColor;

			for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
			{
				b32 playerSelected = (((int)(s16)sdata->characterSelectFlags >> (playerIndex & 0x1fU)) & 1U) != 0;
				if (((s16)iconIndex == iconPerPlayer[playerIndex]) &&

				    // if player selected a character
				    playerSelected)
				{
					iconColor = D230.characterSelect_ChosenColor;
				}
			}

			struct TransitionMeta *iconTransition = &D230.characterSelectTransitionMeta[iconIndex];

			RECTMENU_DrawPolyGT4(gGT->ptrIcons[data.MetaDataCharacters[iconDrawMeta->characterID].iconID],
			                     iconTransition->currX + iconDrawMeta->posX + MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_X,
			                     iconTransition->currY + iconDrawMeta->posY + MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_Y,

			                     &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT,

			                     iconColor.self, iconColor.self, iconColor.self, iconColor.self, TRANS_50_DECAL, FP(1.0));
		}

		iconDrawMeta++;
	}

	// reset
	struct CharacterSelectMeta *activeCharacterSelectMeta = D230.activeCharacterSelectMeta;

	for (s32 playerIndex = 0; playerIndex < MM_CHARACTER_SELECT_MAX_PLAYERS; playerIndex++)
	{
		data.characterIDs[playerIndex] = activeCharacterSelectMeta[(int)iconPerPlayer[playerIndex]].characterID;
	}

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		s16 playerIcon = iconPerPlayer[playerIndex];
		activeCharacterSelectMeta = &D230.activeCharacterSelectMeta[playerIcon];
		b32 playerSelected = (((int)(s16)sdata->characterSelectFlags >> playerIndex) & 1U) != 0;

		// if player has not selected a character
		if (!playerSelected)
		{
			Color animatedColor;
			u16 selectedPlayerFlag = (u16)(1 << playerIndex);
			MM_Characters_AnimateColors((u8 *)&animatedColor, playerIndex,

			                            // flags of which characters are selected
			                            (int)(s16)(sdata->characterSelectFlags & selectedPlayerFlag));

			animatedColor.r = (u8)((int)((u32)animatedColor.r << 2) / 5);
			animatedColor.g = (u8)((int)((u32)animatedColor.g << 2) / 5);
			animatedColor.b = (u8)((int)((u32)animatedColor.b << 2) / 5);

			struct TransitionMeta *selectedIconTransition = &D230.characterSelectTransitionMeta[playerIcon];

			drawRect.x = selectedIconTransition->currX + activeCharacterSelectMeta->posX + MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_X;
			drawRect.y = selectedIconTransition->currY + activeCharacterSelectMeta->posY + MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_Y;
			drawRect.w = MM_CHARACTER_SELECT_HIGHLIGHT_W;
			drawRect.h = MM_CHARACTER_SELECT_HIGHLIGHT_H;

			// this draws the flashing blue square that appears when you highlight a character in the character select screen
			CTR_Box_DrawSolidBox(&drawRect, animatedColor, ot);
		}
		if ((D230.characterSelectModelMoveTimer[playerIndex] == 0) &&
		    (D230.characterSelectPlayerState.currentCharacterID[playerIndex] == data.characterIDs[playerIndex]))
		{
			// get number of players
			u8 numPlyrNextGame = gGT->numPlyrNextGame;

			// if number of players is 1 or 2
			u32 fontType = FONT_CREDITS;

			// if number of players is 3 or 4
			if (numPlyrNextGame >= 3)
			{
				fontType = FONT_SMALL;
			}

			struct TransitionMeta *driverWindowTransition =
			    &D230.characterSelectTransitionMeta[playerIndex + MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST];
			SVec2 *windowPos = &D230.activeCharacterSelectWindowPos[playerIndex];
			s16 nameBaseY = driverWindowTransition->currY + windowPos->y;
			s16 nameYOffset = (s16)((((u32)(numPlyrNextGame < 3) ^ 1) << 0x12) >> 0x10);
			s16 nameY;

			if ((numPlyrNextGame == 4) && (playerIndex > 1))
			{
				nameY = nameBaseY + nameYOffset + MM_CHARACTER_SELECT_4P_NAME_BOTTOM_OFFSET;
			}
			else
			{
				nameY = nameBaseY + D230.characterSelectNameTextY + nameYOffset;
			}

			// draw string
			DecalFont_DrawLine(sdata->lngStrings[data.MetaDataCharacters[activeCharacterSelectMeta->characterID].name_LNG_long],
			                   (int)driverWindowTransition->currX + windowPos->x + (int)((u32)D230.characterSelectWindowWidth >> 1), (int)nameY, fontType,
			                   (JUSTIFY_CENTER | ORANGE));
		}

		// Character preview rotation is authored in retail frames, not host frames.
#if defined(CTR_NATIVE)
		D230.characterSelectPlayerState.angle[playerIndex] +=
		    MM_CHARACTER_SELECT_SPIN_STEP * Platform_GetLegacy30HzTicks();
#else
		D230.characterSelectPlayerState.angle[playerIndex] += MM_CHARACTER_SELECT_SPIN_STEP;
#endif
	}

	// reset
	activeCharacterSelectMeta = D230.activeCharacterSelectMeta;

	// loop through all icons
	for (s32 iconIndex = 0; iconIndex < MM_CHARACTER_SELECT_ICON_COUNT; iconIndex++)
	{
		s16 unlockRequirement = activeCharacterSelectMeta[iconIndex].unlockFlags;

		if (
		    // If Icon is unlocked (from array of icons)
		    (unlockRequirement == MM_CHARACTER_UNLOCK_ALWAYS) ||

		    // if character is unlocked
		    // from the global unlock bitfield
		    // also the variable written by cheats
		    CHECK_ADV_BIT(sdata->gameProgress.unlocks, unlockRequirement))
		{
			struct TransitionMeta *iconTransition = &D230.characterSelectTransitionMeta[iconIndex];

			drawRect.x = iconTransition->currX + activeCharacterSelectMeta[iconIndex].posX;
			drawRect.y = iconTransition->currY + activeCharacterSelectMeta[iconIndex].posY;
			drawRect.w = MM_CHARACTER_SELECT_ICON_RECT_W;
			drawRect.h = MM_CHARACTER_SELECT_ICON_RECT_H;

			// Draw 2D Menu rectangle background
			RECTMENU_DrawInnerRect(&drawRect, 0, ot);
		}
	}

	SVec2 *windowPos = D230.activeCharacterSelectWindowPos;

	for (s32 playerIndex = 0; playerIndex < gGT->numPlyrNextGame; playerIndex++)
	{
		struct TransitionMeta *driverWindowTransition = &D230.characterSelectTransitionMeta[playerIndex + MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST];
		b32 playerSelected = (((int)(s16)sdata->characterSelectFlags >> playerIndex) & 1U) != 0;
		Color animatedColor;

		// store window width and height in one 4-byte variable
		drawRect.x = driverWindowTransition->currX + windowPos->x;
		drawRect.y = driverWindowTransition->currY + windowPos->y;
		drawRect.w = D230.characterSelectWindowWidth;
		drawRect.h = D230.characterSelectWindowHeight;

		MM_Characters_AnimateColors((u8 *)&animatedColor, playerIndex,

		                            // flags of which characters are selected
		                            playerSelected ^ 1);

		RECTMENU_DrawOuterRect_HighLevel(&drawRect, animatedColor, 0, ot);

		// if player selected a character
		if (playerSelected)
		{
			RECT r58;
			r58.x = drawRect.x;
			r58.y = drawRect.y;
			r58.w = drawRect.w;
			r58.h = drawRect.h;

			for (s32 borderIndex = 0; borderIndex < MM_CHARACTER_SELECT_SELECTED_BORDER_COUNT; borderIndex++)
			{
				r58.x += MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_X;
				r58.y += MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_Y;
				r58.w -= MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_W;
				r58.h -= MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_H;

				animatedColor.r = (u8)((int)((u32)animatedColor.r << 2) / 5);
				animatedColor.g = (u8)((int)((u32)animatedColor.g << 2) / 5);
				animatedColor.b = (u8)((int)((u32)animatedColor.b << 2) / 5);

				RECTMENU_DrawOuterRect_HighLevel(&r58, animatedColor, 0, ot);
			}
		}
		windowPos++;

		// Draw 2D Menu rectangle background
		RECTMENU_DrawInnerRect(&drawRect, 9, &ot[3]);

		// not screen-space anymore,
		// this is viewport-space
		drawRect.x = 0;
		drawRect.y = 0;

		RECTMENU_DrawRwdBlueRect(&drawRect, &D230.characterSelect_BlueRectColors[0], &gGT->pushBuffer[playerIndex].ptrOT[0x3ff], &gGT->backBuffer->primMem);
	}
	return;
}
