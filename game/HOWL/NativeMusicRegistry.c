#include <common.h>
#include <NativeMusicRegistry.h>
#include <NativeCustomSampleRegistry.h>
#include <platform/native_assets.h>
#include <platform/native_mod_packages.h>
#include <platform/native_custom_tracks.h>

#if defined(CTR_NATIVE)

#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NATIVE_MUSIC_PATH_MAX 1024
#define NATIVE_MUSIC_SLUG_MAX 64

static unsigned char *s_externalCseq;
static int s_externalCseqSize;
static int s_externalLevel = -1;
static char s_externalSlug[NATIVE_MUSIC_SLUG_MAX];

static char *NativeMusic_Trim(char *text)
{
	char *end;

	/* Accept UTF-8 files written with a BOM. Without this, the first
	 * section header is seen as "\xEF\xBB\xBF[music]" and the whole
	 * package is silently ignored. */
	if ((unsigned char)text[0] == 0xEF &&
	    (unsigned char)text[1] == 0xBB &&
	    (unsigned char)text[2] == 0xBF)
	{
		text += 3;
	}

	while (*text && isspace((unsigned char)*text))
		text++;

	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	return text;
}

static int NativeMusic_IsDirectory(const char *path)
{
	struct stat st;
	return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int NativeMusic_IsSafeLeaf(const char *text)
{
	const unsigned char *p;

	if (text == NULL || text[0] == '\0' || strlen(text) >= 256)
		return 0;

	if (!strcmp(text, ".") || !strcmp(text, ".."))
		return 0;

	for (p = (const unsigned char *)text; *p; p++)
	{
		if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
			return 0;
	}
	return 1;
}

static int NativeMusic_ParseBool(const char *value, int fallback)
{
	if (value == NULL || value[0] == '\0')
		return fallback;
	if (!SDL_strcasecmp(value, "true") ||
	    !SDL_strcasecmp(value, "yes") ||
	    !strcmp(value, "1"))
		return 1;
	if (!SDL_strcasecmp(value, "false") ||
	    !SDL_strcasecmp(value, "no") ||
	    !strcmp(value, "0"))
		return 0;
	return fallback;
}

static int NativeMusic_ParseTargetLevel(const char *value)
{
	char *end = NULL;
	long level;

	if (value == NULL)
		return -1;

	while (*value && isspace((unsigned char)*value))
		value++;

	if (!SDL_strncasecmp(value, "level_", 6))
		value += 6;

	level = strtol(value, &end, 10);
	if (end == value)
		return -1;

	while (*end && isspace((unsigned char)*end))
		end++;

	if (*end != '\0' || level < 0 || level > 255)
		return -1;

	return (int)level;
}

static int NativeMusic_ValidateCseq(
	const unsigned char *data,
	int size)
{
	const struct CseqHeader *header;
	int metadataMin;

	if (data == NULL || size < (int)sizeof(struct CseqHeader))
		return 0;

	header = (const struct CseqHeader *)data;
	if (header->songSize < (int)sizeof(struct CseqHeader) ||
	    header->songSize > size ||
	    header->numSongs == 0)
		return 0;

	metadataMin =
		(int)sizeof(struct CseqHeader) +
		(int)header->numLongSamples * (int)sizeof(struct SampleInstrument) +
		(int)header->numShortSamples * (int)sizeof(struct SampleDrums) +
		(int)header->numSongs * (int)sizeof(s16);

	if (metadataMin > header->songSize)
		return 0;

	return 1;
}

static int NativeMusic_ValidateCustomSampleRefs(const unsigned char *data)
{
	const struct CseqHeader *header = (const struct CseqHeader *)data;
	const struct SampleInstrument *longSamples;
	const struct SampleDrums *shortSamples;
	int customCount = NativeCustomSampleRegistry_GetCount();
	int i;

	longSamples = (const struct SampleInstrument *)(data + sizeof(struct CseqHeader));
	shortSamples = (const struct SampleDrums *)(
		(const unsigned char *)longSamples +
		(int)header->numLongSamples * (int)sizeof(struct SampleInstrument));

	for (i = 0; i < header->numLongSamples; i++)
	{
		int spuIndex = (int)longSamples[i].spuIndex;
		if (spuIndex < 0)
		{
			int slot = -1 - spuIndex;
			if (slot < 0 || slot >= customCount)
				return 0;
		}
	}

	for (i = 0; i < header->numShortSamples; i++)
	{
		int spuIndex = (int)shortSamples[i].spuIndex;
		if (spuIndex < 0)
		{
			int slot = -1 - spuIndex;
			if (slot < 0 || slot >= customCount)
				return 0;
		}
	}

	return 1;
}

static int NativeMusic_LoadPackage(
	const char *folderName,
	int requestedLevel)
{
	char iniRelative[NATIVE_MUSIC_PATH_MAX];
	char iniPath[NATIVE_MUSIC_PATH_MAX];
	char trackRelative[NATIVE_MUSIC_PATH_MAX];
	char source[32] = "";
	char fileName[256] = "track.cseq";
	char target[64] = "";
	char line[1024];
	int enabled = 1;
	int customSamples = 0;
	int inMusicSection = 0;
	int targetLevel;
	FILE *file;
	struct NativeAssetsByteBuffer bytes;

	if (!NativeMusic_IsSafeLeaf(folderName))
		return 0;

	snprintf(
		iniRelative,
		sizeof(iniRelative),
		"mods/music/%s/music.ini",
		folderName);

	if (!NativeAssets_BuildPath(
	        iniRelative,
	        iniPath,
	        sizeof(iniPath)))
		return 0;

	file = fopen(iniPath, "rb");
	if (file == NULL)
		return 0;

	while (fgets(line, sizeof(line), file) != NULL)
	{
		char *key;
		char *value;
		char *equals;
		char *trimmed = NativeMusic_Trim(line);

		if (trimmed[0] == '\0' ||
		    trimmed[0] == '#' ||
		    trimmed[0] == ';')
			continue;

		if (trimmed[0] == '[')
		{
			inMusicSection =
				!SDL_strcasecmp(trimmed, "[music]");
			continue;
		}

		if (!inMusicSection)
			continue;

		equals = strchr(trimmed, '=');
		if (equals == NULL)
			continue;

		*equals = '\0';
		key = NativeMusic_Trim(trimmed);
		value = NativeMusic_Trim(equals + 1);

		if (!SDL_strcasecmp(key, "source"))
			snprintf(source, sizeof(source), "%s", value);
		else if (!SDL_strcasecmp(key, "file"))
			snprintf(fileName, sizeof(fileName), "%s", value);
		else if (!SDL_strcasecmp(key, "target"))
			snprintf(target, sizeof(target), "%s", value);
		else if (!SDL_strcasecmp(key, "enabled"))
			enabled = NativeMusic_ParseBool(value, enabled);
		else if (!SDL_strcasecmp(key, "custom_samples"))
		{
			long parsed = strtol(value, NULL, 10);
			if (parsed >= 0 && parsed <= NATIVE_CUSTOM_SAMPLE_MAX)
				customSamples = (int)parsed;
		}
	}

	fclose(file);

	if (!enabled ||
	    SDL_strcasecmp(source, "cseq") != 0 ||
	    !NativeMusic_IsSafeLeaf(fileName))
		return 0;

	targetLevel = NativeMusic_ParseTargetLevel(target);
	if (targetLevel != requestedLevel)
		return 0;

	snprintf(
		trackRelative,
		sizeof(trackRelative),
		"mods/music/%s/%s",
		folderName,
		fileName);

	if (!NativeAssets_ReadBytes(
	        trackRelative,
	        NATIVE_ASSET_READ_DATA_FILE,
	        &bytes))
	{
		Platform_Log(
			"[CSEQ] failed to read %s\n",
			trackRelative);
		return 0;
	}

	if (!NativeMusic_ValidateCseq(bytes.data, bytes.size))
	{
		Platform_Log(
			"[CSEQ] invalid external CSEQ: %s\n",
			trackRelative);
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	if (!NativeCustomSampleRegistry_LoadPackage(folderName, customSamples))
	{
		Platform_Log(
			"[CSEQ] failed to load custom sample pack for '%s'\n",
			folderName);
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	if (!NativeMusic_ValidateCustomSampleRefs(bytes.data))
	{
		Platform_Log(
			"[CSEQ] external CSEQ '%s' has invalid custom sample references\n",
			folderName);
		NativeCustomSampleRegistry_Clear();
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	free(s_externalCseq);
	s_externalCseq = bytes.data;
	s_externalCseqSize = bytes.size;
	s_externalLevel = requestedLevel;
	snprintf(
		s_externalSlug,
		sizeof(s_externalSlug),
		"%s",
		folderName);

	howl_ParseCseqHeader(
		(struct CseqHeader *)s_externalCseq);

	Platform_Log(
		"[CSEQ] external package '%s' active for level_%02d (%d bytes)\n",
		s_externalSlug,
		requestedLevel,
		s_externalCseqSize);
	return 1;
}

int NativeMusicRegistry_TryApplyCustomTrack(void)
{
    struct NativeAssetsByteBuffer bytes;
    const char *music;
    const char *directory;
    int key;
    if (!NativeCustomTracks_IsActiveRace() ||
        NativeCustomTracks_SelectedMusicKind()!=NATIVE_CUSTOM_TRACK_MUSIC_CSEQ)
        return 0;
    key=NativeCustomTracks_SelectedMusicKey();
    music=NativeCustomTracks_SelectedMusicPath();
    directory=NativeCustomTracks_SelectedDirectory();
    if (!music || !*music || !directory || !*directory) return 0;
    if (s_externalCseq && s_externalLevel==key)
    {
        howl_ParseCseqHeader((struct CseqHeader *)s_externalCseq);
        return 1;
    }
    /* A custom track owns its CSEQ. Do not reuse music loaded for Crash Cove
       or any other retail level. */
    NativeMusicRegistry_Shutdown();
    if(!NativeAssets_ReadBytes(music,NATIVE_ASSET_READ_DATA_FILE,&bytes))
    {
        Platform_Log("[CustomTracks] missing CSEQ: %s\n",music);
        return 0;
    }
    if (!NativeMusic_ValidateCseq(bytes.data,bytes.size) ||
        !NativeCustomSampleRegistry_LoadDirectory(
            directory,NativeCustomTracks_SelectedMusicSamples()) ||
        !NativeMusic_ValidateCustomSampleRefs(bytes.data))
    {
        Platform_Log("[CustomTracks] invalid CSEQ or custom sample data: %s\n",music);
        NativeCustomSampleRegistry_Clear();
        NativeAssets_FreeBytes(&bytes);
        return 0;
    }
    s_externalCseq=bytes.data;
    s_externalCseqSize=bytes.size;
    s_externalLevel=key;
    snprintf(s_externalSlug,sizeof(s_externalSlug),"%s",directory);
    howl_ParseCseqHeader((struct CseqHeader *)s_externalCseq);
    Platform_Log("[CustomTracks] CSEQ from mod: %s (%d bytes)\n",music,bytes.size);
    return 1;
}

int NativeMusicRegistry_TryApplyLevel(int levelID)
{
	char musicDir[NATIVE_MUSIC_PATH_MAX];
	DIR *dir;
	struct dirent *entry;

	if (s_externalCseq != NULL &&
	    s_externalLevel == levelID)
	{
		howl_ParseCseqHeader(
			(struct CseqHeader *)s_externalCseq);
		return 1;
	}

	if (s_externalCseq != NULL)
	{
		free(s_externalCseq);
		s_externalCseq = NULL;
		s_externalCseqSize = 0;
		s_externalLevel = -1;
		s_externalSlug[0] = '\0';
		NativeCustomSampleRegistry_Clear();
	}

	if (NativeAssets_BuildPath(
	        "mods/music", musicDir, sizeof(musicDir)) &&
	    NativeMusic_IsDirectory(musicDir) &&
	    (dir = opendir(musicDir)) != NULL)
	{
	while ((entry = readdir(dir)) != NULL)
	{
		char child[NATIVE_MUSIC_PATH_MAX];

		if (!strcmp(entry->d_name, ".") ||
		    !strcmp(entry->d_name, ".."))
			continue;

		snprintf(
			child,
			sizeof(child),
			"%s/%s",
			musicDir,
			entry->d_name);

		if (!NativeMusic_IsDirectory(child))
			continue;

		if (NativeMusic_LoadPackage(
		        entry->d_name,
		        levelID))
		{
			closedir(dir);
			return 1;
		}
	}

	closedir(dir);
	}

	/* Scan the same unified web-ID packages as character/kart/wheels.
	   Existing music directory packages keep priority for compatibility. */
	if (NativeAssets_BuildPath("mods", musicDir, sizeof(musicDir)) &&
	    NativeMusic_IsDirectory(musicDir) &&
	    (dir = opendir(musicDir)) != NULL)
	{
		while ((entry = readdir(dir)) != NULL)
		{
			char child[NATIVE_MUSIC_PATH_MAX];
			if (!strcmp(entry->d_name, ".") ||
			    !strcmp(entry->d_name, ".."))
				continue;
			snprintf(child, sizeof(child), "%s/%s", musicDir, entry->d_name);
			if (NativeMusic_IsDirectory(child) &&
			    NativeModPackage_IsValid(entry->d_name, "Music") &&
			    NativeMusic_LoadPackage(entry->d_name, levelID))
			{
				closedir(dir);
				return 1;
			}
		}
		closedir(dir);
	}
	Platform_Log(
		"[CSEQ] no external CSEQ target for level_%02d; retail CSEQ remains active\n",
		levelID);
	return 0;
}

int NativeMusicRegistry_IsExternalCseqActive(void)
{
	return s_externalCseq != NULL;
}

const char *NativeMusicRegistry_GetActiveSlug(void)
{
	return s_externalSlug;
}

void NativeMusicRegistry_Shutdown(void)
{
	free(s_externalCseq);
	s_externalCseq = NULL;
	s_externalCseqSize = 0;
	s_externalLevel = -1;
	s_externalSlug[0] = '\0';
	NativeCustomSampleRegistry_Clear();
}

#else

int NativeMusicRegistry_TryApplyCustomTrack(void)
{
    return 0;
}
int NativeMusicRegistry_TryApplyLevel(int levelID)
{
	(void)levelID;
	return 0;
}

int NativeMusicRegistry_IsExternalCseqActive(void)
{
	return 0;
}

const char *NativeMusicRegistry_GetActiveSlug(void)
{
	return "";
}

void NativeMusicRegistry_Shutdown(void)
{
}

#endif
