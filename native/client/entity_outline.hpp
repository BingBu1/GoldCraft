#pragma once
#include "post_scene_snapshot.hpp"
#include <array>

namespace goldcraft::render {
// Minecraft's entity silhouette, edge and two box-blur passes. This owns no
// world/actor state: the caller must supply a fresh, validated mask every time.
class EntityOutline final {
public:
    using MaskDraw=bool (*)(void*,const MetaRendererPostSceneFrame&) noexcept;
    struct Statistics {
        std::uint64_t frames=0,failures=0,allocations=0,resident_bytes=0;
    };
    EntityOutline()=default;
    ~EntityOutline();
    EntityOutline(const EntityOutline&)=delete;
    EntityOutline& operator=(const EntityOutline&)=delete;
    // draw receives a cleared RGBA8 target and the host camera/viewport. It must
    // emit unlit team RGB/opaque alpha, preserving texture alpha rejection and
    // all additional GL/cache state it changes. No world depth is attached:
    // silhouettes remain visible through walls, just like the original game.
    // gamma is the host's actual gamma exponent; it is applied only when the
    // destination is linear. A null draw is an empty frame and touches no GL.
    // All private passes must succeed before host color is committed. Borrowed
    // attachments, depth/stencil, other MRTs and pixels outside viewport survive.
    bool replay(const MetaRendererPostSceneFrame&,float gamma,MaskDraw,void*) noexcept;
    bool reset() noexcept;
    const Statistics& statistics() const noexcept {return stats_;}
    const char* last_error() const noexcept {return error_;}
private:
    struct Target {GLuint fbo=0,color=0;unsigned width=0,height=0,format=0;};
    struct Program {GLuint id=0;GLint direction=-1,gamma=-1;};
    bool initialize() noexcept;
    bool prepare(const MetaRendererPostSceneFrame&) noexcept;
    bool fail(const char*) noexcept;
    HGLRC context_=nullptr;
    GLuint vao_=0,samplers_[2]{};
    std::array<Target,3> targets_{};
    std::array<Program,4> programs_{};
    Statistics stats_{};
    const char* error_="";
};
}
