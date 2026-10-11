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
using Vertex = render::Vertex;
enum class WorkStage {draw_transparent,draw_shadow,draw_opaque,pass_construct,pass_restore,
    sections,entities,feedback,entity_effects,particle_draw,count};
std::array<unsigned,static_cast<unsigned>(WorkStage::count)> work{};
struct WorkScope {
    explicit WorkScope(WorkStage stage){++work[static_cast<unsigned>(stage)];}
};
#include "plugin_draw_types.inc"

struct Link {bool online=true;bool connected()const{return online;}} link;
std::uint64_t world=1;
GLuint atlas_texture{};
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
Cvar* render_particles=nullptr;
Cvar* render_scene_cull=nullptr;
GLenum last_gl_error{};
unsigned logs{},cases{},empty_cases{};
double Seconds(){return now;}
bool EntityFrameFresh(){return fresh;}
bool HasControl(){return control;}
void Log(const std::string&){++logs;}
#include "plugin_draw.inc"

void reset(Resources& r){
    sections.clear();entity_textures.clear();particle_textures.clear();entity_batches.clear();
    crack_batches.clear();entity_effect_batches.clear();particle_batches.clear();outline_vertices.clear();
    outline_gpu=std::make_shared<Mesh>();
    link.online=true;world=1;atlas_texture=r.texture;
    fresh=control=true;minecraft_menu=section_logged=draw_logged=false;
    minecraft_life=server_player_life=feedback_life=particle_life=1;
    now=last_feedback=last_particle_frame=10;render_particles=nullptr;particle_cvar.value=1;
    render_scene_cull=nullptr;render::scene_culling=nullptr;
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

void tests(Resources& r){
    using namespace goldcraft;
    cases=empty_cases=0;programs.clear();r.adapter.effects_shader=true;
    scene_ubo_t scene{};scene.r_g=scene.v_gamma=scene.v_texgamma=scene.v_lightgamma=1;scene.r_g3=.125f;
    camera_ubo_t camera{};camera.numViews=1;camera.views[0].vup_zfar[3]=2048;
    for(unsigned i=0;i<4;++i)camera.views[0].worldMatrix[i][i]=camera.views[0].projMatrix[i][i]=1;
    GLuint ubos[2]{};glGenBuffers(2,ubos);
    const void* data[]{&scene,&camera};const GLsizeiptr sizes[]{sizeof(scene),sizeof(camera)};
    for(unsigned i=0;i<2;++i){glBindBuffer(GL_UNIFORM_BUFFER,ubos[i]);glBufferData(GL_UNIFORM_BUFFER,sizes[i],data[i],GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,i,ubos[i]);}

    // Bounds are published with the actual received section revision. A
    // failed replacement cannot expose candidate bounds over the old mesh.
    reset(r);
    const auto section_packet=[](std::uint64_t revision,float x) {
        Writer out;out.u64(1);out.i32(0);out.i32(0);out.i32(0);out.u64(revision);out.u32(0);out.u32(4);
        for(unsigned i=0;i<4;++i){out.f32(x);out.f32(float(i));out.f32(-float(i));out.f32(0);out.f32(0);out.u32(0xffffffff);}
        return out.data;
    };
    const std::array<std::int32_t,4> section_key{};
    auto received=section_packet(1,2);Reader first(received);ReadSection(first);
    assert(sections.at(section_key).bounds.valid()&&sections.at(section_key).bounds.min[0]==64);
    const auto original_mesh=sections.at(section_key).gpu;
    received=section_packet(2,-3);Reader replaced(received);ReadSection(replaced);
    assert(sections.at(section_key).bounds.min[0]==-96&&sections.at(section_key).bounds.max[0]==-96&&sections.at(section_key).gpu==original_mesh);
    received=section_packet(3,4);received.back()=0;received.pop_back();
    bool malformed=false;try{Reader input(received);ReadSection(input);}catch(const ProtocolError&){malformed=true;}
    assert(malformed&&sections.at(section_key).revision==2&&sections.at(section_key).bounds.min[0]==-96);
    received=section_packet(3,std::numeric_limits<float>::max());Reader overflow(received);ReadSection(overflow);
    assert(!sections.at(section_key).bounds.valid());
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
