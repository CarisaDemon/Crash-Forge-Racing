#include <common.h>
#include <NativeCustomSampleRegistry.h>
#include <platform/native_assets.h>

#if defined(CTR_NATIVE)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NativeCustomSampleEntry
{
	short *samples;
	int sampleCount;
	int sampleRate;
	int loopStart;
	int loopEnd;
	int loopEnabled;
};

static struct NativeCustomSampleEntry s_customSamples[NATIVE_CUSTOM_SAMPLE_MAX];
static int s_customSampleCount;

static unsigned int NativeCustomSample_ReadU32(const unsigned char *p)
{
	return
		(unsigned int)p[0] |
		((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) |
		((unsigned int)p[3] << 24);
}

static unsigned int NativeCustomSample_ReadU16(const unsigned char *p)
{
	return
		(unsigned int)p[0] |
		((unsigned int)p[1] << 8);
}

void NativeCustomSampleRegistry_Clear(void)
{
	int i;
	for (i = 0; i < NATIVE_CUSTOM_SAMPLE_MAX; i++)
	{
		free(s_customSamples[i].samples);
		memset(&s_customSamples[i], 0, sizeof(s_customSamples[i]));
	}
	s_customSampleCount = 0;
}

static int NativeCustomSample_ParseWav(
	const unsigned char *data,
	int size,
	struct NativeCustomSampleEntry *out)
{
	int pos;
	int formatTag = 0;
	int channels = 0;
	int sampleRate = 0;
	int bitsPerSample = 0;
	const unsigned char *pcmBytes = NULL;
	int pcmByteCount = 0;
	int loopStart = 0;
	int loopEnd = 0;
	int loopEnabled = 0;

	if (data == NULL || out == NULL || size < 12)
		return 0;
	if (memcmp(data, "RIFF", 4) != 0 || memcmp(&data[8], "WAVE", 4) != 0)
		return 0;

	for (pos = 12; pos + 8 <= size;)
	{
		const unsigned char *id = &data[pos];
		unsigned int chunkSize = NativeCustomSample_ReadU32(&data[pos + 4]);
		int payload = pos + 8;
		int next;

		if (chunkSize > (unsigned int)(size - payload))
			return 0;
		next = payload + (int)chunkSize + ((int)chunkSize & 1);

		if (memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16)
		{
			formatTag = (int)NativeCustomSample_ReadU16(&data[payload]);
			channels = (int)NativeCustomSample_ReadU16(&data[payload + 2]);
			sampleRate = (int)NativeCustomSample_ReadU32(&data[payload + 4]);
			bitsPerSample = (int)NativeCustomSample_ReadU16(&data[payload + 14]);
		}
		else if (memcmp(id, "data", 4) == 0)
		{
			pcmBytes = &data[payload];
			pcmByteCount = (int)chunkSize;
		}
		else if (memcmp(id, "smpl", 4) == 0 && chunkSize >= 60)
		{
			unsigned int numLoops = NativeCustomSample_ReadU32(&data[payload + 28]);
			if (numLoops > 0)
			{
				unsigned int start = NativeCustomSample_ReadU32(&data[payload + 44]);
				unsigned int endInclusive = NativeCustomSample_ReadU32(&data[payload + 48]);
				if (endInclusive >= start && endInclusive < 0x7fffffffu)
				{
					loopStart = (int)start;
					loopEnd = (int)endInclusive + 1;
					loopEnabled = 1;
				}
			}
		}

		pos = next;
	}

	if (formatTag != 1 ||
	    channels != 1 ||
	    bitsPerSample != 16 ||
	    sampleRate <= 0 ||
	    pcmBytes == NULL ||
	    pcmByteCount < 2 ||
	    (pcmByteCount & 1) != 0)
	{
		return 0;
	}

	out->sampleCount = pcmByteCount / 2;
	out->samples = (short *)malloc((size_t)out->sampleCount * sizeof(short));
	if (out->samples == NULL)
		return 0;

	for (pos = 0; pos < out->sampleCount; pos++)
	{
		unsigned int value = NativeCustomSample_ReadU16(&pcmBytes[pos * 2]);
		out->samples[pos] = (short)(value & 0xffffu);
	}

	out->sampleRate = sampleRate;
	if (loopEnabled &&
	    loopStart >= 0 &&
	    loopEnd > loopStart &&
	    loopEnd <= out->sampleCount)
	{
		out->loopStart = loopStart;
		out->loopEnd = loopEnd;
		out->loopEnabled = 1;
	}
	else
	{
		out->loopStart = 0;
		out->loopEnd = out->sampleCount;
		out->loopEnabled = 0;
	}

	return 1;
}

int NativeCustomSampleRegistry_LoadDirectory(const char *relativeDir,
	                                      int expectedCount)
{
	int index;

	NativeCustomSampleRegistry_Clear();

	if (expectedCount <= 0)
		return 1;
	if (relativeDir == NULL || relativeDir[0] == '\0' ||
	    strstr(relativeDir,"..") != NULL ||
	    expectedCount > NATIVE_CUSTOM_SAMPLE_MAX)
		return 0;

	for (index = 0; index < expectedCount; index++)
	{
		char relative[1024];
		struct NativeAssetsByteBuffer bytes;
		struct NativeCustomSampleEntry entry;

		memset(&bytes, 0, sizeof(bytes));
		memset(&entry, 0, sizeof(entry));
		snprintf(
			relative,
			sizeof(relative),
			"%s/samples/sample_%03d.wav",
			relativeDir,
			index);

		if (!NativeAssets_ReadBytes(
		        relative,
		        NATIVE_ASSET_READ_DATA_FILE,
		        &bytes))
		{
			Platform_Log("[CSEQ] custom sample missing: %s\n", relative);
			NativeCustomSampleRegistry_Clear();
			return 0;
		}

		if (!NativeCustomSample_ParseWav(bytes.data, bytes.size, &entry))
		{
			Platform_Log(
				"[CSEQ] invalid custom sample WAV (PCM16 mono required): %s\n",
				relative);
			NativeAssets_FreeBytes(&bytes);
			NativeCustomSampleRegistry_Clear();
			return 0;
		}

		NativeAssets_FreeBytes(&bytes);
		s_customSamples[index] = entry;
		s_customSampleCount = index + 1;
	}

	Platform_Log(
		"[CSEQ] loaded %d custom PCM sample%s for '%s'\n",
		s_customSampleCount,
		s_customSampleCount == 1 ? "" : "s",
		relativeDir);
	return 1;
}

int NativeCustomSampleRegistry_LoadPackage(const char *slug, int count)
{
	char relative[256];
	if(slug==NULL || strchr(slug,'/') || strchr(slug,'\\'))
		return 0;
	if(snprintf(relative,sizeof(relative),"mods/music/%s",slug)
	   >=(int)sizeof(relative))return 0;
	return NativeCustomSampleRegistry_LoadDirectory(relative,count);
}

int NativeCustomSampleRegistry_GetCount(void)
{
	return s_customSampleCount;
}

int NativeCustomSampleRegistry_GetView(
	int index,
	struct NativeCustomSampleView *out)
{
	const struct NativeCustomSampleEntry *entry;
	if (out == NULL || index < 0 || index >= s_customSampleCount)
		return 0;

	entry = &s_customSamples[index];
	if (entry->samples == NULL || entry->sampleCount <= 0)
		return 0;

	out->samples = entry->samples;
	out->sampleCount = entry->sampleCount;
	out->sampleRate = entry->sampleRate;
	out->loopStart = entry->loopStart;
	out->loopEnd = entry->loopEnd;
	out->loopEnabled = entry->loopEnabled;
	return 1;
}

unsigned int NativeCustomSampleRegistry_EncodeHandle(int index)
{
	if (index < 0 || index >= NATIVE_CUSTOM_SAMPLE_MAX)
		return 0;
	return NATIVE_CUSTOM_SAMPLE_HANDLE_TAG | (unsigned int)(index & 0xffff);
}

int NativeCustomSampleRegistry_IsHandle(unsigned int handle)
{
	return
		(handle & NATIVE_CUSTOM_SAMPLE_HANDLE_MASK) ==
		NATIVE_CUSTOM_SAMPLE_HANDLE_TAG;
}

int NativeCustomSampleRegistry_DecodeHandle(unsigned int handle)
{
	if (!NativeCustomSampleRegistry_IsHandle(handle))
		return -1;
	return (int)(handle & 0xffffu);
}

#else

int NativeCustomSampleRegistry_LoadPackage(const char *slug, int expectedCount)
{
	(void)slug;
	(void)expectedCount;
	return 0;
}
void NativeCustomSampleRegistry_Clear(void) {}
int NativeCustomSampleRegistry_GetCount(void) { return 0; }
int NativeCustomSampleRegistry_GetView(int index, struct NativeCustomSampleView *out)
{
	(void)index;
	(void)out;
	return 0;
}
unsigned int NativeCustomSampleRegistry_EncodeHandle(int index)
{
	(void)index;
	return 0;
}
int NativeCustomSampleRegistry_IsHandle(unsigned int handle)
{
	(void)handle;
	return 0;
}
int NativeCustomSampleRegistry_DecodeHandle(unsigned int handle)
{
	(void)handle;
	return -1;
}

#endif
