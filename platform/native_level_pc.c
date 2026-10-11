/* Crash Forge: Racing - native GPU track mesh groundwork.
 * Build one immutable world-space VBO per retail level, and collect visible
 * quadblocks without generating more PS1 OT packets. Before replacing retail
 * output, this opt-in overlay allows QA of camera, winding and LOD:
 * debug/level_pc_preview.enable, which intentionally does NOT hide legacy.
 * Default drawing still uses exact retail PS1 translation.
 *
 * This file is included by main.c immediately after native_renderer.c; GL
 * context and GLSL 1.40 shader symbols are available through native_glad.
 */
#if defined(CTR_NATIVE)

#define NATIVE_LEVEL_PC_VERTS_PER_BLOCK (24)
#define NATIVE_LEVEL_PC_MAX_QUEUED (512) /* GPU-only preview: more BSP-visible blocks */
#define NATIVE_LEVEL_PC_MAX_BLOCKS (131072)
#define NATIVE_LEVEL_PC_TEXTURE_MODES (3)
#define NATIVE_LEVEL_PC_FACE_MODES (NATIVE_LEVEL_PC_TEXTURE_MODES * 2)
#define NATIVE_LEVEL_PC_MATERIAL_BUCKETS (NATIVE_LEVEL_PC_FACE_MODES * 3)

struct NativeLevelPCMesh
{
	const void *meshKey;
	GLuint vbo;
	GLuint vao;
	GLuint debugProgram;
	GLint uViewMatrix;
	GLint uViewTranslation;
	GLint uScreen;
	GLint uViewport;
	GLint uWide;
	GLuint textureProgram[NATIVE_LEVEL_PC_TEXTURE_MODES];
	GLint texViewMatrix[NATIVE_LEVEL_PC_TEXTURE_MODES];
	GLint texViewTranslation[NATIVE_LEVEL_PC_TEXTURE_MODES];
	GLint texScreen[NATIVE_LEVEL_PC_TEXTURE_MODES];
	GLint texViewport[NATIVE_LEVEL_PC_TEXTURE_MODES];
	GLint texWide[NATIVE_LEVEL_PC_TEXTURE_MODES];
	u8 *faceType; /* block index * 4 + face index; 0..2 or 255 disabled */
	u32 *queueStamp; /* index -> last frame's stamp, allocated once per level */
	u32 queueGeneration;
	int blockCount;
	int valid;
	int queueCount;
	int queued[NATIVE_LEVEL_PC_MAX_QUEUED];
	float matrix[9];
	float translation[3];
	int screen;
	int viewportW;
	int viewportH;
	int wide;
};

static struct NativeLevelPCMesh s_nativeLevelPC;
static GLuint s_nativePCDepthRbo;
static int s_nativePCDepthRboWidth, s_nativePCDepthRboHeight;
static GLuint s_nativePCCompositeFbo, s_nativePCCompositeTex;
static int s_nativePCCompositeWidth, s_nativePCCompositeHeight;

static int NativeRenderer_LevelPCPreviewMode(void)
{
	static int checked;
	static int mode;
	if (!checked)
	{
		FILE *textured = fopen("debug/level_pc_material_preview.enable", "rb");
		FILE *wire = NULL;
		if (textured != NULL)
		{
			mode = 2;
			fclose(textured);
		}
		else
		{
			wire = fopen("debug/level_pc_preview.enable", "rb");
			if (wire != NULL) {mode = 1; fclose(wire);}
		}
        if(mode==0 && NativeRenderer_LevelPCProjectionQAEnabled())
            mode=3; /* hidden transform feedback only, no framebuffer draw */
		checked = 1;
	}
	return mode;
}

/* Explicitly opted-in human QA only: the game-side gate also restricts this
 * to Oxide Station single player. This flag is never set by normal gameplay. */
int NativeRenderer_LevelPCManualPlaytest(void)
{
    static int checked,requested;
    if (!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_manual_playtest.enable","rb");
        requested=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(requested)
            Platform_Log("[LEVEL PC GPU] manual test requested (Oxide Station 1P only)\n");
    }
    return requested;
}

int NativeRenderer_LevelPCEnabled(void)
{
	/* Stable Windows builds always retain original PS1 terrain OT rendering.
	 * Only a separately compiled experimental build may enable native PC
	 * terrain, irrespective of any stale debug marker left on disk. */
#if defined(CTR_GPU_EXPERIMENTAL_RENDER)
	return NativeRenderer_LevelPCPreviewMode() != 0;
#else
	return 0;
#endif
}

void NativeRenderer_LevelPCReset(void)
{
	NativeRenderer_LevelPCProjectionQAReset();
	NativeRenderer_LevelPCMidpointQAReset();
	if (s_nativePCCompositeFbo != 0)
		glDeleteFramebuffers(1, &s_nativePCCompositeFbo);
	if (s_nativePCCompositeTex != 0)
		glDeleteTextures(1, &s_nativePCCompositeTex);
	s_nativePCCompositeFbo=s_nativePCCompositeTex=0;
	s_nativePCCompositeWidth=s_nativePCCompositeHeight=0;
	if (s_nativePCDepthRbo != 0)
		glDeleteRenderbuffers(1, &s_nativePCDepthRbo);
	s_nativePCDepthRbo=0;
	s_nativePCDepthRboWidth=s_nativePCDepthRboHeight=0;
	if (s_nativeLevelPC.vbo != 0) glDeleteBuffers(1, &s_nativeLevelPC.vbo);
	if (s_nativeLevelPC.vao != 0) glDeleteVertexArrays(1, &s_nativeLevelPC.vao);
	if (s_nativeLevelPC.debugProgram != 0) glDeleteProgram(s_nativeLevelPC.debugProgram);
	for (int i = 0; i < NATIVE_LEVEL_PC_TEXTURE_MODES; ++i)
		if (s_nativeLevelPC.textureProgram[i]) glDeleteProgram(s_nativeLevelPC.textureProgram[i]);
	free(s_nativeLevelPC.faceType);
	free(s_nativeLevelPC.queueStamp);
	memset(&s_nativeLevelPC, 0, sizeof(s_nativeLevelPC));
}

