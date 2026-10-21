/* Host PCM engine loops. Audio is decoded before race playback and mixed
 * through the existing 44.1 kHz SDL output, never decoded on the audio thread.
 * The original HOWL engine bank remains the fallback for missing/invalid mods. */
#include <platform/native_audio.h>
#include <SDL3/SDL.h>

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Four split-screen humans plus up to seven bot drivers share CTR's
 * eight real driver IDs. Local + opponent voices never share a driver ID. */
#define NATIVE_ENGINE_PLAYERS 8
#define NATIVE_ENGINE_TRACKS (4 + NATIVE_KART_AUDIO_LAYER_COUNT)
/* track 0=main engine,1=idle,2=turbo,3=legacy secondary speed layer,
 * tracks 4..6 are per-kart optional simultaneous sound loops. */
#define NATIVE_ENGINE_RATE 44100
#define NATIVE_ENGINE_MAX_SECONDS 24
/* CTR's original engine pitch is a nonlinear frequency multiplier table.
 * The gameplay code passes its exact table entry to this native mixer. */
struct NativeEnginePcm {
    s16 *data;
    int frames;
    int channels;
    int rate;
    float loudnessTrim; /* per-sample RMS normalization; never boost audio */
};

struct NativeEngineVoice {
    struct NativeEnginePcm clip[NATIVE_ENGINE_TRACKS];
    uint64_t phase[NATIVE_ENGINE_TRACKS];
    uint32_t step[NATIVE_ENGINE_TRACKS];
    int active;
    int opponent; /* 1 = BOT nearest-corral driver voice */
    int opponentVisible; /* updated once per CTR audio tick */
    int opponentPan; /* CTR EngineSound_NearestAIs left/right 0..255 */
    float opponentEnvelope; /* anti-pop proximity/setting ramp */
    int volume;
    int fxVolume;
    int distortion;
    int ctrRetailPitchTableValue; /* EXACT data.distortConst_Engine[distortion] */
    int speedAbs; /* physical velocity, independent of HOWL distortion cap */
    int physicsUSFTopSpeed; /* source driver's physics-normalized max speed */
    int primaryCurveActive;
    int primaryMode; /* 0=road speed,1=HOWL smoothed RPM */
    int howlRpm;
    int howlRpmMax;
    int drifting, revving, accelTicks;
    /* 0.0 = freshly started powerslide, 1.0 = fully charged turbo meter.
     * Same exact source as the retail/CNK slide gauge. */
    float driftCharge;
    int extraActive[NATIVE_KART_AUDIO_LAYER_COUNT];
    int extraMode[NATIVE_KART_AUDIO_LAYER_COUNT];
    float extraGain[NATIVE_KART_AUDIO_LAYER_COUNT];
    float extraPitchMin[NATIVE_KART_AUDIO_LAYER_COUNT];
    float extraPitchMax[NATIVE_KART_AUDIO_LAYER_COUNT];
    int extraSpeedMax[NATIVE_KART_AUDIO_LAYER_COUNT]; /* 0=physics */
    float extraEnvelope[NATIVE_KART_AUDIO_LAYER_COUNT];
    uint32_t smoothedExtraStep[NATIVE_KART_AUDIO_LAYER_COUNT];
    float primaryPitchMin;
    float primaryPitchMax;
    int primaryPitchSpeedMax; /* 0=auto from physicsUSFTopSpeed */
    int boost;
    float turboMix;
    int secondaryActive;
    float secondaryGain;
    float secondaryPitchMin;
    float secondaryPitchMax;
    int secondarySpeedMax;
    uint32_t smoothedSecondaryStep;
    uint32_t smoothedEngineStep; /* zipper-free modulation at 44.1 kHz */
};

static struct NativeEngineVoice s_nativeEngines[NATIVE_ENGINE_PLAYERS];
/* One shared source, with independent 1x cursors for local split-screen. */
static struct NativeEnginePcm s_nativeCNKCharge;
static uint64_t s_nativeCNKChargePhase[4];
static u8 s_nativeCNKChargePlaying[4];
static uint32_t s_nativeCNKChargeStep;

/* GameCube CNK Tornado Top clips, decoded on the gameplay thread and mixed
 * without allocations in the existing SDL audio callback. All clips are
 * optional: absence must not break ordinary CTR gameplay. */
#define NATIVE_CNK_TORNADO_SOUND_COUNT 3
static struct NativeEnginePcm s_nativeCNKTornadoClips[NATIVE_CNK_TORNADO_SOUND_COUNT];
static uint64_t s_nativeCNKTornadoPhases[NATIVE_CNK_TORNADO_SOUND_COUNT];
static uint32_t s_nativeCNKTornadoSteps[NATIVE_CNK_TORNADO_SOUND_COUNT];
static u8 s_nativeCNKTornadoPlaying[NATIVE_CNK_TORNADO_SOUND_COUNT];
static int s_nativeCNKTornadoPreloaded = 0;

#define NATIVE_ONLINE_SFX_SOUND_COUNT 11
static struct NativeEnginePcm s_nativeOnlineSfxClips[NATIVE_ONLINE_SFX_SOUND_COUNT];
static uint64_t s_nativeOnlineSfxPhases[NATIVE_ONLINE_SFX_SOUND_COUNT];
static uint32_t s_nativeOnlineSfxSteps[NATIVE_ONLINE_SFX_SOUND_COUNT];
static u8 s_nativeOnlineSfxPlaying[NATIVE_ONLINE_SFX_SOUND_COUNT];
static int s_nativeOnlineSfxPreloaded = 0;
static unsigned s_nativeEnginePitchLogCounter[NATIVE_ENGINE_PLAYERS];
/* Single source of truth for the global game SFX slider. */
static int s_nativeEngineGlobalFxVolume = 215;
static int s_nativeEnginePaused = 0;
static int s_nativeEngineKeepOnNextHowlStop = 0;
static int s_nativeHearOtherRacers = 1;

void NativeAudio_EngineSetHearOtherRacers(int enabled)
{
    NativeAudio_LockOutput();
    s_nativeHearOtherRacers = enabled != 0;
    NativeAudio_UnlockOutput();
    Platform_Log("[OPPONENT AUDIO] enabled=%d\n", enabled != 0);
}

void NativeAudio_EngineMarkOpponent(int driverID)
{
    if (driverID < 0 || driverID >= NATIVE_ENGINE_PLAYERS) return;
    NativeAudio_LockOutput();
    struct NativeEngineVoice *v=&s_nativeEngines[driverID];
    v->opponent = 1;
    v->opponentVisible = 0;
    v->opponentPan = 128;
    v->opponentEnvelope = 0.0f;
    NativeAudio_UnlockOutput();
}

void NativeAudio_EngineBeginOpponentFrame(void)
{
    NativeAudio_LockOutput();
    for (int i=0;i<NATIVE_ENGINE_PLAYERS;++i)
        if (s_nativeEngines[i].opponent)
            s_nativeEngines[i].opponentVisible = 0;
    NativeAudio_UnlockOutput();
}

