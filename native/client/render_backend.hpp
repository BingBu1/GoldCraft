#pragma once
#include <windows.h>
#include <GL/glew.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <optional>
#include <IMetaRendererScene.h>
#include <IMetaRendererSceneCulling.h>
#include <IMetaRendererSceneLights.h>
#include <IMetaRendererSceneVision.h>
#include <IMetaRendererSceneDistortion.h>
#include <IMetaRendererSceneStaticShadows.h>
#include <IMetaRendererWorldEdit.h>
#include <IMetaRendererPostScene.h>
#include <IMetaRendererStudioOutline.h>
#include "goldcraft/atlas.hpp"
#include "goldcraft/particles.hpp"
#include "goldcraft/render_bounds.hpp"
#include "goldcraft/lights.hpp"
#include "render_state.hpp"

namespace goldcraft::render {
struct Vertex { float x,y,z,u,v; std::uint32_t color,overlay=0; };
static_assert(sizeof(Vertex)==28);

class Mesh {
public:
    Mesh()=default;
    Mesh(const Mesh&)=delete;
    Mesh& operator=(const Mesh&)=delete;
    ~Mesh();
    void draw(std::span<const Vertex> vertices, std::uint64_t revision, bool dynamic, bool lines=false);
private:
    HGLRC context_=nullptr;
    GLuint vao_=0,vbo_=0,ebo_=0;
    std::uint64_t revision_=0;
    std::size_t vertices_=0,capacity_=0,index_capacity_=0;
};

struct Statistics {
    std::uint64_t gl_loader_calls=0,gl_loader_reuses=0;
    std::uint64_t uploads=0,upload_bytes=0,draws=0,opaque_passes=0,shadow_passes=0;
    std::uint64_t depth_only_shadow_passes=0;
    GLenum error=0;
    std::uint64_t error_count=0;
    std::uint64_t shadow_bounds_tests=0,shadow_bounds_culled=0;
    bool core=false,renderer=false,scene_api=false,post_scene_api=false,scene_culling_api=false;
    bool scene_lights_api=false;
    bool scene_vision_api=false,scene_vision_accepted=false;
    MetaRendererSceneVision scene_vision_state{};
    bool scene_distortion_api=false,scene_distortion_accepted=false;
    MetaRendererSceneDistortion scene_distortion_state{};
    std::uint32_t scene_lights=0;
    bool scene_static_shadows_api=false,scene_static_shadows_registered=false;
    std::uint64_t static_shadow_invalidations=0;
};
const Statistics& statistics();
bool initialize(IMetaRendererSceneCallbacks* callbacks, void(*log)(const std::string&));
void shutdown();
bool scene_active();
bool cull_shadow_bounds(const RenderBounds& bounds);
IMetaRendererWorldEdit* world_edit();
IMetaRendererWorldEdit2* world_edit2();
IMetaRendererPostScene* post_scene();
IMetaRendererStudioOutline* studio_outline();
bool owns_context();
void set_light_shadow(int key, unsigned size);
bool update_scene_lights(std::span<const SceneLight> lights) noexcept;
void clear_scene_lights();
bool update_scene_distortion(const MetaRendererSceneDistortion& state) noexcept;
bool update_scene_vision(const MetaRendererSceneVision& state) noexcept;
bool set_static_scene(IMetaRendererStaticSceneCallbacks* callbacks,bool enabled) noexcept;
void invalidate_static_scene(const RenderBounds* bounds=nullptr) noexcept;
bool static_scene_covered() noexcept;
void upload_texture(GLuint& id,unsigned& width,unsigned& height,unsigned next_width,unsigned next_height,std::span<const std::uint8_t> rgba);
void upload_atlas_patches(GLuint id,std::span<const AtlasPatch> patches);
void validate_atlas_upload();
bool capture(const char* path);
bool draw_hud(GLuint texture,float mouse_x,float mouse_y,bool menu,unsigned width,unsigned height);
class Pass {
public:
    // This scope owns its shader/texture/depth state until destruction. Only
    // GoldCraft mesh operations run inside it; host callbacks run outside.
    Pass(bool transparent, std::uint32_t flags, const MetaRendererPostSceneFrame* post_mask=nullptr);
    ~Pass();
    Pass(const Pass&)=delete;
    Pass& operator=(const Pass&)=delete;
    bool ready() const {return ready_;}
    void texture(GLuint id);
    void emissive(bool value);
    void depth_write(bool value);
    void view_offset(bool value);
    void feedback(unsigned mode);
    bool outline(bool cull);
    bool effect(const ParticleMaterial& material,GLuint texture);
private:
    std::optional<DrawState> state_;
    bool ready_=false;
    GLint emissive_location_=-1;
    GLint feedback_location_=-1;
    GLint effect_location_=-1,cutoff_location_=-1,color_mod_location_=-1;
    GLint view_offset_location_=-1;
    bool view_offset_=false;
    std::optional<ParticleMaterial> effect_material_;
    bool effects_allowed_=false;
    int emissive_value_=-1;
    std::optional<GLuint> texture_;
    bool depth_write_=false;
    bool stencil_write_=false;
    bool shadow_=false;
    bool raw_mask_=false;
    bool depth_only_=false;
};
}
