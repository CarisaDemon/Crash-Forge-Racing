#ifndef PLATFORM_NATIVE_AUDIO_H
#define PLATFORM_NATIVE_AUDIO_H

#include <macros.h>
#include <psx/libspu.h>

s32 NativeAudio_SpuInit(void);
u32 NativeAudio_SpuSetTransferStartAddr(u32 addr);
u32 NativeAudio_SpuWrite(const u8 *addr, u32 size);
void NativeAudio_SpuSetVoiceAttr(SpuVoiceAttr *psxAttrib);
void NativeAudio_SpuSetKey(s32 on_off, u32 voice_bit);
s32 NativeAudio_SpuSetReverb(s32 on_off);
s32 NativeAudio_SpuSetReverbModeParam(SpuReverbAttr *attr);
void NativeAudio_SpuSetReverbModeDepth(s16 left, s16 right);
u32 NativeAudio_SpuSetReverbVoice(s32 on_off, u32 voice_bit);
void NativeAudio_SpuSetCommonMasterVolume(s16 left, s16 right);
void NativeAudio_SpuSetCommonCDMix(s32 enabled);
void NativeAudio_SpuSetCommonCDVolume(s16 left, s16 right);
void NativeAudio_SpuSetCommonCDReverb(s32 enabled);

int NativeAudio_PlayXATrack(int categoryID, int xaID, int volumeLeft, int volumeRight);
int NativeAudio_PlayXAFile(const char *relativePath, int channelFilter, int volumeLeft, int volumeRight);
int NativeAudio_GetXATrackLength(int categoryID, int xaID);
int NativeAudio_IsXAPlaying(void);
int NativeAudio_GetXACurrOffset(void);
int NativeAudio_GetXAMaxSample(void);
int NativeAudio_GetXAMaxSampleAtOffset(int xaCurrOffset);
void NativeAudio_SetXAVolume(int volumeLeft, int volumeRight);
void NativeAudio_StopXA(void);
void NativeAudio_StepVBlank(void);
int NativeAudio_RenderFrames(s16 *out, int frameCount);
void NativeAudio_SetDeterministicRenderMode(int enabled);
int NativeAudio_IsDeterministicRenderMode(void);
int NativeAudio_QueueRenderedFrames(const s16 *frames, int frameCount);
void NativeAudio_ClearOutputQueue(void);
void NativeAudio_Shutdown(void);
/* Player engine overrides; paths are absolute pre-resolved mod asset paths.
 * LOOP is mandatory; IDLE and TURBO are optional, retail is the fallback. */
int NativeAudio_EngineBind(int playerID, const char *loop, const char *idle, const char *turbo);
/* Immediate game SFX/FX volume linkage, including while paused/in options.
 * Unlike per-driver update, this also works with zero gameplay ticks. */
void NativeAudio_EngineSetFxVolume(int fxVolume);
/* Bot motors use real CTR nearest-racer volume/panning and respect YES/NO.
 * API uses the actual driverID; player and AI engine voices never alias. */
void NativeAudio_EngineSetHearOtherRacers(int enabled);
void NativeAudio_EngineBeginOpponentFrame(void);
void NativeAudio_EngineMarkOpponent(int driverID);
void NativeAudio_EngineSetOpponentSpatial(int driverID, int pan);
void NativeAudio_EngineSetPaused(int paused); /* PCM only, HOWL owns retail pause */
void NativeAudio_EnginePreserveNextHowlStop(void);
int NativeAudio_EngineConsumePreserveOnHowlStop(void);
/* Optional PRIMARY engine pitch curve selected from the kart that owns the
 * chosen engine, independent of current character/physics/secondary audio.
 * speedMax=0 follows the driver's current physics USF top speed. */
int NativeAudio_EngineSetPitchCurve(int playerID, float idlePitch,
                                    float usfPitch, int speedMax, int rpmMode);
#define NATIVE_KART_AUDIO_LAYER_COUNT 3
/* Modes match the optional [audio] config; layers do not require custom
 * primary engine: they also work with the retail HOWL motor. */
enum NativeKartLayerMode {
    NATIVE_KART_AUDIO_RPM = 0,
    NATIVE_KART_AUDIO_SPEED = 1,
    NATIVE_KART_AUDIO_ACCEL = 2,
    NATIVE_KART_AUDIO_DRIFT = 3,
    NATIVE_KART_AUDIO_TURBO = 4
};
int NativeAudio_EngineBindLayer(int playerID,int layerIndex,const char *path,
                                int mode,float volumePercent,float pitchMin,
                                float pitchMax,int speedMax);
/* Independent, optional speed-driven engine layer (e.g. straight-cut gears).
 * Works with BOTH retail and custom primary engines. No file means disabled. */
int NativeAudio_EngineBindSecondary(int playerID, const char *loop,
                                    float volumePercent, float pitchMin,
                                    float pitchMax, int speedMax);
/* speedApprox is the actual signed kart speed. Unlike the retail 0..255
 * distortion parameter, this does not saturate midway through acceleration. */
int NativeAudio_EngineUpdate(int playerID, int volume, int distortion,
                             int boost, int fxVolume, int speedApprox,
                             int physicsUSFTopSpeed, int howlEngineRpm,
                             int howlEngineMaxRpm, int drifting, int revving,
                             int driftMeterRoomLeft, int driftMeterMaxRoom,
                             int ctrRetailPitchTableValue);
void NativeAudio_EngineStop(int playerID);
void NativeAudio_EngineUnbindAll(void);
/* GameCube CNK kartbrv3, 1x one-shot on the FIRST positive charge frame. */
int NativeAudio_CNKChargePreload(const char *path);
void NativeAudio_CNKChargePlay(int localPlayerIndex);
void NativeAudio_CNKChargeShutdown(void);
/* CNK Tornado Top SFX: 0=launch/death, 1=lift/impact, 2=travel loop. */
void NativeAudio_CNKTornadoPlay(int effect);
void NativeAudio_CNKTornadoStop(void);
/* Online/Hangout CNK one-shots. IDs mirror NativeOnlineSfxEvent:
 * 0=disconnect, 1=warp warning, 2=warp execute, 3=map reminder. */
int NativeAudio_OnlineSfxPreload(void);
void NativeAudio_OnlineSfxPlay(int effect);
void NativeAudio_OnlineSfxShutdown(void);
void NativeAudio_EngineMixFrameNoLock(int *left, int *right, int masterLeft, int masterRight);
#ifdef CTR_INTERNAL
void NativeAudio_GetOutputStats(int *underrunFrames, int *overflowFrames, int *queuedFrames);
#endif
int NativeAudio_GetStateSize(void);
int NativeAudio_CaptureState(void *dst, int dstSize);
int NativeAudio_RestoreState(const void *src, int srcSize);

#endif
