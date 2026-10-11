#include <common.h>

#if defined(CTR_NATIVE)

#include <NativeSession.h>
#include <stdio.h>
#include <string.h>

#define NATIVE_SESSION_NAME_MAX 31

static enum NativeSessionMode s_nativeSessionMode = NATIVE_SESSION_NONE;
static int s_nativeSessionLocalPlayerID = 0;
static char s_nativeSessionPlayerName[NATIVE_SESSION_NAME_MAX + 1] = "PLAYER";

static void NativeSession_CopySanitized(
	char *dst,
	int dstSize,
	const char *src,
	const char *fallback)
{
	int out = 0;
	int i;

	if (dst == NULL || dstSize <= 0)
		return;

	if (src == NULL || src[0] == 0)
		src = fallback;

	for (i = 0;
	     src != NULL && src[i] != 0 && out < dstSize - 1;
	     i++)
	{
		unsigned char c = (unsigned char)src[i];

		if (c < 0x20 || c == 0x7f)
			continue;

		dst[out++] = (char)c;
	}

	dst[out] = 0;

	if (out == 0 && fallback != NULL)
		snprintf(dst, (size_t)dstSize, "%s", fallback);
}

void NativeSession_Reset(void)
{
	s_nativeSessionMode = NATIVE_SESSION_NONE;
	s_nativeSessionLocalPlayerID = 0;
}

void NativeSession_SetMode(enum NativeSessionMode mode)
{
	s_nativeSessionMode = mode;
}

enum NativeSessionMode NativeSession_GetMode(void)
{
	return s_nativeSessionMode;
}

int NativeSession_IsHangout(void)
{
	return s_nativeSessionMode == NATIVE_SESSION_HANGOUT_LOCAL ||
	       s_nativeSessionMode == NATIVE_SESSION_LINKED_HANGOUT;
}

void NativeSession_SetLocalPlayerID(int playerID)
{
	if (playerID < 0)
		playerID = 0;

	if (playerID > 255)
		playerID = 255;

	s_nativeSessionLocalPlayerID = playerID;
}

int NativeSession_GetLocalPlayerID(void)
{
	return s_nativeSessionLocalPlayerID;
}

void NativeSession_SetLocalPlayerName(const char *name)
{
	NativeSession_CopySanitized(
		s_nativeSessionPlayerName,
		(int)sizeof(s_nativeSessionPlayerName),
		name,
		"PLAYER");
}

const char *NativeSession_GetLocalPlayerName(void)
{
	return s_nativeSessionPlayerName;
}

void NativeSession_ConfigureFromArgs(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++)
	{
		const char *arg = argv[i];

		if (arg == NULL)
			continue;

		if (strncmp(arg, "--hangout-name=", 15) == 0)
		{
			NativeSession_SetLocalPlayerName(arg + 15);
			continue;
		}
	}
}

#endif
