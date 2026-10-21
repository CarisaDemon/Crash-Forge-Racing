#include <common.h>
#include <VehiclePartRegistry.h>
#include <EngineSoundRegistry.h>
#include <NativeSession.h>
#include <platform/native_assets.h>
#include <platform/native_mod_packages.h>

#if defined(CTR_NATIVE)
#include <SDL3/SDL.h>
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

struct VehiclePartList
{
    struct VehiclePartDef *items;
    int count;
    int capacity;
};

static struct VehiclePartList s_vehicleParts[VEHICLE_PART_KIND_COUNT];
static int s_vehiclePartsInitialized;

static char *VehiclePart_Trim(char *text)
{
    char *end;

    while (*text && isspace((unsigned char)*text))
        text++;

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        end--;

    *end = 0;
    return text;
}

static int VehiclePart_ParseBool(const char *value, int fallback)
{
    if (value == NULL || !*value)
        return fallback;

    if (!SDL_strcasecmp(value, "true") ||
        !SDL_strcasecmp(value, "yes") ||
        !SDL_strcasecmp(value, "on") ||
        !strcmp(value, "1"))
        return 1;

    if (!SDL_strcasecmp(value, "false") ||
        !SDL_strcasecmp(value, "no") ||
        !SDL_strcasecmp(value, "off") ||
        !strcmp(value, "0"))
        return 0;

    return fallback;
}
static int VehiclePart_AudioFileSafe(const char *path)
{
    size_t length;
    if (!path || !*path || *path == '/' || *path == '\\' ||
        strstr(path, "..")) return 0;
    length = strlen(path);
    if (length >= 128) return 0;
    for (; *path; ++path) {
        unsigned char c = (unsigned char)*path;
        if (!(isalnum(c) || c == '_' || c == '-' ||
              c == '.' || c == '/' || c == '\\')) return 0;
    }
    return 1;
}
static int VehiclePart_IsDirectory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int VehiclePart_Reserve(int kind, int needed)
{
    struct VehiclePartList *list;
    struct VehiclePartDef *next;
    int capacity;

    if (kind < 0 || kind >= VEHICLE_PART_KIND_COUNT)
        return 0;

    list = &s_vehicleParts[kind];
    if (needed <= list->capacity)
        return 1;

    capacity = list->capacity ? list->capacity * 2 : 16;
    while (capacity < needed)
        capacity *= 2;

    next = realloc(list->items, (size_t)capacity * sizeof(*next));
    if (next == NULL)
        return 0;

    list->items = next;
    list->capacity = capacity;
    return 1;
}
static int VehiclePart_Compare(const void *a, const void *b)
{
    const struct VehiclePartDef *pa = a;
    const struct VehiclePartDef *pb = b;
    return SDL_strcasecmp(pa->displayName, pb->displayName);
}

static int VehiclePart_HasSlug(int kind, const char *slug)
{
    const struct VehiclePartList *list = &s_vehicleParts[kind];

    for (int i = 0; i < list->count; i++)
    {
        if (!SDL_strcasecmp(list->items[i].slug, slug))
            return 1;
    }

    return 0;
}

static const char *VehiclePart_ConfigName(int kind)
{
    return kind == VEHICLE_PART_KART ? "kart.ini" : "wheels.ini";
}

