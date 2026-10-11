#pragma once
#include "post_scene_snapshot.hpp"
#include "goldcraft/post_effects.hpp"
#include <array>
#include <span>
#include <string>

namespace goldcraft::render {
// Replays the installed, hash-pinned VIX/NightFall graph on a private copy of
// the completed CS scene. No Minecraft scene pixels or wire GL names are used.
class PostEffectReplay final {
public:
    using MaskDraw = bool (*)(void*, const MetaRendererPostSceneFrame&,
                             std::uint8_t first_batch, std::uint8_t batch_count) noexcept;
    struct Statistics {
        std::uint64_t frames=0, failures=0, passes=0, allocations=0;
        std::uint64_t resident_bytes=0;
    };
    PostEffectReplay()=default;
    ~PostEffectReplay();
    PostEffectReplay(const PostEffectReplay&)=delete;
    PostEffectReplay& operator=(const PostEffectReplay&)=delete;
    // Call outside drawing, before first replay or after reset. Files are read
    // and SHA-256 checked once when programs are created. They are local assets
    // extracted by tools/Extract-VixPostShaders.py, never published source.
    bool set_shader_directory(std::string directory) noexcept;
    // The mask framebuffer and exact host viewport are active inside draw.
    // draw must use frame's world/projection and preserve all additional GL and
    // Renderer cache state it touches. False aborts the complete frame: host
    // color, depth and stencil stay unchanged. The supplied order is retained.
    bool replay(const MetaRendererPostSceneFrame&, std::span<const PostEffectStage>,
                MaskDraw draw, void* user) noexcept;
    // Call with the owning context current before destroying that context.
    // Wrong-context reset is rejected, preventing deletion of recycled names.
    bool reset() noexcept;
    const Statistics& statistics() const noexcept { return stats_; }
    const char* last_error() const noexcept { return error_; }
private:
    struct Target { GLuint fbo=0,color=0,depth=0; unsigned width=0,height=0,format=0; };
    struct Program {
        GLuint id=0;
        GLint in_size=-1,out_size=-1,time=-1,direction=-1,strength=-1,frequency=-1,progress=-1;
        GLint level=-1,center=-1,color_modulate=-1;
        GLint inter_scale=-1,intensive=-1,base=-1,upper=-1,lower=-1;
    };
    bool initialize_programs() noexcept;
    bool prepare(const MetaRendererPostSceneFrame&) noexcept;
    void pass(unsigned program, const Target&, GLuint first, GLuint second, GLuint third,
              unsigned input_width,unsigned input_height,const PostEffectStage*) noexcept;
    bool fail(const char*) noexcept;
    HGLRC context_=nullptr;
    GLuint vao_=0,sampler_=0;
    PostSceneSnapshot scene_;
    Target mask_{},ping_[2]{},down_[5]{},up_[4]{};
    std::array<Program,8> programs_{};
    std::string shader_directory_;
    Statistics stats_{};
    bool initialization_failed_=false;
    const char* error_="";
};
}
