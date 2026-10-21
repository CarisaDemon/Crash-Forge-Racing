#ifndef RACER_LOADOUT_H
#define RACER_LOADOUT_H

#define RACER_LOADOUT_MAX_DRIVERS 8
#define RACER_LOADOUT_INHERIT (-2)
#define RACER_LOADOUT_ORIGINAL (-1)

/* Physics options are per-player choices independent of the character model,
 * kart or wheels. DEFAULT means original retail/mod engine profile. */
enum NativeRacerPhysicsClass
{
    RACER_PHYSICS_DEFAULT = -1,
    RACER_PHYSICS_BALANCED = BALANCED,
    RACER_PHYSICS_ACCEL = ACCEL,
    RACER_PHYSICS_SPEED = SPEED,
    RACER_PHYSICS_TURN = TURN,
    RACER_PHYSICS_MAX = NUM_CLASSES, /* composite, NOT a MetaPhys table column */
#if defined(CTR_CNK_PHYSICS_PREVIEW_QA)
    RACER_PHYSICS_CNK_PREVIEW = NUM_CLASSES + 1, /* extra style, no MetaPhys index */
#endif
};
#if defined(CTR_CNK_PHYSICS_PREVIEW_QA)
#define RACER_PHYSICS_LAST RACER_PHYSICS_CNK_PREVIEW
#else
#define RACER_PHYSICS_LAST RACER_PHYSICS_MAX
#endif

enum NativeRacerSelectionPart
{
    RACER_SELECT_CHARACTER = 0,
    RACER_SELECT_PHYSICS,
    RACER_SELECT_KART,
    RACER_SELECT_KART_COLOR,
    RACER_SELECT_WHEELS,
    RACER_SELECT_ENGINE,
    RACER_SELECT_READY,
};

struct NativeRacerLoadout
{
    s16 characterID;
    s16 kartID;
    s16 wheelID;
    u8 selectionPart;
    u8 kartColor;
    s8 physicsClass; /* -1 default, 0..3 retail, 4 Penta-style max stats */
    s16 engineSoundID; /* -1 kart engine, -2 character retail,
                         * -3..-6 forced retail class, >=0 audio pack */
};

void RacerLoadout_Init(void);
void RacerLoadout_ResetAll(void);
void RacerLoadout_ResetSlot(int driverID, int characterID);
void RacerLoadout_SyncCharactersFromGame(void);
const struct NativeRacerLoadout *RacerLoadout_Get(int driverID);
int RacerLoadout_GetCharacter(int driverID);
int RacerLoadout_GetKart(int driverID);
int RacerLoadout_GetKartColor(int driverID);
int RacerLoadout_GetWheels(int driverID);
int RacerLoadout_GetPhysicsClass(int driverID);
int RacerLoadout_GetEngineSound(int driverID);
int RacerLoadout_GetEffectiveEngineSound(int driverID);
int RacerLoadout_GetEffectiveEngineSoundClass(int driverID);
int RacerLoadout_GetEffectivePhysicsEngine(int driverID);
int RacerLoadout_UsesMaxStats(int driverID);
const char *RacerLoadout_PhysicsName(int physicsClass);
int RacerLoadout_GetEffectiveKart(int driverID);
int RacerLoadout_GetEffectiveWheels(int driverID);

void RacerLoadout_SetCharacter(int driverID, int characterID);
void RacerLoadout_SetKart(int driverID, int kartID);
void RacerLoadout_SetKartColor(int driverID, int colorID);
void RacerLoadout_SetWheels(int driverID, int wheelID);
void RacerLoadout_SetPhysicsClass(int driverID, int physicsClass);
void RacerLoadout_SetEngineSound(int driverID, int engineSoundID);
void RacerLoadout_SetSelectionPart(int driverID, int part);
int RacerLoadout_GetSelectionPart(int driverID);

b32 RacerLoadout_IsAdventureLocked(void);

#endif
