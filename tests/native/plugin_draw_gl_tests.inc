// Exercise the extracted production plugin dispatcher with real Pass/Mesh and
// production shaders on the hidden driver context. Engine connection, clock,
// ownership/freshness and elapsed-time recording are explicit input adapters.
namespace plugin_draw_probe {
namespace render = goldcraft::render;
using goldcraft::ParticleMaterial;
using goldcraft::particle_untextured;
using goldcraft::RenderBounds;
using goldcraft::Reader;
using goldcraft::Vec3;
using goldcraft::ProtocolError;
using goldcraft::to_goldsrc;
using goldcraft::entity_outline;
using goldcraft::entity_outline_cull;
using goldcraft::read_atlas_patches;
using goldcraft::AtlasFootprint;
using goldcraft::AtlasPixelTiles;
using Vertex = render::Vertex;
enum class WorkStage {draw_transparent,draw_shadow,draw_opaque,pass_construct,pass_restore,
    sections,entities,feedback,entity_effects,particle_draw,count};
std::array<unsigned,static_cast<unsigned>(WorkStage::count)> work{};
struct WorkScope {
    explicit WorkScope(WorkStage stage){++work[static_cast<unsigned>(stage)];}
};
#include "plugin_draw_types.inc"

struct Link {bool online=true,outlines=true;std::uint64_t current_generation=1;bool connected()const{return online;}bool entity_outlines()const{return online&&outlines;}std::uint64_t generation()const{return current_generation;}} link;
std::uint64_t world=1,last_link_generation=1;
GLuint atlas_texture{};
unsigned atlas_width{},atlas_height{};
std::vector<std::uint8_t> atlas_alpha;
AtlasPixelTiles atlas_pixel_tiles;
std::uint64_t atlas_generation=1,atlas_uploads{},atlas_animation_packets{},atlas_patch_bytes{};
double last_atlas_animation{},atlas_animation_interval_ms{};
bool atlas_logged=false;
std::uint64_t static_shadow_callbacks{},static_shadow_draws{},static_shadow_section_skips{},static_shadow_failures{},static_shadow_alpha_invalidations{};
std::uint64_t static_shadow_alpha_changed_packets{},static_shadow_alpha_unreferenced_packets{},static_shadow_alpha_affected_sections{},static_shadow_alpha_fallbacks{};
std::map<std::array<std::int32_t,4>,Section> sections;
std::map<unsigned,Texture> entity_textures,particle_textures;
std::vector<EntityBatch> entity_batches,crack_batches;
std::vector<EffectBatch> entity_effect_batches,particle_batches;
std::vector<Vertex> outline_vertices;
std::shared_ptr<render::Mesh> outline_gpu;
std::uint64_t entity_revision=1,entity_drawn_revision{},entity_presented_frames{},
    feedback_revision=1,feedback_draws{},particle_revision=1,particle_draws{},entity_effect_draws{},frames_drawn{};
unsigned minecraft_life=1,server_player_life=1,feedback_life=1,particle_life=1;
double now=10,last_feedback=10,last_particle_frame=10;
bool fresh=true,control=true,minecraft_menu=false,section_logged=false,draw_logged=false;
struct Cvar {float value=1;} particle_cvar;
std::unique_ptr<render::EntityOutline> entity_outline_replay;
Cvar* entity_outline_gamma=nullptr;
std::string entity_outline_error;
bool matching_protocol=true;
Cvar* render_particles=nullptr;
Cvar* render_scene_cull=nullptr;
Cvar* render_scene_static_shadows=nullptr;
GLenum last_gl_error{};
unsigned logs{},cases{},empty_cases{};
double Seconds(){return now;}
bool EntityFrameFresh(){return fresh;}
bool MatchingHostProtocol(){return matching_protocol;}
bool HasControl(){return control;}
void Log(const std::string&){++logs;}
bool DrawStaticShadow(std::uint32_t flags) noexcept;
struct StaticCallback final : IMetaRendererStaticSceneCallbacks {bool DrawStaticShadow(std::uint32_t flags)noexcept override{return plugin_draw_probe::DrawStaticShadow(flags);}} static_scene_callbacks;
#include "plugin_draw.inc"

void reset(Resources& r){
    sections.clear();entity_textures.clear();particle_textures.clear();entity_batches.clear();
    crack_batches.clear();entity_effect_batches.clear();particle_batches.clear();outline_vertices.clear();
    outline_gpu=std::make_shared<Mesh>();
    link={};last_link_generation=1;world=1;atlas_texture=r.texture;atlas_generation=1;
    matching_protocol=true;entity_outline_error.clear();
    atlas_width=r.tw;atlas_height=r.th;atlas_alpha.assign(std::size_t(r.tw)*r.th,255);
    atlas_pixel_tiles.reset(atlas_width,atlas_height);
    atlas_uploads=atlas_animation_packets=atlas_patch_bytes=0;last_atlas_animation=atlas_animation_interval_ms=0;atlas_logged=false;
    static_shadow_callbacks=static_shadow_draws=static_shadow_section_skips=static_shadow_failures=static_shadow_alpha_invalidations=0;
    static_shadow_alpha_changed_packets=static_shadow_alpha_unreferenced_packets=static_shadow_alpha_affected_sections=static_shadow_alpha_fallbacks=0;
    fresh=control=true;minecraft_menu=section_logged=draw_logged=false;
    minecraft_life=server_player_life=feedback_life=particle_life=1;
    now=last_feedback=last_particle_frame=10;render_particles=nullptr;particle_cvar.value=1;
    render_scene_cull=nullptr;render::scene_culling=nullptr;
    render_scene_static_shadows=nullptr;
    render::set_static_scene(nullptr,false);render::scene_static_shadows=nullptr;render::callback=nullptr;
    render::stats.scene_static_shadows_api=false;
    entity_revision=feedback_revision=particle_revision=1;
    entity_drawn_revision=entity_presented_frames=feedback_draws=particle_draws=entity_effect_draws=frames_drawn=0;
    logs=0;last_gl_error=0;work={};
}
std::vector<Vertex> quad(Resources& r,float center=0,float width=1,float z=0){
    std::vector<Vertex> result(r.quad.begin(),r.quad.end());
    for(auto& v:result){v.x=v.x*width+center;v.z=z;}
    return result;
}
void section(Resources& r,unsigned flags=0,float center=0,float width=1){
    const auto key=static_cast<std::int32_t>(sections.size());
    Section value{1,flags,quad(r,center,width)};
    for(const auto& v:value.vertices)value.bounds.add(v.x,v.y,v.z);
    RebuildSectionFootprint(value);
    sections.emplace(std::array<std::int32_t,4>{key,0,0,0},std::move(value));
}
EntityBatch entity(Resources& r,unsigned texture=0,unsigned flags=0,float center=0,float width=1,float z=0){
    EntityBatch value{texture,flags,quad(r,center,width,z)};
    for(const auto& v:value.vertices)value.bounds.add(v.x,v.y,v.z);
    return value;
}
EffectBatch effect(Resources& r,ParticleMaterial material){
    return {material,quad(r),std::make_shared<Mesh>()};
}
std::uint8_t stencil(int x,int y){
    std::uint8_t value{};glReadPixels(x,y,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&value);return value;
}
float depth(int x=32,int y=32){
    float value{};glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&value);return value;
}
Pixels draw_case(Resources& r,bool transparent,std::uint32_t flags,unsigned draws,
    std::array<float,4> background={0,0,0,0}){
    clear();glClearBufferfv(GL_COLOR,0,background.data());r.sentinel();
    const auto saved=snapshot();const auto initial=statistics().draws;
    const auto initial_uploads=statistics().uploads;const auto initial_frames=frames_drawn;
    work={};probe::reset();probe::count_allocations=draws==0;
    Draw(transparent,flags);
    probe::count_allocations=false;
    assert(statistics().draws==initial+draws);
    assert(work[static_cast<unsigned>(WorkStage::pass_construct)]==(draws?1u:0u));
    assert(work[static_cast<unsigned>(WorkStage::pass_restore)]==(draws?1u:0u));
    assert(frames_drawn==initial_frames+(draws?1u:0u));
    if(!draws){
        // No state capture, shader work, bind, GL error poll, upload, draw or
        // allocation. The production wrapper was never constructed either.
        assert(probe::queries==0&&probe::locations==0&&probe::uniforms==0&&probe::bindings==0&&
            probe::texture_parameters==0&&probe::errors==0&&probe::allocations==0);
        assert(statistics().uploads==initial_uploads);++empty_cases;
    }
    assert(snapshot()==saved);assert(glGetError()==GL_NO_ERROR);++cases;
    const auto result=pixels();
    if(!draws)assert(color_count(result,{0,0,0,0})==Size*Size);
    return result;
}

