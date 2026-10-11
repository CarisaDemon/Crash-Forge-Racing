/* Crash Forge: Racing - first QA stage of resident native OBJ GPU rendering.
 * Real OBJ racer/kart/tire geometry becomes an immutable OpenGL VBO+VAO,
 * shared by every instance of that mesh. This stage does NOT render pixels
 * or bypass the PS1 OT: it verifies buffer residency, asset attributes,
 * render-thread GL ownership and per-mesh memory cost before the vertex
 * transform shader/depth interop can safely be integrated.
 *
 * Explicit opt-in only: debug/gpu_models_resident_qa.enable + stress test.
 * Safe normal behavior: stub calls and no GPU allocations.
 */
#include <platform/native_gpu_model_qa.h>
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)

#define NATIVE_GPU_MODEL_QA_MAX_MESHES 72
#define NATIVE_GPU_MODEL_QA_MAX_QUEUE 72
#define NATIVE_GPU_MODEL_QA_MAX_TRIANGLES 250000

struct NativeGpuModelQAVertex
{
    float p[3];      /* unscaled authored mesh-space position */
    float renderP[3]; /* original PS1-local rigid OBJ projection coordinates */
    float uv[2];     /* full native OBJ high precision atlas UV */
    float rgb[3];    /* PS1 vertex colors normalized to 1.0 at 128 */
    float wheel;     /* -1 rigid body/kart, 0..3 for animated wheels */
};

struct NativeGpuModelQAEntry
{
    const struct NativeObjMesh *mesh;
    const struct NativeObjTriangle *triangleSource;
    int sourceCount;
    int residentCount;
    size_t bytes;
    GLuint vao, vbo;
};

static struct NativeGpuModelQAEntry s_gpuModelQA[NATIVE_GPU_MODEL_QA_MAX_MESHES];
static int s_gpuModelQACount;
static const struct NativeObjMesh *s_gpuModelQAQueue[NATIVE_GPU_MODEL_QA_MAX_QUEUE];
static int s_gpuModelQAQueueCount;
static volatile int s_gpuModelQAStageLock;
static u64 s_gpuModelQABytesTotal;
static u64 s_gpuModelQATriTotal;
static int s_gpuModelQALoggedCap;
#define MODEL_PROBE_BATCH 12
#define MODEL_PROBE_MAX 144
static struct
{
    const struct NativeObjMesh *mesh;
    struct NativeGpuModelQASample sample;
} s_gpuProjectionSamples[MODEL_PROBE_BATCH];
static int s_gpuProjectionPending;
static int s_gpuProjectionAccepted;
static int s_gpuProjectionCompared;
static double s_gpuProjectionXYErrorSum;
static float s_gpuProjectionMaxXYError;
static int s_gpuProjectionMismatches2Px;


static void NativeGpuModelQaLock(void)
{
    while (__sync_lock_test_and_set(&s_gpuModelQAStageLock, 1))
    {
        /* Called only for experiment and never owns the OpenGL context. */
    }
}
static void NativeGpuModelQaUnlock(void)
{
    __sync_lock_release(&s_gpuModelQAStageLock);
}

