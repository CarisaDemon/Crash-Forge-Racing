/* Crash Forge: Racing - native CNK source adapter, FIRST QA phase.
 *
 * Enabled ONLY with CTR_CNK_NATIVE_BRIDGE_QA + existing
 * CTR_CNK_PHYSICS_PREVIEW_QA build flags. The selected CNK test
 * mode keeps CTR BALANCED behavior unchanged while this phase
 * records raw fixed-point driver state BEFORE and AFTER contact
 * callbacks. Original CNK PS2 units and Havok internals have NOT
 * been mapped to CTR; applying CNK numerical impulses to CTR's
 * velocity would be guesswork and is expressly forbidden here.
 *
 * This file has NO assignments to any struct Driver fields.
 * Logging capped at 600 records per local player/race, ~6 Hz.
 * Optional native DLL is found next to the QA executable; missing
 * or unverified DLL never prevents the retail CTR fallback.
 */
#include <common.h>
#include <RacerLoadout.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>

enum {
    CNK_NATIVE_QA_MAX_LOCAL_PLAYERS = 4,
    CNK_NATIVE_QA_SAMPLE_INTERVAL_MS = 160,
    CNK_NATIVE_QA_MAX_SAMPLES = 600
};

struct CNKNativeQASlot {
    int hasForceSample;
    int timeSinceSampleMS;
    int sampleCount;
    int previousRaceTime;
    Vec3 velocityAfterForces;
    Vec3 positionBeforeContacts;
    s16 speedAfterForces;
    /* Five exact post-dispatch snapshots: stage 5 force, 6 moved,
     * 7 vehicle collisions, 8 static contacts, 9 jump/friction.
     * Stage 10 is read in AfterContacts from the live Driver. */
    Vec3 phaseVelocity[5];
    Vec3 phasePosition[5];
    unsigned phaseMask;
};

static struct CNKNativeQASlot s_cnkNativeQASlots[CNK_NATIVE_QA_MAX_LOCAL_PLAYERS];
static int s_cnkNativeQALibraryChecked;

#if defined(_WIN32)
typedef int (__cdecl *CNKNativeQAVectorSeed)(const float *, const float *, float *);
static SDL_SharedObject *s_cnkNativeQALibrary;

static void CNKNativeBridgeQA_CheckLibraryOnce(void)
{
    static const char libraryName[] = "cnk_native_numeric_qa_x86.dll";
    static const char *const required[] = {
        "cnk_ps2_force_seed_add_190854",
        "cnk_ps2_special_state14_190888",
        "cnk_ps2_frame_force_chain_190d80",
        "cnk_ps2_force_dispatch_190d94",
        "cnk_ps2_contact_bridge_1979d0",
        "cnk_ps2_contact_find_23d110",
        "cnk_ps2_world_correction_1963d0"
    };
    const char *base;
    char fileName[1024];
    SDL_SharedObject *lib;
    SDL_FunctionPointer fn;
    CNKNativeQAVectorSeed seed = NULL;
    const float a[4] = {1.5f, -2.0f, 3.0f, 99.0f};
    const float b[4] = {2.5f, 4.0f, -5.0f, -17.0f};
    float output[4] = {0};
    unsigned i;
    int characters;

    if (s_cnkNativeQALibraryChecked)
        return;
    s_cnkNativeQALibraryChecked = 1;

    /* SDL3 handles library loading; windows.h would collide with
     * the game's custom RECT16 and rendering structures. */
    base = SDL_GetBasePath();
    if (!base || !base[0]) {
        Platform_LogWarn("[CNK NATIVE QA] EXE directory unavailable; CTR unchanged\n");
        return;
    }
    characters = snprintf(fileName, sizeof(fileName), "%s%s", base, libraryName);
    if (characters < 0 || characters >= (int)sizeof(fileName)) {
        Platform_LogWarn("[CNK NATIVE QA] DLL path too long; CTR unchanged\n");
        return;
    }
    lib = SDL_LoadObject(fileName);
    if (!lib) {
        Platform_LogWarn("[CNK NATIVE QA] Optional DLL absent next to QA EXE; raw trace only, CTR unchanged\n");
        return;
    }
    for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (!SDL_LoadFunction(lib, required[i])) {
            Platform_LogWarn("[CNK NATIVE QA] DLL missing export %s; CTR unchanged\n", required[i]);
            SDL_UnloadObject(lib);
            return;
        }
    }
    fn = SDL_LoadFunction(lib, required[0]);
    if (sizeof(seed) != sizeof(fn)) {
        Platform_LogWarn("[CNK NATIVE QA] Unexpected Win32 ABI width; CTR unchanged\n");
        SDL_UnloadObject(lib);
        return;
    }
    memcpy(&seed, &fn, sizeof(seed));
    if (!seed || seed(a, b, output) != 1 ||
        output[0] != 4.0f || output[1] != 2.0f ||
        output[2] != -2.0f || output[3] != 99.0f) {
        Platform_LogWarn("[CNK NATIVE QA] DLL numeric ABI mismatch; CTR unchanged\n");
        SDL_UnloadObject(lib);
        return;
    }
    s_cnkNativeQALibrary = lib;
    Platform_Log("[CNK NATIVE QA] SDL3 native DLL: 7 exports and numeric ABI OK; SHADOW ONLY\n");
}
#else
static void CNKNativeBridgeQA_CheckLibraryOnce(void)
{
    if (s_cnkNativeQALibraryChecked)
        return;
    s_cnkNativeQALibraryChecked = 1;
    Platform_LogWarn("[CNK NATIVE QA] Win32 numeric DLL unavailable; CTR unchanged\n");
}
#endif

