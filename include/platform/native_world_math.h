#ifndef CTR_NATIVE_WORLD_MATH_H
#define CTR_NATIVE_WORLD_MATH_H
#include <stdint.h>
/* Q16 interpolation: widen BEFORE subtraction; endpoints are exact. */
static inline int32_t NW_Lerp(int32_t a, int32_t b, int alpha)
{
    return (int32_t)((int64_t)a + (((int64_t)b - a) * alpha) / 65536);
}
static inline int16_t NW_Angle(int16_t a, int16_t b, int alpha)
{
    int delta = ((int)b - (int)a + 2048) & 4095;
    delta -= 2048;
    return (int16_t)(((int)a + (delta * alpha) / 65536) & 4095);
}
/* Presentation-only symmetric rounding. C integer division truncates negative
   deltas toward zero, which creates repeated samples followed by 2-unit jumps
   at high refresh (very visible on the Hub camera). */
static inline int32_t NW_LerpRound(int32_t a, int32_t b, int alpha)
{
    int64_t num = ((int64_t)b - a) * alpha;
    int64_t step = (num >= 0) ? ((num + 32768) / 65536) : -(((-num) + 32768) / 65536);
    return (int32_t)((int64_t)a + step);
}
static inline int16_t NW_AngleRound(int16_t a, int16_t b, int alpha)
{
    int delta = ((int)b - (int)a + 2048) & 4095;
    int64_t num;
    int step;
    delta -= 2048;
    num = (int64_t)delta * alpha;
    step = (num >= 0) ? (int)((num + 32768) / 65536) : -(int)(((-num) + 32768) / 65536);
    return (int16_t)(((int)a + step) & 4095);
}
static inline int NW_ConsumeClock(int *remainder, int elapsed)
{
    int ticks;
    if (elapsed < 0) elapsed = 0;
    *remainder += elapsed;
    ticks = *remainder / 32;
    *remainder -= ticks * 32;
    return ticks;
}
#endif
