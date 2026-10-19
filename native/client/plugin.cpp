#include "render_backend.hpp"
#include "post_effect_replay.hpp"
#include "entity_outline.hpp"
#include "native_outline.hpp"
#include "host_ui.hpp"
#include "host_text.hpp"
#include "work_history.hpp"
#include "async_diagnostics.hpp"
#include "steam_async.hpp"
#include "server_hud.hpp"
#include "hud_sprites.hpp"
#include "precache_client.hpp"
#include "visible_entities.hpp"
#include "packet_entities.hpp"
#include "userinfo_client.hpp"
#include "spectator_menu.hpp"
#include "hud_clock.hpp"
#include "goldcraft/packet_entities.hpp"
#include "vitals.hpp"
#include "status_motion.hpp"
#include "goldcraft/status_motion.hpp"
#include "goldcraft/native_water.hpp"
#include "goldcraft/client_features.hpp"
#include "goldcraft/status_vision.hpp"
#include "goldcraft/status_nausea.hpp"
#include "goldcraft/view_distortion.hpp"
#include "input_audit.hpp"
#include "goldcraft/host_key_events.hpp"
#include "map_collision.hpp"
#include <metahook.h>
#include "client_initialize.hpp"
#include <cl_entity.h>
#include <usercmd.h>
#include <ref_params.h>
#include <r_efx.h>
#include <dlight.h>
#include <demo_api.h>
#include <net_api.h>
#include <cvardef.h>
#include <keydefs.h>
#include <in_buttons.h>
#include <pm_defs.h>
#include <pm_movevars.h>
#include <gl/GL.h>
#include "goldcraft/endpoint.hpp"
#include "local_discovery.hpp"
#include "goldcraft/fluid_volumes.hpp"
#include "goldcraft/atlas_footprint.hpp"
#include "goldcraft/control_diagnostics.hpp"
#include "goldcraft/transport_diagnostics.hpp"
#include "goldcraft/view_interpolator.hpp"
#include "goldcraft/avatar.hpp"
#include "goldcraft/entity_frame.hpp"
#include "goldcraft/hud.hpp"
#include "goldcraft/particles.hpp"
#include "goldcraft/particle_texture_chunks.hpp"
#include "goldcraft/form_menu.hpp"
#include "goldcraft/block_feedback.hpp"
#include "goldcraft/camera.hpp"
#include "goldcraft/host_keys.hpp"
#include "goldcraft/map_edits.hpp"
#include "map_edit_transaction.hpp"
#include <array>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <memory>
#include <filesystem>