void NativeAudio_EngineSetOpponentSpatial(int driverID, int pan)
{
    if (driverID < 0 || driverID >= NATIVE_ENGINE_PLAYERS) return;
    if (pan < 0) pan=0;
    if (pan > 255) pan=255;
    NativeAudio_LockOutput();
    struct NativeEngineVoice *v=&s_nativeEngines[driverID];
    if (v->opponent) {
        v->opponentVisible=1;
        v->opponentPan=pan;
    }
    NativeAudio_UnlockOutput();
}

/* This flag is written/consumed only by the synchronous gameplay thread;
 * the SDL audio callback never accesses it. Keep applies ONCE only, when
 * HOWL is about to destroy temporary retail channels during unpause. */
void NativeAudio_EnginePreserveNextHowlStop(void)
{
    s_nativeEngineKeepOnNextHowlStop = 1;
}

int NativeAudio_EngineConsumePreserveOnHowlStop(void)
{
    int keep = s_nativeEngineKeepOnNextHowlStop;
    s_nativeEngineKeepOnNextHowlStop = 0;
    return keep;
}

/* The native engine loop does not live in HOWL's retail channel list, so
 * pause/unpause must explicitly suspend its playback position. */
void NativeAudio_EngineSetPaused(int paused)
{
    NativeAudio_LockOutput();
    s_nativeEnginePaused = paused != 0;
    NativeAudio_UnlockOutput();
    Platform_Log("[CUSTOM ENGINE STATE] %s\n", paused ? "PAUSED" : "RESUMED");
}

void NativeAudio_EngineSetFxVolume(int fxVolume)
{
    if (fxVolume < 0) fxVolume = 0;
    if (fxVolume > 255) fxVolume = 255;
    NativeAudio_LockOutput();
    s_nativeEngineGlobalFxVolume = fxVolume;
    for (int i = 0; i < NATIVE_ENGINE_PLAYERS; ++i)
        s_nativeEngines[i].fxVolume = fxVolume;
    NativeAudio_UnlockOutput();
    Platform_Log("[CUSTOM ENGINE FX] set=%d/255 (0 mutes every custom engine layer)\n",
                 fxVolume);
}

static void NativeEngine_ClearVoice(struct NativeEngineVoice *voice)
{
    int i;
    for (i = 0; i < NATIVE_ENGINE_TRACKS; i++) {
        free(voice->clip[i].data);
        voice->clip[i].data = NULL;
    }
    memset(voice, 0, sizeof(*voice));
}

static int NativeEngine_LoadOgg(const char *path, struct NativeEnginePcm *dst)
{
    OggVorbis_File vf;
    vorbis_info *info;
    ogg_int64_t frames64;
    size_t allocBytes, readBytes = 0;
    s16 *pcm;
    int bitstream = 0;
    int success = 0;
    if (ov_fopen(path, &vf) < 0) return 0;
    info = ov_info(&vf, -1);
    frames64 = ov_pcm_total(&vf, -1);
    if (!info || (info->channels != 1 && info->channels != 2) ||
        info->rate < 8000 || info->rate > 96000 ||
        frames64 < 2 || frames64 > (ogg_int64_t)info->rate * NATIVE_ENGINE_MAX_SECONDS)
        goto done;
    allocBytes = (size_t)frames64 * info->channels * sizeof(s16);
    pcm = (s16 *)malloc(allocBytes);
    if (!pcm) goto done;
    while (readBytes < allocBytes) {
        long n = ov_read(&vf, (char *)pcm + readBytes,
                         (int)((allocBytes - readBytes) > 65536 ? 65536 : (allocBytes - readBytes)),
                         0, 2, 1, &bitstream);
        if (n == 0) break;
        if (n < 0) { free(pcm); goto done; }
        readBytes += (size_t)n;
    }
    if (readBytes < (size_t)(info->channels * 2 * 2)) {
        free(pcm);
        goto done;
    }
    dst->data = pcm;
    dst->frames = (int)(readBytes / ((size_t)info->channels * sizeof(s16)));
    dst->channels = info->channels;
    dst->rate = info->rate;
    success = 1;
done:
    ov_clear(&vf);
    return success;
}

static int NativeEngine_LoadWav(const char *path, struct NativeEnginePcm *dst)
{
    SDL_AudioSpec spec, desired;
    Uint8 *source = NULL, *converted = NULL;
    Uint32 sourceBytes = 0;
    int convertedBytes = 0;
    int frames;
    if (!SDL_LoadWAV(path, &spec, &source, &sourceBytes))
        return 0;
    if ((spec.channels != 1 && spec.channels != 2) ||
        spec.freq < 8000 || spec.freq > 96000 ||
        sourceBytes > 48u * 1024u * 1024u) {
        SDL_free(source);
        return 0;
    }
    desired = spec;
    desired.format = SDL_AUDIO_S16;
    if (!SDL_ConvertAudioSamples(&spec, source, (int)sourceBytes,
                                 &desired, &converted, &convertedBytes)) {
        SDL_free(source);
        return 0;
    }
    SDL_free(source);
    frames = convertedBytes / ((int)spec.channels * (int)sizeof(s16));
    if (frames < 2 || frames > spec.freq * NATIVE_ENGINE_MAX_SECONDS) {
        SDL_free(converted);
        return 0;
    }
    dst->data = (s16 *)malloc((size_t)convertedBytes);
    if (!dst->data) {
        SDL_free(converted);
        return 0;
    }
    memcpy(dst->data, converted, (size_t)convertedBytes);
    SDL_free(converted);
    dst->frames = frames;
    dst->rate = spec.freq;
    dst->channels = spec.channels;
    return 1;
}

/* Each CNK extraction has a different internal RMS, often far louder
 * than CTR's HOWL bank. Measure only while decoding, NEVER in the audio
 * callback. Quiet mods retain their original loudness (no amplification).
 * -16.5 dBFS-ish RMS target, with a modest per-clip max attenuation cap.
 * This applies to engine, idle, turbo, transmission and extra layers alike. */
static void NativeEngine_SetLoudnessTrim(struct NativeEnginePcm *clip)
{
    double sum = 0.0;
    int count = 0;
    int stride = clip->frames / 8192;
    if (stride < 1) stride = 1;
    clip->loudnessTrim=1.0f;
    if (!clip->data || clip->frames < 2) return;
    for (int frame=0; frame<clip->frames; frame+=stride) {
        for (int channel=0;channel<clip->channels;++channel) {
            float x = (float)clip->data[frame*clip->channels+channel] / 32768.0f;
            sum+=(double)x*(double)x;
            ++count;
        }
    }
    if (count > 0) {
        float rms = (float)sqrt(sum / (double)count);
        if (rms > 0.15f) {
            float trim=0.15f / rms;
            clip->loudnessTrim=trim < 0.25f ? 0.25f : trim;
        }
    }
}

