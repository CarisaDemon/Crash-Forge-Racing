#if defined(CTR_NATIVE)
static const struct Driver *s_landingBoostDriver[8];
static u32 s_landingBoostFrame[8];
static u32 s_landingBoostSkipped[8];
static int VehFire_NativeLandingGrantIsDuplicate(
    const struct Driver *driver,int reserves,u32 type)
{
    int i;
    u32 frame;
    if (!driver || !sdata || !sdata->gGT ||
        type != POWER_SLIDE_HANG_TIME || reserves != 960 ||
        driver->jumpMeter < 640 ||
        (driver->actionsFlagSet & ACTION_AIRBORNE) != 0 ||
        (driver->actionsFlagSetPrevFrame & ACTION_AIRBORNE) == 0)
        return 0;
    i=(int)driver->driverID;
    if(i<0 || i>=8)return 0;
    frame=sdata->frameCounter;
    if(s_landingBoostDriver[i]==driver && s_landingBoostFrame[i]==frame)
    {
        /* First duplicate is useful to diagnose, later ones summarized. */
        if(++s_landingBoostSkipped[i]==1 ||
           (s_landingBoostSkipped[i]%64u)==0)
            Platform_Log("[RESERVE FIX] rejected duplicate landing boost id=%d tick=%u repeats=%u\n",
                i,(unsigned)frame,(unsigned)s_landingBoostSkipped[i]);
        return 1;
    }
    s_landingBoostDriver[i]=driver;
    s_landingBoostFrame[i]=frame;
    return 0;
}
#endif
