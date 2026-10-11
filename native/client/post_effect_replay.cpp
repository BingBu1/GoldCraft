#include "post_effect_replay.hpp"
#include "post_effect_assets.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace goldcraft::render {
namespace {
constexpr std::uint64_t budget=512ull*1024*1024;
constexpr GLenum texture_units[]={GL_TEXTURE0,GL_TEXTURE1,GL_TEXTURE2};
// Same pixel mapping as the pinned ortho vertex shader: bottom-left UV, clip
// Z=0.2. A vertex-ID triangle eliminates geometry uploads and per-frame buffers.
constexpr char vertex_source[]=R"(#version 150
out vec2 texCoord;
void main(){
    vec2 corner=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    texCoord=corner;
    gl_Position=vec4(corner*2.0-1.0,0.2,1.0);
}
)";
void enable(GLenum cap,GLboolean value) noexcept {if(value)glEnable(cap);else glDisable(cap);}
// Direct GL only: Renderer caches are never changed here. The mask callback
// separately preserves any additional cache/state used by its mesh backend.
struct ReplayState {
    GLint read=0,draw=0,program=0,vao=0,active=0,viewport[4]{},polygon[2]{},depth_func=0;
    GLint textures[3]{},samplers[3]{};
    GLint blend_factors[4]{},blend_equations[2]{};
    GLdouble depth_range[2]{};
    GLboolean mask[4]{},depth_mask=0,blend=0;
    bool save_blend_parameters=false;
    static constexpr GLenum capabilities[]={GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_DEPTH_TEST,
        GL_STENCIL_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL};
    GLboolean enabled[std::size(capabilities)]{},clips[8]{};
    explicit ReplayState(bool changes_blend_parameters) noexcept :save_blend_parameters(changes_blend_parameters){
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_POLYGON_MODE,polygon);
        // Core exposes one mode and only accepts FRONT_AND_BACK on restore;
        // compatibility can return distinct front/back modes. Zero is never a
        // valid mode, so normalize an untouched second slot without a query.
        if(!polygon[1])polygon[1]=polygon[0];
        glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
        glGetDoublev(GL_DEPTH_RANGE,depth_range);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);glGetBooleani_v(GL_COLOR_WRITEMASK,0,mask);
        blend=glIsEnabledi(GL_BLEND,0);
        if(save_blend_parameters){
            const GLenum blend_values[]={GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA};
            for(unsigned i=0;i<4;++i)glGetIntegeri_v(blend_values[i],0,&blend_factors[i]);
            glGetIntegeri_v(GL_BLEND_EQUATION_RGB,0,&blend_equations[0]);glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,0,&blend_equations[1]);
        }
        for(unsigned i=0;i<std::size(capabilities);++i)enabled[i]=glIsEnabled(capabilities[i]);
        for(unsigned i=0;i<8;++i)clips[i]=glIsEnabled(GL_CLIP_DISTANCE0+i);
        for(unsigned i=0;i<3;++i){glActiveTexture(texture_units[i]);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegeri_v(GL_SAMPLER_BINDING,i,&samplers[i]);}
        glActiveTexture(GL_TEXTURE0);
    }
    ~ReplayState() noexcept {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        glUseProgram(program);glBindVertexArray(vao);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        if(polygon[0]==polygon[1])glPolygonMode(GL_FRONT_AND_BACK,polygon[0]);
        else {glPolygonMode(GL_FRONT,polygon[0]);glPolygonMode(GL_BACK,polygon[1]);}
        glDepthFunc(depth_func);glDepthMask(depth_mask);glColorMaski(0,mask[0],mask[1],mask[2],mask[3]);
        glDepthRange(depth_range[0],depth_range[1]);
        if(blend)glEnablei(GL_BLEND,0);else glDisablei(GL_BLEND,0);
        if(save_blend_parameters){
            glBlendFuncSeparatei(0,blend_factors[0],blend_factors[1],blend_factors[2],blend_factors[3]);
            glBlendEquationSeparatei(0,blend_equations[0],blend_equations[1]);
        }
        for(unsigned i=0;i<std::size(capabilities);++i)enable(capabilities[i],enabled[i]);
        for(unsigned i=0;i<8;++i)enable(GL_CLIP_DISTANCE0+i,clips[i]);
        for(unsigned i=0;i<3;++i){glActiveTexture(texture_units[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);}
        glActiveTexture(active);
    }
};
void fullscreen_state() noexcept {
    for(auto cap:ReplayState::capabilities)glDisable(cap);
    for(unsigned i=0;i<8;++i)glDisable(GL_CLIP_DISTANCE0+i);
    glDisablei(GL_BLEND,0);glDepthMask(GL_FALSE);glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glDepthRange(0,1);
}
template<class T> void destroy(T& t) noexcept {
    if(t.fbo)glDeleteFramebuffers(1,&t.fbo);if(t.color)glDeleteTextures(1,&t.color);if(t.depth)glDeleteTextures(1,&t.depth);t={};
}
template<class T> std::uint64_t bytes(const T& t) noexcept {
    return std::uint64_t(t.width)*t.height*((t.format==GL_RGBA16F?8u:4u)+(t.depth?4u:0u));
}
template<class T> bool create(T& t,unsigned width,unsigned height,unsigned format,bool depth) noexcept {
    glGenFramebuffers(1,&t.fbo);glGenTextures(1,&t.color);
    glBindTexture(GL_TEXTURE_2D,t.color);glTexStorage2D(GL_TEXTURE_2D,1,format,width,height);
    for(auto axis:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T})glTexParameteri(GL_TEXTURE_2D,axis,GL_CLAMP_TO_EDGE);
    for(auto filter:{GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER})glTexParameteri(GL_TEXTURE_2D,filter,GL_NEAREST);
    if(depth){
        glGenTextures(1,&t.depth);glBindTexture(GL_TEXTURE_2D,t.depth);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,width,height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,t.fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.color,0);
    if(depth)glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,t.depth,0);
    const GLenum output=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&output);
    if(!t.fbo||!t.color||(depth&&!t.depth)||glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    t.width=width;t.height=height;t.format=format;return true;
}
GLuint compile(GLenum type,const char* source,GLint size) noexcept {
    const auto shader=glCreateShader(type);if(!shader)return 0;
    glShaderSource(shader,1,&source,&size);glCompileShader(shader);
    GLint status=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
    if(!status){glDeleteShader(shader);return 0;}return shader;
}
bool hash_matches(const std::string& data,const char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]{},object[1024]{};ULONG size=0,result=0;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&result,0)>=0&&size<=sizeof(object);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object,size,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),static_cast<ULONG>(data.size()),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;ok&&i<32;++i)ok=expected[i*2]==hex[digest[i]>>4]&&expected[i*2+1]==hex[digest[i]&15];
    return ok;
}
}

