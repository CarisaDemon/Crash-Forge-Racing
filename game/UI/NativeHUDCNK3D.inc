/*
 * Crash Forge Racing: CNK 3D gauge / native HUD implementation.
 *
 * Source: Powerslide_Meter.zip / author's original Blender OBJ and UV.
 * Body and golden/black 3D arrow were baked separately with full 1024x1024
 * UV filtering into 256x256 RGBA sprites (bake_meter_v2.py). This removes
 * seams from 120 tiny PS1-style FT4 triangles and preserves Blender's look.
 * Arrow movement follows the actual player's powerslide charge.
 */
#define NATIVE_HUD_CNK3D_BAKED_DIM 256
#define NATIVE_HUD_CNK3D_BODY_PX_PER_WORLD 72.0f
#define NATIVE_HUD_CNK3D_ARROW_PX_PER_WORLD 180.0f
#define NATIVE_HUD_CNK3D_BODY_ORIGIN_X (-0.1317f)
#define NATIVE_HUD_CNK3D_BODY_ORIGIN_Y (0.1566f)
#define NATIVE_HUD_CNK3D_ARROW_CLEARANCE 0.57f
#define NATIVE_HUD_CNK3D_ARROW_REST_ANGLE 2.1800f

struct NativeHUDCNK3DPath
{
    float x, y;
};

/* The outside-right edge of the original OBJ's Plane mesh, from green to
 * red. These are actual vertices of the author's geometry, not estimates. */
static const struct NativeHUDCNK3DPath s_nativeHUDCNK3DOuterEdge[] = {
    {-0.732405f, -1.388610f},
    {-0.367219f, -1.158423f},
    {-0.036492f, -0.885895f},
    { 0.316613f, -0.555064f},
    { 0.534512f, -0.190955f},
    { 0.683469f,  0.309030f},
    { 0.675133f,  0.786099f},
    { 0.632686f,  1.208641f},
    { 0.495479f,  1.687545f},
};
#define NATIVE_HUD_CNK3D_OUTER_COUNT \
    (sizeof(s_nativeHUDCNK3DOuterEdge)/sizeof(s_nativeHUDCNK3DOuterEdge[0]))

static TextureID s_nativeHUDCNK3DBodyTexture;
static TextureID s_nativeHUDCNK3DArrowTexture;
static int s_nativeHUDCNK3DLoadAttempted;

static TextureID NativeHUD_CNK3DLoadOne(const char *leaf)
{
    char logical[160];
    char path[1024];
    SDL_Surface *source;
    SDL_Surface *rgba;
    TextureID tex;
    u8 *pixels;

    snprintf(logical, sizeof(logical), "UI_NATIVE/%s", leaf);
    if (!NativeAssets_ResolvePath(logical, path, sizeof(path)))
    {
        Platform_LogWarn("[CNK3D] no resource: %s\n", leaf);
        return 0;
    }

    source = SDL_LoadPNG(path);
    if (source == NULL)
        return 0;
    if (source->w != NATIVE_HUD_CNK3D_BAKED_DIM ||
        source->h != NATIVE_HUD_CNK3D_BAKED_DIM)
    {
        SDL_DestroySurface(source);
        return 0;
    }
    rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (rgba == NULL)
        return 0;
    pixels = malloc(256u * 256u * 4u);
    if (pixels == NULL)
    {
        SDL_DestroySurface(rgba);
        return 0;
    }
    for (int y=0; y<256; ++y)
        memcpy(pixels+(size_t)y*256u*4u,
               (const u8 *)rgba->pixels+(size_t)y*(size_t)rgba->pitch,
               256u*4u);
    tex = NativeRenderer_CreateImageTexture(pixels, 256, 256);
    free(pixels);
    SDL_DestroySurface(rgba);
    return tex;
}

static int NativeHUD_CNK3DLoadTextures(void)
{
    if (s_nativeHUDCNK3DBodyTexture && s_nativeHUDCNK3DArrowTexture)
        return 1;
    if (s_nativeHUDCNK3DLoadAttempted)
        return 0;
    s_nativeHUDCNK3DLoadAttempted=1;
    s_nativeHUDCNK3DBodyTexture=NativeHUD_CNK3DLoadOne("cnk3d_body_v2.png");
    s_nativeHUDCNK3DArrowTexture=NativeHUD_CNK3DLoadOne("cnk3d_needle_v2.png");
    if (s_nativeHUDCNK3DBodyTexture && s_nativeHUDCNK3DArrowTexture)
    {
        Platform_Log("[CNK3D V2] loaded baked original-UV body and 3D arrow textures\n");
        return 1;
    }
    Platform_LogWarn("[CNK3D V2] missing sprite textures; fallback to CNK 2D\n");
    return 0;
}

