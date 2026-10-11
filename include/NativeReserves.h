#ifndef NATIVE_RESERVES_H
#define NATIVE_RESERVES_H

struct Driver;

s32 NativeReserves_Get(const struct Driver *driver);
void NativeReserves_Set(struct Driver *driver, s32 value);
void NativeReserves_Add(struct Driver *driver, s32 amount);
void NativeReserves_Decrement(struct Driver *driver, int elapsed);
void NativeReserves_DecrementCounter(struct Driver *driver, int elapsed, int *counter);

#endif
