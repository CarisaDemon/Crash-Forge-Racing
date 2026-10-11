#ifndef PLATFORM_NATIVE_INPUT_H
#define PLATFORM_NATIVE_INPUT_H

#include <macros.h>

#define PLATFORM_INPUT_PAD_COUNT 4

/* Temporary test mode kept for compatibility with older builds. */
#define PLATFORM_INPUT_TEMP_MOUSE_KEYBOARD_P1 0

#define PLATFORM_INPUT_BIND_AXIS    0x4000
#define PLATFORM_INPUT_BIND_INVERSE 0x8000
#define PLATFORM_INPUT_DEVICE_NAME_MAX 96

enum PlatformInputDeviceKind
{
    PLATFORM_INPUT_DEVICE_NONE = 0,
    PLATFORM_INPUT_DEVICE_KEYBOARD,
    PLATFORM_INPUT_DEVICE_GAMEPAD,
};

enum PlatformInputGlyphStyle
{
    PLATFORM_INPUT_GLYPH_AUTO = 0,
    PLATFORM_INPUT_GLYPH_PLAYSTATION,
    PLATFORM_INPUT_GLYPH_XBOX,
    PLATFORM_INPUT_GLYPH_GENERIC,
    PLATFORM_INPUT_GLYPH_KEYBOARD,
    PLATFORM_INPUT_GLYPH_NINTENDO,
    PLATFORM_INPUT_GLYPH_STYLE_COUNT
};

enum PlatformInputAction
{
    PLATFORM_INPUT_ACTION_SQUARE = 0,
    PLATFORM_INPUT_ACTION_CIRCLE,
    PLATFORM_INPUT_ACTION_TRIANGLE,
    PLATFORM_INPUT_ACTION_CROSS,
    PLATFORM_INPUT_ACTION_L1,
    PLATFORM_INPUT_ACTION_L2,
    PLATFORM_INPUT_ACTION_L3,
    PLATFORM_INPUT_ACTION_R1,
    PLATFORM_INPUT_ACTION_R2,
    PLATFORM_INPUT_ACTION_R3,
    PLATFORM_INPUT_ACTION_START,
    PLATFORM_INPUT_ACTION_SELECT,
    PLATFORM_INPUT_ACTION_DPAD_LEFT,
    PLATFORM_INPUT_ACTION_DPAD_RIGHT,
    PLATFORM_INPUT_ACTION_DPAD_UP,
    PLATFORM_INPUT_ACTION_DPAD_DOWN,
    PLATFORM_INPUT_ACTION_LEFT_X,
    PLATFORM_INPUT_ACTION_LEFT_Y,
    PLATFORM_INPUT_ACTION_RIGHT_X,
    PLATFORM_INPUT_ACTION_RIGHT_Y,
    PLATFORM_INPUT_ACTION_LEFT_STICK_LEFT,
    PLATFORM_INPUT_ACTION_LEFT_STICK_RIGHT,
    PLATFORM_INPUT_ACTION_LEFT_STICK_UP,
    PLATFORM_INPUT_ACTION_LEFT_STICK_DOWN,
    PLATFORM_INPUT_ACTION_RIGHT_STICK_LEFT,
    PLATFORM_INPUT_ACTION_RIGHT_STICK_RIGHT,
    PLATFORM_INPUT_ACTION_RIGHT_STICK_UP,
    PLATFORM_INPUT_ACTION_RIGHT_STICK_DOWN,
    PLATFORM_INPUT_ACTION_COUNT
};

struct PlatformInputDeviceInfo
{
    int kind;
    int runtimeId;
    int playerIndex;
    int connected;
    int gamepadType;
    int resolvedGlyphStyle;
    char name[PLATFORM_INPUT_DEVICE_NAME_MAX];
};

struct PlatformInputPadSnapshot
{
    u8 status;
    u8 id;
    u8 buttons[2];
    u8 analog[4];
    u8 connected;
    u8 reserved[3];
};

