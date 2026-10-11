/* Crash Forge: Racing - 2026-10-10 GPU resident LEVEL VERTEX transform parity QA.
 * Static retail vertices live in OpenGL GL_STATIC_DRAW, one per vertex.
 * At runtime compare SAME original PS1 GTE projection to a GPU GLSL transform
 * feedback result. Never draws to the game framebuffer; does not replace OT.
 * Only debug/level_pc_geometry_parity_qa.enable and during stress measurement.
 * Designed as a prerequisite to GPU-side per-block LOD/tessellation batching.
 */
#if defined(CTR_NATIVE)
#define CFR_LEVEL_GPU_QA_BATCH 96
#define CFR_LEVEL_GPU_QA_TOTAL 960
typedef struct { s16 x,y,z; } CFRLevelQAShortPosition;
typedef struct {
    int vertexIndex;
    s16 referenceX,referenceY;
    u16 referenceDepth;
} CFRLevelGPUQASample;
static struct {
    const void *meshKey;
    GLuint vertexBuffer,indexBuffer,feedbackBuffer,vao,program;
    int vertexCount, valid,shaderFailed,shaderLogged;
    float m[9],t[3],h,wide,cx,cy;
    CFRLevelGPUQASample samples[CFR_LEVEL_GPU_QA_BATCH];
    int pending,compared,above2px,greaterDepth2;
    int invocations,candidates;
    double sumXY,sumDepth;
    float maxXY,maxDepth;
    /* A GPU projection outside the display or in the GTE saturation zone
     * is not evidence of a visible mesh mismatch. Track separately. */
    int visibleSamples,visibleAbove2px,nearSamples,outsideSamples;
    double visibleSumXY;
    float visibleMaxXY;
} s_cfrLevelGPUQA;

