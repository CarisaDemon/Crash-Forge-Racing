#include <common.h>

#if defined(CTR_NATIVE)

#include <NativeChat.h>
#include <string.h>

static struct NativeChatMessage s_nativeChatHistory[NATIVE_CHAT_HISTORY_MAX];
static int s_nativeChatCount;

static void NativeChat_CopyText(char *dst, int dstSize, const char *src)
{
	int out = 0;
	int i;

	if (dst == NULL || dstSize <= 0)
		return;

	if (src == NULL)
		src = "";

	for (i = 0; src[i] != 0 && out < dstSize - 1; i++)
	{
		unsigned char c = (unsigned char)src[i];

		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';

		if (c < 0x20 || c == 0x7f)
			continue;

		dst[out++] = (char)c;
	}

	dst[out] = 0;
}

static void NativeChat_Add(
	int kind,
	int playerID,
	const char *playerName,
	const char *text,
	unsigned int tick)
{
	struct NativeChatMessage *message;

	if (text == NULL || text[0] == 0)
		return;

	if (s_nativeChatCount < NATIVE_CHAT_HISTORY_MAX)
	{
		message = &s_nativeChatHistory[s_nativeChatCount++];
	}
	else
	{
		memmove(
			&s_nativeChatHistory[0],
			&s_nativeChatHistory[1],
			sizeof(s_nativeChatHistory[0]) * (NATIVE_CHAT_HISTORY_MAX - 1));
		message = &s_nativeChatHistory[NATIVE_CHAT_HISTORY_MAX - 1];
	}

	memset(message, 0, sizeof(*message));
	message->kind = (unsigned char)kind;
	message->playerID = playerID;
	message->tick = tick;

	if (playerName != NULL)
		NativeChat_CopyText(message->playerName, (int)sizeof(message->playerName), playerName);

	NativeChat_CopyText(message->text, (int)sizeof(message->text), text);
}

void NativeChat_Reset(void)
{
	memset(s_nativeChatHistory, 0, sizeof(s_nativeChatHistory));
	s_nativeChatCount = 0;
}

void NativeChat_AddSystem(const char *text, unsigned int tick)
{
	NativeChat_Add(NATIVE_CHAT_SYSTEM, -1, "SYSTEM", text, tick);
}

void NativeChat_AddLocal(int playerID, const char *playerName, const char *text, unsigned int tick)
{
	NativeChat_Add(NATIVE_CHAT_LOCAL, playerID, playerName, text, tick);
}

void NativeChat_AddRemote(int playerID, const char *playerName, const char *text, unsigned int tick)
{
	NativeChat_Add(NATIVE_CHAT_REMOTE, playerID, playerName, text, tick);
}

int NativeChat_GetCount(void)
{
	return s_nativeChatCount;
}

const struct NativeChatMessage *NativeChat_Get(int index)
{
	if (index < 0 || index >= s_nativeChatCount)
		return NULL;

	return &s_nativeChatHistory[index];
}

const struct NativeChatMessage *NativeChat_GetLatestFromPlayer(
	int playerID,
	unsigned int nowTick,
	unsigned int maxAgeTicks)
{
	int i;

	for (i = s_nativeChatCount - 1; i >= 0; i--)
	{
		const struct NativeChatMessage *message = &s_nativeChatHistory[i];
		unsigned int age;

		if (message->playerID != playerID)
			continue;

		age = nowTick - message->tick;
		if (age <= maxAgeTicks)
			return message;

		return NULL;
	}

	return NULL;
}

#endif