cl_enginefunc_t gEngfuncs{};
cl_exportfuncs_t gExportfuncs{};
namespace {
using namespace goldcraft;
Endpoint link;
local_discovery::Publisher discovery;
metahook_api_t* api = nullptr;
Bytes binding;
status_motion::Receiver native_motion;
native_water::Receiver water_state;
status_vision::Receiver native_vision;
status_nausea::Receiver native_nausea;
status_glow::Receiver native_glow;
bool native_outline_ready=false;
view_distortion::Receiver view_distortion_frames;
client_map::EditTransaction map_edits;
edits::Assembler edit_assembler;
double next_edit_query=0;
bool edit_recovery=false;
std::uint64_t world = 0, last_link_generation = 0;
std::uint32_t player_slot=0,player_serial=0;
struct NativeCollider {int slot;float min[3],max[3];};
std::map<std::uint64_t,NativeCollider> native_colliders;
fluids::Receiver native_fluids;
std::uint64_t collider_sequence=0,prediction_frames=0;
double last_colliders=0;
unsigned prediction_objects=0,prediction_overflow=0;
std::string last_error;
diagnostics::Dispatcher diagnostic_io;
GLuint atlas_texture = 0;
unsigned atlas_width=0,atlas_height=0;
std::vector<std::uint8_t> atlas_alpha;
AtlasPixelTiles atlas_pixel_tiles;
std::uint64_t atlas_generation=0,atlas_uploads=0,atlas_animation_packets=0,atlas_patch_bytes=0;
double last_atlas_animation=0,atlas_animation_interval_ms=0;
bool section_logged=false,draw_logged=false,atlas_logged=false;
std::uint64_t frames_drawn=0;
GLenum last_gl_error=0;
std::chrono::steady_clock::time_point next_diagnostic{};
Vec3 observed_origin{},observed_angles{};
std::array<float, 3> native_view_origin{};
bool have_view=false;
std::uint64_t input_sequence=0,pose_sequence=0;
std::uint32_t minecraft_life=0;
std::uint32_t server_player_life=0;
bool server_minecraft_form=false;
double minecraft_transition_until=0;
std::chrono::steady_clock::time_point last_pose{};
Vec3 minecraft_feet{};
float minecraft_eye=1.62f;
bool minecraft_control=false,minecraft_menu=false;
FormMenu form_menu;
pfnUserMsgHook previous_show_menu=nullptr;
pfnUserMsgHook previous_death_message=nullptr,previous_team_info=nullptr,previous_reset_hud=nullptr;
std::array<bool,256> menu_pressed{};
std::uint64_t form_menu_draws=0,form_menu_selections=0;
ViewInterpolator view_interpolator;
CameraInterpolator camera_interpolator;
CameraPose camera_pose;
std::uint64_t camera_revision=0,camera_frames=0;
unsigned camera_life=0;
double last_camera=0;
cvar_t* view_smoothing=nullptr;
cvar_t* replace_players=nullptr;
cvar_t* minecraft_hud=nullptr;
cvar_t* gui_scale=nullptr;
cvar_t* host_viewmodel=nullptr;
cvar_t* render_particles=nullptr;
cvar_t* render_post_effects=nullptr;
cvar_t* render_scene_cull=nullptr;
cvar_t* render_scene_static_shadows=nullptr;
std::uint64_t static_shadow_callbacks=0,static_shadow_draws=0,static_shadow_section_skips=0,
    static_shadow_failures=0,static_shadow_alpha_invalidations=0;
std::uint64_t static_shadow_alpha_changed_packets=0,static_shadow_alpha_unreferenced_packets=0,
    static_shadow_alpha_affected_sections=0,static_shadow_alpha_fallbacks=0;
bool viewmodel_suppressed=false;
float saved_viewmodel=1;
std::uint64_t suppressed_viewmodel_frames=0;
unsigned hud_flags=0,hud_life=0;
GLuint hud_texture=0;
unsigned hud_width=0,hud_height=0,viewport_width=0,viewport_height=0,viewport_scale=0;
unsigned hud_gui_width=0,hud_gui_height=0;
std::uint64_t viewport_id=0,hud_revision=0,hud_menu_id=0,hud_frames=0,hud_bytes=0,hud_draws=0;
bool hud_blank=false;
std::uint64_t hud_blank_frames=0,hud_blank_uploads=0,hud_blank_composites_skipped=0;
double last_hud=0,next_viewport=0,hud_work_ms=0;
double hud_interval_ms=0;
bool viewport_enabled=true,ui_active=false;
float ui_x=0.5f,ui_y=0.5f,ui_angles[3]{};
unsigned ui_modifiers=0;
int hud_slot=0,hud_food=0,hud_armor=0,hud_level=0;
float hud_health=0,hud_experience=0;
double Seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
enum class WorkStage : std::size_t {
    frame,native_hud,audit_commands,transport,map_edits,connection,diagnostics,messages,
    atlas,atlas_patches,entity_texture,entity_frame,particle_texture,particle_texture_chunk,
    particles,post_plan,feedback_message,camera,hud_upload,scene_reset,pose,lights_message,
    section,remove_section,other_message,control_lights,
    draw_opaque,draw_shadow,draw_transparent,pass_construct,pass_restore,
    sections,entities,feedback,entity_effects,particle_draw,post_mask,post_replay,hud_redraw,
    native_hud_redraw,mc_hud_composite,server_hud_overlay,
    chunk_first_append,chunk_continuation_append,chunk_gpu_upload,chunk_publish_release,chunk_failed_release,
    vid_init,vid_cleanup,native_vid_init,render_initialize,post_initialize,create_move,ui_update,
    mouse_capture,native_create_move,input_forward,frame_gap,count
};
constexpr std::array<const char*,static_cast<std::size_t>(WorkStage::count)> work_names{
    "frame","nativeHudFrame","auditCommands","transportPoll","mapEdits","connectionState","diagnosticWrite","messages",
    "atlas","atlasPatches","entityTexture","entityFrame","particleTexture","particleTextureChunk",
    "particles","postPlan","feedbackMessage","camera","hudUpload","sceneReset","pose","lightsMessage",
    "section","removeSection","otherMessage","controlLights",
    "drawOpaque","drawShadow","drawTransparent","passConstruct","passRestore",
    "sections","entities","feedback","entityEffects","particleDraw","postMask","postReplay","hudRedraw",
    "nativeHudRedraw","mcHudComposite","serverHudOverlay",
    "chunkFirstAppend","chunkContinuationAppend","chunkGpuUpload","chunkPublishRelease","chunkFailedRelease",
    "vidInit","vidCleanup","nativeVidInit","renderInitialize","postInitialize","createMove","uiUpdate",
    "mouseCapture","nativeCreateMove","inputForward","frameGap"
};
struct WorkCounter {std::uint64_t count=0;double total_ms=0,max_ms=0;};
std::array<WorkCounter,work_names.size()> work_counters{};
timing::History<work_names.size()> work_history;
double last_frame_start=0;
double work_window_start=0;
std::uint64_t work_window=0;
std::string work_snapshot="{\"schema\":1,\"ready\":false}";
void RecordWork(WorkStage stage,double started) noexcept {
    const double finished=Seconds();
    const double elapsed=std::max(0.0,(finished-started)*1000);
    auto& counter=work_counters[static_cast<std::size_t>(stage)];
    ++counter.count;counter.total_ms+=elapsed;counter.max_ms=std::max(counter.max_ms,elapsed);
    work_history.record(static_cast<std::size_t>(stage),elapsed,finished);
}
class WorkScope {
public:
    explicit WorkScope(WorkStage stage) noexcept:stage_(stage),started_(Seconds()) {
        if(!work_window_start)work_window_start=started_;
        if(stage==WorkStage::frame){
            if(last_frame_start)work_history.record(static_cast<std::size_t>(WorkStage::frame_gap),
                std::max(0.0,(started_-last_frame_start)*1000),started_);
            last_frame_start=started_;
        }
    }
    ~WorkScope(){RecordWork(stage_,started_);}
    WorkScope(const WorkScope&)=delete;
    WorkScope& operator=(const WorkScope&)=delete;
private:
    WorkStage stage_;double started_;
};
// Main-thread elapsed timings include driver/IO waits. Nested scopes are inclusive;
// no CPU/GPU synchronization or per-vertex clock reads are introduced.
void WriteWorkDiagnostics(std::ostream& out) {
    const double now=Seconds();
    if(work_window_start&&now-work_window_start>=2){
        std::ostringstream snapshot;
        snapshot<<"{\"schema\":1,\"ready\":true,\"window\":"<<++work_window
            <<",\"seconds\":"<<now-work_window_start
            <<",\"clock\":\"steady_clock\",\"inclusive\":true,\"stages\":{";
        for(std::size_t i=0;i<work_counters.size();++i){
            if(i)snapshot<<',';
            const auto& value=work_counters[i];
            snapshot<<'"'<<work_names[i]<<"\":{\"count\":"<<value.count
                <<",\"meanMs\":"<<(value.count?value.total_ms/value.count:0)
                <<",\"maxMs\":"<<value.max_ms<<",\"totalMs\":"<<value.total_ms<<'}';
        }
        snapshot<<"}}";work_snapshot=snapshot.str();
        work_counters={};work_window_start=now;
    }
    out<<"\"nativeWork\":"<<work_snapshot;
    out<<",\"nativeWorkHistory\":";work_history.write(out,work_names);
    out<<",\"hostUiWork\":";host_ui::write_work_diagnostics(out);
    out<<",\"hostText\":";host_text::diagnostics(out);
    out<<",\"steamAsync\":";steam_async::diagnostics(out);
    const auto& gl=render::statistics();
    out<<",\"glLoader\":{\"calls\":"<<gl.gl_loader_calls<<",\"reuses\":"<<gl.gl_loader_reuses<<'}';
}
WorkStage MessageWork(Type type) noexcept {
    switch(type){
    case Type::atlas:return WorkStage::atlas;
    case Type::atlas_patches:return WorkStage::atlas_patches;
    case Type::entity_texture:return WorkStage::entity_texture;
    case Type::entity_frame:case Type::entity_outline_frame:return WorkStage::entity_frame;
    case Type::particle_texture:return WorkStage::particle_texture;
    case Type::particle_texture_chunk:return WorkStage::particle_texture_chunk;
    case Type::particle_mesh:return WorkStage::particles;
    case Type::post_effect_plan:return WorkStage::post_plan;
    case Type::block_feedback:return WorkStage::feedback_message;
    case Type::camera:return WorkStage::camera;
    case Type::hud_frame:return WorkStage::hud_upload;
    case Type::scene_reset:return WorkStage::scene_reset;
    case Type::player_pose:return WorkStage::pose;
    case Type::lights:return WorkStage::lights_message;
    case Type::section_mesh:return WorkStage::section;
    case Type::remove_section:return WorkStage::remove_section;
    default:return WorkStage::other_message;
    }
}
class ProfiledPass {
public:
    ProfiledPass(bool transparent,std::uint32_t flags,const MetaRendererPostSceneFrame* mask=nullptr)
        :transparent_(transparent),flags_(flags),mask_(mask){}
    ~ProfiledPass(){if(pass_){WorkScope timing(WorkStage::pass_restore);pass_.reset();}}
    render::Pass& get(){
        if(!pass_){WorkScope timing(WorkStage::pass_construct);pass_.emplace(transparent_,flags_,mask_);}
        return *pass_;
    }
    render::Pass* existing(){return pass_?&*pass_:nullptr;}
private:
    bool transparent_;std::uint32_t flags_;const MetaRendererPostSceneFrame* mask_;
    std::optional<render::Pass> pass_;
};
struct FramerateBenchmark {
    bool active=false;float fps_max=0,fps_override=0,vsync=0;
    std::uint64_t epoch=0,generation=0;unsigned short port=0;
    double expires=0;
} framerate_benchmark;
void RestoreBenchmarkFramerate();
std::ofstream motion_capture;
std::ofstream entity_capture;
double entity_capture_start=0,entity_capture_end=0;
double motion_end=0,motion_start=0,last_view_time=0,frame_work_ms=0,frame_work_max_ms=0,view_frame_max_ms=0;
unsigned last_input_buttons=0;
unsigned input_buttons_observed=0;
std::uint64_t key_events=0;
std::uint64_t forwarded_key_events=0,key_sequence=0;
std::array<bool,256> forwarded_keys{};
int last_key=0,last_key_down=0;
bool last_input_active=false;
bool window_focused=false;
unsigned host_use_sources=0;
bool host_use_sent=false;
std::uint64_t host_use_world=0,host_use_messages=0;
std::uint32_t host_use_life=0;
double next_host_use=0;
float last_input_forward=0,last_input_side=0;
std::uint64_t test_command_id=0;
struct NativeProfileFrame {
    double elapsed, cpu;
    float clientTime;
    Vec3 origin, angles;
    bool focused, playback;
};
std::vector<NativeProfileFrame> native_profile;
double native_profile_start=0,native_profile_end=0,native_profile_cpu=0;
int NativeMouseLookState() {
    return input_audit::mouse_look_state();
}
double NativeThreadCpuMs() {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return -1;
    const auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return double(ticks(kernel)+ticks(user))/10000.0;
}
void NativeProfileFrameEnd(float clientTime) {
    if(!native_profile_end)return;
    const double now=Seconds(),cpu=NativeThreadCpuMs();
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    native_profile.push_back({(now-native_profile_start)*1000,cpu<0?-1:cpu-native_profile_cpu,
        clientTime,observed_origin,observed_angles,foreground==GetCurrentProcessId(),
        gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback()!=0});
    if(now<native_profile_end&&native_profile.size()<8192)return;
    native_profile_end=0;
    if(const char* path=std::getenv("GOLDCRAFT_MOTION_CAPTURE")) {
        std::ofstream out(std::filesystem::path(path).parent_path()/"native-frame-profile.json");
        out<<"{\"schema\":1,\"scope\":\"Main-thread CPU and wall time between HUD callbacks, inclusive of native engine and plugins\",\"frames\":[";
        bool first=true;
        for(const auto& f:native_profile){
            if(!first)out<<',';first=false;
            out<<"{\"time\":"<<f.elapsed<<",\"cpu\":"<<f.cpu<<",\"clientTime\":"<<f.clientTime
               <<",\"focused\":"<<(f.focused?1:0)<<",\"playback\":"<<(f.playback?1:0)
               <<",\"origin\":["<<f.origin.x<<','<<f.origin.y<<','<<f.origin.z
               <<"],\"angles\":["<<f.angles.x<<','<<f.angles.y<<','<<f.angles.z<<"]}";
        }
        out<<"]}";
    }
}
bool capture_requested=false;
std::vector<SceneLight> lamps;
bool lamps_dirty=false,lamps_published=false;
std::chrono::steady_clock::time_point last_lamps{};
bool FreshLights(){
    return world&&link.connected()&&link.generation()==last_link_generation
        &&std::chrono::steady_clock::now()-last_lamps<std::chrono::milliseconds(500);
}
void ClearLights(){
    lamps.clear();lamps_dirty=false;lamps_published=false;last_lamps={};render::clear_scene_lights();
}
void ReadLights(Reader& r){
    const auto epoch=r.u64();if(!world||epoch!=world)return;
    auto next=read_scene_lights(r);
    if(next!=lamps){lamps=std::move(next);lamps_dirty=true;}
    last_lamps=std::chrono::steady_clock::now();
}
void UpdateLights(){
    const bool fresh_lamps=FreshLights();
    if(render::statistics().scene_lights_api){
        if(!fresh_lamps){
            if(lamps_published)render::clear_scene_lights();
            lamps_published=false;
        }else if(lamps_dirty||!lamps_published){
            if(render::update_scene_lights(lamps)){lamps_dirty=false;lamps_published=true;}
        }
    }else{
    // Compatibility with older Renderer builds; diagnostics expose this limited
    // fallback. The current provider keeps MC lights out of the CS dlight pool.
    const auto count=std::min<std::size_t>(12,lamps.size());
    for(unsigned i=0;i<12;++i)render::set_light_shadow(0x47430000+i,fresh_lamps&&i<count?256:0);
    if(gEngfuncs.pEfxAPI&&gEngfuncs.pEfxAPI->CL_AllocDlight&&fresh_lamps) {
        for(unsigned i=0;i<count;++i) {
            auto* light=gEngfuncs.pEfxAPI->CL_AllocDlight(0x47430000+i);if(!light)continue;const auto& source=lamps[i];
            light->origin[0]=source.origin[0];light->origin[1]=source.origin[1];light->origin[2]=source.origin[2];
            light->radius=source.radius;light->color.r=source.color&255;light->color.g=(source.color>>8)&255;light->color.b=(source.color>>16)&255;
            light->die=gEngfuncs.GetClientTime()+0.2f;light->decay=0;light->minlight=0;light->dark=0;
        }
    }
    }
}
using Vertex=render::Vertex;
struct Section { std::uint64_t revision; std::uint32_t flags; std::vector<Vertex> vertices; std::shared_ptr<render::Mesh> gpu=std::make_shared<render::Mesh>(); RenderBounds bounds; AtlasFootprint atlas_footprint; std::uint64_t atlas_footprint_generation=0; };
std::map<std::array<std::int32_t,4>,Section> sections;
void ClearSections(){
    render::invalidate_static_scene();sections.clear();atlas_alpha.clear();atlas_pixel_tiles.clear();
}
struct Texture {GLuint id;unsigned width,height;};
struct EntityBatch {unsigned texture,flags;std::vector<Vertex> vertices;std::shared_ptr<render::Mesh> gpu=std::make_shared<render::Mesh>();RenderBounds bounds;};
std::map<unsigned,Texture> entity_textures;
std::vector<EntityBatch> entity_batches;
std::vector<Vertex> outline_vertices;
std::shared_ptr<render::Mesh> outline_gpu=std::make_shared<render::Mesh>();
std::vector<EntityBatch> crack_batches;
std::uint64_t feedback_revision=0,feedback_frames=0,feedback_draws=0;
unsigned feedback_life=0;
double last_feedback=0;
std::map<unsigned,Texture> particle_textures;
ParticleTextureAssembler particle_texture_upload;
std::uint64_t particle_texture_transfer=0,particle_texture_chunk_uploads=0;
struct EffectBatch {ParticleMaterial material;std::vector<Vertex> vertices;std::shared_ptr<render::Mesh> gpu;};
std::vector<EffectBatch> particle_batches;
std::vector<EffectBatch> entity_effect_batches;
std::uint64_t entity_effect_generation=0,entity_effect_draws=0;
unsigned entity_life=0,entity_effect_vertices=0;
std::uint64_t particle_generation=0,particle_revision=0,particle_frames=0,particle_draws=0,particle_bytes=0;
unsigned particle_count=0,particle_vertices=0,particle_life=0;
double last_particle_frame=0,particle_interval_ms=0;
std::vector<EffectBatch> post_batches;
std::vector<PostEffectStage> post_stages;
std::array<bool,max_particle_batches> post_depth_resets{};
std::unique_ptr<render::PostEffectReplay> post_replay;
std::unique_ptr<render::EntityOutline> entity_outline_replay;
cvar_t* entity_outline_gamma=nullptr;
std::string entity_outline_error;
IMetaRendererPostScene* post_provider=nullptr;
std::uint64_t post_revision=0,post_generation=0,post_frames=0,post_draws=0,post_failures=0;
std::uint64_t post_callbacks=0,post_nonempty_frames=0;
unsigned post_life=0;
double last_post_frame=0;
std::string post_error;
std::uint64_t entity_revision=0,entity_frames=0,entity_presented_frames=0,entity_drawn_revision=0,entity_texture_uploads=0,entity_texture_bytes=0;
double entity_interval_ms=0;
std::uint64_t pending_scene_meshes=0;
double next_scene_request=0;
unsigned entity_count=0,block_entity_count=0;
std::vector<PlayerIdentity> rendered_players;
std::array<PlayerPresentation,65> player_presentations{};
std::array<double,65> presentation_times{};
double last_entity_frame=0;
std::uint64_t suppressed_models=0;
std::array<std::pair<std::uint64_t,std::chrono::steady_clock::time_point>,256> input_times;
float input_latency_ms=0;
void Draw(bool transparent,std::uint32_t flags=0);
bool DrawStaticShadow(std::uint32_t flags) noexcept;
int KeyEvent(int down,int key,const char* binding_text);
void ReleaseMinecraftKeys();
void DrawPostEffects(const MetaRendererPostSceneFrame& frame) noexcept;
void DrawEntityOutlines(const MetaRendererPostSceneFrame& frame) noexcept;
class PostSceneCallbacks final : public IMetaRendererPostSceneCallbacks {
public:
    void DrawPostScene(const MetaRendererPostSceneFrame& frame) noexcept override {DrawEntityOutlines(frame);DrawPostEffects(frame);}
} post_scene_callbacks;
class SceneCallbacks final : public IMetaRendererSceneCallbacks {
public:
    void DrawOpaqueScene(std::uint32_t flags) override {Draw(false,flags);}
} scene_callbacks;
class StaticSceneCallbacks final : public IMetaRendererStaticSceneCallbacks {
public:
    bool DrawStaticShadow(std::uint32_t flags) noexcept override {return ::DrawStaticShadow(flags);}
} static_scene_callbacks;
void UpdateStaticScene(){
    const bool ready=(!render_scene_static_shadows||render_scene_static_shadows->value!=0)&&
        link.connected()&&link.generation()==last_link_generation&&world&&atlas_texture&&atlas_generation&&
        atlas_alpha.size()==std::size_t(atlas_width)*atlas_height;
    render::set_static_scene(&static_scene_callbacks,ready);
}

void PublishClientFeatures() {
    using namespace client_features;
    const unsigned bits = nausea | vision | fluid | edit_batch |
        (client_motion::ready() ? motion | water : 0u) |
        (native_outline_ready ? outline : 0u);
    if (gEngfuncs.Cvar_SetValue && gEngfuncs.pfnGetCvarPointer(key))
        gEngfuncs.Cvar_SetValue(key, static_cast<float>(bits));
}
void ClearPostEffects(){
    post_batches.clear();post_stages.clear();post_revision=post_generation=0;
    post_depth_resets.fill(false);
    post_life=0;last_post_frame=0;
}
void ClearEntityFrame(){
    entity_batches.clear();entity_effect_batches.clear();rendered_players.clear();
    entity_revision=entity_drawn_revision=entity_effect_generation=0;
    entity_count=block_entity_count=entity_life=entity_effect_vertices=0;last_entity_frame=0;
}
bool EntityFrameFresh(){
    return fresh_entity_frame(entity_revision,entity_life,entity_effect_generation,particle_generation,
        minecraft_life,server_player_life,Seconds()-last_entity_frame);
}
void StopPostScene(){
    native_outline_ready=false;native_glow.clear();
    if(gEngfuncs.Cvar_SetValue)gEngfuncs.Cvar_SetValue(status_glow::client_capability,0);
    PublishClientFeatures();
    if(post_provider)post_provider->UnregisterPostSceneCallbacks(&post_scene_callbacks);
    post_provider=nullptr;entity_outline_replay.reset();entity_outline_gamma=nullptr;entity_outline_error.clear();post_replay.reset();ClearPostEffects();
}
void ClearParticles(bool delete_textures=true){
    ClearPostEffects();
    ClearEntityFrame();
    particle_texture_upload.reset();particle_texture_transfer=0;
    if(delete_textures&&render::owns_context())for(auto& [key,texture]:particle_textures)glDeleteTextures(1,&texture.id);
    particle_textures.clear();particle_batches.clear();particle_generation=particle_revision=0;
    particle_count=particle_vertices=particle_life=0;last_particle_frame=0;
}
void ClearDynamic(bool delete_textures=true) {
    ClearParticles(delete_textures);
    outline_vertices.clear();crack_batches.clear();outline_gpu=std::make_shared<render::Mesh>();
    feedback_revision=feedback_life=0;last_feedback=0;
    camera_interpolator.clear();camera_revision=camera_life=0;last_camera=0;view_distortion_frames.clear();
    if(delete_textures&&render::owns_context())for(auto& [key,texture]:entity_textures)glDeleteTextures(1,&texture.id);
    entity_textures.clear();
}

void Log(const std::string& text) {
    diagnostic_io.log("[GoldCraft] "+text+"\n");
}
void RestoreViewModel(){
    if(viewmodel_suppressed&&host_viewmodel)gEngfuncs.Cvar_SetValue("r_drawviewmodel",saved_viewmodel);
    viewmodel_suppressed=false;
}
void ReleaseHostUse(){
    if(host_use_sent&&gEngfuncs.pfnServerCmd){
        auto command="goldcraft_use "+std::to_string(host_use_world)+" "+std::to_string(host_use_life)+" 0\n";
        gEngfuncs.pfnServerCmd(command.data());++host_use_messages;
    }
    host_use_sources=0;host_use_sent=false;host_use_world=0;host_use_life=0;next_host_use=0;
}
void ResetWorld() {
    native_glow.clear();
    RestoreBenchmarkFramerate();
    ReleaseMinecraftKeys();
    form_menu.clear();menu_pressed.fill(false);server_hud::reset();
    ReleaseHostUse();
    RestoreViewModel();
    ClearSections();ClearLights();ClearDynamic();player_presentations={};presentation_times={};view_interpolator.clear();
    native_colliders.clear();collider_sequence=0;last_colliders=0;native_fluids.clear();native_motion.clear();water_state.clear();native_vision.clear();native_nausea.clear();view_distortion_frames.clear();
    map_edits.reset(0);edit_assembler.reset(0);client_map::reset();next_edit_query=0;edit_recovery=false;
    world=0;atlas_generation=0;binding.clear();minecraft_control=minecraft_menu=server_minecraft_form=false;minecraft_transition_until=0;input_sequence=pose_sequence=0;player_slot=player_serial=minecraft_life=server_player_life=0;
    if(hud_texture&&render::owns_context())glDeleteTextures(1,&hud_texture);
    hud_texture=0;hud_width=hud_height=0;hud_revision=hud_menu_id=0;last_hud=next_viewport=0;ui_active=false;hud_blank=false;
}
bool HasControl(){return server_minecraft_form&&minecraft_life==server_player_life&&minecraft_control&&link.connected()&&std::chrono::steady_clock::now()-last_pose<std::chrono::milliseconds(350);}
bool MinecraftFormWaiting(){
    // Fresh pose ownership and the server's selected form are different state.
    // A renderer stall stops controls, but cannot turn the CS weapon back on.
    return world&&server_player_life&&server_minecraft_form&&client_vitals::alive();
}
ControlTrace control_trace;
ControlSample SampleControl(){
    const auto now=std::chrono::steady_clock::now();ControlSample s;
    s.seconds=Seconds();s.world=world;s.connection=link.generation();s.life=minecraft_life;s.host_life=server_player_life;
    s.blocked=control_blocked(server_minecraft_form,minecraft_life==server_player_life,minecraft_control,link.connected(),now-last_pose<std::chrono::milliseconds(350));
    s.hud_blocked=(link.connected()?0u:1u)|((s.blocked==0||(hud_flags&8)||minecraft_menu)?0u:2u)
        |(hud_texture?0u:4u)|(hud_revision?0u:8u)|((hud_life==minecraft_life&&hud_life==server_player_life)?0u:16u)
        |((!minecraft_hud||minecraft_hud->value!=0)?0u:32u)|((s.seconds-last_hud<.5)?0u:64u);
    s.hud_flags=hud_flags;s.ui_flags=(host_ui::state().game_menu?1u:0u)|(minecraft_menu?2u:0u)|(ui_active?4u:0u)|((gEngfuncs.Con_IsVisible&&gEngfuncs.Con_IsVisible())?8u:0u)|(last_input_active?16u:0u)|(host_ui::state().keyboard?32u:0u);
    s.pose_ms=last_pose.time_since_epoch().count()?std::chrono::duration<double,std::milli>(now-last_pose).count():-1;
    s.hud_ms=last_hud?(s.seconds-last_hud)*1000:-1;s.camera_ms=last_camera?(s.seconds-last_camera)*1000:-1;return s;
}
bool CameraFresh(){return HasControl()&&camera_revision&&camera_life==minecraft_life&&Seconds()-last_camera<0.25;}
void SendKey(unsigned kind,int code,int action,float amount=0){
    Writer w;w.u64(world);w.u64(++key_sequence);w.u32(minecraft_life);w.u32(kind);w.i32(code);w.i32(action);w.u32(ui_modifiers);w.f32(amount);
    if(link.send(Type::key_input,w.data))++forwarded_key_events;
}
void ReleaseMinecraftKeys(){
    if(std::any_of(forwarded_keys.begin(),forwarded_keys.end(),[](bool value){return value;})&&world&&link.connected())SendKey(0,0,0);
    forwarded_keys.fill(false);ui_modifiers=0;
}
bool ForwardKey(int down,int key){
    if(key<0||key>=256)return false;
    host_keys::modifier(ui_modifiers,key==K_SHIFT?1:key==K_CTRL?2:key==K_ALT?4:0,down!=0);
    if(key==K_MWHEELUP||key==K_MWHEELDOWN){if(down)SendKey(3,0,0,key==K_MWHEELUP?1.0f:-1.0f);return true;}
    const bool mouse=key>=K_MOUSE1&&key<=K_MOUSE5;
    const int code=mouse?key-K_MOUSE1:glfw_key(key);if(code<0)return false;
    const int action=host_keys::action(forwarded_keys,key,down!=0,mouse);
    if(action>=0)SendKey(mouse?2:1,code,action);
    return true;
}
void UpdateHostUse(){
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool allowed=HasControl()&&world&&minecraft_life&&foreground==GetCurrentProcessId()
        &&!minecraft_menu&&!ui_active&&!gEngfuncs.Con_IsVisible();
    if(!allowed||(host_use_sent&&(host_use_world!=world||host_use_life!=minecraft_life))){ReleaseHostUse();return;}
    if(!host_use_sources){if(host_use_sent)ReleaseHostUse();return;}
    if(host_use_sent&&Seconds()<next_host_use)return;
    auto command="goldcraft_use "+std::to_string(world)+" "+std::to_string(minecraft_life)+" 1\n";
    gEngfuncs.pfnServerCmd(command.data());++host_use_messages;
    host_use_sent=true;host_use_world=world;host_use_life=minecraft_life;next_host_use=Seconds()+0.1;
}
void BeginHostUse(){host_use_sources|=1;UpdateHostUse();}
void EndHostUse(){host_use_sources&=~1u;UpdateHostUse();}
bool HudFresh(){return link.connected()&&(HasControl()||(hud_flags&8)||minecraft_menu)&&hud_texture&&hud_revision&&hud_life==minecraft_life&&hud_life==server_player_life&&(!minecraft_hud||minecraft_hud->value!=0)&&Seconds()-last_hud<0.5;}
bool MinecraftPresentationPending(){
    // Presentation only: never reuse a pose/HUD from the previous life or grant
    // input ownership. Death and an explicit native-form update end the hold.
    return MinecraftFormWaiting()
        &&(!minecraft_hud||minecraft_hud->value!=0)&&!(HudFresh()&&!(hud_flags&8));
}
bool ServerMenuInput(){
    // Own the form menu in both presentations. Letting CS select its original
    // copy alone leaves our ShowMenu mirror open when Minecraft takes over.
    return ((HudFresh()&&!(hud_flags&8))||(world&&form_menu.forms()))
        &&!ui_active&&!minecraft_menu&&form_menu.active(Seconds())
        &&!gEngfuncs.Con_IsVisible()&&!host_ui::state().game_menu&&!host_ui::state().keyboard;
}
bool ServerMenuKey(int key,const char* binding){
    return FormMenu::slot_key(key,binding?binding:"")||(key==K_ESCAPE&&form_menu.forms());
}
void ClearHud(){hud_revision=hud_menu_id=0;hud_flags=hud_life=0;last_hud=next_viewport=0;hud_blank=false;RestoreViewModel();}
void UpdateViewModel(){
    if(((HudFresh()&&(hud_flags&4))||MinecraftPresentationPending())&&host_viewmodel){
        if(!viewmodel_suppressed){saved_viewmodel=host_viewmodel->value;viewmodel_suppressed=true;}
        if(host_viewmodel->value!=0)gEngfuncs.Cvar_SetValue("r_drawviewmodel",0);
    }else RestoreViewModel();
}
void SendUi(unsigned type,int code=0,int action=0,float amount=0){
    if(!HudFresh()||!minecraft_menu||!hud_menu_id)return;
    Writer w;w.u64(world);w.u64(hud_menu_id);w.u32(type);w.i32(code);w.i32(action);w.u32(ui_modifiers);
    w.f32(ui_x);w.f32(ui_y);w.f32(amount);link.send(Type::ui_input,w.data);
}
void UpdateUi(){
    const bool active=HudFresh()&&minecraft_menu&&hud_menu_id;
    if(active&&!ui_active){
        gEngfuncs.GetViewAngles(ui_angles);ui_x=ui_y=0.5f;
        gEngfuncs.pfnClientCmd(const_cast<char*>("-forward\n-back\n-moveleft\n-moveright\n-jump\n-duck\n-speed\n-attack\n-attack2\n"));
        SendUi(1);
    }
    ui_active=active;
}
void SendViewport(){
    if(!world||!link.connected())return;
    SCREENINFO info{};info.iSize=sizeof(info);gEngfuncs.pfnGetScreenInfo(&info);
    if(info.iWidth<320||info.iHeight<240||info.iWidth>4096||info.iHeight>4096)return;
    const unsigned scale=gui_scale?static_cast<unsigned>(std::clamp(gui_scale->value,0.0f,8.0f)):0;
    const bool enabled=!minecraft_hud||minecraft_hud->value!=0;
    if(viewport_width!=static_cast<unsigned>(info.iWidth)||viewport_height!=static_cast<unsigned>(info.iHeight)||viewport_scale!=scale||viewport_enabled!=enabled){
        viewport_width=info.iWidth;viewport_height=info.iHeight;viewport_scale=scale;viewport_enabled=enabled;++viewport_id;ClearHud();
    }
    if(Seconds()<next_viewport)return;
    if(!viewport_id)viewport_id=1;
    Writer w;w.u64(world);w.u64(viewport_id);w.u32(viewport_width);w.u32(viewport_height);w.u32(viewport_scale);w.u32(enabled?1:0);
    if(link.send(Type::viewport,w.data))next_viewport=Seconds()+1;
}
void ReadHud(Reader& r){
    const auto epoch=r.u64(),revision=r.u64(),viewport=r.u64(),menu=r.u64();
    const auto screen_width=r.u32(),screen_height=r.u32(),width=r.u32(),height=r.u32(),flags=r.u32(),life=r.u32(),slot=r.u32();
    const auto health=r.f32();const auto food=r.u32(),armor=r.u32(),level=r.u32();const auto experience=r.f32();
    if(epoch!=world||viewport!=viewport_id||life!=minecraft_life||screen_width!=viewport_width||screen_height!=viewport_height||revision<=hud_revision)return;
    if((flags&~15u)||((flags&12)==12)||((flags&1)!=0)!=(menu!=0)||!width||!height||width>1024||height>1024||slot>8||health<0||food>20||armor>1024||experience<0||experience>1)throw ProtocolError("Invalid HUD state");
    const auto pixel_width=(flags&2)?screen_width:width,pixel_height=(flags&2)?screen_height:height;
    const auto image=read_hud_image(r,pixel_width,pixel_height);
    const bool blank=image.blank();
    if(blank){
        if(!hud_blank||!hud_texture||hud_width!=1||hud_height!=1){
            constexpr std::array<std::uint8_t,4> zero{};
            render::upload_texture(hud_texture,hud_width,hud_height,1,1,zero);++hud_blank_uploads;
        }
    }else render::upload_texture(hud_texture,hud_width,hud_height,pixel_width,pixel_height,image.rgba);
    hud_gui_width=width;hud_gui_height=height;
    hud_blank=blank;if(blank)++hud_blank_frames;
    hud_revision=revision;hud_menu_id=menu;hud_flags=flags;hud_life=life;
    const double now=Seconds();if(last_hud)hud_interval_ms=(now-last_hud)*1000;last_hud=now;++hud_frames;hud_bytes+=std::uint64_t(pixel_width)*pixel_height*4;
    hud_slot=slot;hud_health=health;hud_food=food;hud_armor=armor;hud_level=level;hud_experience=experience;
}
void SendControl(unsigned action) {
    if(!HasControl()&&!(action==14&&HudFresh())&&!(action==15&&link.connected()))return;
    Writer w;w.u64(world);w.u32(action);link.send(Type::control,w.data);
}
void CloseMinecraftMenu(){SendControl(14);}
void OpenMinecraftInventory(){SendControl(10);}
void OpenMinecraftCommand(){SendControl(17);}
void ToggleForm(){gEngfuncs.pfnServerCmd("goldcraft_form toggle\n");}
void SelectFormMenu(int index){
    form_menu.clear();
    // The original client still received ShowMenu while its HUD was hidden.
    // Dismiss that local copy as well, so it cannot reappear on death/form change.
    if(previous_show_menu){std::uint8_t close[5]{};previous_show_menu("ShowMenu",sizeof(close),close);}
    auto command="menuselect "+std::to_string(index)+"\n";
    gEngfuncs.pfnServerCmd(command.data());++form_menu_selections;
}
void OpenFormMenu(){
    if(form_menu.active(Seconds())&&form_menu.forms()){SelectFormMenu(10);return;}
    if(!world)return;
    CloseMinecraftMenu();ReleaseHostUse();
    gEngfuncs.pfnClientCmd(const_cast<char*>("-forward\n-back\n-moveleft\n-moveright\n-jump\n-duck\n-speed\n-attack\n-attack2\n-use\n-reload\n"));
    gEngfuncs.pfnServerCmd("goldcraft_menu\n");
}
bool DefaultFormMenuKey(int key,const char* binding_text){
    // Minecraft screens own text entry even when a printable key is bound to
    // the form menu in native CS. The same key remains a shortcut in gameplay.
    // Half-Life's inherited defaults bind F6 to quicksave, which has no use on
    // a GoldCraft dedicated server. Accept only those exact stock bindings;
    // custom commands and ordinary servers keep their original behavior.
    const bool default_f6=!binding_text||!*binding_text
        ||std::strcmp(binding_text,"save quick")==0
        ||std::strcmp(binding_text,"echo Quicksaving...; wait; save quick")==0;
    return !ui_active&&((world&&key==K_F6&&default_f6)
        ||(binding_text&&std::strcmp(binding_text,"goldcraft_menu")==0));
}
void ReloadMinecraftResources(){SendControl(15);}
void Status() {
    gEngfuncs.Con_Printf("[GoldCraft] protocol=%d engine=%d connected=%d local-port=%d sections=%u\n",protocol_version,api->GetEngineBuildnum(),link.connected()?1:0,link.port(),static_cast<unsigned>(sections.size()));
}
void WriteDiagnostics() {
    if(diagnostic_io.status_enabled()) {
        std::ostringstream out;
        out<<'{'; control_trace.write(out);out<<',';write_transport_stats(out,link.transport_stats());out<<',';
        WriteWorkDiagnostics(out);out<<',';
        diagnostic_io.write_status(out);out<<',';
        client_precache::write_status(out); out<<',';
        visible_entities::write_status(out); out<<',';
        packet_client::write_status(out); out<<',';
        client_userinfo::diagnostics(out); out<<',';
        out<<"\"spectatorMenu\":";spectator_menu::diagnostics(out);out<<',';
        out<<"\"nativeHudClock\":";hud_clock::diagnostics(out);out<<',';
        client_vitals::write_status(out); out<<',';
        input_audit::write_status(out); out<<',';
        client_map::write_status(out); out<<',';
        out<<"\"mapEditWork\":";map_edits.write_work_status(out);out<<',';
        out<<"\"mapEditReceived\":"<<map_edits.state().revision<<",\"mapEditApplied\":"<<map_edits.applied().revision
           <<",\"mapEditPending\":"<<(map_edits.pending()?"true":"false")<<',';
        std::size_t vertices=0; for(const auto& [key,s]:sections) vertices+=s.vertices.size();
        const auto& gpu=render::statistics();
        const auto host_menu=host_ui::state();
        const auto depth_batches=std::count_if(entity_batches.begin(),entity_batches.end(),[](const auto& b){return (b.flags&2)!=0;});
        const auto translucent_batches=std::count_if(entity_batches.begin(),entity_batches.end(),[](const auto& b){return (b.flags&1)!=0;});
        out<<"\"engine\":"<<api->GetEngineBuildnum()<<",\"connected\":"<<(link.connected()?"true":"false")
           <<",\"world\":\""<<world<<"\",\"playerSlot\":"<<player_slot<<",\"playerSerial\":"<<player_serial<<",\"playerLife\":"<<minecraft_life<<",\"sections\":"<<sections.size()<<",\"vertices\":"<<vertices
           <<",\"drawFrames\":"<<frames_drawn<<",\"glError\":"<<last_gl_error<<",\"lights\":"<<lamps.size()<<",\"entities\":"<<entity_count<<",\"blockEntities\":"<<block_entity_count<<",\"entityBatches\":"<<entity_batches.size()<<",\"inputEchoMs\":"<<input_latency_ms<<",\"minecraftControl\":"<<(HasControl()?"true":"false")<<",\"inputSequence\":"<<input_sequence<<",\"poseSequence\":"<<pose_sequence
           <<",\"smoothing\":"<<((!view_smoothing||view_smoothing->value!=0)?"true":"false")<<",\"frameWorkMs\":"<<frame_work_ms<<",\"frameWorkMaxMs\":"<<frame_work_max_ms<<",\"viewFrameMaxMs\":"<<view_frame_max_ms
           <<",\"cameraFrames\":"<<camera_frames<<",\"cameraFresh\":"<<(CameraFresh()?"true":"false")<<",\"perspective\":"<<camera_pose.perspective
           <<",\"cameraAgeMs\":"<<(last_camera?(Seconds()-last_camera)*1000:-1)<<",\"cameraVerticalFov\":"<<camera_pose.vertical_fov
           <<",\"cameraPosition\":["<<camera_pose.position.x<<','<<camera_pose.position.y<<','<<camera_pose.position.z<<']'
           <<",\"cameraAngles\":["<<camera_pose.angles.x<<','<<camera_pose.angles.y<<','<<camera_pose.angles.z<<']'
           <<",\"renderer\":"<<(gpu.renderer?"true":"false")<<",\"sceneApi\":"<<(gpu.scene_api?"true":"false")
           <<",\"sceneCullingApi\":"<<(gpu.scene_culling_api?"true":"false")<<",\"sceneCullingEnabled\":"<<((!render_scene_cull||render_scene_cull->value!=0)?"true":"false")
           <<",\"sceneLightsApi\":"<<(gpu.scene_lights_api?"true":"false")<<",\"sceneLightsSubmitted\":"<<gpu.scene_lights<<",\"sceneLightsFresh\":"<<(FreshLights()?"true":"false")
           <<",\"sceneDistortionApi\":"<<(gpu.scene_distortion_api?"true":"false")
           <<",\"sceneDistortionAccepted\":"<<(gpu.scene_distortion_accepted?"true":"false")
           <<",\"sceneDistortion\":["<<gpu.scene_distortion_state.intensity<<','<<gpu.scene_distortion_state.angle<<']'
           <<",\"sceneVisionApi\":"<<(gpu.scene_vision_api?"true":"false")<<",\"sceneVisionAccepted\":"<<(gpu.scene_vision_accepted?"true":"false")
           <<",\"sceneVision\":["<<gpu.scene_vision_state.nightVision<<','<<gpu.scene_vision_state.blindness<<','<<gpu.scene_vision_state.darkness<<','<<gpu.scene_vision_state.darknessPulse<<']'
           <<",\"sceneStaticShadowsApi\":"<<(gpu.scene_static_shadows_api?"true":"false")
           <<",\"sceneStaticShadowsRegistered\":"<<(gpu.scene_static_shadows_registered?"true":"false")
           <<",\"sceneStaticShadowsEnabled\":"<<((!render_scene_static_shadows||render_scene_static_shadows->value!=0)?"true":"false")
           <<",\"staticShadowCallbacks\":"<<static_shadow_callbacks<<",\"staticShadowDraws\":"<<static_shadow_draws
           <<",\"staticShadowSectionSkips\":"<<static_shadow_section_skips<<",\"staticShadowInvalidations\":"<<gpu.static_shadow_invalidations
           <<",\"staticShadowAlphaInvalidations\":"<<static_shadow_alpha_invalidations<<",\"staticShadowFailures\":"<<static_shadow_failures
           <<",\"staticShadowAlphaChangedPackets\":"<<static_shadow_alpha_changed_packets
           <<",\"staticShadowAlphaUnreferencedPackets\":"<<static_shadow_alpha_unreferenced_packets
           <<",\"staticShadowAlphaAffectedSections\":"<<static_shadow_alpha_affected_sections
           <<",\"staticShadowAlphaFallbacks\":"<<static_shadow_alpha_fallbacks
           <<",\"shadowBoundsTests\":"<<gpu.shadow_bounds_tests<<",\"shadowBoundsCulled\":"<<gpu.shadow_bounds_culled
           <<",\"postSceneApi\":"<<(gpu.post_scene_api?"true":"false")<<",\"coreProfile\":"<<(gpu.core?"true":"false")
           <<",\"fpsLimit\":"<<gEngfuncs.pfnGetCvarFloat("fps_max")<<",\"fpsOverride\":"<<gEngfuncs.pfnGetCvarFloat("fps_override")
           <<",\"vsync\":"<<gEngfuncs.pfnGetCvarFloat("gl_vsync")<<",\"benchmarkFramerate\":"<<(framerate_benchmark.active?"true":"false")
           <<",\"benchmarkRemainingMs\":"<<(framerate_benchmark.active?std::max(0.0,(framerate_benchmark.expires-Seconds())*1000):0)
           <<",\"developerLevel\":"<<gEngfuncs.pfnGetCvarFloat("developer")
           <<",\"atlasUploads\":"<<atlas_uploads<<",\"atlasGeneration\":"<<atlas_generation<<",\"atlasAnimationPackets\":"<<atlas_animation_packets<<",\"atlasPatchBytes\":"<<atlas_patch_bytes<<",\"atlasAnimationIntervalMs\":"<<atlas_animation_interval_ms
           <<",\"gpuUploads\":"<<gpu.uploads<<",\"gpuUploadBytes\":"<<gpu.upload_bytes<<",\"gpuDraws\":"<<gpu.draws<<",\"opaquePasses\":"<<gpu.opaque_passes<<",\"shadowPasses\":"<<gpu.shadow_passes
           <<",\"depthOnlyShadowPasses\":"<<gpu.depth_only_shadow_passes
           <<",\"avatarCount\":"<<rendered_players.size()<<",\"suppressedModels\":"<<suppressed_models<<",\"replacePlayers\":"<<((!replace_players||replace_players->value!=0)?"true":"false")
           <<",\"entityDepthWriteBatches\":"<<depth_batches<<",\"entityTranslucentBatches\":"<<translucent_batches
           <<",\"entityTextures\":"<<entity_textures.size()<<",\"entityRevision\":"<<entity_revision
           <<",\"entityEffectGeneration\":"<<entity_effect_generation<<",\"entityLife\":"<<entity_life
           <<",\"entityEffectBatches\":"<<entity_effect_batches.size()<<",\"entityEffectVertices\":"<<entity_effect_vertices
           <<",\"entityEffectDraws\":"<<entity_effect_draws
           <<",\"outlineEdges\":"<<outline_vertices.size()/2<<",\"crackBatches\":"<<crack_batches.size()
           <<",\"feedbackFrames\":"<<feedback_frames<<",\"feedbackDraws\":"<<feedback_draws
           <<",\"feedbackAgeMs\":"<<(last_feedback?(Seconds()-last_feedback)*1000:-1)
           <<",\"entityFrames\":"<<entity_frames<<",\"entityPresentedFrames\":"<<entity_presented_frames<<",\"entityIntervalMs\":"<<entity_interval_ms
           <<",\"entityTextureUploads\":"<<entity_texture_uploads<<",\"entityTextureBytes\":"<<entity_texture_bytes
           <<",\"sceneReady\":"<<(atlas_generation?"true":"false")<<",\"pendingSceneMeshes\":"<<pending_scene_meshes
           <<",\"particleCount\":"<<particle_count<<",\"particleVertices\":"<<particle_vertices<<",\"particleBatches\":"<<particle_batches.size()
           <<",\"particleFrames\":"<<particle_frames<<",\"particleDraws\":"<<particle_draws<<",\"particleBytes\":"<<particle_bytes
           <<",\"particleGeneration\":"<<particle_generation<<",\"particleRevision\":"<<particle_revision<<",\"particleLife\":"<<particle_life
           <<",\"particleTextureStagingBytes\":"<<particle_texture_upload.pending_bytes()
           <<",\"particleTextureRetainedBytes\":"<<particle_texture_upload.retained_bytes()
           <<",\"particleTextureStagingAllocations\":"<<particle_texture_upload.allocation_count()
           <<",\"particleTextureStagingReuses\":"<<particle_texture_upload.reuse_count()
           <<",\"particleTextureReceivedBytes\":"<<particle_texture_upload.received_bytes()
           <<",\"particleTextureChunkUploads\":"<<particle_texture_chunk_uploads
           <<",\"particleAgeMs\":"<<(last_particle_frame?(Seconds()-last_particle_frame)*1000:-1)<<",\"particleIntervalMs\":"<<particle_interval_ms
           <<",\"particlesEnabled\":"<<((!render_particles||render_particles->value!=0)?"true":"false")
           <<",\"postEffectStages\":"<<post_stages.size()<<",\"postEffectBatches\":"<<post_batches.size()
           <<",\"postEffectFrames\":"<<post_frames<<",\"postEffectDraws\":"<<post_draws<<",\"postEffectFailures\":"<<post_failures
           <<",\"postEffectCallbacks\":"<<post_callbacks<<",\"postEffectNonemptyFrames\":"<<post_nonempty_frames
           <<",\"postEffectRegistered\":"<<(post_provider?"true":"false")
           <<",\"postEffectAgeMs\":"<<(last_post_frame?(Seconds()-last_post_frame)*1000:-1)
           <<",\"postEffectResidentBytes\":"<<(post_replay?post_replay->statistics().resident_bytes:0)
           <<",\"postEffectEnabled\":"<<((!render_post_effects||render_post_effects->value!=0)?"true":"false")
           <<",\"hudVisible\":"<<(HudFresh()?"true":"false")<<",\"hudFrames\":"<<hud_frames<<",\"hudDraws\":"<<hud_draws<<",\"hudWidth\":"<<hud_gui_width<<",\"hudHeight\":"<<hud_gui_height
           <<",\"hudTextureWidth\":"<<hud_width<<",\"hudTextureHeight\":"<<hud_height
           <<",\"hudBlank\":"<<(hud_blank?"true":"false")<<",\"hudBlankFrames\":"<<hud_blank_frames
           <<",\"hudBlankUploads\":"<<hud_blank_uploads<<",\"hudBlankCompositesSkipped\":"<<hud_blank_composites_skipped
           <<",\"minecraftHands\":"<<(HudFresh()&&(hud_flags&4)?"true":"false")<<",\"minecraftChatOverlay\":"<<(HudFresh()&&(hud_flags&8)?"true":"false")
           <<",\"viewModelSuppressed\":"<<(viewmodel_suppressed?"true":"false")
           <<",\"hostViewModel\":"<<(host_viewmodel?host_viewmodel->value:-1)<<",\"viewModelSuppressedFrames\":"<<suppressed_viewmodel_frames<<",\"hudIntervalMs\":"<<hud_interval_ms
           <<",\"hudRevision\":"<<hud_revision<<",\"hudAgeMs\":"<<(last_hud?(Seconds()-last_hud)*1000:-1)<<",\"hudWorkMs\":"<<hud_work_ms<<",\"hudMenuId\":"<<hud_menu_id
           <<",\"formMenuVisible\":"<<(form_menu.active(Seconds())?"true":"false")<<",\"formMenuSlots\":"<<form_menu.keys()
           <<",\"formMenuDraws\":"<<form_menu_draws<<",\"formMenuSelections\":"<<form_menu_selections
           <<",\"serverMenuDraws\":"<<server_hud::menu_draws()<<",\"serverMenuInput\":"<<(ServerMenuInput()?"true":"false")
           <<",\"deathMessages\":"<<server_hud::death_messages()<<",\"deathNoticeDraws\":"<<server_hud::death_draws()<<",\"deathNotices\":"<<server_hud::notices(Seconds())
           <<",\"hudSlot\":"<<hud_slot<<",\"hudHealth\":"<<hud_health<<",\"hudFood\":"<<hud_food<<",\"hudArmor\":"<<hud_armor<<",\"hudLevel\":"<<hud_level<<",\"hudExperience\":"<<hud_experience
           <<",\"uiActive\":"<<(ui_active?"true":"false")<<",\"uiX\":"<<ui_x<<",\"uiY\":"<<ui_y<<",\"viewportId\":"<<viewport_id<<",\"viewportWidth\":"<<viewport_width<<",\"viewportHeight\":"<<viewport_height
           <<",\"testCommand\":"<<test_command_id<<",\"minecraftMenu\":"<<(minecraft_menu?"true":"false")<<",\"inputActive\":"<<(last_input_active?"true":"false")<<",\"inputButtons\":"<<last_input_buttons<<",\"inputForward\":"<<last_input_forward<<",\"minecraftEye\":"<<minecraft_eye
           <<",\"inputButtonsObserved\":"<<input_buttons_observed<<",\"keyEvents\":"<<key_events<<",\"lastKey\":"<<last_key<<",\"lastKeyDown\":"<<last_key_down
           <<",\"forwardedKeys\":"<<forwarded_key_events
           <<",\"consoleVisible\":"<<(gEngfuncs.Con_IsVisible()?"true":"false")
           <<",\"hostUiHook\":"<<(host_menu.hooked?"true":"false")<<",\"hostGameMenu\":"<<(host_menu.game_menu?"true":"false")
           <<",\"hostUiKeyboard\":"<<(host_menu.keyboard?"true":"false")<<",\"hostUiFocus\":\""<<host_menu.focus<<"\""
           <<",\"hostUiEvents\":"<<host_menu.events<<",\"hostUiConsumed\":"<<host_menu.consumed
           <<",\"hostKeyReleaseHook\":"<<(host_menu.release_hooked?"true":"false")<<",\"hostKeyReleases\":"<<host_menu.releases
           <<",\"hostCursorVisible\":"<<(host_menu.cursor_visible?"true":"false")<<",\"mouseCaptureApi\":"<<(host_menu.mouse_api?"true":"false")
           <<",\"rawInput\":"<<gEngfuncs.pfnGetCvarFloat("m_rawinput")<<",\"relativeMouseMode\":"<<host_menu.relative_mouse
           <<",\"mouseLookState\":"<<NativeMouseLookState()
           <<",\"mousePitch\":"<<gEngfuncs.pfnGetCvarFloat("m_pitch")<<",\"mouseYaw\":"<<gEngfuncs.pfnGetCvarFloat("m_yaw")
           <<",\"pitchUp\":"<<gEngfuncs.pfnGetCvarFloat("cl_pitchup")<<",\"pitchDown\":"<<gEngfuncs.pfnGetCvarFloat("cl_pitchdown")
           <<",\"demoPlayback\":"<<(gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback()?"true":"false")
           <<",\"demoRecording\":"<<(gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsRecording()?"true":"false")
           <<",\"mouseCaptureRepairs\":"<<host_menu.mouse_repairs<<",\"mouseCaptureErrors\":"<<host_menu.mouse_errors
           <<",\"windowFocused\":"<<(window_focused?"true":"false")<<",\"hostVolume\":"<<gEngfuncs.pfnGetCvarFloat("volume")
           <<",\"rendererDeferred\":"<<gEngfuncs.pfnGetCvarFloat("r_deferred_lighting")<<",\"rendererDiagnostics\":"<<gEngfuncs.pfnGetCvarFloat("r_studio_diagnostics")
           <<",\"rendererProfile\":"<<gEngfuncs.pfnGetCvarFloat("r_renderer_profile")
           <<",\"rendererShadowCull\":"<<gEngfuncs.pfnGetCvarFloat("r_shadow_caster_cull")
           <<",\"rendererStudioFaceCullAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_studio_shadow_face_cull")?"true":"false")
           <<",\"rendererStudioFaceCull\":"<<gEngfuncs.pfnGetCvarFloat("r_studio_shadow_face_cull")
           <<",\"rendererStudioDepthExecutorAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_studio_shadow_depth_executor")?"true":"false")
           <<",\"rendererStudioDepthExecutor\":"<<gEngfuncs.pfnGetCvarFloat("r_studio_shadow_depth_executor")
           <<",\"rendererStudioBonePaletteCacheAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_studio_bone_palette_cache")?"true":"false")
           <<",\"rendererStudioBonePaletteCache\":"<<gEngfuncs.pfnGetCvarFloat("r_studio_bone_palette_cache")
           <<",\"rendererWorldMaterialBatchAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_world_material_batch")?"true":"false")
           <<",\"rendererWorldMaterialBatch\":"<<gEngfuncs.pfnGetCvarFloat("r_world_material_batch")
           <<",\"rendererStudioShadowPlanAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_studio_shadow_plan")?"true":"false")
           <<",\"rendererStudioShadowPlan\":"<<gEngfuncs.pfnGetCvarFloat("r_studio_shadow_plan")
           <<",\"rendererDrawParameterStreamAvailable\":"<<(gEngfuncs.pfnGetCvarPointer("r_draw_parameter_stream")?"true":"false")
           <<",\"rendererDrawParameterStream\":"<<gEngfuncs.pfnGetCvarFloat("r_draw_parameter_stream")
           <<",\"hostUseHeld\":"<<(host_use_sent?"true":"false")<<",\"hostUseMessages\":"<<host_use_messages
           <<",\"minecraftForm\":"<<(server_minecraft_form?"true":"false")<<",\"serverPlayerLife\":"<<server_player_life
           <<",\"nativeFluidVolumes\":"<<native_fluids.field.state().volumes.size()<<",\"nativeFluidRevision\":"<<native_fluids.field.state().revision<<",\"nativeFluidActive\":"<<(native_fluids.active(world,Seconds())?"true":"false")
           <<",\"nativeColliders\":"<<native_colliders.size()<<",\"predictionObjects\":"<<prediction_objects<<",\"predictionFrames\":"<<prediction_frames<<",\"predictionOverflow\":"<<prediction_overflow
           <<",\"minecraftFeet\":["<<minecraft_feet.x<<','<<minecraft_feet.y<<','<<minecraft_feet.z<<"],\"origin\":[";
        out<<observed_origin.x<<','<<observed_origin.y<<','<<observed_origin.z;
        out<<"],\"viewAngles\":["<<observed_angles.x<<','<<observed_angles.y<<','<<observed_angles.z<<"],\"viewReady\":"<<(have_view?"true":"false")<<"}";
        diagnostic_io.snapshot(std::move(out).str());
        frame_work_max_ms=view_frame_max_ms=0;
    }
}
int GlowMessage(const char*,int size,void* data){
    try{
        if(!data||size!=status_glow::wire_bytes)throw ProtocolError("Invalid GCOutline length");
        const auto p=status_glow::decode_presentation(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(native_outline_ready)native_glow.accept(p,world,Seconds());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int NauseaMessage(const char*,int size,void* data){
    try{
        if(!data||size!=status_nausea::wire_bytes)throw ProtocolError("Invalid GCNausea length");
        const auto s=status_nausea::decode(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(s.epoch==world&&s.slot==player_slot&&s.serial==player_serial&&s.life==server_player_life)
            native_nausea.accept(s,Seconds());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int VisionMessage(const char*,int size,void* data){
    try{
        if(!data||size!=status_vision::wire_bytes)throw ProtocolError("Invalid GCVision length");
        const auto s=status_vision::decode(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(s.epoch==world&&s.slot==player_slot&&s.serial==player_serial&&s.life==server_player_life)
            native_vision.accept(s,Seconds());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int WaterMessage(const char*,int size,void* data){
    try{
        if(!data||size!=56)throw ProtocolError("Invalid GCWater length");
        const auto s=native_water::decode(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(s.epoch==world&&s.slot==player_slot&&s.serial==player_serial&&s.life==server_player_life)water_state.accept(s,Seconds());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int MotionMessage(const char*,int size,void* data){
    try{
        if(!data||size!=44)throw ProtocolError("Invalid GCMotion length");
        const auto s=status_motion::decode(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(s.epoch==world&&s.slot==player_slot&&s.serial==player_serial&&s.life==server_player_life)
            native_motion.accept(s,Seconds());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int BindingMessage(const char*,int size,void* data) {
    try {
        if(size!=32) throw ProtocolError("invalid GCBind length");
        auto view=std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size));
        Reader r(view); auto new_world=r.u64();auto slot=r.u32(),serial=r.u32(); r.key(); r.finish();
        if(new_world!=map_edits.state().epoch){map_edits.reset(new_world);edit_assembler.reset(new_world);client_map::reset();next_edit_query=0;edit_recovery=true;}
        if(new_world!=world){native_glow.clear();ClearSections();ClearLights();ClearDynamic();ClearHud();player_presentations={};presentation_times={};view_interpolator.clear();atlas_generation=0;minecraft_control=false;pose_sequence=input_sequence=0;}
        if(new_world!=world||slot!=player_slot||serial!=player_serial){native_motion.clear();water_state.clear();native_vision.clear();native_nausea.clear();view_distortion_frames.clear();ReleaseHostUse();minecraft_life=server_player_life=0;minecraft_control=server_minecraft_form=false;view_interpolator.clear();}
        world=new_world;player_slot=slot;player_serial=serial;binding.assign(view.begin(),view.end());
        link.send(Type::client_binding,binding);
        if(!section_logged) Log("received server pairing identity for current map");
    } catch(const std::exception& e) { Log(e.what()); }
    return 1;
}
void RequestEdits(){
    if(!world||Seconds()<next_edit_query)return;
    const auto command="goldcraft_edits "+std::to_string(world)+"\n";
    gEngfuncs.pfnServerCmd(command.c_str());next_edit_query=Seconds()+1;edit_recovery=false;
}
bool MatchingHostProtocol(){
    const char* value=gEngfuncs.ServerInfo_ValueForKey?gEngfuncs.ServerInfo_ValueForKey("mc_protocol"):nullptr;
    static const auto expected=std::to_string(protocol_version);
    return value&&value==expected;
}
void UpdateLocalDiscovery(){
    net_status_t status{};
    if(gEngfuncs.pNetAPI&&gEngfuncs.pNetAPI->Status)gEngfuncs.pNetAPI->Status(&status);
    const bool joined=status.connected&&MatchingHostProtocol()&&
        !(gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback());
    discovery.state(joined?world:0,link.connected());
}
bool BenchmarkServer(unsigned short& port){
    if(!std::getenv("GOLDCRAFT_TEST_COMMAND")||!gEngfuncs.pNetAPI||!gEngfuncs.pNetAPI->Status||
       !MatchingHostProtocol()||!world||!link.connected()||link.generation()!=last_link_generation||
       (gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback()))return false;
    net_status_t status{};gEngfuncs.pNetAPI->Status(&status);
    const auto& address=status.remote_address;
    if(!status.connected||address.type!=NA_IP||address.ip[0]!=127||address.ip[1]||address.ip[2]||address.ip[3]!=1||!address.port)return false;
    port=address.port;return true;
}
void RestoreBenchmarkFramerate(){
    if(!framerate_benchmark.active)return;
    const auto saved=framerate_benchmark;framerate_benchmark={};
    gEngfuncs.Cvar_SetValue("fps_max",saved.fps_max);
    gEngfuncs.Cvar_SetValue("fps_override",saved.fps_override);
    gEngfuncs.Cvar_SetValue("gl_vsync",saved.vsync);
}
void CheckBenchmarkFramerate(){
    if(!framerate_benchmark.active)return;
    unsigned short port=0;
    if(Seconds()>=framerate_benchmark.expires||!BenchmarkServer(port)||port!=framerate_benchmark.port||world!=framerate_benchmark.epoch||
       link.generation()!=framerate_benchmark.generation)RestoreBenchmarkFramerate();
}
void SetBenchmarkFramerate(int fps){
    if(fps==0){RestoreBenchmarkFramerate();return;}
    unsigned short port=0;
    if((fps!=100&&fps!=200&&fps!=300)||!BenchmarkServer(port)){
        Log("benchmark_framerate rejected: requires the paired 127.0.0.1 sandbox server");return;
    }
    CheckBenchmarkFramerate();
    if(!framerate_benchmark.active){
        framerate_benchmark={true,gEngfuncs.pfnGetCvarFloat("fps_max"),gEngfuncs.pfnGetCvarFloat("fps_override"),
            gEngfuncs.pfnGetCvarFloat("gl_vsync"),world,link.generation(),port};
    }
    // A forgotten sampling script must not leave a lasting high-framerate lease.
    // Controlled runners also send 0 in finally before closing the game.
    framerate_benchmark.expires=Seconds()+30;
    gEngfuncs.Cvar_SetValue("fps_override",fps>100?1.0f:0.0f);
    gEngfuncs.Cvar_SetValue("gl_vsync",0);
    gEngfuncs.Cvar_SetValue("fps_max",static_cast<float>(fps));
}
void PublishBrushSnapshot(){
    try {
        auto* entity=gEngfuncs.GetEntityByIndex(0);
        map_edits.publish(render::world_edit(),entity?entity->model:nullptr,render::world_edit2());
    }catch(const std::exception& error){Log(error.what());edit_recovery=true;}
}
int BrushIdentityMessage(const char*,int size,void* data){
    try{
        if(!MatchingHostProtocol()||size<0||!data)throw ProtocolError("Invalid GCBrush source/message");
        client_map::brush_identities().accept(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
    }catch(const std::exception& error){
        client_map::brush_identities().invalidate();Log(error.what());
    }
    // Packet parsing can follow HUD_Frame. Publish or revoke before drawing,
    // even when the edit ledger has no pending transaction.
    PublishBrushSnapshot();
    return 1;
}
int EditMessage(const char*,int size,void* data){
    try{
        if(!MatchingHostProtocol())throw ProtocolError("GCEdit from an incompatible server");
        if(size<0||!data)throw ProtocolError("Invalid GCEdit message");
        const auto message=edit_assembler.accept(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        if(!message)return 1;
        const auto result=message->type==Type::map_edit_snapshot?map_edits.accept(edits::snapshot(message->payload)):
            message->type==Type::map_edit_batch?map_edits.accept(edits::batch(message->payload)):
            map_edits.accept(edits::delta(message->payload));
        if(result==edits::Applied::need_snapshot)edit_recovery=true;
        else if(map_edits.ready())edit_recovery=false;
    }catch(const std::exception& error){
        Log(error.what());map_edits.invalidate();edit_recovery=true;
    }
    return 1;
}
int FormMessage(const char*,int size,void* data){
    try{
        if(size!=16)throw ProtocolError("Invalid GCForm length");
        Reader r(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        const auto epoch=r.u64();const auto life=r.u32(),form=r.u32();r.finish();
        if(epoch!=world)return 1;
        if(form>1)throw ProtocolError("Invalid native player form");
        if(life!=server_player_life||server_minecraft_form!=(form!=0)){
            native_motion.clear();water_state.clear();native_vision.clear();native_nausea.clear();view_distortion_frames.clear();minecraft_transition_until=form?Seconds()+0.5:0;
            ClearPostEffects();
            // Resource uploads keep their epoch/generation identity across a
            // form or spawn change. Presentation frames still require new life.
            if(life!=server_player_life)ClearEntityFrame();
            ReleaseMinecraftKeys();
            camera_interpolator.clear();camera_revision=0;last_camera=0;
            ReleaseHostUse();ClearHud();view_interpolator.clear();minecraft_control=minecraft_menu=ui_active=false;
            gEngfuncs.pfnClientCmd(const_cast<char*>("-forward\n-back\n-moveleft\n-moveright\n-jump\n-duck\n-speed\n-attack\n-attack2\n-use\n-reload\n"));
        }
        server_player_life=life;server_minecraft_form=form!=0;UpdateViewModel();
        control_trace.observe(SampleControl());
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int AvatarMessage(const char*,int size,void* data) {
    try {
        if(size!=40)throw ProtocolError("Invalid GCAvatar length");
        Reader r(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        const auto epoch=r.u64();PlayerPresentation next;
        next.identity.slot=r.u32();next.identity.serial=r.u32();next.identity.userid=r.u32();next.flags=r.u32();next.identity.uuid=r.key();r.finish();
        if(epoch!=world)return 1;
        if(!next.identity.slot||next.identity.slot>64||(next.flags&~127u))throw ProtocolError("Invalid native avatar roster");
        player_presentations[next.identity.slot]=next;presentation_times[next.identity.slot]=Seconds();
    }catch(const std::exception& e){Log(e.what());}
    return 1;
}
int AddEntity(int type,cl_entity_t* entity,const char* model) {
    const int accepted=gExportfuncs.HUD_AddEntity?gExportfuncs.HUD_AddEntity(type,entity,model):1;
    if(!accepted||!entity||!entity->player||entity->index<1||entity->index>64||!link.connected()
       ||!world||!atlas_texture||(replace_players&&replace_players->value==0)||!EntityFrameFresh())return accepted;
    if(Seconds()-presentation_times[entity->index]>3)return accepted;
    for(const auto& avatar:rendered_players)if(avatar.slot==static_cast<unsigned>(entity->index)&&replaces_player(avatar,player_presentations[entity->index])) {
        ++suppressed_models;return 0;
    }
    return accepted;
}
void InitHud() {
    gExportfuncs.HUD_Init();
    // VGUI interfaces are available after the native HUD initializes. The
    // earlier LoadClient phase can precede vgui2.dll initialization.
    Log(spectator_menu::install(api)?"native menu stale-item guard installed":
        std::string("native menu stale-item guard unavailable: ")+spectator_menu::error());
    Log(hud_clock::install(api)?"native HUD clock installed":"native HUD clock unavailable");
    server_hud::initialize(&gEngfuncs);
    const char* input_audit_setting=std::getenv("GOLDCRAFT_INPUT_AUDIT");
    input_audit::install(api,input_audit_setting&&std::strcmp(input_audit_setting,"1")==0);
    client_precache::register_commands();
    client_userinfo::initialize();
    gEngfuncs.pfnRegisterVariable(packet_entities::capability,
        packet_client::enabled() ? packet_entities::capability_value : "0", FCVAR_USERINFO);
    gEngfuncs.Cvar_SetValue(packet_entities::capability,
        packet_client::enabled() ? float(packet_entities::capacity) : 0.f);
    Log(client_vitals::install(api) ? "32-bit health/armor HUD installed" :
        std::string("integer vitals unavailable: ") + client_vitals::error());
    host_ui::install(api,[](int down,int key,const char* binding_text){
        if(gEngfuncs.Con_IsVisible()||host_keys::console_binding(binding_text))return false;
        if(key>=0&&key<int(menu_pressed.size())&&menu_pressed[key]){KeyEvent(down,key,binding_text);return true;}
        if(DefaultFormMenuKey(key,binding_text)){
            if(host_ui::state().keyboard)return false;
            KeyEvent(down,key,binding_text);return true;
        }
        if(ui_active&&HudFresh()){KeyEvent(down,key,binding_text);return true;}
        if(ServerMenuInput()&&ServerMenuKey(key,binding_text)){KeyEvent(down,key,binding_text);return true;}
        if(HasControl()&&!host_ui::state().keyboard){
            const auto route=host_keys::gameplay_route(binding_text,true,true);
            if(route==host_keys::Route::observe)ForwardKey(down,key);
            if(route==host_keys::Route::consume){KeyEvent(down,key,binding_text);return true;}
        }
        return false;
    },gEngfuncs.Key_Event,[](int key){
        if(key>=0&&key<int(menu_pressed.size()))menu_pressed[key]=false;
        if(key>=0&&key<int(forwarded_keys.size())&&forwarded_keys[key])ForwardKey(0,key);
    });
    Log(host_text::install(api,[]()->host_text::Context{
        return ui_active&&HudFresh()&&!gEngfuncs.Con_IsVisible()&&!host_ui::game_menu_active()
            ?host_text::Context{world,hud_menu_id}:host_text::Context{};
    },[](std::uint32_t cp,unsigned mods){
        const auto previous=ui_modifiers;ui_modifiers=mods;SendUi(5,int(cp));ui_modifiers=previous;
    })?"SDL text input and IME candidate bridge installed":"SDL text input bridge unavailable");
    gEngfuncs.pfnAddCommand("goldcraft_status",Status);
    gEngfuncs.pfnAddCommand("goldcraft_close_menu",CloseMinecraftMenu);
    gEngfuncs.pfnAddCommand("goldcraft_reload_resources",ReloadMinecraftResources);
    gEngfuncs.pfnAddCommand("goldcraft_inventory",OpenMinecraftInventory);
    gEngfuncs.pfnAddCommand("goldcraft_command",OpenMinecraftCommand);
    gEngfuncs.pfnAddCommand("goldcraft_toggle",ToggleForm);
    gEngfuncs.pfnAddCommand("goldcraft_menu",OpenFormMenu);
    previous_show_menu=api->HookUserMsg("ShowMenu",[](const char* name,int size,void* data)->int {
        const int result=previous_show_menu?previous_show_menu(name,size,data):1;
        try{
            if(size<0)throw ProtocolError("Invalid ShowMenu length");
            form_menu.message(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)),Seconds());
        }catch(const ProtocolError& e){form_menu.clear();Log(e.what());}
        return result;
    });
    previous_death_message=api->HookUserMsg("DeathMsg",[](const char* name,int size,void* data)->int {
        const int result=previous_death_message?previous_death_message(name,size,data):1;
        try{if(size<0||!data)throw ProtocolError("Invalid DeathMsg length");
            server_hud::death(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)),Seconds());
        }catch(const ProtocolError& e){Log(e.what());}return result;
    });
    previous_team_info=api->HookUserMsg("TeamInfo",[](const char* name,int size,void* data)->int {
        const int result=previous_team_info?previous_team_info(name,size,data):1;
        if(size>0&&data)server_hud::team(std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)));
        return result;
    });
    previous_reset_hud=api->HookUserMsg("ResetHUD",[](const char* name,int size,void* data)->int {
        form_menu.clear();
        return previous_reset_hud?previous_reset_hud(name,size,data):1;
    });
    gEngfuncs.pfnAddCommand("+goldcraft_use",BeginHostUse);
    gEngfuncs.pfnAddCommand("-goldcraft_use",EndHostUse);
    view_smoothing=gEngfuncs.pfnRegisterVariable("mc_view_smoothing","1",0);
    replace_players=gEngfuncs.pfnRegisterVariable("mc_replace_players","1",0);
    minecraft_hud=gEngfuncs.pfnRegisterVariable("mc_hud","1",0);
    gui_scale=gEngfuncs.pfnRegisterVariable("mc_gui_scale","0",0);
    host_viewmodel=gEngfuncs.pfnGetCvarPointer("r_drawviewmodel");
    render_particles=gEngfuncs.pfnRegisterVariable("mc_particles","1",0);
    render_post_effects=gEngfuncs.pfnRegisterVariable("mc_post_effects","1",0);
    render_scene_cull=gEngfuncs.pfnRegisterVariable("mc_scene_cull","1",0);
    render_scene_static_shadows=gEngfuncs.pfnRegisterVariable("mc_scene_static_shadows","1",0);
    gEngfuncs.pfnRegisterVariable(status_motion::client_capability,client_motion::ready()?"1":"0",0);
    gEngfuncs.Cvar_SetValue(status_motion::client_capability,client_motion::ready()?1.f:0.f);
    gEngfuncs.pfnHookUserMsg(status_motion::message_name,MotionMessage);
    gEngfuncs.pfnRegisterVariable(native_water::client_capability,client_motion::ready()?"1":"0",0);
    gEngfuncs.Cvar_SetValue(native_water::client_capability,client_motion::ready()?1.f:0.f);
    gEngfuncs.pfnHookUserMsg(native_water::message_name,WaterMessage);
    gEngfuncs.pfnRegisterVariable(status_nausea::client_capability,"1",0);
    gEngfuncs.Cvar_SetValue(status_nausea::client_capability,1);
    gEngfuncs.pfnHookUserMsg(status_nausea::message_name,NauseaMessage);
    gEngfuncs.pfnRegisterVariable(status_vision::client_capability,"1",0);
    gEngfuncs.Cvar_SetValue(status_vision::client_capability,1);
    gEngfuncs.pfnHookUserMsg(status_vision::message_name,VisionMessage);
    gEngfuncs.pfnRegisterVariable(status_glow::client_capability,"0",0);
    gEngfuncs.Cvar_SetValue(status_glow::client_capability,0);
    gEngfuncs.pfnHookUserMsg(status_glow::message_name,GlowMessage);
    gEngfuncs.pfnHookUserMsg("GCBind",BindingMessage);
    gEngfuncs.pfnHookUserMsg("GCEdit",EditMessage);
    gEngfuncs.pfnHookUserMsg(brush::message_name,BrushIdentityMessage);
    gEngfuncs.pfnRegisterVariable(brush::capability,brush::capability_value,FCVAR_USERINFO);
    gEngfuncs.Cvar_SetValue(brush::capability,1.f);
    gEngfuncs.pfnHookUserMsg("GCAvatar",AvatarMessage);
    gEngfuncs.pfnHookUserMsg("GCForm",FormMessage);
    gEngfuncs.pfnHookUserMsg("GCObj",[](const char*,int size,void* data)->int {
        try{
            Reader r(std::span(static_cast<const std::uint8_t*>(data),size));
            auto epoch=r.u64(),sequence=r.u64();auto op=r.u32();auto key=r.u64();NativeCollider c{};c.slot=r.u32();
            for(auto& v:c.min)v=r.f32();for(auto& v:c.max)v=r.f32();r.finish();
            if(epoch!=world||!world||sequence<=collider_sequence)return 1;
            if(op>3)throw ProtocolError("Invalid collision operation");
            if(op==1){
                if(!key||c.slot<1||c.slot>32767||(!native_colliders.contains(key)&&native_colliders.size()>=512))throw ProtocolError("Native collider identity/budget");
                for(int i=0;i<3;++i)if(c.min[i]>=c.max[i]||c.min[i]<-16384||c.max[i]>16384)throw ProtocolError("Native collider bounds");
                native_colliders[key]=c;
            }else if(op==0)native_colliders.clear();else if(op==2)native_colliders.erase(key);
            collider_sequence=sequence;last_colliders=Seconds();
        }catch(const std::exception& e){Log(e.what());}
        return 1;
    });
    gEngfuncs.pfnHookUserMsg(fluids::message_name,[](const char*,int size,void* data)->int {
        try{if(size<24||!data)throw ProtocolError("Fluid message size");
            native_fluids.accept(world,std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size)),Seconds());
        }catch(const std::exception& e){Log(e.what());}return 1;
    });
    gEngfuncs.pfnRegisterVariable(fluids::client_capability,"1",0);
    gEngfuncs.Cvar_SetValue(fluids::client_capability,1.f);
    // Remove only our obsolete optional advertisements, never user settings.
    for (const char* key : {status_motion::client_capability, native_water::client_capability,
            status_nausea::client_capability, status_vision::client_capability,
            status_glow::client_capability, fluids::client_capability})
        gEngfuncs.PlayerInfo_SetValueForKey(key, "");
    gEngfuncs.pfnRegisterVariable(client_features::key, "0", FCVAR_USERINFO);
    PublishClientFeatures();
    Log("client HUD hooks registered");
}
int VidInit() {
    WorkScope work(WorkStage::vid_init);
    {
    WorkScope cleanup(WorkStage::vid_cleanup);
    StopPostScene();
    // Revoke while the old provider is still available. Keep the authenticated
    // epoch so a same-map video reset can recover through a fresh snapshot.
    map_edits.reset(world);edit_assembler.reset(world);edit_recovery=world!=0;next_edit_query=0;
    visible_entities::fixture_clear();
    client_vitals::reset();native_motion.clear();water_state.clear();native_vision.clear();native_nausea.clear();view_distortion_frames.clear();
    packet_client::reset();
    ClearSections();ClearDynamic();ClearHud();
    next_scene_request=0;
    if(hud_texture&&render::owns_context())glDeleteTextures(1,&hud_texture);
    hud_texture=0;hud_width=hud_height=0;
    if(atlas_texture&&render::owns_context())glDeleteTextures(1,&atlas_texture);
    atlas_texture=0;atlas_generation=0;atlas_width=atlas_height=0;view_interpolator.clear();
    have_view=false;
    }
    int result=0;
    {WorkScope native(WorkStage::native_vid_init);result=gExportfuncs.HUD_VidInit();}
    server_hud::video_reset();server_hud::reset();form_menu.clear();menu_pressed.fill(false);
    {WorkScope initialize(WorkStage::render_initialize);render::initialize(&scene_callbacks,Log);}
    lamps_dirty=true;lamps_published=false;
    WorkScope post(WorkStage::post_initialize);
    post_replay=std::make_unique<render::PostEffectReplay>();
    entity_outline_replay=std::make_unique<render::EntityOutline>();
    entity_outline_gamma=gEngfuncs.pfnGetCvarPointer?gEngfuncs.pfnGetCvarPointer("gamma"):nullptr;
    const char* game=gEngfuncs.pfnGetGameDirectory?gEngfuncs.pfnGetGameDirectory():nullptr;
    post_replay->set_shader_directory((std::filesystem::path(game?game:"cstrike")/"metahook/shaders/goldcraft/vix-post").string());
    post_provider=render::post_scene();
    if(post_provider&&!post_provider->RegisterPostSceneCallbacks(&post_scene_callbacks)){
        post_provider=nullptr;Log("Post-scene callback registration failed");
    }
    native_outline_ready=post_provider&&render::studio_outline();
    gEngfuncs.Cvar_SetValue(status_glow::client_capability,native_outline_ready?1.f:0.f);
    PublishClientFeatures();
    return result;
}
void CalcRefDef(ref_params_t* params) {
    gExportfuncs.V_CalcRefdef(params);
    UpdateViewModel();if(viewmodel_suppressed)++suppressed_viewmodel_frames;
    const double now=Seconds();
    if(last_view_time>0)view_frame_max_ms=std::max(view_frame_max_ms,(now-last_view_time)*1000);last_view_time=now;
    if(HasControl()) {
        const ViewPose pose=(!view_smoothing||view_smoothing->value!=0)?view_interpolator.at(now):ViewPose{minecraft_feet,minecraft_eye};
        Vec3 feet=to_goldsrc(pose.feet);
        params->vieworg[0]=feet.x;params->vieworg[1]=feet.y;params->vieworg[2]=feet.z+pose.eye*units_per_block;
        params->simorg[0]=feet.x;params->simorg[1]=feet.y;params->simorg[2]=feet.z+36;
        if(CameraFresh()){
            const auto camera=(!view_smoothing||view_smoothing->value!=0)?camera_interpolator.at(now):camera_pose;
            const auto position=to_goldsrc(camera.position);
            params->vieworg[0]=position.x;params->vieworg[1]=position.y;params->vieworg[2]=position.z;
            params->viewangles[0]=camera.angles.x;params->viewangles[1]=camera.angles.y;params->viewangles[2]=camera.angles.z;
            gEngfuncs.pfnAngleVectors(params->viewangles,params->forward,params->right,params->up);
        }
        if(motion_capture.is_open()) {
            if(now>=motion_end)motion_capture.close();
            else motion_capture<<now-motion_start<<','<<minecraft_feet.x<<','<<minecraft_feet.y<<','<<minecraft_feet.z<<','<<pose.feet.x<<','<<pose.feet.y<<','<<pose.feet.z<<','<<pose.eye<<','<<pose_sequence<<','<<input_sequence<<','<<last_input_buttons<<','<<last_input_forward<<','<<last_input_side<<','<<input_latency_ms<<'\n';
        }
    }
    const auto* distortion=link.view_distortion()&&MatchingHostProtocol()?view_distortion_frames.active(world,player_serial,server_player_life,now):nullptr;
    const auto fallback=MatchingHostProtocol()&&params->health>0&&!params->spectator
        ?native_nausea.sample(world,player_slot,player_serial,server_player_life,now):status_nausea::Sample{};
    const auto selected=status_nausea::select(distortion,fallback,link.view_distortion());
    render::update_scene_distortion({selected.intensity,selected.angle});
    const auto* vision=native_vision.active(world,player_slot,player_serial,server_player_life,now);
    render::update_scene_vision(vision?MetaRendererSceneVision{vision->night,vision->blindness,vision->darkness,vision->pulse}:MetaRendererSceneVision{});
    observed_origin={params->simorg[0],params->simorg[1],params->simorg[2]};
    std::copy_n(params->vieworg, 3, native_view_origin.begin());
    observed_angles={params->viewangles[0],params->viewangles[1],params->viewangles[2]};
    have_view=true;
}
int UpdateClientData(client_data_t* data,float time){
    const int result=gExportfuncs.HUD_UpdateClientData?gExportfuncs.HUD_UpdateClientData(data,time):1;
    if(!data||!CameraFresh())return result;
    const float vertical=camera_interpolator.at(Seconds()).vertical_fov;
    const bool vertical_input=gEngfuncs.pfnGetCvarFloat("r_vertical_fov")>0;
    float aspect=viewport_height?static_cast<float>(viewport_width)/viewport_height:4.0f/3;
    const int wide=static_cast<int>(gEngfuncs.pfnGetCvarFloat("gl_widescreen_yfov"));
    const bool standard=std::abs(aspect-4.0f/3)<0.001f||std::abs(aspect-1.25f)<0.001f;
    float factor=1;
    if(vertical_input){if(!standard&&wide==1&&aspect<4.0f/3)factor=aspect/(4.0f/3);}
    else factor=!standard&&wide==2?4.0f/3:(!standard&&wide==1?std::min(aspect,4.0f/3):aspect);
    constexpr float radians=0.0174532925199433f;
    data->fov=std::clamp(2*std::atan(std::tan(vertical*radians/2)*factor)/radians,1.0f,178.0f);
    return 1;
}
void RebuildSectionFootprint(Section& section) noexcept {
    section.atlas_footprint_generation=0;section.atlas_footprint={};
    if((section.flags&1)||section.vertices.empty())return;
    if(!atlas_generation){section.atlas_footprint.unknown();return;}
    if(section.atlas_footprint.assign_quads(std::span<const Vertex>(section.vertices),atlas_width,atlas_height))
        section.atlas_footprint_generation=atlas_generation;
}
void InvalidateAlphaShadows(const AtlasFootprint& changed) noexcept {
    ++static_shadow_alpha_changed_packets;
    // A section received before the full atlas must not look unreferenced.
    // Unknown UV dependencies force the existing complete-cache fallback.
    for(const auto& [key,section]:sections)if(!(section.flags&1)&&!section.vertices.empty()&&
        section.atlas_footprint_generation!=atlas_generation){
        ++static_shadow_alpha_fallbacks;++static_shadow_alpha_invalidations;
        render::invalidate_static_scene();return;
    }
    unsigned affected=0;
    for(const auto& [key,section]:sections){
        if((section.flags&1)||section.vertices.empty()||!section.atlas_footprint.intersects(changed))continue;
        ++affected;++static_shadow_alpha_affected_sections;
        render::invalidate_static_scene(&section.bounds);
        if(!section.bounds.valid()){++static_shadow_alpha_fallbacks;break;}
    }
    if(affected)++static_shadow_alpha_invalidations;
    else ++static_shadow_alpha_unreferenced_packets;
}
void ReadAtlas(Reader& r) {
    const auto epoch=r.u64(),generation=r.u64();if(epoch!=world)return;
    auto width=r.u32(),height=r.u32();
    if(!generation||!width||!height||width>4096||height>4096||std::uint64_t(width)*height*4!=r.remaining()) throw ProtocolError("invalid atlas dimensions");
    auto pixels=r.bytes(r.remaining());
    std::vector<std::uint8_t> next_alpha(pixels.size()/4);
    for(std::size_t i=0;i<next_alpha.size();++i)next_alpha[i]=pixels[i*4+3];
    render::invalidate_static_scene();atlas_alpha.clear();atlas_pixel_tiles.clear();atlas_generation=0;
    render::upload_texture(atlas_texture,atlas_width,atlas_height,width,height,pixels);
    render::validate_atlas_upload();atlas_alpha=std::move(next_alpha);
    atlas_pixel_tiles.reset(width,height);atlas_generation=generation;
    for(auto& [key,section]:sections)RebuildSectionFootprint(section);
    ++atlas_uploads;last_atlas_animation=0;
    if(!atlas_logged){Log("uploaded Minecraft block atlas "+std::to_string(width)+"x"+std::to_string(height));atlas_logged=true;}
}
void ReadAtlasPatches(Reader& r) {
    const auto epoch=r.u64(),generation=r.u64();
    if(epoch!=world||!atlas_generation||generation!=atlas_generation)return;
    const auto patches=read_atlas_patches(r,atlas_width,atlas_height);
    AtlasFootprint changed;
    if(!atlas_pixel_tiles.changes(atlas_alpha,atlas_width,atlas_height,patches,changed)){
        ++static_shadow_alpha_fallbacks;++static_shadow_alpha_invalidations;
        render::invalidate_static_scene();atlas_alpha.clear();atlas_pixel_tiles.clear();atlas_generation=0;
        throw ProtocolError("Missing complete atlas alpha state");
    }
    const bool alpha_changed=!changed.empty();
    if(alpha_changed)InvalidateAlphaShadows(changed);
    try{render::upload_atlas_patches(atlas_texture,patches);render::validate_atlas_upload();}
    catch(...){render::invalidate_static_scene();atlas_alpha.clear();atlas_pixel_tiles.clear();atlas_generation=0;throw;}
    if(alpha_changed&&atlas_alpha.size()==std::size_t(atlas_width)*atlas_height)
        for(const auto& patch:patches)for(unsigned y=0;y<patch.height;++y)for(unsigned x=0;x<patch.width;++x)
            atlas_alpha[std::size_t(patch.y+y)*atlas_width+patch.x+x]=patch.rgba[(std::size_t(y)*patch.width+x)*4+3];
    ++atlas_animation_packets;for(const auto& patch:patches)atlas_patch_bytes+=patch.rgba.size();
    const double now=Seconds();if(last_atlas_animation>0)atlas_animation_interval_ms=(now-last_atlas_animation)*1000;last_atlas_animation=now;
}
void ReadSection(Reader& r) {
    auto epoch=r.u64(); std::array<std::int32_t,4> key{r.i32(),r.i32(),r.i32(),0};
    auto revision=r.u64(); auto flags=r.u32(); auto count=r.u32();
    if(epoch!=world) return;
    if(count>262144 || count%4 || r.remaining()!=std::size_t(count)*24 || flags>3) throw ProtocolError("invalid section mesh");
    key[3]=static_cast<std::int32_t>(flags);
    if(sections.size()>=2048&&!sections.contains(key)) throw ProtocolError("section limit");
    auto existing=sections.find(key); if(existing!=sections.end()&&existing->second.revision>=revision) return;
    std::size_t stored=0;
    for(const auto& [stored_key,section] : sections) stored+=section.vertices.size()*sizeof(Vertex);
    if(existing!=sections.end()) stored-=existing->second.vertices.size()*sizeof(Vertex);
    if(stored+std::size_t(count)*sizeof(Vertex)>128*1024*1024) throw ProtocolError("x86 mesh memory budget exceeded");
    Section section{revision,flags,{}}; section.vertices.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        Vec3 mc{r.f32(),r.f32(),r.f32()}; auto gs=to_goldsrc(mc);
        Vertex vertex{gs.x,gs.y,gs.z,r.f32(),r.f32(),r.u32()}; section.vertices.push_back(vertex);
        section.bounds.add(gs.x,gs.y,gs.z);
    }
    r.finish();
    RebuildSectionFootprint(section);
    if(count&&existing!=sections.end())section.gpu=existing->second.gpu;
    // Both volumes matter when a replacement moves or removes an occluder.
    // The complete frame is validated before touching the accepted mesh/cache.
    if(!(flags&1)){
        if(existing!=sections.end()&&!existing->second.vertices.empty())render::invalidate_static_scene(&existing->second.bounds);
        if(!section.vertices.empty())render::invalidate_static_scene(&section.bounds);
    }
    sections[key]=std::move(section);
    if(count && !section_logged){section_logged=true;Log("received first nonempty Minecraft section mesh, vertices="+std::to_string(count));}
}
void ReadRemoveSection(Reader& r){
    const auto epoch=r.u64();std::array<std::int32_t,4> key{r.i32(),r.i32(),r.i32(),0};r.finish();
    if(epoch!=world)return;
    for(int layer=0;layer<4;++layer){
        key[3]=layer;auto existing=sections.find(key);if(existing==sections.end())continue;
        if(!(existing->second.flags&1)&&!existing->second.vertices.empty())render::invalidate_static_scene(&existing->second.bounds);
        sections.erase(existing);
    }
}
void ReadEntityTexture(Reader& r) {
    auto epoch=r.u64();auto key=r.u32(),width=r.u32(),height=r.u32();if(epoch!=world)return;
    if(!key||key>256||!width||!height||width>2048||height>2048||std::uint64_t(width)*height*4!=r.remaining())throw ProtocolError("invalid entity texture");
    std::size_t size=std::size_t(width)*height*4;for(const auto& [number,texture]:entity_textures)if(number!=key)size+=std::size_t(texture.width)*texture.height*4;
    if(size>128*1024*1024)throw ProtocolError("entity texture memory budget exceeded");
    auto pixels=r.bytes(r.remaining());auto& texture=entity_textures[key];
    render::upload_texture(texture.id,texture.width,texture.height,width,height,pixels);
    ++entity_texture_uploads;entity_texture_bytes+=pixels.size();
}
void ReadEntityFrame(Reader& r,bool outlines=false) {
    if(outlines&&!link.entity_outlines())throw ProtocolError("Unnegotiated entity outline frame");
    auto frame=read_entity_frame(r,outlines);const auto& base=frame.base;const auto& effects=frame.effects;
    if(!MatchingHostProtocol()||effects.life!=server_player_life||
       !current_entity_frame(frame,world,particle_generation,entity_revision,minecraft_life))return;
    // HUD_VidInit can invalidate textures while IPC is still delivering the old scene.
    // The complete scene replay sends its atlas, then textures, then meshes in order.
    if(!atlas_generation){++pending_scene_meshes;return;}
    validate_entity_frame_textures(frame,particle_generation,
        [](unsigned key){return entity_textures.contains(key);},[](unsigned key){return particle_textures.contains(key);});
    std::vector<EntityBatch> next;next.reserve(base.batches.size());
    for(const auto& source:base.batches) {
        EntityBatch batch{source.texture,source.flags,{}};batch.vertices.reserve(source.vertices.size());
        const auto i=next.size();
        if(i<entity_batches.size()&&entity_batches[i].texture==source.texture&&entity_batches[i].flags==source.flags)batch.gpu=entity_batches[i].gpu;
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color,v.normal});batch.bounds.add(p.x,p.y,p.z);}
        next.push_back(std::move(batch));
    }
    std::vector<EffectBatch> next_effects;next_effects.reserve(effects.batches.size());unsigned effect_vertices=0;
    for(const auto& source:effects.batches){
        EffectBatch batch{source.material,{}};batch.vertices.reserve(source.vertices.size());const auto i=next_effects.size();
        if(effects.generation==entity_effect_generation&&i<entity_effect_batches.size()&&entity_effect_batches[i].material==source.material)
            batch.gpu=entity_effect_batches[i].gpu;
        if(!batch.gpu)batch.gpu=std::make_shared<render::Mesh>();
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color});}
        effect_vertices+=static_cast<unsigned>(source.vertices.size());next_effects.push_back(std::move(batch));
    }
    const double now=Seconds();if(last_entity_frame)entity_interval_ms=(now-last_entity_frame)*1000;
    // Every parse, texture check and allocation finished before either half changes.
    if(effects.generation!=particle_generation){ClearParticles();particle_generation=effects.generation;}
    entity_batches=std::move(next);entity_effect_batches=std::move(next_effects);rendered_players=std::move(frame.base.avatars);
    last_entity_frame=now;entity_revision=base.revision;entity_count=base.count;block_entity_count=base.blocks;
    entity_effect_generation=effects.generation;entity_life=effects.life;entity_effect_vertices=effect_vertices;++entity_frames;
    if(entity_capture.is_open()){
        if(now>entity_capture_end)entity_capture.close();
        else for(const auto& batch:entity_batches)if(!batch.vertices.empty()){
            double x=0,y=0,z=0;
            for(const auto& vertex:batch.vertices){x+=vertex.x;y+=vertex.y;z+=vertex.z;}
            const auto count=batch.vertices.size();
            entity_capture<<now-entity_capture_start<<','<<entity_revision<<','<<batch.texture<<','<<batch.flags<<','<<count
                          <<','<<x/count<<','<<y/count<<','<<z/count<<'\n';
        }
    }
}
void ReadParticleTexture(Reader& r){
    const auto epoch=r.u64(),generation=r.u64();const auto key=r.u32(),width=r.u32(),height=r.u32();
    if(epoch!=world||generation<particle_generation)return;
    if(!generation||!key||key>max_particle_textures||!width||!height||width>2048||height>2048||std::uint64_t(width)*height*4!=r.remaining())
        throw ProtocolError("Invalid particle texture");
    std::size_t size=std::size_t(width)*height*4;
    if(generation==particle_generation)for(const auto& [number,texture]:particle_textures)if(number!=key)size+=std::size_t(texture.width)*texture.height*4;
    if(size>max_particle_texture_resident_bytes)throw ProtocolError("Particle texture memory budget exceeded");
    if(generation!=particle_generation){ClearParticles();particle_generation=generation;}
    auto& texture=particle_textures[key];render::upload_texture(texture.id,texture.width,texture.height,width,height,r.bytes(r.remaining()));
}
void ReadParticleTextureChunk(Reader& r){
    const auto chunk=read_particle_texture_chunk(r);
    // Like unchunked particle textures, immutable image resources are scoped
    // to the map and resource generation. The captured life remains identical
    // within a transfer, but does not authorize any avatar/effect presentation.
    if(chunk.epoch!=world||chunk.generation<particle_generation||!MatchingHostProtocol())return;
    if(chunk.generation!=particle_generation){ClearParticles();particle_generation=chunk.generation;}
    if(chunk.revision<=particle_texture_transfer)throw ProtocolError("Retired particle texture transfer");
    std::size_t resident=chunk.total_bytes;
    for(const auto& [key,texture]:particle_textures)if(key!=chunk.key)
        resident+=std::size_t(texture.width)*texture.height*4;
    if(resident>max_particle_texture_resident_bytes)throw ProtocolError("Particle texture memory budget exceeded");
    bool complete;
    {
        // The first append includes any staging growth and its first copy;
        // repeated transfers reuse storage without clearing the entire image.
        WorkScope work(chunk.offset==0?WorkStage::chunk_first_append:WorkStage::chunk_continuation_append);
        complete=particle_texture_upload.append(chunk);
    }
    if(!complete)return;
    // Keep the previously published image until the complete replacement has
    // GPU storage. Neither partial transfers nor allocation failures expose it.
    Texture replacement{};
    try{
        {
            // Host elapsed time includes driver waits and the existing error
            // check; this is not a GPU timer and introduces no GL synchronization.
            WorkScope work(WorkStage::chunk_gpu_upload);
            render::upload_texture(replacement.id,replacement.width,replacement.height,
                chunk.width,chunk.height,particle_texture_upload.pixels());
            if(glGetError()!=GL_NO_ERROR)throw ProtocolError("Particle texture GPU upload failed");
        }
        WorkScope work(WorkStage::chunk_publish_release);
        auto& published=particle_textures[chunk.key];
        std::swap(published,replacement);
        if(replacement.id)glDeleteTextures(1,&replacement.id);
        particle_texture_transfer=chunk.revision;++particle_texture_chunk_uploads;
        particle_texture_upload.finish();
    }catch(...){
        WorkScope work(WorkStage::chunk_failed_release);
        if(replacement.id)glDeleteTextures(1,&replacement.id);
        particle_texture_upload.reset();throw;
    }
}
void ReadBlockFeedback(Reader& r){
    const auto frame=read_block_feedback(r);
    if(!atlas_generation||frame.epoch!=world||frame.life!=minecraft_life||frame.life!=server_player_life||frame.revision<=feedback_revision)return;
    std::vector<Vertex> lines;lines.reserve(frame.lines.size());
    for(const auto& point:frame.lines){const auto p=to_goldsrc(point);lines.push_back({p.x,p.y,p.z,0,0,0x66000000});}
    std::vector<EntityBatch> cracks;
    for(const auto& source:frame.cracks){
        EntityBatch batch{0,source.stage,{}};batch.vertices.reserve(source.vertices.size());
        if(cracks.size()<crack_batches.size())batch.gpu=crack_batches[cracks.size()].gpu;
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color});}
        cracks.push_back(std::move(batch));
    }
    outline_vertices=std::move(lines);crack_batches=std::move(cracks);
    feedback_revision=frame.revision;feedback_life=frame.life;last_feedback=Seconds();++feedback_frames;
}
void ReadParticles(Reader& r){
    auto snapshot=read_particles(r);
    if(!current_particles(snapshot,world,particle_generation,particle_revision,minecraft_life))return;
    for(const auto& batch:snapshot.batches){
        const auto key=batch.material.texture;
        if(key==0&&!atlas_generation){++pending_scene_meshes;return;}
        if(key!=0&&key!=particle_untextured&&(snapshot.generation!=particle_generation||!particle_textures.contains(key)))
            throw ProtocolError("Missing current-generation particle texture");
    }
    if(snapshot.generation!=particle_generation){ClearParticles();particle_generation=snapshot.generation;}
    std::vector<EffectBatch> next;next.reserve(snapshot.batches.size());unsigned vertices=0;
    for(const auto& source:snapshot.batches){
        EffectBatch batch{source.material,{}};batch.vertices.reserve(source.vertices.size());
        auto i=next.size();if(i<particle_batches.size()&&particle_batches[i].material==batch.material)batch.gpu=particle_batches[i].gpu;
        if(!batch.gpu)batch.gpu=std::make_shared<render::Mesh>();
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color});}
        vertices+=static_cast<unsigned>(source.vertices.size());next.push_back(std::move(batch));
    }
    particle_batches=std::move(next);particle_revision=snapshot.revision;particle_count=snapshot.count;particle_vertices=vertices;particle_life=snapshot.life;
    const double now=Seconds();if(last_particle_frame)particle_interval_ms=(now-last_particle_frame)*1000;last_particle_frame=now;
    ++particle_frames;particle_bytes+=vertices*24;
}
void ReadPostEffects(Reader& r){
    auto frame=read_post_effects(r);
    const auto& snapshot=frame.mesh;
    if(!MatchingHostProtocol()||snapshot.life!=server_player_life||
       !current_particles(snapshot,world,particle_generation,post_generation==snapshot.generation?post_revision:0,minecraft_life))return;
    for(const auto& batch:snapshot.batches){
        const auto key=batch.material.texture;
        if(key==0&&!atlas_generation){ClearPostEffects();++pending_scene_meshes;return;}
        if(key!=0&&key!=particle_untextured&&(snapshot.generation!=particle_generation||!particle_textures.contains(key)))
            throw ProtocolError("Missing current-generation post mask texture");
    }
    if(snapshot.generation!=particle_generation){ClearParticles();particle_generation=snapshot.generation;}
    std::vector<EffectBatch> next;next.reserve(snapshot.batches.size());
    for(const auto& source:snapshot.batches){
        EffectBatch batch{source.material,{}};batch.vertices.reserve(source.vertices.size());
        const auto i=next.size();
        if(i<post_batches.size()&&post_batches[i].material==batch.material)batch.gpu=post_batches[i].gpu;
        if(!batch.gpu)batch.gpu=std::make_shared<render::Mesh>();
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color});}
        next.push_back(std::move(batch));
    }
    post_batches=std::move(next);post_stages=std::move(frame.stages);
    if(!post_stages.empty())++post_nonempty_frames;
    post_depth_resets.fill(false);for(auto index:frame.depth_resets)post_depth_resets[index]=true;
    post_revision=snapshot.revision;post_generation=snapshot.generation;post_life=snapshot.life;
    last_post_frame=Seconds();++post_frames;
}
void TestCommands() {
    const char* path=std::getenv("GOLDCRAFT_TEST_COMMAND");if(!path)return;
    static auto next_poll=std::chrono::steady_clock::now(),release=next_poll;
    static std::string stop_command;
    auto now=std::chrono::steady_clock::now();
    if(!stop_command.empty()&&now>=release){gEngfuncs.pfnClientCmd(stop_command.data());stop_command.clear();}
    if(now<next_poll)return;next_poll=now+std::chrono::milliseconds(50);
    std::ifstream input(path);std::uint64_t id=0;std::string action;
    if(!(input>>id>>action)||id<=test_command_id)return;test_command_id=id;
    if(action=="userinfo_set") {client_userinfo::test_command(input);return;}
    if(action=="view") {float yaw=0,pitch=0;if(input>>yaw>>pitch&&std::isfinite(yaw)&&std::isfinite(pitch)&&std::abs(pitch)<=89){float angles[3]{pitch,yaw,0};gEngfuncs.SetViewAngles(angles);}}
    else if(action=="capture")capture_requested=true;
    else if(action=="scene_resync")atlas_generation=0;
    else if(action=="menu_close")CloseMinecraftMenu();
    else if(action=="inventory")SendControl(10);
    else if(action=="form"){int form=0;if(input>>form&&form>=0&&form<=1){auto command="goldcraft_form "+std::to_string(form)+"\n";gEngfuncs.pfnServerCmd(command.data());}}
    else if(action=="toggle_form")ToggleForm();
    else if(action=="engine_key"){int key=0;if(input>>key&&key>=1&&key<=255){gEngfuncs.Key_Event(key,1);gEngfuncs.Key_Event(key,0);}}
    else if(action=="engine_key_state"){int key=0,down=0;if(input>>key>>down&&key>=1&&key<=255&&down>=0&&down<=1)gEngfuncs.Key_Event(key,down);}
    else if(action=="resume_game")host_ui::resume_game();
    else if(action=="hideconsole")gEngfuncs.pfnClientCmd(const_cast<char*>("hideconsole\n"));
    else if(action=="resource_reload")ReloadMinecraftResources();
    else if(action=="sound_test")gEngfuncs.pfnPlaySoundByName(const_cast<char*>("buttons/blip1.wav"),0.5f);
    else if(action=="host_use_stale"){
        int kind=0;if(input>>kind&&(kind==0||kind==1)&&world&&minecraft_life){
            auto command="goldcraft_use "+std::to_string(world-(kind==0?1:0))+" "+std::to_string(minecraft_life-(kind==1?1:0))+" 1\n";
            gEngfuncs.pfnServerCmd(command.data());
        }
    }
    else if(action=="hud"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("mc_hud",static_cast<float>(enabled));}
    else if(action=="hud_scale"){int scale=0;if(input>>scale&&scale>=0&&scale<=8)gEngfuncs.Cvar_SetValue("mc_gui_scale",static_cast<float>(scale));}
    else if(action=="particles"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("mc_particles",static_cast<float>(enabled));}
    else if(action=="scene_cull"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("mc_scene_cull",static_cast<float>(enabled));}
    else if(action=="slot"){int slot=0;if(input>>slot&&slot>=1&&slot<=9)SendControl(slot);}
    else if(action=="ui_move"){float x=0,y=0;if(input>>x>>y&&x>=0&&x<=1&&y>=0&&y<=1){ui_x=x;ui_y=y;SendUi(1);}}
    else if(action=="ui_click"){int button=0;if(input>>button&&button>=0&&button<=2){SendUi(2,button,1);SendUi(2,button,0);}}
    else if(action=="ui_scroll"){float amount=0;if(input>>amount&&std::abs(amount)<=16)SendUi(3,0,0,amount);}
    else if(action=="ui_key"){int key=0,mods=0;if(input>>key>>mods&&key>=32&&key<=348&&mods>=0&&mods<=7){const auto before=ui_modifiers;ui_modifiers=mods;SendUi(4,key,1);SendUi(4,key,0);ui_modifiers=before;}}
    else if(action=="ui_char"){int character=0;if(input>>character&&character>=32&&character<=0x10ffff&&character!=127&&(character<0xd800||character>0xdfff))SendUi(5,character);}
    else if(action=="kill")gEngfuncs.pfnClientCmd(const_cast<char*>("kill\n"));
    else if(action=="team"){int team=0;if(input>>team&&team>=1&&team<=2){std::string command="jointeam "+std::to_string(team)+"\njoinclass 1\n";gEngfuncs.pfnClientCmd(command.data());}}
    else if(action=="observer")gEngfuncs.pfnClientCmd("jointeam 6\n");
    else if(action=="disconnect")gEngfuncs.pfnClientCmd(const_cast<char*>("disconnect\n"));
    else if(action=="reconnect")gEngfuncs.pfnClientCmd(const_cast<char*>("retry\n"));
    else if(action=="loopback_connect"){
        int port=0;if(input>>port&&port>=1024&&port<=65535){
            auto command="connect 127.0.0.1:"+std::to_string(port)+"\n";
            gEngfuncs.pfnClientCmd(command.data());
        }
    }
    else if(action=="avatars"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("mc_replace_players",static_cast<float>(enabled));}
    else if(action=="smoothing"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("mc_view_smoothing",static_cast<float>(enabled));}
    else if(action=="shadows"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("r_shadow",static_cast<float>(enabled));}
    else if(action=="render_options"){
        int deferred=1,diagnostics=0;
        if(input>>deferred>>diagnostics&&deferred>=0&&deferred<=1&&diagnostics>=0&&diagnostics<=2){
            gEngfuncs.Cvar_SetValue("r_deferred_lighting",static_cast<float>(deferred));
            gEngfuncs.Cvar_SetValue("r_studio_diagnostics",static_cast<float>(diagnostics));
        }
    }
    else if(action=="benchmark_framerate"){int fps=0;if(input>>fps)SetBenchmarkFramerate(fps);}
    else if(action=="render_profile"){
        float seconds=0;int compare=0;
        if(input>>seconds&&std::isfinite(seconds)&&seconds>0&&seconds<=30){
            input>>compare;
            if(compare>=0&&compare<=1){
                gEngfuncs.Cvar_SetValue("r_renderer_profile_compare",static_cast<float>(compare));
                gEngfuncs.Cvar_SetValue("r_renderer_profile",seconds);
            }
        }
    }
    else if(action=="render_shadow_cull"){
        int enabled=1;
        if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("r_shadow_caster_cull",static_cast<float>(enabled));
    }
    else if(action=="render_studio_face_cull"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_studio_shadow_face_cull"))
            gEngfuncs.Cvar_SetValue("r_studio_shadow_face_cull",static_cast<float>(enabled));
    }
    else if(action=="render_studio_depth_executor"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_studio_shadow_depth_executor"))
            gEngfuncs.Cvar_SetValue("r_studio_shadow_depth_executor",static_cast<float>(enabled));
    }
    else if(action=="render_studio_bone_palette_cache"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_studio_bone_palette_cache"))
            gEngfuncs.Cvar_SetValue("r_studio_bone_palette_cache",static_cast<float>(enabled));
    }
    else if(action=="render_world_material_batch"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_world_material_batch"))
            gEngfuncs.Cvar_SetValue("r_world_material_batch",static_cast<float>(enabled));
    }
    else if(action=="render_studio_shadow_plan"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_studio_shadow_plan"))
            gEngfuncs.Cvar_SetValue("r_studio_shadow_plan",static_cast<float>(enabled));
    }
    else if(action=="render_draw_parameter_stream"){
        int enabled=0;
        if(input>>enabled&&enabled>=0&&enabled<=1&&gEngfuncs.pfnGetCvarPointer("r_draw_parameter_stream"))
            gEngfuncs.Cvar_SetValue("r_draw_parameter_stream",static_cast<float>(enabled));
    }
    else if(action=="scene_static_shadows"){
        int enabled=1;
        if(input>>enabled&&enabled>=0&&enabled<=1){
            gEngfuncs.Cvar_SetValue("mc_scene_static_shadows",static_cast<float>(enabled));UpdateStaticScene();
        }
    }
    else if(action=="native_profile"){
        float seconds=0;
        if(input>>seconds&&std::isfinite(seconds)&&seconds>=3&&seconds<=30&&!native_profile_end&&
           std::getenv("GOLDCRAFT_MOTION_CAPTURE")){
            const double cpu=NativeThreadCpuMs();
            if(cpu>=0){
                native_profile.clear();native_profile.reserve(8192);
                native_profile_start=Seconds();native_profile_end=native_profile_start+seconds;native_profile_cpu=cpu;
            }
        }
    }
    else if(action=="native_profile_stop"){native_profile_end=0;native_profile.clear();}
    else if(action=="native_mlook"){
        int enabled=-1;
        if(input>>enabled&&(enabled==0||enabled==1)&&NativeMouseLookState()>=0)
            gEngfuncs.pfnClientCmd(enabled?"+mlook\n":"-mlook\n");
    }
    else if(action=="native_developer"){
        int enabled=-1;
        if(input>>enabled&&(enabled==0||enabled==1))gEngfuncs.Cvar_SetValue("developer",float(enabled));
    }
    // Fixed-name native demo commands for repeatable Renderer comparisons.
    // This opt-in test channel is configured only by the sandbox launcher.
    else if(action=="render_demo_record")gEngfuncs.pfnClientCmd("record goldcraft_native_reference\n");
    else if(action=="render_demo_stop")gEngfuncs.pfnClientCmd("stop\n");
    else if(action=="render_demo_play")gEngfuncs.pfnClientCmd("playdemo goldcraft_native_reference\n");
    else if(action=="render_demo_end")gEngfuncs.pfnClientCmd("stopdemo\n");
    else if(action=="visible_fixture"){
        int count=0;double seconds=0;
        if(input>>count>>seconds)visible_entities::fixture_start(count,seconds);
    }
    else if(action=="visible_fixture_clear")visible_entities::fixture_clear();
    else if(action=="framerate"){int fps=100,vsync=0;if(input>>fps>>vsync&&fps>=30&&fps<=100&&vsync>=0&&vsync<=1){gEngfuncs.Cvar_SetValue("fps_max",static_cast<float>(fps));gEngfuncs.Cvar_SetValue("gl_vsync",static_cast<float>(vsync));}}
    else if(action=="profile") {
        double seconds=0;if(input>>seconds&&seconds>0&&seconds<=30)if(const char* destination=std::getenv("GOLDCRAFT_MOTION_CAPTURE")){
            if(motion_capture.is_open())motion_capture.close();motion_capture.open(destination);motion_start=Seconds();motion_end=motion_start+seconds;
            motion_capture<<"time,raw_x,raw_y,raw_z,view_x,view_y,view_z,eye,pose_sequence,input_sequence,buttons,forward,side,input_echo_ms\n";
        }
    }
    else if(action=="entity_profile"){
        double seconds=0;if(input>>seconds&&seconds>0&&seconds<=30)if(const char* path=std::getenv("GOLDCRAFT_MOTION_CAPTURE")){
            if(entity_capture.is_open())entity_capture.close();
            entity_capture.open(std::filesystem::path(path).parent_path()/"goldcraft-entities.csv");
            entity_capture_start=Seconds();entity_capture_end=entity_capture_start+seconds;
            entity_capture<<"time,revision,texture,flags,vertices,center_x,center_y,center_z\n";
        }
    }
    else {
        float seconds=0;
        std::vector<std::string> actions;
        if(action=="forward_duck")actions={"forward","duck"};
        else if(action=="host_use")actions={"goldcraft_use"};
        else if(action=="use"||action=="reload")actions={action};
        else if(action=="forward_jump")actions={"forward","jump"};
        else if(action=="forward_left")actions={"forward","moveleft"};
        else if(action=="forward"||action=="back"||action=="moveleft"||action=="moveright"||action=="jump"||action=="duck"||action=="attack"||action=="attack2")actions={action};
        if(!actions.empty()&&input>>seconds&&seconds>0&&seconds<=2) {
            if(!stop_command.empty())gEngfuncs.pfnClientCmd(stop_command.data());
            std::string command;stop_command.clear();
            for(const auto& key:actions){command+="+"+key+"\n";stop_command+="-"+key+"\n";}
            gEngfuncs.pfnClientCmd(command.data());release=now+std::chrono::milliseconds(static_cast<int>(seconds*1000));
        }
    }
    Log("sandbox CS command "+std::to_string(id)+": "+action);
}
void Frame(double time) {
    WorkScope frame_timing(WorkStage::frame);
    const double began=Seconds();
    steam_async::frame();
    client_userinfo::frame();
    control_trace.observe(SampleControl());
    {WorkScope timing(WorkStage::native_hud);gExportfuncs.HUD_Frame(time);}
    {WorkScope timing(WorkStage::audit_commands);input_audit::sample();TestCommands();}
    {WorkScope timing(WorkStage::transport);link.poll();}
    UpdateLocalDiscovery();
    CheckBenchmarkFramerate();
    static double next_ready=0;
    {
    WorkScope timing(WorkStage::map_edits);
    if(MatchingHostProtocol()){
        if(have_view&&Seconds()>=next_ready){
            const auto command="goldcraft_ready "+std::to_string(protocol_version)+"\n";
            gEngfuncs.pfnServerCmd(command.c_str());next_ready=Seconds()+1;
        }
        try {
            auto* entity=gEngfuncs.GetEntityByIndex(0);
            map_edits.pump(render::world_edit(),entity?entity->model:nullptr,render::world_edit2());
        } catch(const std::exception& error) { Log(error.what());edit_recovery=true; }
        if(edit_recovery)RequestEdits();
    }else if(map_edits.state().epoch){
        map_edits.reset(0);edit_assembler.reset(0);client_map::reset();edit_recovery=false;
    }
    }
    {
    WorkScope timing(WorkStage::connection);
    if(link.error()!=last_error) { last_error=link.error(); if(!last_error.empty()) Log(last_error); }
    if(link.connected()&&link.generation()!=last_link_generation) {
        ReleaseHostUse();
        last_link_generation=link.generation(); ClearSections();ClearLights();ClearDynamic();ClearHud();view_interpolator.clear();atlas_generation=0;minecraft_control=false;section_logged=draw_logged=atlas_logged=false;
        if(!binding.empty()) link.send(Type::client_binding,binding);
        Log("Fabric client connected with an isolated session");
    }
    if(!link.connected()&&particle_texture_upload.retained_bytes())particle_texture_upload.reset();
    }
    const auto diagnostic_time=std::chrono::steady_clock::now();
    if(diagnostic_time>=next_diagnostic){
        WorkScope timing(WorkStage::diagnostics);
        next_diagnostic=diagnostic_time+std::chrono::milliseconds(std::getenv("GOLDCRAFT_TEST_COMMAND")?100:2000);WriteDiagnostics();
    }
    {
    WorkScope timing(WorkStage::messages);
    for(auto& message : link.take_messages()) {
        WorkScope message_timing(MessageWork(message.type));
        try {
            Reader r(message.payload);
            if(message.type==Type::atlas) ReadAtlas(r);
            else if(message.type==Type::atlas_patches)ReadAtlasPatches(r);
            else if(message.type==Type::entity_texture)ReadEntityTexture(r);
            else if(message.type==Type::entity_frame)ReadEntityFrame(r);
            else if(message.type==Type::entity_outline_frame)ReadEntityFrame(r,true);
            else if(message.type==Type::particle_texture)ReadParticleTexture(r);
            else if(message.type==Type::particle_texture_chunk){
                try{ReadParticleTextureChunk(r);}catch(...){particle_texture_upload.reset();throw;}
            }
            else if(message.type==Type::particle_mesh)ReadParticles(r);
            else if(message.type==Type::post_effect_plan){
                try{ReadPostEffects(r);}catch(...){ClearPostEffects();throw;}
            }
            else if(message.type==Type::block_feedback)ReadBlockFeedback(r);
            else if(message.type==Type::view_distortion){
                const auto frame=view_distortion::read(r);
                if(link.view_distortion()&&MatchingHostProtocol()&&minecraft_life==server_player_life)
                    view_distortion_frames.accept(frame,world,player_serial,server_player_life,Seconds());
            }
            else if(message.type==Type::camera){
                const auto frame=read_camera(r);
                if(frame.epoch==world&&frame.life==server_player_life&&frame.life==minecraft_life&&frame.revision>camera_revision){
                    if(camera_life!=frame.life)camera_interpolator.clear();
                    const auto now=Seconds();
                    if(camera_interpolator.push(frame.pose,static_cast<double>(frame.produced)*1e-9,now)){
                        camera_pose=frame.pose;camera_life=frame.life;camera_revision=frame.revision;last_camera=now;++camera_frames;
                    }
                }
            }
            else if(message.type==Type::hud_frame)ReadHud(r);
            else if(message.type==Type::scene_reset){auto epoch=r.u64();r.finish();if(epoch==world){ClearSections();ClearLights();ClearDynamic();atlas_generation=0;}}
            else if(message.type==Type::player_pose) {
                auto epoch=r.u64(),sequence=r.u64(),produced=r.u64();auto flags=r.u32(),life=r.u32();Vec3 feet{r.f32(),r.f32(),r.f32()};float eye=r.f32();r.finish();
                if(epoch==world&&life==server_player_life&&sequence>=pose_sequence&&eye>=0&&eye<=4&&(flags&~9u)==0){
                    if(!HasControl()||life!=minecraft_life){ReleaseHostUse();view_interpolator.clear();}
                    minecraft_life=life;
                    view_interpolator.push({feet,eye},static_cast<double>(produced)*1e-9,Seconds());
                    pose_sequence=sequence;minecraft_feet=feet;minecraft_eye=eye;minecraft_control=(flags&1)!=0;minecraft_menu=(flags&8)!=0;last_pose=std::chrono::steady_clock::now();
                    const auto& sent=input_times[sequence%input_times.size()];if(sent.first==sequence)input_latency_ms=std::chrono::duration<float,std::milli>(last_pose-sent.second).count();
                }
            }
            else if(message.type==Type::lights) ReadLights(r);
            else if(message.type==Type::section_mesh) ReadSection(r);
            else if(message.type==Type::remove_section) ReadRemoveSection(r);
        } catch(const std::exception& e) { Log(e.what()); }
    }
    }
    {
    WorkScope timing(WorkStage::control_lights);
    control_trace.observe(SampleControl());
    SendViewport();UpdateUi();UpdateViewModel();UpdateHostUse();
    // A video/context reset invalidates the local atlas without reconnecting IPC.
    // Request a complete scene instead of relying on periodic full-texture readbacks.
    if(link.connected()&&world&&!atlas_generation&&Seconds()>=next_scene_request){
        Writer request;request.u64(world);link.send(Type::scene_reset,request.data);next_scene_request=Seconds()+1;
    }
    UpdateLights();UpdateStaticScene();
    }
    frame_work_ms=(Seconds()-began)*1000;frame_work_max_ms=std::max(frame_work_max_ms,frame_work_ms);
}
void CreateMove(float frame_time,usercmd_t* cmd,int active) {
    WorkScope work(WorkStage::create_move);
    // GoldSrc's active argument describes a game connection, not OS focus.
    // Keep each paired MC runtime's input/audio lease tied to its own window.
    DWORD foreground_process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
    window_focused=foreground_process==GetCurrentProcessId();
    {WorkScope ui(WorkStage::ui_update);UpdateUi();}
    {WorkScope capture(WorkStage::mouse_capture);host_ui::update_mouse_capture(active&&window_focused,gEngfuncs.pfnGetCvarFloat("m_rawinput")!=0);}
    // Keep GoldSrc's normal relative mouse capture, converting its look delta to a GUI cursor.
    // A temporary zero angle avoids losing cursor motion at pitch/yaw clamps.
    const bool mc_look=HasControl();
    const bool relative_look=mc_look&&active&&window_focused&&!ui_active
        &&!gEngfuncs.Con_IsVisible()&&!host_ui::game_menu_active();
    float restored_angles[3]{},look_pitch=0,look_yaw=0;
    if(mc_look){
        gEngfuncs.GetViewAngles(restored_angles);
        if(CameraFresh()){
            restored_angles[0]=camera_pose.angles.x;restored_angles[1]=camera_pose.angles.y;restored_angles[2]=0;
        }
    }
    if(relative_look){float zero[3]{};gEngfuncs.SetViewAngles(zero);}
    if(ui_active){float zero[3]{};gEngfuncs.SetViewAngles(zero);}
    {WorkScope native(WorkStage::native_create_move);gExportfuncs.CL_CreateMove(frame_time,cmd,active);}
    if(relative_look){
        look_pitch=cmd->viewangles[0];look_yaw=std::remainder(cmd->viewangles[1],360.0f);
        gEngfuncs.SetViewAngles(restored_angles);
        std::copy(std::begin(restored_angles),std::end(restored_angles),cmd->viewangles);
    }
    input_buttons_observed|=cmd->buttons;
    if(ui_active){
        const float old_x=ui_x,old_y=ui_y;
        const float sensitivity=std::max(0.1f,gEngfuncs.pfnGetCvarFloat("sensitivity"));
        const float yaw_scale=sensitivity*gEngfuncs.pfnGetCvarFloat("m_yaw"),pitch_scale=sensitivity*gEngfuncs.pfnGetCvarFloat("m_pitch");
        if(active&&viewport_width&&viewport_height){
            if(std::abs(yaw_scale)>0.00001f)ui_x=std::clamp(ui_x-std::remainder(cmd->viewangles[1],360.0f)/(yaw_scale*viewport_width),0.0f,1.0f);
            if(std::abs(pitch_scale)>0.00001f)ui_y=std::clamp(ui_y+std::remainder(cmd->viewangles[0],360.0f)/(pitch_scale*viewport_height),0.0f,1.0f);
        }
        gEngfuncs.SetViewAngles(ui_angles);std::copy(std::begin(ui_angles),std::end(ui_angles),cmd->viewangles);
        if(ui_x!=old_x||ui_y!=old_y)SendUi(1);
        cmd->buttons=0;cmd->forwardmove=cmd->sidemove=cmd->upmove=0;
    }
    const bool input_active=active&&window_focused&&!gEngfuncs.Con_IsVisible()&&!host_ui::game_menu_active();
    if(!input_active)menu_pressed.fill(false);
    if(!input_active||!HasControl()||ui_active||gEngfuncs.Con_IsVisible()||host_ui::game_menu_active())ReleaseMinecraftKeys();
    // Preserve the user's actual +use binding; only its destination changes
    // while MC owns movement. R/+reload and all CS-form commands remain native.
    if(HasControl()&&input_active&&(cmd->buttons&IN_USE)&&!ui_active)host_use_sources|=2;
    else host_use_sources&=~2u;
    if(!input_active)ReleaseHostUse();else UpdateHostUse();
    last_input_active=input_active;last_input_buttons=input_active?cmd->buttons:0;last_input_forward=input_active?cmd->forwardmove:0;last_input_side=input_active?cmd->sidemove:0;
    if(!link.connected()||!world) {
        if(MinecraftFormWaiting()){cmd->forwardmove=cmd->sidemove=cmd->upmove=0;cmd->buttons=0;cmd->impulse=0;}
        return;
    }
    WorkScope forward(WorkStage::input_forward);
    Writer w; w.u64(world);w.u64(++input_sequence); w.f32(frame_time); w.u32((input_active?1u:0u)|(mc_look?2u:0u)); w.u32(last_input_buttons);
    input_times[input_sequence%input_times.size()]={input_sequence,std::chrono::steady_clock::now()};
    w.f32(mc_look?look_pitch:cmd->viewangles[0]); w.f32(mc_look?look_yaw:cmd->viewangles[1]); w.f32(last_input_forward); w.f32(last_input_side); w.f32(input_active?cmd->upmove:0);
    link.send(Type::input,w.data);
    if(MinecraftFormWaiting()){cmd->forwardmove=cmd->sidemove=cmd->upmove=0;cmd->buttons=0;cmd->impulse=0;}
}
void PlayerMoveWithFluids(playermove_t* move,int server){
    const auto* field=!server&&!HasControl()?native_fluids.active(world,Seconds()):nullptr;
    fluids::MovementScope scope(*move,field);
    const auto* state=!server&&!server_minecraft_form&&client_motion::ready()?native_motion.active(world,player_slot,player_serial,server_player_life,Seconds()):nullptr;
    status_motion::MovementScope motion(*move,state);
    const auto* water=!server&&!server_minecraft_form&&client_motion::ready()?water_state.active(world,player_slot,player_serial,server_player_life,Seconds()):nullptr;
    native_water::MovementScope water_scope(*move,water);gExportfuncs.HUD_PlayerMove(move,server);
}
void PlayerMove(playermove_t* move,int server){
    prediction_objects=0;
    if(!server&&!MatchingHostProtocol()){
        native_fluids.clear();native_motion.clear();water_state.clear();native_vision.clear();native_nausea.clear();view_distortion_frames.clear();
        map_edits.reset(0);edit_assembler.reset(0);client_map::reset();edit_recovery=false;
        gExportfuncs.HUD_PlayerMove(move,server);return;
    }
    if(!server&&!HasControl()&&world&&Seconds()-last_colliders<1.0){
        // The public client PM callback exposes exactly the physent array used
        // by the original client movement code; no private hw.dll offset is used.
        for(const auto& [key,c]:native_colliders){
            bool nearby=true;for(int i=0;i<3;++i)if(c.max[i]<move->origin[i]-256||c.min[i]>move->origin[i]+256)nearby=false;
            if(!nearby)continue;
            int index=-1;for(int i=1;i<move->numphysent;++i)if(move->physents[i].info==c.slot){index=i;break;}
            if(index<0){if(move->numphysent>=MAX_PHYSENTS){++prediction_overflow;continue;}index=move->numphysent++;}
            auto& p=move->physents[index];p={};p.info=c.slot;p.solid=2;p.movetype=0;
            std::strcpy(p.name,"goldcraft_object");
            for(int i=0;i<3;++i){p.origin[i]=(c.min[i]+c.max[i])*0.5f;p.mins[i]=c.min[i]-p.origin[i];p.maxs[i]=c.max[i]-p.origin[i];}
            ++prediction_objects;
        }
        ++prediction_frames;
    }
    client_map::move(move,server,PlayerMoveWithFluids);
}
int InitializeClient(cl_enginefunc_t* engine,int version){
    client_map::engine_initialized(engine);
    hud_sprites::bind(engine->pfnSPR_GetList);
    const int result=client::initialize_with_events(engine,version,
        client_map::event_api(engine->pEventAPI),gExportfuncs.Initialize,hud_sprites::list);
    if(result)gEngfuncs.pfnSPR_GetList=hud_sprites::list;
    if(!result)client_map::shutdown_events();
    return result;
}
void PlayerMoveInit(playermove_t* move){
    gExportfuncs.HUD_PlayerMoveInit(move);
    client_map::movement_initialized(move);
}
int KeyEvent(int down,int key,const char* binding_text) {
    ++key_events;last_key=key;last_key_down=down;
    // The engine executes console bindings only when this callback lets them
    // through. BaseUI declining an event alone does not bypass HUD_Key_Event.
    if(gEngfuncs.Con_IsVisible()||host_keys::console_binding(binding_text)){
        if(down){ReleaseMinecraftKeys();ReleaseHostUse();}
        return gExportfuncs.HUD_Key_Event?gExportfuncs.HUD_Key_Event(down,key,binding_text):1;
    }
    if(key>=0&&key<int(menu_pressed.size())&&menu_pressed[key]){
        if(!down)menu_pressed[key]=false;return 0;
    }
    if(DefaultFormMenuKey(key,binding_text)){
        if(down){
            if(key>=0&&key<int(menu_pressed.size()))menu_pressed[key]=true;
            OpenFormMenu();
        }
        return 0;
    }
    if(ServerMenuInput()&&ServerMenuKey(key,binding_text)){
        if(down){if(key>=0&&key<int(menu_pressed.size()))menu_pressed[key]=true;
            if(const int selection=form_menu.selection(key==K_ESCAPE?'0':key,key==K_ESCAPE?"":binding_text?binding_text:"",Seconds());selection>0)SelectFormMenu(selection);}
        return 0;
    }
    host_keys::modifier(ui_modifiers,key==K_SHIFT?1:key==K_CTRL?2:key==K_ALT?4:0,down!=0);
    if(ui_active&&HudFresh()){
        ui_modifiers=host_text::modifiers(ui_modifiers);
        if(key>=K_MOUSE1&&key<=K_MOUSE3){SendUi(2,key-K_MOUSE1,down?1:0);return 0;}
        if(key==K_MWHEELUP||key==K_MWHEELDOWN){if(down)SendUi(3,0,0,key==K_MWHEELUP?1.0f:-1.0f);return 0;}
        const int glfw=glfw_key(key);
        if(host_text::block_key(glfw))return 0;
        if(glfw>=32&&glfw<=348)SendUi(4,glfw,down?1:0);
        return 0;
    }
    if(HasControl()&&!host_ui::state().keyboard) {
        if(key=='i'||key=='I'){if(down)SendControl(10);return 0;}
        const auto route=host_keys::gameplay_route(binding_text,false,host_ui::state().hooked);
        if(route!=host_keys::Route::native&&ForwardKey(down,key)&&route==host_keys::Route::consume)return 0;
    }
    return gExportfuncs.HUD_Key_Event?gExportfuncs.HUD_Key_Event(down,key,binding_text):1;
}
bool DrawPostMask(void*,const MetaRendererPostSceneFrame& frame,std::uint8_t first,std::uint8_t count) noexcept {
    WorkScope timing(WorkStage::post_mask);
    try{
        if(first>post_batches.size()||count>post_batches.size()-first)return false;
        const auto previous_error=render::statistics().error_count;
        {
            // Base VIX Bloom.start copies shared world depth for each render
            // type while retaining accumulated color in the same mask target.
            struct ReadFramebuffer {
                GLint previous=0;bool used=false;
                void reset(const MetaRendererPostSceneFrame& source){
                    if(!used){glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);used=true;}
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,source.framebuffer);
                    glBlitFramebuffer(0,0,source.width,source.height,0,0,source.width,source.height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
                }
                ~ReadFramebuffer(){if(used)glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);}
            } read;
            ProfiledPass measured_pass(true,0,&frame);auto& pass=measured_pass.get();if(!pass.ready())return false;
            for(unsigned i=first;i<unsigned(first)+count;++i){
                if(post_depth_resets[i])read.reset(frame);
                const auto& batch=post_batches[i];const auto key=batch.material.texture;
                GLuint texture=atlas_texture;
                if(key==particle_untextured)texture=0;
                else if(key){auto found=particle_textures.find(key);if(found==particle_textures.end())return false;texture=found->second.id;}
                if(!pass.effect(batch.material,texture))return false;
                batch.gpu->draw(batch.vertices,post_revision,true);
            }
        }
        return render::statistics().error_count==previous_error;
    }catch(...){return false;}
}
void DrawPostEffects(const MetaRendererPostSceneFrame& frame) noexcept {
    ++post_callbacks;
    if(!post_replay||!link.connected()||link.generation()!=last_link_generation||!world||!MatchingHostProtocol()||post_stages.empty()||
       post_life!=minecraft_life||post_life!=server_player_life||post_generation!=particle_generation||
       Seconds()-last_post_frame>=.25||(render_particles&&render_particles->value==0)||
       (render_post_effects&&render_post_effects->value==0))return;
    WorkScope timing(WorkStage::post_replay);
    if(post_replay->replay(frame,post_stages,DrawPostMask,nullptr)){++post_draws;post_error.clear();}
    else{
        ++post_failures;
        try{if(post_error!=post_replay->last_error()){post_error=post_replay->last_error();Log(post_error);}}catch(...){}
    }
}
bool DrawEntityOutlineMask(void* user,const MetaRendererPostSceneFrame& frame) noexcept {
    const auto& submission=*static_cast<const OutlineSubmission*>(user);
    try{
        const auto errors=render::statistics().error_count;
        if(submission.minecraft){
            render::Pass pass(true,0,&frame);if(!pass.ready())return false;
            for(const auto& batch:entity_batches){
                if(!(batch.flags&entity_outline)||batch.vertices.empty())continue;
                const auto found=entity_textures.find(batch.texture);
                if(batch.texture&&found==entity_textures.end())return false;
                if(!pass.outline((batch.flags&entity_outline_cull)!=0))return false;
                pass.texture(batch.texture?found->second.id:atlas_texture);
                batch.gpu->draw(batch.vertices,entity_revision,true);
            }
        }
        if(submission.count&&(!render::studio_outline()||!render::studio_outline()->DrawPlayerMask(frame,submission.players.data(),submission.count)))return false;
        return errors==render::statistics().error_count;
    }catch(...){return false;}
}
void DrawEntityOutlines(const MetaRendererPostSceneFrame& frame) noexcept {
    if(!entity_outline_replay||!world||!MatchingHostProtocol()||!render::owns_context())return;
    OutlineSubmission submission;
    if(native_outline_ready)submission=native_outline_submission(native_glow,player_presentations,presentation_times,world,Seconds());
    submission.minecraft=link.entity_outlines()&&link.generation()==last_link_generation&&atlas_texture&&atlas_generation&&EntityFrameFresh()&&
        std::any_of(entity_batches.begin(),entity_batches.end(),[](const auto& b){return (b.flags&entity_outline)&&!b.vertices.empty();});
    if(!submission.minecraft&&!submission.count)return;
    if((frame.flags&META_POST_SCENE_LINEAR)&&!entity_outline_gamma)return;
    const float gamma=(frame.flags&META_POST_SCENE_LINEAR)?entity_outline_gamma->value:1.f;
    if(entity_outline_replay->replay(frame,gamma,DrawEntityOutlineMask,&submission))entity_outline_error.clear();
    else try{if(entity_outline_error!=entity_outline_replay->last_error()){entity_outline_error=entity_outline_replay->last_error();Log(entity_outline_error);}}catch(...){}
}
bool DrawStaticShadow(std::uint32_t flags) noexcept {
    ++static_shadow_callbacks;
    try{
        if(!(flags&META_SCENE_SHADOW)||!link.connected()||link.generation()!=last_link_generation||!world||
           !atlas_texture||!atlas_generation||atlas_alpha.size()!=std::size_t(atlas_width)*atlas_height||!render::owns_context()||
           (render_scene_static_shadows&&render_scene_static_shadows->value==0)){
            ++static_shadow_failures;return false;
        }
        const auto previous_error=render::statistics().error_count;
        {
            WorkScope timing(WorkStage::draw_shadow);
            ProfiledPass measured_pass(false,flags);
            const bool cull=!render_scene_cull||render_scene_cull->value!=0;
            for(const auto& [key,section]:sections){
                if((section.flags&1)||section.vertices.empty())continue;
                if(cull&&render::cull_shadow_bounds(section.bounds))continue;
                auto& pass=measured_pass.get();if(!pass.ready()||!section.gpu){++static_shadow_failures;return false;}
                pass.texture(atlas_texture);pass.emissive((section.flags&2)!=0);
                section.gpu->draw(section.vertices,section.revision,false);++static_shadow_draws;
            }
        }
        // Pass reports/restores GL state in its destructor, before accepting
        // even an otherwise complete bake. Valid empty scenes need no GL pass.
        if(render::statistics().error_count==previous_error)return true;
    }catch(...){}
    ++static_shadow_failures;return false;
}
void Draw(bool transparent,std::uint32_t flags) {
    WorkScope draw_timing(transparent?WorkStage::draw_transparent:
        (flags&META_SCENE_SHADOW)?WorkStage::draw_shadow:WorkStage::draw_opaque);
    if(!link.connected()||!world) return;
    const bool effects_ready=transparent&&flags==0&&!particle_batches.empty()&&
        particle_life==minecraft_life&&particle_life==server_player_life&&Seconds()-last_particle_frame<0.25&&
        (!render_particles||render_particles->value!=0);
    if(!atlas_texture&&!effects_ready)return;
    const bool cull_shadow=(flags&META_SCENE_SHADOW)&&!transparent&&(!render_scene_cull||render_scene_cull->value!=0);
    const bool static_covered=(flags&META_SCENE_SHADOW)&&!transparent&&
        (!render_scene_static_shadows||render_scene_static_shadows->value!=0)&&render::static_scene_covered();
    {
    ProfiledPass measured_pass(transparent,flags);
    if(atlas_texture){
    {
    WorkScope timing(WorkStage::sections);
    // Count section records whose traversal was skipped, including empty and
    // translucent layers. Actual GPU draws are counted separately above.
    if(static_covered)static_shadow_section_skips+=sections.size();
    else for(const auto& [key,section] : sections) {
        if(((section.flags&1)!=0)!=transparent || section.vertices.empty()) continue;
        if(cull_shadow&&render::cull_shadow_bounds(section.bounds))continue;
        auto& pass=measured_pass.get();if(!pass.ready())return;
        pass.texture(atlas_texture);
        pass.emissive((section.flags&2)!=0);
        section.gpu->draw(section.vertices,section.revision,false);
    }
    }
    {
    WorkScope timing(WorkStage::entities);
    for(const auto& batch:entity_batches) {
        if(!EntityFrameFresh())break;
        if((batch.flags&entity_outline)||((batch.flags&1)!=0)!=transparent||batch.vertices.empty())continue;
        auto texture=entity_textures.find(batch.texture);if(batch.texture&&texture==entity_textures.end())continue;
        if(cull_shadow&&render::cull_shadow_bounds(batch.bounds))continue;
        auto& pass=measured_pass.get();if(!pass.ready())return;
        pass.emissive(false);
        pass.depth_write((batch.flags&2)!=0);
        pass.view_offset((batch.flags&4)!=0);
        pass.texture(batch.texture?texture->second.id:atlas_texture);batch.gpu->draw(batch.vertices,entity_revision,true);
        if(flags==0&&entity_drawn_revision!=entity_revision){entity_drawn_revision=entity_revision;++entity_presented_frames;}
    }
    }
    // Feedback uses the current scene depth; never include outlines/cracks in lighting or shadow maps.
    if(auto* pass=measured_pass.existing())pass->view_offset(false);
    if(transparent&&flags==0&&feedback_life==minecraft_life&&feedback_life==server_player_life&&Seconds()-last_feedback<0.25){
        WorkScope timing(WorkStage::feedback);
        bool cracks_started=false;
        for(const auto& batch:crack_batches){
            if(batch.vertices.empty())continue;
            auto& pass=measured_pass.get();if(!pass.ready())return;
            if(!cracks_started){pass.texture(atlas_texture);pass.feedback(1);cracks_started=true;}
            batch.gpu->draw(batch.vertices,feedback_revision,true);++feedback_draws;
        }
        if(HasControl()&&!minecraft_menu&&!outline_vertices.empty()){
            auto& pass=measured_pass.get();if(!pass.ready())return;
            pass.texture(atlas_texture);
            pass.feedback(2);outline_gpu->draw(outline_vertices,feedback_revision,true,true);++feedback_draws;
        }
    }
    }
    // Ordered camera-specific effects run after ordinary meshes and feedback.
    // Pass restores host state afterward; these never enter GBuffer or shadows.
    if(transparent&&flags==0&&EntityFrameFresh()){
        WorkScope timing(WorkStage::entity_effects);
        for(const auto& batch:entity_effect_batches){
            if(batch.vertices.empty())continue;
            const auto key=batch.material.texture;GLuint texture=atlas_texture;
            if(key==particle_untextured)texture=0;
            else if(key){auto found=particle_textures.find(key);if(found==particle_textures.end())continue;texture=found->second.id;}
            auto& pass=measured_pass.get();if(!pass.ready())return;
            if(pass.effect(batch.material,texture)){batch.gpu->draw(batch.vertices,entity_revision,true);++entity_effect_draws;}
        }
    }
    if(effects_ready){
        WorkScope timing(WorkStage::particle_draw);
        for(const auto& batch:particle_batches){
            if(batch.vertices.empty())continue;
            const auto key=batch.material.texture;
            GLuint texture=atlas_texture;
            if(key==particle_untextured)texture=0;
            else if(key){auto found=particle_textures.find(key);if(found==particle_textures.end())continue;texture=found->second.id;}
            auto& pass=measured_pass.get();if(!pass.ready())return;
            if(pass.effect(batch.material,texture)){batch.gpu->draw(batch.vertices,particle_revision,true);++particle_draws;}
        }
    }
    if(!measured_pass.existing())return;
    }
    last_gl_error=render::statistics().error;++frames_drawn;
    if(section_logged&&!draw_logged){draw_logged=true;Log("Minecraft scene draw reached OpenGL, error="+std::to_string(last_gl_error));}
}
int Redraw(float time,int intermission) {
    WorkScope timing(WorkStage::hud_redraw);
    const double began=Seconds();
    const bool visible=HudFresh();
    const bool pending=MinecraftPresentationPending();
    const bool native_hud=(!visible||(hud_flags&8))&&!pending;
    int result=1;
    bool native_updated=false;
    const auto native_redraw=[&]{native_updated=true;WorkScope timing(WorkStage::native_hud_redraw);return gExportfuncs.HUD_Redraw(time,intermission);};
    if(native_hud)result=native_redraw();
    bool composed=false;
    if(visible){
        if(hud_blank&&!ui_active){composed=true;++hud_blank_composites_skipped;}
        else{
            {WorkScope timing(WorkStage::mc_hud_composite);composed=render::draw_hud(hud_texture,ui_x,ui_y,ui_active,hud_gui_width,hud_gui_height);}
            if(composed)++hud_draws;
            else if(!native_hud){control_trace.hud_fallback(SampleControl(),render::statistics().error);result=native_redraw();}
        }
    }
    if(!native_updated)hud_clock::advance(time);
    // Retain server kill notices and menus above the full Minecraft HUD.
    // Native/ordinary/death HUD and render failure use the original client path.
    if((composed||pending)&&!native_hud&&!gEngfuncs.Con_IsVisible()&&!host_ui::state().game_menu){
        WorkScope timing(WorkStage::server_hud_overlay);
        server_hud::draw(Seconds(),form_menu,!ui_active&&!minecraft_menu);
        form_menu_draws=server_hud::menu_draws();
    }
    hud_work_ms=(Seconds()-began)*1000;last_gl_error=render::statistics().error;
    NativeProfileFrameEnd(time);
    const char* path=std::getenv("GOLDCRAFT_CAPTURE_PATH");
    if(!path||!capture_requested)return result;
    capture_requested=false;
    render::capture(path);
    return result;
}
void DrawNormal() { gExportfuncs.HUD_DrawNormalTriangles(); if(!render::scene_active())Draw(false); }
void CreateEntities() {
    if(MatchingHostProtocol())PublishBrushSnapshot();
    else if(map_edits.state().epoch){
        map_edits.reset(0);edit_assembler.reset(0);client_map::reset();edit_recovery=false;
    }
    gExportfuncs.HUD_CreateEntities(); client_precache::create_entities();
    if(have_view){
        const float angles[]{observed_angles.x,observed_angles.y,observed_angles.z};
        visible_entities::create_entities(native_view_origin.data(),angles);
    }
}
void DrawTransparent() { gExportfuncs.HUD_DrawTransparentTriangles(); Draw(true); }
void ShutdownClient() {
    // Engine shutdown still calls Steam after unloading the client DLL. Release
    // context hooks while their IAT slots and client descriptors remain mapped.
    steam_async::shutdown();
    Log("Steam async hooks released before client shutdown");
    spectator_menu::shutdown();
    hud_clock::shutdown();
    gExportfuncs.HUD_Shutdown();
}
}

