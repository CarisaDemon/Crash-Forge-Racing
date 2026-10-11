#include <common.h>
#include <NativeHowlSampleCatalog.h>
#include <platform/native_assets.h>

#if defined(CTR_NATIVE)

#define NATIVE_HOWL_KART_PATH "SOUNDS/KART.HWL"
#define NATIVE_HOWL_SECTOR_SIZE 2048
#define NATIVE_HOWL_HEADER_FIXED_SIZE 40
#define NATIVE_HOWL_MAX_SAMPLES 4096
#define NATIVE_HOWL_MAX_BANKS 1024

struct NativeHowlCatalogState
{
	int ready;
	u8 *hwlData;
	int hwlSize;
	struct NativeHowlSampleInfo *samples;
	int sampleCount;
};

static struct NativeHowlCatalogState s_nativeHowlCatalog;

static u16 NativeHowl_ReadLE16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static s16 NativeHowl_ReadLES16(const u8 *p)
{
	return (s16)NativeHowl_ReadLE16(p);
}

static u32 NativeHowl_ReadLE32(const u8 *p)
{
	return (u32)p[0] |
	       ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static int NativeHowl_RangeValid(int offset, int size, int total)
{
	if (offset < 0 || size < 0 || total < 0)
		return 0;
	if (offset > total)
		return 0;
	return size <= total - offset;
}

void NativeHowlSampleCatalog_Shutdown(void)
{
	free(s_nativeHowlCatalog.samples);
	free(s_nativeHowlCatalog.hwlData);
	memset(&s_nativeHowlCatalog, 0, sizeof(s_nativeHowlCatalog));
}

int NativeHowlSampleCatalog_Init(void)
{
	struct NativeAssetsByteBuffer bytes;
	u32 numSpuAddrs;
	u32 numOtherFX;
	u32 numEngineFX;
	u32 numBanks;
	u32 numSequences;
	int spuTableOffset;
	int bankTableOffset;
	u32 bankID;

	if (s_nativeHowlCatalog.ready)
		return 1;

	memset(&bytes, 0, sizeof(bytes));
	if (!NativeAssets_ReadBytes(
	        NATIVE_HOWL_KART_PATH,
	        NATIVE_ASSET_READ_DATA_FILE,
	        &bytes))
	{
		Platform_LogWarn("[NativeHOWL] unable to read %s\n", NATIVE_HOWL_KART_PATH);
		return 0;
	}

	if (bytes.size < NATIVE_HOWL_HEADER_FIXED_SIZE)
	{
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	/* "HOWL" in little-endian plus retail version 0x80. */
	if (NativeHowl_ReadLE32(&bytes.data[0]) != 0x4C574F48u ||
	    NativeHowl_ReadLE32(&bytes.data[4]) != 0x80u)
	{
		Platform_LogWarn("[NativeHOWL] invalid HOWL header\n");
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	numSpuAddrs = NativeHowl_ReadLE32(&bytes.data[16]);
	numOtherFX = NativeHowl_ReadLE32(&bytes.data[20]);
	numEngineFX = NativeHowl_ReadLE32(&bytes.data[24]);
	numBanks = NativeHowl_ReadLE32(&bytes.data[28]);
	numSequences = NativeHowl_ReadLE32(&bytes.data[32]);
	(void)numSequences;

	if (numSpuAddrs == 0 || numSpuAddrs > NATIVE_HOWL_MAX_SAMPLES ||
	    numBanks == 0 || numBanks > NATIVE_HOWL_MAX_BANKS)
	{
		Platform_LogWarn(
			"[NativeHOWL] unreasonable catalog sizes samples=%u banks=%u\n",
			(unsigned int)numSpuAddrs,
			(unsigned int)numBanks);
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	spuTableOffset = NATIVE_HOWL_HEADER_FIXED_SIZE;
	bankTableOffset =
		spuTableOffset +
		(int)numSpuAddrs * 4 +
		(int)numOtherFX * 8 +
		(int)numEngineFX * 8;

	if (!NativeHowl_RangeValid(
	        spuTableOffset,
	        (int)numSpuAddrs * 4,
	        bytes.size) ||
	    !NativeHowl_RangeValid(
	        bankTableOffset,
	        (int)numBanks * 2,
	        bytes.size))
	{
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	s_nativeHowlCatalog.samples =
		(struct NativeHowlSampleInfo *)calloc(
			(size_t)numSpuAddrs,
			sizeof(*s_nativeHowlCatalog.samples));
	if (s_nativeHowlCatalog.samples == NULL)
	{
		NativeAssets_FreeBytes(&bytes);
		return 0;
	}

	for (u32 i = 0; i < numSpuAddrs; i++)
	{
		struct NativeHowlSampleInfo *sample =
			&s_nativeHowlCatalog.samples[i];
		const u8 *entry = &bytes.data[spuTableOffset + (int)i * 4];

		sample->spuIndex = (int)i;
		sample->byteLength = (int)NativeHowl_ReadLE16(entry + 2) * 8;
		sample->firstHwlOffset = -1;
		sample->firstBank = -1;
		sample->occurrenceCount = 0;
	}

	/* Each bank has a small ID table at bankSector, then sample blobs start
	   at bankSector+1 in exactly that ID order. This reproduces the storage
	   audit without relying on generated CSV files. */
	for (bankID = 0; bankID < numBanks; bankID++)
	{
		int bankSector =
			(int)NativeHowl_ReadLE16(
				&bytes.data[bankTableOffset + (int)bankID * 2]);
		int bankHeaderOffset = bankSector * NATIVE_HOWL_SECTOR_SIZE;
		int sampleCursor;
		int numSamples;

		if (!NativeHowl_RangeValid(bankHeaderOffset, 2, bytes.size))
			continue;

		numSamples =
			(int)NativeHowl_ReadLES16(&bytes.data[bankHeaderOffset]);
		if (numSamples < 0 ||
		    !NativeHowl_RangeValid(
			    bankHeaderOffset + 2,
			    numSamples * 2,
			    bytes.size))
		{
			continue;
		}

		sampleCursor = (bankSector + 1) * NATIVE_HOWL_SECTOR_SIZE;

		for (int local = 0; local < numSamples; local++)
		{
			int sampleID =
				(int)NativeHowl_ReadLES16(
					&bytes.data[bankHeaderOffset + 2 + local * 2]);
			int sampleBytes;
			struct NativeHowlSampleInfo *sample;

			if (sampleID < 0 || sampleID >= (int)numSpuAddrs)
				continue;

			sample = &s_nativeHowlCatalog.samples[sampleID];
			sampleBytes = sample->byteLength;

			if (!NativeHowl_RangeValid(
			        sampleCursor,
			        sampleBytes,
			        bytes.size))
			{
				break;
			}

			if (sample->firstHwlOffset < 0)
			{
				sample->firstHwlOffset = sampleCursor;
				sample->firstBank = (int)bankID;
			}
			sample->occurrenceCount++;
			sampleCursor += sampleBytes;
		}
	}

	s_nativeHowlCatalog.hwlData = bytes.data;
	s_nativeHowlCatalog.hwlSize = bytes.size;
	s_nativeHowlCatalog.sampleCount = (int)numSpuAddrs;
	s_nativeHowlCatalog.ready = 1;

	Platform_LogWarn(
		"[NativeHOWL] global read-only catalog ready: %d samples, %.2f MiB HOWL resident\n",
		s_nativeHowlCatalog.sampleCount,
		(double)s_nativeHowlCatalog.hwlSize / (1024.0 * 1024.0));

	return 1;
}

int NativeHowlSampleCatalog_IsReady(void)
{
	return s_nativeHowlCatalog.ready;
}

int NativeHowlSampleCatalog_GetCount(void)
{
	if (!s_nativeHowlCatalog.ready &&
	    !NativeHowlSampleCatalog_Init())
		return 0;
	return s_nativeHowlCatalog.sampleCount;
}

int NativeHowlSampleCatalog_GetInfo(
	int spuIndex,
	struct NativeHowlSampleInfo *out)
{
	if (out == NULL)
		return 0;

	if (!s_nativeHowlCatalog.ready &&
	    !NativeHowlSampleCatalog_Init())
		return 0;

	if (spuIndex < 0 ||
	    spuIndex >= s_nativeHowlCatalog.sampleCount)
		return 0;

	*out = s_nativeHowlCatalog.samples[spuIndex];
	return 1;
}

int NativeHowlSampleCatalog_GetBlob(
	int spuIndex,
	const unsigned char **dataOut,
	int *sizeOut)
{
	const struct NativeHowlSampleInfo *sample;

	if (dataOut == NULL || sizeOut == NULL)
		return 0;

	*dataOut = NULL;
	*sizeOut = 0;

	if (!s_nativeHowlCatalog.ready &&
	    !NativeHowlSampleCatalog_Init())
		return 0;

	if (spuIndex < 0 ||
	    spuIndex >= s_nativeHowlCatalog.sampleCount)
		return 0;

	sample = &s_nativeHowlCatalog.samples[spuIndex];
	if (sample->firstHwlOffset < 0 ||
	    sample->occurrenceCount <= 0 ||
	    !NativeHowl_RangeValid(
		    sample->firstHwlOffset,
		    sample->byteLength,
		    s_nativeHowlCatalog.hwlSize))
	{
		return 0;
	}

	*dataOut =
		&s_nativeHowlCatalog.hwlData[sample->firstHwlOffset];
	*sizeOut = sample->byteLength;
	return 1;
}

unsigned int NativeHowlSampleCatalog_EncodeRetailHandle(int spuIndex)
{
	if (spuIndex < 0 || spuIndex > 0xFFFF)
		return 0;
	return NATIVE_HOWL_RETAIL_HANDLE_TAG |
	       (unsigned int)(spuIndex & 0xFFFF);
}

int NativeHowlSampleCatalog_IsRetailHandle(unsigned int handle)
{
	return
		(handle & NATIVE_HOWL_RETAIL_HANDLE_MASK) ==
		NATIVE_HOWL_RETAIL_HANDLE_TAG;
}

int NativeHowlSampleCatalog_DecodeRetailHandle(unsigned int handle)
{
	if (!NativeHowlSampleCatalog_IsRetailHandle(handle))
		return -1;
	return (int)(handle & 0xFFFFu);
}

#else

int NativeHowlSampleCatalog_Init(void) { return 0; }
void NativeHowlSampleCatalog_Shutdown(void) {}
int NativeHowlSampleCatalog_IsReady(void) { return 0; }
int NativeHowlSampleCatalog_GetCount(void) { return 0; }
int NativeHowlSampleCatalog_GetInfo(
	int spuIndex,
	struct NativeHowlSampleInfo *out)
{
	(void)spuIndex;
	(void)out;
	return 0;
}
int NativeHowlSampleCatalog_GetBlob(
	int spuIndex,
	const unsigned char **dataOut,
	int *sizeOut)
{
	(void)spuIndex;
	(void)dataOut;
	(void)sizeOut;
	return 0;
}
unsigned int NativeHowlSampleCatalog_EncodeRetailHandle(int spuIndex)
{
	(void)spuIndex;
	return 0;
}
int NativeHowlSampleCatalog_IsRetailHandle(unsigned int handle)
{
	(void)handle;
	return 0;
}
int NativeHowlSampleCatalog_DecodeRetailHandle(unsigned int handle)
{
	(void)handle;
	return -1;
}

#endif
