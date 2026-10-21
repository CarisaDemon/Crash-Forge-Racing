#ifndef NATIVE_RENDERER_H
#define NATIVE_RENDERER_H

#include <platform/native_renderer_types.h>

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen);
int NativeRenderer_InitialisePSX(void);
void NativeRenderer_SetDisplayAspect(int wide);
void NativeRenderer_SetDisplayAspectSize(int width,int height);
int NativeRenderer_WindowToPSX(float windowX, float windowY, int *psxX, int *psxY);
void NativeRenderer_Shutdown(void);
void NativeRenderer_ResetDevice(void);
void NativeRenderer_BeginScene(void);
void NativeRenderer_EndScene(void);
void NativeRenderer_UpdateSwapIntervalState(int swapInterval);
void NativeRenderer_InvalidateSwapIntervalState(void);
void NativeRenderer_SwapWindow(void);
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h);
void NativeRenderer_StoreFrameBufferDeferred(int x, int y, int w, int h);
void NativeRenderer_PresentVRAMDisplay(void);
void NativeRenderer_PresentVRAMRect(int x, int y, int w, int h);
void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int readFromFramebuffer);
void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dstX, int dstY);
void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dstW, int dstH);
void NativeRenderer_UpdateVRAM(void);
void NativeRenderer_ReadFramebufferDataToVRAM(void);
void NativeRenderer_DiscardFramebufferReadback(void);
int NativeRenderer_GetVRAMStateSize(void);
int NativeRenderer_CaptureVRAMState(void *dst, int dstSize);
int NativeRenderer_RestoreVRAMState(const void *src, int srcSize);
TextureID NativeRenderer_GetVRAMTexture(void);
TextureID NativeRenderer_GetWhiteTexture(void);
TextureID NativeRenderer_CreateImageTexture(const u8 *rgba, int width, int height);
void NativeRenderer_ReleaseImageTexture(TextureID texture);
void NativeRenderer_SetBlendMode(BlendMode blendMode);
int NativeRenderer_CanSinglePassPSXTexturedSemiTrans(BlendMode blendMode);
void NativeRenderer_SetPSXTexturedSemiTransBlendMode(BlendMode blendMode);
void NativeRenderer_SetStencilMode(int drawPrim);
void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, const DISPENV *displayEnv, int enable);
void NativeRenderer_SetupClipMode(const RECT16 *clipRect, const DISPENV *displayEnv, int enable);
void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat);
void NativeRenderer_SetOverrideTextureSize(int width, int height);
void NativeRenderer_SetPSXTextureSemiTransPass(int pass);
void NativeRenderer_SetPSXTextureOutputSTP(int enabled);
void NativeRenderer_SetPSXDrawMaskSet(int maskSet);
int NativeRenderer_GetGpuTimingEnabled(void);
void NativeRenderer_SetGpuTimingEnabled(int enabled);
double NativeRenderer_GetLastGpuFrameMs(void);
int NativeRenderer_GetExperimentalCompactVRAM(void);
void NativeRenderer_SetExperimentalCompactVRAM(int enabled);
int NativeRenderer_GetExperimentalZBuffer(void);
void NativeRenderer_SetExperimentalZBuffer(int enabled);
void NativeRenderer_SetExperimentalDepthEligible(int eligible);
int NativeRenderer_GetLevelPCDepthSceneActive(void);
void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int count);
void NativeRenderer_DrawTriangles(int startVertex, int triangles);

/* Experimental static-world PC renderer. Disabled unless the game-side debug
 * gate explicitly uploads/queues a level. The legacy PS1 renderer remains the
 * fallback for every unsupported block. */
int NativeRenderer_LevelPCEnabled(void);
int NativeRenderer_LevelPCManualPlaytest(void);
int NativeRenderer_LevelPCUpload(const void *meshKey, const NativeLevelPCVertex *vertices, const u8 *faceTypes,
	int vertexCount, int blockCount);
void NativeRenderer_LevelPCBeginFrame(const void *meshKey, const s16 viewM[3][3], const s32 viewT[3],
                                      int geomScreen, int viewportW, int viewportH, int wide);
float NativeRenderer_LevelPCWorldDepthQA(float x, float y, float z);
int NativeRenderer_LevelPCQueueBlock(const void *meshKey, int blockIndex);
void NativeRenderer_LevelPCDrawQueued(void);
void NativeRenderer_LevelPCReset(void);
/* Stress-only world-space vertex GPU transform feedback against original PS1
 * projected SXY/SZ. Hidden on screen. All real rasterization remains retail. */
int NativeRenderer_LevelPCProjectionQAEnabled(void);
void NativeRenderer_LevelPCProjectionQAReset(void);
void NativeRenderer_LevelPCProjectionQAUpload(const void *meshKey,
    const void *vertexArray,int vertexCount);
void NativeRenderer_LevelPCProjectionQABegin(const void *meshKey,
    const s16 viewM[3][3],const s32 viewT[3],int screen,int wide,
    s32 offsetXQ16,s32 offsetYQ16);
void NativeRenderer_LevelPCProjectionQARecord(const void *meshKey,
    const void *vertexBase,const void *vertex,
    int sx,int sy,int depth);
void NativeRenderer_LevelPCProjectionQAFlush(void);
struct DrawLevelOvr1PScratchVertex;
void NativeRenderer_LevelPCMidpointQAReset(void);
void NativeRenderer_LevelPCMidpointQARecord(
    const struct DrawLevelOvr1PScratchVertex *a,
    const struct DrawLevelOvr1PScratchVertex *b,
    const struct DrawLevelOvr1PScratchVertex *mid);
void NativeRenderer_LevelPCMidpointQAFlush(void);

/* Stage-0 PC level validation: one legacy QuadBlock is drawn as a magenta
 * wireframe after the normal renderer. It never replaces game geometry. */
void NativeRenderer_LevelPCDebugQueueBlock(
    const s16 positions[9][3],
    const u8 colors[9][4],
    const s16 viewM[3][3],
    const s32 viewT[3],
    int geomScreen,
    int viewportW,
    int viewportH,
    int wide,
    int blockIndex);
void NativeRenderer_LevelPCDebugQueueTexturedFace(
    const NativeLevelPCVertex vertices[6],
    int blockIndex,
    int faceIndex);
void NativeRenderer_LevelPCDebugDrawQueued(void);
void NativeRenderer_PushDebugLabel(const char *label);
void NativeRenderer_PopDebugLabel(void);

#endif