int NativeRenderer_LevelPCUpload(const void *meshKey, const NativeLevelPCVertex *vertices, const u8 *faceTypes,
	int vertexCount, int blockCount)
{
	const size_t sizeBytes = (size_t)vertexCount * sizeof(NativeLevelPCVertex);
	GLint oldVao, oldArrayBuffer;
	if (!NativeRenderer_LevelPCEnabled())
		return 0;
	if (meshKey == NULL || vertices == NULL || faceTypes == NULL || blockCount < 1 ||
	    blockCount > NATIVE_LEVEL_PC_MAX_BLOCKS ||
	    vertexCount != blockCount * NATIVE_LEVEL_PC_VERTS_PER_BLOCK)
		return 0;

	/* Preserve preview shader if one was compiled in this context. */
	if (s_nativeLevelPC.vbo != 0)
		glDeleteBuffers(1, &s_nativeLevelPC.vbo);
	if (s_nativeLevelPC.vao != 0)
		glDeleteVertexArrays(1, &s_nativeLevelPC.vao);
	s_nativeLevelPC.vbo = 0;
	s_nativeLevelPC.vao = 0;
	s_nativeLevelPC.valid = 0;
	free(s_nativeLevelPC.faceType);
	free(s_nativeLevelPC.queueStamp);
	s_nativeLevelPC.faceType = NULL;
	s_nativeLevelPC.queueStamp = NULL;
	s_nativeLevelPC.queueGeneration = 1;
	s_nativeLevelPC.queueCount = 0;

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
	glGenBuffers(1, &s_nativeLevelPC.vbo);
	glGenVertexArrays(1, &s_nativeLevelPC.vao);
	if (s_nativeLevelPC.vbo == 0 || s_nativeLevelPC.vao == 0)
	{
		glBindVertexArray((GLuint)oldVao);
		glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldArrayBuffer);
		return 0;
	}

	glBindVertexArray(s_nativeLevelPC.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_nativeLevelPC.vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeBytes, vertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(a_position);
	glEnableVertexAttribArray(a_color);
	glEnableVertexAttribArray(a_texcoord);
	glVertexAttribPointer(a_position, 4, GL_FLOAT, GL_FALSE,
		sizeof(NativeLevelPCVertex), (void *)offsetof(NativeLevelPCVertex, x));
	glVertexAttribPointer(a_texcoord, 4, GL_FLOAT, GL_FALSE,
		sizeof(NativeLevelPCVertex), (void *)offsetof(NativeLevelPCVertex, u));
	glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE,
		sizeof(NativeLevelPCVertex), (void *)offsetof(NativeLevelPCVertex, r));
	glBindVertexArray((GLuint)oldVao);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldArrayBuffer);

	s_nativeLevelPC.faceType = (u8 *)malloc((size_t)blockCount * 4u);
	if (s_nativeLevelPC.faceType == NULL)
	{
		glDeleteBuffers(1, &s_nativeLevelPC.vbo);
		glDeleteVertexArrays(1, &s_nativeLevelPC.vao);
		s_nativeLevelPC.vbo = s_nativeLevelPC.vao = 0;
		return 0;
	}
	memcpy(s_nativeLevelPC.faceType, faceTypes, (size_t)blockCount * 4u);
	s_nativeLevelPC.queueStamp = (u32 *)calloc((size_t)blockCount, sizeof(u32));
	s_nativeLevelPC.meshKey = meshKey;
	s_nativeLevelPC.blockCount = blockCount;
	s_nativeLevelPC.valid = 1;
	Platform_Log("[LEVEL PC GPU] static VBO uploaded: blocks=%d vertices=%d bytes=%u (material-ready preview)\n",
		blockCount, vertexCount, (unsigned)sizeBytes);
	return 1;
}

void NativeRenderer_LevelPCBeginFrame(
	const void *meshKey,
	const s16 viewM[3][3],
	const s32 viewT[3],
	int geomScreen, int viewportW, int viewportH, int wide)
{
	struct NativeLevelPCMesh *pc = &s_nativeLevelPC;
	pc->queueCount = 0;
	if (++pc->queueGeneration == 0)
	{
		if (pc->queueStamp)
			memset(pc->queueStamp,0,(size_t)pc->blockCount*sizeof(u32));
		pc->queueGeneration=1;
	}
	if (!pc->valid || !NativeRenderer_LevelPCEnabled() ||
	    meshKey != pc->meshKey || viewM == NULL || viewT == NULL ||
	    geomScreen <= 0 || viewportW <= 0 || viewportH <= 0)
		return;

	/* GL uses column-major matrices, GTE stores rows. */
	for (int row = 0; row < 3; ++row)
	{
		pc->translation[row] = (float)viewT[row];
		for (int col = 0; col < 3; ++col)
			pc->matrix[col * 3 + row] = (float)viewM[row][col] / 4096.0f;
	}
	pc->screen = geomScreen;
	pc->viewportW = viewportW;
	pc->viewportH = viewportH;
	pc->wide = wide != 0;
}

/* Camera-space Z of a precompiled native terrain vertex, QA only. */
float NativeRenderer_LevelPCWorldDepthQA(float x, float y, float z)
{
    const struct NativeLevelPCMesh *pc=&s_nativeLevelPC;
    if(!pc->valid || pc->viewportW<=0) return -1.0f;
    return pc->matrix[2]*x+pc->matrix[5]*y+pc->matrix[8]*z+pc->translation[2];
}

int NativeRenderer_LevelPCQueueBlock(const void *meshKey, int blockIndex)
{
	struct NativeLevelPCMesh *pc = &s_nativeLevelPC;
	if (!pc->valid || !NativeRenderer_LevelPCEnabled() ||
	    meshKey != pc->meshKey || pc->viewportW == 0 ||
	    blockIndex < 0 || blockIndex >= pc->blockCount)
		return 0;
	/* O(1) duplicate filtering for BSP-visible native blocks.
	 * On allocation failure retain the safe linear fallback. */
	if (pc->queueStamp)
	{
		if (pc->queueStamp[blockIndex] == pc->queueGeneration)
			return 1;
	}
	else
	{
		for (int i = 0; i < pc->queueCount; ++i)
			if (pc->queued[i] == blockIndex) return 1;
	}
	/* Never suppress retail OT on a static-cache block with missing material
	 * or animated water/vertex/texture data. The compiled face mask represents
	 * whole-block readiness; low-detail QA is a single-face special case. */
	int validFaces = 0;
	for (int face = 0; face < 4; ++face)
		validFaces += pc->faceType[blockIndex * 4 + face] < NATIVE_LEVEL_PC_MATERIAL_BUCKETS;
	if (validFaces != 4 && validFaces != 1)
		return 0;
	if (pc->queueCount >= NATIVE_LEVEL_PC_MAX_QUEUED)
		return 0; /* Caller MUST retain legacy PS1 rendering. */
	if (pc->queueStamp)
		pc->queueStamp[blockIndex] = pc->queueGeneration;
	pc->queued[pc->queueCount++] = blockIndex;
	return 1;
}