/* Arc-length interpolation and smoothly varying tangent for consistent
 * movement/orientation. We smooth tangent between neighbouring edges to
 * avoid abrupt rotation each time the arrow reaches a polygon corner. */
static void NativeHUD_CNK3DSample(float progress, float *x, float *y,
                                  float *nx, float *ny)
{
    const int count=(int)NATIVE_HUD_CNK3D_OUTER_COUNT;
    float total=0.0f, distance;
    int segment=0;
    float local=0.0f;

    progress=fmaxf(0.0f,fminf(1.0f,progress));
    for (int i=0;i<count-1;++i)
        total += hypotf(s_nativeHUDCNK3DOuterEdge[i+1].x -
                        s_nativeHUDCNK3DOuterEdge[i].x,
                        s_nativeHUDCNK3DOuterEdge[i+1].y -
                        s_nativeHUDCNK3DOuterEdge[i].y);
    distance=progress*total;
    for (int i=0;i<count-1;++i)
    {
        float dx=s_nativeHUDCNK3DOuterEdge[i+1].x -
                 s_nativeHUDCNK3DOuterEdge[i].x;
        float dy=s_nativeHUDCNK3DOuterEdge[i+1].y -
                 s_nativeHUDCNK3DOuterEdge[i].y;
        float len=hypotf(dx,dy);
        if (distance <= len || i==count-2)
        {
            segment=i;
            local=(len>0)?fmaxf(0.0f,fminf(distance/len,1.0f)):0;
            break;
        }
        distance -= len;
    }
    const struct NativeHUDCNK3DPath *a=&s_nativeHUDCNK3DOuterEdge[segment];
    const struct NativeHUDCNK3DPath *b=&s_nativeHUDCNK3DOuterEdge[segment+1];
    *x=a->x+(b->x-a->x)*local;
    *y=a->y+(b->y-a->y)*local;

    /* Estimate tangent from the nearest path samples and smooth at turns. */
    int pa=segment>0?segment-1:segment;
    int pb=segment+2<count?segment+2:segment+1;
    float vax=b->x-s_nativeHUDCNK3DOuterEdge[pa].x;
    float vay=b->y-s_nativeHUDCNK3DOuterEdge[pa].y;
    float vbx=s_nativeHUDCNK3DOuterEdge[pb].x-a->x;
    float vby=s_nativeHUDCNK3DOuterEdge[pb].y-a->y;
    float vx=(1-local)*vax+local*vbx;
    float vy=(1-local)*vay+local*vby;
    float length=hypotf(vx,vy);
    if (length<0.00001f) {vx=b->x-a->x;vy=b->y-a->y;length=hypotf(vx,vy);}
    if (length<0.00001f) {vx=0;vy=1;length=1;}
    /* Normal points to the right/outside of green->red curve. */
    *nx=vy/length;
    *ny=-vx/length;
}

static int NativeHUD_CNK3DEmitSprite(struct GameTracker *gGT,
                                     TextureID texture, int mirrorX,
                                     float cx, float cy,
                                     float halfPx, float angle)
{
    struct PrimMem *mem=&gGT->backBuffer->primMem;
    const size_t stride=sizeof(DR_PSYX_TEX_FT4);
    if ((size_t)((char *)mem->guardEnd-(char *)mem->cursor)<stride+8u)
        return 0;

    DR_PSYX_TEX_FT4 *packet=(DR_PSYX_TEX_FT4 *)mem->cursor;
    POLY_FT4 *p;
    memset(packet,0,stride);
    setlen(packet,(int)(stride/sizeof(u32))-1);
    packet->code[0]=0xb4000000u|((u32)texture&0x00ffffffu);
    packet->code[1]=256u|(256u<<16);
    p=&packet->poly;
    setPolyFT4(p);
    setRGB0(p,128,128,128);
    p->clut=0;
    p->tpage=0;

    const float xx[4]={-halfPx,halfPx,-halfPx,halfPx};
    const float yy[4]={-halfPx,-halfPx,halfPx,halfPx};
    s16 px[4],py[4];
    const float cs=cosf(angle),sn=sinf(angle);
    for (int i=0;i<4;++i)
    {
        /* Angles live in author's +Y up plane. Screen Y is inverted.
         * Mirroring is done in screen X, never rotating the textured face. */
        float dx=cs*xx[i]+sn*yy[i];
        float dy=-sn*xx[i]+cs*yy[i];
        px[i]=(s16)lroundf(cx+(mirrorX?-dx:dx));
        py[i]=(s16)lroundf(cy+dy);
    }
    p->x0=px[0];p->y0=py[0];p->u0=0;  p->v0=0;
    p->x1=px[1];p->y1=py[1];p->u1=255;p->v1=0;
    p->x2=px[2];p->y2=py[2];p->u2=0;  p->v2=255;
    p->x3=px[3];p->y3=py[3];p->u3=255;p->v3=255;
    addPrim(gGT->pushBuffer_UI.ptrOT,packet);
    mem->cursor=(char *)packet+stride;
    return 1;
}

