/* Crash Forge: Racing — independent menu music scheduler.
 *
 * The PS1 VBlank tick comes from the main/render thread. Keeping HOWL's
 * sequencer on that callback lets a blocking menu/driver/asset operation
 * hold back a CSEQ instrument for 50-200 ms. The SDL audio device keeps
 * mixing old voices meanwhile, causing an audible split between layers.
 *
 * Run ONLY HOWL's sequencer on this dedicated 60-Hz thread while a menu
 * is fully loaded. No game/physics/input/VBlank callbacks leave the main
 * thread. During level loads and gameplay the retail clock is restored.
 *
 * HOWL Smart_Enter/ExitCriticalSection uses the same mutex while this
 * scheduler is active. The per-thread recursive depth avoids SDL mutex
 * recursion assumptions when HOWL's own update invokes volume helpers.
 */
#include <platform/native_menu_music_clock.h>
#include <SDL3/SDL.h>

static SDL_Mutex *s_menuMusicMutex;
static SDL_Thread *s_menuMusicThread;
static SDL_AtomicInt s_menuMusicRunning;

#if defined(_MSC_VER)
static __declspec(thread) int s_menuMusicLockDepth;
#else
static __thread int s_menuMusicLockDepth;
#endif

void NativeMenuMusicClock_Lock(void)
{
    if (!s_menuMusicMutex)
        return;

    if (s_menuMusicLockDepth++ == 0)
        SDL_LockMutex(s_menuMusicMutex);
}

void NativeMenuMusicClock_Unlock(void)
{
    if (!s_menuMusicMutex || s_menuMusicLockDepth <= 0)
        return;

    if (--s_menuMusicLockDepth == 0)
        SDL_UnlockMutex(s_menuMusicMutex);
}

int NativeMenuMusicClock_IsRunning(void)
{
    return SDL_GetAtomicInt(&s_menuMusicRunning) != 0;
}

static int SDLCALL NativeMenuMusicClock_ThreadMain(void *userdata)
{
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 period = freq / 60u;
    Uint64 nextTick = SDL_GetPerformanceCounter() + period;
    Uint64 lastTick = 0;
    int lateEvents = 0;
    unsigned int clockTicks = 0;
    unsigned int skippedTicks = 0;
    (void)userdata;

    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    while (NativeMenuMusicClock_IsRunning())
    {
        Uint64 now = SDL_GetPerformanceCounter();

        if (now < nextTick)
        {
            const Uint64 ns = (nextTick - now) * 1000000000ULL / freq;
            if (ns > 450000)
                SDL_DelayPrecise(ns - 250000);
            else
                SDL_DelayPrecise(ns);
            continue;
        }

        /* Keep the tiny HOWL update independent from full-scene rendering,
         * but never race an SFX, CSEQ, channel, or song-pool write. */
        NativeMenuMusicClock_Lock();
        if (sdata != NULL && sdata->boolAudioEnabled != 0 &&
            sdata->criticalSectionCount == 0)
        {
            howl_PlayAudio_Update();
            clockTicks++;
        }
        else
            skippedTicks++;
        NativeMenuMusicClock_Unlock();

        /* Verify that this thread really advances musical note timing,
         * not merely that the thread was created. Lightweight 10-s stats
         * are also useful when diagnosing a melody stutter report. */
        if (clockTicks > 0 && clockTicks % 600u == 0)
            Platform_Log("[MENU MUSIC CLOCK] ticks=%u skipped=%u (60 Hz independent)\n",
                         clockTicks, skippedTicks);

        now = SDL_GetPerformanceCounter();
        if (lastTick && (now - lastTick) > freq / 20 &&
            lateEvents < 16)
        {
            const double ms = (double)(now - lastTick) * 1000.0 / (double)freq;
            Platform_Log("[MENU MUSIC CLOCK] background HOWL tick delayed %.1f ms (%d/16)\n",
                         ms, ++lateEvents);
        }
        lastTick = now;

        /* Never rush multiple CSEQ ticks after a Windows scheduler pause:
         * dropped time is preferable to a burst of piled-up note-ons. */
        if (now > nextTick + period * 2u)
            nextTick = now + period;
        else
            nextTick += period;
    }

    return 0;
}

int NativeMenuMusicClock_Start(void)
{
    if (NativeMenuMusicClock_IsRunning())
        return 1;
    if (!sdata || !sdata->boolAudioEnabled ||
        NativeAudio_IsDeterministicRenderMode())
        return 0;

    if (!s_menuMusicMutex)
    {
        s_menuMusicMutex = SDL_CreateMutex();
        if (!s_menuMusicMutex)
        {
            Platform_LogWarn("[MENU MUSIC CLOCK] mutex unavailable: %s\n", SDL_GetError());
            return 0;
        }
    }

    SDL_SetAtomicInt(&s_menuMusicRunning, 1);
    s_menuMusicThread = SDL_CreateThread(
        NativeMenuMusicClock_ThreadMain, "CFR_MenuHOWL_60Hz", NULL);
    if (!s_menuMusicThread)
    {
        SDL_SetAtomicInt(&s_menuMusicRunning, 0);
        Platform_LogWarn("[MENU MUSIC CLOCK] thread unavailable; using VBlank fallback: %s\n",
                         SDL_GetError());
        return 0;
    }

    Platform_Log("[MENU MUSIC CLOCK] independent 60-Hz sequencer START (menu only)\n");
    return 1;
}

void NativeMenuMusicClock_Stop(void)
{
    if (s_menuMusicThread != NULL)
    {
        SDL_SetAtomicInt(&s_menuMusicRunning, 0);
        SDL_WaitThread(s_menuMusicThread, NULL);
        s_menuMusicThread = NULL;
        Platform_Log("[MENU MUSIC CLOCK] sequencer STOP (VBlank/gameplay fallback)\n");
    }
}

void NativeMenuMusicClock_Shutdown(void)
{
    NativeMenuMusicClock_Stop();
    if (s_menuMusicMutex != NULL)
    {
        SDL_DestroyMutex(s_menuMusicMutex);
        s_menuMusicMutex = NULL;
    }
}
