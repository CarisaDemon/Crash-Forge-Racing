#ifndef NATIVE_SESSION_H
#define NATIVE_SESSION_H

#if defined(CTR_NATIVE)

enum NativeSessionMode
{
	NATIVE_SESSION_NONE = 0,
	NATIVE_SESSION_HANGOUT_LOCAL,
	NATIVE_SESSION_LINKED_HANGOUT,
	NATIVE_SESSION_LINKED_RACE,
};

void NativeSession_Reset(void);
void NativeSession_SetMode(enum NativeSessionMode mode);
enum NativeSessionMode NativeSession_GetMode(void);
int NativeSession_IsHangout(void);

void NativeSession_SetLocalPlayerID(int playerID);
int NativeSession_GetLocalPlayerID(void);

void NativeSession_SetLocalPlayerName(const char *name);
const char *NativeSession_GetLocalPlayerName(void);

void NativeSession_ConfigureFromArgs(int argc, char **argv);
int NativeSession_NoModsActive(void);

#endif

#endif