static GLuint NativeRenderer_LevelPCCompileShader(GLenum kind, const char *source)
{
	GLuint shader = glCreateShader(kind);
	GLint ok = 0;
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char message[512] = {0};
		glGetShaderInfoLog(shader, sizeof(message), NULL, message);
		Platform_LogError("[LEVEL PC GPU] shader compile failed: %s\n", message);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static int NativeRenderer_LevelPCEnsureShader(void)
{
	struct NativeLevelPCMesh *pc = &s_nativeLevelPC;
	static const char vs[] =
		"#version 140\n"
		"in vec4 a_position;\n"
		"in vec4 a_color;\n"
		"uniform mat3 u_viewM;\n"
		"uniform vec3 u_viewT;\n"
		"uniform float u_screen;\n"
		"uniform vec2 u_viewport;\n"
		"uniform float u_wide;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		" vec3 eye=u_viewM*a_position.xyz + u_viewT;\n"
		" float nearZ=max(1.0,u_screen*0.5+1.0);\n"
		" vec2 clipXY=vec2(2.0*eye.x*u_screen*u_wide/u_viewport.x,\n"
		"                 -2.0*eye.y*u_screen/u_viewport.y);\n"
		" gl_Position=vec4(clipXY,eye.z-2.0*nearZ,eye.z);\n"
		" v_color=a_color;\n"
		"}\n";
	static const char fs[] =
		"#version 140\n"
		"in vec4 v_color;\n"
		"out vec4 fragColor;\n"
		"void main() { fragColor=vec4(1.0,0.12,0.86,1.0); }\n";
	GLuint vert,frag,program;
	GLint ok = 0;
	if (pc->debugProgram != 0) return 1;
	vert=NativeRenderer_LevelPCCompileShader(GL_VERTEX_SHADER,vs);
	frag=NativeRenderer_LevelPCCompileShader(GL_FRAGMENT_SHADER,fs);
	if (vert==0 || frag==0)
	{
		if (vert!=0) glDeleteShader(vert);
		if (frag!=0) glDeleteShader(frag);
		return 0;
	}
	program=glCreateProgram();
	glAttachShader(program,vert);
	glAttachShader(program,frag);
	glBindAttribLocation(program,a_position,"a_position");
	glBindAttribLocation(program,a_color,"a_color");
	glLinkProgram(program);
	glDeleteShader(vert);
	glDeleteShader(frag);
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char message[512] = {0};
		glGetProgramInfoLog(program,sizeof(message),NULL,message);
		Platform_LogError("[LEVEL PC GPU] shader link failed: %s\n",message);
		glDeleteProgram(program);
		return 0;
	}
	pc->debugProgram=program;
	pc->uViewMatrix=glGetUniformLocation(program,"u_viewM");
	pc->uViewTranslation=glGetUniformLocation(program,"u_viewT");
	pc->uScreen=glGetUniformLocation(program,"u_screen");
	pc->uViewport=glGetUniformLocation(program,"u_viewport");
	pc->uWide=glGetUniformLocation(program,"u_wide");
	return 1;
}

/* Material-correct resident-world preview:
 * Reuse the existing pixel-exact PS1 VRAM palette fragment shaders; only the
 * vertex stage transforms world-space quadblock vertices directly on the GPU.
 * This is NOT the shipping path yet: transparencies/OT ordering must be
 * validated before any of the retail packet writers can be skipped. */
static int NativeRenderer_LevelPCAffineWorldQAEnabled(void)
{
#if defined(CTR_GPU_EXPERIMENTAL_RENDER) && defined(CTR_INTERNAL)
    static int checked, enabled;
    if (!checked)
    {
        FILE *f = fopen("debug/level_pc_affine_uv_qa.enable", "rb");
        enabled = (f != NULL);
        if (f) fclose(f);
        checked = 1;
        if (enabled)
            Platform_Log("[LEVEL PC GPU] experimental affine PS1-style world interpolation enabled\n");
    }
    return enabled;
#else
    return 0;
#endif
}

