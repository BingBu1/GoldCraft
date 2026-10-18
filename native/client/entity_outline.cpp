#include "entity_outline.hpp"
#include <algorithm>
#include <cmath>

namespace goldcraft::render {
namespace {
constexpr std::uint64_t budget=256ull*1024*1024;
constexpr char vertex[]=R"(#version 430
out vec2 uv;
void main(){uv=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(uv*2.0-1.0,0,1);}
)";
// Independently expressed 1.21.1 entity_sobel/outline_box_blur operations.
// RGBA8 intermediates and the original alpha blend are part of the algorithm.
constexpr const char* fragments[]={R"(#version 430
layout(binding=0) uniform sampler2D Source;
in vec2 uv;out vec4 color;
void main(){
    vec2 d=1.0/vec2(textureSize(Source,0));
    vec4 c=texture(Source,uv),a=texture(Source,uv+vec2(d.x,0)),
         b=texture(Source,uv-vec2(d.x,0)),e=texture(Source,uv+vec2(0,d.y)),f=texture(Source,uv-vec2(0,d.y));
    float edge=clamp(abs(a.a-c.a)+abs(b.a-c.a)+abs(e.a-c.a)+abs(f.a-c.a),0.0,1.0);
    color=vec4((c.rgb*c.a+a.rgb*a.a+b.rgb*b.a+e.rgb*e.a+f.rgb*f.a)*0.2,edge);
})",R"(#version 430
layout(binding=0) uniform sampler2D Source;
uniform vec2 Direction;in vec2 uv;out vec4 color;
void main(){
    vec2 d=Direction/vec2(textureSize(Source,0));
    vec4 sum=texture(Source,uv-d*1.5)+texture(Source,uv+d*0.5)+texture(Source,uv+d*2.0)*0.5;
    color=vec4(sum.rgb/2.5,sum.a);
})",R"(#version 430
layout(binding=0) uniform sampler2D Source;
layout(binding=1) uniform sampler2D Scene;
uniform float Gamma;in vec2 uv;out vec4 color;
void main(){
    vec4 outline=texture(Source,uv),scene=texelFetch(Scene,ivec2(gl_FragCoord.xy),0);
    color=vec4(mix(scene.rgb,pow(max(outline.rgb,vec3(0)),vec3(Gamma)),outline.a),scene.a);
})",R"(#version 430
layout(binding=0) uniform sampler2D Source;
in vec2 uv;out vec4 color;
void main(){color=texture(Source,uv);}
)"};
void enable(GLenum cap,GLboolean on) noexcept {if(on)glEnable(cap);else glDisable(cap);}
// Only direct GL state is touched, never Renderer cached bindings. Geometry
// callbacks separately own their shader/VAO/buffer/texture/engine-cache changes.
struct State {
    GLint draw{},read{},program{},vao{},active{},viewport[4]{},polygon[2]{};
    GLint textures[2]{},samplers[2]{},factors[4]{},equations[2]{};
    GLboolean mask[4]{},depth_write{},blend{},clips[8]{};
    static constexpr GLenum caps[]={GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_DEPTH_TEST,GL_STENCIL_TEST,
        GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL};
    GLboolean enabled[std::size(caps)]{};
    State() noexcept {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_POLYGON_MODE,polygon);
        if(!polygon[1])polygon[1]=polygon[0];
        glGetBooleani_v(GL_COLOR_WRITEMASK,0,mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_write);blend=glIsEnabledi(GL_BLEND,0);
        constexpr GLenum parameters[]={GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA};
        for(unsigned i=0;i<4;++i)glGetIntegeri_v(parameters[i],0,factors+i);
        glGetIntegeri_v(GL_BLEND_EQUATION_RGB,0,equations);glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,0,equations+1);
        for(unsigned i=0;i<std::size(caps);++i)enabled[i]=glIsEnabled(caps[i]);
        for(unsigned i=0;i<8;++i)clips[i]=glIsEnabled(GL_CLIP_DISTANCE0+i);
        for(unsigned i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,textures+i);glGetIntegeri_v(GL_SAMPLER_BINDING,i,samplers+i);}
        glActiveTexture(GL_TEXTURE0);
    }
    ~State() noexcept {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glUseProgram(program);glBindVertexArray(vao);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        if(polygon[0]==polygon[1])glPolygonMode(GL_FRONT_AND_BACK,polygon[0]);
        else {glPolygonMode(GL_FRONT,polygon[0]);glPolygonMode(GL_BACK,polygon[1]);}
        glColorMaski(0,mask[0],mask[1],mask[2],mask[3]);glDepthMask(depth_write);
        if(blend)glEnablei(GL_BLEND,0);else glDisablei(GL_BLEND,0);
        glBlendFuncSeparatei(0,factors[0],factors[1],factors[2],factors[3]);glBlendEquationSeparatei(0,equations[0],equations[1]);
        for(unsigned i=0;i<std::size(caps);++i)enable(caps[i],enabled[i]);
        for(unsigned i=0;i<8;++i)enable(GL_CLIP_DISTANCE0+i,clips[i]);
        for(unsigned i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);}
        glActiveTexture(active);
    }
};
void clean_state() noexcept {
    for(auto cap:State::caps)glDisable(cap);
    for(unsigned i=0;i<8;++i)glDisable(GL_CLIP_DISTANCE0+i);
    glDisablei(GL_BLEND,0);glColorMaski(0,1,1,1,1);glDepthMask(GL_FALSE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
}
template<class T> void destroy(T& t) noexcept {
    if(t.fbo)glDeleteFramebuffers(1,&t.fbo);if(t.color)glDeleteTextures(1,&t.color);t={};
}
template<class T> bool create(T& t,unsigned w,unsigned h,unsigned format) noexcept {
    glGenFramebuffers(1,&t.fbo);glGenTextures(1,&t.color);glBindTexture(GL_TEXTURE_2D,t.color);
    glTexStorage2D(GL_TEXTURE_2D,1,format,w,h);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,t.fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.color,0);
    constexpr GLenum out=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&out);
    if(!t.fbo||!t.color||glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    t.width=w;t.height=h;t.format=format;return true;
}
GLuint compile(GLenum type,const char* source) noexcept {
    auto shader=glCreateShader(type);if(!shader)return 0;glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(shader);return 0;}return shader;
}
}
EntityOutline::~EntityOutline(){reset();}
bool EntityOutline::fail(const char* reason) noexcept {error_=reason;++stats_.failures;return false;}
bool EntityOutline::initialize() noexcept {
    if(vao_)return true;
    auto vs=compile(GL_VERTEX_SHADER,vertex);if(!vs)return fail("Outline vertex shader failed");
    std::array<Program,4> next{};
    const auto cleanup=[&](){glDeleteShader(vs);for(auto& p:next)if(p.id)glDeleteProgram(p.id);};
    for(unsigned i=0;i<next.size();++i){
        auto fs=compile(GL_FRAGMENT_SHADER,fragments[i]);if(!fs){cleanup();return fail("Outline fragment shader failed");}
        auto& p=next[i];p.id=glCreateProgram();glAttachShader(p.id,vs);glAttachShader(p.id,fs);glLinkProgram(p.id);glDeleteShader(fs);
        GLint ok=0;glGetProgramiv(p.id,GL_LINK_STATUS,&ok);if(!ok){cleanup();return fail("Outline shader link failed");}
        p.direction=glGetUniformLocation(p.id,"Direction");p.gamma=glGetUniformLocation(p.id,"Gamma");
    }
    GLuint next_vao=0,next_samplers[2]{};glGenVertexArrays(1,&next_vao);glGenSamplers(2,next_samplers);
    if(!next_vao||!next_samplers[0]||!next_samplers[1]){
        if(next_vao)glDeleteVertexArrays(1,&next_vao);glDeleteSamplers(2,next_samplers);cleanup();return fail("Outline objects failed");
    }
    for(unsigned i=0;i<2;++i){
        for(auto filter:{GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER})glSamplerParameteri(next_samplers[i],filter,i?GL_LINEAR:GL_NEAREST);
        for(auto wrap:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T})glSamplerParameteri(next_samplers[i],wrap,GL_CLAMP_TO_EDGE);
    }
    if(glGetError()!=GL_NO_ERROR){glDeleteVertexArrays(1,&next_vao);glDeleteSamplers(2,next_samplers);cleanup();return fail("Outline initialization GL error");}
    glDeleteShader(vs);programs_=next;vao_=next_vao;std::copy(std::begin(next_samplers),std::end(next_samplers),samplers_);
    return true;
}
bool EntityOutline::prepare(const MetaRendererPostSceneFrame& f) noexcept {
    if(targets_[0].width==f.width&&targets_[0].height==f.height&&targets_[2].format==f.color_format)return true;
    std::array<Target,3> next{};
    for(unsigned i=0;i<next.size();++i)if(!create(next[i],f.width,f.height,i==2?f.color_format:GL_RGBA8)){
        for(auto& t:next)destroy(t);return fail("Outline target allocation failed");
    }
    if(glGetError()!=GL_NO_ERROR){for(auto& t:next)destroy(t);return fail("Outline target GL error");}
    for(auto& t:targets_)destroy(t);targets_=next;++stats_.allocations;
    stats_.resident_bytes=std::uint64_t(f.width)*f.height*(f.color_format==GL_RGBA16F?16u:12u);return true;
}
bool EntityOutline::replay(const MetaRendererPostSceneFrame& f,float gamma,MaskDraw draw,void* user) noexcept {
    if(!draw){error_="";return true;}
    auto current=wglGetCurrentContext();
    if(!current||(context_&&context_!=current)||!GLEW_VERSION_4_4)return fail("Outline context rejected");
    constexpr unsigned colors=META_POST_SCENE_LINEAR|META_POST_SCENE_GAMMA;
    const auto space=f.flags&colors;
    if(f.struct_size!=sizeof(f)||!f.framebuffer||!f.color_texture||!f.depth_texture||!f.depth_sample_texture||f.view_index||
       f.color_texture==f.depth_texture||f.color_texture==f.depth_sample_texture||f.depth_texture==f.depth_sample_texture||
       (f.flags&~(colors|META_POST_SCENE_HDR))||(space!=META_POST_SCENE_GAMMA&&space!=META_POST_SCENE_LINEAR)||
       !f.width||!f.height||f.width>16384||f.height>16384||f.samples!=1||f.depth_format!=GL_DEPTH24_STENCIL8||
       (f.color_format!=GL_RGBA8&&f.color_format!=GL_RGBA16F)||!std::isfinite(gamma)||gamma<=0||gamma>8||
       !std::isfinite(f.near_plane)||!std::isfinite(f.far_plane)||f.near_plane<=0||f.far_plane<=f.near_plane||
       f.viewport[0]<0||f.viewport[1]<0||f.viewport[2]<=0||f.viewport[3]<=0||
       std::uint64_t(f.viewport[0])+f.viewport[2]>f.width||std::uint64_t(f.viewport[1])+f.viewport[3]>f.height)
        return fail("Outline frame rejected");
    for(const auto* matrix:{f.world_matrix,f.projection_matrix,f.inverse_world_matrix,f.inverse_projection_matrix})
        for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return fail("Outline camera rejected");
    const auto bytes=std::uint64_t(f.width)*f.height*(f.color_format==GL_RGBA16F?16u:12u);
    const bool resize=targets_[0].width!=f.width||targets_[0].height!=f.height||targets_[2].format!=f.color_format;
    if(bytes+(resize?stats_.resident_bytes:0)>budget)return fail("Outline residency budget exceeded");
    State state;
    for(const auto& t:targets_)if(t.fbo&&(state.draw==GLint(t.fbo)||state.read==GLint(t.fbo)||
        state.textures[0]==GLint(t.color)||state.textures[1]==GLint(t.color)||f.framebuffer==t.fbo||f.color_texture==t.color))
        return fail("Outline private target retained by caller");
    context_=current;clean_state();
    if(!initialize()||!prepare(f))return false;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targets_[0].fbo);constexpr GLfloat clear[4]{};glClearBufferfv(GL_COLOR,0,clear);
    glViewport(f.viewport[0],f.viewport[1],f.viewport[2],f.viewport[3]);glBindSampler(0,0);glBindSampler(1,0);
    if(!draw(user,f))return fail("Outline mask rejected");
    clean_state();glBindVertexArray(vao_);glViewport(0,0,f.width,f.height);
    const auto pass=[&](unsigned p,unsigned target,unsigned source,unsigned filter){
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targets_[target].fbo);glUseProgram(programs_[p].id);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,targets_[source].color);glBindSampler(0,samplers_[filter]);
        glDrawArrays(GL_TRIANGLES,0,3);
    };
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targets_[1].fbo);glClearBufferfv(GL_COLOR,0,clear);
    glEnablei(GL_BLEND,0);glBlendEquationSeparatei(0,GL_FUNC_ADD,GL_FUNC_ADD);
    glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    pass(0,1,0,0);glDisablei(GL_BLEND,0);
    glProgramUniform2f(programs_[1].id,programs_[1].direction,1,0);pass(1,0,1,1);
    glProgramUniform2f(programs_[1].id,programs_[1].direction,0,1);pass(1,1,0,1);
    // Vanilla's final blit is also SRC_ALPHA/ONE_MINUS_SRC_ALPHA onto clear
    // RGBA8. Preserve its second quantization and squared alpha before the
    // separate world composition; folding it into the latter changes edges.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targets_[0].fbo);glClearBufferfv(GL_COLOR,0,clear);
    glEnablei(GL_BLEND,0);pass(3,0,1,0);glDisablei(GL_BLEND,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,f.color_texture);glBindSampler(1,samplers_[0]);
    glProgramUniform1f(programs_[2].id,programs_[2].gamma,space==META_POST_SCENE_LINEAR?gamma:1.f);pass(2,2,0,0);
    if(glGetError()!=GL_NO_ERROR)return fail("Outline GL error before commit");
    glCopyImageSubData(targets_[2].color,GL_TEXTURE_2D,0,f.viewport[0],f.viewport[1],0,
        f.color_texture,GL_TEXTURE_2D,0,f.viewport[0],f.viewport[1],0,f.viewport[2],f.viewport[3],1);
    if(glGetError()!=GL_NO_ERROR)return fail("Outline color commit failed");
    ++stats_.frames;error_="";return true;
}
bool EntityOutline::reset() noexcept {
    if(context_&&context_!=wglGetCurrentContext())return false;
    for(auto& t:targets_)destroy(t);for(auto& p:programs_){if(p.id)glDeleteProgram(p.id);p={};}
    if(vao_)glDeleteVertexArrays(1,&vao_);if(samplers_[0]||samplers_[1])glDeleteSamplers(2,samplers_);vao_=samplers_[0]=samplers_[1]=0;
    context_=nullptr;stats_.resident_bytes=0;error_="";return true;
}
}
