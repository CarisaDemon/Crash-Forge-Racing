#include <common.h>

/* Strictly optional internal QA. Production and default sky PS1 packet
 * generation have no extra work or allocations. */

struct DrawSkyContext
{
	const struct ShortVertex *verts;
	uint32_t *ot;
	u32 screenBounds;
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
    int cacheMode;
    int cacheReady;
    int cacheVertices;
    const struct SkyboxFace *lastFace;
#endif
};

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
struct DrawSkyProjectedVertex
{
    u32 sxy;
    u8 valid;
};
static struct DrawSkyProjectedVertex *s_skyProjected;
static int s_skyProjectedCapacity;
static u64 s_skyCacheEligibleFaces;
static u64 s_skyCacheMismatchFaces;
static u64 s_skyCacheFastFaces;
static u64 s_skyCacheFallbackFaces;
static u64 s_skyCacheSeenFaces;
static u64 s_skyCacheSeenVertices;
static int s_skyCacheFrames;

static int DrawSky_CacheMode(void)
{
    static int checked,mode;
    if(!checked)
    {
        FILE *f=fopen("debug/sky_vertex_cache_qa.txt","rb");
        checked=1;
        if(f)
        {
            int requested=0;
            if(fscanf(f,"%d",&requested)==1 && (requested==1||requested==2))
                mode=requested;
            fclose(f);
            if(mode)
                Platform_Log("[SKY CACHE QA] optional PS1 sky vertex batch mode=%d\n",mode);
        }
    }
    return NativePerf_IsStressTestMeasuring()?mode:0;
}

static void DrawSky_ProjectCacheBatch(
    const struct Skybox *sky,const int indices[3],int count)
{
    /* Reuse one PS1 RTPT for up to three unique vertices, and retain only
     * all-flag-clear values. Odd remaining lanes repeat the final index. */
    const int aIndex=indices[0];
    const int bIndex=indices[count>1?1:0];
    const int cIndex=indices[count>2?2:count-1];
    const struct ShortVertex *a=&sky->ptrVertex[aIndex];
    const struct ShortVertex *b=&sky->ptrVertex[bIndex];
    const struct ShortVertex *c=&sky->ptrVertex[cIndex];
    MTC2(*(const u32 *)(const void *)&a->Position,0);
    MTC2(*((const u32 *)(const void *)&a->Position+1),1);
    MTC2(*(const u32 *)(const void *)&b->Position,2);
    MTC2(*((const u32 *)(const void *)&b->Position+1),3);
    MTC2(*(const u32 *)(const void *)&c->Position,4);
    MTC2(*((const u32 *)(const void *)&c->Position+1),5);
    gte_rtpt_b();
    const u8 good=(u8)(CFC2(31)==0);
    s_skyProjected[aIndex].sxy=MFC2(12);
    s_skyProjected[aIndex].valid=good;
    if(count>1)
    {
        s_skyProjected[bIndex].sxy=MFC2(13);
        s_skyProjected[bIndex].valid=good;
    }
    if(count>2)
    {
        s_skyProjected[cIndex].sxy=MFC2(14);
        s_skyProjected[cIndex].valid=good;
    }
}