static int NativeRenderer_LevelPCEnsureMaterialShader(int mode)
{
	struct NativeLevelPCMesh *pc = &s_nativeLevelPC;
	static const char vs[] =
		"#version 140\n"
        "#ifdef CFR_AFFINE_LEVEL_VARYINGS\n"
        "#define CFR_AFFINE_QUALIFIER noperspective\n"
        "#else\n"
        "#define CFR_AFFINE_QUALIFIER\n"
        "#endif\n"
		"in vec4 a_position;\n"
		"in vec4 a_texcoord;\n"
		"in vec4 a_color;\n"
		"uniform mat3 u_viewM;\n"
		"uniform vec3 u_viewT;\n"
		"uniform float u_screen;\n"
		"uniform vec2 u_viewport;\n"
		"uniform float u_wide;\n"
		"uniform int u_depthInterop;\n"
		"uniform float u_depthScale;\n"
		"CFR_AFFINE_QUALIFIER out vec4 v_texcoord;\n"
		"CFR_AFFINE_QUALIFIER out vec3 v_texcoord_persp;\n"
		"CFR_AFFINE_QUALIFIER out vec4 v_color;\n"
		"CFR_AFFINE_QUALIFIER out vec4 v_page_clut;\n"
		"CFR_AFFINE_QUALIFIER out vec2 v_ditherCoord;\n"
		"CFR_AFFINE_QUALIFIER out float v_z;\n"
		"void main() {\n"
		" vec3 eye=u_viewM*a_position.xyz + u_viewT;\n"
		" float nearZ=max(1.0,u_screen*0.5+1.0);\n"
		" vec2 clipXY=vec2(2.0*eye.x*u_screen*u_wide/u_viewport.x,\n"
		"                 -2.0*eye.y*u_screen/u_viewport.y);\n"
		" float psxZ=clamp(eye.z*u_depthScale/65535.0,0.00005,0.9999);\n"
		" gl_Position=vec4(clipXY,u_depthInterop>0 ? eye.z*(2.0*psxZ-1.0) : eye.z-2.0*nearZ,eye.z);\n"
		" gl_ClipDistance[0]=eye.z-nearZ;\n"
		" vec2 screen=(eye.xy*u_screen/max(eye.z,nearZ))*vec2(u_wide,1.0);\n"
		" v_ditherCoord=screen + u_viewport * 0.5;\n"
		" v_texcoord=vec4(a_texcoord.xy,2.0,a_texcoord.w);\n"
		" v_texcoord_persp=vec3(v_texcoord.xy,1.0);\n"
		" v_color=a_color;\n"
		" v_color.xyz *= 2.0;\n"
		" v_page_clut.x=fract(a_position.w/16.0)*1024.0 + 0.00025;\n"
		" v_page_clut.y=floor(a_position.w/16.0)*256.0 + 0.00025;\n"
		" v_page_clut.z=fract(a_texcoord.z/64.0) + 0.00025;\n"
		" v_page_clut.w=floor(a_texcoord.z/64.0)/512.0 + 0.00025;\n"
		" v_z=(gl_Position.z-40.0)*0.005;\n"
		"}\n";
	static const char fragHeader[] =
		"#version 140\n"
		"#define varying in\n"
		"#define texture2D texture\n"
		"out vec4 fragColor;\n";
    static const char fragHeaderAffine[] =
        "#version 140\n"
        "#define varying noperspective in\n"
        "#define texture2D texture\n"
        "out vec4 fragColor;\n";
	const char *shaderBody[3] = {gte_shader_4, gte_shader_8, gte_shader_16};
	const char *fragmentSources[4] = {
		fragHeader, gpu_shader_common, GPU_DITHERING, NULL
	};
	GLuint vertex,fragment,program;
	GLint ok=0;
	if (mode < 0 || mode >= NATIVE_LEVEL_PC_TEXTURE_MODES) return 0;
	if (pc->textureProgram[mode] != 0) return 1;
    /* PS1 GT4 textures are affine in screen space. The original experimental
     * shader used GLSL smooth interpolation, which introduces perspective
     * warping on walls/track faces. Compare exact same GPU path with an
     * opt-in, GL 3.3 noperspective varying shader. No shipping path changes. */
    const int affineQA = NativeRenderer_LevelPCAffineWorldQAEnabled();
    char affineVertex[sizeof(vs) + 80];
    const char *vsSource = vs;
    if (affineQA)
    {
        const char *version = "#version 140\n";
        const size_t prefix = strlen(version);
        memcpy(affineVertex, version, prefix);
        snprintf(affineVertex + prefix, sizeof(affineVertex) - prefix,
            "#define CFR_AFFINE_LEVEL_VARYINGS 1\n%s", vs + prefix);
        vsSource = affineVertex;
    }
    fragmentSources[0] = affineQA ? fragHeaderAffine : fragHeader;
	fragmentSources[3] = shaderBody[mode];
	vertex=NativeRenderer_LevelPCCompileShader(GL_VERTEX_SHADER,vsSource);
	if (!vertex) return 0;
	fragment=glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment,4,fragmentSources,NULL);
	glCompileShader(fragment);
	glGetShaderiv(fragment,GL_COMPILE_STATUS,&ok);
	if (!ok)
	{
		char message[768]={0};
		glGetShaderInfoLog(fragment,sizeof(message),NULL,message);
		Platform_LogError("[LEVEL PC GPU] textured shader compile failed: %s\n",message);
		glDeleteShader(fragment);
		glDeleteShader(vertex);
		return 0;
	}
	program=glCreateProgram();
	glAttachShader(program,vertex);
	glAttachShader(program,fragment);
	glBindAttribLocation(program,a_position,"a_position");
	glBindAttribLocation(program,a_texcoord,"a_texcoord");
	glBindAttribLocation(program,a_color,"a_color");
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glGetProgramiv(program,GL_LINK_STATUS,&ok);
	if (!ok)
	{
		char message[768]={0};
		glGetProgramInfoLog(program,sizeof(message),NULL,message);
		Platform_LogError("[LEVEL PC GPU] textured shader link failed: %s\n",message);
		glDeleteProgram(program);
		return 0;
	}
	pc->textureProgram[mode]=program;
	pc->texViewMatrix[mode]=glGetUniformLocation(program,"u_viewM");
	pc->texViewTranslation[mode]=glGetUniformLocation(program,"u_viewT");
	pc->texScreen[mode]=glGetUniformLocation(program,"u_screen");
	pc->texViewport[mode]=glGetUniformLocation(program,"u_viewport");
	pc->texWide[mode]=glGetUniformLocation(program,"u_wide");
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program,"s_texture"),0);
	glUniform1i(glGetUniformLocation(program,"s_rgLut"),1);
	glUniform1i(glGetUniformLocation(program,"bilinearFilter"),g_cfg_bilinearFiltering);
	glUniform1i(glGetUniformLocation(program,"psxSemiTransPass"),0);
	glUniform1i(glGetUniformLocation(program,"psxDrawMaskSet"),0);
	glUniform1i(glGetUniformLocation(program,"psxTextureOutputStp"),0);
	glUseProgram(0);
	return 1;
}

/* Explicitly experimental native track depth-buffer validation.
 * Never enabled for the default retail renderer, and never for normal play.
 * This test clears depth ONLY after the PS1 framebuffer has been submitted,
 * so that resident GPU track faces can depth-test against one another. It
 * does not solve ordering/occlusion of existing PS1 sprites or objects.
 */
/* A/B-only depth scale, values 0.25..8.0; marker is read once per process.
 * This adjusts only GPU occlusion Z, NEVER world position/collision/physics. */
static float NativeRenderer_LevelPCInteropDepthScaleQA(void)
{
	static int checked;
	static float scale=1.0f;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_gpu_depth_scale_stress.txt","rb");
		if (f)
		{
			float value=0.0f;
			if (fscanf(f,"%f",&value)==1 && value>=0.25f && value<=8.0f)
				scale=value;
			fclose(f);
		}
		checked=1;
		Platform_Log("[LEVEL PC GPU] stress depth-world scale=%.4f\n",(double)scale);
	}
	return scale;
}

static int NativeRenderer_LevelPCStressInteropQA(void)
{
	static int checked, enabled;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_depth_interop_stress.enable","rb");
		enabled=(f!=NULL);
		if (f)fclose(f);
		checked=1;
	}
	return enabled && NativePerf_IsStressTestMeasuring();
}

