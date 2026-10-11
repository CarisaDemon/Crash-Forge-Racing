/* Crash Forge: Racing -- native terrain subdivision GPU parity, 2026-10-10.
 * GPU interpolates original PS1 3D midpoint POS, vertex RGB, UV bytes.
 * Transform feedback is offscreen; original CPU near-recursion and OT packet
 * output remain authoritative. Enabled only with geometry QA marker/stress.
 */
#if defined(CTR_NATIVE)
#define CFR_NEAR_QA_BATCH 64
#define CFR_NEAR_QA_LIMIT 1024
struct CFRNearSample {
    float in[16]; /* vertex A XYZ, B XYZ, RGB A, RGB B, UV A, UV B */
    float expected[8]; /* XYZ, RGB, UV expected PS1 arithmetic */
};
static struct {
    GLuint vbo,vao,tfo,program;
    int failed,pending,compared,mismatch,frames,sourceSkipped;
    double sumError;
    float worstError;
    struct CFRNearSample samples[CFR_NEAR_QA_BATCH];
} s_cfrNearQA;

void NativeRenderer_LevelPCMidpointQAReset(void)
{
    if(s_cfrNearQA.vbo)glDeleteBuffers(1,&s_cfrNearQA.vbo);
    if(s_cfrNearQA.vao)glDeleteVertexArrays(1,&s_cfrNearQA.vao);
    if(s_cfrNearQA.tfo)glDeleteBuffers(1,&s_cfrNearQA.tfo);
    if(s_cfrNearQA.program)glDeleteProgram(s_cfrNearQA.program);
    memset(&s_cfrNearQA,0,sizeof(s_cfrNearQA));
}
void NativeRenderer_LevelPCMidpointQARecord(
    const struct DrawLevelOvr1PScratchVertex *a,
    const struct DrawLevelOvr1PScratchVertex *b,
    const struct DrawLevelOvr1PScratchVertex *mid)
{
    if(!NativeRenderer_LevelPCProjectionQAEnabled() ||
       !NativePerf_IsStressTestMeasuring() || !a || !b || !mid ||
       s_cfrNearQA.compared>=CFR_NEAR_QA_LIMIT ||
       s_cfrNearQA.pending>=CFR_NEAR_QA_BATCH)return;
    if((++s_cfrNearQA.sourceSkipped%11)!=0)return;
    struct CFRNearSample *s=&s_cfrNearQA.samples[s_cfrNearQA.pending++];
    for(int j=0;j<3;++j) {
        s->in[j]=(float)a->pos[j];
        s->in[3+j]=(float)b->pos[j];
        s->in[6+j]=(float)a->color_hi[j];
        s->in[9+j]=(float)b->color_hi[j];
        s->expected[j]=(float)mid->pos[j];
        s->expected[3+j]=(float)mid->color_hi[j];
    }
    const u8 *aUv=(const u8 *)&a->flags;
    const u8 *bUv=(const u8 *)&b->flags;
    const u8 *midUv=(const u8 *)&mid->flags;
    for(int j=0;j<2;++j) {
        s->in[12+j]=(float)aUv[j];
        s->in[14+j]=(float)bUv[j];
        s->expected[6+j]=(float)midUv[j];
    }
}
static int NativeRenderer_LevelPCMidpointQAEnsureShader(void)
{
    if(s_cfrNearQA.failed)return 0;
    if(s_cfrNearQA.program)return 1;
    static const char src[]=
        "#version 140\n"
        "in vec3 a_posA,a_posB,a_colorA,a_colorB;\n"
        "in vec2 a_uvA,a_uvB;\n"
        "out vec3 tf_midpos,tf_midcol;\n"
        "out vec2 tf_miduv;\n"
        "void main(){\n"
        " tf_midpos=floor((a_posA+a_posB)*0.5);\n"
        " tf_midcol=floor((a_colorA+a_colorB)*0.5);\n"
        " tf_miduv=floor((a_uvA+a_uvB)*0.5);\n"
        " gl_Position=vec4(0.0,0.0,0.0,1.0);\n"
        "}\n";
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);
    const GLchar *ps=src;
    glShaderSource(shader,1,&ps,NULL);
    glCompileShader(shader);
    GLint good=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&good);
    if(!good) {
        char log[768]={0};
        glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        Platform_LogWarn("[LEVEL NEAR GPU QA] failed compile: %s\n",log);
        glDeleteShader(shader);
        s_cfrNearQA.failed=1;
        return 0;
    }
    s_cfrNearQA.program=glCreateProgram();
    glAttachShader(s_cfrNearQA.program,shader);
    glBindAttribLocation(s_cfrNearQA.program,0,"a_posA");
    glBindAttribLocation(s_cfrNearQA.program,1,"a_posB");
    glBindAttribLocation(s_cfrNearQA.program,2,"a_colorA");
    glBindAttribLocation(s_cfrNearQA.program,3,"a_colorB");
    glBindAttribLocation(s_cfrNearQA.program,4,"a_uvA");
    glBindAttribLocation(s_cfrNearQA.program,5,"a_uvB");
    const GLchar *varyings[]={"tf_midpos","tf_midcol","tf_miduv"};
    glTransformFeedbackVaryings(s_cfrNearQA.program,3,varyings,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(s_cfrNearQA.program);
    glDeleteShader(shader);
    glGetProgramiv(s_cfrNearQA.program,GL_LINK_STATUS,&good);
    if(!good) {
        char log[768]={0};
        glGetProgramInfoLog(s_cfrNearQA.program,sizeof(log),NULL,log);
        Platform_LogWarn("[LEVEL NEAR GPU QA] failed link: %s\n",log);
        s_cfrNearQA.failed=1;
        return 0;
    }
    glGenVertexArrays(1,&s_cfrNearQA.vao);
    glGenBuffers(1,&s_cfrNearQA.vbo);
    glGenBuffers(1,&s_cfrNearQA.tfo);
    if(!s_cfrNearQA.vao || !s_cfrNearQA.vbo || !s_cfrNearQA.tfo){
        s_cfrNearQA.failed=1;
        return 0;
    }
    GLint oldVao=0,oldVbo=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldVbo);
    glBindVertexArray(s_cfrNearQA.vao);
    glBindBuffer(GL_ARRAY_BUFFER,s_cfrNearQA.vbo);
    glBufferData(GL_ARRAY_BUFFER,
        CFR_NEAR_QA_BATCH*16*sizeof(float),NULL,GL_STREAM_DRAW);
    const int arity[6]={3,3,3,3,2,2};
    const int offset[6]={0,3,6,9,12,14};
    for(int a=0;a<6;++a){
        glEnableVertexAttribArray(a);
        glVertexAttribPointer(a,arity[a],GL_FLOAT,GL_FALSE,
            16*sizeof(float),
            (void *)((size_t)offset[a]*sizeof(float)));
    }
    glBindVertexArray((GLuint)oldVao);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldVbo);
    Platform_Log("[LEVEL NEAR GPU QA] GPU midpoint tessellation vertex shader ready, POS+RGB+UV, no raster\n");
    return 1;
}
void NativeRenderer_LevelPCMidpointQAFlush(void)
{
    if(!NativeRenderer_LevelPCProjectionQAEnabled())return;
    int n=s_cfrNearQA.pending;
    s_cfrNearQA.pending=0;
    if(!NativePerf_IsStressTestMeasuring() || n<=0 ||
       s_cfrNearQA.compared>=CFR_NEAR_QA_LIMIT ||
       !NativeRenderer_LevelPCMidpointQAEnsureShader())return;
    GLint oldProgram=0,oldVao=0,oldBuffer=0,oldTF=0,oldIndexed=0;
    GLboolean discard=glIsEnabled(GL_RASTERIZER_DISCARD);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,&oldTF);
    glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,0,&oldIndexed);
    /* Interleave ONLY the input portion of each sample; expected values
     * remain on CPU and aren't uploaded. */
    float inputs[CFR_NEAR_QA_BATCH][16];
    for(int i=0;i<n;++i)
        memcpy(inputs[i],s_cfrNearQA.samples[i].in,sizeof(inputs[i]));
    glBindVertexArray(s_cfrNearQA.vao);
    glBindBuffer(GL_ARRAY_BUFFER,s_cfrNearQA.vbo);
    glBufferData(GL_ARRAY_BUFFER,n*sizeof(inputs[0]),inputs,GL_STREAM_DRAW);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,s_cfrNearQA.tfo);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,n*8*sizeof(float),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,s_cfrNearQA.tfo);
    glUseProgram(s_cfrNearQA.program);
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS,0,n);
    glEndTransformFeedback();
    float output[CFR_NEAR_QA_BATCH][8];
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,n*sizeof(output[0]),output);
    GLenum err=glGetError();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,(GLuint)oldIndexed);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,(GLuint)oldTF);
    glBindVertexArray((GLuint)oldVao);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
    glUseProgram((GLuint)oldProgram);
    if(!discard)glDisable(GL_RASTERIZER_DISCARD);
    if(err!=GL_NO_ERROR){
        Platform_LogWarn("[LEVEL NEAR GPU QA] transform feedback GL=0x%x\n",(unsigned)err);
        s_cfrNearQA.failed=1;return;
    }
    ++s_cfrNearQA.frames;
    for(int i=0;i<n && s_cfrNearQA.compared<CFR_NEAR_QA_LIMIT;++i) {
        float maxerr=0;
        for(int j=0;j<8;++j) {
            const float e=fabsf(output[i][j]-s_cfrNearQA.samples[i].expected[j]);
            if(e>maxerr)maxerr=e;
        }
        s_cfrNearQA.sumError+=maxerr;
        if(maxerr>s_cfrNearQA.worstError)s_cfrNearQA.worstError=maxerr;
        if(maxerr>0.01f){
            ++s_cfrNearQA.mismatch;
            if(s_cfrNearQA.mismatch<10)
                Platform_Log("[LEVEL NEAR GPU QA] mismatch i=%d err=%.2f pos=(%.1f %.1f %.1f) actual=(%.1f %.1f %.1f)\n",
                    i,maxerr,output[i][0],output[i][1],output[i][2],
                    s_cfrNearQA.samples[i].expected[0],s_cfrNearQA.samples[i].expected[1],s_cfrNearQA.samples[i].expected[2]);
        }
        ++s_cfrNearQA.compared;
    }
    if(s_cfrNearQA.compared>=CFR_NEAR_QA_LIMIT &&
       s_cfrNearQA.compared-n<CFR_NEAR_QA_LIMIT)
        Platform_Log("[LEVEL NEAR GPU QA] FINAL count=%d mismatches=%d meanMaxError=%.4f maxError=%.4f frames=%d GPU_midpoints_POS_RGB_UV=1 gamePixelsUnchanged=1\n",
            s_cfrNearQA.compared,s_cfrNearQA.mismatch,
            s_cfrNearQA.sumError/s_cfrNearQA.compared,
            s_cfrNearQA.worstError,s_cfrNearQA.frames);
}
#endif
