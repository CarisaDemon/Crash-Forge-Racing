#include <stdio.h>
#include <stdlib.h>
typedef unsigned int u32;
#define CTR_NATIVE 1
#define POWER_SLIDE_HANG_TIME 2
#define ACTION_AIRBORNE 4
#define Platform_Log(...) ((void)0)
struct GameTracker { int valid; };
struct SData { struct GameTracker *gGT; u32 frameCounter; };
static struct GameTracker game = {1};
static struct SData state = {&game, 0};
static struct SData *sdata = &state;
struct Driver {
    int driverID;
    int jumpMeter;
    u32 actionsFlagSet;
    u32 actionsFlagSetPrevFrame;
};
#include "landing_guard_under_test.h"
static void check(int okay,const char *label) {
    if (!okay) { fprintf(stderr,"FAIL %s\n",label); exit(3); }
    printf("PASS %s\n",label);
}
int main(void) {
    struct Driver a={0,1440,0,ACTION_AIRBORNE};
    struct Driver b={1,1440,0,ACTION_AIRBORNE};
    state.frameCounter=50;
    check(!VehFire_NativeLandingGrantIsDuplicate(&a,960,POWER_SLIDE_HANG_TIME),"first landing boost granted");
    check(VehFire_NativeLandingGrantIsDuplicate(&a,960,POWER_SLIDE_HANG_TIME),"repeated render cannot grant the same landing twice");
    check(!VehFire_NativeLandingGrantIsDuplicate(&b,960,POWER_SLIDE_HANG_TIME),"separate bot may land in same logic tick");
    state.frameCounter=51;
    check(!VehFire_NativeLandingGrantIsDuplicate(&a,960,POWER_SLIDE_HANG_TIME),"new physics tick allows a new landing");
    check(!VehFire_NativeLandingGrantIsDuplicate(&a,720,POWER_SLIDE_HANG_TIME),"different reserves (drift) unaffected");
    a.actionsFlagSet=ACTION_AIRBORNE;
    check(!VehFire_NativeLandingGrantIsDuplicate(&a,960,POWER_SLIDE_HANG_TIME),"airborne boost path not suppressed");
    a.actionsFlagSet=0; a.actionsFlagSetPrevFrame=0;
    check(!VehFire_NativeLandingGrantIsDuplicate(&a,960,POWER_SLIDE_HANG_TIME),"no landing edge: no filter");
    puts("LANDING_GUARD_SELFTEST_OK");
    return 0;
}
