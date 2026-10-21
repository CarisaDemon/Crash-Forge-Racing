#include <common.h>
#include <RacerLoadout.h>
#include <CharacterRegistry.h>
#include <VehiclePartRegistry.h>
#include <EngineSoundRegistry.h>
#if defined(CTR_NATIVE)
#include <NativeHangout.h>
#endif

static struct NativeRacerLoadout s_racerLoadouts[RACER_LOADOUT_MAX_DRIVERS];
static int s_racerLoadoutsInitialized;

static int RacerLoadout_ValidDriver(int driverID)
{
    return driverID >= 0 && driverID < RACER_LOADOUT_MAX_DRIVERS;
}

static void RacerLoadout_EnsureInit(void)
{
    if (!s_racerLoadoutsInitialized)
        RacerLoadout_Init();
}

b32 RacerLoadout_IsAdventureLocked(void)
{
    const u32 storyMask =
        ADVENTURE_MODE |
        ADVENTURE_BOSS |
        ADVENTURE_CUP |
        ADVENTURE_ARENA;

#if defined(CTR_NATIVE)
    /* Hangout deliberately reuses Adventure-hub geometry and therefore keeps
     * ADVENTURE_ARENA set, but it is a sandbox rather than Story mode. Only
     * this explicit social session may use the normal modular loadout path. */
    if (NativeHangout_IsActive())
        return false;
#endif

    return sdata->gGT != NULL &&
           (sdata->gGT->gameMode1 & storyMask) != 0;
}

void RacerLoadout_ResetSlot(int driverID, int characterID)
{
    if (!RacerLoadout_ValidDriver(driverID))
        return;

    s_racerLoadouts[driverID].characterID = (s16)characterID;
    s_racerLoadouts[driverID].kartID = RACER_LOADOUT_INHERIT;
    s_racerLoadouts[driverID].wheelID = RACER_LOADOUT_INHERIT;
    s_racerLoadouts[driverID].selectionPart = RACER_SELECT_CHARACTER;
    s_racerLoadouts[driverID].kartColor = 0;
    s_racerLoadouts[driverID].physicsClass = RACER_PHYSICS_DEFAULT;
    s_racerLoadouts[driverID].engineSoundID = ENGINE_SOUND_AUTO;
}

void RacerLoadout_ResetAll(void)
{
    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
        RacerLoadout_ResetSlot(i, data.characterIDs[i]);

    s_racerLoadoutsInitialized = 1;
}

void RacerLoadout_Init(void)
{
    if (s_racerLoadoutsInitialized)
        return;

    RacerLoadout_ResetAll();
}

void RacerLoadout_SyncCharactersFromGame(void)
{
    RacerLoadout_EnsureInit();

    for (int i = 0; i < RACER_LOADOUT_MAX_DRIVERS; i++)
        s_racerLoadouts[i].characterID = data.characterIDs[i];
}

const struct NativeRacerLoadout *RacerLoadout_Get(int driverID)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID))
        return NULL;

    return &s_racerLoadouts[driverID];
}

int RacerLoadout_GetCharacter(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->characterID : CRASH_BANDICOOT;
}

int RacerLoadout_GetKart(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->kartID : RACER_LOADOUT_ORIGINAL;
}

int RacerLoadout_GetKartColor(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->kartColor : 0;
}

int RacerLoadout_GetWheels(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->wheelID : RACER_LOADOUT_ORIGINAL;
}

int RacerLoadout_GetPhysicsClass(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->physicsClass : RACER_PHYSICS_DEFAULT;
}

int RacerLoadout_GetEngineSound(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->engineSoundID : ENGINE_SOUND_AUTO;
}

int RacerLoadout_GetEffectiveEngineSound(int driverID)
{
    int selected;
    /* Human and bot slots use the same ENGINE pack resolution.
     * Adventure still forces authored vanilla rival engines. */
    if (!RacerLoadout_ValidDriver(driverID) ||
        RacerLoadout_IsAdventureLocked()) return -1;
    selected = RacerLoadout_GetEngineSound(driverID);
    /* Only KART ENGINE automatically inherits the equipped kart's COMPLETE
     * audio pack. Explicit ENGINE choices override main, transmission and
     * optional layers as one unit (including forced retail class sounds). */
    if (selected == ENGINE_SOUND_AUTO)
    {
        /* KART ENGINE uses this kart's registered sound pack only: a valid
         * main loop, transmission or optional sound layer. No accidental
         * standalone pack inheritance and no borrowing from other karts. */
        int kartID = RacerLoadout_GetEffectiveKart(driverID);
        const struct VehiclePartDef *kart =
            VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
        if (kart == NULL)
            return -1;
        return EngineSoundRegistry_FindForKart(kartID);
    }
    if (selected >= 0)
        return EngineSoundRegistry_Get(selected) ? selected : -1;
    return -1;
}