static const char *VehiclePart_DirectoryName(int kind)
{
    return kind == VEHICLE_PART_KART ? "karts" : "wheels";
}
static void VehiclePart_LoadOne(int kind, const char *folderName, int unified)
{
    struct VehiclePartList *list = &s_vehicleParts[kind];
    struct VehiclePartDef temp;
    char relative[512];
    char configPath[1024];
    char section[64] = "";
    char line[1024];
    FILE *file;
    int enabled = 1;
    int valid = 1;
    u8 seatComponents = 0;
    u8 wheelComponents[4] = {0, 0, 0, 0};
    u8 exhaustComponents[VEHICLE_PART_MAX_EXHAUSTS] = {0};
    int exhaustCountExplicit = -1;
    int highestExhaustIndex = -1;
    int enginePitchMinSeen = 0;
    int enginePitchMaxSeen = 0;

    if (folderName == NULL || !*folderName ||
        strlen(folderName) >= sizeof(temp.slug) ||
        VehiclePart_HasSlug(kind, folderName))
        return;

    if (unified)
        snprintf(relative, sizeof(relative), "mods/%s/%s",
                 folderName, VehiclePart_ConfigName(kind));
    else
        snprintf(relative, sizeof(relative), "mods/%s/%s/%s",
                 VehiclePart_DirectoryName(kind),
                 folderName,
                 VehiclePart_ConfigName(kind));

    if (!NativeAssets_BuildPath(relative, configPath, sizeof(configPath)))
        return;

    file = fopen(configPath, "rb");
    if (file == NULL)
        return;

    memset(&temp, 0, sizeof(temp));
    temp.kind = (u8)kind;
    temp.unifiedMod = (u8)(unified != 0);
    temp.modelScale = 1.0f;
    /* Optional secondary motor: no audio file means absolutely no effect. */
    temp.secondaryEngineVolume = 35.0f;
    temp.secondaryEnginePitchMin = 0.85f;
    temp.secondaryEnginePitchMax = 2.5f;
    temp.secondaryEngineSpeedMax = 32767;
    temp.enginePitchMin = 0.86f;
    temp.enginePitchMax = 2.65f;
    temp.enginePitchSpeedMax = 0; /* auto: match actual physics USF maximum */
    for (int i = 0; i < VEHICLE_PART_AUDIO_LAYER_COUNT; ++i) {
        temp.audioLayerVolume[i] = 35.0f;
        temp.audioLayerPitchMin[i] = 0.80f;
        temp.audioLayerPitchMax[i] = 2.50f;
        temp.audioLayerSpeedMax[i] = 0; /* physics reference */
        temp.audioLayerMode[i] = VEHICLE_AUDIO_MODE_RPM;
    }
    for (int i = 0; i < VEHICLE_PART_MAX_EXHAUSTS; ++i)
        temp.exhaustScale[i] = 1.0f;
    snprintf(temp.slug, sizeof(temp.slug), "%s", folderName);
    snprintf(temp.assetName, sizeof(temp.assetName), "%s", folderName);
    snprintf(temp.displayName, sizeof(temp.displayName), "%s", folderName);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *text = VehiclePart_Trim(line);
        char *equal;
        char *key;
        char *value;

        if (!*text || *text == ';' || *text == '#')
            continue;

        if (*text == '[')
        {
            char *close = strchr(text, ']');
            if (close != NULL)
            {
                *close = 0;
                snprintf(section, sizeof(section), "%s",
                         VehiclePart_Trim(text + 1));
            }
            continue;
        }

        equal = strchr(text, '=');
        if (equal == NULL)
            continue;
        *equal = 0;
        key = VehiclePart_Trim(text);
        value = VehiclePart_Trim(equal + 1);

        if (!SDL_strcasecmp(section, "kart") ||
            !SDL_strcasecmp(section, "wheels") ||
            !SDL_strcasecmp(section, "wheel"))
        {
            if (!SDL_strcasecmp(key, "name"))
                snprintf(temp.displayName, sizeof(temp.displayName), "%s", value);
            else if (!SDL_strcasecmp(key, "enabled"))
                enabled = VehiclePart_ParseBool(value, enabled);
            else if (kind == VEHICLE_PART_KART &&
                     !SDL_strcasecmp(key, "hide_wheels"))
                temp.hideWheels = (u8)VehiclePart_ParseBool(
                    value, temp.hideWheels);
            else if (kind == VEHICLE_PART_KART &&
                     (!SDL_strcasecmp(key, "replace_original") ||
                      !SDL_strcasecmp(key, "replaces_original") ||
                      !SDL_strcasecmp(key, "original_replacement")))
                temp.replacesOriginal =
                    (u8)VehiclePart_ParseBool(value, temp.replacesOriginal);
        }
        else if (!SDL_strcasecmp(section, "model"))
        {
            float parsed = 0.0f;

            if (!SDL_strcasecmp(key, "asset") ||
                !SDL_strcasecmp(key, "asset_name"))
            {
                if (strlen(value) >= sizeof(temp.assetName))
                    valid = 0;
                else
                    snprintf(temp.assetName, sizeof(temp.assetName), "%s", value);
                continue;
            }

            if (sscanf(value, "%f", &parsed) != 1 || !isfinite(parsed))
                continue;
            if (!SDL_strcasecmp(key, "scale"))
            {
                if (parsed > 0.001f && parsed < 100.0f)
                    temp.modelScale = parsed;
            }
            else if (!SDL_strcasecmp(key, "offset_x"))
                temp.modelOffset[0] = parsed;
            else if (!SDL_strcasecmp(key, "offset_y"))
                temp.modelOffset[1] = parsed;
            else if (!SDL_strcasecmp(key, "offset_z"))
                temp.modelOffset[2] = parsed;
        }
        else if (!SDL_strcasecmp(section, "attachments") ||
                 !SDL_strcasecmp(section, "seat") ||
                 !SDL_strcasecmp(section, "exhaust"))
        {
            static const char *wheelNames[4] = {
                "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"
            };
            float parsed = 0.0f;

            if (kind == VEHICLE_PART_KART &&
                ((!SDL_strcasecmp(section, "attachments") &&
                  !SDL_strcasecmp(key, "exhaust_count")) ||
                 (!SDL_strcasecmp(section, "exhaust") &&
                  (!SDL_strcasecmp(key, "count") ||
                   !SDL_strcasecmp(key, "exhaust_count")))))
            {
                char *end = NULL;
                long count = strtol(value, &end, 10);

                if (end != value && *VehiclePart_Trim(end) == 0 &&
                    count >= 0 && count <= VEHICLE_PART_MAX_EXHAUSTS)
                {
                    exhaustCountExplicit = (int)count;
                    temp.exhaustCount = (u8)count;
                    temp.hasExhaustConfig = 1;
                }
                continue;
            }

            if (sscanf(value, "%f", &parsed) != 1 || !isfinite(parsed))
                continue;

            if (!SDL_strcasecmp(key, "seat_x") ||
                (!SDL_strcasecmp(section, "seat") && !SDL_strcasecmp(key, "x")))
            {
                temp.seatOffset[0] = parsed;
                seatComponents |= 1;
                continue;
            }
            if (!SDL_strcasecmp(key, "seat_y") ||
                (!SDL_strcasecmp(section, "seat") && !SDL_strcasecmp(key, "y")))
            {
                temp.seatOffset[1] = parsed;
                seatComponents |= 2;
                continue;
            }
            if (!SDL_strcasecmp(key, "seat_z") ||
                (!SDL_strcasecmp(section, "seat") && !SDL_strcasecmp(key, "z")))
            {
                temp.seatOffset[2] = parsed;
                seatComponents |= 4;
                continue;
            }

            for (int wheel = 0; wheel < 4; wheel++)
            {
                char expected[32];

                snprintf(expected, sizeof(expected), "%s_x", wheelNames[wheel]);
                if (!SDL_strcasecmp(key, expected))
                {
                    temp.wheelMount[wheel][0] = parsed;
                    wheelComponents[wheel] |= 1;
                    break;
                }

                snprintf(expected, sizeof(expected), "%s_y", wheelNames[wheel]);
                if (!SDL_strcasecmp(key, expected))
                {
                    temp.wheelMount[wheel][1] = parsed;
                    wheelComponents[wheel] |= 2;
                    break;
                }

                snprintf(expected, sizeof(expected), "%s_z", wheelNames[wheel]);
                if (!SDL_strcasecmp(key, expected))
                {
                    temp.wheelMount[wheel][2] = parsed;
                    wheelComponents[wheel] |= 4;
                    break;
                }
            }

            if (kind == VEHICLE_PART_KART)
            {
                for (int exhaust = 0;
                     exhaust < VEHICLE_PART_MAX_EXHAUSTS;
                     exhaust++)
                {
                    char expected[64];

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_x", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustMount[exhaust][0] = parsed;
                        exhaustComponents[exhaust] |= 1;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_y", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustMount[exhaust][1] = parsed;
                        exhaustComponents[exhaust] |= 2;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_z", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustMount[exhaust][2] = parsed;
                        exhaustComponents[exhaust] |= 4;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_scale", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        /* Clamp extreme scales so malformed mods cannot
                         * overflow native s16 transforms/particle sizes. */
                        if (parsed >= 0.1f && parsed <= 8.0f)
                            temp.exhaustScale[exhaust] = parsed;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_rotation_x", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustRotation[exhaust][0] = parsed;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_rotation_y", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustRotation[exhaust][1] = parsed;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }

                    snprintf(expected, sizeof(expected),
                             "exhaust_%d_rotation_z", exhaust);
                    if (!SDL_strcasecmp(key, expected))
                    {
                        temp.exhaustRotation[exhaust][2] = parsed;
                        temp.hasExhaustConfig = 1;
                        if (exhaust > highestExhaustIndex)
                            highestExhaustIndex = exhaust;
                        break;
                    }
                }
            }
        }
        else if (kind == VEHICLE_PART_KART &&
                 (!SDL_strcasecmp(section, "audio") ||
                  !SDL_strcasecmp(section, "engine_audio")))
        {
            /* Filenames and numeric controls share [audio]. Do not run the
             * path-only validation on percentages or float pitch options. */
            /* Audio layer keys are intercepted before the general
             * path/number parser, preventing numeric settings from being
             * mistaken for filenames. Unknown mode keeps RPM default. */
            int handledLayer = 0;
            for (int layer=0; layer<VEHICLE_PART_AUDIO_LAYER_COUNT; ++layer) {
                char prefix[32];
                snprintf(prefix,sizeof(prefix),"audio_layer_%d_",layer+1);
                size_t prefixLen = strlen(prefix);
                if (SDL_strncasecmp(key,prefix,prefixLen) != 0) continue;
                const char *opt = key + prefixLen;
                float number=0.0f;
                char trailer;
                int intVal;
                if (!SDL_strcasecmp(opt,"loop")) {
                    if (VehiclePart_AudioFileSafe(value))
                        snprintf(temp.audioLayerLoop[layer],
                                 sizeof(temp.audioLayerLoop[layer]),"%s",value);
                } else if (!SDL_strcasecmp(opt,"mode")) {
                    if (!SDL_strcasecmp(value,"rpm") || !SDL_strcasecmp(value,"engine"))
                        temp.audioLayerMode[layer]=VEHICLE_AUDIO_MODE_RPM;
                    else if (!SDL_strcasecmp(value,"speed") || !SDL_strcasecmp(value,"transmission"))
                        temp.audioLayerMode[layer]=VEHICLE_AUDIO_MODE_SPEED;
                    else if (!SDL_strcasecmp(value,"accel") || !SDL_strcasecmp(value,"acceleration"))
                        temp.audioLayerMode[layer]=VEHICLE_AUDIO_MODE_ACCEL;
                    else if (!SDL_strcasecmp(value,"drift"))
                        temp.audioLayerMode[layer]=VEHICLE_AUDIO_MODE_DRIFT;
                    else if (!SDL_strcasecmp(value,"turbo"))
                        temp.audioLayerMode[layer]=VEHICLE_AUDIO_MODE_TURBO;
                } else if (!SDL_strcasecmp(opt,"volume")) {
                    if (sscanf(value,"%f %c",&number,&trailer)==1 &&
                        isfinite(number) && number>=0.0f && number<=100.0f)
                        temp.audioLayerVolume[layer]=number;
                } else if (!SDL_strcasecmp(opt,"pitch_min")) {
                    if (sscanf(value,"%f %c",&number,&trailer)==1 &&
                        isfinite(number) && number>=0.1f && number<=8.0f)
                        temp.audioLayerPitchMin[layer]=number;
                } else if (!SDL_strcasecmp(opt,"pitch_max")) {
                    if (sscanf(value,"%f %c",&number,&trailer)==1 &&
                        isfinite(number) && number>=0.1f && number<=8.0f)
                        temp.audioLayerPitchMax[layer]=number;
                } else if (!SDL_strcasecmp(opt,"speed_max")) {
                    if (sscanf(value,"%d %c",&intVal,&trailer)==1 &&
                        (intVal==0 || (intVal>=1000 && intVal<=32767)))
                        temp.audioLayerSpeedMax[layer]=intVal;
                }
                handledLayer=1;
                break;
            }
            if (handledLayer) continue;
            if (!SDL_strcasecmp(key, "engine_pitch_mode")) {
                if (!SDL_strcasecmp(value,"rpm") || !SDL_strcasecmp(value,"howl"))
                    temp.enginePitchMode=1;
                else if (!SDL_strcasecmp(value,"speed"))
                    temp.enginePitchMode=0;
            }
            else if (!SDL_strcasecmp(key, "engine_pitch_min"))
            {
                float number;
                char trailer;
                if (sscanf(value, "%f %c", &number, &trailer) == 1 &&
                    isfinite(number) && number >= 0.1f && number <= 8.0f) {
                    temp.enginePitchMin = number;
                    enginePitchMinSeen = 1;
                }
            }
            else if (!SDL_strcasecmp(key, "engine_pitch_max"))
            {
                float number;
                char trailer;
                if (sscanf(value, "%f %c", &number, &trailer) == 1 &&
                    isfinite(number) && number >= 0.1f && number <= 8.0f) {
                    temp.enginePitchMax = number;
                    enginePitchMaxSeen = 1;
                }
            }
            else if (!SDL_strcasecmp(key, "engine_speed_max"))
            {
                int number;
                char trailer;
                /* 0 = automatic max-USF reference from driver physics. */
                if (sscanf(value, "%d %c", &number, &trailer) == 1 &&
                    (number == 0 || (number >= 1000 && number <= 32767)))
                    temp.enginePitchSpeedMax = number;
            }
            else if (!SDL_strcasecmp(key, "secondary_engine_volume") ||
                     !SDL_strcasecmp(key, "transmission_volume"))
            {
                float number;
                if (sscanf(value, "%f", &number) == 1 && isfinite(number) &&
                    number >= 0.0f && number <= 100.0f)
                    temp.secondaryEngineVolume = number;
            }
            else if (!SDL_strcasecmp(key, "secondary_engine_pitch_min") ||
                     !SDL_strcasecmp(key, "transmission_pitch_min"))
            {
                float number;
                if (sscanf(value, "%f", &number) == 1 && isfinite(number) &&
                    number >= 0.25f && number <= 6.0f)
                    temp.secondaryEnginePitchMin = number;
            }
            else if (!SDL_strcasecmp(key, "secondary_engine_pitch_max") ||
                     !SDL_strcasecmp(key, "transmission_pitch_max"))
            {
                float number;
                if (sscanf(value, "%f", &number) == 1 && isfinite(number) &&
                    number >= 0.25f && number <= 6.0f)
                    temp.secondaryEnginePitchMax = number;
            }
            else if (!SDL_strcasecmp(key, "secondary_engine_speed_max") ||
                     !SDL_strcasecmp(key, "transmission_speed_max"))
            {
                int number;
                char trailer;
                if (sscanf(value, "%d %c", &number, &trailer) == 1 &&
                    number >= 1000 && number <= 32767)
                    temp.secondaryEngineSpeedMax = number;
            }
            else if (VehiclePart_AudioFileSafe(value))
            {
                if (!SDL_strcasecmp(key, "engine_loop"))
                    snprintf(temp.engineLoop, sizeof(temp.engineLoop), "%s", value);
                else if (!SDL_strcasecmp(key, "engine_idle"))
                    snprintf(temp.engineIdle, sizeof(temp.engineIdle), "%s", value);
                else if (!SDL_strcasecmp(key, "engine_turbo"))
                    snprintf(temp.engineTurbo, sizeof(temp.engineTurbo), "%s", value);
                else if (!SDL_strcasecmp(key, "secondary_engine_loop") ||
                         !SDL_strcasecmp(key, "transmission_loop"))
                    snprintf(temp.secondaryEngineLoop,
                             sizeof(temp.secondaryEngineLoop), "%s", value);
            }
        }
        else if (kind == VEHICLE_PART_KART &&
                 !SDL_strcasecmp(section, "colors"))
        {
            /* Entries are presented in kart.ini order. One OBJ; each entry
             * names only a texture atlas in the current kart's folder. */
            char colorRelative[512], colorPath[1024];
            struct stat colorInfo;
            int index = (int)temp.colorCount;
            if (index >= VEHICLE_PART_MAX_COLORS ||
                !*key || !*value ||
                strlen(key) >= sizeof(temp.colorNames[0]) ||
                strlen(value) >= sizeof(temp.colorTextureNames[0]) ||
                strchr(value, '/') || strchr(value, '\\') ||
                strstr(value, ".."))
                continue;
            if (temp.unifiedMod)
                snprintf(colorRelative, sizeof(colorRelative),
                         "mods/%s/%s", folderName, value);
            else
                snprintf(colorRelative, sizeof(colorRelative),
                         "mods/karts/%s/%s", folderName, value);
            if (!NativeAssets_BuildPath(colorRelative,
                                        colorPath, sizeof(colorPath)) ||
                stat(colorPath, &colorInfo) != 0 ||
                !S_ISREG(colorInfo.st_mode))
                continue;
            snprintf(temp.colorNames[index], sizeof(temp.colorNames[0]),
                     "%s", key);
            snprintf(temp.colorTextureNames[index],
                     sizeof(temp.colorTextureNames[0]), "%s", value);
            temp.colorCount++;
        }
        else if (!SDL_strcasecmp(section, "assets"))
        {
            if (!SDL_strcasecmp(key, "asset") ||
                !SDL_strcasecmp(key, "asset_name"))
            {
                if (strlen(value) >= sizeof(temp.assetName))
                    valid = 0;
                else
                    snprintf(temp.assetName, sizeof(temp.assetName), "%s", value);
            }
        }
    }

    fclose(file);

    temp.hasSeatOffset = seatComponents == 7;
    for (int wheel = 0; wheel < 4; wheel++)
    {
        if (wheelComponents[wheel] == 7)
            temp.wheelMountMask |= (u8)(1U << wheel);
    }

    for (int exhaust = 0;
         exhaust < VEHICLE_PART_MAX_EXHAUSTS;
         exhaust++)
    {
        if (exhaustComponents[exhaust] == 7)
            temp.exhaustMountValid[exhaust] = 1;
    }

    if (temp.hasExhaustConfig && exhaustCountExplicit < 0)
    {
        temp.exhaustCount =
            highestExhaustIndex >= 0 ?
            (u8)(highestExhaustIndex + 1) : 0;
    }

    for (int layer=0; layer<VEHICLE_PART_AUDIO_LAYER_COUNT; ++layer) {
        if (temp.audioLayerLoop[layer][0]) {
            if (temp.audioLayerPitchMax[layer] < temp.audioLayerPitchMin[layer]) {
                Platform_Log("[KART AUDIO LAYER] kart=%s layer=%d invalid pitch range, disabled\n",
                             folderName,layer+1);
                temp.audioLayerLoop[layer][0]=0;
            } else {
                Platform_Log("[KART AUDIO LAYER INI] kart=%s layer=%d mode=%d vol=%.1f pitch=%.2f..%.2f\n",
                             folderName,layer+1,temp.audioLayerMode[layer],
                             temp.audioLayerVolume[layer],temp.audioLayerPitchMin[layer],
                             temp.audioLayerPitchMax[layer]);
            }
        }
    }
    if (kind == VEHICLE_PART_KART && temp.engineLoop[0] &&
        enginePitchMinSeen && enginePitchMaxSeen &&
        temp.enginePitchMax >= temp.enginePitchMin) {
        temp.customEnginePitchCurve = 1;
        Platform_Log("[ENGINE CURVE INI] kart=%s idle=%.3fx usf=%.3fx speedMax=%d (0=auto)\n",
                     folderName, temp.enginePitchMin, temp.enginePitchMax,
                     temp.enginePitchSpeedMax);
    } else if (enginePitchMinSeen || enginePitchMaxSeen) {
        Platform_Log("[ENGINE CURVE INI] kart=%s ignored incomplete/invalid endpoints\n",
                     folderName);
    }

    if (!enabled || !valid)
        return;
    if (!VehiclePart_Reserve(kind, list->count + 1))
        return;

    temp.id = (s16)list->count;
    list->items[list->count++] = temp;

    Platform_Log("[VEHICLE PART] registered %s id=%d slug=%s name=%s\n",
                 kind == VEHICLE_PART_KART ? "kart" : "wheels",
                 temp.id, temp.slug, temp.displayName);
}