static int NativeEngine_LoadClip(const char *path, struct NativeEnginePcm *dst)
{
    size_t len;
    int ok=0;
    if (!path || !*path) return 0;
    len = strlen(path);
    if (len >= 4 && !SDL_strcasecmp(path + len - 4, ".ogg"))
        ok=NativeEngine_LoadOgg(path, dst);
    else if (len >= 4 && !SDL_strcasecmp(path + len - 4, ".wav"))
        ok=NativeEngine_LoadWav(path, dst);
    if (ok) {
        NativeEngine_SetLoudnessTrim(dst);
        Platform_Log("[ENGINE LEVEL] clip=%s trim=%.3f (normalized, no boosting)\n",
                     path,dst->loudnessTrim);
    }
    return ok;
}

static uint32_t NativeEngine_GetStep(const struct NativeEnginePcm *clip, float pitch)
{
    double step;
    if (!clip->data || clip->frames < 2) return 0;
    step = ((double)clip->rate * 65536.0 * pitch) / (double)NATIVE_ENGINE_RATE;
    if (step < 1.0) step = 1.0;
    if (step > 4294967295.0) step = 4294967295.0;
    return (uint32_t)step;
}


/* Predecode once (not in the audio callback and not at the drift edge).
 * In contrast to the Oxide engine, this sound always stays pitch 1x. */
int NativeAudio_CNKChargePreload(const char *path)
{
    struct NativeEnginePcm next;
    if (s_nativeCNKCharge.data) return 1;
    if (!path || !*path) return 0;
    memset(&next,0,sizeof(next));
    if (!NativeEngine_LoadClip(path,&next)) {
        Platform_LogWarn("[CNK CHARGE SFX] failed to decode %s\n",path);
        return 0;
    }
    NativeAudio_LockOutput();
    if (!s_nativeCNKCharge.data) {
        s_nativeCNKCharge=next;
        s_nativeCNKChargeStep=NativeEngine_GetStep(&next,1.0f);
        memset(s_nativeCNKChargePhase,0,sizeof(s_nativeCNKChargePhase));
        memset(s_nativeCNKChargePlaying,0,sizeof(s_nativeCNKChargePlaying));
        next.data=NULL;
    }
    NativeAudio_UnlockOutput();
    free(next.data);
    Platform_Log("[CNK CHARGE SFX] ready path=%s rate=%d frames=%d pitch=1.00x\n",
                 path,s_nativeCNKCharge.rate,s_nativeCNKCharge.frames);
    return 1;
}

void NativeAudio_CNKChargePlay(int playerIndex)
{
    int started=0;
    if (playerIndex<0 || playerIndex>=4) return;
    NativeAudio_LockOutput();
    /* HUD calls only at a genuine new physics charge attempt. Rewind
     * that player's one-shot even if the previous long sample is still
     * playing, otherwise boost #2/#3 in split-screen can be inaudible. */
    if (s_nativeCNKCharge.data && s_nativeCNKCharge.frames>1) {
        s_nativeCNKChargePhase[playerIndex]=0;
        s_nativeCNKChargePlaying[playerIndex]=1;
        started=1;
    }
    NativeAudio_UnlockOutput();
    if (started)
        Platform_Log("[CNK CHARGE SFX] player=%d charge started; kartbrv3 pitch=1.00x\n",
                     playerIndex+1);
}

void NativeAudio_CNKChargeShutdown(void)
{
    struct NativeEnginePcm old;
    NativeAudio_LockOutput();
    old=s_nativeCNKCharge;
    memset(&s_nativeCNKCharge,0,sizeof(s_nativeCNKCharge));
    memset(s_nativeCNKChargePhase,0,sizeof(s_nativeCNKChargePhase));
    memset(s_nativeCNKChargePlaying,0,sizeof(s_nativeCNKChargePlaying));
    s_nativeCNKChargeStep=0;
    NativeAudio_UnlockOutput();
    free(old.data);
}


static void NativeEngine_ReadSample(const struct NativeEnginePcm *clip,
                                    uint64_t phase, int *left, int *right);

static void NativeAudio_CNKTornadoEnsureLoaded(void)
{
    static const char *paths[NATIVE_CNK_TORNADO_SOUND_COUNT] = {
        "assets/audio/cnk_tornado/topfire.wav",
        "assets/audio/cnk_tornado/toplift.wav",
        "assets/audio/cnk_tornado/topspin2.wav"
    };
    struct NativeEnginePcm loaded[NATIVE_CNK_TORNADO_SOUND_COUNT] = {{0}};
    if (s_nativeCNKTornadoPreloaded) return;
    for (int i = 0; i < NATIVE_CNK_TORNADO_SOUND_COUNT; ++i)
    {
        if (!NativeEngine_LoadClip(paths[i], &loaded[i]))
            Platform_LogWarn("[CNK TORNADO] Missing original SFX: %s\n", paths[i]);
    }
    NativeAudio_LockOutput();
    if (!s_nativeCNKTornadoPreloaded)
    {
        for (int i = 0; i < NATIVE_CNK_TORNADO_SOUND_COUNT; ++i)
        {
            s_nativeCNKTornadoClips[i] = loaded[i];
            s_nativeCNKTornadoSteps[i] = NativeEngine_GetStep(&loaded[i], 1.0f);
            loaded[i].data = NULL;
        }
        s_nativeCNKTornadoPreloaded = 1;
    }
    NativeAudio_UnlockOutput();
    for (int i = 0; i < NATIVE_CNK_TORNADO_SOUND_COUNT; ++i)
        free(loaded[i].data);
}

void NativeAudio_CNKTornadoPlay(int effect)
{
    if (effect < 0 || effect >= NATIVE_CNK_TORNADO_SOUND_COUNT) return;
    NativeAudio_CNKTornadoEnsureLoaded();
    NativeAudio_LockOutput();
    if (s_nativeCNKTornadoClips[effect].data &&
        s_nativeCNKTornadoClips[effect].frames >= 2)
    {
        s_nativeCNKTornadoPhases[effect] = 0;
        s_nativeCNKTornadoPlaying[effect] = 1;
    }
    NativeAudio_UnlockOutput();
}

void NativeAudio_CNKTornadoStop(void)
{
    NativeAudio_LockOutput();
    s_nativeCNKTornadoPlaying[2] = 0; /* stop traveling wind loop */
    NativeAudio_UnlockOutput();
}