int RacerLoadout_GetEffectiveEngineSoundClass(int driverID)
{
    int characterID;
    int retailID;
    int original;
    int selected;
    const struct GameTracker *gGT = sdata->gGT;

    if (!RacerLoadout_ValidDriver(driverID))
        return BALANCED;

    characterID = data.characterIDs[driverID];
    /* A custom character can declare a fallback retail model. The sound
     * fallback should follow that retail character, NOT the chosen physics. */
    retailID = CharacterRegistry_GetDriverPackID(characterID);
    original = CharacterRegistry_GetEngineClass(retailID);
    if (original < BALANCED || original >= NUM_CLASSES)
        original = BALANCED;

    if (!gGT || driverID >= gGT->numPlyrCurrGame ||
        driverID >= 4 || RacerLoadout_IsAdventureLocked())
        return original;

    selected = RacerLoadout_GetEngineSound(driverID);
    switch (selected)
    {
    case ENGINE_SOUND_ACCEL: return ACCEL;
    case ENGINE_SOUND_SPEED: return SPEED;
    case ENGINE_SOUND_TURN: return TURN;
    case ENGINE_SOUND_BALANCED: return BALANCED;
    default: return original;
    }
}

void RacerLoadout_SetEngineSound(int driverID, int engineSoundID)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID) || RacerLoadout_IsAdventureLocked())
        return;
    /* Fixed retail sound choices remain usable even without an engine mod. */
    if ((engineSoundID < 0 &&
         engineSoundID != ENGINE_SOUND_AUTO &&
         engineSoundID != ENGINE_SOUND_RETAIL &&
         engineSoundID != ENGINE_SOUND_ACCEL &&
         engineSoundID != ENGINE_SOUND_SPEED &&
         engineSoundID != ENGINE_SOUND_TURN &&
         engineSoundID != ENGINE_SOUND_BALANCED) ||
        (engineSoundID >= 0 && !EngineSoundRegistry_Get(engineSoundID)))
        engineSoundID = ENGINE_SOUND_AUTO;
    s_racerLoadouts[driverID].engineSoundID = (s16)engineSoundID;
}

void RacerLoadout_SetPhysicsClass(int driverID, int physicsClass)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID) || RacerLoadout_IsAdventureLocked())
        return;
    if (physicsClass < RACER_PHYSICS_DEFAULT || physicsClass > RACER_PHYSICS_LAST)
        physicsClass = RACER_PHYSICS_DEFAULT;
    s_racerLoadouts[driverID].physicsClass = (s8)physicsClass;
}

const char *RacerLoadout_PhysicsName(int physicsClass)
{
    switch (physicsClass)
    {
    case RACER_PHYSICS_DEFAULT: return "DEFAULT";
    case RACER_PHYSICS_BALANCED: return "BALANCED";
    case RACER_PHYSICS_ACCEL: return "ACCEL";
    case RACER_PHYSICS_SPEED: return "SPEED";
    case RACER_PHYSICS_TURN: return "TURN";
    case RACER_PHYSICS_MAX: return "MAX STATS";
#if defined(CTR_CNK_PHYSICS_PREVIEW_QA)
    case RACER_PHYSICS_CNK_PREVIEW: return "CNK (TEST)";
#endif
    default: return "DEFAULT";
    }
}

int RacerLoadout_GetEffectivePhysicsEngine(int driverID)
{
    /* Do not affect bots, main-menu AI drivers or Adventure's retail story
     * physics. This only applies to the local human slots, up to P4. */
    int characterID;
    int original;
    int selected;
    const struct GameTracker *gGT=sdata->gGT;
    if (!RacerLoadout_ValidDriver(driverID))
        return BALANCED;
    characterID=data.characterIDs[driverID];
    original=CharacterRegistry_GetEngineClass(characterID);
    if (original < BALANCED || original >= NUM_CLASSES)
        original=BALANCED;
    if (!gGT || driverID >= gGT->numPlyrCurrGame ||
        driverID >= 4 || RacerLoadout_IsAdventureLocked())
        return original;
    selected=RacerLoadout_GetPhysicsClass(driverID);
#if defined(CTR_CNK_PHYSICS_PREVIEW_QA)
    /* Use Balanced as the safe backing class; the CNK test adapter only
     * changes selected handling parameters, never speed or engine sounds. */
    if (selected == RACER_PHYSICS_CNK_PREVIEW)
        return BALANCED;
#endif
    if (selected>=BALANCED && selected<NUM_CLASSES)
        return selected;
    if (selected==RACER_PHYSICS_MAX)
        return SPEED; /* safe engine-audio fallback and common base stats */
    return original;
}

int RacerLoadout_UsesMaxStats(int driverID)
{
    const struct GameTracker *gGT=sdata->gGT;
    if (!RacerLoadout_ValidDriver(driverID) || !gGT ||
        driverID >= gGT->numPlyrCurrGame || driverID >= 4 ||
        RacerLoadout_IsAdventureLocked())
        return 0;
    return RacerLoadout_GetPhysicsClass(driverID) == RACER_PHYSICS_MAX;
}