static void VehiclePart_ScanKind(int kind)
{
    struct VehiclePartList *list = &s_vehicleParts[kind];
    char relative[128];
    char root[1024];
    DIR *dir;
    struct dirent *entry;

    snprintf(relative, sizeof(relative), "mods/%s",
             VehiclePart_DirectoryName(kind));

    if (NativeAssets_BuildPath(relative, root, sizeof(root)) &&
        VehiclePart_IsDirectory(root) &&
        (dir = opendir(root)) != NULL)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            char child[1200];
            if (!strcmp(entry->d_name, ".") ||
                !strcmp(entry->d_name, ".."))
                continue;
            snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
            if (VehiclePart_IsDirectory(child))
                VehiclePart_LoadOne(kind, entry->d_name, 0);
        }
        closedir(dir);
    }

    /* New unified packages live at mods/<web-id>/<kart.ini|wheels.ini>.
       Type is validated in [forge_mod], not inferred from a random asset. */
    if (NativeAssets_BuildPath("mods", root, sizeof(root)) &&
        VehiclePart_IsDirectory(root))
    {
        dir = opendir(root);
        if (dir != NULL)
        {
            while ((entry = readdir(dir)) != NULL)
            {
                char child[1200];
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                    continue;
                snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
                if (VehiclePart_IsDirectory(child) &&
                    NativeModPackage_IsValid(entry->d_name,
                        kind == VEHICLE_PART_KART ? "Kart" : "Wheels"))
                    VehiclePart_LoadOne(kind, entry->d_name, 1);
            }
            closedir(dir);
        }
    }

    if (list->count > 1)
        qsort(list->items, (size_t)list->count,
              sizeof(*list->items), VehiclePart_Compare);

    for (int i = 0; i < list->count; i++)
        list->items[i].id = (s16)i;
}