static int NativeGpuModelQaEnabled(void)
{
    static int checked,enabled;
    if(!checked)
    {
        FILE *f=fopen("debug/gpu_models_resident_qa.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
        if(enabled)
            Platform_Log("[MODEL GPU QA] immutable model VBO residency enabled (pixels unchanged)\n");
    }
    return enabled && NativePerf_IsStressTestMeasuring();
}

int NativeGpuModel_ProjectionQaActive(void)
{
    static int checked,enabled;
    if(!checked)
    {
        FILE *f=fopen("debug/gpu_models_projection_qa.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
    }
    return enabled && NativeGpuModelQaEnabled();
}

void NativeGpuModel_StageRigidSample(const struct NativeObjMesh *mesh,
    const struct NativeGpuModelQASample *sample)
{
    if(!mesh || !sample || !NativeGpuModel_ProjectionQaActive())
        return;
    NativeGpuModelQaLock();
    if(s_gpuProjectionPending<MODEL_PROBE_BATCH &&
       s_gpuProjectionAccepted<MODEL_PROBE_MAX)
    {
        s_gpuProjectionSamples[s_gpuProjectionPending].mesh=mesh;
        s_gpuProjectionSamples[s_gpuProjectionPending].sample=*sample;
        ++s_gpuProjectionPending;
        ++s_gpuProjectionAccepted;
    }
    NativeGpuModelQaUnlock();
}

void NativeGpuModel_StageMesh(const struct NativeObjMesh *mesh)
{
    if(mesh==NULL || mesh->triangles==NULL || mesh->triangleCount<=0 ||
       mesh->triangleCount>NATIVE_GPU_MODEL_QA_MAX_TRIANGLES ||
       !NativeGpuModelQaEnabled())
        return;

    NativeGpuModelQaLock();
    for(int i=0;i<s_gpuModelQAQueueCount;++i)
    {
        if(s_gpuModelQAQueue[i]==mesh)
        {
            NativeGpuModelQaUnlock();
            return;
        }
    }
    if(s_gpuModelQAQueueCount<NATIVE_GPU_MODEL_QA_MAX_QUEUE)
        s_gpuModelQAQueue[s_gpuModelQAQueueCount++]=mesh;
    NativeGpuModelQaUnlock();
}

static struct NativeGpuModelQAEntry *NativeGpuModelQaFind(const struct NativeObjMesh *mesh)
{
    for(int i=0;i<s_gpuModelQACount;++i)
        if(s_gpuModelQA[i].mesh==mesh)
            return &s_gpuModelQA[i];
    return NULL;
}

static int NativeGpuModelQaUpload(const struct NativeObjMesh *mesh)
{
    const size_t triCount=(size_t)mesh->triangleCount;
    const size_t totalVerts=triCount*3u;
    const size_t bytes=totalVerts*sizeof(struct NativeGpuModelQAVertex);
    struct NativeGpuModelQAVertex *verts=NULL;
    struct NativeGpuModelQAEntry *entry=NativeGpuModelQaFind(mesh);
    GLint prevVao=0,prevArray=0;
    GLenum err;
    int cached=entry!=NULL;

    if(entry && entry->triangleSource==mesh->triangles &&
       entry->sourceCount==mesh->triangleCount && entry->vbo!=0)
        return 1;

    if(!entry)
    {
        if(s_gpuModelQACount>=NATIVE_GPU_MODEL_QA_MAX_MESHES)
        {
            if(!s_gpuModelQALoggedCap)
            {
                s_gpuModelQALoggedCap=1;
                Platform_Log("[MODEL GPU QA] cache full; skipping additional meshes\n");
            }
            return 0;
        }
        entry=&s_gpuModelQA[s_gpuModelQACount];
        memset(entry,0,sizeof(*entry));
    }

    verts=(struct NativeGpuModelQAVertex *)malloc(bytes);
    if(!verts)
    {
        Platform_LogWarn("[MODEL GPU QA] unable to stage %u bytes\n",(unsigned)bytes);
        return 0;
    }
    for(int i=0;i<mesh->triangleCount;++i)
    {
        const struct NativeObjTriangle *tri=&mesh->triangles[i];
        for(int v=0;v<3;++v)
        {
            const struct NativeObjVertex *source=&tri->v[v];
            struct NativeGpuModelQAVertex *dst=&verts[(size_t)i*3u+(size_t)v];
            for(int a=0;a<3;++a)
            {
                dst->p[a]=source->p[a];
                dst->renderP[a]=source->renderP[a];
                dst->rgb[a]=(float)source->renderColor[a]*(1.0f/128.0f);
            }
            for(int a=0;a<2;++a)
                dst->uv[a]=((float)source->renderUV[a]+
                   ((float)source->renderUVFrac[a]/256.0f))*(1.0f/255.0f);
            dst->wheel=(float)tri->wheel;
        }
    }

    /* All GL calls are deferred to DrawAllSplits on the real GL render
     * thread. Reuse the same VAO/VBO for every bot with this mesh. */
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&prevArray);
    if(entry->vbo)
        glDeleteBuffers(1,&entry->vbo);
    if(entry->vao)
        glDeleteVertexArrays(1,&entry->vao);
    entry->vbo=entry->vao=0;
    glGenVertexArrays(1,&entry->vao);
    glGenBuffers(1,&entry->vbo);
    if(entry->vao && entry->vbo)
    {
        glBindVertexArray(entry->vao);
        glBindBuffer(GL_ARRAY_BUFFER,entry->vbo);
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,verts,GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,
            sizeof(*verts),(void *)offsetof(struct NativeGpuModelQAVertex,p));
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,
            sizeof(*verts),(void *)offsetof(struct NativeGpuModelQAVertex,uv));
        glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,
            sizeof(*verts),(void *)offsetof(struct NativeGpuModelQAVertex,rgb));
        glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,
            sizeof(*verts),(void *)offsetof(struct NativeGpuModelQAVertex,wheel));
        glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,
            sizeof(*verts),(void *)offsetof(struct NativeGpuModelQAVertex,renderP));
    }
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)prevArray);
    err=glGetError();
    free(verts);
    if(!entry->vbo || !entry->vao || err!=GL_NO_ERROR)
    {
        Platform_LogWarn("[MODEL GPU QA] VBO create failed: tris=%d glError=0x%x\n",
             mesh->triangleCount,(unsigned)err);
        if(entry->vbo)glDeleteBuffers(1,&entry->vbo);
        if(entry->vao)glDeleteVertexArrays(1,&entry->vao);
        entry->vbo=entry->vao=0;
        return 0;
    }

    if(cached)
    {
        s_gpuModelQABytesTotal-=entry->bytes;
        s_gpuModelQATriTotal-=(u64)entry->residentCount;
    }
    else
        ++s_gpuModelQACount;
    entry->mesh=mesh;
    entry->triangleSource=mesh->triangles;
    entry->sourceCount=mesh->triangleCount;
    entry->residentCount=mesh->triangleCount;
    entry->bytes=bytes;
    s_gpuModelQABytesTotal+=(u64)bytes;
    s_gpuModelQATriTotal+=(u64)triCount;
    Platform_Log("[MODEL GPU QA] resident mesh=%d tris=%d verts=%u bytes=%u wheelMask=0x%x totalMeshes=%d totalBytes=%llu GL=OK\n",
        s_gpuModelQACount,mesh->triangleCount,(unsigned)totalVerts,
        (unsigned)bytes,mesh->wheelMask,s_gpuModelQACount,
        (unsigned long long)s_gpuModelQABytesTotal);
    return 1;
}