int RacerLoadout_GetEffectiveKart(int driverID)
{
    int selected;
    const int characterID = RacerLoadout_GetCharacter(driverID);
    const struct CharacterDef *character =
        CharacterRegistry_GetByID(characterID);
    const b32 needsStandaloneOriginal =
        character != NULL &&
        (((character->flags & CHARACTER_REGISTRY_FLAG_RETAIL) != 0) ||
         CharacterRegistry_IsModularBody(characterID));
    const int originalReplacement =
        needsStandaloneOriginal ?
        VehiclePartRegistry_GetOriginalReplacementKartID() : -1;

    if (RacerLoadout_IsAdventureLocked())
        return RACER_LOADOUT_ORIGINAL;

    selected = RacerLoadout_GetKart(driverID);

    /*
     * Outside Adventure, ORIGINAL can be represented by a standalone native
     * kart. This is what lets retail/PC bodies and body-only mods share the
     * same original CTR vehicle without baking that kart into every racer.
     */
    if (selected == RACER_LOADOUT_ORIGINAL)
        return originalReplacement >= 0 ?
            originalReplacement : RACER_LOADOUT_ORIGINAL;

    if (selected != RACER_LOADOUT_INHERIT)
        return selected;

    /* `kart = <slug>` in character.ini is only a default. A player's explicit
       kart selection always wins, preserving the independent loadout system. */
    {
        const char *slug =
            CharacterRegistry_GetKartSlug(characterID);
        int defaultKart = slug ?
            VehiclePartRegistry_GetIDBySlug(VEHICLE_PART_KART, slug) : -1;

        if (defaultKart >= 0)
            return defaultKart;

        /*
         * Retail racers and modular bodies inherit the standalone ORIGINAL
         * replacement when one is installed. Legacy combined OBJ racers remain
         * untouched so their baked kart behavior is preserved.
         */
        if (originalReplacement >= 0)
            return originalReplacement;

        /*
         * Bots and other non-menu spawns do not pass through the interactive
         * Character -> Kart selector. A body-only racer still needs a vehicle,
         * so use the first registered kart as the final safe fallback.
         */
        if (CharacterRegistry_IsModularBody(characterID) &&
            VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0)
            return 0;
    }

    return RACER_LOADOUT_INHERIT;
}
int RacerLoadout_GetEffectiveWheels(int driverID)
{
    if (RacerLoadout_IsAdventureLocked())
        return RACER_LOADOUT_ORIGINAL;

    /* A no-wheel kart overrides any previous or global 3D wheel selection,
       but never deletes it: changing back to a normal kart restores wheels. */
    if (VehiclePartRegistry_HidesWheels(
            RacerLoadout_GetEffectiveKart(driverID)))
        return RACER_LOADOUT_ORIGINAL;

    return RacerLoadout_GetWheels(driverID);
}

void RacerLoadout_SetCharacter(int driverID, int characterID)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID))
        return;

    s_racerLoadouts[driverID].characterID = (s16)characterID;
    data.characterIDs[driverID] = (s16)characterID;
}

void RacerLoadout_SetKart(int driverID, int kartID)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID) || RacerLoadout_IsAdventureLocked())
        return;

    if (s_racerLoadouts[driverID].kartID != (s16)kartID)
        s_racerLoadouts[driverID].kartColor = 0;
    s_racerLoadouts[driverID].kartID = (s16)kartID;
}

void RacerLoadout_SetKartColor(int driverID, int colorID)
{
    int kartID;
    int count;
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID) ||
        RacerLoadout_IsAdventureLocked())
        return;
    kartID = RacerLoadout_GetEffectiveKart(driverID);
    count = VehiclePartRegistry_GetColorCount(kartID);
    if (colorID >= 0 && colorID < count)
        s_racerLoadouts[driverID].kartColor = (u8)colorID;
    else
        s_racerLoadouts[driverID].kartColor = 0;
}

void RacerLoadout_SetWheels(int driverID, int wheelID)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID) || RacerLoadout_IsAdventureLocked())
        return;

    s_racerLoadouts[driverID].wheelID = (s16)wheelID;
}

void RacerLoadout_SetSelectionPart(int driverID, int part)
{
    RacerLoadout_EnsureInit();
    if (!RacerLoadout_ValidDriver(driverID))
        return;

    if (part < RACER_SELECT_CHARACTER)
        part = RACER_SELECT_CHARACTER;
    if (part > RACER_SELECT_READY)
        part = RACER_SELECT_READY;

    s_racerLoadouts[driverID].selectionPart = (u8)part;
}

int RacerLoadout_GetSelectionPart(int driverID)
{
    const struct NativeRacerLoadout *loadout = RacerLoadout_Get(driverID);
    return loadout ? loadout->selectionPart : RACER_SELECT_CHARACTER;
}