void VehiclePartRegistry_Init(void)
{
    if (s_vehiclePartsInitialized)
        return;

    s_vehiclePartsInitialized = 1;
#if defined(CTR_NATIVE)
    if (!NativeSession_NoModsActive())
#endif
    {
        VehiclePart_ScanKind(VEHICLE_PART_KART);
        VehiclePart_ScanKind(VEHICLE_PART_WHEELS);
    }

    Platform_Log("[VEHICLE PART] %d karts, %d wheel sets\n",
                 s_vehicleParts[VEHICLE_PART_KART].count,
                 s_vehicleParts[VEHICLE_PART_WHEELS].count);
}

void VehiclePartRegistry_Shutdown(void)
{
    EngineSoundRegistry_Shutdown();
    for (int kind = 0; kind < VEHICLE_PART_KIND_COUNT; kind++)
    {
        free(s_vehicleParts[kind].items);
        memset(&s_vehicleParts[kind], 0, sizeof(s_vehicleParts[kind]));
    }

    s_vehiclePartsInitialized = 0;
}

int VehiclePartRegistry_GetCount(int kind)
{
    VehiclePartRegistry_Init();

    if (kind < 0 || kind >= VEHICLE_PART_KIND_COUNT)
        return 0;

    return s_vehicleParts[kind].count;
}