static int DrawSky_BuildProjected(
    const struct Skybox *sky,u32 baseFaceOffset,u32 baseCountOffset)
{
    const int n=sky->numVertex;
    if(n<=0 || n>5461 || !sky->ptrVertex)return 0;
    if(s_skyProjectedCapacity<n)
    {
        struct DrawSkyProjectedVertex *p=(struct DrawSkyProjectedVertex *)
            realloc(s_skyProjected,(size_t)n*sizeof(*p));
        if(!p)return 0;
        s_skyProjected=p;
        s_skyProjectedCapacity=n;
    }
    /* There are eight authored skybox sectors; retail draws four per camera
     * azimuth. Most skybox vertices are unused this frame. Read only the
     * same four sector lists as DrawSky_Full and flag referenced indices. */
    for(int i=0;i<n;++i)s_skyProjected[i].valid=0;
    static const u32 faceOffsets[4]={0u,4u,28u,24u};
    static const u32 countOffsets[4]={0u,2u,14u,12u};
    const u32 stride=(u32)sizeof(struct ShortVertex);
    for(int sector=0;sector<4;++sector)
    {
        const u32 fi=((baseFaceOffset+faceOffsets[sector])&0x1cu)>>2;
        const u32 ci=((baseCountOffset+countOffsets[sector])&0xeu)>>1;
        const u32 count=(u16)sky->numFaces[ci];
        const struct SkyboxFace *faces=sky->ptrFaces[fi];
        if(count>8192 || (count && faces==NULL))return 0;
        for(u32 j=0;j<count;++j)
        {
            const struct SkyboxFace *f=&faces[j];
            const u32 offsets[3]={f->A,f->B,f->C};
            for(int v=0;v<3;++v)
            {
                const u32 offset=offsets[v];
                if(offset%stride==0 && offset/stride<(u32)n)
                    s_skyProjected[offset/stride].valid=2;
            }
        }
    }
    int packed[3]={0,0,0},count=0;
    int unique=0;
    for(int i=0;i<n;++i)
    {
        if(s_skyProjected[i].valid!=2)continue;
        packed[count++]=i;
        ++unique;
        if(count==3)
        {
            DrawSky_ProjectCacheBatch(sky,packed,3);
            count=0;
        }
    }
    if(count)DrawSky_ProjectCacheBatch(sky,packed,count);
    s_skyCacheSeenVertices+=(u64)unique;
    return 1;
}
static int DrawSky_CachedIndices(
    const struct DrawSkyContext *ctx,const struct SkyboxFace *face,
    int *i0,int *i1,int *i2)
{
    const int stride=(int)sizeof(struct ShortVertex);
    if(!ctx->cacheReady || (face->A%stride)||(face->B%stride)||(face->C%stride))
        return 0;
    *i0=face->A/stride;
    *i1=face->B/stride;
    *i2=face->C/stride;
    if(*i0>=ctx->cacheVertices || *i1>=ctx->cacheVertices ||
       *i2>=ctx->cacheVertices)
        return 0;
    return s_skyProjected[*i0].valid &&
           s_skyProjected[*i1].valid &&
           s_skyProjected[*i2].valid;
}

static void DrawSky_LogCacheFrame(void)
{
    if(++s_skyCacheFrames%1200==0)
    {
        Platform_Log("[SKY CACHE QA] frames=1200 vertices=%llu faces=%llu eligible=%llu fast=%llu fallback=%llu mismatches=%llu\n",
          (unsigned long long)s_skyCacheSeenVertices,
          (unsigned long long)s_skyCacheSeenFaces,
          (unsigned long long)s_skyCacheEligibleFaces,
          (unsigned long long)s_skyCacheFastFaces,
          (unsigned long long)s_skyCacheFallbackFaces,
          (unsigned long long)s_skyCacheMismatchFaces);
        s_skyCacheSeenVertices=s_skyCacheSeenFaces=s_skyCacheEligibleFaces=
            s_skyCacheFastFaces=s_skyCacheFallbackFaces=s_skyCacheMismatchFaces=0;
    }
}
#endif


struct DrawSkyScratch
{
	u32 baseFaceOffset;
	u32 baseCountOffset;
};

CTR_STATIC_ASSERT(sizeof(struct DrawSkyScratch) == 0x8);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseFaceOffset) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseCountOffset) == 0x4);

static u32 DrawSky_ReadWord(const void *base, int offset)
{
	return *(const u32 *)(const void *)((const char *)base + offset);
}

static int DrawSky_IsVisible(u32 gteFlag, u32 sxy0, u32 sxy1, u32 sxy2, u32 screenBounds)
{
	u32 overlap;
	u32 bounds;

	if (((gteFlag << 13) >> 29) != 0)
	{
		return 0;
	}

	overlap = sxy0 & sxy1 & sxy2;
	bounds = ~((sxy0 - screenBounds) | (sxy1 - screenBounds) | (sxy2 - screenBounds)) | overlap;
	if ((s32)bounds < 0)
	{
		return 0;
	}

	return (s32)(bounds << 16) >= 0;
}

