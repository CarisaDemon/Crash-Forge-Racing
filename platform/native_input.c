#include <platform/native_input.h>
#include <NativeHangout.h>
#include <platform/native_input_glyphs.h>

#include <macros.h>
#include "psx/libpad.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int Platform_DebugFreecamConsumesKey(int key);

#define NATIVE_INPUT_MAX_CONTROLLERS       PLATFORM_INPUT_PAD_COUNT
#define NATIVE_INPUT_PHYSICAL_SLOT_COUNT   2
#define NATIVE_INPUT_PAD_PACKET_BYTES      8
#define NATIVE_INPUT_MULTITAP_HEADER       2
#define NATIVE_INPUT_PAD_DIGITAL           0x41
#define NATIVE_INPUT_PAD_ANALOG            0x73
#define NATIVE_INPUT_PAD_MULTITAP          0x80
#define NATIVE_INPUT_PAD_DISCONNECT        0xff
#define NATIVE_INPUT_AXIS_DEADZONE         500
#define NATIVE_INPUT_MAP_FLAG_AXIS         0x4000
#define NATIVE_INPUT_MAP_FLAG_INVERSE      0x8000
#define NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT 0
#define NATIVE_INPUT_CONTROLS_FILE "ctr_controls.cfg"
#define NATIVE_INPUT_GUID_STRING_MAX 33
#define NATIVE_INPUT_SERIAL_MAX 128
#define NATIVE_INPUT_PATH_MAX 320
// NOTE(aalhendi): Little-endian tag `CTRI` = CTR native Input snapshot.
#define NATIVE_INPUT_STATE_MAGIC           0x49525443
#define NATIVE_INPUT_STATE_VERSION         1

// NOTE(aalhendi): Native input preserves behavior from PsyCross's
// MIT-licensed pad implementation while moving host ownership into ctr-native.
// See THIRD_PARTY_NOTICES.md.

struct NativeInputKeyboardMapping
{
	s32 id;

	s32 kc_square, kc_circle, kc_triangle, kc_cross;

	s32 kc_l1, kc_l2, kc_l3;
	s32 kc_r1, kc_r2, kc_r3;

	s32 kc_start, kc_select;

	s32 kc_dpad_left, kc_dpad_right, kc_dpad_up, kc_dpad_down;
};

struct NativeInputControllerMapping
{
	s32 id;

	s32 gc_square, gc_circle, gc_triangle, gc_cross;

	s32 gc_l1, gc_l2, gc_l3;
	s32 gc_r1, gc_r2, gc_r3;

	s32 gc_start, gc_select;

	s32 gc_dpad_left, gc_dpad_right, gc_dpad_up, gc_dpad_down;

	s32 gc_axis_left_x, gc_axis_left_y;
	s32 gc_axis_right_x, gc_axis_right_y;

	s32 gc_left_stick_left, gc_left_stick_right;
	s32 gc_left_stick_up, gc_left_stick_down;
	s32 gc_right_stick_left, gc_right_stick_right;
	s32 gc_right_stick_up, gc_right_stick_down;
};

struct NativeInputController
{
	SDL_JoystickID instanceId;
	SDL_Gamepad *controller;
	s32 analogEnabled;
	s32 switchingAnalog;
	struct PlatformInputPadSnapshot snapshot;
};

struct NativeInputPreferredGamepad
{
	char guid[NATIVE_INPUT_GUID_STRING_MAX];
	char serial[NATIVE_INPUT_SERIAL_MAX];
	char path[NATIVE_INPUT_PATH_MAX];
	char name[PLATFORM_INPUT_DEVICE_NAME_MAX];
};

struct NativeInputControllerStateSnapshot
{
	struct PlatformInputPadSnapshot snapshot;
	s32 analogEnabled;
	s32 switchingAnalog;
	s32 controllerToSlotMapping;
};

struct NativeInputStateSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	s32 keyboardControllerSlot;
	s32 lastActiveControllerSlot;
	s32 installedSnapshotsActive;
	struct PlatformInputPadSnapshot installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
	struct NativeInputControllerStateSnapshot controllers[NATIVE_INPUT_MAX_CONTROLLERS];
};

global_variable struct NativeInputControllerMapping s_controllerMappings[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable struct NativeInputKeyboardMapping s_keyboardMappings[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable s32 s_playerDeviceKind[NATIVE_INPUT_MAX_CONTROLLERS] = {
	PLATFORM_INPUT_DEVICE_KEYBOARD,
	PLATFORM_INPUT_DEVICE_GAMEPAD,
	PLATFORM_INPUT_DEVICE_GAMEPAD,
	PLATFORM_INPUT_DEVICE_GAMEPAD
};
global_variable s32 s_playerGlyphStyle[NATIVE_INPUT_MAX_CONTROLLERS] = {
	PLATFORM_INPUT_GLYPH_AUTO,
	PLATFORM_INPUT_GLYPH_AUTO,
	PLATFORM_INPUT_GLYPH_AUTO,
	PLATFORM_INPUT_GLYPH_AUTO
};
global_variable struct NativeInputPreferredGamepad s_preferredGamepads[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable s32 s_controllerToSlotMapping[NATIVE_INPUT_MAX_CONTROLLERS] = {-1, -1, -1, -1};

global_variable struct NativeInputController s_controllers[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable struct PlatformInputPadSnapshot s_installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable u8 *s_padSlotData[NATIVE_INPUT_PHYSICAL_SLOT_COUNT];
global_variable const bool *s_keyboardState;
global_variable s32 s_inputInitialized;
global_variable s32 s_installedSnapshotsActive;
global_variable s32 s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
global_variable s32 s_lastActiveControllerSlot = -1;
global_variable float s_mouseWheelY = 0.0f;
global_variable s32 s_controlsLoaded = 0;
global_variable s32 s_rebindCaptureKind = PLATFORM_INPUT_DEVICE_NONE;
global_variable s32 s_rebindCaptureRuntimeId = -1;
global_variable s32 s_rebindCaptureArmed = 0;

#define NATIVE_INPUT_REBIND_AXIS_THRESHOLD 16000

global_variable const char *s_inputActionNames[PLATFORM_INPUT_ACTION_COUNT] =
{
    "Square", "Circle", "Triangle", "Cross",
    "L1", "L2", "L3", "R1", "R2", "R3",
    "Start", "Select",
    "D-Pad Left", "D-Pad Right", "D-Pad Up", "D-Pad Down",
    "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y",
    "Left Stick Left", "Left Stick Right", "Left Stick Up", "Left Stick Down",
    "Right Stick Left", "Right Stick Right", "Right Stick Up", "Right Stick Down"
};

global_variable const char *s_inputActionDisplayNames[PLATFORM_INPUT_ACTION_COUNT] =
{
    "BRAKE / REVERSE", "ITEM", "TRIANGLE", "ACCELERATE",
    "L1 - POWERSLIDE", "L2", "L3", "R1 - POWERSLIDE", "R2", "R3",
    "START / PAUSE", "SELECT",
    "D-PAD LEFT", "D-PAD RIGHT", "D-PAD UP", "D-PAD DOWN",
    "LEFT STICK - LEFT / RIGHT", "LEFT STICK - UP / DOWN", "RIGHT STICK - LEFT / RIGHT", "RIGHT STICK - UP / DOWN",
    "LEFT STICK - LEFT", "LEFT STICK - RIGHT", "LEFT STICK - UP", "LEFT STICK - DOWN",
    "RIGHT STICK - LEFT", "RIGHT STICK - RIGHT", "RIGHT STICK - UP", "RIGHT STICK - DOWN"
};

global_variable const char *s_inputGlyphStyleNames[PLATFORM_INPUT_GLYPH_STYLE_COUNT] =
{
    "AUTO", "PLAYSTATION", "XBOX", "GENERIC", "KEYBOARD", "NINTENDO"
};

internal void NativeInput_CapturePreferredGamepad(s32 slot);
internal void NativeInput_UpdateGamepadPlayerIndices(void);
internal void NativeInput_ReorderControllersToPreferences(void);

extern s32 g_padCommEnable;

internal u16 NativeInput_GetSnapshotButtons(const struct PlatformInputPadSnapshot *snapshot)
{
	return (u16)(snapshot->buttons[0] | (snapshot->buttons[1] << 8));
}

internal void NativeInput_SetSnapshotButtons(struct PlatformInputPadSnapshot *snapshot, u16 buttons)
{
	snapshot->buttons[0] = (u8)(buttons & 0xff);
	snapshot->buttons[1] = (u8)(buttons >> 8);
}

internal void NativeInput_ResetSnapshot(s32 slot)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;

	(void)slot;
	snapshot->connected = 0;
	snapshot->status = NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal s32 NativeInput_IsValidControllerSlot(s32 slot)
{
	return (slot >= 0) && (slot < NATIVE_INPUT_MAX_CONTROLLERS);
}

internal s32 NativeInput_NextControllerSlot(s32 slot)
{
	slot++;
	if (slot >= NATIVE_INPUT_MAX_CONTROLLERS)
	{
		slot = 0;
	}

	return slot;
}

internal void NativeInput_MakeDisconnectedSnapshot(struct PlatformInputPadSnapshot *snapshot)
{
	if (snapshot == NULL)
	{
		return;
	}

	snapshot->connected = 0;
	snapshot->status = NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal void NativeInput_WritePadPacket(u8 *dst, const struct PlatformInputPadSnapshot *snapshot)
{
	if ((dst == NULL) || (snapshot == NULL))
	{
		return;
	}

	dst[0] = snapshot->status;
	dst[1] = snapshot->id;
	dst[2] = snapshot->buttons[0];
	dst[3] = snapshot->buttons[1];
	dst[4] = snapshot->analog[0];
	dst[5] = snapshot->analog[1];
	dst[6] = snapshot->analog[2];
	dst[7] = snapshot->analog[3];
}

internal s32 NativeInput_UseMultitapBus(void)
{
	s32 slot;

	for (slot = NATIVE_INPUT_PHYSICAL_SLOT_COUNT; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].snapshot.connected != 0)
		{
			return 1;
		}
	}

	return 0;
}

internal void NativeInput_WritePadBus(void)
{
	u8 *slot0 = s_padSlotData[0];
	u8 *slot1 = s_padSlotData[1];
	s32 useMultitap = NativeInput_UseMultitapBus();
	s32 slot;

	if (slot0 != NULL)
	{
		if (useMultitap != 0)
		{
			slot0[0] = 0;
			slot0[1] = NATIVE_INPUT_PAD_MULTITAP;
			for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
			{
				NativeInput_WritePadPacket(&slot0[NATIVE_INPUT_MULTITAP_HEADER + (slot * NATIVE_INPUT_PAD_PACKET_BYTES)], &s_controllers[slot].snapshot);
			}
		}
		else
		{
			NativeInput_WritePadPacket(slot0, &s_controllers[0].snapshot);
		}
	}

	if (slot1 != NULL)
	{
		if (useMultitap != 0)
		{
			struct PlatformInputPadSnapshot disconnected;

			NativeInput_MakeDisconnectedSnapshot(&disconnected);
			NativeInput_WritePadPacket(slot1, &disconnected);
		}
		else
		{
			NativeInput_WritePadPacket(slot1, &s_controllers[1].snapshot);
		}
	}
}

internal void NativeInput_WriteInstalledSnapshots(void)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].snapshot = s_installedSnapshots[slot];
	}

	NativeInput_WritePadBus();
}