static int CNKNativeBridgeQA_IsEligible(const struct Driver *driver)
{
    const struct GameTracker *gGT;
    if (!driver || !sdata || !sdata->gGT)
        return 0;
    gGT = sdata->gGT;
    if (driver->driverID >= CNK_NATIVE_QA_MAX_LOCAL_PLAYERS ||
        driver->driverID >= gGT->numPlyrCurrGame ||
        (gGT->gameMode1 & MAIN_MENU) != 0 ||
        gGT->levelID == MAIN_MENU_LEVEL ||
        RacerLoadout_IsAdventureLocked())
        return 0;
    return RacerLoadout_GetPhysicsClass(driver->driverID) ==
           RACER_PHYSICS_CNK_PREVIEW;
}

/* Source point: end of VehPhysForce_OnApplyForces, BEFORE collision sweeps. */
static void CNKNativeBridgeQA_AfterForces(struct Driver *driver)
{
    struct CNKNativeQASlot *slot;
    if (!CNKNativeBridgeQA_IsEligible(driver))
        return;
    slot = &s_cnkNativeQASlots[driver->driverID];
    slot->velocityAfterForces = driver->velocity;
    slot->positionBeforeContacts = driver->posCurr;
    slot->speedAfterForces = driver->speed;
    slot->phaseVelocity[0] = driver->velocity;
    slot->phasePosition[0] = driver->posCurr;
    slot->phaseMask = 1u;
    slot->hasForceSample = 1;
}

/* Called just AFTER each original DriverFunc callback by MainFrame.c,
 * in both the hub and regular dispatch path. No gameplay mutation.
 * Record exactly the callback boundary: no assumption that every
 * delta is caused by track collisions or matches PS2/Havok. */
static void CNKNativeBridgeQA_AfterDriverStage(int stage, struct Driver *driver)
{
    struct CNKNativeQASlot *slot;
    unsigned index;
    if (stage < DRIVER_FUNC_COLL_MOVED || stage > DRIVER_FUNC_JUMP_FRICTION)
        return;
    if (!CNKNativeBridgeQA_IsEligible(driver))
        return;
    slot = &s_cnkNativeQASlots[driver->driverID];
    if (!slot->hasForceSample || !(slot->phaseMask & 1u))
        return;
    index = (unsigned)(stage - DRIVER_FUNC_APPLY_FORCES);
    if (index == 0u || index >= 5u)
        return;
    slot->phaseVelocity[index] = driver->velocity;
    slot->phasePosition[index] = driver->posCurr;
    slot->phaseMask |= (1u << index);
}

/* The phase array is stage 5..9, and live Driver is after stage 10.
 * Each logged dvN/dpN is the RAW change between stages N-1 and N.
 * Pairwise telescoping allows the next parser to prove that all five
 * deltas sum to existing [CNK SHADOW] post-pre vector deltas. */
