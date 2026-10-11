#ifndef NATIVE_RACER_VISUAL_H
#define NATIVE_RACER_VISUAL_H

struct Model;

struct Model *NativeRacerVisual_ResolveModel(int characterID);
b32 NativeRacerVisual_UsesSkeletalModel(int characterID);

#endif