internal void NativeInput_SetDefaultMappingsForPlayer(int playerIndex)
{
	struct NativeInputKeyboardMapping *keyboard;
	struct NativeInputControllerMapping *controller;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return;

	keyboard = &s_keyboardMappings[playerIndex];
	controller = &s_controllerMappings[playerIndex];

	memset(keyboard, 0, sizeof(*keyboard));
	memset(controller, 0, sizeof(*controller));

	keyboard->kc_square = SDL_SCANCODE_X;
	keyboard->kc_circle = SDL_SCANCODE_V;
	keyboard->kc_triangle = SDL_SCANCODE_Z;
	keyboard->kc_cross = SDL_SCANCODE_C;

	keyboard->kc_l1 = SDL_SCANCODE_LSHIFT;
	keyboard->kc_l2 = SDL_SCANCODE_LCTRL;
	keyboard->kc_l3 = SDL_SCANCODE_LEFTBRACKET;

	keyboard->kc_r1 = SDL_SCANCODE_RSHIFT;
	keyboard->kc_r2 = SDL_SCANCODE_RCTRL;
	keyboard->kc_r3 = SDL_SCANCODE_RIGHTBRACKET;

	keyboard->kc_dpad_up = SDL_SCANCODE_UP;
	keyboard->kc_dpad_down = SDL_SCANCODE_DOWN;
	keyboard->kc_dpad_left = SDL_SCANCODE_LEFT;
	keyboard->kc_dpad_right = SDL_SCANCODE_RIGHT;

	keyboard->kc_select = SDL_SCANCODE_SPACE;
	keyboard->kc_start = SDL_SCANCODE_RETURN;

	controller->gc_square = SDL_GAMEPAD_BUTTON_WEST;
	controller->gc_circle = SDL_GAMEPAD_BUTTON_EAST;
	controller->gc_triangle = SDL_GAMEPAD_BUTTON_NORTH;
	controller->gc_cross = SDL_GAMEPAD_BUTTON_SOUTH;

	controller->gc_l1 = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	controller->gc_l2 = SDL_GAMEPAD_AXIS_LEFT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_l3 = SDL_GAMEPAD_BUTTON_LEFT_STICK;

	controller->gc_r1 = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	controller->gc_r2 = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_r3 = SDL_GAMEPAD_BUTTON_RIGHT_STICK;

	controller->gc_dpad_up = SDL_GAMEPAD_BUTTON_DPAD_UP;
	controller->gc_dpad_down = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	controller->gc_dpad_left = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	controller->gc_dpad_right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

	controller->gc_select = SDL_GAMEPAD_BUTTON_BACK;
	controller->gc_start = SDL_GAMEPAD_BUTTON_START;

	controller->gc_axis_left_x = SDL_GAMEPAD_AXIS_LEFTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_axis_left_y = SDL_GAMEPAD_AXIS_LEFTY | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_axis_right_x = SDL_GAMEPAD_AXIS_RIGHTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_axis_right_y = SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_INPUT_MAP_FLAG_AXIS;

	controller->gc_left_stick_left =
		SDL_GAMEPAD_AXIS_LEFTX | NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE;
	controller->gc_left_stick_right =
		SDL_GAMEPAD_AXIS_LEFTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_left_stick_up =
		SDL_GAMEPAD_AXIS_LEFTY | NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE;
	controller->gc_left_stick_down =
		SDL_GAMEPAD_AXIS_LEFTY | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_right_stick_left =
		SDL_GAMEPAD_AXIS_RIGHTX | NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE;
	controller->gc_right_stick_right =
		SDL_GAMEPAD_AXIS_RIGHTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	controller->gc_right_stick_up =
		SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE;
	controller->gc_right_stick_down =
		SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_INPUT_MAP_FLAG_AXIS;
}

internal void NativeInput_DefaultMappings(void)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < NATIVE_INPUT_MAX_CONTROLLERS; playerIndex++)
		NativeInput_SetDefaultMappingsForPlayer(playerIndex);
}

internal s32 *NativeInput_KeyboardBindingPtr(int playerIndex, int action)
{
	struct NativeInputKeyboardMapping *mapping;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return NULL;

	mapping = &s_keyboardMappings[playerIndex];

	switch (action)
	{
	case PLATFORM_INPUT_ACTION_SQUARE: return &mapping->kc_square;
	case PLATFORM_INPUT_ACTION_CIRCLE: return &mapping->kc_circle;
	case PLATFORM_INPUT_ACTION_TRIANGLE: return &mapping->kc_triangle;
	case PLATFORM_INPUT_ACTION_CROSS: return &mapping->kc_cross;
	case PLATFORM_INPUT_ACTION_L1: return &mapping->kc_l1;
	case PLATFORM_INPUT_ACTION_L2: return &mapping->kc_l2;
	case PLATFORM_INPUT_ACTION_L3: return &mapping->kc_l3;
	case PLATFORM_INPUT_ACTION_R1: return &mapping->kc_r1;
	case PLATFORM_INPUT_ACTION_R2: return &mapping->kc_r2;
	case PLATFORM_INPUT_ACTION_R3: return &mapping->kc_r3;
	case PLATFORM_INPUT_ACTION_START: return &mapping->kc_start;
	case PLATFORM_INPUT_ACTION_SELECT: return &mapping->kc_select;
	case PLATFORM_INPUT_ACTION_DPAD_LEFT: return &mapping->kc_dpad_left;
	case PLATFORM_INPUT_ACTION_DPAD_RIGHT: return &mapping->kc_dpad_right;
	case PLATFORM_INPUT_ACTION_DPAD_UP: return &mapping->kc_dpad_up;
	case PLATFORM_INPUT_ACTION_DPAD_DOWN: return &mapping->kc_dpad_down;
	default: return NULL;
	}
}

internal s32 *NativeInput_GamepadBindingPtr(int playerIndex, int action)
{
	struct NativeInputControllerMapping *mapping;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return NULL;

	mapping = &s_controllerMappings[playerIndex];

	switch (action)
	{
	case PLATFORM_INPUT_ACTION_SQUARE: return &mapping->gc_square;
	case PLATFORM_INPUT_ACTION_CIRCLE: return &mapping->gc_circle;
	case PLATFORM_INPUT_ACTION_TRIANGLE: return &mapping->gc_triangle;
	case PLATFORM_INPUT_ACTION_CROSS: return &mapping->gc_cross;
	case PLATFORM_INPUT_ACTION_L1: return &mapping->gc_l1;
	case PLATFORM_INPUT_ACTION_L2: return &mapping->gc_l2;
	case PLATFORM_INPUT_ACTION_L3: return &mapping->gc_l3;
	case PLATFORM_INPUT_ACTION_R1: return &mapping->gc_r1;
	case PLATFORM_INPUT_ACTION_R2: return &mapping->gc_r2;
	case PLATFORM_INPUT_ACTION_R3: return &mapping->gc_r3;
	case PLATFORM_INPUT_ACTION_START: return &mapping->gc_start;
	case PLATFORM_INPUT_ACTION_SELECT: return &mapping->gc_select;
	case PLATFORM_INPUT_ACTION_DPAD_LEFT: return &mapping->gc_dpad_left;
	case PLATFORM_INPUT_ACTION_DPAD_RIGHT: return &mapping->gc_dpad_right;
	case PLATFORM_INPUT_ACTION_DPAD_UP: return &mapping->gc_dpad_up;
	case PLATFORM_INPUT_ACTION_DPAD_DOWN: return &mapping->gc_dpad_down;
	case PLATFORM_INPUT_ACTION_LEFT_X: return &mapping->gc_axis_left_x;
	case PLATFORM_INPUT_ACTION_LEFT_Y: return &mapping->gc_axis_left_y;
	case PLATFORM_INPUT_ACTION_RIGHT_X: return &mapping->gc_axis_right_x;
	case PLATFORM_INPUT_ACTION_RIGHT_Y: return &mapping->gc_axis_right_y;
	case PLATFORM_INPUT_ACTION_LEFT_STICK_LEFT: return &mapping->gc_left_stick_left;
	case PLATFORM_INPUT_ACTION_LEFT_STICK_RIGHT: return &mapping->gc_left_stick_right;
	case PLATFORM_INPUT_ACTION_LEFT_STICK_UP: return &mapping->gc_left_stick_up;
	case PLATFORM_INPUT_ACTION_LEFT_STICK_DOWN: return &mapping->gc_left_stick_down;
	case PLATFORM_INPUT_ACTION_RIGHT_STICK_LEFT: return &mapping->gc_right_stick_left;
	case PLATFORM_INPUT_ACTION_RIGHT_STICK_RIGHT: return &mapping->gc_right_stick_right;
	case PLATFORM_INPUT_ACTION_RIGHT_STICK_UP: return &mapping->gc_right_stick_up;
	case PLATFORM_INPUT_ACTION_RIGHT_STICK_DOWN: return &mapping->gc_right_stick_down;
	default: return NULL;
	}
}