/* Opt-in GL triangle winding checks, no production impact. */
static int NativeRenderer_LevelPCStressCullModeQA(void)
{
	static int checked,mode;
	if(!checked)
	{
		FILE *f=fopen("debug/level_pc_gpu_face_cull_stress.txt","rb");
		if(f)
		{
			int value=0;
			if(fscanf(f,"%d",&value)==1 && value>=0 && value<=2)
				mode=value;
			fclose(f);
		}
		checked=1;
	}
	return (NativePerf_IsStressTestMeasuring() ? mode : 0);
}
static int NativeRenderer_LevelPCStressDepthQA(void)
{
	static int checked, enabled;
	if (!checked)
	{
		FILE *f=fopen("debug/level_pc_gpu_depth_stress.enable","rb");
		enabled=(f!=NULL);
		if (f)fclose(f);
		checked=1;
	}
	return enabled &&
        (NativePerf_IsStressTestMeasuring() ||
         NativeRenderer_LevelPCManualPlaytest());
}

/* Per-face cull toggle is confined to GPU stress QA. Normal retail rendering
 * and default native fallback are unchanged. */
static int NativeRenderer_LevelPCPerFaceCullQA(void)
{
    static int checked,enabled;
    if (!checked)
    {
        FILE *f=fopen("debug/level_pc_gpu_per_face_cull_stress.enable","rb");
        enabled=f!=NULL;
        if (f)fclose(f);
        checked=1;
    }
    return enabled && NativePerf_IsStressTestMeasuring();
}

