/*
 * Assembly Lane is a large world mesh, not a PS1-scale kart model.
 * The usual OBJ path feeds camera-relative world XYZ directly into s16 GTE
 * registers, clamps values above 32767, and drops whole faces when one
 * corner crosses the near plane. That distorts the road and opens large holes.
 *
 * Keep the same mesh/world coordinates as NativeObj_CustomTrack_WorldVertex,
 * but rotate into camera space as float, clip each authored triangle against
 * the near/far and side planes, and THEN quantize its visible corners.
 *
 * This path is only called for the Assembly Lane instance. Racing geometry,
 * collision and other OBJ assets retain their original render path.
 */
#if defined(CTR_NATIVE)
struct AssemblyClipVertex
{
    float p[3];       /* camera-space world units, including PAL/NTSC Y aspect */
    float uv[2];      /* virtual texture coordinates in [0,255] */
    float color[3];   /* already-lit native OBJ RGB */
};

static float AssemblyClip_Distance(const struct AssemblyClipVertex *v, int plane)
{
    switch (plane)
    {
    case 0: return v->p[2] - 32.0f;         /* camera near plane */
    case 1: return 32000.0f - v->p[2];    /* signed GTE Z capacity */
    case 2: return 2.0f*v->p[2] + v->p[0];
    case 3: return 2.0f*v->p[2] - v->p[0];
    case 4: return 2.0f*v->p[2] + v->p[1];
    case 5: return 2.0f*v->p[2] - v->p[1];
    case 6: return 32000.0f + v->p[0];   /* signed GTE X/Y capacity */
    case 7: return 32000.0f - v->p[0];
    case 8: return 32000.0f + v->p[1];
    default:return 32000.0f - v->p[1];
    }
}

static struct AssemblyClipVertex AssemblyClip_Lerp(
    const struct AssemblyClipVertex *a,
    const struct AssemblyClipVertex *b,
    float t)
{
    struct AssemblyClipVertex v;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (int k=0; k<3; ++k)
    {
        v.p[k] = a->p[k] + t*(b->p[k] - a->p[k]);
        v.color[k] = a->color[k] + t*(b->color[k] - a->color[k]);
    }
    for (int k=0; k<2; ++k)
        v.uv[k] = a->uv[k] + t*(b->uv[k] - a->uv[k]);
    return v;
}

/* Clipping is Sutherland-Hodgman and preserves winding, texture UVs and RGB. */
static int AssemblyClip_Triangle(
    const struct AssemblyClipVertex original[3],
    struct AssemblyClipVertex result[24])
{
    struct AssemblyClipVertex a[24], b[24];
    struct AssemblyClipVertex *src = a, *dst = b, *tmp;
    int n=3;
    unsigned int insideMask=0, outsideMask=0;
    for (int j=0; j<3; ++j)
    {
        unsigned int bitmask=0;
        a[j] = original[j];
        for (int plane=0; plane<10; ++plane)
            if (AssemblyClip_Distance(&a[j], plane) >= 0.0f)
                bitmask |= 1u<<plane;
        insideMask |= bitmask;
        outsideMask |= (~bitmask)&0x3ffu;
    }
    if (insideMask != 0x3ffu) return 0; /* entire triangle outside a plane */
    if (outsideMask == 0)
    {
        for (int j=0; j<3; ++j) result[j]=a[j];
        return 3; /* all corners inside, avoid the expensive clipper */
    }

    for (int plane=0; plane<10 && n>0; ++plane)
    {
        if (!(outsideMask & (1u<<plane))) continue;
        int out=0;
        struct AssemblyClipVertex prev=src[n-1];
        float dPrev=AssemblyClip_Distance(&prev,plane);
        for (int j=0; j<n; ++j)
        {
            const struct AssemblyClipVertex cur=src[j];
            const float dCur=AssemblyClip_Distance(&cur,plane);
            const int inPrev = dPrev >= 0.0f;
            const int inCur = dCur >= 0.0f;
            if (inPrev != inCur && out<24)
            {
                const float div=dPrev-dCur;
                const float t=fabsf(div)>1.e-12f ? dPrev/div : 0.0f;
                dst[out++]=AssemblyClip_Lerp(&prev,&cur,t);
            }
            if (inCur && out<24) dst[out++]=cur;
            prev=cur;
            dPrev=dCur;
        }
        n=out;
        tmp=src; src=dst; dst=tmp;
    }
    for (int j=0; j<n; ++j) result[j]=src[j];
    return n;
}

static u8 AssemblyClip_Color(float v)
{
    if (v<=0.0f) return 0;
    if (v>=255.0f) return 255;
    return (u8)(v+0.5f);
}

static void AssemblyClip_UV(float value, u8 *baseOut, s8 *fractionOut)
{
    if (value < 0.0f) value=0.0f;
    if (value > 255.0f) value=255.0f;
    int base=(int)floorf(value+0.5f);
    int frac=(int)lroundf((value-(float)base)*256.0f);
    if (frac>127) { ++base; frac-=256; }
    if (frac<-128) { --base; frac+=256; }
    if (base<0) { base=0; frac=0; }
    if (base>255) { base=255; frac=0; }
    *baseOut=(u8)base;
    *fractionOut=(s8)frac;
}