int Platform_InputInit(void);
void Platform_InputShutdown(void);
void Platform_InputUpdate(void);
void Platform_InputControllerAdded(int deviceIndex);
void Platform_InputControllerRemoved(int instanceId);
void Platform_InputMouseWheel(float y);
int Platform_InputCycleKeyboardController(void);
int Platform_InputCycleGamepadController(void);

/* Menu-only keyboard alias. Returns pressed PSX RAW_BTN_* bits as active-high
   input without changing the player's gameplay device assignment. */
unsigned int Platform_InputGetMenuKeyboardRawButtons(int playerIndex);

/* Backend for the future Controls menu. Device index 0 is always keyboard;
   gamepads follow in their current connected order. Player indices are 0..3. */
int Platform_InputGetDeviceCount(void);
int Platform_InputGetDeviceInfo(int deviceIndex, struct PlatformInputDeviceInfo *outInfo);
int Platform_InputAssignDeviceToPlayer(int deviceIndex, int playerIndex);
int Platform_InputGetKeyboardPlayer(void);
int Platform_InputSetKeyboardPlayer(int playerIndex);
int Platform_InputGetGamepadPlayer(int runtimeId);
int Platform_InputAssignGamepadToPlayer(int runtimeId, int playerIndex);
int Platform_InputGetPlayerDeviceKind(int playerIndex);
int Platform_InputSetPlayerDeviceKind(int playerIndex, int kind);
int Platform_InputGetPlayerDeviceIndex(int playerIndex);
int Platform_InputGetPlayerGlyphStyle(int playerIndex);
int Platform_InputSetPlayerGlyphStyle(int playerIndex, int style);
int Platform_InputResolvePlayerGlyphStyle(int playerIndex);
const char *Platform_InputGetGlyphStyleName(int style);

/* Rebinding backend. Bindings are stored per logical player. Keyboard
   bindings use SDL scancode integer values. Gamepad bindings use
   SDL_GamepadButton, or SDL_GamepadAxis | AXIS flag. */
int Platform_InputGetKeyboardBindingForPlayer(int playerIndex, int action);
int Platform_InputSetKeyboardBindingForPlayer(int playerIndex, int action, int binding);
int Platform_InputGetGamepadBindingForPlayer(int playerIndex, int action);
int Platform_InputSetGamepadBindingForPlayer(int playerIndex, int action, int binding);
void Platform_InputResetBindingsForPlayer(int playerIndex);

/* Compatibility wrappers use Player 1. */
int Platform_InputGetKeyboardBinding(int action);
int Platform_InputSetKeyboardBinding(int action, int binding);
int Platform_InputGetGamepadBinding(int action);
int Platform_InputSetGamepadBinding(int action, int binding);
void Platform_InputResetBindings(void);
int Platform_InputSaveControls(void);
int Platform_InputReloadControls(void);
const char *Platform_InputGetActionName(int action);
const char *Platform_InputGetActionDisplayName(int action);
int Platform_InputFormatBindingName(int deviceKind, int binding, char *out, int outSize);
int Platform_InputFormatPlayerBindingName(int playerIndex, int action, char *out, int outSize);

/* Optional capture helper for the future rebind UI. Begin waits for all
   controls on that device to return to neutral, then Poll returns the next
   key/button/axis as an encoded binding. */
int Platform_InputBeginRebindCapture(int deviceIndex);
int Platform_InputPollRebindCapture(int *outBinding);
void Platform_InputCancelRebindCapture(void);

void Platform_InputPadInit(int slot, unsigned char *padData);
int Platform_InputPadGetState(int port);
void Platform_InputPadVibrate(int port, unsigned char *table, int len);
int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count);
int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count);
void Platform_InputClearInstalledPadSnapshots(void);
int Platform_InputGetStateSize(void);
int Platform_InputCaptureState(void *dst, int dstSize);
int Platform_InputRestoreState(const void *src, int srcSize);

#endif