internal int NativeInput_ValidateGamepadBinding(int binding)
{
    int raw = binding & ~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);
    if ((binding & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
        return raw >= 0 && raw < SDL_GAMEPAD_AXIS_COUNT;
    return raw >= 0 && raw < SDL_GAMEPAD_BUTTON_COUNT;
}

int Platform_InputSaveControls(void)
{
	FILE *f = fopen(NATIVE_INPUT_CONTROLS_FILE, "wb");
	int playerIndex;
	int action;

	if (f == NULL)
		return 0;

	fprintf(f, "version=2\n");
	fprintf(f, "keyboard_player=%d\n", s_keyboardControllerSlot);

	for (playerIndex = 0; playerIndex < NATIVE_INPUT_MAX_CONTROLLERS; playerIndex++)
	{
		const struct NativeInputPreferredGamepad *preferred =
			&s_preferredGamepads[playerIndex];

		fprintf(f, "player_%d_device=%d\n",
		        playerIndex, s_playerDeviceKind[playerIndex]);
		fprintf(f, "player_%d_glyph=%d\n",
		        playerIndex, s_playerGlyphStyle[playerIndex]);
		fprintf(f, "player_%d_gamepad_guid=%s\n",
		        playerIndex, preferred->guid);
		fprintf(f, "player_%d_gamepad_serial=%s\n",
		        playerIndex, preferred->serial);
		fprintf(f, "player_%d_gamepad_path=%s\n",
		        playerIndex, preferred->path);
		fprintf(f, "player_%d_gamepad_name=%s\n",
		        playerIndex, preferred->name);

		for (action = 0; action < PLATFORM_INPUT_ACTION_COUNT; action++)
		{
			s32 *key = NativeInput_KeyboardBindingPtr(playerIndex, action);
			s32 *pad = NativeInput_GamepadBindingPtr(playerIndex, action);

			if (key != NULL)
				fprintf(f, "keyboard_%d_%d=%d\n",
				        playerIndex, action, *key);
			if (pad != NULL)
				fprintf(f, "gamepad_%d_%d=%d\n",
				        playerIndex, action, *pad);
		}
	}

	fclose(f);
	return 1;
}

internal void NativeInput_CopyConfigText(
	char *dst,
	size_t dstSize,
	const char *src)
{
	size_t len;

	if (dst == NULL || dstSize == 0)
		return;

	if (src == NULL)
	{
		dst[0] = 0;
		return;
	}

	snprintf(dst, dstSize, "%s", src);
	len = strlen(dst);
	while (len > 0 &&
	       (dst[len - 1] == '\r' || dst[len - 1] == '\n'))
	{
		dst[--len] = 0;
	}
}

int Platform_InputReloadControls(void)
{
	FILE *f;
	char line[512];
	int configVersion = 1;
	int playerIndex;

	NativeInput_DefaultMappings();
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	memset(s_preferredGamepads, 0, sizeof(s_preferredGamepads));

	for (playerIndex = 0; playerIndex < NATIVE_INPUT_MAX_CONTROLLERS; playerIndex++)
	{
		s_playerDeviceKind[playerIndex] =
			(playerIndex == NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT) ?
			PLATFORM_INPUT_DEVICE_KEYBOARD :
			PLATFORM_INPUT_DEVICE_NONE;
		s_playerGlyphStyle[playerIndex] = PLATFORM_INPUT_GLYPH_AUTO;
	}

	f = fopen(NATIVE_INPUT_CONTROLS_FILE, "rb");
	if (f == NULL)
	{
		s_controlsLoaded = 1;
		return 1;
	}

	while (fgets(line, sizeof(line), f) != NULL)
	{
		int action;
		int value;
		int parsedPlayer;

		if (sscanf(line, "version=%d", &value) == 1)
		{
			configVersion = value;
			continue;
		}

		if (sscanf(line, "keyboard_player=%d", &value) == 1)
		{
			if (value >= -1 && value < NATIVE_INPUT_MAX_CONTROLLERS)
				s_keyboardControllerSlot = value;
			continue;
		}

		if (sscanf(line, "player_%d_device=%d",
		           &parsedPlayer, &value) == 2)
		{
			if (NativeInput_IsValidControllerSlot(parsedPlayer) &&
			    value >= PLATFORM_INPUT_DEVICE_NONE &&
			    value <= PLATFORM_INPUT_DEVICE_GAMEPAD)
			{
				s_playerDeviceKind[parsedPlayer] = value;
			}
			continue;
		}

		if (sscanf(line, "player_%d_glyph=%d",
		           &parsedPlayer, &value) == 2)
		{
			if (NativeInput_IsValidControllerSlot(parsedPlayer) &&
			    value >= PLATFORM_INPUT_GLYPH_AUTO &&
			    value < PLATFORM_INPUT_GLYPH_STYLE_COUNT)
			{
				s_playerGlyphStyle[parsedPlayer] = value;
			}
			continue;
		}

		for (parsedPlayer = 0;
		     parsedPlayer < NATIVE_INPUT_MAX_CONTROLLERS;
		     parsedPlayer++)
		{
			char prefix[64];
			size_t prefixLen;

			snprintf(prefix, sizeof(prefix),
			         "player_%d_gamepad_guid=", parsedPlayer);
			prefixLen = strlen(prefix);
			if (strncmp(line, prefix, prefixLen) == 0)
			{
				NativeInput_CopyConfigText(
					s_preferredGamepads[parsedPlayer].guid,
					sizeof(s_preferredGamepads[parsedPlayer].guid),
					line + prefixLen);
				break;
			}

			snprintf(prefix, sizeof(prefix),
			         "player_%d_gamepad_serial=", parsedPlayer);
			prefixLen = strlen(prefix);
			if (strncmp(line, prefix, prefixLen) == 0)
			{
				NativeInput_CopyConfigText(
					s_preferredGamepads[parsedPlayer].serial,
					sizeof(s_preferredGamepads[parsedPlayer].serial),
					line + prefixLen);
				break;
			}

			snprintf(prefix, sizeof(prefix),
			         "player_%d_gamepad_path=", parsedPlayer);
			prefixLen = strlen(prefix);
			if (strncmp(line, prefix, prefixLen) == 0)
			{
				NativeInput_CopyConfigText(
					s_preferredGamepads[parsedPlayer].path,
					sizeof(s_preferredGamepads[parsedPlayer].path),
					line + prefixLen);
				break;
			}

			snprintf(prefix, sizeof(prefix),
			         "player_%d_gamepad_name=", parsedPlayer);
			prefixLen = strlen(prefix);
			if (strncmp(line, prefix, prefixLen) == 0)
			{
				NativeInput_CopyConfigText(
					s_preferredGamepads[parsedPlayer].name,
					sizeof(s_preferredGamepads[parsedPlayer].name),
					line + prefixLen);
				break;
			}
		}
		if (parsedPlayer < NATIVE_INPUT_MAX_CONTROLLERS)
			continue;

		if (sscanf(line, "keyboard_%d_%d=%d",
		           &parsedPlayer, &action, &value) == 3)
		{
			s32 *binding =
				NativeInput_KeyboardBindingPtr(parsedPlayer, action);
			if (binding != NULL &&
			    value >= -1 && value < SDL_SCANCODE_COUNT)
			{
				*binding = value;
			}
			continue;
		}

		if (sscanf(line, "gamepad_%d_%d=%d",
		           &parsedPlayer, &action, &value) == 3)
		{
			s32 *binding =
				NativeInput_GamepadBindingPtr(parsedPlayer, action);
			if (binding != NULL &&
			    (value == -1 || NativeInput_ValidateGamepadBinding(value)))
			{
				*binding = value;
			}
			continue;
		}

		/* v1 migration: the old backend had one keyboard/gamepad map.
		   Copy it to every player so the first v2 save preserves behavior. */
		if (sscanf(line, "keyboard_%d=%d", &action, &value) == 2)
		{
			if (value >= 0 && value < SDL_SCANCODE_COUNT)
			{
				for (playerIndex = 0;
				     playerIndex < NATIVE_INPUT_MAX_CONTROLLERS;
				     playerIndex++)
				{
					s32 *binding =
						NativeInput_KeyboardBindingPtr(playerIndex, action);
					if (binding != NULL)
						*binding = value;
				}
			}
			continue;
		}

		if (sscanf(line, "gamepad_%d=%d", &action, &value) == 2)
		{
			if (NativeInput_ValidateGamepadBinding(value))
			{
				for (playerIndex = 0;
				     playerIndex < NATIVE_INPUT_MAX_CONTROLLERS;
				     playerIndex++)
				{
					s32 *binding =
						NativeInput_GamepadBindingPtr(playerIndex, action);
					if (binding != NULL)
						*binding = value;
				}
			}
		}
	}

	fclose(f);

	if (configVersion < 2)
	{
		/* v1 never persisted physical gamepad ownership. Keep the unique
		   keyboard player, but leave every other player explicitly
		   UNASSIGNED until the user selects a device in Controls. */
		for (playerIndex = 0;
		     playerIndex < NATIVE_INPUT_MAX_CONTROLLERS;
		     playerIndex++)
		{
			s_playerDeviceKind[playerIndex] = PLATFORM_INPUT_DEVICE_NONE;
			s_playerGlyphStyle[playerIndex] = PLATFORM_INPUT_GLYPH_AUTO;
		}

		if (NativeInput_IsValidControllerSlot(s_keyboardControllerSlot))
		{
			s_playerDeviceKind[s_keyboardControllerSlot] =
				PLATFORM_INPUT_DEVICE_KEYBOARD;
		}
	}

	/* Keep the unique keyboard assignment coherent even if the cfg was
	   manually edited. */
	if (NativeInput_IsValidControllerSlot(s_keyboardControllerSlot))
	{
		for (playerIndex = 0;
		     playerIndex < NATIVE_INPUT_MAX_CONTROLLERS;
		     playerIndex++)
		{
			if (playerIndex != s_keyboardControllerSlot &&
			    s_playerDeviceKind[playerIndex] ==
				    PLATFORM_INPUT_DEVICE_KEYBOARD)
			{
				s_playerDeviceKind[playerIndex] =
					PLATFORM_INPUT_DEVICE_GAMEPAD;
			}
		}
		s_playerDeviceKind[s_keyboardControllerSlot] =
			PLATFORM_INPUT_DEVICE_KEYBOARD;
	}

	s_controlsLoaded = 1;
	return 1;
}

int Platform_InputGetKeyboardBindingForPlayer(int playerIndex, int action)
{
	s32 *binding = NativeInput_KeyboardBindingPtr(playerIndex, action);
	return binding != NULL ? *binding : -1;
}

int Platform_InputSetKeyboardBindingForPlayer(
	int playerIndex,
	int action,
	int binding)
{
	s32 *dst = NativeInput_KeyboardBindingPtr(playerIndex, action);

	if (dst == NULL || binding < -1 || binding >= SDL_SCANCODE_COUNT)
		return 0;

	*dst = binding;
	return Platform_InputSaveControls();
}

int Platform_InputGetGamepadBindingForPlayer(int playerIndex, int action)
{
	s32 *binding = NativeInput_GamepadBindingPtr(playerIndex, action);
	return binding != NULL ? *binding : -1;
}

int Platform_InputSetGamepadBindingForPlayer(
	int playerIndex,
	int action,
	int binding)
{
	s32 *dst = NativeInput_GamepadBindingPtr(playerIndex, action);

	if (dst == NULL ||
	    (binding != -1 && !NativeInput_ValidateGamepadBinding(binding)))
		return 0;

	*dst = binding;
	return Platform_InputSaveControls();
}

void Platform_InputResetBindingsForPlayer(int playerIndex)
{
	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return;

	NativeInput_SetDefaultMappingsForPlayer(playerIndex);
	Platform_InputSaveControls();
}

int Platform_InputGetKeyboardBinding(int action)
{
	return Platform_InputGetKeyboardBindingForPlayer(0, action);
}

int Platform_InputSetKeyboardBinding(int action, int binding)
{
	return Platform_InputSetKeyboardBindingForPlayer(0, action, binding);
}

int Platform_InputGetGamepadBinding(int action)
{
	return Platform_InputGetGamepadBindingForPlayer(0, action);
}

int Platform_InputSetGamepadBinding(int action, int binding)
{
	return Platform_InputSetGamepadBindingForPlayer(0, action, binding);
}

void Platform_InputResetBindings(void)
{
	NativeInput_DefaultMappings();
	Platform_InputSaveControls();
}

const char *Platform_InputGetActionName(int action)
{
	if (action < 0 || action >= PLATFORM_INPUT_ACTION_COUNT)
		return "Unknown";
	return s_inputActionNames[action];
}

const char *Platform_InputGetActionDisplayName(int action)
{
	if (action < 0 || action >= PLATFORM_INPUT_ACTION_COUNT)
		return "UNKNOWN";
	return s_inputActionDisplayNames[action];
}

const char *Platform_InputGetGlyphStyleName(int style)
{
	if (style < 0 || style >= PLATFORM_INPUT_GLYPH_STYLE_COUNT)
		return "AUTO";
	return s_inputGlyphStyleNames[style];
}

int Platform_InputGetPlayerDeviceKind(int playerIndex)
{
	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return PLATFORM_INPUT_DEVICE_NONE;
	return s_playerDeviceKind[playerIndex];
}

int Platform_InputSetPlayerDeviceKind(int playerIndex, int kind)
{
	if (!NativeInput_IsValidControllerSlot(playerIndex) ||
	    kind < PLATFORM_INPUT_DEVICE_NONE ||
	    kind > PLATFORM_INPUT_DEVICE_GAMEPAD)
	{
		return 0;
	}

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
		return Platform_InputSetKeyboardPlayer(playerIndex);

	s_playerDeviceKind[playerIndex] = kind;
	if (s_keyboardControllerSlot == playerIndex)
		s_keyboardControllerSlot = -1;

	if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD &&
	    s_controllers[playerIndex].controller != NULL)
	{
		NativeInput_CapturePreferredGamepad(playerIndex);
	}

	NativeInput_UpdateGamepadPlayerIndices();
	return Platform_InputSaveControls();
}

