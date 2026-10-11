#ifndef RACER_LOADOUT_H
#define RACER_LOADOUT_H

#define RACER_LOADOUT_MAX_DRIVERS 8
#define RACER_LOADOUT_INHERIT (-2)
#define RACER_LOADOUT_ORIGINAL (-1)

enum NativeRacerSelectionPart
{
    RACER_SELECT_CHARACTER = 0,
    RACER_SELECT_KART,
    RACER_SELECT_KART_COLOR,
    RACER_SELECT_WHEELS,
    RACER_SELECT_READY,
};

struct NativeRacerLoadout
{
    s16 characterID;
    s16 kartID;
    s16 wheelID;
    u8 selectionPart;
    u8 kartColor;
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
int RacerLoadout_GetEffectiveKart(int driverID);
int RacerLoadout_GetEffectiveWheels(int driverID);

void RacerLoadout_SetCharacter(int driverID, int characterID);
void RacerLoadout_SetKart(int driverID, int kartID);
void RacerLoadout_SetKartColor(int driverID, int colorID);
void RacerLoadout_SetWheels(int driverID, int wheelID);
void RacerLoadout_SetSelectionPart(int driverID, int part);
int RacerLoadout_GetSelectionPart(int driverID);

b32 RacerLoadout_IsAdventureLocked(void);

#endif
