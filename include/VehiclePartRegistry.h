#ifndef VEHICLE_PART_REGISTRY_H
#define VEHICLE_PART_REGISTRY_H

enum VehiclePartKind
{
    VEHICLE_PART_KART = 0,
    VEHICLE_PART_WHEELS,
    VEHICLE_PART_KIND_COUNT,
};

/* 4 was the old fixed layout. 64 is a 32-bit safe resource budget;
 * keep UI, preview, emitter and flame Instance pool in sync. */
#define VEHICLE_PART_MAX_EXHAUSTS 64
#define VEHICLE_PART_MAX_COLORS 12
/* Three independently configured optional audio layers per kart.
 * [audio] audio_layer_1_* through audio_layer_3_*. */
#define VEHICLE_PART_AUDIO_LAYER_COUNT 3
#define VEHICLE_AUDIO_MODE_RPM 0
#define VEHICLE_AUDIO_MODE_SPEED 1
#define VEHICLE_AUDIO_MODE_ACCEL 2
#define VEHICLE_AUDIO_MODE_DRIFT 3
#define VEHICLE_AUDIO_MODE_TURBO 4

struct VehiclePartDef
{
    s16 id;
    u8 kind;
    u8 reserved;
    char slug[64];
    char assetName[64];
    char displayName[64];
    float modelScale;
    float modelOffset[3];
    float referenceMin[3];
    float referenceExtent[3];
    u8 hasReferenceBounds;
    u8 replacesOriginal;
    u8 hideWheels; /* A no-wheel/hovercraft kart suppresses external tires. */
    u8 unifiedMod; /* assets are stored in mods/<id> instead of mods/karts/<id>. */

    /* Optional attachment points in CTR local model coordinates.
       Karts can override the retail seat/wheel locations without affecting
       legacy parts that omit this section. */
    float seatOffset[3];
    float wheelMount[4][3];

    /*
     * Optional exhaust attachment points.
     * Position uses the same CTR-local units as seat/wheel mounts.
     * Rotation is Euler degrees (X/Y/Z), applied locally to smoke velocity and
     * turbo flame orientation. Zero rotation preserves CTR's default exhaust
     * direction.
     */
    float exhaustMount[VEHICLE_PART_MAX_EXHAUSTS][3];
    float exhaustRotation[VEHICLE_PART_MAX_EXHAUSTS][3];
    /* Individual visual scale of flame + smoke, 1.0 on existing INIs. */
    float exhaustScale[VEHICLE_PART_MAX_EXHAUSTS];

    u8 hasSeatOffset;
    u8 wheelMountMask;
    /* One validity flag per outlet (the old 8-bit bitmask only held 4). */
    u8 exhaustMountValid[VEHICLE_PART_MAX_EXHAUSTS];
    u8 exhaustCount;
    u8 hasExhaustConfig;
    /* Optional kart skin palette; position 0 always uses the original MTL. */
    u8 colorCount;
    char colorNames[VEHICLE_PART_MAX_COLORS][24];
    char colorTextureNames[VEHICLE_PART_MAX_COLORS][128];
    /* Optional audio file paths relative to this kart's mod directory. */
    char engineLoop[128];
    char engineIdle[128];
    char engineTurbo[128];
    /* Secondary gearbox: attached to the selected ENGINE pack owner,
     * never automatically retained by a different engine sound choice. */
    char secondaryEngineLoop[128];
    float secondaryEngineVolume; /* percent (0..100), default 35 */
    float secondaryEnginePitchMin; /* playback pitch at zero speed */
    float secondaryEnginePitchMax; /* playback pitch at full speed */
    int secondaryEngineSpeedMax; /* reference physical kart speed */
    /* Optional [audio] PRIMARY engine pitch curve. Enabled only when
     * engine_pitch_min and engine_pitch_max are both valid. */
    u8 customEnginePitchCurve;
    float enginePitchMin; /* e.g. crash3 idle=0.62x */
    float enginePitchMax; /* e.g. crash3 USF=3.50x */
    int enginePitchSpeedMax; /* 0=driver's physics USF top-speed reference */
    u8 enginePitchMode; /* 0=road speed (old behavior), 1=retail HOWL rev state */
    /* Audio layers belong to the chosen ENGINE pack's owner kart, not its
     * visual kart. They appear under one kart-named ENGINE menu option.
     * Each mode supports independent gain/pitch/activation. */
    char audioLayerLoop[VEHICLE_PART_AUDIO_LAYER_COUNT][128];
    u8 audioLayerMode[VEHICLE_PART_AUDIO_LAYER_COUNT];
    float audioLayerVolume[VEHICLE_PART_AUDIO_LAYER_COUNT];
    float audioLayerPitchMin[VEHICLE_PART_AUDIO_LAYER_COUNT];
    float audioLayerPitchMax[VEHICLE_PART_AUDIO_LAYER_COUNT];
    int audioLayerSpeedMax[VEHICLE_PART_AUDIO_LAYER_COUNT]; /* 0=physics */
};

void VehiclePartRegistry_Init(void);
void VehiclePartRegistry_Shutdown(void);
int VehiclePartRegistry_GetCount(int kind);
const struct VehiclePartDef *VehiclePartRegistry_GetByID(int kind, int id);
const struct VehiclePartDef *VehiclePartRegistry_GetByIndex(int kind, int index);
const char *VehiclePartRegistry_GetDisplayName(int kind, int id);
const char *VehiclePartRegistry_GetAssetName(int kind, int id);
int VehiclePartRegistry_GetColorCount(int kartID);
const char *VehiclePartRegistry_GetColorName(int kartID, int colorIndex);
const char *VehiclePartRegistry_GetColorTexture(int kartID, int colorIndex);
int VehiclePartRegistry_GetIDBySlug(int kind, const char *slug);
int VehiclePartRegistry_GetOriginalReplacementKartID(void);
b32 VehiclePartRegistry_HidesWheels(int kartID);
b32 VehiclePartRegistry_GetSeatOffset(int kartID, float out[3]);
b32 VehiclePartRegistry_GetWheelMount(int kartID, int wheelIndex, float out[3]);
int VehiclePartRegistry_GetExhaustCount(int kartID);
float VehiclePartRegistry_GetExhaustScale(int kartID, int exhaustIndex);
b32 VehiclePartRegistry_GetExhaustMount(
    int kartID,
    int exhaustIndex,
    float outPosition[3],
    float outRotation[3]);

#endif