int Platform_InputGetPlayerGlyphStyle(int playerIndex)
{
	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return PLATFORM_INPUT_GLYPH_AUTO;
	return s_playerGlyphStyle[playerIndex];
}

int Platform_InputSetPlayerGlyphStyle(int playerIndex, int style)
{
	if (!NativeInput_IsValidControllerSlot(playerIndex) ||
	    style < PLATFORM_INPUT_GLYPH_AUTO ||
	    style >= PLATFORM_INPUT_GLYPH_STYLE_COUNT)
	{
		return 0;
	}

	s_playerGlyphStyle[playerIndex] = style;
	return Platform_InputSaveControls();
}

int Platform_InputResolvePlayerGlyphStyle(int playerIndex)
{
	int requested;
	SDL_GamepadType type;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return PLATFORM_INPUT_GLYPH_GENERIC;

	requested = s_playerGlyphStyle[playerIndex];
	if (requested != PLATFORM_INPUT_GLYPH_AUTO)
		return requested;

	if (s_playerDeviceKind[playerIndex] == PLATFORM_INPUT_DEVICE_KEYBOARD)
		return PLATFORM_INPUT_GLYPH_KEYBOARD;

	if (s_playerDeviceKind[playerIndex] != PLATFORM_INPUT_DEVICE_GAMEPAD ||
	    s_controllers[playerIndex].controller == NULL)
	{
		return PLATFORM_INPUT_GLYPH_GENERIC;
	}

	type = SDL_GetGamepadType(s_controllers[playerIndex].controller);
	switch (type)
	{
	case SDL_GAMEPAD_TYPE_PS3:
	case SDL_GAMEPAD_TYPE_PS4:
	case SDL_GAMEPAD_TYPE_PS5:
		return PLATFORM_INPUT_GLYPH_PLAYSTATION;

	case SDL_GAMEPAD_TYPE_XBOX360:
	case SDL_GAMEPAD_TYPE_XBOXONE:
		return PLATFORM_INPUT_GLYPH_XBOX;

	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
		return PLATFORM_INPUT_GLYPH_NINTENDO;

	default:
		return PLATFORM_INPUT_GLYPH_GENERIC;
	}
}

internal const char *NativeInput_KeyboardDisplayName(int binding)
{
	switch ((SDL_Scancode)binding)
	{
	case SDL_SCANCODE_SPACE: return "SPACE";
	case SDL_SCANCODE_RETURN: return "ENTER";
	case SDL_SCANCODE_ESCAPE: return "ESC";
	case SDL_SCANCODE_BACKSPACE: return "BKSP";
	case SDL_SCANCODE_TAB: return "TAB";
	case SDL_SCANCODE_CAPSLOCK: return "CAPS";
	case SDL_SCANCODE_LSHIFT: return "L-SHIFT";
	case SDL_SCANCODE_RSHIFT: return "R-SHIFT";
	case SDL_SCANCODE_LCTRL: return "L-CTRL";
	case SDL_SCANCODE_RCTRL: return "R-CTRL";
	case SDL_SCANCODE_LALT: return "L-ALT";
	case SDL_SCANCODE_RALT: return "R-ALT";
	case SDL_SCANCODE_UP: return "UP";
	case SDL_SCANCODE_DOWN: return "DOWN";
	case SDL_SCANCODE_LEFT: return "LEFT";
	case SDL_SCANCODE_RIGHT: return "RIGHT";
	case SDL_SCANCODE_PAGEUP: return "PGUP";
	case SDL_SCANCODE_PAGEDOWN: return "PGDN";
	case SDL_SCANCODE_HOME: return "HOME";
	case SDL_SCANCODE_END: return "END";
	case SDL_SCANCODE_DELETE: return "DEL";
	case SDL_SCANCODE_INSERT: return "INS";
	default:
		return SDL_GetScancodeName((SDL_Scancode)binding);
	}
}

internal const char *NativeInput_GamepadButtonDisplayName(
	int style,
	SDL_GamepadButton button)
{
	if (style == PLATFORM_INPUT_GLYPH_PLAYSTATION)
	{
		switch (button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH: return "Cross";
		case SDL_GAMEPAD_BUTTON_EAST: return "Circle";
		case SDL_GAMEPAD_BUTTON_WEST: return "Square";
		case SDL_GAMEPAD_BUTTON_NORTH: return "Triangle";
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L1";
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R1";
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
		case SDL_GAMEPAD_BUTTON_BACK: return "Select";
		case SDL_GAMEPAD_BUTTON_START: return "Start";
		default: break;
		}
	}
	else if (style == PLATFORM_INPUT_GLYPH_XBOX)
	{
		switch (button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
		case SDL_GAMEPAD_BUTTON_EAST: return "B";
		case SDL_GAMEPAD_BUTTON_WEST: return "X";
		case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
		case SDL_GAMEPAD_BUTTON_BACK: return "View";
		case SDL_GAMEPAD_BUTTON_START: return "Menu";
		default: break;
		}
	}
	else if (style == PLATFORM_INPUT_GLYPH_NINTENDO)
	{
		switch (button)
		{
		/* Nintendo physical layout: X/Y/A/B around the diamond. */
		case SDL_GAMEPAD_BUTTON_NORTH: return "X";
		case SDL_GAMEPAD_BUTTON_WEST: return "Y";
		case SDL_GAMEPAD_BUTTON_EAST: return "A";
		case SDL_GAMEPAD_BUTTON_SOUTH: return "B";
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
		case SDL_GAMEPAD_BUTTON_BACK: return "Select";
		case SDL_GAMEPAD_BUTTON_START: return "Start";
		default: break;
		}
	}
	else
	{
		switch (button)
		{
		/* Generic face buttons stay numbered; shoulders use PlayStation-style
		   names so the remap screen is immediately readable. */
		case SDL_GAMEPAD_BUTTON_NORTH: return "1";
		case SDL_GAMEPAD_BUTTON_WEST: return "2";
		case SDL_GAMEPAD_BUTTON_EAST: return "3";
		case SDL_GAMEPAD_BUTTON_SOUTH: return "4";
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L1";
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R1";
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
		case SDL_GAMEPAD_BUTTON_BACK: return "Select";
		case SDL_GAMEPAD_BUTTON_START: return "Start";
		default: break;
		}
	}

	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-UP";
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-DOWN";
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-LEFT";
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-RIGHT";
	case SDL_GAMEPAD_BUTTON_BACK: return "BACK";
	case SDL_GAMEPAD_BUTTON_START: return "START";
	default: return SDL_GetGamepadStringForButton(button);
	}
}

internal const char *NativeInput_GamepadAxisDisplayName(
	int style,
	SDL_GamepadAxis axis)
{
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFTX: return "L-X";
	case SDL_GAMEPAD_AXIS_LEFTY: return "L-Y";
	case SDL_GAMEPAD_AXIS_RIGHTX: return "R-X";
	case SDL_GAMEPAD_AXIS_RIGHTY: return "R-Y";
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		if (style == PLATFORM_INPUT_GLYPH_XBOX ||
		    style == PLATFORM_INPUT_GLYPH_NINTENDO) return "LT";
		return "L2";
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		if (style == PLATFORM_INPUT_GLYPH_XBOX ||
		    style == PLATFORM_INPUT_GLYPH_NINTENDO) return "RT";
		return "R2";
	default:
		return SDL_GetGamepadStringForAxis(axis);
	}
}

