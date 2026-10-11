#ifndef CRASH_FORGE_NATIVE_CUSTOM_SAMPLE_REGISTRY_H
#define CRASH_FORGE_NATIVE_CUSTOM_SAMPLE_REGISTRY_H

#define NATIVE_CUSTOM_SAMPLE_HANDLE_TAG  0xCF010000u
#define NATIVE_CUSTOM_SAMPLE_HANDLE_MASK 0xFFFF0000u
#define NATIVE_CUSTOM_SAMPLE_MAX 256

struct NativeCustomSampleView
{
	const short *samples;
	int sampleCount;
	int sampleRate;
	int loopStart;
	int loopEnd;
	int loopEnabled;
};

int NativeCustomSampleRegistry_LoadPackage(const char *slug, int expectedCount);
int NativeCustomSampleRegistry_LoadDirectory(const char *relativeDir, int expectedCount);
void NativeCustomSampleRegistry_Clear(void);
int NativeCustomSampleRegistry_GetCount(void);
int NativeCustomSampleRegistry_GetView(int index, struct NativeCustomSampleView *out);

unsigned int NativeCustomSampleRegistry_EncodeHandle(int index);
int NativeCustomSampleRegistry_IsHandle(unsigned int handle);
int NativeCustomSampleRegistry_DecodeHandle(unsigned int handle);

#endif
