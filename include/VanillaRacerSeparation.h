#ifndef VANILLA_RACER_SEPARATION_H
#define VANILLA_RACER_SEPARATION_H

void VanillaRacerSeparation_Init(void);
void VanillaRacerSeparation_Reload(void);
b32 VanillaRacerSeparation_HasMask(int characterID);
b32 VanillaRacerSeparation_ShouldHidePrimitive(int characterID, int primitiveIndex);
b32 VanillaRacerSeparation_ShouldHidePrimitiveForInstance(const struct Instance *inst, int primitiveIndex);
int VanillaRacerSeparation_CountPrimitives(const struct ModelHeader *mh);

void VanillaRacerSeparation_DebugBegin(const struct Instance *inst, const struct ModelHeader *mh);
int VanillaRacerSeparation_DebugIsActiveFor(const struct Instance *inst);
void VanillaRacerSeparation_DebugTriangle(const struct Instance *inst, int primitiveIndex,
                                          int sourceA, int sourceB, int sourceC,
                                          int colorA, int colorB, int colorC,
                                          int ax, int ay, int az,
                                          int bx, int by, int bz,
                                          int cx, int cy, int cz);
void VanillaRacerSeparation_DebugEnd(const struct Instance *inst);

#endif