int Platform_InputFormatBindingName(
	int deviceKind,
	int binding,
	char *out,
	int outSize)
{
	const char *name = NULL;

	if (out == NULL || outSize <= 0)
		return 0;

	out[0] = 0;

	if (deviceKind == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		if (binding < 0 || binding >= SDL_SCANCODE_COUNT)
			return 0;

		name = NativeInput_KeyboardDisplayName(binding);
		if (name != NULL && name[0] != 0)
			snprintf(out, (size_t)outSize, "%s", name);
		else
			snprintf(out, (size_t)outSize, "Key %d", binding);
		return 1;
	}

	if (deviceKind == PLATFORM_INPUT_DEVICE_GAMEPAD &&
	    NativeInput_ValidateGamepadBinding(binding))
	{
		int raw =
			binding &
			~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);

		if ((binding & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
			name = NativeInput_GamepadAxisDisplayName(
				PLATFORM_INPUT_GLYPH_GENERIC,
				(SDL_GamepadAxis)raw);
		else
			name = NativeInput_GamepadButtonDisplayName(
				PLATFORM_INPUT_GLYPH_GENERIC,
				(SDL_GamepadButton)raw);

		snprintf(out, (size_t)outSize, "%s%s",
		         (name != NULL && name[0] != 0) ? name : "Unknown",
		         (binding & NATIVE_INPUT_MAP_FLAG_INVERSE) != 0 ?
			         " -" : "");
		return 1;
	}

	return 0;
}

int Platform_InputFormatPlayerBindingName(
	int playerIndex,
	int action,
	char *out,
	int outSize)
{
	int kind;
	int binding;
	int style;
	const char *name;

	if (!NativeInput_IsValidControllerSlot(playerIndex) ||
	    out == NULL || outSize <= 0)
	{
		return 0;
	}

	kind = s_playerDeviceKind[playerIndex];
	style = Platform_InputResolvePlayerGlyphStyle(playerIndex);

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		binding =
			Platform_InputGetKeyboardBindingForPlayer(playerIndex, action);
		if (binding < 0)
		{
			snprintf(out, (size_t)outSize, "--");
			return 1;
		}

		/* Auto on keyboard shows the physical key. A forced visual style
		 * intentionally changes only the glyph presentation: the real
		 * keyboard binding remains untouched and the equivalent logical
		 * gamepad action is used for PlayStation/Xbox/Generic labels. */
		if (style == PLATFORM_INPUT_GLYPH_KEYBOARD)
		{
			return Platform_InputFormatBindingName(
				PLATFORM_INPUT_DEVICE_KEYBOARD,
				binding,
				out,
				outSize);
		}

		binding =
			Platform_InputGetGamepadBindingForPlayer(playerIndex, action);
	}
	else if (kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
	{
		binding =
			Platform_InputGetGamepadBindingForPlayer(playerIndex, action);
		if (style == PLATFORM_INPUT_GLYPH_KEYBOARD)
			style = PLATFORM_INPUT_GLYPH_GENERIC;
	}
	else
	{
		snprintf(out, (size_t)outSize, "--");
		return 1;
	}

	if (!NativeInput_ValidateGamepadBinding(binding))
	{
		snprintf(out, (size_t)outSize, "--");
		return 1;
	}
	if ((binding & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
	{
		int raw =
			binding &
			~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);
		name = NativeInput_GamepadAxisDisplayName(style, (SDL_GamepadAxis)raw);
	}
	else
	{
		name = NativeInput_GamepadButtonDisplayName(
			style,
			(SDL_GamepadButton)binding);
	}

	snprintf(out, (size_t)outSize, "%s%s",
	         (name != NULL && name[0] != 0) ? name : "Unknown",
	         (binding & NATIVE_INPUT_MAP_FLAG_INVERSE) != 0 ? " -" : "");
	return 1;
}

internal s32 NativeInput_ControllerButtonState(SDL_Gamepad *controller, s32 buttonOrAxis)
{
	if (controller == NULL || buttonOrAxis < 0)
	{
		return 0;
	}

	if ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
	{
		s32 axis = buttonOrAxis & ~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);
		s32 value = SDL_GetGamepadAxis(controller, (SDL_GamepadAxis)axis);

		if ((abs(value) > NATIVE_INPUT_AXIS_DEADZONE) && ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_INVERSE) != 0))
		{
			value *= -1;
		}

		return value;
	}

	if (buttonOrAxis < 0)
	{
		return 0;
	}

	return SDL_GetGamepadButton(controller, (SDL_GamepadButton)buttonOrAxis) * 32767;
}

internal u8 NativeInput_AxisToByte(s32 axis)
{
	s32 value = (axis / 256) + 128;

	if (value < 0)
	{
		return 0;
	}

	if (value > 0xff)
	{
		return 0xff;
	}

	return (u8)value;
}

internal s32 NativeInput_AxisIsActive(s32 axis)
{
	return abs(axis) > NATIVE_INPUT_AXIS_DEADZONE;
}

internal s32 NativeInput_DirectionalBindingValue(
	SDL_Gamepad *controller,
	s32 binding)
{
	s32 value = NativeInput_ControllerButtonState(controller, binding);

	/* Directional stick bindings contribute only their positive half.
	   This lets each of the four directions be rebound independently to
	   either an axis direction or a digital button. */
	if (value <= 0)
		return 0;
	if (value > 32767)
		return 32767;
	return value;
}

internal void NativeInput_ApplyController(s32 slot)
{
	struct NativeInputController *nativeController = &s_controllers[slot];
	struct PlatformInputPadSnapshot *snapshot = &nativeController->snapshot;
	const struct NativeInputControllerMapping *mapping = &s_controllerMappings[slot];
	SDL_Gamepad *controller = nativeController->controller;
	u16 buttons = 0xffff;
	s32 rightX;
	s32 rightY;
	s32 leftX;
	s32 leftY;

	if (s_playerDeviceKind[slot] != PLATFORM_INPUT_DEVICE_GAMEPAD ||
	    (controller == NULL) ||
	    (SDL_GamepadConnected(controller) == 0))
	{
		return;
	}

	snapshot->connected = 1;
	snapshot->status = 0;
	snapshot->id = nativeController->analogEnabled ? NATIVE_INPUT_PAD_ANALOG : NATIVE_INPUT_PAD_DIGITAL;

	if (NativeInput_ControllerButtonState(controller, mapping->gc_square) > 16384)
	{
		buttons &= ~0x8000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_circle) > 16384)
	{
		buttons &= ~0x2000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_triangle) > 16384)
	{
		buttons &= ~0x1000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_cross) > 16384)
	{
		buttons &= ~0x4000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l1) > 16384)
	{
		buttons &= ~0x400;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r1) > 16384)
	{
		buttons &= ~0x800;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l2) > 16384)
	{
		buttons &= ~0x100;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r2) > 16384)
	{
		buttons &= ~0x200;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_up) > 16384)
	{
		buttons &= ~0x10;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_down) > 16384)
	{
		buttons &= ~0x40;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_left) > 16384)
	{
		buttons &= ~0x80;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_right) > 16384)
	{
		buttons &= ~0x20;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l3) > 16384)
	{
		buttons &= ~0x2;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r3) > 16384)
	{
		buttons &= ~0x4;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_select) > 16384)
	{
		buttons &= ~0x1;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_start) > 16384)
	{
		buttons &= ~0x8;
	}

	rightX =
		NativeInput_DirectionalBindingValue(controller, mapping->gc_right_stick_right) -
		NativeInput_DirectionalBindingValue(controller, mapping->gc_right_stick_left);
	rightY =
		NativeInput_DirectionalBindingValue(controller, mapping->gc_right_stick_down) -
		NativeInput_DirectionalBindingValue(controller, mapping->gc_right_stick_up);
	leftX =
		NativeInput_DirectionalBindingValue(controller, mapping->gc_left_stick_right) -
		NativeInput_DirectionalBindingValue(controller, mapping->gc_left_stick_left);
	leftY =
		NativeInput_DirectionalBindingValue(controller, mapping->gc_left_stick_down) -
		NativeInput_DirectionalBindingValue(controller, mapping->gc_left_stick_up);

	if ((buttons != 0xffff) || NativeInput_AxisIsActive(rightX) || NativeInput_AxisIsActive(rightY) || NativeInput_AxisIsActive(leftX) ||
	    NativeInput_AxisIsActive(leftY))
	{
		s_lastActiveControllerSlot = slot;
	}

	if (((buttons & 0x1) == 0) && ((buttons & 0x8) == 0))
	{
		buttons = 0xffff;
		if (nativeController->switchingAnalog == 0)
		{
			nativeController->analogEnabled = nativeController->analogEnabled == 0;
		}
		nativeController->switchingAnalog = 1;
	}
	else
	{
		nativeController->switchingAnalog = 0;
	}

	NativeInput_SetSnapshotButtons(snapshot, buttons);
	snapshot->analog[0] = NativeInput_AxisToByte(rightX);
	snapshot->analog[1] = NativeInput_AxisToByte(rightY);
	snapshot->analog[2] = NativeInput_AxisToByte(leftX);
	snapshot->analog[3] = NativeInput_AxisToByte(leftY);
}

internal u16 NativeInput_ReadMouse(void)
{
	SDL_MouseButtonFlags mouseButtons;
	u16 buttons = 0xffff;

	mouseButtons = SDL_GetMouseState(NULL, NULL);

	// Mouse is a temporary PS1-pad facade for P1 while the wheel/vehicle mod is debugged.
	if ((mouseButtons & SDL_BUTTON_LMASK) != 0)
	{
		buttons &= ~0x4000; // Cross / confirm / accelerate
	}
	if ((mouseButtons & SDL_BUTTON_RMASK) != 0)
	{
		buttons &= ~0x1000; // Triangle / back
	}
	if ((mouseButtons & SDL_BUTTON_MMASK) != 0)
	{
		buttons &= ~0x2000; // Circle
	}
	if ((mouseButtons & SDL_BUTTON_X1MASK) != 0)
	{
		buttons &= ~0x8000; // Square
	}
	if ((mouseButtons & SDL_BUTTON_X2MASK) != 0)
	{
		buttons &= ~0x800; // R1
	}

	if (s_mouseWheelY > 0.0f)
	{
		buttons &= ~0x10; // D-pad up
	}
	else if (s_mouseWheelY < 0.0f)
	{
		buttons &= ~0x40; // D-pad down
	}

	return buttons;
}