struct StaticProvider final : IMetaRendererSceneStaticShadows {
    IMetaRendererSceneCallbacks* owner=nullptr;
    IMetaRendererStaticSceneCallbacks* registered=nullptr;
    bool accept=true,covered=false;
    unsigned registrations=0,unregistrations=0,queries=0;
    struct Bounds {bool all;std::array<float,3> min{},max{};};
    std::vector<Bounds> invalidations;
    bool RegisterStaticScene(IMetaRendererSceneCallbacks* next,IMetaRendererStaticSceneCallbacks* draw) noexcept override {
        ++registrations;if(!accept)return false;owner=next;registered=draw;return true;
    }
    void UnregisterStaticScene(IMetaRendererSceneCallbacks* next) noexcept override {
        ++unregistrations;if(next==owner){owner=nullptr;registered=nullptr;covered=false;}
    }
    void InvalidateStaticScene(IMetaRendererSceneCallbacks*,const float* min,const float* max) noexcept override {
        Bounds value{!min||!max};
        if(!value.all)for(unsigned i=0;i<3;++i){value.min[i]=min[i];value.max[i]=max[i];}
        invalidations.push_back(value);covered=false;
    }
    bool IsStaticSceneCovered(IMetaRendererSceneCallbacks* next) noexcept override {++queries;return next==owner&&registered&&covered;}
};
struct StaticOwner final : IMetaRendererSceneCallbacks {void DrawOpaqueScene(std::uint32_t flags)override{Draw(false,flags);}};
std::vector<float> depth_pixels(){
    std::vector<float> result(Size*Size);glReadPixels(0,0,Size,Size,GL_DEPTH_COMPONENT,GL_FLOAT,result.data());return result;
}
void static_case(Resources& r,std::uint32_t flags,unsigned draws,bool success){
    clear();r.sentinel();const auto saved=snapshot();const auto before=statistics().draws;
    work={};probe::reset();probe::count_allocations=draws==0&&success;
    assert(DrawStaticShadow(flags)==success);probe::count_allocations=false;
    assert(statistics().draws==before+draws&&snapshot()==saved&&glGetError()==GL_NO_ERROR);
    if(!draws&&success)assert(probe::queries==0&&probe::allocations==0&&probe::bindings==0);
    ++cases;
}

