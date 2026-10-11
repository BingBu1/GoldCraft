#include "post_scene_snapshot.hpp"

namespace goldcraft::render {
namespace {
constexpr std::uint64_t snapshot_budget=256ull*1024*1024;
std::uint64_t bytes(unsigned width,unsigned height,unsigned format) noexcept {
    return std::uint64_t(width)*height*(format==GL_RGBA16F?12u:8u);
}
// Framebuffer blits do not use shader, vertex, viewport, depth-test, color-mask
// or pixel-store state. Preserve only the bindings and raster state they change.
struct CopyState {
    GLint read=0,draw=0;
    GLboolean scissor=GL_FALSE,srgb=GL_FALSE;
    CopyState() noexcept {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        scissor=glIsEnabled(GL_SCISSOR_TEST);srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
        if(scissor)glDisable(GL_SCISSOR_TEST);
        if(srgb)glDisable(GL_FRAMEBUFFER_SRGB);
    }
    ~CopyState() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        if(scissor)glEnable(GL_SCISSOR_TEST);
        if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);
    }
};
void destroy(GLuint framebuffer,GLuint color,GLuint depth) noexcept {
    if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
    if(color)glDeleteTextures(1,&color);
    if(depth)glDeleteTextures(1,&depth);
}
}

PostSceneSnapshot::~PostSceneSnapshot(){reset();}
std::uint64_t PostSceneSnapshot::resident_bytes() const noexcept {
    return framebuffer_?bytes(width_,height_,format_):0;
}
PostSceneSnapshot::Image PostSceneSnapshot::image() const noexcept {
    if(!valid_||context_!=wglGetCurrentContext())return {};
    return {framebuffer_,color_,depth_,width_,height_,format_,generation_};
}
bool PostSceneSnapshot::reset() noexcept {
    valid_=false;
    if(framebuffer_&&context_!=wglGetCurrentContext())return false;
    destroy(framebuffer_,color_,depth_);
    framebuffer_=color_=depth_=0;width_=height_=format_=0;context_=nullptr;
    return true;
}
bool PostSceneSnapshot::prepare(const MetaRendererPostSceneFrame& frame) noexcept {
    if(framebuffer_&&width_==frame.width&&height_==frame.height&&format_==frame.color_format)return true;
    // Include both generations in the bound while resize commits atomically.
    if(bytes(frame.width,frame.height,frame.color_format)+resident_bytes()>snapshot_budget)return false;
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    GLuint next_fbo=0,next_color=0,next_depth=0;
    glGenFramebuffers(1,&next_fbo);glGenTextures(1,&next_color);glGenTextures(1,&next_depth);
    glBindTexture(GL_TEXTURE_2D,next_color);
    glTexStorage2D(GL_TEXTURE_2D,1,frame.color_format,frame.width,frame.height);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,next_depth);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,frame.width,frame.height);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
    glBindTexture(GL_TEXTURE_2D,texture);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,next_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,next_color,0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,next_depth,0);
    const GLenum output=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&output);
    if(!next_fbo||!next_color||!next_depth||glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        destroy(next_fbo,next_color,next_depth);return false;
    }
    destroy(framebuffer_,color_,depth_);
    framebuffer_=next_fbo;color_=next_color;depth_=next_depth;
    width_=frame.width;height_=frame.height;format_=frame.color_format;
    context_=wglGetCurrentContext();++generation_;++allocations_;return true;
}
bool PostSceneSnapshot::capture(const MetaRendererPostSceneFrame& frame) noexcept {
    valid_=false;
    const auto current=wglGetCurrentContext();
    constexpr unsigned color_flags=META_POST_SCENE_GAMMA|META_POST_SCENE_LINEAR;
    const auto space=frame.flags&color_flags;
    if(!current||(context_&&context_!=current)||!GLEW_VERSION_4_4||
       frame.struct_size!=sizeof(frame)||!frame.framebuffer||!frame.color_texture||!frame.depth_texture||
       !frame.depth_sample_texture||frame.view_index||(frame.flags&~(color_flags|META_POST_SCENE_HDR))||
       (space!=META_POST_SCENE_GAMMA&&space!=META_POST_SCENE_LINEAR)||
       frame.color_texture==frame.depth_texture||frame.color_texture==frame.depth_sample_texture||
       frame.depth_texture==frame.depth_sample_texture||
       !frame.width||!frame.height||frame.width>16384||frame.height>16384||frame.samples!=1||
       (frame.color_format!=GL_RGBA8&&frame.color_format!=GL_RGBA16F)||frame.depth_format!=GL_DEPTH24_STENCIL8||
       bytes(frame.width,frame.height,frame.color_format)>snapshot_budget||
       frame.framebuffer==framebuffer_||frame.color_texture==color_||frame.depth_texture==depth_)return false;
    if(frame.viewport[0]<0||frame.viewport[1]<0||frame.viewport[2]<=0||frame.viewport[3]<=0||
       std::uint64_t(frame.viewport[0])+frame.viewport[2]>frame.width||
       std::uint64_t(frame.viewport[1])+frame.viewport[3]>frame.height)return false;
    CopyState state;
    // If the caller is using one of our old images, replacing that generation
    // would invalidate its saved framebuffer binding. Reject that ownership error.
    if(framebuffer_&&(state.read==static_cast<GLint>(framebuffer_)||state.draw==static_cast<GLint>(framebuffer_)))return false;
    if(!prepare(frame))return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,frame.framebuffer);
    GLint source_read=0;glGetIntegerv(GL_READ_BUFFER,&source_read);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer_);
    glBlitFramebuffer(0,0,frame.width,frame.height,0,0,frame.width,frame.height,
        GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    glReadBuffer(source_read);
    ++copies_;valid_=true;return true;
}
}