internal s32 NativeInput_KeyboardBindingPressed(s32 binding)
{
	if (s_keyboardState == NULL ||
	    binding < 0 ||
	    binding >= SDL_SCANCODE_COUNT)
	{
		return 0;
	}

	if (Platform_DebugFreecamConsumesKey(binding))
		return 0;

	return s_keyboardState[binding] != 0;
}

internal u16 NativeInput_ReadKeyboard(s32 slot)
{
	const struct NativeInputKeyboardMapping *mapping;
	u16 buttons = 0xffff;

#if defined(CTR_NATIVE)
	if (NativeHangout_ChatIsOpen())
		return buttons;
#endif

	if (!NativeInput_IsValidControllerSlot(slot) ||
	    s_keyboardState == NULL)
	{
		return buttons;
	}

	mapping = &s_keyboardMappings[slot];

	if (NativeInput_KeyboardBindingPressed(mapping->kc_square)) buttons &= ~0x8000;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_circle)) buttons &= ~0x2000;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_triangle)) buttons &= ~0x1000;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_cross)) buttons &= ~0x4000;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_l1)) buttons &= ~0x400;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_l2)) buttons &= ~0x100;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_l3)) buttons &= ~0x2;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_r1)) buttons &= ~0x800;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_r2)) buttons &= ~0x200;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_r3)) buttons &= ~0x4;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_dpad_up)) buttons &= ~0x10;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_dpad_down)) buttons &= ~0x40;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_dpad_left)) buttons &= ~0x80;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_dpad_right)) buttons &= ~0x20;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_select)) buttons &= ~0x1;
	if (NativeInput_KeyboardBindingPressed(mapping->kc_start)) buttons &= ~0x8;

	return buttons;
}

internal s32 NativeInput_KeyboardSuppressed(void)
{
	if (s_keyboardState == NULL)
	{
		return 0;
	}

	return s_keyboardState[SDL_SCANCODE_RALT] || s_keyboardState[SDL_SCANCODE_LALT];
}

unsigned int Platform_InputGetMenuKeyboardRawButtons(int playerIndex)
{
	u16 buttons;

	/* UI keyboard fallback is deliberately independent from gameplay-device
	   ownership. When P1 uses a gamepad, keyboard + mouse still remain valid
	   menu devices, using P1's keyboard bindings. */
	if (!NativeInput_IsValidControllerSlot(playerIndex) ||
	    s_keyboardState == NULL ||
	    NativeInput_KeyboardSuppressed())
	{
		return 0;
	}

	buttons = NativeInput_ReadKeyboard(playerIndex);
	return ((unsigned int)(~buttons)) & 0xffffu;
}

internal void NativeInput_ApplyKeyboard(s32 slot, u16 keyboardButtons)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;
	u16 buttons;

	if (slot != s_keyboardControllerSlot ||
	    s_playerDeviceKind[slot] != PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		return;
	}

	if (snapshot->connected == 0)
	{
		snapshot->connected = 1;
		snapshot->status = 0;
		snapshot->id = NATIVE_INPUT_PAD_DIGITAL;
	}

	buttons = NativeInput_GetSnapshotButtons(snapshot);
	NativeInput_SetSnapshotButtons(snapshot, buttons & keyboardButtons);
}

internal s32 NativeInput_FindActiveControllerSlot(void)
{
	s32 slot;

	if (NativeInput_IsValidControllerSlot(s_lastActiveControllerSlot) && (s_controllers[s_lastActiveControllerSlot].controller != NULL))
	{
		return s_lastActiveControllerSlot;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL)
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_SwapControllerSlots(s32 slotA, s32 slotB)
{
	struct NativeInputController controller;
	s32 mapping;

	if (!NativeInput_IsValidControllerSlot(slotA) || !NativeInput_IsValidControllerSlot(slotB) || (slotA == slotB))
	{
		return;
	}

	controller = s_controllers[slotA];
	s_controllers[slotA] = s_controllers[slotB];
	s_controllers[slotB] = controller;

	mapping = s_controllerToSlotMapping[slotA];
	s_controllerToSlotMapping[slotA] = s_controllerToSlotMapping[slotB];
	s_controllerToSlotMapping[slotB] = mapping;

	if (s_lastActiveControllerSlot == slotA)
	{
		s_lastActiveControllerSlot = slotB;
	}
	else if (s_lastActiveControllerSlot == slotB)
	{
		s_lastActiveControllerSlot = slotA;
	}
}

internal void NativeInput_CapturePreferredGamepad(s32 slot)
{
	struct NativeInputPreferredGamepad *preferred;
	struct NativeInputController *controller;
	const char *serial;
	const char *path;
	const char *name;
	SDL_GUID guid;

	if (!NativeInput_IsValidControllerSlot(slot))
		return;

	preferred = &s_preferredGamepads[slot];
	controller = &s_controllers[slot];

	if (controller->controller == NULL)
		return;

	memset(preferred, 0, sizeof(*preferred));

	guid = SDL_GetGamepadGUIDForID(controller->instanceId);
	SDL_GUIDToString(
		guid,
		preferred->guid,
		(int)sizeof(preferred->guid));

	serial = SDL_GetGamepadSerial(controller->controller);
	path = SDL_GetGamepadPath(controller->controller);
	name = SDL_GetGamepadName(controller->controller);

	if (serial != NULL)
		snprintf(preferred->serial, sizeof(preferred->serial), "%s", serial);
	if (path != NULL)
		snprintf(preferred->path, sizeof(preferred->path), "%s", path);
	if (name != NULL)
		snprintf(preferred->name, sizeof(preferred->name), "%s", name);
}

internal int NativeInput_PreferredGamepadScore(
	s32 slot,
	const struct NativeInputPreferredGamepad *preferred)
{
	struct NativeInputController *controller;
	char guidText[NATIVE_INPUT_GUID_STRING_MAX];
	const char *serial;
	const char *path;
	const char *name;
	int score = 0;

	if (!NativeInput_IsValidControllerSlot(slot) ||
	    preferred == NULL ||
	    (preferred->guid[0] == 0 &&
	     preferred->serial[0] == 0 &&
	     preferred->path[0] == 0 &&
	     preferred->name[0] == 0))
	{
		return 0;
	}

	controller = &s_controllers[slot];
	if (controller->controller == NULL ||
	    !SDL_GamepadConnected(controller->controller))
	{
		return 0;
	}

	guidText[0] = 0;
	SDL_GUIDToString(
		SDL_GetGamepadGUIDForID(controller->instanceId),
		guidText,
		(int)sizeof(guidText));

	serial = SDL_GetGamepadSerial(controller->controller);
	path = SDL_GetGamepadPath(controller->controller);
	name = SDL_GetGamepadName(controller->controller);

	if (preferred->serial[0] != 0 &&
	    serial != NULL &&
	    strcmp(preferred->serial, serial) == 0)
	{
		score += 1000;
	}

	if (preferred->path[0] != 0 &&
	    path != NULL &&
	    strcmp(preferred->path, path) == 0)
	{
		score += 700;
	}

	if (preferred->guid[0] != 0 &&
	    guidText[0] != 0 &&
	    strcmp(preferred->guid, guidText) == 0)
	{
		score += 400;
	}

	if (preferred->name[0] != 0 &&
	    name != NULL &&
	    strcmp(preferred->name, name) == 0)
	{
		score += 80;
	}

	return score;
}

internal void NativeInput_UpdateGamepadPlayerIndices(void)
{
	int slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		SDL_Gamepad *pad = s_controllers[slot].controller;

		if (pad != NULL && SDL_GamepadConnected(pad))
		{
			SDL_SetGamepadPlayerIndex(
				pad,
				s_playerDeviceKind[slot] ==
					PLATFORM_INPUT_DEVICE_GAMEPAD ?
					slot : -1);
		}
	}
}

internal void NativeInput_ReorderControllersToPreferences(void)
{
	int fixed[NATIVE_INPUT_MAX_CONTROLLERS] = {0, 0, 0, 0};
	int target;

	for (target = 0;
	     target < NATIVE_INPUT_MAX_CONTROLLERS;
	     target++)
	{
		int bestSlot = -1;
		int bestScore = 0;
		int slot;

		if (s_playerDeviceKind[target] != PLATFORM_INPUT_DEVICE_GAMEPAD)
			continue;

		for (slot = 0;
		     slot < NATIVE_INPUT_MAX_CONTROLLERS;
		     slot++)
		{
			int score;

			if (fixed[slot])
				continue;

			score = NativeInput_PreferredGamepadScore(
				slot,
				&s_preferredGamepads[target]);

			if (score > bestScore)
			{
				bestScore = score;
				bestSlot = slot;
			}
		}

		if (bestSlot >= 0)
		{
			if (bestSlot != target)
				NativeInput_SwapControllerSlots(bestSlot, target);

			fixed[target] = 1;
		}
	}

	NativeInput_UpdateGamepadPlayerIndices();
}

internal s32 NativeInput_FindSlotForDeviceIndex(Sint32 deviceIndex)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllerToSlotMapping[slot] == deviceIndex)
		{
			return slot;
		}
	}

#if PLATFORM_INPUT_TEMP_MOUSE_KEYBOARD_P1
	// P1 is temporarily reserved for mouse + keyboard. Physical pads begin at P2.
	for (slot = 1; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
#else
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
#endif
	{
		if ((s_controllerToSlotMapping[slot] < 0) && (s_controllers[slot].controller == NULL))
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_CloseController(s32 slot)
{
	struct NativeInputController *controller;

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		SDL_CloseGamepad(controller->controller);
	}

	controller->controller = NULL;
	controller->instanceId = -1;
	s_controllerToSlotMapping[slot] = -1;
	controller->analogEnabled = 0;
	controller->switchingAnalog = 0;

	if (s_lastActiveControllerSlot == slot)
	{
		s_lastActiveControllerSlot = -1;
	}
}

internal void NativeInput_OpenController(SDL_JoystickID instanceId, s32 slot)
{
	struct NativeInputController *controller;
	SDL_Joystick *joystick;

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	if (SDL_IsGamepad(instanceId) == 0)
	{
		return;
	}

	controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		return;
	}

	controller->controller = SDL_OpenGamepad(instanceId);
	if (controller->controller == NULL)
	{
		return;
	}

	joystick = SDL_GetGamepadJoystick(controller->controller);
	controller->instanceId = joystick != NULL ? SDL_GetJoystickID(joystick) : instanceId;
	s_controllerToSlotMapping[slot] = controller->instanceId;
	controller->analogEnabled = 1;
	controller->switchingAnalog = 0;

	/* A physical pad may stay parked in the same logical slot as the
	   keyboard. s_playerDeviceKind decides which one actually drives it. */
	NativeInput_UpdateGamepadPlayerIndices();
}