static void NativeRenderer_LevelPCDrawTextured(void)
{
	struct NativeLevelPCMesh *pc=&s_nativeLevelPC;
	GLint oldProgram=0,oldVao=0,oldBuffer=0,oldTextureActive=0;
	GLint oldTex0=0,oldTex1=0,oldSrcRGB=GL_ONE,oldDstRGB=GL_ZERO;
	GLint oldSrcA=GL_ONE,oldDstA=GL_ZERO,oldEqRGB=GL_FUNC_ADD,oldEqA=GL_FUNC_ADD;
	GLfloat oldBlendColor[4]={0};
	GLboolean wasBlend=glIsEnabled(GL_BLEND);
	GLboolean wasDepth=glIsEnabled(GL_DEPTH_TEST);
	GLboolean oldDepthMask=GL_TRUE;
	GLint oldDepthFunc=GL_LESS, availableDepth=0;
	GLfloat oldDepthClear=1.0f;
	const int nativeDepthQA=NativeRenderer_LevelPCStressDepthQA();
	const int faceCullQA=nativeDepthQA ? NativeRenderer_LevelPCStressCullModeQA() : 0;
	const int perFaceCullQA=nativeDepthQA ? NativeRenderer_LevelPCPerFaceCullQA() : 0;
	GLint oldCullFaceMode=GL_BACK,oldFrontFace=GL_CCW;
	int depthAttachmentAdded=0;
	GLint framebufferBefore=0, originalDepthAttachment=GL_NONE;
	GLint oldReadFbo=0,oldDrawFbo=0;
	GLint originalViewport[4]={0,0,0,0};
	int compositeFboActive=0;
	GLboolean wasCull=glIsEnabled(GL_CULL_FACE);
	GLboolean wasScissor=glIsEnabled(GL_SCISSOR_TEST);
	if(faceCullQA || perFaceCullQA)
	{
		glGetIntegerv(GL_CULL_FACE_MODE,&oldCullFaceMode);
		glGetIntegerv(GL_FRONT_FACE,&oldFrontFace);
	}
	int lastMode=-1;
	static int multiDrawChecked=0, multiDrawEnabled=1;
	static u64 previewFrameCount, previewFaces, previewDriverDraws;
	const int depthInteropQA=NativeRenderer_LevelPCStressInteropQA() && nativeDepthQA;
	const float depthInteropScale=depthInteropQA ?
		NativeRenderer_LevelPCInteropDepthScaleQA() : 1.0f;
	const GLboolean wasClip0=glIsEnabled(GL_CLIP_DISTANCE0);
	if (!multiDrawChecked)
	{
		FILE *disable=fopen("debug/level_pc_multidraw.disable","rb");
		if (disable) { multiDrawEnabled=0; fclose(disable); }
		multiDrawChecked=1;
	}
	glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE,&oldTextureActive);
	glGetIntegerv(GL_BLEND_SRC_RGB,&oldSrcRGB);
	glGetIntegerv(GL_BLEND_DST_RGB,&oldDstRGB);
	glGetIntegerv(GL_BLEND_SRC_ALPHA,&oldSrcA);
	glGetIntegerv(GL_BLEND_DST_ALPHA,&oldDstA);
	glGetIntegerv(GL_BLEND_EQUATION_RGB,&oldEqRGB);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldEqA);
	glGetFloatv(GL_BLEND_COLOR,oldBlendColor);
	if (nativeDepthQA)
	{
		glGetBooleanv(GL_DEPTH_WRITEMASK,&oldDepthMask);
		glGetIntegerv(GL_DEPTH_FUNC,&oldDepthFunc);
		glGetFloatv(GL_DEPTH_CLEAR_VALUE,&oldDepthClear);
		glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebufferBefore);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldReadFbo);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDrawFbo);
		GLint defaultDepthBits=0,defaultDepthType=GL_NONE;
		if(framebufferBefore==0)
		{
			/* In GL Core 3.3, GL_DEPTH_BITS queried via glGetIntegerv
			 * is removed and returned GL_INVALID_ENUM. Query the default
			 * framebuffer's GL_DEPTH attachment via the modern interface. */
			(void)glGetError();
			glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&defaultDepthType);
			GLenum queryError=glGetError();
			if(queryError==GL_NO_ERROR && defaultDepthType!=GL_NONE)
			{
				glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH,
					GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,&defaultDepthBits);
				queryError=glGetError();
			}
			static int depthQueryLogged;
			if(!depthQueryLogged)
			{
				Platform_Log("[LEVEL PC GPU] default framebuffer depth type=0x%x bits=%d queryErr=0x%x\\n",
					(unsigned)defaultDepthType,(int)defaultDepthBits,(unsigned)queryError);
				depthQueryLogged=1;
			}
			if(queryError!=GL_NO_ERROR) defaultDepthBits=0;
		}
		if (framebufferBefore != 0)
		{
			glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&originalDepthAttachment);
			if (originalDepthAttachment == GL_NONE)
			{
				GLint viewport[4],oldRenderbuffer=0;
				glGetIntegerv(GL_VIEWPORT,viewport);
				glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
				if (viewport[2]>0 && viewport[3]>0 &&
				    viewport[2]<=4096 && viewport[3]<=4096)
				{
					if (!s_nativePCDepthRbo)
						glGenRenderbuffers(1,&s_nativePCDepthRbo);
					if (s_nativePCDepthRbo)
					{
						glBindRenderbuffer(GL_RENDERBUFFER,s_nativePCDepthRbo);
						if (s_nativePCDepthRboWidth != viewport[2] ||
						    s_nativePCDepthRboHeight != viewport[3])
						{
							glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,
								viewport[2],viewport[3]);
							s_nativePCDepthRboWidth=viewport[2];
							s_nativePCDepthRboHeight=viewport[3];
						}
						glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
							GL_RENDERBUFFER,s_nativePCDepthRbo);
						depthAttachmentAdded=1;
					}
				}
				glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)oldRenderbuffer);
			}
		}
		/* Windows SDL default framebuffer reports GL_DEPTH_BITS=0 on
		 * this RX570, despite SDL requesting a 24-bit depth pixel format.
		 * Only for the explicit stress QA mode, copy the current game
		 * backbuffer into a full-color/depth FBO, render native world
		 * geometry there, then blit the result back. PS1 paths never use
		 * this FBO, and regular gameplay is completely unchanged. */
		if (framebufferBefore==0 && defaultDepthBits==0)
		{
			GLint oldActiveTexture=0,oldBoundTexture=0,oldRenderbuffer=0;
			glGetIntegerv(GL_VIEWPORT,originalViewport);
			glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActiveTexture);
			glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldBoundTexture);
			glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
			const int w=originalViewport[2],h=originalViewport[3];
			static int loggedViewport;
			if (!loggedViewport)
			{
				Platform_Log("[LEVEL PC GPU] QA viewport x=%d y=%d w=%d h=%d pcW=%d pcH=%d fb=%d read=%d draw=%d glErr=0x%x\n",
					originalViewport[0],originalViewport[1],w,h,
					pc->viewportW,pc->viewportH,framebufferBefore,
					oldReadFbo,oldDrawFbo,(unsigned)glGetError());
				loggedViewport=1;
			}
			if (w>0 && h>0 && w<=4096 && h<=2304)
			{
				if (!s_nativePCCompositeFbo)
					glGenFramebuffers(1,&s_nativePCCompositeFbo);
				if (!s_nativePCCompositeTex)
					glGenTextures(1,&s_nativePCCompositeTex);
				if (!s_nativePCDepthRbo)
					glGenRenderbuffers(1,&s_nativePCDepthRbo);
				if (s_nativePCCompositeFbo && s_nativePCCompositeTex && s_nativePCDepthRbo)
				{
					glBindTexture(GL_TEXTURE_2D,s_nativePCCompositeTex);
					glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
					glBindRenderbuffer(GL_RENDERBUFFER,s_nativePCDepthRbo);
					if (w!=s_nativePCCompositeWidth || h!=s_nativePCCompositeHeight)
					{
						glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
						glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
						s_nativePCCompositeWidth=w;s_nativePCCompositeHeight=h;
						s_nativePCDepthRboWidth=w;s_nativePCDepthRboHeight=h;
					}
					glBindFramebuffer(GL_FRAMEBUFFER,s_nativePCCompositeFbo);
					glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D,s_nativePCCompositeTex,0);
					glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
						GL_RENDERBUFFER,s_nativePCDepthRbo);
					const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
					if (status==GL_FRAMEBUFFER_COMPLETE)
					{
						glDisable(GL_SCISSOR_TEST);
						glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)oldReadFbo);
						glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s_nativePCCompositeFbo);
						glBlitFramebuffer(originalViewport[0],originalViewport[1],
							originalViewport[0]+w,originalViewport[1]+h,
							0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
						glBindFramebuffer(GL_FRAMEBUFFER,s_nativePCCompositeFbo);
						glViewport(0,0,w,h);
						if (wasScissor)glEnable(GL_SCISSOR_TEST);
						compositeFboActive=1;
					}
					else
					{
						static int onceError;
						if(!onceError){Platform_LogError("[LEVEL PC GPU] temporary FBO incomplete=0x%x\n",(unsigned)status);onceError=1;}
						glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)oldReadFbo);
						glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)oldDrawFbo);
					}
				}
			}
			glBindTexture(GL_TEXTURE_2D,(GLuint)oldBoundTexture);
			glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)oldRenderbuffer);
			glActiveTexture((GLenum)oldActiveTexture);
		}
		/* GL_DEPTH_BITS (0x0D56) is removed from modern Core profiles:
		 * glGetIntegerv(GL_DEPTH_BITS) generated GL_INVALID_ENUM on RX570,
		 * masked the valid 24-bit native renderbuffer, and contaminated
		 * subsequent screenshot glGetError state. Query the FBO attachment
		 * directly, the supported GL 3.x Core form. */
		if (compositeFboActive || framebufferBefore!=0)
		{
			GLint objType=GL_NONE;
			glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
				GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&objType);
			if (objType!=GL_NONE)
				glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
					GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,&availableDepth);
		}
		else if(framebufferBefore==0)
			availableDepth=defaultDepthBits;
		static int once;
		if (!once)
		{
			Platform_Log("[LEVEL PC GPU] stress depth target framebuffer=%d depthBits=%d tempAttachment=%d compositeFbo=%d err=0x%x\n",
				(int)framebufferBefore,(int)availableDepth,depthAttachmentAdded,compositeFboActive,(unsigned)glGetError());
			once=1;
		}
	}
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex0);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex1);
	glBindTexture(GL_TEXTURE_2D,s_rgLutTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,NativeRenderer_GetVRAMTexture());

	/* Alpha overlay lets the developer compare the real PS1 output under
	 * the resident geometry without corrupting the normally rendered game.
	 * Keep depth/blend/viewport side effects isolated from the retail path. */
	glBindVertexArray(pc->vao);
	glDisable(GL_DEPTH_TEST);
	if(faceCullQA)
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glFrontFace(faceCullQA==1?GL_CCW:GL_CW);
		static int logged;
		if(!logged)
		{
			Platform_Log("[LEVEL PC GPU] stress face-cull GL_FRONT_FACE=%s\n",
				faceCullQA==1?"CCW":"CW");
			logged=1;
		}
	}
	else glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
	if (nativeDepthQA && availableDepth > 0)
	{
		static int logOnce;
		if (!logOnce)
		{
			Platform_Log("[LEVEL PC GPU] test-only depth buffer enabled bits=%d (sprite/OT ordering not solved)\n",availableDepth);
			logOnce=1;
		}
		glClearDepth(1.0);
		glDepthMask(GL_TRUE);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glBlendColor(0.0f,0.0f,0.0f,1.0f);
	}
	else
		glBlendColor(0.0f,0.0f,0.0f,0.50f);
	glBlendFuncSeparate(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_ONE,GL_ZERO);
	if(depthInteropQA && availableDepth>0) glEnable(GL_CLIP_DISTANCE0);

	if (multiDrawEnabled)
	{
		/* The world mesh is persistent GL_STATIC_DRAW. Build only an
		 * O(visible blocks) index list; each shader/material format is one
		 * native OpenGL multi-draw instead of up to 256 driver draw calls.
		 * This QA overlay must not replace retail OT until transparency and
		 * front/back geometry are validated. */
		GLint first[NATIVE_LEVEL_PC_TEXTURE_MODES*3][NATIVE_LEVEL_PC_MAX_QUEUED*4];
		GLsizei count[NATIVE_LEVEL_PC_TEXTURE_MODES*3][NATIVE_LEVEL_PC_MAX_QUEUED*4];
		int num[NATIVE_LEVEL_PC_TEXTURE_MODES*3]={0};
		for (int i=0;i<pc->queueCount;++i)
		{
			const int quad=pc->queued[i];
			for (int face=0;face<4;++face)
			{
				const int encoded=pc->faceType[quad*4+face];
				if (encoded>=NATIVE_LEVEL_PC_MATERIAL_BUCKETS) continue;
				const int primitive=encoded%NATIVE_LEVEL_PC_FACE_MODES;
				const int mode=primitive%NATIVE_LEVEL_PC_TEXTURE_MODES;
				const int vertexCount=(primitive>=NATIVE_LEVEL_PC_TEXTURE_MODES)?3:6;
				const int cull=encoded/NATIVE_LEVEL_PC_FACE_MODES;
				const int bucket=mode*3+(perFaceCullQA ? cull : 0);
				const int at=num[bucket]++;
				first[bucket][at]=quad*NATIVE_LEVEL_PC_VERTS_PER_BLOCK+face*6;
				count[bucket][at]=vertexCount;
			}
		}
		for (int bucket=0;bucket<NATIVE_LEVEL_PC_TEXTURE_MODES*3;++bucket)
		{
			if (!num[bucket]) continue;
			const int mode=bucket/3;
			const int cull=bucket%3;
			if (perFaceCullQA)
			{
				if (cull==0)glDisable(GL_CULL_FACE);
				else
				{
					glEnable(GL_CULL_FACE);
					glCullFace(GL_BACK);
					glFrontFace(cull==1 ? GL_CCW : GL_CW);
				}
			}
			if (!NativeRenderer_LevelPCEnsureMaterialShader(mode)) continue;
			glUseProgram(pc->textureProgram[mode]);
			glUniformMatrix3fv(pc->texViewMatrix[mode],1,GL_FALSE,pc->matrix);
			glUniform3fv(pc->texViewTranslation[mode],1,pc->translation);
			glUniform1f(pc->texScreen[mode],(float)pc->screen);
			glUniform2f(pc->texViewport[mode],(float)pc->viewportW,(float)pc->viewportH);
			glUniform1f(pc->texWide[mode],pc->wide?0.75f:1.0f);
			glUniform1i(glGetUniformLocation(pc->textureProgram[mode],"u_depthInterop"),
				depthInteropQA && availableDepth>0);
			glUniform1f(glGetUniformLocation(pc->textureProgram[mode],"u_depthScale"),
				depthInteropScale);
			glMultiDrawArrays(GL_TRIANGLES,first[bucket],count[bucket],num[bucket]);
			previewDriverDraws++;
			previewFaces+=(u64)num[bucket];
		}
	}
	else
	{
		/* Bit-for-bit legacy preview fallback for A/B validation. */
		for(int i=0;i<pc->queueCount;++i)
		{
			const int quad=pc->queued[i];
			for(int face=0;face<4;++face)
			{
				const int encoded=pc->faceType[quad*4+face];
				if (encoded>=NATIVE_LEVEL_PC_MATERIAL_BUCKETS) continue;
				const int primitive=encoded%NATIVE_LEVEL_PC_FACE_MODES;
				const int mode=primitive%NATIVE_LEVEL_PC_TEXTURE_MODES;
				const int vertexCount=(primitive>=NATIVE_LEVEL_PC_TEXTURE_MODES)?3:6;
				const int cull=encoded/NATIVE_LEVEL_PC_FACE_MODES;
				if(perFaceCullQA)
				{
					if(cull==0)glDisable(GL_CULL_FACE);
					else
					{
						glEnable(GL_CULL_FACE);
						glCullFace(GL_BACK);
						glFrontFace(cull==1 ? GL_CCW : GL_CW);
					}
				}
				if (!NativeRenderer_LevelPCEnsureMaterialShader(mode)) continue;
				if(lastMode!=mode)
				{
					glUseProgram(pc->textureProgram[mode]);
					glUniformMatrix3fv(pc->texViewMatrix[mode],1,GL_FALSE,pc->matrix);
					glUniform3fv(pc->texViewTranslation[mode],1,pc->translation);
					glUniform1f(pc->texScreen[mode],(float)pc->screen);
					glUniform2f(pc->texViewport[mode],(float)pc->viewportW,(float)pc->viewportH);
					glUniform1f(pc->texWide[mode],pc->wide?0.75f:1.0f);
					glUniform1i(glGetUniformLocation(pc->textureProgram[mode],"u_depthInterop"),
						depthInteropQA && availableDepth>0);
					glUniform1f(glGetUniformLocation(pc->textureProgram[mode],"u_depthScale"),
						depthInteropScale);
					lastMode=mode;
				}
				glDrawArrays(GL_TRIANGLES,
					quad*NATIVE_LEVEL_PC_VERTS_PER_BLOCK+face*6,vertexCount);
				previewDriverDraws++;
				previewFaces++;
			}
		}
	}
	/* One-time depth probe after native world draw, at 3 screen locations.
	 * This reads ONLY the game's own GPU backbuffer, never the desktop.
	 * GL readback is slow: diagnostic only, under explicit interop marker. */
	if(depthInteropQA && availableDepth>0)
	{
		static int depthSampled;
		if(!depthSampled && pc->queueCount>=32)
		{
			GLint viewport[4]={0,0,0,0};
			glGetIntegerv(GL_VIEWPORT,viewport);
			const float xFrac[4]={0.5f,0.5f,0.3f,0.7f};
			const float yFrac[4]={0.20f,0.35f,0.20f,0.20f};
			for(int si=0;si<4;++si)
			{
				const int sx=viewport[0]+(int)(xFrac[si]*viewport[2]);
				const int sy=viewport[1]+(int)(yFrac[si]*viewport[3]);
				GLfloat z=0.0f;
				glReadPixels(sx,sy,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&z);
				Platform_Log("[LEVEL PC GPU] SZ probe normalizedX=%.2f normalizedY=%.2f worldDepth=%.6f worldSZ=%.1f glError=0x%x\n",
					xFrac[si],yFrac[si],(double)z,(double)z*65535.0,(unsigned)glGetError());
			}
			depthSampled=1;
		}
	}
	previewFrameCount++;
	if ((previewFrameCount%300u)==0)
	{
		Platform_Log("[LEVEL PC GPU] preview path=%s frames=300 faces=%llu drawcalls=%llu mesh=%d blocks\n",
			multiDrawEnabled?"multidraw":"legacy",
			(unsigned long long)previewFaces,
			(unsigned long long)previewDriverDraws,pc->queueCount);
		previewFaces=0;
		previewDriverDraws=0;
	}
	glBlendEquationSeparate((GLenum)oldEqRGB,(GLenum)oldEqA);
	glBlendFuncSeparate((GLenum)oldSrcRGB,(GLenum)oldDstRGB,
		(GLenum)oldSrcA,(GLenum)oldDstA);
	glBlendColor(oldBlendColor[0],oldBlendColor[1],
		oldBlendColor[2],oldBlendColor[3]);
	if(wasBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
	if(wasDepth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
	if(nativeDepthQA)
	{
		glDepthMask(oldDepthMask);
		glDepthFunc((GLenum)oldDepthFunc);
		glClearDepth((GLdouble)oldDepthClear);
		if(depthAttachmentAdded)
		{
			/* The PS1 framebuffer explicitly has no depth attachment.
			 * Remove the temporary RBO before returning to its owner. */
			glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
				GL_RENDERBUFFER,0);
		}
	}
	if (compositeFboActive)
	{
		const int w=originalViewport[2],h=originalViewport[3];
		glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER,s_nativePCCompositeFbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)oldDrawFbo);
		glBlitFramebuffer(0,0,w,h,
			originalViewport[0],originalViewport[1],
			originalViewport[0]+w,originalViewport[1]+h,
			GL_COLOR_BUFFER_BIT,GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)oldReadFbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)oldDrawFbo);
		glViewport(originalViewport[0],originalViewport[1],w,h);
	}
	if(wasClip0)glEnable(GL_CLIP_DISTANCE0);else glDisable(GL_CLIP_DISTANCE0);
	if(faceCullQA || perFaceCullQA)
	{
		glCullFace((GLenum)oldCullFaceMode);
		glFrontFace((GLenum)oldFrontFace);
	}
	if(wasCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
	if(wasScissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
	glBindVertexArray((GLuint)oldVao);
	glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,(GLuint)oldTex0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D,(GLuint)oldTex1);
	glActiveTexture((GLenum)oldTextureActive);
	glUseProgram((GLuint)oldProgram);
	pc->queueCount=0;
}