int NativeAudio_OnlineSfxPreload(void)
{
    static const char *paths[NATIVE_ONLINE_SFX_SOUND_COUNT] = {
        "assets/audio/online_sfx/misscam.wav",
        "assets/audio/online_sfx/gateactv.wav",
        "assets/audio/online_sfx/warpaway.wav",
        "assets/audio/online_sfx/clktick2.wav",
        "assets/audio/online_sfx/gatwarp2.wav",
        "assets/audio/online_sfx/gatwarp3.wav",
        "assets/audio/online_sfx/clktick3.wav",
        "assets/audio/online_sfx/magtran2.wav",
        "assets/audio/online_sfx/gateunlk.wav",
        "assets/audio/online_sfx/wfdown.wav",
        "assets/audio/online_sfx/stoplit1.wav"
    };
    struct NativeEnginePcm loaded[NATIVE_ONLINE_SFX_SOUND_COUNT] = {{0}};
    int ready = 0;

    if (s_nativeOnlineSfxPreloaded)
        return 1;

    for (int i = 0; i < NATIVE_ONLINE_SFX_SOUND_COUNT; ++i)
    {
        if (NativeEngine_LoadClip(paths[i], &loaded[i]))
            ++ready;
        else
            Platform_LogWarn("[ONLINE SFX] missing/invalid clip: %s\n", paths[i]);
    }

    NativeAudio_LockOutput();
    if (!s_nativeOnlineSfxPreloaded)
    {
        for (int i = 0; i < NATIVE_ONLINE_SFX_SOUND_COUNT; ++i)
        {
            s_nativeOnlineSfxClips[i] = loaded[i];
            s_nativeOnlineSfxSteps[i] = NativeEngine_GetStep(&loaded[i], 1.0f);
            loaded[i].data = NULL;
        }
        s_nativeOnlineSfxPreloaded = 1;
    }
    NativeAudio_UnlockOutput();

    for (int i = 0; i < NATIVE_ONLINE_SFX_SOUND_COUNT; ++i)
        free(loaded[i].data);

    Platform_Log("[ONLINE SFX] preload ready=%d/%d\n",
                 ready, NATIVE_ONLINE_SFX_SOUND_COUNT);
    return ready == NATIVE_ONLINE_SFX_SOUND_COUNT;
}

void NativeAudio_OnlineSfxPlay(int effect)
{
    if (effect < 0 || effect >= NATIVE_ONLINE_SFX_SOUND_COUNT)
        return;

    if (!s_nativeOnlineSfxPreloaded)
        NativeAudio_OnlineSfxPreload();

    NativeAudio_LockOutput();
    if (s_nativeOnlineSfxClips[effect].data &&
        s_nativeOnlineSfxClips[effect].frames >= 2 &&
        s_nativeOnlineSfxSteps[effect] != 0)
    {
        s_nativeOnlineSfxPhases[effect] = 0;
        s_nativeOnlineSfxPlaying[effect] = 1;
    }
    NativeAudio_UnlockOutput();
}

void NativeAudio_OnlineSfxShutdown(void)
{
    struct NativeEnginePcm old[NATIVE_ONLINE_SFX_SOUND_COUNT];

    NativeAudio_LockOutput();
    memcpy(old, s_nativeOnlineSfxClips, sizeof(old));
    memset(s_nativeOnlineSfxClips, 0, sizeof(s_nativeOnlineSfxClips));
    memset(s_nativeOnlineSfxPhases, 0, sizeof(s_nativeOnlineSfxPhases));
    memset(s_nativeOnlineSfxSteps, 0, sizeof(s_nativeOnlineSfxSteps));
    memset(s_nativeOnlineSfxPlaying, 0, sizeof(s_nativeOnlineSfxPlaying));
    s_nativeOnlineSfxPreloaded = 0;
    NativeAudio_UnlockOutput();

    for (int i = 0; i < NATIVE_ONLINE_SFX_SOUND_COUNT; ++i)
        free(old[i].data);
}

static void NativeEngine_MixOnlineSfxNoLock(int *mixL, int *mixR,
                                            int masterL, int masterR)
{
    for (int i = 0; i < NATIVE_ONLINE_SFX_SOUND_COUNT; ++i)
    {
        struct NativeEnginePcm *clip = &s_nativeOnlineSfxClips[i];
        uint64_t limit;
        int l = 0, r = 0;
        float gain;

        if (!s_nativeOnlineSfxPlaying[i] || !clip->data ||
            clip->frames < 2 || !s_nativeOnlineSfxSteps[i])
            continue;

        limit = (uint64_t)clip->frames << 16;
        if (s_nativeOnlineSfxPhases[i] >= limit)
        {
            s_nativeOnlineSfxPlaying[i] = 0;
            continue;
        }

        NativeEngine_ReadSample(clip, s_nativeOnlineSfxPhases[i], &l, &r);
        /* Clock cues are short and have much lower RMS than the warp/join
         * clips. Keep their original peaks but mix them slightly louder so
         * they remain audible over Hangout music. Enum order: clktick2=3,
         * clktick3=6. */
        gain = ((float)s_nativeEngineGlobalFxVolume / 255.0f) *
               ((i == 3 || i == 6) ? 1.00f : 0.72f) *
               clip->loudnessTrim;
        *mixL += (int)((float)l * gain * ((float)masterL / 32768.0f));
        *mixR += (int)((float)r * gain * ((float)masterR / 32768.0f));

        s_nativeOnlineSfxPhases[i] += s_nativeOnlineSfxSteps[i];
        if (s_nativeOnlineSfxPhases[i] >= limit)
            s_nativeOnlineSfxPlaying[i] = 0;
    }
}

static void NativeEngine_MixCNKTornadoNoLock(int *mixL, int *mixR,
                                             int masterL, int masterR)
{
    for (int i = 0; i < NATIVE_CNK_TORNADO_SOUND_COUNT; ++i)
    {
        struct NativeEnginePcm *clip = &s_nativeCNKTornadoClips[i];
        uint64_t limit;
        int l = 0, r = 0;
        float gain;
        if (!s_nativeCNKTornadoPlaying[i] || !clip->data ||
            clip->frames < 2 || !s_nativeCNKTornadoSteps[i]) continue;
        limit = (uint64_t)clip->frames << 16;
        if (s_nativeCNKTornadoPhases[i] >= limit)
        {
            if (i == 2) s_nativeCNKTornadoPhases[i] %= limit;
            else { s_nativeCNKTornadoPlaying[i] = 0; continue; }
        }
        NativeEngine_ReadSample(clip, s_nativeCNKTornadoPhases[i], &l, &r);
        gain = (float)s_nativeEngineGlobalFxVolume / 255.0f *
               0.38f * clip->loudnessTrim;
        *mixL += (int)((float)l * gain *
                       ((float)masterL / 32768.0f));
        *mixR += (int)((float)r * gain *
                       ((float)masterR / 32768.0f));
        s_nativeCNKTornadoPhases[i] += s_nativeCNKTornadoSteps[i];
        if (s_nativeCNKTornadoPhases[i] >= limit && i != 2)
            s_nativeCNKTornadoPlaying[i] = 0;
    }
}

static float NativeEngine_Clamp01(float amount)
{
    if (amount < 0.0f) return 0.0f;
    if (amount > 1.0f) return 1.0f;
    return amount;
}

/* Original CTR retail HOWL EngineFX pitch:
 *   channel.pitch = engineFX->pitch * data.distortConst_Engine[d] >> 16
 *
 * No new drift curves: the sound responds exactly to EngineSound_Player's
 * acceleration, revving, powerslide charge, wobble, fire and turbo changes.
 * Normalize the original *multipliers* between retail 0x3c idle and 0xc8
 * player maximum, then express pitch intervals in LOG FREQUENCY (semitones)
 * instead of the old LINEAR Hz interpolation. Custom minimum and maximum
 * remain precise and are NEVER overwritten by this mapping.
 *
 * Reference table entries are literal exact values from zGlobal_DATA.c:
 *   data.distortConst_Engine[0x3c] = 31378
 *   data.distortConst_Engine[0xc8] = 142935
 * Gameplay passes the live table value itself. No duplicate table in the
 * PCM code and no extra meter-based fake modulation.
 */