internal void NativeInput_OpenKnownControllers(void)
{
	SDL_JoystickID *gamepads;
	s32 count = 0;
	s32 i;

	gamepads = SDL_GetGamepads(&count);
	for (i = 0; i < count; i++)
	{
		s32 slot = NativeInput_FindSlotForDeviceIndex(gamepads[i]);

		if (slot >= 0)
		{
			NativeInput_OpenController(gamepads[i], slot);
		}
	}
	SDL_free(gamepads);

	NativeInput_ReorderControllersToPreferences();
}

int Platform_InputGetKeyboardPlayer(void)
{
	return NativeInput_IsValidControllerSlot(s_keyboardControllerSlot) ?
	       s_keyboardControllerSlot : -1;
}

int Platform_InputSetKeyboardPlayer(int playerIndex)
{
	int oldPlayer = s_keyboardControllerSlot;

	if (playerIndex < -1 || playerIndex >= NATIVE_INPUT_MAX_CONTROLLERS)
		return 0;

	if (NativeInput_IsValidControllerSlot(oldPlayer) &&
	    oldPlayer != playerIndex &&
	    s_playerDeviceKind[oldPlayer] == PLATFORM_INPUT_DEVICE_KEYBOARD)
	{
		s_playerDeviceKind[oldPlayer] =
			(s_controllers[oldPlayer].controller != NULL) ?
			PLATFORM_INPUT_DEVICE_GAMEPAD :
			PLATFORM_INPUT_DEVICE_NONE;
	}

	s_keyboardControllerSlot = playerIndex;

	if (NativeInput_IsValidControllerSlot(playerIndex))
		s_playerDeviceKind[playerIndex] = PLATFORM_INPUT_DEVICE_KEYBOARD;

	NativeInput_UpdateGamepadPlayerIndices();
	return Platform_InputSaveControls();
}

int Platform_InputGetGamepadPlayer(int runtimeId)
{
	int slot;
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL &&
		    s_controllers[slot].instanceId == (SDL_JoystickID)runtimeId)
		{
			return slot;
		}
	}
	return -1;
}

int Platform_InputAssignGamepadToPlayer(int runtimeId, int playerIndex)
{
	int sourceSlot;
	int oldKeyboardPlayer = s_keyboardControllerSlot;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return 0;

	sourceSlot = Platform_InputGetGamepadPlayer(runtimeId);
	if (sourceSlot < 0)
		return 0;

	if (sourceSlot != playerIndex)
		NativeInput_SwapControllerSlots(sourceSlot, playerIndex);

	s_playerDeviceKind[playerIndex] = PLATFORM_INPUT_DEVICE_GAMEPAD;
	if (oldKeyboardPlayer == playerIndex)
		s_keyboardControllerSlot = -1;

	NativeInput_CapturePreferredGamepad(playerIndex);

	/* If assigning caused a swap and the source player is gamepad-driven,
	   remember the controller that moved there too. */
	if (sourceSlot != playerIndex &&
	    s_playerDeviceKind[sourceSlot] == PLATFORM_INPUT_DEVICE_GAMEPAD &&
	    s_controllers[sourceSlot].controller != NULL)
	{
		NativeInput_CapturePreferredGamepad(sourceSlot);
	}

	NativeInput_UpdateGamepadPlayerIndices();
	NativeInput_WritePadBus();
	Platform_InputSaveControls();
	return 1;
}

int Platform_InputGetDeviceCount(void)
{
	int slot;
	int count = 1; /* keyboard */

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL &&
		    SDL_GamepadConnected(s_controllers[slot].controller))
		{
			count++;
		}
	}

	return count;
}

int Platform_InputGetDeviceInfo(int deviceIndex, struct PlatformInputDeviceInfo *outInfo)
{
	int slot;
	int index = 1;

	if (outInfo == NULL || deviceIndex < 0)
		return 0;

	memset(outInfo, 0, sizeof(*outInfo));
	outInfo->playerIndex = -1;
	outInfo->runtimeId = -1;

	if (deviceIndex == 0)
	{
		outInfo->kind = PLATFORM_INPUT_DEVICE_KEYBOARD;
		outInfo->connected = 1;
		outInfo->playerIndex = Platform_InputGetKeyboardPlayer();
		outInfo->gamepadType = SDL_GAMEPAD_TYPE_UNKNOWN;
		outInfo->resolvedGlyphStyle = PLATFORM_INPUT_GLYPH_KEYBOARD;
		snprintf(outInfo->name, sizeof(outInfo->name), "Keyboard");
		return 1;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		struct NativeInputController *controller = &s_controllers[slot];
		const char *name;

		if (controller->controller == NULL ||
		    !SDL_GamepadConnected(controller->controller))
		{
			continue;
		}

		if (index++ != deviceIndex)
			continue;

		outInfo->kind = PLATFORM_INPUT_DEVICE_GAMEPAD;
		outInfo->connected = 1;
		outInfo->playerIndex =
			(s_playerDeviceKind[slot] == PLATFORM_INPUT_DEVICE_GAMEPAD) ?
			slot : -1;
		outInfo->runtimeId = (int)controller->instanceId;
		outInfo->gamepadType = (int)SDL_GetGamepadType(controller->controller);

		if (outInfo->playerIndex >= 0)
		{
			outInfo->resolvedGlyphStyle =
				Platform_InputResolvePlayerGlyphStyle(slot);
		}
		else
		{
			switch ((SDL_GamepadType)outInfo->gamepadType)
			{
			case SDL_GAMEPAD_TYPE_PS3:
			case SDL_GAMEPAD_TYPE_PS4:
			case SDL_GAMEPAD_TYPE_PS5:
				outInfo->resolvedGlyphStyle =
					PLATFORM_INPUT_GLYPH_PLAYSTATION;
				break;
			case SDL_GAMEPAD_TYPE_XBOX360:
			case SDL_GAMEPAD_TYPE_XBOXONE:
				outInfo->resolvedGlyphStyle =
					PLATFORM_INPUT_GLYPH_XBOX;
				break;
			default:
				outInfo->resolvedGlyphStyle =
					PLATFORM_INPUT_GLYPH_GENERIC;
				break;
			}
		}

		name = SDL_GetGamepadName(controller->controller);
		snprintf(outInfo->name, sizeof(outInfo->name), "%s",
		         (name != NULL && name[0] != 0) ? name : "Gamepad");
		return 1;
	}

	return 0;
}

int Platform_InputGetPlayerDeviceIndex(int playerIndex)
{
	int kind;
	int deviceIndex;
	int deviceCount;

	if (!NativeInput_IsValidControllerSlot(playerIndex))
		return -1;

	kind = s_playerDeviceKind[playerIndex];
	if (kind == PLATFORM_INPUT_DEVICE_NONE)
		return -1;

	if (kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
		return (s_keyboardControllerSlot == playerIndex) ? 0 : -1;

	deviceCount = Platform_InputGetDeviceCount();
	for (deviceIndex = 1; deviceIndex < deviceCount; deviceIndex++)
	{
		struct PlatformInputDeviceInfo info;
		if (Platform_InputGetDeviceInfo(deviceIndex, &info) &&
		    info.kind == PLATFORM_INPUT_DEVICE_GAMEPAD &&
		    info.playerIndex == playerIndex)
		{
			return deviceIndex;
		}
	}

	return -1;
}

int Platform_InputAssignDeviceToPlayer(int deviceIndex, int playerIndex)
{
	struct PlatformInputDeviceInfo info;

	if (!NativeInput_IsValidControllerSlot(playerIndex) ||
	    !Platform_InputGetDeviceInfo(deviceIndex, &info))
	{
		return 0;
	}

	if (info.kind == PLATFORM_INPUT_DEVICE_KEYBOARD)
		return Platform_InputSetKeyboardPlayer(playerIndex);

	if (info.kind == PLATFORM_INPUT_DEVICE_GAMEPAD)
		return Platform_InputAssignGamepadToPlayer(info.runtimeId, playerIndex);

	return 0;
}


internal SDL_Gamepad *NativeInput_GetGamepadByRuntimeId(int runtimeId)
{
    int slot;
    for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
    {
        if (s_controllers[slot].controller != NULL &&
            s_controllers[slot].instanceId == (SDL_JoystickID)runtimeId)
        {
            return s_controllers[slot].controller;
        }
    }
    return NULL;
}

internal int NativeInput_KeyboardAnyPressed(int *outBinding)
{
    int count = 0;
    const bool *keys = SDL_GetKeyboardState(&count);
    int i;

    if (keys == NULL)
        return 0;

    if (count > SDL_SCANCODE_COUNT)
        count = SDL_SCANCODE_COUNT;

    for (i = 0; i < count; i++)
    {
        if (keys[i])
        {
            if (outBinding != NULL)
                *outBinding = i;
            return 1;
        }
    }

    return 0;
}

internal int NativeInput_GamepadAnyPressed(SDL_Gamepad *pad, int *outBinding)
{
    int i;

    if (pad == NULL || !SDL_GamepadConnected(pad))
        return 0;

    for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
    {
        if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)i))
        {
            if (outBinding != NULL)
                *outBinding = i;
            return 1;
        }
    }

    for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
    {
        int value = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)i);
        if (abs(value) >= NATIVE_INPUT_REBIND_AXIS_THRESHOLD)
        {
            if (outBinding != NULL)
            {
                *outBinding = i | NATIVE_INPUT_MAP_FLAG_AXIS;
                if (value < 0)
                    *outBinding |= NATIVE_INPUT_MAP_FLAG_INVERSE;
            }
            return 1;
        }
    }

    return 0;
}

int Platform_InputBeginRebindCapture(int deviceIndex)
{
    struct PlatformInputDeviceInfo info;

    Platform_InputCancelRebindCapture();

    if (!Platform_InputGetDeviceInfo(deviceIndex, &info) || !info.connected)
        return 0;

    if (info.kind != PLATFORM_INPUT_DEVICE_KEYBOARD &&
        info.kind != PLATFORM_INPUT_DEVICE_GAMEPAD)
        return 0;

    s_rebindCaptureKind = info.kind;
    s_rebindCaptureRuntimeId = info.runtimeId;
    s_rebindCaptureArmed = 0;
    return 1;
}