PostEffectReplay::~PostEffectReplay(){reset();}
bool PostEffectReplay::fail(const char* error) noexcept {error_=error;++stats_.failures;return false;}
bool PostEffectReplay::set_shader_directory(std::string directory) noexcept {
    if(context_||programs_[0].id)return fail("Reset before changing post shader resources");
    try{shader_directory_=std::move(directory);initialization_failed_=false;error_="";return true;}catch(...){return fail("Post shader path allocation failed");}
}
bool PostEffectReplay::initialize_programs() noexcept {
    if(programs_[0].id)return true;
    if(initialization_failed_)return false;
    initialization_failed_=true;
    if(shader_directory_.empty())return fail("Post shader directory is unset");
    std::array<Program,10> next{};GLuint vertex=0,vao=0,sampler=0;
    const auto cleanup=[&](){if(vertex)glDeleteShader(vertex);for(auto& p:next)if(p.id)glDeleteProgram(p.id);if(vao)glDeleteVertexArrays(1,&vao);if(sampler)glDeleteSamplers(1,&sampler);};
    try{
        vertex=compile(GL_VERTEX_SHADER,vertex_source,sizeof(vertex_source)-1);
        if(!vertex){cleanup();return fail("Post vertex shader did not compile");}
        for(unsigned i=0;i<next.size();++i){
            const auto& asset=post_assets::shaders[i];
            std::ifstream file(shader_directory_+"/"+asset.name,std::ios::binary|std::ios::ate);
            const auto length=file.tellg();
            if(!file||length<=0||length>16384){cleanup();return fail("Post shader missing or exceeds size budget");}
            std::string source(static_cast<std::size_t>(length),'\0');file.seekg(0);file.read(source.data(),length);
            if(!file||!hash_matches(source,asset.sha256)){cleanup();return fail("Post shader SHA-256 does not match installed allowlist");}
            const GLuint fragment=compile(GL_FRAGMENT_SHADER,source.data(),static_cast<GLint>(source.size()));
            if(!fragment){cleanup();return fail("Pinned post fragment shader did not compile");}
            auto& p=next[i];p.id=glCreateProgram();glAttachShader(p.id,vertex);glAttachShader(p.id,fragment);
            glBindFragDataLocation(p.id,0,"fragColor");glLinkProgram(p.id);glDeleteShader(fragment);
            GLint linked=0;glGetProgramiv(p.id,GL_LINK_STATUS,&linked);
            if(!linked){cleanup();return fail("Pinned post shader program did not link");}
            const auto location=[&](const char* name){return glGetUniformLocation(p.id,name);};
            glProgramUniform1i(p.id,location("DiffuseSampler"),0);
            glProgramUniform1i(p.id,location("Mask"),1);glProgramUniform1i(p.id,location("DownTexture"),1);
            glProgramUniform1i(p.id,location("Background"),2);
            p.in_size=location("InSize");p.out_size=location("OutSize");p.time=location("Time");p.direction=location("Direction");
            p.strength=location("DistortionStrength");p.frequency=location("Frequency");p.progress=location("Progress");
            p.inter_scale=location("InterScale");p.intensive=location("BloomIntensive");p.base=location("BloomBase");
            p.upper=location("BloomThresholdUp");p.lower=location("BloomThresholdDown");
            p.level=location("Level");p.center=location("Center");p.color_modulate=location("ColorModulate");
            if(i==8){
                p.contrast=location("Contrast");p.brightness=location("Brightness");p.intensity=location("Intensity");
                p.speed=location("Speed");p.mode=location("Mode");p.impact_threshold=location("ImpactThreshold");
                p.impact_threshold_lerp=location("ImpactThresholdLerp");p.focal_uv=location("FocalUV");
                p.chromatic_strength=location("ChromaticStrength");p.lens_distort_strength=location("LensDistortStrength");
                p.color_dark=location("ColorDark");p.color_light=location("ColorLight");p.invert_factor=location("InvertFactor");
                p.capped_grayscale=location("CappedGrayscale");p.radial_strength=location("RadialStrength");p.samples=location("Samples");
                for(auto uniform:{p.contrast,p.brightness,p.time,p.intensity,p.speed,p.mode,p.impact_threshold,p.impact_threshold_lerp,
                                  p.focal_uv,p.chromatic_strength,p.lens_distort_strength,p.color_dark,p.color_light,p.invert_factor,
                                  p.capped_grayscale,p.radial_strength,p.samples})
                    if(uniform<0){cleanup();return fail("Pinned bw_contrast uniform missing");}
            }
            if(i==9&&p.color_modulate<0){cleanup();return fail("Pinned blit ColorModulate uniform missing");}
        }
        glGenVertexArrays(1,&vao);glGenSamplers(1,&sampler);
        if(!vao||!sampler){cleanup();return fail("Post geometry/sampler creation failed");}
        glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glSamplerParameteri(sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glSamplerParameteri(sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glDeleteShader(vertex);vertex=0;programs_=next;vao_=vao;sampler_=sampler;context_=wglGetCurrentContext();initialization_failed_=false;return true;
    }catch(...){cleanup();return fail("Post shader initialization failed");}
}
bool PostEffectReplay::prepare(const MetaRendererPostSceneFrame& frame) noexcept {
    if(mask_.width==frame.width&&mask_.height==frame.height&&ping_[0].format==frame.color_format)return true;
    std::array<Target,12> next{};
    const auto clean=[&](){for(auto& t:next)destroy(t);};
    // Mask and pyramid targets use Minecraft RenderTarget RGBA8. Working color
    // retains native RGBA16F when Renderer uses HDR; no hidden sRGB conversion.
    if(!create(next[0],frame.width,frame.height,GL_RGBA8,true)||
       !create(next[1],frame.width,frame.height,frame.color_format,false)||
       !create(next[2],frame.width,frame.height,frame.color_format,false)){clean();return false;}
    for(unsigned i=0;i<5;++i)if(!create(next[3+i],std::max(1u,frame.width>>(i+1)),std::max(1u,frame.height>>(i+1)),GL_RGBA8,false)){clean();return false;}
    for(unsigned i=0;i<4;++i)if(!create(next[8+i],std::max(1u,frame.width>>(i+1)),std::max(1u,frame.height>>(i+1)),GL_RGBA8,false)){clean();return false;}
    destroy(mask_);for(auto& t:ping_)destroy(t);for(auto& t:down_)destroy(t);for(auto& t:up_)destroy(t);
    mask_=next[0];ping_[0]=next[1];ping_[1]=next[2];
    for(unsigned i=0;i<5;++i)down_[i]=next[3+i];for(unsigned i=0;i<4;++i)up_[i]=next[8+i];
    ++stats_.allocations;stats_.resident_bytes=0;for(const auto& t:next)stats_.resident_bytes+=bytes(t);
    return true;
}
void PostEffectReplay::pass(unsigned index,const Target& output,GLuint first,GLuint second,GLuint third,
                            unsigned input_width,unsigned input_height,const PostEffectStage* stage) noexcept {
    const auto& p=programs_[index];fullscreen_state();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.fbo);glViewport(0,0,output.width,output.height);
    glUseProgram(p.id);glBindVertexArray(vao_);
    const GLuint textures[]={first,second,third};
    for(unsigned i=0;i<3;++i){glActiveTexture(texture_units[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,sampler_);}
    glUniform2f(p.in_size,float(input_width),float(input_height));glUniform2f(p.out_size,float(output.width),float(output.height));
    if(stage&&index==9){
        glUniform4fv(p.color_modulate,1,stage->params.data()+26);
    }else if(stage&&stage->kind==static_cast<unsigned>(PostEffectKind::bw_contrast)){
        const auto& v=stage->params;
        glUniform1f(p.contrast,v[0]);glUniform1f(p.brightness,v[1]);glUniform1f(p.time,v[2]);
        glUniform1f(p.intensity,v[3]);glUniform1f(p.speed,v[4]);glUniform1f(p.mode,v[5]);
        glUniform1f(p.impact_threshold,v[6]);glUniform1f(p.impact_threshold_lerp,v[7]);glUniform2f(p.focal_uv,v[8],v[9]);
        glUniform1f(p.chromatic_strength,v[10]);glUniform1f(p.lens_distort_strength,v[11]);
        glUniform3fv(p.color_dark,1,v.data()+12);glUniform3fv(p.color_light,1,v.data()+15);
        glUniform1f(p.invert_factor,v[18]);glUniform1f(p.capped_grayscale,v[19]);
        glUniform1f(p.radial_strength,v[20]);glUniform1i(p.samples,static_cast<int>(v[21]));
        // PostPassBase clears the destination after applying the JSON blend
        // state. The same source-alpha factors apply to RGB and alpha.
        glClearBufferfv(GL_COLOR,0,v.data()+22);glEnablei(GL_BLEND,0);
        glBlendEquationi(0,GL_FUNC_ADD);glBlendFunci(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }else if(stage&&stage->kind==static_cast<unsigned>(PostEffectKind::color_dispersion)) {
        glUniform3f(p.level,stage->params[0],stage->params[1],stage->params[2]);
        glUniform2f(p.center,stage->params[3],stage->params[4]);
        glUniform3f(p.color_modulate,stage->params[5],stage->params[6],stage->params[7]);
    }else if(stage){
        glUniform1f(p.time,stage->params[0]);glUniform2f(p.direction,stage->params[1],stage->params[2]);
        glUniform1f(p.strength,stage->params[3]);glUniform1f(p.frequency,stage->params[4]);glUniform1f(p.progress,stage->params[5]);
        glUniform1f(p.inter_scale,stage->params[3]);glUniform1f(p.intensive,stage->params[3]);glUniform1f(p.base,stage->params[4]);
        glUniform1f(p.upper,stage->params[6]);glUniform1f(p.lower,stage->params[7]);
    }
    glDrawArrays(GL_TRIANGLES,0,3);++stats_.passes;
}
bool PostEffectReplay::replay(const MetaRendererPostSceneFrame& frame,std::span<const PostEffectStage> stages,MaskDraw draw,void* user) noexcept {
    // Empty producer frames never reuse a stale mask or touch GL resources.
    if(stages.empty()){error_="";return true;}
    const auto current=wglGetCurrentContext();
    if(!current||(context_&&context_!=current)||!GLEW_VERSION_4_4||stages.size()>max_post_effect_stages)return fail("Post context or stage budget rejected");
    if(initialization_failed_)return false;
    bool has_bloom=false,has_impact=false;
    for(const auto& stage:stages){if(!valid_post_effect_stage(stage)||(stage.batch_count&&!draw))return fail("Invalid post stage or missing mask callback");has_bloom|=stage.kind==1;has_impact|=stage.kind==static_cast<unsigned>(PostEffectKind::bw_contrast);}
    if(!frame.width||!frame.height||frame.width>16384||frame.height>16384||(has_bloom&&(frame.width<32||frame.height<32)))return fail("Post target dimensions rejected");
    const std::uint64_t pixels=std::uint64_t(frame.width)*frame.height;
    const std::uint64_t next_bytes=pixels*(frame.color_format==GL_RGBA16F?36u:24u)+pixels*3;
    const bool resize=mask_.width!=frame.width||mask_.height!=frame.height||ping_[0].format!=frame.color_format;
    if(next_bytes+(resize?stats_.resident_bytes+scene_.resident_bytes():0)>budget)return fail("Post texture residency budget exceeded");
    for(const float* matrix:{frame.world_matrix,frame.projection_matrix,frame.inverse_world_matrix,frame.inverse_projection_matrix})
        for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return fail("Non-finite host post camera");
    ReplayState state(has_impact);
    const auto bound=[&](const Target& t){return t.fbo&&(state.read==GLint(t.fbo)||state.draw==GLint(t.fbo)||
        state.textures[0]==GLint(t.color)||state.textures[1]==GLint(t.color)||state.textures[2]==GLint(t.color));};
    if(bound(mask_)||bound(ping_[0])||bound(ping_[1]))return fail("Post private target retained by caller");
    for(auto& t:down_)if(bound(t))return fail("Post pyramid target retained by caller");
    for(auto& t:up_)if(bound(t))return fail("Post pyramid target retained by caller");
    fullscreen_state();
    if(!initialize_programs())return false;
    if(!scene_.capture(frame)||!prepare(frame))return fail("Post target/scene preparation failed");
    const auto snapshot=scene_.image();GLuint scene_color=snapshot.color;unsigned destination=0;
    for(const auto& stage:stages){
        if(stage.batch_count){
            // Private depth is reset per pipeline; one mask never occludes the
            // next, nor writes host depth or stencil.
            glCopyImageSubData(snapshot.depth,GL_TEXTURE_2D,0,0,0,0,mask_.depth,GL_TEXTURE_2D,0,0,0,0,frame.width,frame.height,1);
            fullscreen_state();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,mask_.fbo);
            const GLfloat clear[4]{};glClearBufferfv(GL_COLOR,0,clear);
            glViewport(frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3]);
            glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
            glActiveTexture(GL_TEXTURE0);
            for(unsigned i=0;i<3;++i)glBindSampler(i,0);
            if(!draw(user,frame,stage.first_batch,stage.batch_count))return fail("Post mask callback rejected frame");
        }
        auto& output=ping_[destination];
        if(stage.kind==1){
            GLuint source=mask_.color;unsigned width=frame.width,height=frame.height;
            for(unsigned i=0;i<5;++i){pass(0,down_[i],source,0,0,width,height,nullptr);source=down_[i].color;width=down_[i].width;height=down_[i].height;}
            for(int i=3;i>=0;--i){pass(1,up_[i],source,down_[i].color,0,width,height,nullptr);source=up_[i].color;width=up_[i].width;height=up_[i].height;}
            pass(2,output,source,mask_.color,scene_color,width,height,&stage);
        }else if(stage.kind==static_cast<unsigned>(PostEffectKind::bw_contrast)){
            // NightFall first blits with its effective ColorModulate into an
            // RGBA8 TextureTarget. Reuse the mask target: this kind owns no mask.
            pass(9,mask_,scene_color,0,0,frame.width,frame.height,&stage);
            if(output.format==GL_RGBA8){
                pass(8,output,mask_.color,0,0,frame.width,frame.height,&stage);
            }else{
                // The original main target is RGBA8 as well. Retain both its
                // pre-blend clear quantization and final rounding on HDR hosts
                // without allocating another full-size target. The first copy
                // preserves the quantized input while mask_ becomes the output.
                glBindFramebuffer(GL_READ_FRAMEBUFFER,mask_.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.fbo);
                glBlitFramebuffer(0,0,frame.width,frame.height,0,0,frame.width,frame.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);++stats_.passes;
                pass(8,mask_,output.color,0,0,frame.width,frame.height,&stage);
                glBindFramebuffer(GL_READ_FRAMEBUFFER,mask_.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,output.fbo);
                glBlitFramebuffer(0,0,frame.width,frame.height,0,0,frame.width,frame.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);++stats_.passes;
            }
        }else pass(stage.kind+1,output,scene_color,stage.batch_count?mask_.color:0,0,frame.width,frame.height,&stage);
        scene_color=output.color;destination^=1;
    }
    // Only successful complete graphs write host color; no borrowed target is
    // sampled while attached for drawing. Host FBO selections are untouched.
    if(glGetError()!=GL_NO_ERROR)return fail("Post GL command failed before scene commit");
    // Post effects are defined over the supplied viewport. Preserve color in
    // attachment pixels outside it; this also prevents a nested/inset frame
    // from publishing to the host's letterbox or other view.
    glCopyImageSubData(scene_color,GL_TEXTURE_2D,0,frame.viewport[0],frame.viewport[1],0,
                       frame.color_texture,GL_TEXTURE_2D,0,frame.viewport[0],frame.viewport[1],0,
                       frame.viewport[2],frame.viewport[3],1);
    if(glGetError()!=GL_NO_ERROR)return fail("Post scene color copy failed");
    ++stats_.frames;error_="";return true;
}
bool PostEffectReplay::reset() noexcept {
    if(context_&&context_!=wglGetCurrentContext())return false;
    if(!scene_.reset())return false;
    for(auto& p:programs_){if(p.id)glDeleteProgram(p.id);p={};}
    if(vao_)glDeleteVertexArrays(1,&vao_);if(sampler_)glDeleteSamplers(1,&sampler_);vao_=sampler_=0;
    destroy(mask_);for(auto& t:ping_)destroy(t);for(auto& t:down_)destroy(t);for(auto& t:up_)destroy(t);
    context_=nullptr;stats_.resident_bytes=0;initialization_failed_=false;error_="";return true;
}
}
