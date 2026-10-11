#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int32_t s32;
typedef int16_t s16;
struct Driver { int driverID; s16 reserves; };
#define CTR_MipsAddLo(a,b) ((int32_t)((uint32_t)(a) + (uint32_t)(b)))
enum { PLATFORM_FPS_LIMIT_30 = 1, PLATFORM_FPS_LIMIT_UNLIMITED = 9 };
int Platform_GetFPSLimitMode(void);
