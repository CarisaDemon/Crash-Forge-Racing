#include <common.h>
#include <NativeReserves.h>
#if defined(CTR_NATIVE)
#include <platform/native_world.h>
#endif

#define NATIVE_RESERVES_SLOT_COUNT 8
#define NATIVE_RESERVES_RETAIL_MIRROR_MAX 0x7fff
#define NATIVE_RESERVES_NATIVE_MAX 0x3fffffff

struct NativeReservesSlot
{
    struct Driver *driver;
    s32 value;
    /* Fractional 24/25 units preserve a real 30 Hz burn rate at any
     * presentation FPS. Keep this independent from the legacy s16 mirror. */
    s32 highRefreshFraction;
};

static struct NativeReservesSlot s_nativeReserves[NATIVE_RESERVES_SLOT_COUNT];

static int NativeReserves_GetSlotIndex(const struct Driver *driver)
{
    int driverID;

    if (driver == NULL)
        return -1;

    driverID = (int)driver->driverID;
    if (driverID < 0 || driverID >= NATIVE_RESERVES_SLOT_COUNT)
        return -1;

    return driverID;
}

static void NativeReserves_Mirror(struct Driver *driver, s32 value)
{
    if (driver == NULL)
        return;

    if (value <= 0)
        driver->reserves = 0;
    else if (value > NATIVE_RESERVES_RETAIL_MIRROR_MAX)
        driver->reserves = NATIVE_RESERVES_RETAIL_MIRROR_MAX;
    else
        driver->reserves = (s16)value;
}

static struct NativeReservesSlot *NativeReserves_Ensure(struct Driver *driver)
{
    int slotIndex = NativeReserves_GetSlotIndex(driver);
    struct NativeReservesSlot *slot;
    s32 mirror;
    s32 expectedMirror;

    if (slotIndex < 0)
        return NULL;

    slot = &s_nativeReserves[slotIndex];
    mirror = driver->reserves > 0 ? (s32)driver->reserves : 0;

    if (slot->driver != driver)
    {
        slot->driver = driver;
        slot->value = mirror;
        slot->highRefreshFraction = 0;
        return slot;
    }

    /* Respect legacy code that explicitly clears reserves (brake, crash,
       mask grab, etc.) even when the native sidecar had more than 32767. */
    if (driver->reserves == 0 && slot->value != 0)
    {
        slot->value = 0;
        slot->highRefreshFraction = 0;
        return slot;
    }

    expectedMirror = slot->value > NATIVE_RESERVES_RETAIL_MIRROR_MAX ?
        NATIVE_RESERVES_RETAIL_MIRROR_MAX : slot->value;

    /* If untouched retail code writes a positive reserve value directly,
       adopt it rather than fighting that state. */
    if (driver->reserves > 0 && driver->reserves != expectedMirror)
    {
        slot->value = mirror;
        slot->highRefreshFraction = 0;
    }

    return slot;
}

s32 NativeReserves_Get(const struct Driver *driver)
{
    struct NativeReservesSlot *slot;

    if (driver == NULL)
        return 0;

    slot = NativeReserves_Ensure((struct Driver *)driver);
    if (slot != NULL)
        return slot->value;

    return driver->reserves > 0 ? (s32)driver->reserves : 0;
}

void NativeReserves_Set(struct Driver *driver, s32 value)
{
    struct NativeReservesSlot *slot;

    if (driver == NULL)
        return;

    if (value < 0)
        value = 0;
    if (value > NATIVE_RESERVES_NATIVE_MAX)
        value = NATIVE_RESERVES_NATIVE_MAX;

    slot = NativeReserves_Ensure(driver);
    if (slot != NULL)
    {
        slot->value = value;
        if (value == 0)
            slot->highRefreshFraction = 0;
    }

    NativeReserves_Mirror(driver, value);
}

void NativeReserves_Add(struct Driver *driver, s32 amount)
{
    long long next;

    if (driver == NULL)
        return;

    next = (long long)NativeReserves_Get(driver) + (long long)amount;
    if (next < 0)
        next = 0;
    if (next > NATIVE_RESERVES_NATIVE_MAX)
        next = NATIVE_RESERVES_NATIVE_MAX;

    NativeReserves_Set(driver, (s32)next);
}

void NativeReserves_Decrement(struct Driver *driver, int elapsed)
{
    s32 value;

    if (driver == NULL || elapsed <= 0)
        return;

    value = NativeReserves_Get(driver);
    if (value <= 0)
        return;

#if defined(CTR_NATIVE)
    /* Coherent racing/battle physics already runs exactly ONCE per original
     * 32-unit game tick at every presentation FPS (30, 200, Unlimited).
     * Scaling that 32 by 24/25 only at high refresh made boost expire at a
     * different rate compared with 30 FPS and let held pads refill faster
     * than the logical drain. Never scale a fixed-step simulation tick.
     *
     * Keep the fractional 24/25 conversion only for the legacy host-slice
     * fallback outside coherent races. The old fractional remainder is
     * cleared when returning to a coherent world or a 30 FPS baseline. */
    if (Platform_GetFPSLimitMode() != PLATFORM_FPS_LIMIT_30 &&
        !NativeWorld_IsActive())
    {
        struct NativeReservesSlot *slot = NativeReserves_Ensure(driver);
        if (slot != NULL)
        {
            const long long scaled =
                (long long)elapsed * 24LL + slot->highRefreshFraction;
            elapsed = (int)(scaled / 25LL);
            slot->highRefreshFraction = (s32)(scaled % 25LL);
        }
    }
    else
    {
        struct NativeReservesSlot *slot = NativeReserves_Ensure(driver);
        if (slot != NULL)
            slot->highRefreshFraction = 0;
    }
#endif
    if (elapsed <= 0)
        return;

    value -= elapsed;
    if (value < 0)
        value = 0;

    NativeReserves_Set(driver, value);
}

void NativeReserves_DecrementCounter(struct Driver *driver, int elapsed, int *counter)
{
    if (driver == NULL || elapsed <= 0)
        return;

    if (NativeReserves_Get(driver) > 0)
    {
        NativeReserves_Decrement(driver, elapsed);
        if (counter != NULL)
            *counter = CTR_MipsAddLo(*counter, elapsed);
    }
}
