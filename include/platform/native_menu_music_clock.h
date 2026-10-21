#ifndef CTR_NATIVE_MENU_MUSIC_CLOCK_H
#define CTR_NATIVE_MENU_MUSIC_CLOCK_H

/* Host-side HOWL sequencer scheduler used only after a menu is loaded.
 * Gameplay, PS1 VBlank, input, rendering and TAS keep their original clock.
 * The SDL audio callback stays strictly PCM-only. */
int NativeMenuMusicClock_Start(void);
void NativeMenuMusicClock_Stop(void);
void NativeMenuMusicClock_Shutdown(void);
int NativeMenuMusicClock_IsRunning(void);

/* Recursive per-thread exclusion shared with HOWL Smart_*CriticalSection.
 * Never acquire this while holding the SDL audio stream lock. */
void NativeMenuMusicClock_Lock(void);
void NativeMenuMusicClock_Unlock(void);

#endif
