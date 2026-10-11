#include <common.h>
#include <NativeReserves.h>
#include <stdlib.h>
static int currentMode = PLATFORM_FPS_LIMIT_30;
int Platform_GetFPSLimitMode(void) { return currentMode; }
static void check(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); exit(3); }
    printf("PASS: %s\n", label);
}
int main(void) {
    struct Driver d = { 0, 0 };
    int counter = 0;
    currentMode = PLATFORM_FPS_LIMIT_30;
    NativeReserves_Set(&d, 10000);
    for (int i=0; i<120; i++) NativeReserves_DecrementCounter(&d,32,&counter);
    const int expected = NativeReserves_Get(&d);
    check(expected==6160, "reference 30Hz: 120 logical ticks/4s -> 3840 reserve units");
    check(counter==3840, "timeSpentUsingReserves uses original elapsed units");
    currentMode = PLATFORM_FPS_LIMIT_UNLIMITED;
    NativeReserves_Set(&d, 0);
    NativeReserves_Set(&d, 10000);
    counter = 0;
    for (int i=0; i<125; i++) NativeReserves_DecrementCounter(&d,32,&counter);
    check(NativeReserves_Get(&d)==expected, "unlocked fixed 32ms ticks: same 4s decay as 30Hz reference");
    check(counter==4000, "unlocked counter remains in elapsedTimeMS units");
    NativeReserves_Set(&d, 0); NativeReserves_Set(&d, 10000);
    for (int i=0; i<200; i++) NativeReserves_Decrement(&d,5);
    check(NativeReserves_Get(&d)==9040, "200Hz host slices: fractional 960 units per 1000ms");
    NativeReserves_Set(&d, 0); NativeReserves_Set(&d, 10000);
    for (int i=0; i<40; i++) {
        NativeReserves_Decrement(&d,8);
        NativeReserves_Decrement(&d,8);
        NativeReserves_Decrement(&d,9);
    }
    check(NativeReserves_Get(&d)==9040, "120Hz mixed 8/9ms slices preserve same drain");
    NativeReserves_Set(&d,0); NativeReserves_Set(&d,10000);
    NativeReserves_Decrement(&d,1);
    NativeReserves_Set(&d,0); NativeReserves_Set(&d,10000);
    NativeReserves_Decrement(&d,1);
    check(NativeReserves_Get(&d)==10000, "clear resets fractional carry");
    NativeReserves_Set(&d,100000);
    check(d.reserves == 32767 && NativeReserves_Get(&d)==100000, "native sidecar preserves more than signed-16-bit reserve cap");
    puts("RESERVE_DECAY_SELFTEST_OK");
    return 0;
}