static void NativeHUD_DrawCNK3DSlideMeter(struct Driver *driver,
                                          const struct NativeHUDPlayerData *hud)
{
    struct GameTracker *gGT=sdata->gGT;
    int centerX,meterY,reservesY;
    int playerIndex,sideOffset,mirrorX;
    int maxRoom,progress;
    const RECT *viewport;
    float fraction,scale;
    float edgeX,edgeY,normalX,normalY;
    float arrowX,arrowY,orientation;
    float anchorX,anchorY,arrowScreenX,arrowScreenY;

    if (gGT==NULL || !hud->isDrifting)return;

    maxRoom=hud->slideMaxRoom;
    if (maxRoom<=0)maxRoom=1000;
    if (!NativeHUD_CNK3DLoadTextures())
    {
        NativeHUD_DrawCNKSlideMeter(driver,hud);
        return;
    }
    NativeHUD_GetAnchor(driver,&centerX,&meterY,&reservesY);
    playerIndex=driver->driverID;
    if (playerIndex<0 || playerIndex>=4)playerIndex=0;
    viewport=&gGT->pushBuffer[playerIndex].rect;

    mirrorX=driver->multDrift<0;
    progress=NativeHUD_Clamp(maxRoom-hud->slideRoom,0,maxRoom);
    fraction=(float)progress/(float)maxRoom;

    NativeHUD_CNK3DSample(fraction,&edgeX,&edgeY,&normalX,&normalY);
    /* Float arrow centre is OUTSIDE the path; its GOLD TIP faces INWARD
     * toward the mesh and never crosses the coloured bar. */
    arrowX=edgeX+normalX*NATIVE_HUD_CNK3D_ARROW_CLEARANCE;
    arrowY=edgeY+normalY*NATIVE_HUD_CNK3D_ARROW_CLEARANCE;
    orientation=atan2f(-normalY,-normalX)-NATIVE_HUD_CNK3D_ARROW_REST_ANGLE;

    /* Move CNK 3D slightly farther from the screen's centre without
     * moving the original CNK 2D; keep the gauge inside each viewport. */
    sideOffset=(gGT->numPlyrCurrGame>=3)?
        NATIVE_HUD_CNK_SIDE_OFFSET_SPLIT+7:NATIVE_HUD_CNK_SIDE_OFFSET+12;
    scale=(gGT->numPlyrCurrGame>=3)?5.0f:
          ((gGT->numPlyrCurrGame==2)?7.0f:9.5f);
    anchorX=(float)(centerX+(mirrorX?-sideOffset:sideOffset));
    /* CNK height is viewport-relative for split-screen; LOW untouched. */
    anchorY=(float)(viewport->y+
                    ((NativeHUD_GetCNKMeterHeight() == NATIVE_HUD_CNK_HEIGHT_HIGH)
                      ? viewport->h/2 + NATIVE_HUD_CNK_HIGH_DROP
                      : (viewport->h*3)/4));
    arrowScreenX=anchorX+(mirrorX?-1.f:1.f)*
        (arrowX-NATIVE_HUD_CNK3D_BODY_ORIGIN_X)*scale;
    arrowScreenY=anchorY-
        (arrowY-NATIVE_HUD_CNK3D_BODY_ORIGIN_Y)*scale;

    /* OT prepend means arrow first and background second: gauge background
     * is rendered first and arrow on top. Exactly two UI quads per frame. */
    if ((size_t)((char *)gGT->backBuffer->primMem.guardEnd-
                 (char *)gGT->backBuffer->primMem.cursor) <
        sizeof(DR_PSYX_TEX_FT4)*2u+8u)
        return;
    NativeHUD_CNK3DEmitSprite(gGT,s_nativeHUDCNK3DArrowTexture,
                              mirrorX,arrowScreenX,arrowScreenY,
                              (128.f*scale)/NATIVE_HUD_CNK3D_ARROW_PX_PER_WORLD,
                              orientation);
    NativeHUD_CNK3DEmitSprite(gGT,s_nativeHUDCNK3DBodyTexture,
                              mirrorX,anchorX,anchorY,
                              (128.f*scale)/NATIVE_HUD_CNK3D_BODY_PX_PER_WORLD,
                              0.0f);
}