void NativeRenderer_LevelPCDrawQueued(void)
{
    if(NativeRenderer_LevelPCPreviewMode()==3)
    {
        /* QA-only: compare GPU transform output with GTE without drawing. */
        NativeRenderer_LevelPCProjectionQAFlush();
        NativeRenderer_LevelPCMidpointQAFlush();
        s_nativeLevelPC.queueCount=0;
        return;
    }
	struct NativeLevelPCMesh *pc=&s_nativeLevelPC;
	if (pc->queueCount && pc->valid &&
	    NativeRenderer_LevelPCPreviewMode() == 2)
	{
		NativeRenderer_LevelPCDrawTextured();
		return;
	}
	GLint oldProgram=0,oldVao=0,oldBuffer=0,polygonMode[2]={GL_FILL,GL_FILL};
	GLboolean blend,depth,cull,scissor;
	if (!pc->valid || pc->queueCount==0 || pc->viewportW<=0 ||
	    !NativeRenderer_LevelPCEnabled())
		return;
	if (!NativeRenderer_LevelPCEnsureShader())
		return;
	glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
	glGetIntegerv(GL_POLYGON_MODE,polygonMode);
	blend=glIsEnabled(GL_BLEND);
	depth=glIsEnabled(GL_DEPTH_TEST);
	cull=glIsEnabled(GL_CULL_FACE);
	scissor=glIsEnabled(GL_SCISSOR_TEST);

	glUseProgram(pc->debugProgram);
	glBindVertexArray(pc->vao);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glUniformMatrix3fv(pc->uViewMatrix,1,GL_FALSE,pc->matrix);
	glUniform3fv(pc->uViewTranslation,1,pc->translation);
	glUniform1f(pc->uScreen,(float)pc->screen);
	glUniform2f(pc->uViewport,(float)pc->viewportW,(float)pc->viewportH);
	glUniform1f(pc->uWide,pc->wide?0.75f:1.0f);
	for(int i=0;i<pc->queueCount;++i)
	{
		const int offset=pc->queued[i]*NATIVE_LEVEL_PC_VERTS_PER_BLOCK;
		glDrawArrays(GL_TRIANGLES,offset,NATIVE_LEVEL_PC_VERTS_PER_BLOCK);
	}
	glPolygonMode(GL_FRONT,(GLenum)polygonMode[0]);
	glPolygonMode(GL_BACK,(GLenum)polygonMode[1]);
	if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
	if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
	if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
	if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
	glBindVertexArray((GLuint)oldVao);
	glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
	glUseProgram((GLuint)oldProgram);
	pc->queueCount=0;
}
#endif /* CTR_NATIVE */
