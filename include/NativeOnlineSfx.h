#ifndef CFR_NATIVE_ONLINE_SFX_H
#define CFR_NATIVE_ONLINE_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

enum NativeOnlineSfxEvent
{
    NATIVE_ONLINE_SFX_PLAYER_DISCONNECT = 0,
    NATIVE_ONLINE_SFX_WARP_WARNING,
    NATIVE_ONLINE_SFX_WARP_EXECUTE,
    NATIVE_ONLINE_SFX_MAP_PICK_REMINDER,
    NATIVE_ONLINE_SFX_PLAYER_JOIN_A,
    NATIVE_ONLINE_SFX_PLAYER_JOIN_B,
    NATIVE_ONLINE_SFX_FINAL_COUNTDOWN_TICK,
    NATIVE_ONLINE_SFX_CUSTOMIZATION_DONE,
    NATIVE_ONLINE_SFX_COUNTDOWN_UNLOCK,
    NATIVE_ONLINE_SFX_CHAT_PLAYER,
    NATIVE_ONLINE_SFX_CHAT_SYSTEM
};

typedef void (*NativeOnlineSfxPlayFn)(enum NativeOnlineSfxEvent eventId, void *user);

struct NativeOnlineSfxState
{
    int mapPickCountdownActive;
    unsigned int mapPickStartMs;
    unsigned int lastMapPick10sBucket;

    int raceWarpCountdownActive;
    int lastRaceWarpSecond;
    int lastFinalCountdownSecond;
    int warpExecuted;
};

void NativeOnlineSfx_Reset(struct NativeOnlineSfxState *state);
void NativeOnlineSfx_SetMapPickCountdown(
    struct NativeOnlineSfxState *state,
    int active,
    unsigned int authoritativeNowMs);
void NativeOnlineSfx_UpdateMapPickCountdown(
    struct NativeOnlineSfxState *state,
    unsigned int authoritativeNowMs,
    NativeOnlineSfxPlayFn play,
    void *user);
void NativeOnlineSfx_SetRaceWarpCountdown(
    struct NativeOnlineSfxState *state,
    int active);
void NativeOnlineSfx_UpdateRaceWarpCountdown(
    struct NativeOnlineSfxState *state,
    int authoritativeMsRemaining,
    NativeOnlineSfxPlayFn play,
    void *user);
void NativeOnlineSfx_PlayerDisconnected(
    struct NativeOnlineSfxState *state,
    NativeOnlineSfxPlayFn play,
    void *user);
void NativeOnlineSfx_PlayerJoined(
    struct NativeOnlineSfxState *state,
    int variant,
    NativeOnlineSfxPlayFn play,
    void *user);

const char *NativeOnlineSfx_GetAssetName(enum NativeOnlineSfxEvent eventId);

#ifdef __cplusplus
}
#endif

#endif
