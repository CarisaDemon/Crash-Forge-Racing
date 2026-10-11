#ifndef VEHICLE_PART_REGISTRY_H
#define VEHICLE_PART_REGISTRY_H

enum VehiclePartKind
{
    VEHICLE_PART_KART = 0,
    VEHICLE_PART_WHEELS,
    VEHICLE_PART_KIND_COUNT,
};

#define VEHICLE_PART_MAX_EXHAUSTS 4
#define VEHICLE_PART_MAX_COLORS 12

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

    u8 hasSeatOffset;
    u8 wheelMountMask;
    u8 exhaustMountMask;
    u8 exhaustCount;
    u8 hasExhaustConfig;
    /* Optional kart skin palette; position 0 always uses the original MTL. */
    u8 colorCount;
    char colorNames[VEHICLE_PART_MAX_COLORS][24];
    char colorTextureNames[VEHICLE_PART_MAX_COLORS][128];
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
b32 VehiclePartRegistry_GetSeatOffset(int kartID, float out[3]);
b32 VehiclePartRegistry_GetWheelMount(int kartID, int wheelIndex, float out[3]);
int VehiclePartRegistry_GetExhaustCount(int kartID);
b32 VehiclePartRegistry_GetExhaustMount(
    int kartID,
    int exhaustIndex,
    float outPosition[3],
    float outRotation[3]);

#endif
