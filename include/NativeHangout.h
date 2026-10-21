#ifndef NATIVE_HANGOUT_H
#define NATIVE_HANGOUT_H

#if defined(CTR_NATIVE)

/* Virtual music target used only by Hangout. It deliberately does not reuse
 * N_SANITY_BEACH (26), so Hangout can have independent OGG/CSEQ overrides. */
#define NATIVE_HANGOUT_MUSIC_LEVEL_ID 99

/* Reserved native RectMenu row ID. Retail language tables never index this
 * value; RECTMENU resolves it to the native localization key below. */
#define NATIVE_HANGOUT_MENU_CONNECT_STRING 0x7ffc
#define NATIVE_HANGOUT_MENU_DISCONNECT_STRING 0x7ffd
#define NATIVE_HANGOUT_MENU_CUSTOMIZATION_STRING 0x7ffe

struct GameTracker;
struct Instance;

void NativeHangout_RequestLaunch(void);
int NativeHangout_ConsumeLaunchRequest(void);

void NativeHangout_RequestExit(void);
int NativeHangout_ConsumeExitRequest(void);
void NativeHangout_RequestRaceTest(void);
int NativeHangout_ShouldSuppressRetailWarpSfx(void);

void NativeHangout_SetActive(int active);
int NativeHangout_IsActive(void);

void NativeHangout_Update(struct GameTracker *gGT);
void NativeHangout_Render(struct GameTracker *gGT);
void NativeHangout_BindSaveScreen(struct Instance *inst);

int NativeHangout_HandleHostKey(int scancode, int down, int repeat);
void NativeHangout_HandleTextInput(const char *text);
int NativeHangout_ChatIsOpen(void);
void NativeHangout_SetMenuOpen(int open);
int NativeHangout_IsMenuOpen(void);
void NativeHangout_SetCustomizationOpen(int open);
int NativeHangout_IsCustomizationOpen(void);
void NativeHangout_RequestCustomizationTransition(void);
void NativeHangout_RequestCustomizationExitTransition(int apply);
int NativeHangout_IsCustomizationTransitionActive(void);
int NativeHangout_IsPlayerDisconnected(void);
void NativeHangout_DisconnectPlayer(void);
void NativeHangout_ConnectPlayer(void);
void NativeHangout_PlayCustomizationDoneSfx(void);
void NativeHangout_PrepareDisconnectedCustomization(void);
void NativeHangout_FinishDisconnectedCustomization(int apply);
int NativeHangout_ShouldHideGameplayHud(void);
int NativeHangout_ShouldBlockGameplayInput(void);

#endif

#endif