static void CNKNativeBridgeQA_WritePhaseLog(
    const struct Driver *driver, const struct CNKNativeQASlot *slot,
    int row, int time)
{
    char line[1200];
    int used;
    int index;
    if ((slot->phaseMask & 0x1fu) != 0x1fu) {
        Platform_LogWarn(
            "[CNK PHASE MISSING] p=%u row=%d t=%d mask=0x%x\n",
            (unsigned)driver->driverID + 1u, row, time,
            slot->phaseMask & 0x1fu);
        return;
    }
    used = snprintf(line, sizeof(line),
                    "[CNK PHASE] p=%u row=%d t=%d mask=0x%x",
                    (unsigned)driver->driverID + 1u,
                    row, time, slot->phaseMask & 0x1fu);
    if (used < 0 || used >= (int)sizeof(line))
        return;
    for (index = 1; index <= 5; ++index) {
        const Vec3 *v0 = &slot->phaseVelocity[index - 1];
        const Vec3 *p0 = &slot->phasePosition[index - 1];
        const Vec3 *v1 = (index == 5) ? &driver->velocity
                                     : &slot->phaseVelocity[index];
        const Vec3 *p1 = (index == 5) ? &driver->posCurr
                                     : &slot->phasePosition[index];
        int written = snprintf(
            line + used, sizeof(line) - (size_t)used,
            " dv%d=(%lld,%lld,%lld) dp%d=(%lld,%lld,%lld)",
            index + DRIVER_FUNC_APPLY_FORCES,
            (long long)v1->x - (long long)v0->x,
            (long long)v1->y - (long long)v0->y,
            (long long)v1->z - (long long)v0->z,
            index + DRIVER_FUNC_APPLY_FORCES,
            (long long)p1->x - (long long)p0->x,
            (long long)p1->y - (long long)p0->y,
            (long long)p1->z - (long long)p0->z);
        if (written < 0 || written >= (int)(sizeof(line) - (size_t)used)) {
            Platform_LogWarn("[CNK PHASE MISSING] phase output truncated; CTR unchanged\n");
            return;
        }
        used += written;
    }
    Platform_Log("%s\n", line);
}

/* Source point: end of VehPhysForce_TranslateMatrix, AFTER contacts.
 * Raw CTR integer/fixed-point fields intentionally NOT scaled to PS2. */
static void CNKNativeBridgeQA_AfterContacts(struct Driver *driver)
{
    const struct GameTracker *gGT;
    struct CNKNativeQASlot *slot;
    int ms;
    if (!CNKNativeBridgeQA_IsEligible(driver))
        return;
    slot = &s_cnkNativeQASlots[driver->driverID];
    if (!slot->hasForceSample)
        return;
    slot->hasForceSample = 0;
    gGT = sdata->gGT;

    if (gGT->elapsedEventTime < slot->previousRaceTime) {
        slot->sampleCount = 0;
        slot->timeSinceSampleMS = 0;
    }
    slot->previousRaceTime = gGT->elapsedEventTime;
    ms = gGT->elapsedTimeMS;
    if (ms < 0 || ms > 250)
        ms = 0;
    if (slot->sampleCount >= CNK_NATIVE_QA_MAX_SAMPLES)
        return;
    if (slot->timeSinceSampleMS < CNK_NATIVE_QA_SAMPLE_INTERVAL_MS) {
        slot->timeSinceSampleMS += ms;
        return;
    }
    slot->timeSinceSampleMS = 0;
    CNKNativeBridgeQA_CheckLibraryOnce();
    ++slot->sampleCount;

    /* Position before and after one contact step, before-force and
     * post-contact velocity, exact game flags and frame delta.
     * No writes to driver data, no conversion constants or Havok guesses. */
    Platform_Log(
        "[CNK SHADOW] p=%u row=%d t=%d dtms=%d state=%u "
        "prePos=(%d,%d,%d) postPos=(%d,%d,%d) "
        "preVel=(%d,%d,%d) postVel=(%d,%d,%d) "
        "speed=%d approx=%d yaw=%d terrain=%u step=0x%x "
        "touch=%u flags=0x%x jump=%d\n",
        (unsigned)driver->driverID + 1u,
        slot->sampleCount, (int)gGT->elapsedEventTime,
        (int)gGT->elapsedTimeMS, (unsigned)driver->kartState,
        (int)slot->positionBeforeContacts.x,
        (int)slot->positionBeforeContacts.y,
        (int)slot->positionBeforeContacts.z,
        (int)driver->posCurr.x, (int)driver->posCurr.y,
        (int)driver->posCurr.z,
        (int)slot->velocityAfterForces.x,
        (int)slot->velocityAfterForces.y,
        (int)slot->velocityAfterForces.z,
        (int)driver->velocity.x, (int)driver->velocity.y,
        (int)driver->velocity.z,
        (int)driver->speed, (int)driver->speedApprox,
        (int)driver->rotCurr.y, (unsigned)driver->currentTerrain,
        (unsigned)driver->stepFlagSet,
        driver->currBlockTouching ? 1u : 0u,
        (unsigned)driver->actionsFlagSet,
        (int)driver->jumpHeightCurr);
    /* Only diagnostic vector decomposition; no writes to Driver. */
    CNKNativeBridgeQA_WritePhaseLog(
        driver, slot, slot->sampleCount, (int)gGT->elapsedEventTime);
}