static float NativeEngine_RpmRatio(const struct NativeEngineVoice *v)
{
    const float ctrIdle = 31378.0f;
    const float ctrPlayerMax = 142935.0f;
    float retail = (float)v->ctrRetailPitchTableValue;
    if (retail <= 0.0f) retail = ctrIdle; /* first frame before tick */
    if (retail < ctrIdle) retail = ctrIdle;
    if (retail > ctrPlayerMax) retail = ctrPlayerMax;
    return NativeEngine_Clamp01(
        logf(retail / ctrIdle) / logf(ctrPlayerMax / ctrIdle));
}

static float NativeEngine_SpeedRatio(const struct NativeEngineVoice *v,
                                    int maxSpeed)
{
    if (maxSpeed == 0) maxSpeed = v->physicsUSFTopSpeed;
    if (maxSpeed < 1000) maxSpeed = 32767;
    return NativeEngine_Clamp01((float)v->speedAbs / (float)maxSpeed);
}

static float NativeEngine_PrimaryPitch(const struct NativeEngineVoice *v)
{
    if (v->primaryCurveActive) {
        if (v->primaryMode == 1) {
            /* Mirror CTR's exponential pitch behavior while retaining
             * kart-authored endpoints: every distortion change shifts an
             * equivalent number of semitones, not a fixed Hz interval. */
            return v->primaryPitchMin *
                powf(v->primaryPitchMax / v->primaryPitchMin,
                     NativeEngine_RpmRatio(v));
        }
        /* Preserve existing road-speed mode for gearbox-style engines. */
        float speedRatio = NativeEngine_SpeedRatio(v,v->primaryPitchSpeedMax);
        return v->primaryPitchMin +
               (v->primaryPitchMax - v->primaryPitchMin) * speedRatio;
    }
    /* All unconfigured custom engines retain their existing algorithm. */
    return 0.86f + (float)v->speedAbs * 0.000055f +
           ((float)v->distortion / 255.0f) * 0.12f;
}

static void NativeEngine_UpdateSteps(struct NativeEngineVoice *v)
{
    /* Kart-defined engines may override this with their own dynamic pitch
     * curve. Other mod engines preserve the existing HOWL+speed algorithm. */
    float basePitch = NativeEngine_PrimaryPitch(v);
    v->step[0] = NativeEngine_GetStep(&v->clip[0], basePitch);
    v->step[1] = NativeEngine_GetStep(&v->clip[1], 1.0f);
    v->step[2] = NativeEngine_GetStep(&v->clip[2], 1.0f);
    /* A separate layer only follows road speed, not HOWL rev/distortion. */
    if (v->secondaryActive) {
        float speed = NativeEngine_SpeedRatio(v,v->secondarySpeedMax);
        v->step[3] = NativeEngine_GetStep(&v->clip[3],
                  v->secondaryPitchMin +
                  (v->secondaryPitchMax - v->secondaryPitchMin) * speed);
    }
    for (int layer=0; layer<NATIVE_KART_AUDIO_LAYER_COUNT; ++layer) {
        if (!v->extraActive[layer]) continue;
        float ratio = v->extraMode[layer] == NATIVE_KART_AUDIO_SPEED ?
            NativeEngine_SpeedRatio(v,v->extraSpeedMax[layer]) :
            NativeEngine_RpmRatio(v);
        float pitch;
        if (v->extraMode[layer] == NATIVE_KART_AUDIO_SPEED) {
            pitch = v->extraPitchMin[layer] +
                (v->extraPitchMax[layer] - v->extraPitchMin[layer]) * ratio;
        } else {
            /* Optional engine/reactive layers use the same HOWL semitone
             * response as the main engine, independent of their fade mode. */
            pitch = v->extraPitchMin[layer] *
                powf(v->extraPitchMax[layer] / v->extraPitchMin[layer],ratio);
        }
        v->step[4+layer] = NativeEngine_GetStep(&v->clip[4+layer],pitch);
    }
}

int NativeAudio_EngineSetPitchCurve(int playerID, float idlePitch,
                                    float usfPitch, int speedMax, int rpmMode)
{
    if (playerID < 0 || playerID >= NATIVE_ENGINE_PLAYERS ||
        !isfinite(idlePitch) || !isfinite(usfPitch) ||
        idlePitch < 0.1f || usfPitch > 8.0f || usfPitch < idlePitch ||
        (speedMax != 0 && (speedMax < 1000 || speedMax > 32767)) ||
        (rpmMode != 0 && rpmMode != 1))
        return 0;
    NativeAudio_LockOutput();
    struct NativeEngineVoice *v = &s_nativeEngines[playerID];
    if (!v->active) {
        NativeAudio_UnlockOutput();
        return 0;
    }
    v->primaryCurveActive = 1;
    v->primaryPitchMin = idlePitch;
    v->primaryPitchMax = usfPitch;
    v->primaryPitchSpeedMax = speedMax;
    v->primaryMode = rpmMode;
    NativeEngine_UpdateSteps(v);
    v->smoothedEngineStep = v->step[0];
    NativeAudio_UnlockOutput();
    Platform_Log("[ENGINE CURVE] driver=%d idle=%.3f usf=%.3f maxSpeed=%d mode=%s\n",
                 playerID, idlePitch, usfPitch, speedMax,
                 rpmMode == 1 ? "RPM/HOWL" : "ROAD_SPEED");
    return 1;
}

int NativeAudio_EngineBindLayer(int playerID,int layerIndex,const char *path,
                                int mode,float volumePercent,float pitchMin,
                                float pitchMax,int speedMax)
{
    struct NativeEnginePcm pcm,old;
    struct NativeEngineVoice *v;
    if (playerID<0 || playerID>=NATIVE_ENGINE_PLAYERS ||
        layerIndex<0 || layerIndex>=NATIVE_KART_AUDIO_LAYER_COUNT ||
        mode<NATIVE_KART_AUDIO_RPM || mode>NATIVE_KART_AUDIO_TURBO ||
        !path || !*path || !isfinite(volumePercent) ||
        !isfinite(pitchMin) || !isfinite(pitchMax) ||
        volumePercent<0.0f || volumePercent>100.0f ||
        pitchMin<0.1f || pitchMax>8.0f || pitchMax<pitchMin ||
        (speedMax!=0 && (speedMax<1000 || speedMax>32767)))
        return 0;
    memset(&pcm,0,sizeof(pcm));
    if (!NativeEngine_LoadClip(path,&pcm)) {
        Platform_Log("[KART AUDIO LAYER] player=%d layer=%d invalid clip '%s'\n",
                     playerID,layerIndex+1,path);
        return 0;
    }
    NativeAudio_LockOutput();
    v=&s_nativeEngines[playerID];
    old=v->clip[4+layerIndex];
    v->clip[4+layerIndex]=pcm;
    v->extraActive[layerIndex]=1;
    v->extraMode[layerIndex]=mode;
    v->extraGain[layerIndex]=volumePercent/100.0f;
    v->extraPitchMin[layerIndex]=pitchMin;
    v->extraPitchMax[layerIndex]=pitchMax;
    v->extraSpeedMax[layerIndex]=speedMax;
    v->fxVolume=s_nativeEngineGlobalFxVolume;
    NativeEngine_UpdateSteps(v);
    v->smoothedExtraStep[layerIndex]=v->step[4+layerIndex];
    NativeAudio_UnlockOutput();
    free(old.data);
    Platform_Log("[KART AUDIO LAYER] player=%d layer=%d mode=%d gain=%.1f%% pitch=%.2f..%.2f file=%s\n",
                 playerID,layerIndex+1,mode,volumePercent,pitchMin,pitchMax,path);
    return 1;
}

