#include "NativeOnlineSfx.h"

#include <string.h>

void NativeOnlineSfx_Reset(struct NativeOnlineSfxState *state)
{
    if (state == 0)
        return;

    memset(state, 0, sizeof(*state));
    state->lastRaceWarpSecond = -1;
    state->lastFinalCountdownSecond = -1;
}

void NativeOnlineSfx_SetMapPickCountdown(
    struct NativeOnlineSfxState *state,
    int active,
    unsigned int authoritativeNowMs)
{
    if (state == 0)
        return;

    if (!active)
    {
        state->mapPickCountdownActive = 0;
        state->mapPickStartMs = 0;
        state->lastMapPick10sBucket = 0;
        return;
    }

    if (!state->mapPickCountdownActive)
    {
        state->mapPickCountdownActive = 1;
        state->mapPickStartMs = authoritativeNowMs;
        state->lastMapPick10sBucket = 0;
    }
}

void NativeOnlineSfx_UpdateMapPickCountdown(
    struct NativeOnlineSfxState *state,
    unsigned int authoritativeNowMs,
    NativeOnlineSfxPlayFn play,
    void *user)
{
    unsigned int elapsed;
    unsigned int bucket;

    if (state == 0 || !state->mapPickCountdownActive || play == 0)
        return;

    elapsed = authoritativeNowMs - state->mapPickStartMs;
    bucket = elapsed / 10000u;

    if (bucket > state->lastMapPick10sBucket)
    {
        state->lastMapPick10sBucket = bucket;
        play(NATIVE_ONLINE_SFX_MAP_PICK_REMINDER, user);
    }
}

void NativeOnlineSfx_SetRaceWarpCountdown(
    struct NativeOnlineSfxState *state,
    int active)
{
    if (state == 0)
        return;

    if (!active)
    {
        state->raceWarpCountdownActive = 0;
        state->lastRaceWarpSecond = -1;
        state->lastFinalCountdownSecond = -1;
        state->warpExecuted = 0;
        return;
    }

    if (!state->raceWarpCountdownActive)
    {
        state->raceWarpCountdownActive = 1;
        state->lastRaceWarpSecond = -1;
        state->lastFinalCountdownSecond = -1;
        state->warpExecuted = 0;
    }
}

void NativeOnlineSfx_UpdateRaceWarpCountdown(
    struct NativeOnlineSfxState *state,
    int authoritativeMsRemaining,
    NativeOnlineSfxPlayFn play,
    void *user)
{
    int wholeSecond;

    if (state == 0 || !state->raceWarpCountdownActive || play == 0)
        return;

    if (authoritativeMsRemaining <= 0)
    {
        if (!state->warpExecuted)
        {
            state->warpExecuted = 1;
            play(NATIVE_ONLINE_SFX_WARP_EXECUTE, user);
        }
        return;
    }

    wholeSecond = (authoritativeMsRemaining + 999) / 1000;

    if (wholeSecond >= 1 &&
        wholeSecond <= 10 &&
        wholeSecond != state->lastFinalCountdownSecond)
    {
        state->lastFinalCountdownSecond = wholeSecond;
        play(NATIVE_ONLINE_SFX_FINAL_COUNTDOWN_TICK, user);
    }

    if (wholeSecond >= 1 &&
        wholeSecond <= 3 &&
        wholeSecond != state->lastRaceWarpSecond)
    {
        state->lastRaceWarpSecond = wholeSecond;
        if (wholeSecond == 3)
            play(NATIVE_ONLINE_SFX_COUNTDOWN_UNLOCK, user);
        play(NATIVE_ONLINE_SFX_WARP_WARNING, user);
    }
}

void NativeOnlineSfx_PlayerDisconnected(
    struct NativeOnlineSfxState *state,
    NativeOnlineSfxPlayFn play,
    void *user)
{
    (void)state;

    if (play != 0)
        play(NATIVE_ONLINE_SFX_PLAYER_DISCONNECT, user);
}

void NativeOnlineSfx_PlayerJoined(
    struct NativeOnlineSfxState *state,
    int variant,
    NativeOnlineSfxPlayFn play,
    void *user)
{
    (void)state;

    if (play == 0)
        return;

    play((variant & 1) != 0 ?
        NATIVE_ONLINE_SFX_PLAYER_JOIN_B :
        NATIVE_ONLINE_SFX_PLAYER_JOIN_A,
        user);
}

const char *NativeOnlineSfx_GetAssetName(enum NativeOnlineSfxEvent eventId)
{
    switch (eventId)
    {
        case NATIVE_ONLINE_SFX_PLAYER_DISCONNECT:
            return "misscam.wav";
        case NATIVE_ONLINE_SFX_WARP_WARNING:
            return "gateactv.wav";
        case NATIVE_ONLINE_SFX_WARP_EXECUTE:
            return "warpaway.wav";
        case NATIVE_ONLINE_SFX_MAP_PICK_REMINDER:
            return "clktick2.wav";
        case NATIVE_ONLINE_SFX_PLAYER_JOIN_A:
            return "gatwarp2.wav";
        case NATIVE_ONLINE_SFX_PLAYER_JOIN_B:
            return "gatwarp3.wav";
        case NATIVE_ONLINE_SFX_FINAL_COUNTDOWN_TICK:
            return "clktick3.wav";
        case NATIVE_ONLINE_SFX_CUSTOMIZATION_DONE:
            return "magtran2.wav";
        case NATIVE_ONLINE_SFX_COUNTDOWN_UNLOCK:
            return "gateunlk.wav";
        case NATIVE_ONLINE_SFX_CHAT_PLAYER:
            return "wfdown.wav";
        case NATIVE_ONLINE_SFX_CHAT_SYSTEM:
            return "stoplit1.wav";
        default:
            return "";
    }
}
