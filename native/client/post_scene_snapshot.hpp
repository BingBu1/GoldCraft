#pragma once
#include <windows.h>
#include <GL/glew.h>
#include <IMetaRendererPostScene.h>
#include <cstdint>

namespace goldcraft::render {
// Owns a read-only copy of the completed host world, before viewmodel depth is
// cleared. It is a GPU copy, never a screenshot/readback or hidden MC scene.
// Frame descriptors and returned GL names must stay within the owning context.
// Returned names are borrowed until the next capture/reset. Unbind them before
// capture; resizing may release the prior generation.
class PostSceneSnapshot final {
public:
    struct Image {
        GLuint framebuffer=0,color=0,depth=0;
        unsigned width=0,height=0,color_format=0;
        std::uint64_t generation=0;
    };
    PostSceneSnapshot()=default;
    ~PostSceneSnapshot();
    PostSceneSnapshot(const PostSceneSnapshot&)=delete;
    PostSceneSnapshot& operator=(const PostSceneSnapshot&)=delete;
    bool capture(const MetaRendererPostSceneFrame& frame) noexcept;
    // A reset must happen before destroying the owner context. A different
    // current context is rejected, so recycled GL names cannot be deleted there.
    bool reset() noexcept;
    void invalidate() noexcept {valid_=false;}
    Image image() const noexcept;
    std::uint64_t allocations() const noexcept {return allocations_;}
    std::uint64_t copies() const noexcept {return copies_;}
    std::uint64_t resident_bytes() const noexcept;
private:
    bool prepare(const MetaRendererPostSceneFrame& frame) noexcept;
    HGLRC context_=nullptr;
    GLuint framebuffer_=0,color_=0,depth_=0;
    unsigned width_=0,height_=0,format_=0;
    std::uint64_t generation_=0,allocations_=0,copies_=0;
    bool valid_=false;
};
}