static void RenderBucket_DrawAssemblyLaneClipped(
    struct RenderBucketDrawContext *ctx,
    const struct NativeObjMesh *mesh)
{
    if (!ctx || !ctx->pb || !ctx->pb->ptrOT || !mesh)
        return;

    /* PB has the same world position and rotation used by retail terrain. */
    const MATRIX *view=&ctx->pb->matrix_ViewProj;
    float r[3][3];
    for (int axis=0; axis<3; ++axis)
        for (int k=0; k<3; ++k)
            r[axis][k]=(float)view->m[axis][k]*(1.0f/4096.0f);
    const float cam[3]={
        (float)ctx->pb->matrix_Camera.t[0],
        (float)ctx->pb->matrix_Camera.t[1],
        (float)ctx->pb->matrix_Camera.t[2]
    };
    const float origin[3]={
        (float)ctx->inst->matrix.t[0],
        (float)ctx->inst->matrix.t[1],
        (float)ctx->inst->matrix.t[2]
    };
    const float size[3]={
        mesh->extent[0]*(1000.0f/4096.0f),
        mesh->extent[1]*(1000.0f/4096.0f),
        mesh->extent[2]*(1000.0f/4096.0f)
    };
    /* A lightweight verification trace: confirms that the actual test build
       takes this presentation path with the recentered world and camera. */
    static unsigned int assemblyDrawCount=0;
    ++assemblyDrawCount;
    if (assemblyDrawCount==1 || (assemblyDrawCount%2500)==0)
        Platform_Log("[CTR AssemblyLane] render active frame=%u cam=(%.0f,%.0f,%.0f) origin=(%.0f,%.0f,%.0f) faces=%d\n",
                     assemblyDrawCount,cam[0],cam[1],cam[2],
                     origin[0],origin[1],origin[2],mesh->triangleCount);

    /* Set the viewport explicitly: QueueDraw bypasses the retail one-OBJ
     * bounding-box projection for this giant static world. */
    gte_SetGeomOffset(ctx->pb->rect.w >> 1, ctx->pb->rect.h >> 1);
    gte_SetGeomScreen(ctx->pb->distanceToScreen_PREV);

    /* Camera rotation and perspective are now separate. Identity GTE
       consumes clipped camera-space points, no s16 world-coordinate overflow. */
    MATRIX identity={0};
    identity.m[0][0]=4096;
    identity.m[1][1]=4096;
    identity.m[2][2]=4096;
    gte_SetRotMatrix(&identity);
    gte_SetTransMatrix(&identity);

    unsigned int submittedFaces=0;
    unsigned int cameraVisibleFaces=0;
    for (int i=0; i<mesh->triangleCount; ++i)
    {
        const struct NativeObjTriangle *t=&mesh->triangles[i];
        struct AssemblyClipVertex vertices[3], clipped[24];
        for (int j=0; j<3; ++j)
        {
            float rel[3];
            for (int axis=0; axis<3; ++axis)
                rel[axis]=t->v[j].renderP[axis]*size[axis]+origin[axis]-cam[axis];
            for (int axis=0; axis<3; ++axis)
                vertices[j].p[axis]=r[axis][0]*rel[0]+r[axis][1]*rel[1]+r[axis][2]*rel[2];
            for (int axis=0; axis<3; ++axis)
                vertices[j].color[axis]=(float)t->v[j].renderColor[axis];
            for (int axis=0; axis<2; ++axis)
                vertices[j].uv[axis]=(float)t->v[j].renderUV[axis]+
                    (float)t->v[j].renderUVFrac[axis]*(1.0f/256.0f);
        }
        const int count=AssemblyClip_Triangle(vertices,clipped);
        if (count<3) continue;
        ++cameraVisibleFaces;

        /* A clipped source triangle can generate a 3-12 vertex convex
           polygon. Fan triangulation retains the original face materials. */
        for (int f=1; f+1<count; ++f)
        {
            const struct AssemblyClipVertex *v[3]={
                &clipped[0], &clipped[f], &clipped[f+1]
            };
            u32 packed[3], dep[3];
            s32 x16[3], y16[3];
            for (int j=0; j<3; ++j)
            {
                const s16 x=(s16)lroundf(v[j]->p[0]);
                const s16 y=(s16)lroundf(v[j]->p[1]);
                const s16 z=(s16)lroundf(v[j]->p[2]);
                MTC2(RenderBucket_PackXY(x,y),0);
                MTC2((u16)z,1);
                gte_rtps();
                packed[j]=MFC2(14);
                dep[j]=(u16)MFC2(19);
                if (!NativeGTE_GetCurrentSubpixelFixed(2,&x16[j],&y16[j]))
                {
                    x16[j]=(s32)(s16)packed[j]*65536;
                    y16[j]=(s32)(s16)(packed[j]>>16)*65536;
                }
            }
            if (dep[0]<16 || dep[1]<16 || dep[2]<16)
                continue;
            const double ax=(double)x16[1]-(double)x16[0];
            const double ay=(double)y16[1]-(double)y16[0];
            const double bx=(double)x16[2]-(double)x16[0];
            const double by=(double)y16[2]-(double)y16[0];
            const double area=ax*by-ay*bx;
            /* Unlike a kart, a large static level has floors, ramps and thin
             * barriers whose back sides must remain visible when the camera
             * changes elevation or crosses a surface. The legacy OBJ NCLIP
             * rejection could hide a full stretch of track at some angles.
             * Only reject zero-area triangles; the camera frustum and PSY-X
             * depth buffer already constrain the visible surface. */
            if (area == 0.0)
                continue;

            u32 far=dep[0];
            if (dep[1]>far) far=dep[1];
            if (dep[2]>far) far=dep[2];
            int slot=(int)(far>>6);
            if (slot>0x3ff) slot=0x3ff;
            u32 *ot=ctx->pb->ptrOT+slot;

            if (t->renderTextured)
            {
                const size_t bytes=sizeof(DR_PSYX_TEX_GT3);
                if ((char *)ctx->primMem->cursor+bytes >=
                    (char *)ctx->primMem->guardEnd)
                {
                    static unsigned int warnings=0;
                    if (warnings++ < 6)
                        Platform_Log("[CTR AssemblyLane] PRIMMEM_FULL: face=%d submitted=%u cap=%u\n",
                                     i, submittedFaces, ctx->primMem->capacityBytes);
                    return;
                }
                DR_PSYX_TEX_GT3 *packet=(DR_PSYX_TEX_GT3 *)ctx->primMem->cursor;
                packet->code[0]=0xb3000000 | (t->renderTexture & 0xffffff);
                packet->code[1]=256 | (256<<16);
                packet->nativeFlags=
                    DR_PSYX_TEX_GT3_FLAG_HIRES_UV |
                    DR_PSYX_TEX_GT3_FLAG_EXACT_METADATA;
                POLY_GT3 *p=&packet->poly;
                p->tag=0; p->clut=0; p->tpage=0;
                p->p1=0; p->p2=0; p->pad2=0;
                setPolyGT3(p);
                VERTTYPE *xy[3][2]={
                    {&p->x0,&p->y0},{&p->x1,&p->y1},{&p->x2,&p->y2}
                };
                u8 *cols[3]={&p->r0,&p->r1,&p->r2};
                u8 *uv[3]={&p->u0,&p->u1,&p->u2};
                for (int j=0; j<3; ++j)
                {
                    CtrGpu_WritePackedXY(xy[j][0],packed[j]);
                    RenderBucket_EncodeProjectionAxis(x16[j],xy[j][0],&packet->xyFrac[j][0]);
                    RenderBucket_EncodeProjectionAxis(y16[j],xy[j][1],&packet->xyFrac[j][1]);
                    packet->depth[j]=dep[j];
                    for (int k=0; k<3; ++k)
                        cols[j][k]=AssemblyClip_Color(v[j]->color[k]);
                    for (int k=0; k<2; ++k)
                        AssemblyClip_UV(v[j]->uv[k],&uv[j][k],&packet->uvFrac[j][k]);
                }
                RenderBucket_LinkPrimRaw(
                    ot,packet,((u32)(sizeof(*packet)/sizeof(u32)-1))<<24);
                ctx->primMem->cursor=(char *)packet+bytes;
                ++submittedFaces;
            }
            else
            {
                const size_t bytes=sizeof(POLY_G3);
                if ((char *)ctx->primMem->cursor+bytes >=
                    (char *)ctx->primMem->guardEnd)
                {
                    static unsigned int warnings=0;
                    if (warnings++ < 6)
                        Platform_Log("[CTR AssemblyLane] PRIMMEM_FULL: face=%d submitted=%u cap=%u\n",
                                     i, submittedFaces, ctx->primMem->capacityBytes);
                    return;
                }
                POLY_G3 *p=(POLY_G3 *)ctx->primMem->cursor;
                p->tag=0; p->pad1=0; p->pad2=0;
                setPolyG3(p);
                VERTTYPE *xy[3][2]={
                    {&p->x0,&p->y0},{&p->x1,&p->y1},{&p->x2,&p->y2}
                };
                u8 *cols[3]={&p->r0,&p->r1,&p->r2};
                for (int j=0; j<3; ++j)
                {
                    NativeGTE_SaveSXYStoreExact(xy[j][0],packed[j],x16[j],y16[j],dep[j]);
                    for (int k=0; k<3; ++k)
                        cols[j][k]=AssemblyClip_Color(v[j]->color[k]);
                }
                RenderBucket_LinkPrimRaw(ot,p,0x06000000);
                ctx->primMem->cursor=(char *)p+bytes;
                ++submittedFaces;
            }
        }
    }
    if (assemblyDrawCount==1 || (assemblyDrawCount%250)==0)
        Platform_Log("[CTR AssemblyLane] face clip: frame=%u candidates=%u submitted=%u total=%d prim_capacity=%u\n",
                     assemblyDrawCount, cameraVisibleFaces, submittedFaces,
                     mesh->triangleCount, ctx->primMem->capacityBytes);
}
#endif /* CTR_NATIVE */