void static_shadow_tests(Resources& r){
    StaticProvider provider;StaticOwner owner;StaticCallback callback;
    const auto bind=[&]{render::scene_static_shadows=&provider;render::callback=&owner;render::stats.scene_static_shadows_api=true;};
    reset(r);bind();provider.accept=false;
    assert(!render::set_static_scene(&callback,true)&&!statistics().scene_static_shadows_registered);
    provider.accept=true;assert(render::set_static_scene(&callback,true));
    assert(statistics().scene_static_shadows_registered&&provider.invalidations.size()==1&&provider.invalidations.back().all);
    const auto registrations=provider.registrations,invalidations=provider.invalidations.size();
    assert(render::set_static_scene(&callback,true)&&provider.registrations==registrations&&provider.invalidations.size()==invalidations);
    provider.covered=true;assert(render::static_scene_covered());
    assert(!render::set_static_scene(&callback,false)&&!render::static_scene_covered());
    assert(provider.unregistrations==1&&provider.invalidations.size()==invalidations+1);
    assert(render::set_static_scene(&callback,true));
    reset(r);assert(!render::set_static_scene(&callback,true)&&!render::static_scene_covered());

    // A valid empty scene completes with no Pass or GL work. Missing atlas,
    // connection/generation, world and context cannot complete a cache bake.
    static_case(r,META_SCENE_SHADOW,0,true);
    atlas_generation=0;static_case(r,META_SCENE_SHADOW,0,false);atlas_generation=1;
    atlas_texture=0;static_case(r,META_SCENE_SHADOW,0,false);atlas_texture=r.texture;
    link.online=false;static_case(r,META_SCENE_SHADOW,0,false);link.online=true;
    ++link.current_generation;static_case(r,META_SCENE_SHADOW,0,false);--link.current_generation;
    world=0;static_case(r,META_SCENE_SHADOW,0,false);world=1;
    const auto context=render::current_context;render::current_context=nullptr;
    static_case(r,META_SCENE_SHADOW,0,false);render::current_context=context;
    static_case(r,0,0,false);
    Cvar disabled;disabled.value=0;render_scene_static_shadows=&disabled;
    static_case(r,META_SCENE_SHADOW,0,false);render_scene_static_shadows=nullptr;

    reset(r);section(r,0,-.5f,.5f);entity_batches.push_back(entity(r,0,2,.5f,.5f));
    section(r,1);ParticleMaterial material;material.texture=particle_untextured;
    particle_batches.push_back(effect(r,material));entity_effect_batches.push_back(effect(r,material));
    crack_batches.push_back(entity(r));outline_vertices=quad(r);
    draw_case(r,false,META_SCENE_SHADOW,2);const auto legacy_depth=depth_pixels();
    bind();assert(render::set_static_scene(&callback,true));
    static_case(r,META_SCENE_SHADOW,1,true);
    const auto static_depth=depth_pixels();assert(static_depth!=legacy_depth);
    provider.covered=true;const auto before=statistics().draws;const auto saved=snapshot();
    Draw(false,META_SCENE_SHADOW);
    assert(statistics().draws==before+1&&depth_pixels()==legacy_depth&&snapshot()==saved);
    assert(static_shadow_section_skips==sections.size()&&particle_draws==0&&feedback_draws==0&&entity_effect_draws==0);
    provider.covered=false;draw_case(r,false,META_SCENE_SHADOW,2); // Point-cache failure / spot / directional / static0.
    provider.covered=true;render_scene_static_shadows=&disabled;draw_case(r,false,META_SCENE_SHADOW,2);
    render_scene_static_shadows=nullptr;const auto queries=provider.queries;
    draw_case(r,false,0,2);assert(provider.queries==queries); // Main scene never queries coverage.

    reset(r);section(r);
    const auto renderer=render::renderer;render::renderer=nullptr;
    static_case(r,META_SCENE_SHADOW,0,false);render::renderer=renderer;
    // A GL failure is recorded when the actual Pass is destroyed. It must
    // reject completion rather than being noticed after a true return value.
    clear();r.sentinel();glEnable(0xdead);const auto failures=static_shadow_failures;
    assert(!DrawStaticShadow(META_SCENE_SHADOW)&&static_shadow_failures==failures+1);
    assert(glGetError()==GL_NO_ERROR);++cases;
    reset(r);bind();UpdateStaticScene();assert(statistics().scene_static_shadows_registered);
    link.online=false;UpdateStaticScene();assert(!statistics().scene_static_shadows_registered&&!provider.registered);
    link.online=true;++link.current_generation;UpdateStaticScene();assert(!statistics().scene_static_shadows_registered);
    last_link_generation=link.current_generation;atlas_generation=0;UpdateStaticScene();assert(!statistics().scene_static_shadows_registered);
    atlas_generation=1;UpdateStaticScene();assert(statistics().scene_static_shadows_registered);
    render_scene_static_shadows=&disabled;UpdateStaticScene();assert(!statistics().scene_static_shadows_registered);
    render_scene_static_shadows=nullptr;UpdateStaticScene();assert(statistics().scene_static_shadows_registered);
    ClearSections();UpdateStaticScene();assert(!statistics().scene_static_shadows_registered);
    reset(r); // Revoke fixture pointers before their stack lifetime ends.
}
void atlas_shadow_tests(Resources& r){
    reset(r);StaticProvider provider;StaticOwner owner;StaticCallback callback;
    render::scene_static_shadows=&provider;render::callback=&owner;
    assert(render::set_static_scene(&callback,true));provider.invalidations.clear();
    atlas_texture=0;atlas_width=atlas_height=0;atlas_alpha.clear();atlas_generation=0;
    const auto full_packet=[](std::uint64_t generation){
        goldcraft::Writer out;out.u64(1);out.u64(generation);out.u32(2);out.u32(2);
        for(unsigned i=0;i<4;++i)out.u32(0xffffffff);return out.data;
    };
    const auto patch_packet=[](std::uint32_t color){
        goldcraft::Writer out;out.u64(1);out.u64(2);out.u32(1);out.u32(0);out.u32(0);out.u32(1);out.u32(1);out.u32(color);return out.data;
    };
    auto data=full_packet(2);Reader full(data);ReadAtlas(full);
    assert(atlas_texture&&atlas_generation==2&&atlas_alpha==std::vector<std::uint8_t>(4,255));
    assert(provider.invalidations.size()==1&&provider.invalidations.back().all);
    section(r);static_case(r,META_SCENE_SHADOW,1,true);const auto opaque_depth=depth_pixels();
    data=patch_packet(0xff345678);Reader rgb(data);ReadAtlasPatches(rgb);
    assert(provider.invalidations.size()==1&&static_shadow_alpha_invalidations==0&&atlas_alpha[0]==255);
    static_case(r,META_SCENE_SHADOW,1,true);assert(depth_pixels()==opaque_depth);
    data=patch_packet(0x00345678);Reader alpha(data);ReadAtlasPatches(alpha);
    assert(provider.invalidations.size()==2&&static_shadow_alpha_invalidations==1&&atlas_alpha[0]==0);
    static_case(r,META_SCENE_SHADOW,1,true);assert(depth_pixels()!=opaque_depth);
    Reader unchanged(data);ReadAtlasPatches(unchanged);assert(provider.invalidations.size()==2);
    data=patch_packet(0xfe345678);Reader raw_alpha(data);ReadAtlasPatches(raw_alpha);
    assert(provider.invalidations.size()==3&&atlas_alpha[0]==254);
    data=patch_packet(0xff345678);Reader raw_alpha_again(data);ReadAtlasPatches(raw_alpha_again);
    assert(provider.invalidations.size()==4&&atlas_alpha[0]==255); // Raw bytes, not a guessed binary cutoff.
    const auto accepted=atlas_alpha;data.pop_back();bool rejected=false;
    try{Reader malformed(data);ReadAtlasPatches(malformed);}catch(const ProtocolError&){rejected=true;}
    assert(rejected&&atlas_alpha==accepted&&provider.invalidations.size()==4);
    data=patch_packet(0xff778899);glEnable(0xdead);rejected=false;
    try{Reader failed(data);ReadAtlasPatches(failed);}catch(const ProtocolError&){rejected=true;}
    assert(rejected&&atlas_generation==0&&atlas_alpha.empty()&&provider.invalidations.size()==5&&glGetError()==GL_NO_ERROR);
    static_case(r,META_SCENE_SHADOW,0,false);
    data=full_packet(3);Reader replacement(data);ReadAtlas(replacement);
    assert(atlas_generation==3&&provider.invalidations.size()==6);ClearSections();
    assert(sections.empty()&&atlas_alpha.empty()&&provider.invalidations.size()==7);
    static_case(r,META_SCENE_SHADOW,0,false);
    const auto texture=atlas_texture;reset(r);glDeleteTextures(1,&texture);
}
void selective_atlas_shadow_tests(Resources& r){
    reset(r);StaticProvider provider;StaticOwner owner;StaticCallback callback;
    render::scene_static_shadows=&provider;render::callback=&owner;
    assert(render::set_static_scene(&callback,true));provider.invalidations.clear();
    atlas_texture=0;atlas_width=atlas_height=0;atlas_alpha.clear();atlas_pixel_tiles.clear();atlas_generation=0;
    const auto full_packet=[](unsigned width,unsigned height,std::uint64_t generation){
        goldcraft::Writer out;out.u64(1);out.u64(generation);out.u32(width);out.u32(height);
        for(unsigned i=0;i<width*height;++i)out.u32(0xffffffff);return out.data;
    };
    const auto patch_packet=[](std::uint64_t generation,std::initializer_list<std::array<unsigned,3>> patches){
        goldcraft::Writer out;out.u64(1);out.u64(generation);out.u32(static_cast<unsigned>(patches.size()));
        for(const auto& p:patches){out.u32(p[0]);out.u32(p[1]);out.u32(1);out.u32(1);out.u32(p[2]);}return out.data;
    };
    const auto section_packet=[&](int key,std::uint64_t revision,unsigned flags,float center,float u,float v){
        goldcraft::Writer out;out.u64(1);out.i32(key);out.i32(0);out.i32(0);out.u64(revision);out.u32(flags);out.u32(4);
        for(const auto& vertex:quad(r,center,.5f)){
            out.f32(vertex.x/32);out.f32(64+vertex.z/32);out.f32(-vertex.y/32);out.f32(u);out.f32(v);out.u32(0xffffffff);
        }
        return out.data;
    };
    const std::array<std::int32_t,4> left_key{0,0,0,0},right_key{1,0,0,2};
    auto bytes=section_packet(0,1,0,-.5f,.125f,.25f);Reader left(bytes);ReadSection(left);
    bytes=section_packet(1,1,2,.5f,.875f,.75f);Reader right(bytes);ReadSection(right);
    bytes=section_packet(2,1,1,0,.5f,.5f);Reader translucent(bytes);ReadSection(translucent);
    assert(sections.at(left_key).atlas_footprint_generation==0&&sections.at(right_key).atlas_footprint_generation==0);
    static_case(r,META_SCENE_SHADOW,0,false);
    bytes=full_packet(128,64,2);Reader first_atlas(bytes);ReadAtlas(first_atlas);
    assert(sections.at(left_key).atlas_footprint_generation==2&&sections.at(right_key).atlas_footprint_generation==2);
    static_case(r,META_SCENE_SHADOW,2,true);const auto full_depth=depth_pixels();provider.invalidations.clear();

    // The middle animation is used only by the translucent layer, which is
    // not present in the static shadow callback. Neither opaque quad uses it.
    bytes=patch_packet(2,{{64,32,0x00345678}});Reader unused(bytes);ReadAtlasPatches(unused);
    assert(provider.invalidations.empty()&&static_shadow_alpha_changed_packets==1&&
        static_shadow_alpha_unreferenced_packets==1&&static_shadow_alpha_invalidations==0);
    static_case(r,META_SCENE_SHADOW,2,true);assert(depth_pixels()==full_depth);
    bytes=patch_packet(2,{{16,16,0x00345678}});Reader used(bytes);ReadAtlasPatches(used);
    assert(provider.invalidations.size()==1&&!provider.invalidations[0].all&&provider.invalidations[0].max[0]<0);
    assert(static_shadow_alpha_affected_sections==1&&static_shadow_alpha_invalidations==1&&static_shadow_alpha_fallbacks==0);
    static_case(r,META_SCENE_SHADOW,2,true);assert(depth_pixels()!=full_depth);
    const auto before_rgb=provider.invalidations.size();
    bytes=patch_packet(2,{{112,48,0xff345678}});Reader rgb(bytes);ReadAtlasPatches(rgb);
    assert(provider.invalidations.size()==before_rgb&&static_shadow_alpha_changed_packets==2);

    // Overlap is processed in wire order after conservative detection against
    // the old accepted state: erase then restore may invalidate once but must
    // leave both CPU alpha and actual GPU depth at their final values.
    static_case(r,META_SCENE_SHADOW,2,true);const auto before_overlap=depth_pixels();provider.invalidations.clear();
    bytes=patch_packet(2,{{112,48,0x00345678},{112,48,0xff123456}});Reader overlap(bytes);ReadAtlasPatches(overlap);
    assert(provider.invalidations.size()==1&&provider.invalidations[0].min[0]>0&&atlas_alpha[48*128+112]==255);
    static_case(r,META_SCENE_SHADOW,2,true);assert(depth_pixels()==before_overlap);
    const auto accepted=atlas_alpha;bytes.push_back(0);bool rejected=false;
    try{Reader bad(bytes);ReadAtlasPatches(bad);}catch(const ProtocolError&){rejected=true;}
    assert(rejected&&atlas_alpha==accepted&&provider.invalidations.size()==1);
    bytes=section_packet(1,2,2,.5f,.125f,.25f);bytes.pop_back();rejected=false;
    try{Reader bad_mesh(bytes);ReadSection(bad_mesh);}catch(const ProtocolError&){rejected=true;}
    assert(rejected&&sections.at(right_key).revision==1&&sections.at(right_key).atlas_footprint_generation==2);

    // Removal drops its UV dependency. A later replacement commits its new UV
    // footprint with its accepted revision, preserving malformed-frame rollback.
    goldcraft::Writer remove;remove.u64(1);remove.i32(0);remove.i32(0);remove.i32(0);
    Reader removal(remove.data);ReadRemoveSection(removal);provider.invalidations.clear();
    bytes=patch_packet(2,{{16,16,0xff111111}});Reader removed(bytes);ReadAtlasPatches(removed);
    assert(provider.invalidations.empty());
    bytes=section_packet(1,2,2,.5f,.125f,.25f);Reader replacement(bytes);ReadSection(replacement);provider.invalidations.clear();
    bytes=patch_packet(2,{{112,48,0x00123456}});Reader old_uv(bytes);ReadAtlasPatches(old_uv);
    assert(provider.invalidations.empty());
    bytes=patch_packet(2,{{16,16,0x00111111}});Reader new_uv(bytes);ReadAtlasPatches(new_uv);
    assert(provider.invalidations.size()==1&&provider.invalidations[0].min[0]>0);

    // Full replacement changes texel tolerance and rebuilds retained sections.
    bytes=full_packet(16,8,3);Reader resized(bytes);ReadAtlas(resized);
    assert(sections.at(right_key).atlas_footprint_generation==3);provider.invalidations.clear();
    bytes=patch_packet(3,{{12,6,0}});Reader resized_unused(bytes);ReadAtlasPatches(resized_unused);assert(provider.invalidations.empty());
    bytes=patch_packet(3,{{2,2,0}});Reader resized_used(bytes);ReadAtlasPatches(resized_used);
    assert(provider.invalidations.size()==1&&!provider.invalidations[0].all);
    // Unknown dependencies cannot be treated as an empty used-tile set.
    sections.at(right_key).atlas_footprint_generation=0;provider.invalidations.clear();
    bytes=patch_packet(3,{{12,6,0xff000000}});Reader unknown(bytes);ReadAtlasPatches(unknown);
    assert(provider.invalidations.size()==1&&provider.invalidations[0].all&&static_shadow_alpha_fallbacks==1);
    RebuildSectionFootprint(sections.at(right_key));
    bytes=full_packet(1,1,4);Reader tiny(bytes);ReadAtlas(tiny);provider.invalidations.clear();
    bytes=patch_packet(4,{{0,0,0}});Reader tiny_alpha(bytes);ReadAtlasPatches(tiny_alpha);
    assert(provider.invalidations.size()==1&&!provider.invalidations[0].all);
    atlas_pixel_tiles.clear();bytes=patch_packet(4,{{0,0,0xff000000}});rejected=false;
    try{Reader missing(bytes);ReadAtlasPatches(missing);}catch(const ProtocolError&){rejected=true;}
    assert(rejected&&atlas_generation==0&&atlas_alpha.empty()&&provider.invalidations.back().all);
    UpdateStaticScene();assert(!statistics().scene_static_shadows_registered);

    // Recover through a complete atlas, then exercise two disjoint material
    // quads inside one received section. Its UV bounding rectangle would
    // cover the middle animation, but its actual per-quad dependencies do not.
    bytes=full_packet(128,64,5);Reader recovered(bytes);ReadAtlas(recovered);UpdateStaticScene();
    goldcraft::Writer joined;joined.u64(1);joined.i32(1);joined.i32(0);joined.i32(0);
    joined.u64(3);joined.u32(2);joined.u32(8);
    for(unsigned side=0;side<2;++side)for(const auto& vertex:quad(r,side?.5f:-.5f,.5f)){
        joined.f32(vertex.x/32);joined.f32(64+vertex.z/32);joined.f32(-vertex.y/32);
        joined.f32(side?.875f:.125f);joined.f32(side?.75f:.25f);joined.u32(0xffffffff);
    }
    Reader joined_section(joined.data);ReadSection(joined_section);
    assert(sections.at(right_key).vertices.size()==8&&sections.at(right_key).atlas_footprint_generation==5);
    bytes=section_packet(1,2,2,.5f,.5f,.5f);Reader stale(bytes);ReadSection(stale);
    assert(sections.at(right_key).revision==3&&sections.at(right_key).vertices.size()==8);
    static_case(r,META_SCENE_SHADOW,1,true);const auto joined_depth=depth_pixels();provider.invalidations.clear();
    bytes=patch_packet(5,{{64,32,0}});Reader gap(bytes);ReadAtlasPatches(gap);
    assert(provider.invalidations.empty());
    static_case(r,META_SCENE_SHADOW,1,true);assert(depth_pixels()==joined_depth);
    bytes=patch_packet(5,{{16,16,0}});Reader joined_used(bytes);ReadAtlasPatches(joined_used);
    assert(provider.invalidations.size()==1&&!provider.invalidations[0].all&&
        provider.invalidations[0].min[0]<0&&provider.invalidations[0].max[0]>0);
    static_case(r,META_SCENE_SHADOW,1,true);assert(depth_pixels()!=joined_depth);
    const auto texture=atlas_texture;reset(r);glDeleteTextures(1,&texture);
}