int NativeAudio_EngineBindSecondary(int playerID, const char *path,
                                    float volumePercent, float pitchMin,
                                    float pitchMax, int speedMax)
{
    struct NativeEnginePcm pcm;
    if (playerID < 0 || playerID >= NATIVE_ENGINE_PLAYERS ||
        !path || !*path ||
        volumePercent < 0.0f || volumePercent > 100.0f ||
        pitchMin < 0.25f || pitchMax > 6.0f ||
        pitchMax < pitchMin ||
        speedMax < 1000 || speedMax > 32767)
        return 0;
    memset(&pcm, 0, sizeof(pcm));
    if (!NativeEngine_LoadClip(path, &pcm)) {
        Platform_Log("[SECONDARY ENGINE] player=%d invalid file=%s; disabled\n",
                     playerID, path);
        return 0;
    }
    NativeAudio_LockOutput();
    struct NativeEngineVoice *v = &s_nativeEngines[playerID];
    struct NativeEnginePcm old = v->clip[3];
    v->clip[3] = pcm;
    v->secondaryActive = 1;
    v->secondaryGain = volumePercent * 0.01f;
    v->secondaryPitchMin = pitchMin;
    v->secondaryPitchMax = pitchMax;
    v->secondarySpeedMax = speedMax;
    v->fxVolume = s_nativeEngineGlobalFxVolume;
    NativeEngine_UpdateSteps(v);
    v->smoothedSecondaryStep = v->step[3];
    NativeAudio_UnlockOutput();
    free(old.data);
    Platform_Log("[SECONDARY ENGINE] player=%d file=%s volume=%.1f%% pitch=%.2f..%.2f speedMax=%d\n",
                 playerID, path, volumePercent, pitchMin, pitchMax, speedMax);
    return 1;
}

int NativeAudio_EngineBind(int playerID, const char *loopPath,
                           const char *idlePath, const char *turboPath)
{
    struct NativeEngineVoice next, old;
    if (playerID < 0 || playerID >= NATIVE_ENGINE_PLAYERS)
        return 0;
    memset(&next, 0, sizeof(next));
    if (!NativeEngine_LoadClip(loopPath, &next.clip[0])) {
        Platform_Log("[ENGINE AUDIO] player=%d invalid loop '%s'; using retail\n",
                     playerID, loopPath ? loopPath : "");
        NativeEngine_ClearVoice(&next);
        return 0;
    }
    if (idlePath && *idlePath && !NativeEngine_LoadClip(idlePath, &next.clip[1]))
        Platform_Log("[ENGINE AUDIO] optional idle invalid: %s\n", idlePath);
    if (turboPath && *turboPath && !NativeEngine_LoadClip(turboPath, &next.clip[2]))
        Platform_Log("[ENGINE AUDIO] optional turbo invalid: %s\n", turboPath);
    next.active = 1;
    next.volume = 180;
    next.fxVolume = s_nativeEngineGlobalFxVolume;
    next.distortion = 80;
    next.speedAbs = 0;
    next.physicsUSFTopSpeed = 32767;
    next.howlRpmMax = 32767;
    NativeEngine_UpdateSteps(&next);
    next.smoothedEngineStep = next.step[0];
    NativeAudio_LockOutput();
    old = s_nativeEngines[playerID];
    s_nativeEngines[playerID] = next;
    NativeAudio_UnlockOutput();
    NativeEngine_ClearVoice(&old);
    Platform_Log("[ENGINE AUDIO] player=%d bound loop=%s idle=%d turbo=%d\n",
                 playerID, loopPath, next.clip[1].data != NULL,
                 next.clip[2].data != NULL);
    return 1;
}

void NativeAudio_EngineUnbindAll(void)
{
    struct NativeEngineVoice old[NATIVE_ENGINE_PLAYERS];
    NativeAudio_LockOutput();
    memcpy(old, s_nativeEngines, sizeof(old));
    memset(s_nativeEngines, 0, sizeof(s_nativeEngines));
    s_nativeEnginePaused = 0;
    s_nativeEngineKeepOnNextHowlStop = 0;
    memset(s_nativeEnginePitchLogCounter, 0, sizeof(s_nativeEnginePitchLogCounter));
    memset(s_nativeCNKChargePlaying,0,sizeof(s_nativeCNKChargePlaying));
    memset(s_nativeCNKChargePhase,0,sizeof(s_nativeCNKChargePhase));
    memset(s_nativeCNKTornadoPlaying,0,sizeof(s_nativeCNKTornadoPlaying));
    NativeAudio_UnlockOutput();
    for (int i = 0; i < NATIVE_ENGINE_PLAYERS; i++)
        NativeEngine_ClearVoice(&old[i]);
}

void NativeAudio_EngineStop(int playerID)
{
    if (playerID < 0 || playerID >= NATIVE_ENGINE_PLAYERS) return;
    NativeAudio_LockOutput();
    s_nativeEngines[playerID].active = 0;
    s_nativeEngines[playerID].secondaryActive = 0;
    for (int i=0;i<NATIVE_KART_AUDIO_LAYER_COUNT;++i)
        s_nativeEngines[playerID].extraActive[i]=0;
    NativeAudio_UnlockOutput();
}

