// Driver-backed, hidden-window tests of production replay plus independent
// vendor-vertex/fragment reference draws and numeric SpaceBroken/bloom oracles.
// No game process, live UI, deployment or game-FPS claim is involved.
#include "../../native/client/post_effect_replay.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <vector>
using goldcraft::PostEffectStage;
using goldcraft::render::PostEffectReplay;
namespace probe {
bool count=false;std::size_t allocations=0,storages=0,locations=0,uploads=0;unsigned fail_storage=0,fail_copy=0;
PFNGLTEXSTORAGE2DPROC original_storage;
PFNGLGETUNIFORMLOCATIONPROC original_location;
PFNGLBUFFERDATAPROC original_buffer;
PFNGLCOPYIMAGESUBDATAPROC original_copy;
void APIENTRY storage(GLenum a,GLsizei b,GLenum c,GLsizei d,GLsizei e){++storages;if(fail_storage&&!--fail_storage)return;original_storage(a,b,c,d,e);}
GLint APIENTRY location(GLuint a,const GLchar* b){++locations;return original_location(a,b);}
void APIENTRY buffer(GLenum a,GLsizeiptr b,const void* c,GLenum d){++uploads;original_buffer(a,b,c,d);}
void APIENTRY copy(GLuint a,GLenum b,GLint c,GLint d,GLint e,GLint f,GLuint g,GLenum h,GLint i,GLint j,GLint k,GLint l,GLsizei m,GLsizei n,GLsizei o){if(fail_copy&&!--fail_copy)m=-1;original_copy(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o);}
}
void* operator new(std::size_t n){if(probe::count)++probe::allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
using Color=std::array<float,4>;
std::string read(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
GLuint shader(GLenum type,const std::string& text){const auto s=glCreateShader(type);const char* p=text.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);std::fprintf(stderr,"%s\n",log);}assert(ok);return s;}
GLuint program(const std::string& vertex,const std::string& fragment){auto vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glBindAttribLocation(p,0,"Position");glBindFragDataLocation(p,0,"fragColor");glLinkProgram(p);glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);return p;}
struct Target {
    GLuint fbo{},color{},depth{},depth_view{},second{};unsigned width,height,format;
    Target(unsigned w,unsigned h,unsigned fmt=GL_RGBA8,bool host=false):width(w),height(h),format(fmt){
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexStorage2D(GL_TEXTURE_2D,1,fmt,w,h);
        for(auto p:{GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER})glTexParameteri(GL_TEXTURE_2D,p,GL_NEAREST);
        for(auto p:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T})glTexParameteri(GL_TEXTURE_2D,p,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        if(host){
            glGenTextures(1,&second);glBindTexture(GL_TEXTURE_2D,second);glTexStorage2D(GL_TEXTURE_2D,1,fmt,w,h);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,second,0);
            glGenTextures(1,&depth);glBindTexture(GL_TEXTURE_2D,depth);glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,w,h);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth,0);
            glGenTextures(1,&depth_view);glTextureView(depth_view,GL_TEXTURE_2D,depth,GL_DEPTH24_STENCIL8,0,1,0,1);
        }
        const GLenum targets[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(host?2:1,targets);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glBindTexture(GL_TEXTURE_2D,0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    ~Target(){glDeleteFramebuffers(1,&fbo);const GLuint ids[]={color,depth,depth_view,second};glDeleteTextures(4,ids);}
    Target(const Target&)=delete;
    std::vector<Color> pixels(unsigned attachment=0) const {glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0+attachment);std::vector<Color> p(width*height);glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,p.data());return p;}
    std::vector<unsigned> depth_stencil() const {glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);std::vector<unsigned> p(width*height);glReadPixels(0,0,width,height,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,p.data());return p;}
    MetaRendererPostSceneFrame frame() const {MetaRendererPostSceneFrame f{};f.struct_size=sizeof(f);f.flags=format==GL_RGBA16F?META_POST_SCENE_LINEAR|META_POST_SCENE_HDR:META_POST_SCENE_GAMMA;f.framebuffer=fbo;f.color_texture=color;f.depth_texture=depth;f.depth_sample_texture=depth_view;f.width=width;f.height=height;f.color_format=format;f.depth_format=GL_DEPTH24_STENCIL8;f.viewport[2]=width;f.viewport[3]=height;f.samples=1;f.near_plane=4;f.far_plane=4096;for(unsigned i=0;i<16;i+=5)f.world_matrix[i]=f.projection_matrix[i]=f.inverse_world_matrix[i]=f.inverse_projection_matrix[i]=1;return f;}
    void pattern(){
        std::vector<Color> data(width*height);for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)data[y*width+x]={float(x)/width,float(y)/height,float((x^y)&15)/15,1};
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,data.data());glBindTexture(GL_TEXTURE_2D,0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glColorMaski(1,1,1,1,1);Color extra{.2f,.6f,.8f,.4f};glClearBufferfv(GL_COLOR,1,extra.data());glDepthMask(GL_TRUE);glStencilMask(255);glClearBufferfi(GL_DEPTH_STENCIL,0,.75f,83);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,width/2,height);glClearBufferfi(GL_DEPTH_STENCIL,0,.25f,37);glDisable(GL_SCISSOR_TEST);
    }
};
struct Mask {
    GLuint shader{},vao{};GLint color_uniform=-1,world_uniform=-1,projection_uniform=-1;unsigned calls=0,fail_on=0;bool depth_write=false,inject_error=false;
    std::array<Color,4> colors{{{51.f/255,76.f/255,102.f/255,1},{.8f,.7f,.1f,.8f},{.4f,.2f,.6f,.6f},{0,0,0,0}}};
    Mask(){shader=program(R"(#version 150
uniform mat4 World;uniform mat4 Projection;
void main(){vec2 xy=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0;gl_Position=Projection*World*vec4(xy,0,1);})",R"(#version 150
uniform vec4 Color;out vec4 fragColor;void main(){fragColor=Color;})");glGenVertexArrays(1,&vao);color_uniform=glGetUniformLocation(shader,"Color");world_uniform=glGetUniformLocation(shader,"World");projection_uniform=glGetUniformLocation(shader,"Projection");}
    ~Mask(){glDeleteProgram(shader);glDeleteVertexArrays(1,&vao);}
    static bool draw(void* user,const MetaRendererPostSceneFrame& f,std::uint8_t first,std::uint8_t count) noexcept {
        auto& m=*static_cast<Mask*>(user);assert(count==1&&first<m.colors.size());++m.calls;
        GLint previous_program=0,previous_vao=0;GLboolean write=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous_program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous_vao);glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
        glUseProgram(m.shader);glBindVertexArray(m.vao);glUniform4fv(m.color_uniform,1,m.colors[first].data());
        glUniformMatrix4fv(m.world_uniform,1,GL_FALSE,f.world_matrix);glUniformMatrix4fv(m.projection_uniform,1,GL_FALSE,f.projection_matrix);glDepthMask(m.depth_write);
        glDrawArrays(GL_TRIANGLES,0,3);glDepthMask(write);glUseProgram(previous_program);glBindVertexArray(previous_vao);
        if(m.inject_error)glEnable(0xffffffffu);
        return !m.fail_on||m.calls!=m.fail_on;
    }
};
PostEffectStage stage(unsigned kind,unsigned batch=0){PostEffectStage s{};s.kind=static_cast<std::uint8_t>(kind);s.first_batch=static_cast<std::uint8_t>(batch);s.batch_count=(kind==5||kind==6)?0:1;s.params={1.75f,1,.3f,.1f,12,.4f,1.35f,.28f};if(kind==1)s.params[3]=1.65f,s.params[4]=0;if(kind==2)s.params[3]=1.05f;if(kind==6)s.params={1.15f,1.075f,1,.31f,.63f,.7f,.7f,1.1f};return s;}
std::vector<GLint> state(){
    std::vector<GLint> result;for(GLenum p:{GL_READ_FRAMEBUFFER_BINDING,GL_READ_BUFFER,GL_DRAW_FRAMEBUFFER_BINDING,GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_DEPTH_FUNC,GL_STENCIL_FUNC,GL_STENCIL_REF,GL_STENCIL_VALUE_MASK,GL_STENCIL_WRITEMASK,GL_STENCIL_FAIL,GL_STENCIL_PASS_DEPTH_FAIL,GL_STENCIL_PASS_DEPTH_PASS,GL_STENCIL_BACK_FUNC,GL_STENCIL_BACK_REF,GL_STENCIL_BACK_VALUE_MASK,GL_STENCIL_BACK_WRITEMASK,GL_STENCIL_BACK_FAIL,GL_STENCIL_BACK_PASS_DEPTH_FAIL,GL_STENCIL_BACK_PASS_DEPTH_PASS}){GLint v=0;glGetIntegerv(p,&v);result.push_back(v);}
    GLint pair[4]{};for(GLenum p:{GL_VIEWPORT,GL_SCISSOR_BOX}){glGetIntegerv(p,pair);result.insert(result.end(),pair,pair+4);}
    pair[1]=0;glGetIntegerv(GL_POLYGON_MODE,pair);if(!pair[1])pair[1]=pair[0];result.insert(result.end(),pair,pair+2);
    for(GLenum p:{GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL,GL_DEPTH_WRITEMASK}){GLboolean b=0;glGetBooleanv(p,&b);result.push_back(b);}
    GLdouble depth_range[2]{};glGetDoublev(GL_DEPTH_RANGE,depth_range);for(auto d:depth_range)result.push_back(static_cast<GLint>(d*1000000));
    for(unsigned i=0;i<8;++i)result.push_back(glIsEnabled(GL_CLIP_DISTANCE0+i));
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);GLint v=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&v);result.push_back(v);glGetIntegeri_v(GL_SAMPLER_BINDING,i,&v);result.push_back(v);glGetIntegerv(GL_DRAW_BUFFER0+i,&v);result.push_back(v);result.push_back(glIsEnabledi(GL_BLEND,i));GLboolean mask[4];glGetBooleani_v(GL_COLOR_WRITEMASK,i,mask);for(auto c:mask)result.push_back(c);for(GLenum p:{GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA}){glGetIntegeri_v(p,i,&v);result.push_back(v);}}
    glActiveTexture(active);return result;
}
struct Sentinel {
    Target target{16,16,GL_RGBA8,true};GLuint samplers[4]{},textures[4]{},vao{},buffer{};bool core;
    explicit Sentinel(bool core_profile):core(core_profile){glGenSamplers(4,samplers);glGenTextures(4,textures);glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);}
    ~Sentinel(){glDeleteSamplers(4,samplers);glDeleteTextures(4,textures);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&buffer);}
    void set(){glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glReadBuffer(GL_COLOR_ATTACHMENT1);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);glUseProgram(0);glViewport(3,5,7,9);glScissor(2,4,6,8);
        for(GLenum cap:{GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_STENCIL_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP})glEnable(cap);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_GREATER);glDepthRange(.2,.6);glEnable(GL_POLYGON_OFFSET_FILL);
        if(core)glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);else {glPolygonMode(GL_FRONT,GL_LINE);glPolygonMode(GL_BACK,GL_POINT);}
        glStencilFuncSeparate(GL_FRONT,GL_NOTEQUAL,6,0x3f);glStencilFuncSeparate(GL_BACK,GL_EQUAL,13,0x55);glStencilMaskSeparate(GL_FRONT,0x1f);glStencilMaskSeparate(GL_BACK,0xf0);glStencilOpSeparate(GL_FRONT,GL_INVERT,GL_REPLACE,GL_INCR);glStencilOpSeparate(GL_BACK,GL_ZERO,GL_DECR,GL_KEEP);
        for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);glEnablei(GL_BLEND,i);glBlendFuncSeparatei(i,GL_ONE,GL_DST_COLOR,GL_DST_ALPHA,GL_ZERO);glBlendEquationSeparatei(i,GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);glColorMaski(i,i%2,GL_FALSE,GL_TRUE,GL_FALSE);}glEnable(GL_CLIP_DISTANCE2);glActiveTexture(GL_TEXTURE3);
    }
};
void ordinary(){for(GLenum cap:{GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_STENCIL_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_DEPTH_TEST,GL_BLEND})glDisable(cap);for(unsigned i=0;i<8;++i)glDisable(GL_CLIP_DISTANCE0+i);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColorMask(1,1,1,1);glDepthMask(0);for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,0);glBindSampler(i,0);}glActiveTexture(GL_TEXTURE0);glBindFramebuffer(GL_FRAMEBUFFER,0);}
void compare_pixels(const std::vector<Color>& a,const std::vector<Color>& b,float epsilon=1.1f/255){assert(a.size()==b.size());for(unsigned i=0;i<a.size();++i)for(unsigned c=0;c<4;++c)if(std::abs(a[i][c]-b[i][c])>epsilon){std::fprintf(stderr,"pixel %u/%u got %.8f expected %.8f\n",i,c,a[i][c],b[i][c]);assert(false);}}
bool checked_replay(PostEffectReplay& replay,const MetaRendererPostSceneFrame& frame,
                    std::span<const PostEffectStage> stages,PostEffectReplay::MaskDraw draw,void* user){
    const bool result=replay.replay(frame,stages,draw,user);
    // ReplayState restores after replay's internal error checks. Check here so
    // a successful graph cannot leak a destructor error into the next draw.
    const auto error=glGetError();
    if(error!=GL_NO_ERROR){std::fprintf(stderr,"post replay returned %s, failures=%llu, GL error after return=%u\n",result?"true":"false",static_cast<unsigned long long>(replay.statistics().failures),error);std::exit(3);}
    return result;
}
// Independent fullscreen fixture uses the original hashed vendor vertex and
// fragment asset with a real POSITION quad and ortho matrix (not gl_VertexID).
struct Reference {
    std::array<GLuint,6> programs{};GLuint vao{},vbo{};
    explicit Reference(const std::string& folder){const char* names[]={"unity_composite","space_broken","chromatic_aberration","air_disturbance","screen_distortion","color_dispersion"};const auto vertex=read(folder+"/blit.vsh");for(unsigned i=0;i<6;++i)programs[i]=program(vertex,read(folder+"/"+names[i]+".fsh"));glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);}
    ~Reference(){for(auto p:programs)glDeleteProgram(p);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);}
    void draw(Target& out,const Target& source,const Target& mask,const PostEffectStage& s){ordinary();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,out.fbo);glViewport(0,0,out.width,out.height);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);float v[]={0,0,float(out.width),0,float(out.width),float(out.height),0,float(out.height)};glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STREAM_DRAW);GLuint p=programs[s.kind-1];glUseProgram(p);auto u=[&](const char* name){return glGetUniformLocation(p,name);};const float ortho[]={2.f/out.width,0,0,0,0,2.f/out.height,0,0,0,0,-2.f/999.9f,0,-1,-1,-1000.1f/999.9f,1};glUniformMatrix4fv(u("ProjMat"),1,GL_FALSE,ortho);glUniform2f(u("OutSize"),float(out.width),float(out.height));glUniform1i(u("DiffuseSampler"),0);glUniform1i(u("Mask"),1);glUniform1f(u("Time"),s.params[0]);glUniform2f(u("Direction"),s.params[1],s.params[2]);glUniform1f(u("DistortionStrength"),s.params[3]);glUniform1f(u("InterScale"),s.params[3]);glUniform1f(u("Frequency"),s.params[4]);glUniform1f(u("Progress"),s.params[5]);glUniform3f(u("Level"),s.params[0],s.params[1],s.params[2]);glUniform2f(u("Center"),s.params[3],s.params[4]);glUniform3f(u("ColorModulate"),s.params[5],s.params[6],s.params[7]);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.color);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,mask.color);glDrawArrays(GL_TRIANGLE_FAN,0,4);}
};
#include "bw_contrast_gl_tests.inc"
#include "hsv_filter_gl_tests.inc"
}
int run_profile(int profile,const char* directory){
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,4);glfwWindowHint(GLFW_OPENGL_PROFILE,profile);
    auto* window=glfwCreateWindow(64,64,"GoldCraft post replay hidden verification",nullptr,nullptr);if(!window){glfwTerminate();return 2;}glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)return 2;while(glGetError()!=GL_NO_ERROR){}
    GLint profile_mask=0;glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile_mask);const bool core=(profile_mask&GL_CONTEXT_CORE_PROFILE_BIT)!=0;assert(core==(profile==GLFW_OPENGL_CORE_PROFILE));
    std::printf("Driver: %s / %s / profile: %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),core?"core":"compatibility");std::fflush(stdout);
    probe::original_storage=__glewTexStorage2D;probe::original_location=__glewGetUniformLocation;probe::original_buffer=__glewBufferData;probe::original_copy=__glewCopyImageSubData;__glewTexStorage2D=probe::storage;__glewGetUniformLocation=probe::location;__glewBufferData=probe::buffer;__glewCopyImageSubData=probe::copy;
    {
        PostEffectReplay replay;assert(replay.set_shader_directory(directory));Target host(64,64,GL_RGBA8,true);Mask mask;Sentinel sentinel(core);Reference reference(directory);host.pattern();const auto original=host.pixels();const auto depth=host.depth_stencil();const auto extra=host.pixels(1);const auto frame=host.frame();
        auto effect=stage(2);sentinel.set();const auto before=state();assert(glGetError()==GL_NO_ERROR);assert(checked_replay(replay,frame,{&effect,1},Mask::draw,&mask));assert(state()==before);assert(host.depth_stencil()==depth&&host.pixels(1)==extra);
        // Numeric oracle: left-half native depth .25 occludes a .5 mask; right
        // half .75 admits it. Sample coordinates are exact nearest texels.
        auto expected=original;const auto color=mask.colors[0];const float scale=1.05f*1.054f;
        for(unsigned y=0;y<64;++y)for(unsigned x=32;x<64;++x){const float u=(x+.5f)/64/scale+(scale-1)*color[1],v=(y+.5f)/64/scale+(scale-1)*color[2];auto value=original[std::clamp(int(v*64),0,63)*64+std::clamp(int(u*64),0,63)];for(unsigned c=0;c<3;++c)value[c]=std::min(1.f,value[c]*(1.05f+color[0]*.1f));value[3]=1;expected[y*64+x]=value;}
        compare_pixels(host.pixels(),expected);ordinary();
        for(GLenum mode:{GL_FILL,GL_LINE,GL_POINT}){host.pattern();ordinary();glPolygonMode(GL_FRONT_AND_BACK,mode);const auto saved=state();assert(checked_replay(replay,frame,{&effect,1},Mask::draw,&mask));assert(state()==saved);compare_pixels(host.pixels(),expected);assert(host.depth_stencil()==depth);ordinary();}
        // The mask callback receives and applies actual host camera matrices
        // and a nonzero viewport. CPU clip-space triangle inclusion is separate
        // from the mask shader and post rasterizer.
        host.pattern();ordinary();auto camera=frame;camera.viewport[0]=8;camera.viewport[1]=12;camera.viewport[2]=48;camera.viewport[3]=36;camera.world_matrix[12]=-.5f;camera.projection_matrix[0]=.5f;const auto camera_before=host.pixels();
        assert(checked_replay(replay,camera,{&effect,1},Mask::draw,&mask));auto camera_expected=camera_before;
        for(unsigned y=12;y<48;++y)for(unsigned x=32;x<56;++x){float ox=2*((float(x)+.5f-8)/48*2-1)+.5f,oy=(float(y)+.5f-12)/36*2-1;if(ox< -1||oy< -1||ox+oy>2)continue;float u=(x+.5f)/64/scale+(scale-1)*color[1],v=(y+.5f)/64/scale+(scale-1)*color[2];auto value=original[std::clamp(int(v*64),0,63)*64+std::clamp(int(u*64),0,63)];for(unsigned c=0;c<3;++c)value[c]=std::min(1.f,value[c]*(1.05f+color[0]*.1f));value[3]=1;camera_expected[y*64+x]=value;}
        compare_pixels(host.pixels(),camera_expected);assert(host.depth_stencil()==depth);ordinary();
        // All complex effects match an independent original-vertex draw using
        // the actual pinned fragment shader, parameters, and depth-tested mask.
        Target input(64,64),mask_target(64,64),output(64,64);std::vector<Color> mask_pixels(64*64);for(unsigned y=0;y<64;++y)for(unsigned x=32;x<64;++x)mask_pixels[y*64+x]=mask.colors[0];
        glBindTexture(GL_TEXTURE_2D,mask_target.color);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,64,64,GL_RGBA,GL_FLOAT,mask_pixels.data());
        for(unsigned kind:{2u,3u,4u,5u,6u}){host.pattern();ordinary();glCopyImageSubData(host.color,GL_TEXTURE_2D,0,0,0,0,input.color,GL_TEXTURE_2D,0,0,0,0,64,64,1);auto s=stage(kind);reference.draw(output,input,mask_target,s);const auto reference_pixels=output.pixels();ordinary();sentinel.set();const auto saved=state();assert(checked_replay(replay,frame,{&s,1},Mask::draw,&mask));assert(state()==saved);compare_pixels(host.pixels(),reference_pixels,2.1f/255);assert(host.depth_stencil()==depth);ordinary();if(kind==5||kind==6){host.pattern();ordinary();assert(checked_replay(replay,camera,{&s,1},nullptr,nullptr));auto inset_expected=original;for(unsigned y=12;y<48;++y)for(unsigned x=8;x<56;++x)inset_expected[y*64+x]=reference_pixels[y*64+x];compare_pixels(host.pixels(),inset_expected,2.1f/255);assert(host.depth_stencil()==depth);ordinary();}}
        // Maskless color dispersion uses channel-specific center scaling, then
        // either RGB modulation or its actual grayscale branch. No mask draw
        // is permitted, and host depth/stencil remain untouched.
        for(bool grayscale:{false,true}) {
            host.pattern();ordinary();auto dispersion=stage(6);
            if(grayscale)dispersion.params[5]=dispersion.params[6]=dispersion.params[7]=0;
            auto dispersion_expected=original;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                Color sampled{};
                for(unsigned c=0;c<3;++c) {
                    const float u=((x+.5f)/64-dispersion.params[3])/dispersion.params[c]+dispersion.params[3];
                    const float v=((y+.5f)/64-dispersion.params[4])/dispersion.params[c]+dispersion.params[4];
                    sampled[c]=original[std::clamp(int(v*64),0,63)*64+std::clamp(int(u*64),0,63)][c];
                }
                const float gray=(sampled[0]+sampled[1]+sampled[2])/3;
                for(unsigned c=0;c<3;++c)dispersion_expected[y*64+x][c]=grayscale?gray:std::clamp(sampled[c]*dispersion.params[c+5],0.f,1.f);
                dispersion_expected[y*64+x][3]=1;
            }
            const auto calls=mask.calls;sentinel.set();const auto saved=state();
            assert(checked_replay(replay,frame,{&dispersion,1},nullptr,nullptr));assert(state()==saved&&mask.calls==calls);
            compare_pixels(host.pixels(),dispersion_expected);assert(host.depth_stencil()==depth);ordinary();
        }
        impact_pixels(replay,directory,sentinel);hsv_pixels(replay,directory,sentinel);
        // Screen stages retain the previous bloom/space result in one atomic graph.
        std::array<PostEffectStage,3> mixed_stages{stage(1),stage(2,1),stage(6)};
        host.pattern();ordinary();assert(checked_replay(replay,frame,mixed_stages,Mask::draw,&mask));const auto mixed_pixels=host.pixels();
        host.pattern();ordinary();for(const auto& item:mixed_stages)assert(checked_replay(replay,frame,{&item,1},Mask::draw,&mask));
        compare_pixels(host.pixels(),mixed_pixels);assert(host.depth_stencil()==depth);ordinary();
        // A second pipeline sees the first's composed scene; its mask starts
        // from copied world depth, even when earlier geometry wrote depth.
        std::array<PostEffectStage,2> ordered{stage(2),stage(3,1)};host.pattern();mask.depth_write=true;ordinary();assert(checked_replay(replay,frame,ordered,Mask::draw,&mask));auto ordered_pixels=host.pixels();host.pattern();ordinary();auto first=ordered[0],second=ordered[1];assert(checked_replay(replay,frame,{&first,1},Mask::draw,&mask));assert(checked_replay(replay,frame,{&second,1},Mask::draw,&mask));compare_pixels(host.pixels(),ordered_pixels);assert(host.depth_stencil()==depth);mask.depth_write=false;
        // Bloom uniform plane has an independent arithmetic oracle for all
        // five downs/four ups/unity steps, including RGBA8 pyramid rounding.
        host.pattern();ordinary();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,host.fbo);Color background{.1f,.2f,.3f,1};glClearBufferfv(GL_COLOR,0,background.data());glDepthMask(1);glStencilMask(255);glClearBufferfi(GL_DEPTH_STENCIL,0,1,17);mask.colors[0]={.05f,.08f,.11f,.2f};auto bloom=stage(1);ordinary();assert(checked_replay(replay,frame,{&bloom,1},Mask::draw,&mask));auto bloom_pixels=host.pixels();Color bloom_expected{};auto q=[](float f){return std::round(std::clamp(f,0.f,1.f)*255)/255;};Color h{},b{},up{};for(unsigned c=0;c<4;++c){h[c]=q(mask.colors[0][c]);b[c]=q(background[c]);up[c]=h[c];for(unsigned n=0;n<4;++n)up[c]=q(.8f*up[c]+.8f*h[c]);}Color mixed{};for(unsigned c=0;c<3;++c)mixed[c]=b[c]*(1-h[3])+h[c]*h[3];float luminance=.299f*mixed[0]+.587f*mixed[1]+.114f*mixed[2];for(unsigned c=0;c<3;++c)bloom_expected[c]=q(mixed[c]+1.65f*.8f*(up[c]+h[c])*((1-luminance)*(1.35f-.28f)+.28f));bloom_expected[3]=1;compare_pixels(bloom_pixels,std::vector<Color>(64*64,bloom_expected),3.1f/255);mask.colors[0]=color;
        // Failure never publishes half a graph, and an explicit empty frame
        // never replays stale effects or touches GL state/resources.
        host.pattern();ordinary();mask.calls=0;mask.fail_on=2;sentinel.set();auto saved=state();assert(!checked_replay(replay,frame,ordered,Mask::draw,&mask));assert(state()==saved);compare_pixels(host.pixels(),original,0);assert(host.depth_stencil()==depth);mask.fail_on=0;sentinel.set();saved=state();const auto storage_before=probe::storages;assert(checked_replay(replay,frame,{},nullptr,nullptr));assert(state()==saved&&probe::storages==storage_before);compare_pixels(host.pixels(),original,0);
        ordinary();mask.inject_error=true;assert(!checked_replay(replay,frame,ordered,Mask::draw,&mask));mask.inject_error=false;assert(glGetError()==GL_NO_ERROR);compare_pixels(host.pixels(),original,0);assert(host.depth_stencil()==depth);
        // One invalid final-copy command exercises commit failure without
        // changing source pixels; GL errors must be diagnosed, not presented.
        ordinary();auto only_screen=stage(5);probe::fail_copy=1;assert(!checked_replay(replay,frame,{&only_screen,1},nullptr,nullptr));assert(probe::fail_copy==0&&glGetError()==GL_NO_ERROR);compare_pixels(host.pixels(),original,0);
        auto invalid=frame;invalid.samples=4;ordinary();assert(!checked_replay(replay,invalid,{&effect,1},Mask::draw,&mask));invalid=frame;invalid.width=invalid.height=16384;assert(!checked_replay(replay,invalid,{&effect,1},Mask::draw,&mask));compare_pixels(host.pixels(),original,0);
        // Resize failures use a real incomplete target (storage deliberately
        // omitted), then recover without corrupting the host or prior state.
        for(unsigned failure:{1u,3u,8u}){ordinary();Target resized(96,64,GL_RGBA16F,true);resized.pattern();const auto pixels=resized.pixels();const auto ds=resized.depth_stencil();sentinel.set();saved=state();probe::fail_storage=failure;assert(!checked_replay(replay,resized.frame(),{&effect,1},Mask::draw,&mask));assert(probe::fail_storage==0&&state()==saved);compare_pixels(resized.pixels(),pixels,0);assert(resized.depth_stencil()==ds);ordinary();assert(checked_replay(replay,frame,{&effect,1},Mask::draw,&mask));}
        ordinary();{Target resized(96,64,GL_RGBA16F,true);resized.pattern();const auto ds=resized.depth_stencil();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resized.fbo);Color hdr{2,1.5f,.5f,1};glClearBufferfv(GL_COLOR,0,hdr.data());ordinary();auto zero_mask=stage(2,3);assert(checked_replay(replay,resized.frame(),{&zero_mask,1},Mask::draw,&mask));compare_pixels(resized.pixels(),std::vector<Color>(96*64,hdr),0);assert(resized.depth_stencil()==ds);}
        ordinary();auto* other=glfwCreateWindow(16,16,"Post replay independent hidden context",nullptr,nullptr);assert(other);glfwMakeContextCurrent(other);assert(!checked_replay(replay,frame,{&effect,1},Mask::draw,&mask)&&!replay.reset());glfwMakeContextCurrent(window);glfwDestroyWindow(other);
        host.pattern();ordinary();auto screen=stage(5);std::array<PostEffectStage,8> all{stage(1),stage(2,1),stage(3,2),stage(4,3),screen,stage(6),impact_stage(),hsv_stage()};assert(checked_replay(replay,frame,all,Mask::draw,&mask));const auto allocations=replay.statistics().allocations;const auto storages=probe::storages,locations=probe::locations,uploads=probe::uploads;probe::allocations=0;probe::count=true;for(unsigned i=0;i<128;++i)assert(checked_replay(replay,frame,all,Mask::draw,&mask));probe::count=false;assert(probe::allocations==0&&probe::storages==storages&&probe::locations==locations&&probe::uploads==uploads&&replay.statistics().allocations==allocations);assert(glGetError()==GL_NO_ERROR);
        assert(replay.reset());ordinary();PostEffectReplay missing;assert(missing.set_shader_directory(std::string(directory)+"/missing"));const auto pixels=host.pixels();assert(!checked_replay(missing,frame,{&screen,1},nullptr,nullptr));compare_pixels(host.pixels(),pixels,0);probe::allocations=0;probe::count=true;for(unsigned n=0;n<128;++n)assert(!checked_replay(missing,frame,{&screen,1},nullptr,nullptr));probe::count=false;assert(probe::allocations==0&&missing.reset());
        const auto corrupted=std::filesystem::path(directory).parent_path()/"post-tampered-shader-fixture";std::filesystem::create_directories(corrupted);{std::ofstream file(corrupted/"down_sampling.fsh",std::ios::binary);file<<"#version 150\nvoid main(){}\n";}
        assert(missing.set_shader_directory(corrupted.string()));assert(!checked_replay(missing,frame,{&screen,1},nullptr,nullptr));assert(std::strstr(missing.last_error(),"SHA-256"));compare_pixels(host.pixels(),pixels,0);assert(missing.reset()&&missing.set_shader_directory(directory));assert(checked_replay(missing,frame,{&screen,1},nullptr,nullptr));assert(missing.reset());assert(glGetError()==GL_NO_ERROR);
        std::puts("{\"postReplay\":true,\"realDriver\":true,\"allEightKinds\":true,\"pinnedOriginalShaderReference\":true,\"spaceNumericOracle\":true,\"bloomFiveDownFourUpOracle\":true,\"nativeDepthOcclusion\":true,\"actualMatricesAndViewport\":true,\"orderedSceneChain\":true,\"hostDepthStencilUnchanged\":true,\"indexedStateRestored\":true,\"failureAtomic\":true,\"glFailureRejected\":true,\"emptyNoop\":true,\"resizeRecovery\":true,\"floatRangePreserved\":true,\"wrongContextRejected\":true,\"hashMismatchRejected\":true,\"missingResourceSticky\":true,\"resourceResetRecovery\":true,\"warmEightStageFrames\":128,\"warmAllocations\":0,\"warmStorage\":0,\"warmUniformLookups\":0,\"warmBufferUploads\":0,\"liveAcceptance\":false}");
        ordinary();
    }
    __glewTexStorage2D=probe::original_storage;__glewGetUniformLocation=probe::original_location;__glewBufferData=probe::original_buffer;__glewCopyImageSubData=probe::original_copy;
    glfwDestroyWindow(window);return 0;
}
int main(int argc,char** argv){
    assert(argc==2);if(!glfwInit())return 2;
    for(int profile:{GLFW_OPENGL_CORE_PROFILE,GLFW_OPENGL_COMPAT_PROFILE}){const int result=run_profile(profile,argv[1]);if(result){glfwTerminate();return result;}}
    glfwTerminate();std::puts("{\"profiles\":[\"core\",\"compatibility\"],\"polygonModesRestored\":true,\"postReturnGlErrors\":0}");return 0;
}