/* First real GPU-side vertex processing/draw. Never touches game pixels:
 * rasterize an already resident OBJ with GPU vertex shader into a private
 * 128x128 framebuffer, verify nonempty output, restore all relevant GL state.
 * This validates VBO layout/shader/fbo on RX570 before OT-depth integration. */
static void NativeGpuModelQaDrawShaderProbe(void)
{
    static int checked,enabled,done;
    if(!checked)
    {
        FILE *f=fopen("debug/gpu_models_shader_probe_qa.enable","rb");
        enabled=(f!=NULL);
        if(f)fclose(f);
        checked=1;
    }
    if(done || !enabled || s_gpuModelQACount<=0 ||
       !NativePerf_IsStressTestMeasuring())
        return;
    done=1;
    const struct NativeGpuModelQAEntry *entry=&s_gpuModelQA[0];
    if(entry->mesh==NULL || entry->mesh->triangles==NULL ||
       entry->triangleSource!=entry->mesh->triangles || entry->vao==0)
        return;

    static const char vs[]=
        "#version 140\n"
        "in vec3 a_position;\n"
        "in vec3 a_rgb;\n"
        "in float a_wheel;\n"
        "uniform vec3 u_center;\n"
        "uniform vec3 u_range;\n"
        "uniform float u_spin;\n"
        "out vec3 v_rgb;\n"
        "void main() {\n"
        " vec3 p=a_position;\n"
        " if(a_wheel>=0.0) {\n"
        "  float s=sin(u_spin), c=cos(u_spin);\n"
        "  p.yz=vec2(c*p.y-s*p.z,s*p.y+c*p.z);\n"
        " }\n"
        " gl_Position=vec4((p-u_center)*u_range*0.84,1.0);\n"
        " v_rgb=clamp(a_rgb,vec3(0.14),vec3(1.0));\n"
        "}\n";
    static const char fs[]=
        "#version 140\n"
        "in vec3 v_rgb;\n"
        "out vec4 outColor;\n"
        "void main(){ outColor=vec4(v_rgb,1.0); }\n";

    GLuint vertex=0,fragment=0,program=0,fb=0,color=0;
    GLint ok=0;
    GLint oldDrawFbo=0,oldReadFbo=0,oldVao=0,oldProgram=0;
    GLint oldActiveTex=0,oldBoundTex=0,oldPackAlign=4;
    GLint oldViewport[4]={0};
    GLfloat oldClearColor[4]={0};
    GLboolean oldBlend,oldDepth,oldStencil,oldScissor,oldCull,oldClip0;
    u8 pixels[128*128*4]={0};
    int activePixels=0,glError=0,shaderLinked=0;

    oldBlend=glIsEnabled(GL_BLEND);
    oldDepth=glIsEnabled(GL_DEPTH_TEST);
    oldStencil=glIsEnabled(GL_STENCIL_TEST);
    oldScissor=glIsEnabled(GL_SCISSOR_TEST);
    oldCull=glIsEnabled(GL_CULL_FACE);
    oldClip0=glIsEnabled(GL_CLIP_DISTANCE0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldReadFbo);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActiveTex);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldBoundTex);
    glGetIntegerv(GL_PACK_ALIGNMENT,&oldPackAlign);
    glGetIntegerv(GL_VIEWPORT,oldViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,oldClearColor);

    vertex=glCreateShader(GL_VERTEX_SHADER);
    fragment=glCreateShader(GL_FRAGMENT_SHADER);
    if(!vertex || !fragment)goto cleanup;
    const GLchar *sourceVs=vs,*sourceFs=fs;
    glShaderSource(vertex,1,&sourceVs,NULL);
    glCompileShader(vertex);
    glGetShaderiv(vertex,GL_COMPILE_STATUS,&ok);
    if(!ok)
    {
        char msg[768]={0};
        glGetShaderInfoLog(vertex,sizeof(msg),NULL,msg);
        Platform_LogWarn("[MODEL GPU QA] vertex shader fail: %s\n",msg);
        goto cleanup;
    }
    glShaderSource(fragment,1,&sourceFs,NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment,GL_COMPILE_STATUS,&ok);
    if(!ok)
    {
        char msg[768]={0};
        glGetShaderInfoLog(fragment,sizeof(msg),NULL,msg);
        Platform_LogWarn("[MODEL GPU QA] fragment shader fail: %s\n",msg);
        goto cleanup;
    }
    program=glCreateProgram();
    if(!program)goto cleanup;
    glAttachShader(program,vertex);
    glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"a_position");
    glBindAttribLocation(program,2,"a_rgb");
    glBindAttribLocation(program,3,"a_wheel");
    glLinkProgram(program);
    glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok)
    {
        char msg[768]={0};
        glGetProgramInfoLog(program,sizeof(msg),NULL,msg);
        Platform_LogWarn("[MODEL GPU QA] shader link fail: %s\n",msg);
        goto cleanup;
    }
    shaderLinked=1;

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&color);
    glBindTexture(GL_TEXTURE_2D,color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&fb);
    glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D,color,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
    {
        Platform_LogWarn("[MODEL GPU QA] hidden FBO not complete\n");
        goto cleanup;
    }

    float lower[3]={1e30f,1e30f,1e30f};
    float upper[3]={-1e30f,-1e30f,-1e30f};
    const struct NativeObjMesh *mesh=entry->mesh;
    for(int i=0;i<mesh->triangleCount;++i)
        for(int v=0;v<3;++v)
            for(int a=0;a<3;++a)
            {
                float k=mesh->triangles[i].v[v].p[a];
                if(k<lower[a])lower[a]=k;
                if(k>upper[a])upper[a]=k;
            }
    GLfloat center[3],scale[3];
    for(int a=0;a<3;++a)
    {
        center[a]=(lower[a]+upper[a])*0.5f;
        scale[a]=2.0f/fmaxf(upper[a]-lower[a],0.001f);
    }
    glViewport(0,0,128,128);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_CLIP_DISTANCE0);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glUniform3fv(glGetUniformLocation(program,"u_center"),1,center);
    glUniform3fv(glGetUniformLocation(program,"u_range"),1,scale);
    glUniform1f(glGetUniformLocation(program,"u_spin"),0.0f);
    glBindVertexArray(entry->vao);
    glDrawArrays(GL_TRIANGLES,0,entry->residentCount*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(int i=0;i<128*128;++i)
        if(pixels[i*4]>12 || pixels[i*4+1]>12 || pixels[i*4+2]>12)
            ++activePixels;
    glError=(int)glGetError();

cleanup:
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)oldDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)oldReadFbo);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glUseProgram((GLuint)oldProgram);
    glBindVertexArray((GLuint)oldVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,0);
    if(fb)glDeleteFramebuffers(1,&fb);
    if(color)glDeleteTextures(1,&color);
    if(program)glDeleteProgram(program);
    if(vertex)glDeleteShader(vertex);
    if(fragment)glDeleteShader(fragment);
    glActiveTexture((GLenum)oldActiveTex);
    glBindTexture(GL_TEXTURE_2D,(GLuint)oldBoundTex);
    glPixelStorei(GL_PACK_ALIGNMENT,oldPackAlign);
    glClearColor(oldClearColor[0],oldClearColor[1],oldClearColor[2],oldClearColor[3]);
    if(oldBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(oldDepth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(oldStencil)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
    if(oldScissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    if(oldCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(oldClip0)glEnable(GL_CLIP_DISTANCE0);else glDisable(GL_CLIP_DISTANCE0);
    Platform_Log("[MODEL GPU QA] hidden shader draw shaderLinked=%d meshTris=%d activePixels=%d/16384 glError=0x%x PS1_PIXELS_UNCHANGED=1\n",
        shaderLinked,entry->residentCount,activePixels,(unsigned)glError);
}

/* Compare the real VBO vertex against the unchanged PS1/GTE projection.
 * Runs in RASTERIZER_DISCARD, never writes the game's framebuffer, and restores
 * GL buffer/VAO/shader/TF state. Rounding differences are diagnosed, not hidden. */
static void NativeGpuModelQaCompareProjections(void)
{
    static GLuint program,vertex,tf;
    static int checked,ready,failed;
    if(!NativeGpuModel_ProjectionQaActive())
        return;
    struct {
        const struct NativeObjMesh *mesh;
        struct NativeGpuModelQASample sample;
    } work[MODEL_PROBE_BATCH];
    int n=0;
    NativeGpuModelQaLock();
    for(int i=0;i<s_gpuProjectionPending;++i)
    {
        work[n].mesh=s_gpuProjectionSamples[i].mesh;
        work[n].sample=s_gpuProjectionSamples[i].sample;
        ++n;
    }
    s_gpuProjectionPending=0;
    NativeGpuModelQaUnlock();
    if(n==0 || failed)return;
    if(!checked)
    {
        checked=1;
        static const char shader[]=
            "#version 140\n"
            "in vec3 a_renderP;\n"
            "uniform vec3 u_offset,u_scale,u_r0,u_r1,u_r2,u_t;\n"
            "uniform vec3 u_projection;\n"
            "out vec4 tf_screen;\n"
            "void main(){\n"
            " vec3 local=trunc(clamp((a_renderP+u_offset)*u_scale,"
            "vec3(-32767.0),vec3(32767.0)));\n"
            " vec3 eye=vec3(dot(local,u_r0),dot(local,u_r1),"
            "dot(local,u_r2))+u_t;\n"
            " float z=max(eye.z,1.0);\n"
            " tf_screen=vec4((u_projection.y+u_projection.x*eye.x/z)*0.75+63.0,"
            "u_projection.z+u_projection.x*eye.y/z,eye.z,1.0);\n"
            " gl_Position=vec4(0,0,0,1);\n"
            "}\n";
        vertex=glCreateShader(GL_VERTEX_SHADER);
        const GLchar *src=shader;
        glShaderSource(vertex,1,&src,NULL);
        glCompileShader(vertex);
        GLint ok=0;
        glGetShaderiv(vertex,GL_COMPILE_STATUS,&ok);
        if(!ok)
        {
            char log[768]={0};
            glGetShaderInfoLog(vertex,sizeof(log),NULL,log);
            Platform_LogWarn("[MODEL GPU QA] projection vertex compile failure: %s\n",log);
            failed=1;return;
        }
        program=glCreateProgram();
        glAttachShader(program,vertex);
        glBindAttribLocation(program,4,"a_renderP");
        const GLchar *varyings[]={"tf_screen"};
        glTransformFeedbackVaryings(program,1,varyings,GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(program);
        glGetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok)
        {
            char log[768]={0};
            glGetProgramInfoLog(program,sizeof(log),NULL,log);
            Platform_LogWarn("[MODEL GPU QA] projection link failure: %s\n",log);
            failed=1;return;
        }
        glGenBuffers(1,&tf);
        if(!tf){failed=1;return;}
        ready=1;
        Platform_Log("[MODEL GPU QA] projection transform feedback ready GPU=ON, PS1_PIXELS_UNCHANGED=1\n");
    }
    if(!ready)return;
    GLint prevProgram=0,prevVao=0,prevTf=0,prevTfIndexed=0;
    GLboolean oldDiscard=glIsEnabled(GL_RASTERIZER_DISCARD);
    glGetIntegerv(GL_CURRENT_PROGRAM,&prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&prevVao);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,&prevTf);
    glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,0,&prevTfIndexed);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,tf);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
        MODEL_PROBE_BATCH*4*sizeof(float),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,tf);
    glUseProgram(program);
    glEnable(GL_RASTERIZER_DISCARD);
    int drawCount=0;
    struct NativeGpuModelQASample sampled[MODEL_PROBE_BATCH];
    glBeginTransformFeedback(GL_POINTS);
    for(int i=0;i<n;++i)
    {
        struct NativeGpuModelQAEntry *e=NativeGpuModelQaFind(work[i].mesh);
        const struct NativeGpuModelQASample *s=&work[i].sample;
        if(!e || !e->vao || e->triangleSource!=work[i].mesh->triangles ||
           s->vertexIndex<0 || s->vertexIndex>=e->residentCount*3)
            continue;
        glUniform3fv(glGetUniformLocation(program,"u_offset"),1,s->offset);
        glUniform3fv(glGetUniformLocation(program,"u_scale"),1,s->scale);
        glUniform3fv(glGetUniformLocation(program,"u_r0"),1,&s->rot[0]);
        glUniform3fv(glGetUniformLocation(program,"u_r1"),1,&s->rot[3]);
        glUniform3fv(glGetUniformLocation(program,"u_r2"),1,&s->rot[6]);
        glUniform3fv(glGetUniformLocation(program,"u_t"),1,s->translation);
        glUniform3f(glGetUniformLocation(program,"u_projection"),
            s->h,s->centerX,s->centerY);
        glBindVertexArray(e->vao);
        glDrawArrays(GL_POINTS,s->vertexIndex,1);
        sampled[drawCount++]=*s;
    }
    glEndTransformFeedback();
    float result[MODEL_PROBE_BATCH*4]={0};
    if(drawCount>0)
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,
            drawCount*4*sizeof(float),result);
    GLenum err=glGetError();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,(GLuint)prevTfIndexed);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,(GLuint)prevTf);
    glBindVertexArray((GLuint)prevVao);
    glUseProgram((GLuint)prevProgram);
    if(!oldDiscard)glDisable(GL_RASTERIZER_DISCARD);
    if(err!=GL_NO_ERROR)
    {
        Platform_LogWarn("[MODEL GPU QA] projection TF failed gl=0x%x after=%d\n",
                         (unsigned)err,drawCount);
        failed=1;return;
    }
    for(int i=0;i<drawCount;++i)
    {
        float dx=fabsf(result[i*4]-sampled[i].expectedX);
        float dy=fabsf(result[i*4+1]-sampled[i].expectedY);
        float maxerr=fmaxf(dx,dy);
        s_gpuProjectionXYErrorSum+=(double)(dx+dy)*0.5;
        if(maxerr>s_gpuProjectionMaxXYError)s_gpuProjectionMaxXYError=maxerr;
        if(maxerr>2.0f)++s_gpuProjectionMismatches2Px;
        ++s_gpuProjectionCompared;
        if(s_gpuProjectionCompared<=8)
            Platform_Log("[MODEL GPU QA] sample=%d gpuXY=(%.3f,%.3f) ps1XY=(%d,%d) gpuDepth=%.3f ps1Depth=%d\n",
                s_gpuProjectionCompared,result[i*4],result[i*4+1],
                sampled[i].expectedX,sampled[i].expectedY,
                result[i*4+2],sampled[i].expectedDepth);
    }
    if(s_gpuProjectionCompared>=MODEL_PROBE_MAX &&
       s_gpuProjectionCompared-drawCount<MODEL_PROBE_MAX)
        Platform_Log("[MODEL GPU QA] projection QA samples=%d meanPixelError=%.3f maxPixelError=%.3f >2px=%d PS1_PIXELS_UNCHANGED=1\n",
            s_gpuProjectionCompared,
            s_gpuProjectionXYErrorSum/s_gpuProjectionCompared,
            s_gpuProjectionMaxXYError,s_gpuProjectionMismatches2Px);
}

void NativeGpuModel_FlushStaged(void)
{
    const struct NativeObjMesh *pending[NATIVE_GPU_MODEL_QA_MAX_QUEUE];
    int count;
    NativeGpuModelQaLock();
    count=s_gpuModelQAQueueCount;
    for(int i=0;i<count;++i)
        pending[i]=s_gpuModelQAQueue[i];
    s_gpuModelQAQueueCount=0;
    NativeGpuModelQaUnlock();
    if(!count)
        return;
    for(int i=0;i<count;++i)
        if(pending[i]!=NULL)
            NativeGpuModelQaUpload(pending[i]);
    NativeGpuModelQaDrawShaderProbe();
    NativeGpuModelQaCompareProjections();
}

#else
int NativeGpuModel_ProjectionQaActive(void) {return 0;}
void NativeGpuModel_StageRigidSample(const struct NativeObjMesh *mesh,
    const struct NativeGpuModelQASample *sample) {(void)mesh;(void)sample;}
void NativeGpuModel_StageMesh(const struct NativeObjMesh *mesh) {(void)mesh;}
void NativeGpuModel_FlushStaged(void) {}
#endif