int NativeAudio_EngineUpdate(int playerID, int volume, int distortion,
                             int boost, int fxVolume, int speedApprox,
                             int physicsUSFTopSpeed, int howlEngineRpm,
                             int howlEngineMaxRpm, int drifting, int revving,
                             int driftMeterRoomLeft, int driftMeterMaxRoom,
                             int ctrRetailPitchTableValue)
{
    struct NativeEngineVoice *v;
    int speedAbs = speedApprox < 0 ? -speedApprox : speedApprox;
    int shouldLog = 0;
    int logDrifting = 0;
    float logDriftCharge = 0.0f;
    float currentPitch = 0.0f;
    if (playerID<0 || playerID>=NATIVE_ENGINE_PLAYERS) return 0;
    int extrasLive=0;
    for (int i=0;i<NATIVE_KART_AUDIO_LAYER_COUNT;++i)
        if (s_nativeEngines[playerID].extraActive[i])
            extrasLive=1;
    if (!s_nativeEngines[playerID].active &&
        !s_nativeEngines[playerID].secondaryActive &&
        !extrasLive) return 0;
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    if (distortion < 0) distortion = 0;
    if (distortion > 255) distortion = 255;
    if (fxVolume < 0) fxVolume = 0;
    if (fxVolume > 255) fxVolume = 255;
    if (speedAbs > 32768) speedAbs = 32768; /* Driver.speedApprox is s16 */
    NativeAudio_LockOutput();
    v = &s_nativeEngines[playerID];
    if (v->active || v->secondaryActive || extrasLive) {
        v->volume = volume;
        v->fxVolume = fxVolume;
        v->distortion = distortion;
        /* This is the SAME zGlobal_DATA lookup used by the retail
         * EngineAudio_Recalculate, not a synthetic drift/rpm approximation. */
        v->ctrRetailPitchTableValue = ctrRetailPitchTableValue;
        if (speedAbs > v->speedAbs+25 || revving)
            v->accelTicks=4;
        else if (v->accelTicks>0)
            --v->accelTicks;
        v->speedAbs = speedAbs;
        v->howlRpm = howlEngineRpm > 0 ? howlEngineRpm : 0;
        v->howlRpmMax = howlEngineMaxRpm;
        v->drifting = drifting != 0;
        v->revving = revving != 0;
        v->driftCharge = 0.0f;
        if (v->drifting && driftMeterMaxRoom > 0) {
            /* Mirror the HUD calculation:
             * progress = (maxRoom - turbo_MeterRoomLeft) / maxRoom.
             * Restrict invalid/custom meter values to the safe 0..1
             * domain, without changing any physical drift values. */
            v->driftCharge = NativeEngine_Clamp01(
                1.0f - (float)driftMeterRoomLeft /
                       (float)driftMeterMaxRoom);
        }
        v->physicsUSFTopSpeed =
            physicsUSFTopSpeed >= 1000 && physicsUSFTopSpeed <= 32767 ?
            physicsUSFTopSpeed : 32767;
        v->boost = boost != 0;
        NativeEngine_UpdateSteps(v);
        /* One diagnostic per ~3 seconds for P1, outside the audio lock. */
        shouldLog = (playerID == 0 &&
                    ++s_nativeEnginePitchLogCounter[playerID] % 90 == 0);
        if (shouldLog) {
            currentPitch = NativeEngine_PrimaryPitch(v);
            logDrifting = v->drifting;
            logDriftCharge = v->driftCharge;
        }
    }
    /* Read this while holding the lock already acquired above. A second
     * SDL_LockAudioStream here deadlocked the audio callback and silenced
     * the entire game immediately after race audio initialization. */
    int hasPrimary = v->active;
    NativeAudio_UnlockOutput();
    if (shouldLog)
        Platform_Log("[ENGINE PITCH] player=%d speed=%d ctrDistortion=%d ctrTable=%d drift=%d charge=%.2f pitch=%.3f\n",
                     playerID, speedAbs, distortion,
                     ctrRetailPitchTableValue, logDrifting, logDriftCharge,
                     currentPitch);
    /* Never bypass the retail engine's HOWL pitch when ONLY the secondary
     * track exists: EngineAudio_Recalculate must still run as normal. */
    return hasPrimary;
}

static void NativeEngine_ReadSample(const struct NativeEnginePcm *clip,
                                    uint64_t phase, int *left, int *right)
{
    uint64_t idx = phase >> 16;
    uint64_t next;
    unsigned frac = (unsigned)(phase & 0xffff);
    int a, b;
    const s16 *pcm = clip->data;
    if (idx >= (uint64_t)clip->frames) idx %= (uint64_t)clip->frames;
    next = idx + 1;
    if (next >= (uint64_t)clip->frames) next = 0;
    a = pcm[idx * clip->channels];
    b = pcm[next * clip->channels];
    *left = a + (int)(((int64_t)(b - a) * frac) >> 16);
    if (clip->channels == 2) {
        a = pcm[idx * 2 + 1];
        b = pcm[next * 2 + 1];
        *right = a + (int)(((int64_t)(b - a) * frac) >> 16);
    } else {
        *right = *left;
    }
}

static void NativeEngine_MixClip(struct NativeEngineVoice *v, int index,
                                 float gainL, float gainR, int *mixL, int *mixR)
{
    const struct NativeEnginePcm *clip = &v->clip[index];
    uint64_t limit;
    int left, right;
    if (!clip->data || gainL <= 0.00001f && gainR <= 0.00001f)
        return;
    NativeEngine_ReadSample(clip, v->phase[index], &left, &right);
    *mixL += (int)((float)left * gainL * clip->loudnessTrim);
    *mixR += (int)((float)right * gainR * clip->loudnessTrim);
    v->phase[index] += index == 0 ? v->smoothedEngineStep :
                       index == 3 ? v->smoothedSecondaryStep :
                       index >= 4 ? v->smoothedExtraStep[index-4] :
                       v->step[index];
    limit = (uint64_t)clip->frames << 16;
    if (v->phase[index] >= limit) v->phase[index] %= limit;
}

/* True one-shot, not a loop/engine, so sound stops when its source ends.
 * The original 10% preview level was inaudible under kart motors in racing:
 * use a clear 40% mix, still respecting game SFX/master sliders. */
static void NativeEngine_MixCNKChargeNoLock(int *mixL,int *mixR,
                                             int masterL,int masterR)
{
    const struct NativeEnginePcm *clip=&s_nativeCNKCharge;
    if (!clip->data || clip->frames<2 || !s_nativeCNKChargeStep) return;
    const uint64_t limit=(uint64_t)clip->frames<<16;
    const float gain=((float)s_nativeEngineGlobalFxVolume/255.0f) *
                     0.40f * clip->loudnessTrim;
    for (int i=0;i<4;++i) {
        int l=0,r=0;
        if (!s_nativeCNKChargePlaying[i]) continue;
        if (s_nativeCNKChargePhase[i]>=limit) {
            s_nativeCNKChargePlaying[i]=0;continue;
        }
        NativeEngine_ReadSample(clip,s_nativeCNKChargePhase[i],&l,&r);
        *mixL+=(int)((float)l*gain*((float)masterL/32768.0f));
        *mixR+=(int)((float)r*gain*((float)masterR/32768.0f));
        s_nativeCNKChargePhase[i]+=s_nativeCNKChargeStep;
        if (s_nativeCNKChargePhase[i]>=limit)
            s_nativeCNKChargePlaying[i]=0;
    }
}