const struct VehiclePartDef *VehiclePartRegistry_GetByID(int kind, int id)
{
    VehiclePartRegistry_Init();

    if (kind < 0 || kind >= VEHICLE_PART_KIND_COUNT ||
        id < 0 || id >= s_vehicleParts[kind].count)
        return NULL;
    return &s_vehicleParts[kind].items[id];
}

const struct VehiclePartDef *VehiclePartRegistry_GetByIndex(int kind, int index)
{
    return VehiclePartRegistry_GetByID(kind, index);
}

const char *VehiclePartRegistry_GetDisplayName(int kind, int id)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(kind, id);

    return part ? part->displayName : "ORIGINAL";
}

const char *VehiclePartRegistry_GetAssetName(int kind, int id)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(kind, id);

    return part ? part->assetName : NULL;
}

int VehiclePartRegistry_GetIDBySlug(int kind, const char *slug)
{
    VehiclePartRegistry_Init();

    if (kind < 0 || kind >= VEHICLE_PART_KIND_COUNT ||
        slug == NULL || !*slug)
        return -1;

    for (int i = 0; i < s_vehicleParts[kind].count; ++i)
        if (!SDL_strcasecmp(s_vehicleParts[kind].items[i].slug, slug))
            return s_vehicleParts[kind].items[i].id;

    return -1;
}

