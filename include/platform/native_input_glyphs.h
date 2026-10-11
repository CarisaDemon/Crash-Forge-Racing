#ifndef PLATFORM_NATIVE_INPUT_GLYPHS_H
#define PLATFORM_NATIVE_INPUT_GLYPHS_H

#include <stdint.h>

struct PrimMem;

/* Returns non-zero when a supplied controller glyph was emitted.
   Face buttons, shoulders, triggers, Start and Select use image assets;
   bindings without supplied art fall back to readable text names. */
int Platform_InputGlyphsDrawBinding(
    int playerIndex,
    int binding,
    int x, int y, int w, int h,
    struct PrimMem *primMem,
    uint32_t *ot);

/* waiting=0: empty/unbound slot. waiting=1: darker capture slot. */
int Platform_InputGlyphsDrawState(
    int waiting,
    int x, int y, int w, int h,
    struct PrimMem *primMem,
    uint32_t *ot);

void Platform_InputGlyphsShutdown(void);

#endif
