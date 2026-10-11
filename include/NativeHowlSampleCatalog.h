#ifndef CRASH_FORGE_NATIVE_HOWL_SAMPLE_CATALOG_H
#define CRASH_FORGE_NATIVE_HOWL_SAMPLE_CATALOG_H

#define NATIVE_HOWL_RETAIL_HANDLE_TAG  0xCF000000u
#define NATIVE_HOWL_RETAIL_HANDLE_MASK 0xFFFF0000u

struct NativeHowlSampleInfo
{
	int spuIndex;
	int byteLength;
	int firstHwlOffset;
	int firstBank;
	int occurrenceCount;
};

int NativeHowlSampleCatalog_Init(void);
void NativeHowlSampleCatalog_Shutdown(void);
int NativeHowlSampleCatalog_IsReady(void);
int NativeHowlSampleCatalog_GetCount(void);
int NativeHowlSampleCatalog_GetInfo(int spuIndex, struct NativeHowlSampleInfo *out);
int NativeHowlSampleCatalog_GetBlob(int spuIndex, const unsigned char **dataOut, int *sizeOut);

unsigned int NativeHowlSampleCatalog_EncodeRetailHandle(int spuIndex);
int NativeHowlSampleCatalog_IsRetailHandle(unsigned int handle);
int NativeHowlSampleCatalog_DecodeRetailHandle(unsigned int handle);

#endif