int VehiclePartRegistry_GetColorCount(int kartID)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
    return part ? part->colorCount : 0;
}

const char *VehiclePartRegistry_GetColorName(int kartID, int colorIndex)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
    if (!part || colorIndex < 0 || colorIndex >= part->colorCount)
        return "DEFAULT";
    return part->colorNames[colorIndex];
}

const char *VehiclePartRegistry_GetColorTexture(int kartID, int colorIndex)
{
    const struct VehiclePartDef *part =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
    if (!part || colorIndex < 0 || colorIndex >= part->colorCount)
        return NULL;
    return part->colorTextureNames[colorIndex];
}

int VehiclePartRegistry_GetOriginalReplacementKartID(void)
{
    VehiclePartRegistry_Init();

    for (int i = 0; i < s_vehicleParts[VEHICLE_PART_KART].count; ++i)
        if (s_vehicleParts[VEHICLE_PART_KART].items[i].replacesOriginal)
            return s_vehicleParts[VEHICLE_PART_KART].items[i].id;

    return -1;
}

b32 VehiclePartRegistry_HidesWheels(int kartID)
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
    return kart != NULL && kart->hideWheels != 0;
}

b32 VehiclePartRegistry_GetSeatOffset(int kartID, float out[3])
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);

    if (out == NULL || kart == NULL || !kart->hasSeatOffset)
        return false;

    out[0] = kart->seatOffset[0];
    out[1] = kart->seatOffset[1];
    out[2] = kart->seatOffset[2];
    return true;
}

