#include <common.h>
#include <NativeRacerVisual.h>
#include <CharacterRegistry.h>
#include <RacerLoadout.h>
#include <VehiclePartRegistry.h>
#include <platform/native_gltf.h>

struct Model *NativeRacerVisual_ResolveModel(int characterID)
{
    struct Model *model;
    int fallbackID;
    int modelFormat;

    /*
     * Story/Adventure always keeps CTR's original retail visual path.
     * Mod IDs intentionally collapse to their configured retail fallback here.
     */
    if (RacerLoadout_IsAdventureLocked())
    {
        fallbackID =
            (characterID >= CRASH_BANDICOOT &&
             characterID <= NITROS_OXIDE) ?
            characterID :
            CharacterRegistry_GetDriverPackID(characterID);

        return VehBirth_GetModelByName(
            data.MetaDataCharacters[fallbackID].name_Debug);
    }

    modelFormat =
        CharacterRegistry_GetModelFormat(characterID);

    /*
     * AUTO prefers a GLB when character.glb exists. This is what allows the
     * built-in retail slugs (crash, coco, etc.) to have optional PC/skeletal
     * visual overrides without creating duplicate roster entries.
     */
    if (modelFormat != CHARACTER_MODEL_OBJ)
    {
        const b32 isModularBody =
            CharacterRegistry_IsModularBody(characterID);
        const b32 hasModularVehicle =
            VehiclePartRegistry_GetCount(VEHICLE_PART_KART) > 0;

        /*
         * GLB racers are body-only presentation assets in this pipeline.
         * Require character.ini to mark them modular (directly, format=glb,
         * or kart=<slug>) so a stray character.glb can never appear without
         * a vehicle.
         */
        if (isModularBody &&
            hasModularVehicle)
        {
            model = NativeGltf_LoadRacer(characterID);
            if (model != NULL)
                return model;
        }
    }

    /*
     * Explicit GLB does not silently consume an OBJ with the same slug. If the
     * GLB is missing/broken, fall back to retail instead.
     */
    if (modelFormat != CHARACTER_MODEL_GLB)
    {
        model = VehBirth_GetModelByName(
            (char *)CharacterRegistry_GetAssetName(characterID));
        if (model != NULL)
            return model;
    }

    fallbackID =
        CharacterRegistry_GetDriverPackID(characterID);

    return VehBirth_GetModelByName(
        data.MetaDataCharacters[fallbackID].name_Debug);
}

b32 NativeRacerVisual_UsesSkeletalModel(int characterID)
{
    struct Model *model;

    if (RacerLoadout_IsAdventureLocked())
        return false;

    if (!CharacterRegistry_IsModularBody(characterID))
        return false;

    if (VehiclePartRegistry_GetCount(VEHICLE_PART_KART) <= 0)
        return false;

    model = NativeGltf_LoadRacer(characterID);
    return model != NULL && NativeGltf_IsModel(model);
}
