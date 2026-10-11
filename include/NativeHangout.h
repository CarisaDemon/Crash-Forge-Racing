#ifndef NATIVE_HANGOUT_H
#define NATIVE_HANGOUT_H

#if defined(CTR_NATIVE)

/* Virtual music target used only by Hangout. It deliberately does not reuse
 * N_SANITY_BEACH (26), so Hangout can have independent OGG/CSEQ overrides. */
#define NATIVE_HANGOUT_MUSIC_LEVEL_ID 99

struct GameTracker;
struct Instance;

void NativeHangout_RequestLaunch(void);
int NativeHangout_ConsumeLaunchRequest(void);

void NativeHangout_RequestExit(void);
int NativeHangout_ConsumeExitRequest(void);

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
int NativeHangout_ShouldBlockGameplayInput(void);

#endif

#endif