b32 VehiclePartRegistry_GetWheelMount(int kartID, int wheelIndex, float out[3])
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);

    if (out == NULL || kart == NULL ||
        wheelIndex < 0 || wheelIndex >= 4 ||
        (kart->wheelMountMask & (1U << wheelIndex)) == 0)
        return false;

    out[0] = kart->wheelMount[wheelIndex][0];
    out[1] = kart->wheelMount[wheelIndex][1];
    out[2] = kart->wheelMount[wheelIndex][2];
    return true;
}

int VehiclePartRegistry_GetExhaustCount(int kartID)
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);

    /* -1 is reserved for *no custom kart* (retail/vanilla fallback).
     * A real custom kart without explicit exhaust attachments has ZERO
     * exhaust outlets, not a reason to resurrect two vanilla pipes. */
    if (kart == NULL)
        return -1;

    return kart->hasExhaustConfig ? (int)kart->exhaustCount : 0;
}

float VehiclePartRegistry_GetExhaustScale(int kartID, int exhaustIndex)
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);
    if (kart == NULL || exhaustIndex < 0 ||
        exhaustIndex >= (int)kart->exhaustCount ||
        exhaustIndex >= VEHICLE_PART_MAX_EXHAUSTS)
        return 1.0f;
    return kart->exhaustScale[exhaustIndex];
}

b32 VehiclePartRegistry_GetExhaustMount(
    int kartID,
    int exhaustIndex,
    float outPosition[3],
    float outRotation[3])
{
    const struct VehiclePartDef *kart =
        VehiclePartRegistry_GetByID(VEHICLE_PART_KART, kartID);

    if (kart == NULL ||
        exhaustIndex < 0 ||
        exhaustIndex >= VEHICLE_PART_MAX_EXHAUSTS ||
        exhaustIndex >= (int)kart->exhaustCount ||
        !kart->exhaustMountValid[exhaustIndex])
        return false;

    if (outPosition != NULL)
    {
        outPosition[0] = kart->exhaustMount[exhaustIndex][0];
        outPosition[1] = kart->exhaustMount[exhaustIndex][1];
        outPosition[2] = kart->exhaustMount[exhaustIndex][2];
    }

    if (outRotation != NULL)
    {
        outRotation[0] = kart->exhaustRotation[exhaustIndex][0];
        outRotation[1] = kart->exhaustRotation[exhaustIndex][1];
        outRotation[2] = kart->exhaustRotation[exhaustIndex][2];
    }

    return true;
}