void IPluginsV4::Init(metahook_api_t* pApi,mh_interface_t*,mh_enginesave_t*) {
    api=pApi;
    diagnostics::Config diagnostics_config;
    if(const char* path=std::getenv("GOLDCRAFT_CLIENT_LOG")) diagnostics_config.log_path=path;
    if(const char* path=std::getenv("GOLDCRAFT_CLIENT_STATUS")) diagnostics_config.status_path=path;
    diagnostic_io.start(std::move(diagnostics_config));
    Log("plugin Init, protocol "+std::to_string(protocol_version));
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* engine) {
    gEngfuncs=*engine;
    Log(client_userinfo::install(api) ? "1024-byte userinfo storage installed" :
        std::string("userinfo expansion unavailable: ") + client_userinfo::error());
    Log(packet_client::install(api) ? "1024 packet entities and bounded snapshot transport installed" :
        std::string("packet entity expansion unavailable: ") + packet_client::install_error());
    Log(visible_entities::install(api) ? "visible entity capacity 4096 installed" :
        std::string("visible entity expansion unavailable: ") + visible_entities::install_error());
    client_precache::set_reset_observer(client_userinfo::reset);
    Log(client_precache::install(api) ? "dynamic precache installed" :
        std::string("dynamic precache unavailable: ") + client_precache::install_error());
    try {
        auto config=goldcraft::environment_config("GOLDCRAFT_CLIENT",goldcraft::Role::host_client,goldcraft::Role::fabric_client);
        if(!config){config=EndpointConfig{};config->session=random_key();config->token=random_key();}
        link.start(*config);discovery.start(*config,link.port());
        Log("loopback endpoint listening on "+std::to_string(link.port()));
    } catch(const std::exception& e) { Log(e.what()); }
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* functions) {
    goldcraft::steam_async::install(api);
    gExportfuncs=*functions;
    Log(client_motion::install(api)?"native status motion prediction installed":std::string("native motion unavailable: ")+client_motion::error());
    functions->Initialize=InitializeClient;
    functions->HUD_PlayerMoveInit=PlayerMoveInit;
    Log(client_precache::install_client() ? "client media v3 installed" :
        std::string("client media unavailable: ") + client_precache::install_error());
    functions->HUD_Init=InitHud; functions->HUD_VidInit=VidInit; functions->HUD_Frame=Frame;
    functions->HUD_Shutdown=ShutdownClient;
    functions->HUD_Redraw=Redraw;
    functions->V_CalcRefdef=CalcRefDef;
    functions->HUD_UpdateClientData=UpdateClientData;
    functions->CL_CreateMove=CreateMove;
    functions->HUD_PlayerMove=PlayerMove;
    functions->HUD_Key_Event=KeyEvent;
    functions->HUD_AddEntity=AddEntity;
    functions->HUD_CreateEntities=CreateEntities;
    functions->HUD_DrawNormalTriangles=DrawNormal; functions->HUD_DrawTransparentTriangles=DrawTransparent;
    Log("LoadClient complete, engine build "+std::to_string(api->GetEngineBuildnum()));
}
void IPluginsV4::ExitGame(int) {
    // MetaHook unloads hw.dll between ExitGame and Shutdown. Restore engine
    // patches and release every engine/renderer callback while it is mapped.
    goldcraft::steam_async::shutdown();
    client_motion::shutdown();
    spectator_menu::shutdown();
    hud_clock::shutdown();
    client_map::shutdown_preparation();
    client_map::shutdown_events();
    input_audit::shutdown();
    host_text::shutdown();
    host_ui::shutdown();
    discovery.stop();
    link.stop();
    ResetWorld();
    StopPostScene();
    render::shutdown();
    client_userinfo::shutdown();
    Log("ExitGame");
}
void IPluginsV4::Shutdown() {
    // Only plugin-owned services are valid after engine unload. In particular,
    // repeating StopPostScene here calls Cvar_SetValue in the unloaded module.
    discovery.stop();
    link.stop();
    Log("Shutdown");
    diagnostic_io.stop();
}
const char* IPluginsV4::GetVersion() { static const auto version="GoldCraft dev protocol "+std::to_string(goldcraft::protocol_version);return version.c_str(); }
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);
