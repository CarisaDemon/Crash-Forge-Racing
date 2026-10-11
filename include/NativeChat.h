#ifndef NATIVE_CHAT_H
#define NATIVE_CHAT_H

#if defined(CTR_NATIVE)

#define NATIVE_CHAT_HISTORY_MAX 32
#define NATIVE_CHAT_TEXT_MAX 128
#define NATIVE_CHAT_NAME_MAX 32

enum NativeChatKind
{
	NATIVE_CHAT_SYSTEM = 0,
	NATIVE_CHAT_LOCAL,
	NATIVE_CHAT_REMOTE,
};

struct NativeChatMessage
{
	int playerID;
	unsigned int tick;
	unsigned char kind;
	char playerName[NATIVE_CHAT_NAME_MAX];
	char text[NATIVE_CHAT_TEXT_MAX];
};

void NativeChat_Reset(void);
void NativeChat_AddSystem(const char *text, unsigned int tick);
void NativeChat_AddLocal(int playerID, const char *playerName, const char *text, unsigned int tick);
void NativeChat_AddRemote(int playerID, const char *playerName, const char *text, unsigned int tick);

int NativeChat_GetCount(void);
const struct NativeChatMessage *NativeChat_Get(int index);
const struct NativeChatMessage *NativeChat_GetLatestFromPlayer(int playerID, unsigned int nowTick, unsigned int maxAgeTicks);

#endif

#endif
