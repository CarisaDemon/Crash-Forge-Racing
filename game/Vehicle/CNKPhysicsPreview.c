/* CF:R CNK handling prototype (QA ONLY).
 *
 * Adds CNK (TEST) to the EXISTING Multiplayer / Time Trial physics selector.
 * It preserves all existing CTR classes, bots, adventure and the physics
 * integration code. This is a handling adapter, not the CNK retail solver.
 *
 * Original CNK data sources from the owner's local extraction:
 *   GameCube/common/physics/kpbase.csv
 *   GameCube/common/physics/kpcrash.csv
 *   (all physical CSVs byte-identical for PS2).
 * Verified original Crash CNK acceleration:
 *   26.0 * 0.906593407 = 23.571428582
 * Verified original CNK drift turn rate ratio:
 *   135.0 / 70.0 = 1.92857143.
 *
 * CNK's coordinates and units differ from CTR's 32-bit fixed-point physics,
 * so these relations are normalized to existing CTR stat ranges.
 * This cannot be called an exact port of the CNK kart movement solver.
 * For a full port continue the PS2 MIPS recovery in CNK_Physics_C.
 */
#include <common.h>
#include <RacerLoadout.h>

static int CNKPhysicsPreview_ClampStat(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void CNKPhysicsPreview_Apply(struct Driver *driver)
{
    struct GameTracker *gGT;
    int balancedAccel, balancedTurn, balancedDrift, turnCeiling, driftCeiling;
    int accel, turn, drift;
    const double cnkBaseAccel = 26.0;
    const double cnkCrashAccelMultiplier = 0.906593407;
    const double cnkSlideRatio = 135.0 / 70.0;

    if (driver == NULL || sdata == NULL || sdata->gGT == NULL)
        return;
    gGT = sdata->gGT;

    /* Human P1-P4 only, Time Trial or Arcade. No bots, ghosts or story. */
    if (driver->driverID >= 4 ||
        driver->driverID >= gGT->numPlyrCurrGame ||
        RacerLoadout_IsAdventureLocked() ||
        RacerLoadout_GetPhysicsClass(driver->driverID) != RACER_PHYSICS_CNK_PREVIEW)
        return;
#if defined(CTR_CNK_NATIVE_BRIDGE_QA)
    /* This mode is an ORIGINAL-CTR baseline + source-verified CNK shadow
     * tracing, NOT the former relative-CSV handling approximation.
     * No CNK impulse is applied until original PS2/CTR units are mapped. */
    Platform_LogWarn("[CNK NATIVE QA] source-backed observer ONLY: player=%u, CTR BALANCED physics retained, no CNK forces injected\n",
                     (unsigned)driver->driverID + 1u);
    return;
#endif

    /* The original game's BALANCED values remain the reference for every
     * stat that is NOT explicitly handled here, especially forward speed,
     * Wumpa bonus, reserves, turbo and jump impulses. This avoids inventing
     * a speed advantage before measured lap-time balancing is available. */
    balancedAccel = data.metaPhys[ACCEL_CLASS_STAT].value[BALANCED];
    balancedTurn = data.metaPhys[TURN_RATE].value[BALANCED];
    balancedDrift = data.metaPhys[DRIFT_TURN_BASE].value[BALANCED];
    turnCeiling = data.metaPhys[TURN_RATE].value[TURN];
    driftCeiling = data.metaPhys[DRIFT_TURN_BASE].value[TURN];

    /* Original CNK Crash is about 9.34% below the generic 26.0
     * acceleration parameter. Use the RELATIVE factor in the CTR scale. */
    accel = (int)((double)balancedAccel * cnkCrashAccelMultiplier + 0.5);

    /* Original CNK "out of slide" steer rate is 135 vs "into slide" 70.
     * CTR already supplies the collision, powerslide and camera systems.
     * Apply this RELATIVE turn preference, capped at existing TURN class. */
    turn = balancedTurn +
        (int)((double)(turnCeiling - balancedTurn)*(cnkSlideRatio - 1.0)+0.5);
    drift = balancedDrift +
        (int)((double)(driftCeiling - balancedDrift)*(cnkSlideRatio - 1.0)+0.5);

    /* Guardrails for fair experimental testing; not a measured balance
     * certification. CTR speed and the entire boost/reserve system remain
     * exactly the original BALANCED engine-class values. */
    accel = CNKPhysicsPreview_ClampStat(accel, (balancedAccel*4)/5, balancedAccel);
    turn = CNKPhysicsPreview_ClampStat(turn, balancedTurn, turnCeiling);
    if (driftCeiling > balancedDrift+1) --driftCeiling;
    drift = CNKPhysicsPreview_ClampStat(drift, balancedDrift, driftCeiling);

    driver->const_Accel_ClassStat = (s16)accel;
    driver->const_TurnRate = (u8)turn;
    driver->const_DriftTurnBase = (s8)drift;

    Platform_LogWarn("[CNK PHYS QA] player=%d style=CNK_TEST accel=%d turn=%d drift=%d speed=%d cap=%d; original CTR top speed/boost/jump retained\n",
        driver->driverID+1, accel, turn, drift,
        (int)driver->const_Speed_ClassStat,
        (int)driver->const_AccelSpeed_ClassStat);

    (void)cnkBaseAccel;
}