void NativeAudio_EngineMixFrameNoLock(int *mixLeft, int *mixRight,
                                       int masterLeft, int masterRight)
{
    /* Keep engine sample playback positions frozen during game pause, exactly
     * like HOWL's paused SPU channels. The audio callback still mixes music
     * and UI through their separate paths. */
    if (s_nativeEnginePaused) return;
    for (int i = 0; i < NATIVE_ENGINE_PLAYERS; i++) {
        struct NativeEngineVoice *v = &s_nativeEngines[i];
        float gain, leftGain, rightGain, driveBlend;
        int extraExists=0;
        for (int layer=0;layer<NATIVE_KART_AUDIO_LAYER_COUNT;++layer)
            if (v->extraActive[layer] && v->clip[layer+4].data)
                extraExists=1;
        if ((!v->active || !v->clip[0].data) &&
            (!v->secondaryActive || !v->clip[3].data) &&
            !extraExists) continue;
        /* Smooth game-tick pitch changes every audio frame. This avoids an
         * audible step when speed changes abruptly, without delaying audio. */
        if (v->smoothedEngineStep != v->step[0]) {
            int64_t delta = (int64_t)v->step[0] -
                            (int64_t)v->smoothedEngineStep;
            int64_t nudge = delta / 256;
            if (!nudge) nudge = delta > 0 ? 1 : -1;
            v->smoothedEngineStep =
                (uint32_t)((int64_t)v->smoothedEngineStep + nudge);
        }
        if (v->secondaryActive && v->clip[3].data &&
            v->smoothedSecondaryStep != v->step[3]) {
            int64_t delta = (int64_t)v->step[3] -
                            (int64_t)v->smoothedSecondaryStep;
            int64_t change = delta / 256;
            if (!change) change = delta > 0 ? 1 : -1;
            v->smoothedSecondaryStep =
                (uint32_t)((int64_t)v->smoothedSecondaryStep + change);
        }
        for (int layer=0;layer<NATIVE_KART_AUDIO_LAYER_COUNT;++layer) {
            if (!v->extraActive[layer] || !v->clip[layer+4].data) continue;
            if (v->smoothedExtraStep[layer]!=v->step[layer+4]) {
                int64_t delta=(int64_t)v->step[layer+4] -
                              (int64_t)v->smoothedExtraStep[layer];
                int64_t change=delta/256;
                if (!change) change=delta>0 ? 1 : -1;
                v->smoothedExtraStep[layer]=(uint32_t)(
                    (int64_t)v->smoothedExtraStep[layer]+change);
            }
        }
        /* Global SFX slider controls EVERY custom PCM layer. 0 mutes all
         * kart effects immediately even while the game is paused. */
        gain = ((float)v->volume / 255.0f) *
               ((float)v->fxVolume / 255.0f) * 0.60f;
        if (v->opponent) {
            /* Two nearest bot engines only. Smoothed 70-ms fade, including
             * live YES/NO changes and switching who is nearest. Keep player
             * engines unaffected and mix with the vanilla CTR pan. */
            float wanted=(s_nativeHearOtherRacers && v->opponentVisible) ?
                         1.0f : 0.0f;
            v->opponentEnvelope += (wanted-v->opponentEnvelope)*0.00070f;
            if (v->opponentEnvelope < 0.0001f && wanted < 0.5f) continue;
            gain *= v->opponentEnvelope * 0.72f;
        }
        leftGain = gain * ((float)masterLeft / 32768.0f);
        rightGain = gain * ((float)masterRight / 32768.0f);
        if (v->opponent) {
            float pan=(float)v->opponentPan / 255.0f;
            /* keep 15% cross-ear bleed rather than hard L/R shutoff */
            if (pan > 0.5f)
                leftGain *= 1.0f - (pan-0.5f)*1.70f;
            if (pan < 0.5f)
                rightGain *= 1.0f - (0.5f-pan)*1.70f;
        }
        if (v->secondaryActive && v->clip[3].data) {
            float speedPart = (float)v->speedAbs /
                              (float)v->secondarySpeedMax;
            float ramp = (speedPart - 0.015f) * 5.0f;
            if (ramp < 0.0f) ramp = 0.0f;
            if (ramp > 1.0f) ramp = 1.0f;
            NativeEngine_MixClip(v, 3, leftGain * v->secondaryGain * ramp,
                     rightGain * v->secondaryGain * ramp, mixLeft, mixRight);
        }
        /* Optional per-kart layers mix alongside the retail/custom engine.
         * Modes RPM/ACCEL/DRIFT/TURBO pitch follow HOWL revs, never speed
         * alone. Mode SPEED is for a speed-linked gearbox transmission. */
        for (int layer=0;layer<NATIVE_KART_AUDIO_LAYER_COUNT;++layer) {
            if (!v->extraActive[layer] || !v->clip[layer+4].data) continue;
            float target=1.0f;
            int mode=v->extraMode[layer];
            if (mode==NATIVE_KART_AUDIO_SPEED) {
                float speed=NativeEngine_SpeedRatio(
                    v,v->extraSpeedMax[layer]);
                target=NativeEngine_Clamp01((speed-0.015f)*5.0f);
            } else if (mode==NATIVE_KART_AUDIO_ACCEL) {
                target=v->accelTicks>0 || v->revving ? 1.0f : 0.0f;
            } else if (mode==NATIVE_KART_AUDIO_DRIFT) {
                target=v->drifting ? 1.0f : 0.0f;
            } else if (mode==NATIVE_KART_AUDIO_TURBO) {
                target=v->boost ? 1.0f : 0.0f;
            }
            v->extraEnvelope[layer]+=
                (target-v->extraEnvelope[layer])*0.0012f;
            float layerGain=v->extraEnvelope[layer] *
                v->extraGain[layer];
            NativeEngine_MixClip(v,4+layer,
                leftGain*layerGain,rightGain*layerGain,mixLeft,mixRight);
        }
        /* Secondary and added loops NEVER replace the main engine. */
        if (!v->active || !v->clip[0].data) continue;
        driveBlend = ((float)v->distortion - 60.0f) / 110.0f;
        if (driveBlend < 0.0f) driveBlend = 0.0f;
        if (driveBlend > 1.0f) driveBlend = 1.0f;
        if (v->clip[1].data) {
            NativeEngine_MixClip(v, 1, leftGain * (1.0f - driveBlend),
                                 rightGain * (1.0f - driveBlend), mixLeft, mixRight);
            leftGain *= driveBlend;
            rightGain *= driveBlend;
        }
        NativeEngine_MixClip(v, 0, leftGain, rightGain, mixLeft, mixRight);
        if (v->boost && v->turboMix < 1.0f) v->turboMix += 0.0010f;
        if (!v->boost && v->turboMix > 0.0f) v->turboMix -= 0.0010f;
        if (v->turboMix < 0.0f) v->turboMix = 0.0f;
        if (v->turboMix > 1.0f) v->turboMix = 1.0f;
        if (v->clip[2].data && v->turboMix > 0.0f) {
            float turboGain = gain * 0.52f * v->turboMix;
            NativeEngine_MixClip(v, 2,
                turboGain * ((float)masterLeft / 32768.0f),
                turboGain * ((float)masterRight / 32768.0f),
                mixLeft, mixRight);
        }
    }
    NativeEngine_MixCNKChargeNoLock(mixLeft,mixRight,masterLeft,masterRight);
    NativeEngine_MixCNKTornadoNoLock(mixLeft,mixRight,masterLeft,masterRight);
    NativeEngine_MixOnlineSfxNoLock(mixLeft,mixRight,masterLeft,masterRight);
}

