/* Crash Forge: Racing - "Modo PC" de dibujo, etapa 1: profundidad compartida.
 *
 * Retail PS1 ordena cada triangulo con una tabla de orden (OT) y lo pinta
 * "de atras hacia adelante". Una PC usa un buffer de profundidad (Z-buffer):
 * cada pixel sabe que tan lejos esta y la placa de video decide sola que
 * tapa a que. Esta etapa hace que la geometria SOLIDA del mundo 3D (pista,
 * karts, personajes, ruedas) use ese Z-buffer real. Humo, particulas,
 * transparencias, cielo, HUD y menus siguen exactamente igual que antes.
 *
 * Es la base para la etapa 2 (pista dibujada directo por la placa de video):
 * las dos partes necesitan compartir la misma profundidad para no taparse mal.
 *
 * Solo existe en builds de prueba: CMake -DCTR_PC_RENDER_QA=ON.
 * Sin esa opcion, todas las macros de abajo quedan vacias y el juego compila
 * exactamente igual que antes.
 */
#pragma once

#if defined(CTR_NATIVE) && defined(CTR_PC_RENDER_QA)

/* Ciclo de cada frame. */
void NativePcRender_BeginFrame(void);
void NativePcRender_EndScene(void);

/* Rango de memoria de paquetes que pertenece al mundo 3D solido.
 * Se marca con el cursor de PrimMem antes y despues de dibujar. */
void NativePcRender_WorldBegin(const void *cursor);
void NativePcRender_WorldEnd(const void *cursor);
int NativePcRender_IsWorldPacket(const void *packet);

/* Modo activo (con override temporal para las capturas A/B). */
int NativePcRender_IsActive(void);
void NativePcRender_SetActive(int enabled);
void NativePcRender_Toggle(void);

/* Estadisticas para el log: cuantas partes solidas del mundo usaron Z. */
void NativePcRender_SetDepthStatus(int available);
void NativePcRender_NoteSplit(int worldOpaque, int depthTested);

/* Diagnostico (debug/pc_render_diag.enable): solo anota en el log. */
int NativePcRender_DiagActive(void);
u32 NativePcRender_DiagFrameId(void);
void NativePcRender_DiagCountLine(void);
u32 NativePcRender_PacketRangeTags(const void *packet);

#define NPR_BEGIN_FRAME()    NativePcRender_BeginFrame()
#define NPR_WORLD_BEGIN(gGT) NativePcRender_WorldBegin((gGT)->backBuffer->primMem.cursor)
#define NPR_WORLD_END(gGT)   NativePcRender_WorldEnd((gGT)->backBuffer->primMem.cursor)

#else

#define NPR_BEGIN_FRAME()    ((void)0)
#define NPR_WORLD_BEGIN(gGT) ((void)0)
#define NPR_WORLD_END(gGT)   ((void)0)

#endif