void entity_outline_tests(Resources& r){
    reset(r);
    // Real immutable host color/depth attachments and a distinct depth view.
    // Replace this fixture's renderbuffer only for the post-scene callback.
    GLuint depth_texture{},depth_view{};
    glGenTextures(1,&depth_texture);glBindTexture(GL_TEXTURE_2D,depth_texture);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,Size,Size);
    glGenTextures(1,&depth_view);glTextureView(depth_view,GL_TEXTURE_2D,depth_texture,GL_DEPTH24_STENCIL8,0,1,0,1);
    glBindFramebuffer(GL_FRAMEBUFFER,r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth_texture,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    MetaRendererPostSceneFrame frame{};frame.struct_size=sizeof(frame);frame.flags=META_POST_SCENE_GAMMA;
    frame.framebuffer=r.fbo;frame.color_texture=r.colors[0];frame.depth_texture=depth_texture;frame.depth_sample_texture=depth_view;
    frame.width=frame.height=Size;frame.color_format=GL_RGBA8;frame.depth_format=GL_DEPTH24_STENCIL8;frame.samples=1;
    frame.viewport[0]=4;frame.viewport[1]=5;frame.viewport[2]=56;frame.viewport[3]=54;
    frame.near_plane=4;frame.far_plane=2048;
    for(unsigned i=0;i<16;i+=5)frame.world_matrix[i]=frame.projection_matrix[i]=frame.inverse_world_matrix[i]=frame.inverse_projection_matrix[i]=1;
    entity_outline_replay=std::make_unique<render::EntityOutline>();
    auto mask=entity(r,0,entity_outline);for(auto& v:mask.vertices){v.color=0x0057b912;v.overlay=0x4d0000ff;}
    entity_batches.push_back(mask);
    // The same mask cannot enter ordinary opaque/translucent or shadow draws.
    draw_case(r,false,0,0);draw_case(r,true,0,0);draw_case(r,false,META_SCENE_SHADOW,0);
    const auto seed=[&]{
        clear();glViewport(0,0,Size,Size);const float color[]={0,0,0,.5f};glClearBufferfv(GL_COLOR,0,color);
        glClearBufferfi(GL_DEPTH_STENCIL,0,.1f,17);glReadBuffer(GL_COLOR_ATTACHMENT0);r.sentinel();
    };
    const auto state=[](){
        auto s=snapshot();GLint values[4]{};glGetIntegerv(GL_VIEWPORT,values);s.insert(s.end(),values,values+4);
        for(auto p:{GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,GL_READ_BUFFER}){glGetIntegerv(p,values);s.push_back(values[0]);}
        return s;
    };
    seed();const auto before=state();const auto original=pixels();const auto depths=depth_pixels();const auto extra=pixels(1);glReadBuffer(GL_COLOR_ATTACHMENT0);
    DrawEntityOutlines(frame);
    assert(entity_outline_error.empty()&&entity_outline_replay->statistics().frames==1&&state()==before);
    const auto outlined=pixels();assert(outlined!=original&&depth_pixels()==depths&&stencil(32,32)==17);
    assert(pixels(1)==extra);glReadBuffer(GL_COLOR_ATTACHMENT0);
    unsigned colored=0;
    for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){
        const auto i=(y*Size+x)*4;assert(outlined[i+3]==original[i+3]);
        if(x<4||x>=60||y<5||y>=59)for(unsigned c=0;c<4;++c)assert(outlined[i+c]==original[i+c]);
        if(outlined[i+1]){++colored;assert(outlined[i+1]>outlined[i]&&outlined[i+1]>outlined[i+2]);}
    }
    assert(colored>0&&outlined[(32*Size+32)*4+1]==0); // Edges, not a solid silhouette.
    const auto skip=[&]{
        seed();const auto old=pixels();const auto saved=state();const auto draws=statistics().draws,frames=entity_outline_replay->statistics().frames;
        probe::reset();probe::count_allocations=true;DrawEntityOutlines(frame);probe::count_allocations=false;
        assert(probe::queries==0&&probe::bindings==0&&probe::allocations==0&&probe::errors==0);
        assert(statistics().draws==draws&&entity_outline_replay->statistics().frames==frames&&state()==saved&&pixels()==old);++cases;
    };
    link.outlines=false;skip();link.outlines=true;
    link.online=false;skip();link.online=true;
    ++link.current_generation;skip();--link.current_generation;
    world=0;skip();world=1;
    matching_protocol=false;skip();matching_protocol=true;
    fresh=false;skip();fresh=true;
    atlas_texture=0;skip();atlas_texture=r.texture;
    atlas_generation=0;skip();atlas_generation=1;
    const auto context=render::current_context;render::current_context=nullptr;skip();render::current_context=context;
    entity_batches.clear();skip();entity_batches.push_back(entity(r));skip();entity_batches.clear();entity_batches.push_back(mask);
    frame.flags=META_POST_SCENE_LINEAR;entity_outline_gamma=nullptr;skip();
    Cvar gamma;gamma.value=2.2f;entity_outline_gamma=&gamma;
    seed();const auto linear_frames=entity_outline_replay->statistics().frames;DrawEntityOutlines(frame);
    assert(entity_outline_replay->statistics().frames==linear_frames+1&&entity_outline_error.empty());
    frame.flags=META_POST_SCENE_GAMMA;entity_outline_gamma=nullptr;
    // A missing resource aborts the private mask, retaining all host pixels.
    entity_batches.push_back(entity(r,77,entity_outline));seed();const auto old=pixels();const auto saved=state();
    DrawEntityOutlines(frame);assert(!entity_outline_error.empty()&&pixels()==old&&state()==saved);
    entity_batches.pop_back();seed();DrawEntityOutlines(frame);assert(entity_outline_error.empty()&&pixels()!=old);
    // Received empty frame/life reset cannot replay the previous GPU mask.
    entity_batches.clear();skip();entity_outline_replay.reset();
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,r.depth);
    glDeleteTextures(1,&depth_view);glDeleteTextures(1,&depth_texture);glViewport(0,0,Size,Size);
    assert(glGetError()==GL_NO_ERROR);reset(r);
    std::puts("{\"entityOutlineProductionCallbacks\":true,\"realAttachments\":true,\"ordinaryShadowIsolation\":true,\"lifecycleGates\":13,\"failurePreservesHost\":true,\"teamEdgesAndDepth\":true}");
}