int Platform_InputPollRebindCapture(int *outBinding)
{
    int binding = -1;
    int active = 0;

    if (outBinding != NULL)
        *outBinding = -1;

    if (s_rebindCaptureKind == PLATFORM_INPUT_DEVICE_NONE)
        return -1;

    SDL_PumpEvents();

    if (s_rebindCaptureKind == PLATFORM_INPUT_DEVICE_KEYBOARD)
    {
        active = NativeInput_KeyboardAnyPressed(&binding);
    }
    else
    {
        SDL_Gamepad *pad =
            NativeInput_GetGamepadByRuntimeId(s_rebindCaptureRuntimeId);

        if (pad == NULL)
        {
            Platform_InputCancelRebindCapture();
            return -1;
        }

        active = NativeInput_GamepadAnyPressed(pad, &binding);
    }

    if (!s_rebindCaptureArmed)
    {
        if (!active)
            s_rebindCaptureArmed = 1;
        return 0;
    }

    if (!active)
        return 0;

    if (outBinding != NULL)
        *outBinding = binding;

    Platform_InputCancelRebindCapture();
    return 1;
}

void Platform_InputCancelRebindCapture(void)
{
    s_rebindCaptureKind = PLATFORM_INPUT_DEVICE_NONE;
    s_rebindCaptureRuntimeId = -1;
    s_rebindCaptureArmed = 0;
}

int Platform_InputInit(void)
{
	s32 slot;

	if (s_inputInitialized != 0)
	{
		return 1;
	}

	memset(s_controllers, 0, sizeof(s_controllers));
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].instanceId = -1;
		s_controllerToSlotMapping[slot] = -1;
		NativeInput_ResetSnapshot(slot);
		s_installedSnapshots[slot] = s_controllers[slot].snapshot;
	}

	Platform_InputReloadControls();
	s_lastActiveControllerSlot = -1;
	s_installedSnapshotsActive = 0;
	s_keyboardState = SDL_GetKeyboardState(NULL);

	if (SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC) == 0)
	{
		fprintf(stderr, "[CTR Native] Failed to initialise SDL input subsystem: %s\n", SDL_GetError());
		return 0;
	}

	SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
	NativeInput_OpenKnownControllers();

	s_inputInitialized = 1;
	return 1;
}

void Platform_InputShutdown(void)
{
	s32 slot;

	/* Glyphs own a native GL image texture, so release it while the renderer
	   context is still alive. */
	Platform_InputGlyphsShutdown();

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_CloseController(slot);
	}

	if (s_inputInitialized != 0)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
	}

	s_inputInitialized = 0;
	s_installedSnapshotsActive = 0;
	s_controlsLoaded = 0;
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	s_lastActiveControllerSlot = -1;
	s_mouseWheelY = 0.0f;
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	s_keyboardState = NULL;
}

void Platform_InputUpdate(void)
{
	u16 keyboardButtons;
	s32 slot;

	if (s_inputInitialized == 0)
	{
		return;
	}

	if (s_installedSnapshotsActive != 0)
	{
		// NOTE(aalhendi): replay/state installs PSX-shaped pad bytes here;
		// SDL host state is not serialized.
		NativeInput_WriteInstalledSnapshots();
		return;
	}

	if (g_padCommEnable == 0)
	{
		return;
	}

	SDL_PumpEvents();

	keyboardButtons = 0xffff;
	if (NativeInput_IsValidControllerSlot(s_keyboardControllerSlot) &&
	    s_playerDeviceKind[s_keyboardControllerSlot] ==
		    PLATFORM_INPUT_DEVICE_KEYBOARD &&
	    !NativeInput_KeyboardSuppressed())
	{
		keyboardButtons =
			NativeInput_ReadKeyboard(s_keyboardControllerSlot);
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_ResetSnapshot(slot);
		NativeInput_ApplyController(slot);
		NativeInput_ApplyKeyboard(slot, keyboardButtons);
	}
	NativeInput_WritePadBus();
}

void Platform_InputMouseWheel(float y)
{
	s_mouseWheelY += y;
}

void Platform_InputControllerAdded(int deviceIndex)
{
	s32 slot;

	if (s_inputInitialized == 0)
	{
		return;
	}

	slot = NativeInput_FindSlotForDeviceIndex(deviceIndex);
	if (slot >= 0)
	{
		NativeInput_OpenController(deviceIndex, slot);
		NativeInput_ReorderControllersToPreferences();
		NativeInput_WritePadBus();
	}
}

void Platform_InputControllerRemoved(int instanceId)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].instanceId == (SDL_JoystickID)instanceId)
		{
			NativeInput_CloseController(slot);
			NativeInput_UpdateGamepadPlayerIndices();
			NativeInput_WritePadBus();
			return;
		}
	}
}

int Platform_InputCycleKeyboardController(void)
{
#if PLATFORM_INPUT_TEMP_MOUSE_KEYBOARD_P1
	Platform_InputSetKeyboardPlayer(0);
	return 1;
#else
	int nextSlot =
		NativeInput_NextControllerSlot(s_keyboardControllerSlot);
	Platform_InputSetKeyboardPlayer(nextSlot);
	return nextSlot + 1;
#endif
}

int Platform_InputCycleGamepadController(void)
{
	s32 slot = NativeInput_FindActiveControllerSlot();
	s32 nextSlot;
	int runtimeId;

	if (slot < 0)
	{
		return 0;
	}

	runtimeId = (int)s_controllers[slot].instanceId;
	nextSlot = NativeInput_NextControllerSlot(slot);
#if PLATFORM_INPUT_TEMP_MOUSE_KEYBOARD_P1
	if (nextSlot == 0)
	{
		nextSlot = 1;
	}
#endif

	if (!Platform_InputAssignGamepadToPlayer(runtimeId, nextSlot))
	{
		return 0;
	}

	return nextSlot + 1;
}

void Platform_InputPadInit(int slot, unsigned char *padData)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_PHYSICAL_SLOT_COUNT))
	{
		return;
	}

	s_padSlotData[slot] = padData;
	NativeInput_WritePadBus();
}

int Platform_InputPadGetState(int port)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return PadStateDiscon;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return PadStateDiscon;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return PadStateDiscon;
	}

	return s_controllers[slot].snapshot.connected ? PadStateStable : PadStateDiscon;
}

int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count)
{
	s32 slot;

	if ((dst == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		dst[slot] = s_controllers[slot].snapshot;
	}

	return NATIVE_INPUT_MAX_CONTROLLERS;
}

int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count)
{
	s32 slot;

	if ((src == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = src[slot];
	}

	s_installedSnapshotsActive = 1;
	NativeInput_WriteInstalledSnapshots();
	return NATIVE_INPUT_MAX_CONTROLLERS;
}

void Platform_InputClearInstalledPadSnapshots(void)
{
	s_installedSnapshotsActive = 0;
}

int Platform_InputGetStateSize(void)
{
	return (int)sizeof(struct NativeInputStateSnapshot);
}

int Platform_InputCaptureState(void *dst, int dstSize)
{
	struct NativeInputStateSnapshot *snapshot = (struct NativeInputStateSnapshot *)dst;
	s32 slot;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_INPUT_STATE_MAGIC;
	snapshot->version = NATIVE_INPUT_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->keyboardControllerSlot = s_keyboardControllerSlot;
	snapshot->lastActiveControllerSlot = s_lastActiveControllerSlot;
	snapshot->installedSnapshotsActive = s_installedSnapshotsActive;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		snapshot->installedSnapshots[slot] = s_installedSnapshots[slot];
		snapshot->controllers[slot].snapshot = s_controllers[slot].snapshot;
		snapshot->controllers[slot].analogEnabled = s_controllers[slot].analogEnabled;
		snapshot->controllers[slot].switchingAnalog = s_controllers[slot].switchingAnalog;
		snapshot->controllers[slot].controllerToSlotMapping = s_controllerToSlotMapping[slot];
	}

	return 1;
}

int Platform_InputRestoreState(const void *src, int srcSize)
{
	const struct NativeInputStateSnapshot *snapshot = (const struct NativeInputStateSnapshot *)src;
	s32 slot;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_INPUT_STATE_MAGIC) || (snapshot->version != NATIVE_INPUT_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if (snapshot->keyboardControllerSlot < -1 ||
	    snapshot->keyboardControllerSlot >= NATIVE_INPUT_MAX_CONTROLLERS)
	{
		return 0;
	}
	if ((snapshot->lastActiveControllerSlot < -1) || (snapshot->lastActiveControllerSlot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}
	if ((snapshot->installedSnapshotsActive < 0) || (snapshot->installedSnapshotsActive > 1))
	{
		return 0;
	}
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((snapshot->controllers[slot].analogEnabled < 0) || (snapshot->controllers[slot].analogEnabled > 1))
		{
			return 0;
		}
		if ((snapshot->controllers[slot].switchingAnalog < 0) || (snapshot->controllers[slot].switchingAnalog > 1))
		{
			return 0;
		}
		if (snapshot->controllers[slot].controllerToSlotMapping < -1)
		{
			return 0;
		}
	}

	s_keyboardControllerSlot = snapshot->keyboardControllerSlot;
	s_lastActiveControllerSlot = snapshot->lastActiveControllerSlot;
	s_installedSnapshotsActive = snapshot->installedSnapshotsActive != 0;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = snapshot->installedSnapshots[slot];
		s_controllers[slot].snapshot = snapshot->controllers[slot].snapshot;
		s_controllers[slot].analogEnabled = snapshot->controllers[slot].analogEnabled;
		s_controllers[slot].switchingAnalog = snapshot->controllers[slot].switchingAnalog;
		s_controllerToSlotMapping[slot] = snapshot->controllers[slot].controllerToSlotMapping;
	}
	NativeInput_WritePadBus();

	return 1;
}

void Platform_InputPadVibrate(int port, unsigned char *table, int len)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;
	struct NativeInputController *controller;
	u16 freqHigh;
	u16 freqLow;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS) || (table == NULL) || (len <= 0))
	{
		return;
	}

	controller = &s_controllers[slot];
	if (controller->controller == NULL)
	{
		return;
	}

	freqHigh = table[0] * 255;
	freqLow = len > 1 ? table[1] * 255 : 0;

	if ((freqLow != 0) && (freqLow < 4096))
	{
		freqLow = 4096;
	}

	if ((freqHigh != 0) && (freqHigh < 4096))
	{
		freqHigh = 4096;
	}

	SDL_RumbleGamepad(controller->controller, freqLow, freqHigh, 200);
}
