#include <common.h>
#include <RacerLoadout.h>
#include <CharacterRegistry.h>
#include <VehiclePartRegistry.h>

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