void tests(Resources& r){
    using namespace goldcraft;
    cases=empty_cases=0;programs.clear();r.adapter.effects_shader=true;
    scene_ubo_t scene{};scene.r_g=scene.v_gamma=scene.v_texgamma=scene.v_lightgamma=1;scene.r_g3=.125f;
    camera_ubo_t camera{};camera.numViews=1;camera.views[0].vup_zfar[3]=2048;
    for(unsigned i=0;i<4;++i)camera.views[0].worldMatrix[i][i]=camera.views[0].projMatrix[i][i]=1;
    GLuint ubos[2]{};glGenBuffers(2,ubos);
    const void* data[]{&scene,&camera};const GLsizeiptr sizes[]{sizeof(scene),sizeof(camera)};
    for(unsigned i=0;i<2;++i){glBindBuffer(GL_UNIFORM_BUFFER,ubos[i]);glBufferData(GL_UNIFORM_BUFFER,sizes[i],data[i],GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,i,ubos[i]);}
    static_shadow_tests(r);
    atlas_shadow_tests(r);
    selective_atlas_shadow_tests(r);
    entity_outline_tests(r);

    // Bounds are published with the actual received section revision. A
    // failed replacement cannot expose candidate bounds over the old mesh.
    reset(r);
    StaticProvider mutation_provider;StaticOwner mutation_owner;StaticCallback mutation_callback;
    render::scene_static_shadows=&mutation_provider;render::callback=&mutation_owner;
    assert(render::set_static_scene(&mutation_callback,true));mutation_provider.invalidations.clear();
    const auto section_packet=[](std::uint64_t revision,float x) {
        Writer out;out.u64(1);out.i32(0);out.i32(0);out.i32(0);out.u64(revision);out.u32(0);out.u32(4);
        for(unsigned i=0;i<4;++i){out.f32(x);out.f32(float(i));out.f32(-float(i));out.f32(0);out.f32(0);out.u32(0xffffffff);}
        return out.data;
    };
    const std::array<std::int32_t,4> section_key{};
    auto received=section_packet(1,2);Reader first(received);ReadSection(first);
    assert(sections.at(section_key).bounds.valid()&&sections.at(section_key).bounds.min[0]==64);
    assert(mutation_provider.invalidations.size()==1&&mutation_provider.invalidations[0].min[0]==64);
    const auto original_mesh=sections.at(section_key).gpu;
    received=section_packet(2,-3);Reader replaced(received);ReadSection(replaced);
    assert(sections.at(section_key).bounds.min[0]==-96&&sections.at(section_key).bounds.max[0]==-96&&sections.at(section_key).gpu==original_mesh);
    assert(mutation_provider.invalidations.size()==3&&mutation_provider.invalidations[1].min[0]==64&&mutation_provider.invalidations[2].min[0]==-96);
    received=section_packet(3,4);received.back()=0;received.pop_back();
    bool malformed=false;try{Reader input(received);ReadSection(input);}catch(const ProtocolError&){malformed=true;}
    assert(malformed&&sections.at(section_key).revision==2&&sections.at(section_key).bounds.min[0]==-96);
    assert(mutation_provider.invalidations.size()==3);
    received=section_packet(3,std::numeric_limits<float>::max());Reader overflow(received);ReadSection(overflow);
    assert(!sections.at(section_key).bounds.valid());
    assert(mutation_provider.invalidations.size()==5&&mutation_provider.invalidations.back().all);
    Writer removal;removal.u64(1);removal.i32(0);removal.i32(0);removal.i32(0);
    auto invalid_removal=removal.data;invalid_removal.push_back(0);malformed=false;
    try{Reader input(invalid_removal);ReadRemoveSection(input);}catch(const ProtocolError&){malformed=true;}
    assert(malformed&&sections.size()==1&&mutation_provider.invalidations.size()==5);
    Reader remove(removal.data);ReadRemoveSection(remove);
    assert(sections.empty()&&mutation_provider.invalidations.size()==6&&mutation_provider.invalidations.back().all);
    reset(r);

    // The provider's production point-light geometry/dispatch contract is
    // tested separately in Renderer. Here an explicit rejection exercises
    // the actual client interface call, lazy Pass and GL-state boundary.
    struct CullProvider final : IMetaRendererSceneCulling {
        unsigned calls=0;bool reject=true;
        bool CullShadowBounds(const float*,const float*) noexcept override {++calls;return reject;}
    } culler;
    reset(r);section(r);entity_batches.push_back(entity(r));render::scene_culling=&culler;
    const auto old_tests=statistics().shadow_bounds_tests,old_culled=statistics().shadow_bounds_culled;
    draw_case(r,false,META_SCENE_SHADOW|META_SCENE_MULTIVIEW,0);
    assert(culler.calls==2&&statistics().shadow_bounds_tests==old_tests+2&&statistics().shadow_bounds_culled==old_culled+2);
    static_case(r,META_SCENE_SHADOW,0,true); // A fully culled valid scene is still complete.
    render::scene_culling=nullptr;
    const auto without_provider=draw_case(r,false,META_SCENE_SHADOW,2);
    culler.reject=false;render::scene_culling=&culler;
    assert(draw_case(r,false,META_SCENE_SHADOW,2)==without_provider);
    culler.reject=true;Cvar disabled;disabled.value=0;render_scene_cull=&disabled;
    const auto calls=culler.calls;
    assert(draw_case(r,false,META_SCENE_SHADOW,2)==without_provider&&culler.calls==calls);
    render_scene_cull=nullptr;
    draw_case(r,false,0,2);assert(culler.calls==calls); // Main scene never uses a shadow volume.
    for(auto& [key,value]:sections)value.bounds={};
    entity_batches.front().bounds.add(std::numeric_limits<float>::infinity(),0,0);
    assert(draw_case(r,false,META_SCENE_SHADOW,2)==without_provider&&culler.calls==calls);
    reset(r);section(r,1);entity_batches.push_back(entity(r,0,1));render::scene_culling=&culler;
    draw_case(r,true,0,2);assert(culler.calls==calls);

    reset(r);atlas_texture=0;draw_case(r,false,0,0);
    reset(r);section(r);link.online=false;draw_case(r,false,0,0);
    reset(r);section(r);world=0;draw_case(r,false,0,0);
    reset(r);
    // Match the measured idle scene: retained empty section records are valid
    // after removals. Empty batches have null Mesh pointers so a no-op draw
    // cannot accidentally conceal the dispatch regression.
    for(int i=0;i<500;++i)sections.emplace(std::array<std::int32_t,4>{i,0,0,0},Section{1,unsigned(i&3),{},nullptr});
    entity_batches.push_back({0,0,{},nullptr});entity_batches.push_back({0,1,{},nullptr});
    crack_batches.push_back({0,0,{},nullptr});
    entity_effect_batches.push_back({{}, {}, nullptr});particle_batches.push_back({{}, {}, nullptr});
    section_logged=true;
    draw_case(r,false,0,0);draw_case(r,true,0,0);
    draw_case(r,false,META_SCENE_GBUFFER,0);draw_case(r,false,META_SCENE_SHADOW,0);
    draw_case(r,false,META_SCENE_SHADOW|META_SCENE_LINEAR_DEPTH,0);
    assert(!draw_logged&&logs==0);

    reset(r);section(r,1);draw_case(r,false,0,0);
    reset(r);entity_batches.push_back(entity(r));fresh=false;draw_case(r,false,0,0);
    reset(r);entity_batches.push_back(entity(r,1));draw_case(r,false,0,0);
    reset(r);entity_batches.push_back(entity(r,0,1));draw_case(r,false,0,0);
    ParticleMaterial material;material.texture=1;
    reset(r);entity_effect_batches.push_back(effect(r,material));draw_case(r,true,0,0);
    reset(r);atlas_texture=0;particle_batches.push_back(effect(r,material));draw_case(r,true,0,0);
    reset(r);outline_vertices=quad(r);control=false;draw_case(r,true,0,0);
    control=true;minecraft_menu=true;draw_case(r,true,0,0);
    reset(r);crack_batches.push_back(entity(r));last_feedback=now-.25;draw_case(r,true,0,0);
    last_feedback=now;feedback_life=2;draw_case(r,true,0,0);
    material.texture=particle_untextured;
    reset(r);particle_batches.push_back(effect(r,material));last_particle_frame=now-.25;draw_case(r,true,0,0);
    last_particle_frame=now;particle_life=2;draw_case(r,true,0,0);
    particle_life=1;server_player_life=2;draw_case(r,true,0,0);
    server_player_life=1;render_particles=&particle_cvar;particle_cvar.value=0;draw_case(r,true,0,0);
    reset(r);entity_effect_batches.push_back(effect(r,material));fresh=false;draw_case(r,true,0,0);
    reset(r);entity_effect_batches.push_back(effect(r,material));particle_batches.push_back(effect(r,material));
    crack_batches.push_back(entity(r));outline_vertices=quad(r);
    draw_case(r,false,META_SCENE_GBUFFER,0);draw_case(r,false,META_SCENE_SHADOW,0);

    // First-draw initialization and emissive -> ordinary entity transition.
    reset(r);section(r,2);section_logged=true;
    assert(color_count(draw_case(r,false,0,1),{255,255,255,255})==1024);
    assert(draw_logged&&logs==1&&stencil(32,32)==STENCIL_MASK_NO_LIGHTING);
    reset(r);section(r,2,-.5f,.5f);entity_batches.push_back(entity(r,0,2,.5f,.5f));
    const auto mixed=draw_case(r,false,0,2);
    assert(color_count(mixed,{255,255,255,255})==512&&color_count(mixed,{140,140,140,255},1)==512);
    assert(stencil(16,32)==STENCIL_MASK_NO_LIGHTING&&stencil(48,32)==0);
    assert(entity_presented_frames==1&&entity_drawn_revision==entity_revision);

    reset(r);entity_textures.emplace(1,Texture{r.hud,1,1});
    entity_batches.push_back({0,0,{},nullptr});entity_batches.push_back(entity(r,2));
    entity_batches.push_back(entity(r,1,2));
    assert(color_count(draw_case(r,false,0,1),{28,56,84,255},1)==1024);
    assert(entity_presented_frames==1);
    draw_case(r,false,0,1);assert(entity_presented_frames==1);

    reset(r);section(r,2,-.5f,.5f);entity_batches.push_back(entity(r,0,2,.5f,.5f));
    draw_case(r,false,META_SCENE_GBUFFER,2);
    const auto light=pixels(1);
    assert(color_count(light,{255,255,255,255})==512&&color_count(light,{140,140,140,255},1)==512);
    assert(entity_presented_frames==0&&stencil(16,32)==STENCIL_MASK_NO_LIGHTING&&stencil(48,32)==0);

    // A first entity uses its own texture, and view-space offset reaches the
    // depth buffer. Shadow dispatch must ignore the same requested offset.
    reset(r);entity_batches.push_back(entity(r,0,6,0,1,.5f));
    draw_case(r,false,0,1);assert(std::abs(depth()-(.5f*.99975586f*.5f+.5f))<1e-6f);
    draw_case(r,false,META_SCENE_SHADOW,1);assert(std::abs(depth()-.75f)<1e-6f);

    // First outline and first crack must initialize the lazy Pass correctly.
    reset(r);const auto q=quad(r);
    for(unsigned i=0;i<4;++i){outline_vertices.push_back(q[i]);outline_vertices.push_back(q[(i+1)%4]);}
    const auto outline=draw_case(r,true,0,1);
    assert(color_count(outline,{255,255,255,0})>0&&feedback_draws==1);
    reset(r);auto crack=entity(r);for(auto& v:crack.vertices)v.color=0xff000040;
    crack_batches.push_back({0,0,{},nullptr});crack_batches.push_back(crack);
    const auto crack_pixels=draw_case(r,true,0,1,{1,1,1,1});
    assert(color_count(crack_pixels,{128,0,0,255},1)==1024&&feedback_draws==1);
    const auto one_crack_uniforms=probe::uniforms;
    auto second=entity(r);for(auto& v:second.vertices)v.color=0xff000040;crack_batches.push_back(second);
    draw_case(r,true,0,2,{1,1,1,1});assert(probe::uniforms==one_crack_uniforms);

    // The ordinary entity leaves view offset enabled. Feedback then entity
    // depth-only effects and particle EQUAL color must form one ordered pass.
    // Actual production shaders make a leaked offset or feedback mode visible.
    reset(r);entity_batches.push_back(entity(r,0,5,0,1,.5f));
    crack_batches.push_back(entity(r));
    auto depth_material=material;depth_material.flags=particle_depth_write|particle_depth_test;
    depth_material.depth_func=GL_LESS;depth_material.color_mask=0;
    auto equal=material;equal.flags=particle_depth_test;equal.depth_func=GL_EQUAL;equal.color_mod={.2f,.4f,.6f,1};
    entity_effect_batches.push_back(effect(r,depth_material));particle_batches.push_back(effect(r,equal));
    assert(color_count(draw_case(r,true,0,4),{51,102,153,255},1)==1024);
    assert(std::abs(depth()-.5f)<1e-6f&&entity_effect_draws==1&&particle_draws==1&&feedback_draws==1);
    std::swap(entity_effect_batches,particle_batches);
    assert(color_count(draw_case(r,true,0,4),{51,102,153,255},1)==0);

    // A valid standalone particle may be the first resource with no atlas.
    reset(r);atlas_texture=0;particle_batches.push_back(effect(r,equal));
    particle_batches.back().material.depth_func=GL_ALWAYS;
    assert(color_count(draw_case(r,true,0,1),{51,102,153,255},1)==1024);
    reset(r);entity_effect_batches.push_back(effect(r,equal));entity_effect_batches.back().material.depth_func=GL_ALWAYS;
    assert(color_count(draw_case(r,true,0,1),{51,102,153,255},1)==1024);

    // A cached shader load failure is detected after real state capture; the early return
    // must still destroy the optional Pass and restore every host state field.
    reset(r);section(r);programs.clear();programs.emplace(0u,render::Program{});
    clear();r.sentinel();const auto saved=snapshot();const auto draws=statistics().draws;
    Draw(false,0);programs.clear();
    assert(snapshot()==saved&&statistics().draws==draws&&frames_drawn==0);
    assert(work[static_cast<unsigned>(WorkStage::pass_construct)]==1&&work[static_cast<unsigned>(WorkStage::pass_restore)]==1);
    assert(color_count(pixels(),{0,0,0,0})==Size*Size&&glGetError()==GL_NO_ERROR);++cases;

    reset(r);outline_gpu.reset();glDeleteBuffers(2,ubos);programs.clear();r.adapter.effects_shader=false;
    const GLenum targets[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};glDrawBuffers(4,targets);
    std::printf("{\"productionPluginDrawGL\":true,\"cases\":%u,\"emptyCases\":%u,\"emptyPasses\":0,\"emptyBackendQueries\":0,\"emptyUploads\":0,\"emptyDraws\":0,\"stateRestored\":true,\"foregroundFPSValidated\":false}\n",cases,empty_cases);
}
} // namespace plugin_draw_probe