static void DrawSky_LoadFaceVertices(struct DrawSkyContext *ctx, const struct SkyboxFace *face)
{
	const char *verts = (const char *)ctx->verts;
	const struct ShortVertex *a = (const struct ShortVertex *)(const void *)(verts + face->A);
	const struct ShortVertex *b = (const struct ShortVertex *)(const void *)(verts + face->B);
	const struct ShortVertex *c = (const struct ShortVertex *)(const void *)(verts + face->C);

	MTC2(DrawSky_ReadWord(&a->Position, 0x0), 0);
	MTC2(DrawSky_ReadWord(&a->Position, 0x4), 1);
	MTC2(DrawSky_ReadWord(&a->Color, 0x0), 20);

	MTC2(DrawSky_ReadWord(&b->Position, 0x0), 2);
	MTC2(DrawSky_ReadWord(&b->Position, 0x4), 3);
	MTC2(DrawSky_ReadWord(&b->Color, 0x0), 21);

	MTC2(DrawSky_ReadWord(&c->Position, 0x0), 4);
	MTC2(DrawSky_ReadWord(&c->Position, 0x4), 5);
	MTC2(DrawSky_ReadWord(&c->Color, 0x0), 22);
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
static void DrawSky_EmitPrimitiveCached(
    u32 **primCursor,uint32_t *ot,const struct ShortVertex *a,
    const struct ShortVertex *b,const struct ShortVertex *c,
    u32 sxy0,u32 sxy1,u32 sxy2)
{
    POLY_G3 *poly=(POLY_G3 *)*primCursor;
    CtrGpu_WriteColorCode(&poly->r0,DrawSky_ReadWord(&a->Color,0));
    CtrGpu_WritePackedXY(&poly->x0,sxy0);
    CtrGpu_WriteColorCode(&poly->r1,DrawSky_ReadWord(&b->Color,0));
    CtrGpu_WritePackedXY(&poly->x1,sxy1);
    CtrGpu_WriteColorCode(&poly->r2,DrawSky_ReadWord(&c->Color,0));
    CtrGpu_WritePackedXY(&poly->x2,sxy2);
    CtrGpu_LinkPacket24(ot,&poly->tag,poly,0x06000000);
    *primCursor=(u32 *)(poly+1);
}
#endif

static void DrawSky_EmitPrimitive(u32 **primCursor, uint32_t *ot)
{
	POLY_G3 *poly = (POLY_G3 *)*primCursor;

	CtrGpu_WriteColorCode(&poly->r0, MFC2(20));
	CtrGpu_WritePackedXY(&poly->x0, MFC2(12));
	CtrGpu_WriteColorCode(&poly->r1, MFC2(21));
	CtrGpu_WritePackedXY(&poly->x1, MFC2(13));
	CtrGpu_WriteColorCode(&poly->r2, MFC2(22));
	CtrGpu_WritePackedXY(&poly->x2, MFC2(14));
	CtrGpu_LinkPacket24(ot, &poly->tag, poly, 0x06000000);

	*primCursor = (u32 *)(poly + 1);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80069cc4-0x80069e70
static u32 *DrawSky_Piece(struct Skybox *skybox, struct DrawSkyContext *ctx, int faceIndex, int countIndex, u32 *prim)
{
	u32 numFaces = (u16)skybox->numFaces[countIndex];
	const struct SkyboxFace *face = skybox->ptrFaces[faceIndex];

	if (numFaces == 0)
	{
		return prim;
	}

	for (u32 i = 0; i < numFaces; i++, face++)
	{
		uint32_t *ot = (uint32_t *)(void *)((char *)ctx->ot + (s16)face->D);
		u32 sxy0;
		u32 sxy1;
		u32 sxy2;
		u32 gteFlag;

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
        int ca=0,cb=0,cc=0;
        const int cacheValid=ctx->cacheMode && DrawSky_CachedIndices(
            ctx,face,&ca,&cb,&cc);
        if(ctx->cacheMode)
        {
            ++s_skyCacheSeenFaces;
            if(cacheValid)++s_skyCacheEligibleFaces;
            ctx->lastFace=face;
        }
        if(cacheValid && ctx->cacheMode==2)
        {
            ++s_skyCacheFastFaces;
            sxy0=s_skyProjected[ca].sxy;
            sxy1=s_skyProjected[cb].sxy;
            sxy2=s_skyProjected[cc].sxy;
            if(DrawSky_IsVisible(0,sxy0,sxy1,sxy2,ctx->screenBounds))
                DrawSky_EmitPrimitiveCached(&prim,ot,
                    &ctx->verts[ca],&ctx->verts[cb],&ctx->verts[cc],
                    sxy0,sxy1,sxy2);
            continue;
        }
        if(ctx->cacheMode==2)
            ++s_skyCacheFallbackFaces;
#endif
		DrawSky_LoadFaceVertices(ctx, face);
		gte_rtpt_b();

		sxy0 = MFC2(12);
		gteFlag = CFC2(31);
		sxy1 = MFC2(13);
		sxy2 = MFC2(14);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
        if(cacheValid && ctx->cacheMode==1)
        {
            s_skyCacheMismatchFaces +=
                (sxy0!=s_skyProjected[ca].sxy ||
                 sxy1!=s_skyProjected[cb].sxy ||
                 sxy2!=s_skyProjected[cc].sxy ||
                 gteFlag!=0);
        }
#endif

		if (DrawSky_IsVisible(gteFlag, sxy0, sxy1, sxy2, ctx->screenBounds))
		{
			DrawSky_EmitPrimitive(&prim, ot);
		}
	}

	return prim;
}

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
/* Some community-imported LEV files repeat exactly the same large opaque
 * G3 sky mesh in all eight sector lists. Rendering four byte-identical
 * opaque copies is wasteful: they write the exact same pixels, but incur
 * repeated PS1 GTE work and packet submission. Only skip when the four
 * selected lists and their vertices are confirmed identical and opaque.
 * Original retail skies and all non-matching community skies remain intact. */
static int DrawSky_SelectedSectorsIdenticalOpaque(
    const struct Skybox *sky, u32 baseFaceOffset)
{
    const int first = (int)((baseFaceOffset & 0x1cu) >> 2);
    const int indices[4] = {
        first, (first + 1) & 7, (first + 7) & 7, (first + 6) & 7
    };
    const int count = (int)sky->numFaces[first];
    /* Tiny retail sector lists do not benefit enough from the checks;
     * leave them on their original draw path. */
    if (count < 512 || count > 8192 ||
        !sky->ptrVertex || !sky->ptrFaces[first] ||
        sky->numVertex <= 0 || sky->numVertex > 5461)
        return 0;
    const size_t bytes = (size_t)count * sizeof(struct SkyboxFace);
    /* G3 0x30 is fully opaque; repeated semitransparent sky faces
     * must NOT be removed because alpha blending is not idempotent. */
    for (int i = 0; i < sky->numVertex; ++i)
    {
        const u8 code = ((const u8 *)(const void *)&sky->ptrVertex[i].Color)[3];
        if (code != 0x30) return 0;
    }
    for (int j = 1; j < 4; ++j)
    {
        const int ix = indices[j];
        if ((int)sky->numFaces[ix] != count || !sky->ptrFaces[ix])
            return 0;
        if (sky->ptrFaces[ix] != sky->ptrFaces[first] &&
            memcmp(sky->ptrFaces[ix], sky->ptrFaces[first], bytes) != 0)
            return 0;
    }
    return 1;
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80069bb0-0x80069cc4
void DrawSky_Full(void *skybox, struct PushBuffer *pb, struct PrimMem *primMem)
{
	struct Skybox *sky = skybox;
	u32 *prim = (u32 *)primMem->cursor;

	// NOTE(aalhendi): PSX-backfeed blocker: retail saves/restores ra and s0-s2 in scratchpad 0x00-0x0c.
	// Native C relies on the host ABI because the only retail data temporaries live in 0x10 and 0x14 and are explicit below.
	if (sky != NULL)
	{
		struct DrawSkyScratch *scratch = CTR_SCRATCHPAD_PTR(struct DrawSkyScratch, 0x10);
		struct DrawSkyContext ctx;
		u32 baseFaceOffset;
		u32 baseCountOffset;
		int faceIndex;
		int countIndex;

		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x00), 0);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x04), 1);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x08), 2);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x0c), 3);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x10), 4);
		CTC2(0, 5);
		CTC2(0, 6);
		CTC2(0, 7);

		baseFaceOffset = ((DrawSky_ReadWord(pb, 0x08) + 0x500) >> 7) & 0x1c;
		baseCountOffset = (baseFaceOffset >> 1) & 0xe;

		scratch->baseFaceOffset = baseFaceOffset;
		scratch->baseCountOffset = baseCountOffset;

		ctx.verts = sky->ptrVertex;
		ctx.ot = &pb->ptrOT[0x3ff];
		ctx.screenBounds = DrawSky_ReadWord(pb, 0x20);
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
        const int dedupSky =
            DrawSky_SelectedSectorsIdenticalOpaque(sky,baseFaceOffset);
        /* Keep the optional experimental projection cache disabled by default:
         * repeated stress runs did not show a consistent additional gain. */
        ctx.cacheMode=DrawSky_CacheMode();
        ctx.cacheVertices=sky->numVertex;
        ctx.cacheReady=0;
        ctx.lastFace=NULL;
        if(ctx.cacheMode)
            ctx.cacheReady=DrawSky_BuildProjected(sky,baseFaceOffset,baseCountOffset);
#endif

		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
        if (!dedupSky)
#endif
        {
		baseFaceOffset = (baseFaceOffset + 4) & 0x1c;
		baseCountOffset = (baseCountOffset + 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 4) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 8) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 4) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);
        }
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
        if(ctx.cacheMode)
        {
            /* Restore the exact retail last triangle's COP2 state. Other
             * drawing code sometimes observes GTE state after sky rendering. */
            if(ctx.cacheMode==2 && ctx.lastFace)
            {
                DrawSky_LoadFaceVertices(&ctx,ctx.lastFace);
                gte_rtpt_b();
            }
            DrawSky_LogCacheFrame();
        }
#endif
	}

	primMem->cursor = prim;
}