int NativeRenderer_LevelPCProjectionQAEnabled(void)
{
    static int checked,enabled;
    if(!checked) {
        FILE *f=fopen("debug/level_pc_geometry_parity_qa.enable","rb");
        enabled=f!=NULL;
        if(f)fclose(f);
        checked=1;
    }
    return enabled;
}
void NativeRenderer_LevelPCProjectionQAReset(void)
{
    if(s_cfrLevelGPUQA.vao)glDeleteVertexArrays(1,&s_cfrLevelGPUQA.vao);
    if(s_cfrLevelGPUQA.vertexBuffer)glDeleteBuffers(1,&s_cfrLevelGPUQA.vertexBuffer);
    if(s_cfrLevelGPUQA.indexBuffer)glDeleteBuffers(1,&s_cfrLevelGPUQA.indexBuffer);
    if(s_cfrLevelGPUQA.feedbackBuffer)glDeleteBuffers(1,&s_cfrLevelGPUQA.feedbackBuffer);
    if(s_cfrLevelGPUQA.program)glDeleteProgram(s_cfrLevelGPUQA.program);
    memset(&s_cfrLevelGPUQA,0,sizeof(s_cfrLevelGPUQA));
}
void NativeRenderer_LevelPCProjectionQAUpload(const void *meshKey,
    const void *vertexArray,int vertexCount)
{
    if(!NativeRenderer_LevelPCProjectionQAEnabled() || !meshKey ||
       !vertexArray || vertexCount<1 || vertexCount>500000)return;
    NativeRenderer_LevelPCProjectionQAReset();
    CFRLevelQAShortPosition *tmp=(CFRLevelQAShortPosition *)
        malloc((size_t)vertexCount*sizeof(*tmp));
    if(!tmp)return;
    const struct LevVertex *v=(const struct LevVertex *)vertexArray;
    for(int i=0;i<vertexCount;++i) {
        tmp[i].x=v[i].pos.x;
        tmp[i].y=v[i].pos.y;
        tmp[i].z=v[i].pos.z;
    }
    GLint oldVAO=0,oldVBO=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldVBO);
    glGenBuffers(1,&s_cfrLevelGPUQA.vertexBuffer);
    glGenBuffers(1,&s_cfrLevelGPUQA.indexBuffer);
    glGenVertexArrays(1,&s_cfrLevelGPUQA.vao);
    if(s_cfrLevelGPUQA.vertexBuffer && s_cfrLevelGPUQA.indexBuffer &&
        s_cfrLevelGPUQA.vao) {
        glBindVertexArray(s_cfrLevelGPUQA.vao);
        glBindBuffer(GL_ARRAY_BUFFER,s_cfrLevelGPUQA.vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertexCount*sizeof(*tmp)),
            tmp,GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,3,GL_SHORT,GL_FALSE,sizeof(*tmp),(void *)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s_cfrLevelGPUQA.indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            CFR_LEVEL_GPU_QA_BATCH*sizeof(GLuint),NULL,GL_STREAM_DRAW);
        s_cfrLevelGPUQA.valid=(glGetError()==GL_NO_ERROR);
        s_cfrLevelGPUQA.meshKey=meshKey;
        s_cfrLevelGPUQA.vertexCount=vertexCount;
        Platform_Log("[LEVEL GPU GEOM QA] immutable world vertex VBO: verts=%d bytes=%u valid=%d gamePixelsUnchanged=1\n",
            vertexCount,(unsigned)(vertexCount*sizeof(*tmp)),s_cfrLevelGPUQA.valid);
    }
    glBindVertexArray((GLuint)oldVAO);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldVBO);
    free(tmp);
}
void NativeRenderer_LevelPCProjectionQABegin(const void *meshKey,
    const s16 viewM[3][3],const s32 viewT[3],int screen,int wide,
    s32 offsetXQ16,s32 offsetYQ16)
{
    s_cfrLevelGPUQA.pending=0;
    s_cfrLevelGPUQA.candidates=0;
    if(!NativeRenderer_LevelPCProjectionQAEnabled() ||
       !s_cfrLevelGPUQA.valid || meshKey!=s_cfrLevelGPUQA.meshKey ||
       !viewM || !viewT || screen<=0)return;
    for(int row=0;row<3;++row) {
        s_cfrLevelGPUQA.t[row]=(float)viewT[row];
        for(int col=0;col<3;++col)
            s_cfrLevelGPUQA.m[row*3+col]=(float)viewM[row][col]/4096.0f;
    }
    s_cfrLevelGPUQA.h=(float)screen;
    s_cfrLevelGPUQA.wide=wide?0.75f:1.0f;
    s_cfrLevelGPUQA.cx=(float)offsetXQ16/65536.0f;
    s_cfrLevelGPUQA.cy=(float)offsetYQ16/65536.0f;
}
void NativeRenderer_LevelPCProjectionQARecord(const void *meshKey,
    const void *vertexBase,const void *vertex,
    int sx,int sy,int depth)
{
    if(!NativeRenderer_LevelPCProjectionQAEnabled() ||
       !NativePerf_IsStressTestMeasuring() ||
       !s_cfrLevelGPUQA.valid || meshKey!=s_cfrLevelGPUQA.meshKey ||
       !vertexBase || !vertex || s_cfrLevelGPUQA.compared>=CFR_LEVEL_GPU_QA_TOTAL ||
       s_cfrLevelGPUQA.pending>=CFR_LEVEL_GPU_QA_BATCH)return;
    if(((++s_cfrLevelGPUQA.candidates)&7)!=0)return;
    const uintptr_t base=(uintptr_t)vertexBase,at=(uintptr_t)vertex;
    if(at<base)return;
    const uintptr_t diff=at-base;
    if(diff%sizeof(struct LevVertex))return;
    const int index=(int)(diff/sizeof(struct LevVertex));
    if(index<0 || index>=s_cfrLevelGPUQA.vertexCount || depth<32 || depth>=64000)return;
    CFRLevelGPUQASample *s=&s_cfrLevelGPUQA.samples[s_cfrLevelGPUQA.pending++];
    s->vertexIndex=index;
    s->referenceX=(s16)sx;
    s->referenceY=(s16)sy;
    s->referenceDepth=(u16)depth;
}
static int NativeRenderer_LevelPCProjectionQAShader(void)
{
    if(s_cfrLevelGPUQA.program)return 1;
    if(s_cfrLevelGPUQA.shaderFailed)return 0;
    static const char source[]=
        "#version 140\n"
        "in vec3 a_source;\n"
        "uniform vec3 u_m0,u_m1,u_m2,u_t;\n"
        "uniform float u_h,u_wide,u_cx,u_cy;\n"
        "out vec4 tf_projected;\n"
        "void main(){\n"
        " vec3 eye=vec3(dot(a_source,u_m0),dot(a_source,u_m1),"
        "dot(a_source,u_m2))+u_t;\n"
        " float z=max(eye.z,1.0);\n"
        " float hOverZ=min(u_h/z,131071.0/65536.0);\n"
        " tf_projected=vec4(clamp(u_cx+eye.x*hOverZ*u_wide,-1024.0,1023.0),"
        "clamp(u_cy+eye.y*hOverZ,-1024.0,1023.0),clamp(eye.z,0.0,65535.0),1.0);\n"
        " gl_Position=vec4(0.0,0.0,0.0,1.0);\n"
        "}\n";
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);
    const GLchar *src=source;
    /* Developer-only GLSL probe file: future projection experiments need no
     * recompilation. The release build still uses the embedded shader. */
    char qaSource[4096];
    FILE *testShader=fopen("debug/level_pc_geometry_shader_qa.glsl","rb");
    if(testShader){
        size_t bytes=fread(qaSource,1,sizeof(qaSource)-1,testShader);
        fclose(testShader);
        qaSource[bytes]='\0';
        if(bytes>24){
            src=qaSource;
            Platform_Log("[LEVEL GPU GEOM QA] using temporary GLSL override bytes=%u\n",(unsigned)bytes);
        }
    }
    glShaderSource(shader,1,&src,NULL);
    glCompileShader(shader);
    GLint ok=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){
        char info[900]={0};
        glGetShaderInfoLog(shader,sizeof(info),NULL,info);
        Platform_LogError("[LEVEL GPU GEOM QA] shader compile failure %s\n",info);
        glDeleteShader(shader);s_cfrLevelGPUQA.shaderFailed=1;return 0;
    }
    GLuint program=glCreateProgram();
    glAttachShader(program,shader);
    glBindAttribLocation(program,0,"a_source");
    const GLchar *outNames[]={"tf_projected"};
    glTransformFeedbackVaryings(program,1,outNames,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);glDeleteShader(shader);
    glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){
        char info[900]={0};
        glGetProgramInfoLog(program,sizeof(info),NULL,info);
        Platform_LogError("[LEVEL GPU GEOM QA] shader link failure %s\n",info);
        glDeleteProgram(program);s_cfrLevelGPUQA.shaderFailed=1;return 0;
    }
    glGenBuffers(1,&s_cfrLevelGPUQA.feedbackBuffer);
    if(!s_cfrLevelGPUQA.feedbackBuffer){
        glDeleteProgram(program);s_cfrLevelGPUQA.shaderFailed=1;return 0;
    }
    s_cfrLevelGPUQA.program=program;
    Platform_Log("[LEVEL GPU GEOM QA] GPU matrix shader ready; rasterizer discarded, framebuffer unchanged\n");
    return 1;
}
void NativeRenderer_LevelPCProjectionQAFlush(void)
{
    if(!NativeRenderer_LevelPCProjectionQAEnabled())return;
    int n=s_cfrLevelGPUQA.pending;
    s_cfrLevelGPUQA.pending=0;
    if(!s_cfrLevelGPUQA.valid || n<=0 ||
       s_cfrLevelGPUQA.compared>=CFR_LEVEL_GPU_QA_TOTAL ||
       !NativePerf_IsStressTestMeasuring() ||
       !NativeRenderer_LevelPCProjectionQAShader())return;
    GLint oldProg=0,oldVAO=0,oldTF=0,oldIndexed=0;
    GLboolean oldDiscard=glIsEnabled(GL_RASTERIZER_DISCARD);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVAO);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,&oldTF);
    glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,0,&oldIndexed);
    GLuint indices[CFR_LEVEL_GPU_QA_BATCH];
    for(int i=0;i<n;++i)indices[i]=(GLuint)s_cfrLevelGPUQA.samples[i].vertexIndex;
    glBindVertexArray(s_cfrLevelGPUQA.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s_cfrLevelGPUQA.indexBuffer);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,n*sizeof(GLuint),indices);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,s_cfrLevelGPUQA.feedbackBuffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,n*4*sizeof(float),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,s_cfrLevelGPUQA.feedbackBuffer);
    glUseProgram(s_cfrLevelGPUQA.program);
    glUniform3fv(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_m0"),1,&s_cfrLevelGPUQA.m[0]);
    glUniform3fv(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_m1"),1,&s_cfrLevelGPUQA.m[3]);
    glUniform3fv(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_m2"),1,&s_cfrLevelGPUQA.m[6]);
    glUniform3fv(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_t"),1,s_cfrLevelGPUQA.t);
    glUniform1f(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_h"),s_cfrLevelGPUQA.h);
    glUniform1f(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_wide"),s_cfrLevelGPUQA.wide);
    glUniform1f(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_cx"),s_cfrLevelGPUQA.cx);
    glUniform1f(glGetUniformLocation(s_cfrLevelGPUQA.program,"u_cy"),s_cfrLevelGPUQA.cy);
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);
    glDrawElements(GL_POINTS,n,GL_UNSIGNED_INT,(void *)0);
    glEndTransformFeedback();
    float projected[CFR_LEVEL_GPU_QA_BATCH*4]={0};
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,
        n*4*sizeof(float),projected);
    GLenum error=glGetError();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,(GLuint)oldIndexed);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,(GLuint)oldTF);
    glBindVertexArray((GLuint)oldVAO);
    glUseProgram((GLuint)oldProg);
    if(!oldDiscard)glDisable(GL_RASTERIZER_DISCARD);
    if(error!=GL_NO_ERROR){
        Platform_LogWarn("[LEVEL GPU GEOM QA] transform feedback error=0x%x samples=%d\n",
            (unsigned)error,n);
        s_cfrLevelGPUQA.shaderFailed=1;return;
    }
    ++s_cfrLevelGPUQA.invocations;
    for(int i=0;i<n && s_cfrLevelGPUQA.compared<CFR_LEVEL_GPU_QA_TOTAL;++i){
        const CFRLevelGPUQASample *s=&s_cfrLevelGPUQA.samples[i];
        float dx=fabsf(projected[i*4]-s->referenceX);
        float dy=fabsf(projected[i*4+1]-s->referenceY);
        float dz=fabsf(projected[i*4+2]-s->referenceDepth);
        float maxXY=fmaxf(dx,dy);
        s_cfrLevelGPUQA.sumXY+=(double)(dx+dy)*0.5;
        s_cfrLevelGPUQA.sumDepth+=dz;
        if(maxXY>s_cfrLevelGPUQA.maxXY)s_cfrLevelGPUQA.maxXY=maxXY;
        if(dz>s_cfrLevelGPUQA.maxDepth)s_cfrLevelGPUQA.maxDepth=dz;
        if(maxXY>2.0f){
            ++s_cfrLevelGPUQA.above2px;
            if(s_cfrLevelGPUQA.above2px<=20)
                Platform_Log("[LEVEL GPU GEOM QA] mismatch=%d index=%d G=(%.2f,%.2f,Z%.1f) PS1=(%d,%d,Z%d) h=%.1f wide=%.2f\n",
                    s_cfrLevelGPUQA.above2px,s->vertexIndex,
                    projected[i*4],projected[i*4+1],projected[i*4+2],
                    s->referenceX,s->referenceY,s->referenceDepth,
                    s_cfrLevelGPUQA.h,s_cfrLevelGPUQA.wide);
        }
        if(dz>2.0f)++s_cfrLevelGPUQA.greaterDepth2;
        /* GTE clamps perspective when H >= 2*Z. Those vertices are often
         * behind the near clipping plane and distort whole-scene XY means. */
        const float screenW=s_cfrLevelGPUQA.cx*2.0f;
        const float screenH=s_cfrLevelGPUQA.cy*2.0f;
        const int nearSaturated=(float)s->referenceDepth*2.0f<=s_cfrLevelGPUQA.h+2.0f;
        const int visible=(screenW>=64.0f && screenH>=64.0f &&
            projected[i*4]>=0.0f && projected[i*4]<=screenW &&
            projected[i*4+1]>=0.0f && projected[i*4+1]<=screenH &&
            (float)s->referenceX>=0.0f && (float)s->referenceX<=screenW &&
            (float)s->referenceY>=0.0f && (float)s->referenceY<=screenH);
        if(nearSaturated) ++s_cfrLevelGPUQA.nearSamples;
        else if(!visible) ++s_cfrLevelGPUQA.outsideSamples;
        else {
            ++s_cfrLevelGPUQA.visibleSamples;
            s_cfrLevelGPUQA.visibleSumXY+=(double)(dx+dy)*0.5;
            if(maxXY>s_cfrLevelGPUQA.visibleMaxXY)
                s_cfrLevelGPUQA.visibleMaxXY=maxXY;
            if(maxXY>2.0f)++s_cfrLevelGPUQA.visibleAbove2px;
        }
        ++s_cfrLevelGPUQA.compared;
        if(s_cfrLevelGPUQA.compared<=10)
            Platform_Log("[LEVEL GPU GEOM QA] sample=%d GPU=(%.3f,%.3f,Z%.3f) PS1=(%d,%d,Z%d) center=(%.2f,%.2f) wide=%.2f\n",
                s_cfrLevelGPUQA.compared,projected[i*4],projected[i*4+1],
                projected[i*4+2],s->referenceX,s->referenceY,s->referenceDepth,
                s_cfrLevelGPUQA.cx,s_cfrLevelGPUQA.cy,s_cfrLevelGPUQA.wide);
    }
    if(s_cfrLevelGPUQA.compared>=CFR_LEVEL_GPU_QA_TOTAL) {
        Platform_Log("[LEVEL GPU GEOM QA] FINAL samples=%d meanXY=%.3fpx maxXY=%.3fpx >2px=%d meanZ=%.3f maxZ=%.3f >2depth=%d probes=%d immutableGPUVertex=1 pixelsUnchanged=1\n",
            s_cfrLevelGPUQA.compared,
            s_cfrLevelGPUQA.sumXY/s_cfrLevelGPUQA.compared,
            s_cfrLevelGPUQA.maxXY,s_cfrLevelGPUQA.above2px,
            s_cfrLevelGPUQA.sumDepth/s_cfrLevelGPUQA.compared,
            s_cfrLevelGPUQA.maxDepth,s_cfrLevelGPUQA.greaterDepth2,
            s_cfrLevelGPUQA.invocations);
        Platform_Log("[LEVEL GPU GEOM QA] ONSCREEN visible=%d meanXY=%.3fpx maxXY=%.3fpx gt2px=%d nearSaturated=%d outOfViewport=%d (approx display bounds from center)\n",
            s_cfrLevelGPUQA.visibleSamples,
            s_cfrLevelGPUQA.visibleSamples ?
                s_cfrLevelGPUQA.visibleSumXY/(double)s_cfrLevelGPUQA.visibleSamples : 0.0,
            s_cfrLevelGPUQA.visibleMaxXY,
            s_cfrLevelGPUQA.visibleAbove2px,
            s_cfrLevelGPUQA.nearSamples,
            s_cfrLevelGPUQA.outsideSamples);
    }
}
#endif
