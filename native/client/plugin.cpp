#include "render_backend.hpp"
#include "host_ui.hpp"
#include <metahook.h>
#include <cl_entity.h>
#include <usercmd.h>
#include <ref_params.h>
#include <r_efx.h>
#include <dlight.h>
#include <cvardef.h>
#include <keydefs.h>
#include <in_buttons.h>
#include <pm_defs.h>
#include <gl/GL.h>
#include "goldcraft/endpoint.hpp"
#include "goldcraft/view_interpolator.hpp"
#include "goldcraft/avatar.hpp"
#include "goldcraft/hud.hpp"
#include "goldcraft/particles.hpp"
#include "goldcraft/form_menu.hpp"
#include "goldcraft/block_feedback.hpp"
#include "goldcraft/camera.hpp"
#include "goldcraft/host_keys.hpp"
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
metahook_api_t* api = nullptr;
Bytes binding;
std::uint64_t world = 0, last_link_generation = 0;
std::uint32_t player_slot=0,player_serial=0;
struct NativeCollider {int slot;float min[3],max[3];};
std::map<std::uint64_t,NativeCollider> native_colliders;
std::uint64_t collider_sequence=0,prediction_frames=0;
double last_colliders=0;
unsigned prediction_objects=0,prediction_overflow=0;
std::string last_error;
std::ofstream log_file;
GLuint atlas_texture = 0;
unsigned atlas_width=0,atlas_height=0;
std::uint64_t atlas_generation=0,atlas_uploads=0,atlas_animation_packets=0,atlas_patch_bytes=0;
double last_atlas_animation=0,atlas_animation_interval_ms=0;
bool section_logged=false,draw_logged=false,atlas_logged=false;
std::uint64_t frames_drawn=0;
GLenum last_gl_error=0;
std::chrono::steady_clock::time_point next_diagnostic{};
Vec3 observed_origin{},observed_angles{};
bool have_view=false;
std::uint64_t input_sequence=0,pose_sequence=0;
std::uint32_t minecraft_life=0;
std::uint32_t server_player_life=0;
bool server_minecraft_form=false;
std::chrono::steady_clock::time_point last_pose{};
Vec3 minecraft_feet{};
float minecraft_eye=1.62f;
bool minecraft_control=false,minecraft_menu=false;
FormMenu form_menu;
pfnUserMsgHook previous_show_menu=nullptr;
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
bool viewmodel_suppressed=false;
float saved_viewmodel=1;
std::uint64_t suppressed_viewmodel_frames=0;
unsigned hud_flags=0,hud_life=0;
GLuint hud_texture=0;
unsigned hud_width=0,hud_height=0,viewport_width=0,viewport_height=0,viewport_scale=0;
unsigned hud_gui_width=0,hud_gui_height=0;
std::uint64_t viewport_id=0,hud_revision=0,hud_menu_id=0,hud_frames=0,hud_bytes=0,hud_draws=0;
double last_hud=0,next_viewport=0,hud_work_ms=0;
double hud_interval_ms=0;
bool viewport_enabled=true,ui_active=false;
float ui_x=0.5f,ui_y=0.5f,ui_angles[3]{};
unsigned ui_modifiers=0;
int hud_slot=0,hud_food=0,hud_armor=0,hud_level=0;
float hud_health=0,hud_experience=0;
double Seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
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
bool capture_requested=false;
struct Lamp {Vec3 origin;float radius;std::uint32_t color;};
std::vector<Lamp> lamps;
std::chrono::steady_clock::time_point last_lamps{};
using Vertex=render::Vertex;
struct Section { std::uint64_t revision; std::uint32_t flags; std::vector<Vertex> vertices; std::shared_ptr<render::Mesh> gpu=std::make_shared<render::Mesh>(); };
std::map<std::array<std::int32_t,4>,Section> sections;
struct Texture {GLuint id;unsigned width,height;};
struct EntityBatch {unsigned texture,flags;std::vector<Vertex> vertices;std::shared_ptr<render::Mesh> gpu=std::make_shared<render::Mesh>();};
std::map<unsigned,Texture> entity_textures;
std::vector<EntityBatch> entity_batches;
std::vector<Vertex> outline_vertices;
std::shared_ptr<render::Mesh> outline_gpu=std::make_shared<render::Mesh>();
std::vector<EntityBatch> crack_batches;
std::uint64_t feedback_revision=0,feedback_frames=0,feedback_draws=0;
unsigned feedback_life=0;
double last_feedback=0;
std::map<unsigned,Texture> particle_textures;
std::vector<EntityBatch> particle_batches;
std::uint64_t particle_generation=0,particle_revision=0,particle_frames=0,particle_draws=0,particle_bytes=0;
unsigned particle_count=0,particle_vertices=0,particle_life=0;
double last_particle_frame=0,particle_interval_ms=0;
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
int KeyEvent(int down,int key,const char* binding_text);
void ReleaseMinecraftKeys();
class SceneCallbacks final : public IMetaRendererSceneCallbacks {
public:
    void DrawOpaqueScene(std::uint32_t flags) override {Draw(false,flags);}
} scene_callbacks;

void ClearParticles(bool delete_textures=true){
    if(delete_textures&&render::owns_context())for(auto& [key,texture]:particle_textures)glDeleteTextures(1,&texture.id);
    particle_textures.clear();particle_batches.clear();particle_generation=particle_revision=0;
    particle_count=particle_vertices=particle_life=0;last_particle_frame=0;
}
void ClearDynamic(bool delete_textures=true) {
    ClearParticles(delete_textures);
    outline_vertices.clear();crack_batches.clear();outline_gpu=std::make_shared<render::Mesh>();
    feedback_revision=feedback_life=0;last_feedback=0;
    camera_interpolator.clear();camera_revision=camera_life=0;last_camera=0;
    if(delete_textures&&render::owns_context())for(auto& [key,texture]:entity_textures)glDeleteTextures(1,&texture.id);
    entity_textures.clear();entity_batches.clear();entity_revision=0;entity_count=block_entity_count=0;
    rendered_players.clear();last_entity_frame=0;entity_drawn_revision=0;
}

void Log(const std::string& text) {
    auto line="[GoldCraft] "+text+"\n";
    OutputDebugStringA(line.c_str());
    if(log_file) { log_file << line; log_file.flush(); }
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
    ReleaseMinecraftKeys();
    form_menu.clear();
    ReleaseHostUse();
    RestoreViewModel();
    sections.clear();lamps.clear();ClearDynamic();player_presentations={};presentation_times={};view_interpolator.clear();
    native_colliders.clear();collider_sequence=0;last_colliders=0;
    world=0;atlas_generation=0;binding.clear();minecraft_control=minecraft_menu=server_minecraft_form=false;input_sequence=pose_sequence=0;player_slot=player_serial=minecraft_life=server_player_life=0;
    if(hud_texture&&render::owns_context())glDeleteTextures(1,&hud_texture);
    hud_texture=0;hud_width=hud_height=0;hud_revision=hud_menu_id=0;last_hud=next_viewport=0;ui_active=false;
}
bool HasControl(){return server_minecraft_form&&minecraft_life==server_player_life&&minecraft_control&&link.connected()&&std::chrono::steady_clock::now()-last_pose<std::chrono::milliseconds(350);}
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
    if(key==K_MWHEELUP||key==K_MWHEELDOWN){if(down)SendKey(3,0,0,key==K_MWHEELUP?1.0f:-1.0f);return true;}
    const bool mouse=key>=K_MOUSE1&&key<=K_MOUSE5;
    const int code=mouse?key-K_MOUSE1:glfw_key(key);if(code<0)return false;
    if(!down&&!forwarded_keys[key])return true;
    const int action=down?(forwarded_keys[key]?2:1):0;
    forwarded_keys[key]=down!=0;SendKey(mouse?2:1,code,action);return true;
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
void ClearHud(){hud_revision=hud_menu_id=0;hud_flags=hud_life=0;last_hud=next_viewport=0;RestoreViewModel();}
void UpdateViewModel(){
    if(HudFresh()&&(hud_flags&4)&&host_viewmodel){
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
    const auto pixels=read_hud_pixels(r,pixel_width,pixel_height);
    render::upload_texture(hud_texture,hud_width,hud_height,pixel_width,pixel_height,pixels);
    hud_gui_width=width;hud_gui_height=height;
    hud_revision=revision;hud_menu_id=menu;hud_flags=flags;hud_life=life;
    const double now=Seconds();if(last_hud)hud_interval_ms=(now-last_hud)*1000;last_hud=now;++hud_frames;hud_bytes+=pixels.size();
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
    auto command="menuselect "+std::to_string(index)+"\n";
    gEngfuncs.pfnServerCmd(command.data());++form_menu_selections;
}
void OpenFormMenu(){
    if(form_menu.active(Seconds())){SelectFormMenu(10);return;}
    if(!world)return;
    CloseMinecraftMenu();ReleaseHostUse();
    gEngfuncs.pfnClientCmd(const_cast<char*>("-forward\n-back\n-moveleft\n-moveright\n-jump\n-duck\n-speed\n-attack\n-attack2\n-use\n-reload\n"));
    gEngfuncs.pfnServerCmd("goldcraft_menu\n");
}
bool DefaultFormMenuKey(int key,const char* binding_text){return key==K_F6&&(!binding_text||!*binding_text);}
void ReloadMinecraftResources(){SendControl(15);}
void Status() {
    gEngfuncs.Con_Printf("[GoldCraft] protocol=%d engine=%d connected=%d local-port=%d sections=%u\n",protocol_version,api->GetEngineBuildnum(),link.connected()?1:0,link.port(),static_cast<unsigned>(sections.size()));
}
void WriteDiagnostics() {
    if(const char* path=std::getenv("GOLDCRAFT_CLIENT_STATUS")) {
        std::ofstream out(path);
        std::size_t vertices=0; for(const auto& [key,s]:sections) vertices+=s.vertices.size();
        const auto& gpu=render::statistics();
        const auto host_menu=host_ui::state();
        const auto depth_batches=std::count_if(entity_batches.begin(),entity_batches.end(),[](const auto& b){return (b.flags&2)!=0;});
        const auto translucent_batches=std::count_if(entity_batches.begin(),entity_batches.end(),[](const auto& b){return (b.flags&1)!=0;});
        out<<"{\"engine\":"<<api->GetEngineBuildnum()<<",\"connected\":"<<(link.connected()?"true":"false")
           <<",\"world\":\""<<world<<"\",\"playerSlot\":"<<player_slot<<",\"playerSerial\":"<<player_serial<<",\"playerLife\":"<<minecraft_life<<",\"sections\":"<<sections.size()<<",\"vertices\":"<<vertices
           <<",\"drawFrames\":"<<frames_drawn<<",\"glError\":"<<last_gl_error<<",\"lights\":"<<lamps.size()<<",\"entities\":"<<entity_count<<",\"blockEntities\":"<<block_entity_count<<",\"entityBatches\":"<<entity_batches.size()<<",\"inputEchoMs\":"<<input_latency_ms<<",\"minecraftControl\":"<<(HasControl()?"true":"false")<<",\"inputSequence\":"<<input_sequence<<",\"poseSequence\":"<<pose_sequence
           <<",\"smoothing\":"<<((!view_smoothing||view_smoothing->value!=0)?"true":"false")<<",\"frameWorkMs\":"<<frame_work_ms<<",\"frameWorkMaxMs\":"<<frame_work_max_ms<<",\"viewFrameMaxMs\":"<<view_frame_max_ms
           <<",\"cameraFrames\":"<<camera_frames<<",\"cameraFresh\":"<<(CameraFresh()?"true":"false")<<",\"perspective\":"<<camera_pose.perspective
           <<",\"cameraAgeMs\":"<<(last_camera?(Seconds()-last_camera)*1000:-1)<<",\"cameraVerticalFov\":"<<camera_pose.vertical_fov
           <<",\"cameraPosition\":["<<camera_pose.position.x<<','<<camera_pose.position.y<<','<<camera_pose.position.z<<']'
           <<",\"cameraAngles\":["<<camera_pose.angles.x<<','<<camera_pose.angles.y<<','<<camera_pose.angles.z<<']'
           <<",\"renderer\":"<<(gpu.renderer?"true":"false")<<",\"sceneApi\":"<<(gpu.scene_api?"true":"false")<<",\"coreProfile\":"<<(gpu.core?"true":"false")
           <<",\"fpsLimit\":"<<gEngfuncs.pfnGetCvarFloat("fps_max")<<",\"vsync\":"<<gEngfuncs.pfnGetCvarFloat("gl_vsync")
           <<",\"atlasUploads\":"<<atlas_uploads<<",\"atlasGeneration\":"<<atlas_generation<<",\"atlasAnimationPackets\":"<<atlas_animation_packets<<",\"atlasPatchBytes\":"<<atlas_patch_bytes<<",\"atlasAnimationIntervalMs\":"<<atlas_animation_interval_ms
           <<",\"gpuUploads\":"<<gpu.uploads<<",\"gpuUploadBytes\":"<<gpu.upload_bytes<<",\"gpuDraws\":"<<gpu.draws<<",\"opaquePasses\":"<<gpu.opaque_passes<<",\"shadowPasses\":"<<gpu.shadow_passes
           <<",\"avatarCount\":"<<rendered_players.size()<<",\"suppressedModels\":"<<suppressed_models<<",\"replacePlayers\":"<<((!replace_players||replace_players->value!=0)?"true":"false")
           <<",\"entityDepthWriteBatches\":"<<depth_batches<<",\"entityTranslucentBatches\":"<<translucent_batches
           <<",\"entityTextures\":"<<entity_textures.size()<<",\"entityRevision\":"<<entity_revision
           <<",\"outlineEdges\":"<<outline_vertices.size()/2<<",\"crackBatches\":"<<crack_batches.size()
           <<",\"feedbackFrames\":"<<feedback_frames<<",\"feedbackDraws\":"<<feedback_draws
           <<",\"feedbackAgeMs\":"<<(last_feedback?(Seconds()-last_feedback)*1000:-1)
           <<",\"entityFrames\":"<<entity_frames<<",\"entityPresentedFrames\":"<<entity_presented_frames<<",\"entityIntervalMs\":"<<entity_interval_ms
           <<",\"entityTextureUploads\":"<<entity_texture_uploads<<",\"entityTextureBytes\":"<<entity_texture_bytes
           <<",\"sceneReady\":"<<(atlas_generation?"true":"false")<<",\"pendingSceneMeshes\":"<<pending_scene_meshes
           <<",\"particleCount\":"<<particle_count<<",\"particleVertices\":"<<particle_vertices<<",\"particleBatches\":"<<particle_batches.size()
           <<",\"particleFrames\":"<<particle_frames<<",\"particleDraws\":"<<particle_draws<<",\"particleBytes\":"<<particle_bytes
           <<",\"particleGeneration\":"<<particle_generation<<",\"particleRevision\":"<<particle_revision<<",\"particleLife\":"<<particle_life
           <<",\"particleAgeMs\":"<<(last_particle_frame?(Seconds()-last_particle_frame)*1000:-1)<<",\"particleIntervalMs\":"<<particle_interval_ms
           <<",\"particlesEnabled\":"<<((!render_particles||render_particles->value!=0)?"true":"false")
           <<",\"hudVisible\":"<<(HudFresh()?"true":"false")<<",\"hudFrames\":"<<hud_frames<<",\"hudDraws\":"<<hud_draws<<",\"hudWidth\":"<<hud_gui_width<<",\"hudHeight\":"<<hud_gui_height
           <<",\"hudTextureWidth\":"<<hud_width<<",\"hudTextureHeight\":"<<hud_height
           <<",\"minecraftHands\":"<<(HudFresh()&&(hud_flags&4)?"true":"false")<<",\"minecraftChatOverlay\":"<<(HudFresh()&&(hud_flags&8)?"true":"false")
           <<",\"viewModelSuppressed\":"<<(viewmodel_suppressed?"true":"false")
           <<",\"hostViewModel\":"<<(host_viewmodel?host_viewmodel->value:-1)<<",\"viewModelSuppressedFrames\":"<<suppressed_viewmodel_frames<<",\"hudIntervalMs\":"<<hud_interval_ms
           <<",\"hudRevision\":"<<hud_revision<<",\"hudAgeMs\":"<<(last_hud?(Seconds()-last_hud)*1000:-1)<<",\"hudWorkMs\":"<<hud_work_ms<<",\"hudMenuId\":"<<hud_menu_id
           <<",\"formMenuVisible\":"<<(form_menu.active(Seconds())?"true":"false")<<",\"formMenuSlots\":"<<form_menu.keys()
           <<",\"formMenuDraws\":"<<form_menu_draws<<",\"formMenuSelections\":"<<form_menu_selections
           <<",\"hudSlot\":"<<hud_slot<<",\"hudHealth\":"<<hud_health<<",\"hudFood\":"<<hud_food<<",\"hudArmor\":"<<hud_armor<<",\"hudLevel\":"<<hud_level<<",\"hudExperience\":"<<hud_experience
           <<",\"uiActive\":"<<(ui_active?"true":"false")<<",\"uiX\":"<<ui_x<<",\"uiY\":"<<ui_y<<",\"viewportId\":"<<viewport_id<<",\"viewportWidth\":"<<viewport_width<<",\"viewportHeight\":"<<viewport_height
           <<",\"testCommand\":"<<test_command_id<<",\"minecraftMenu\":"<<(minecraft_menu?"true":"false")<<",\"inputActive\":"<<(last_input_active?"true":"false")<<",\"inputButtons\":"<<last_input_buttons<<",\"inputForward\":"<<last_input_forward<<",\"minecraftEye\":"<<minecraft_eye
           <<",\"inputButtonsObserved\":"<<input_buttons_observed<<",\"keyEvents\":"<<key_events<<",\"lastKey\":"<<last_key<<",\"lastKeyDown\":"<<last_key_down
           <<",\"forwardedKeys\":"<<forwarded_key_events
           <<",\"consoleVisible\":"<<(gEngfuncs.Con_IsVisible()?"true":"false")
           <<",\"hostUiHook\":"<<(host_menu.hooked?"true":"false")<<",\"hostGameMenu\":"<<(host_menu.game_menu?"true":"false")
           <<",\"hostUiKeyboard\":"<<(host_menu.keyboard?"true":"false")<<",\"hostUiFocus\":\""<<host_menu.focus<<"\""
           <<",\"hostUiEvents\":"<<host_menu.events<<",\"hostUiConsumed\":"<<host_menu.consumed
           <<",\"windowFocused\":"<<(window_focused?"true":"false")<<",\"hostVolume\":"<<gEngfuncs.pfnGetCvarFloat("volume")
           <<",\"hostUseHeld\":"<<(host_use_sent?"true":"false")<<",\"hostUseMessages\":"<<host_use_messages
           <<",\"minecraftForm\":"<<(server_minecraft_form?"true":"false")<<",\"serverPlayerLife\":"<<server_player_life
           <<",\"nativeColliders\":"<<native_colliders.size()<<",\"predictionObjects\":"<<prediction_objects<<",\"predictionFrames\":"<<prediction_frames<<",\"predictionOverflow\":"<<prediction_overflow
           <<",\"minecraftFeet\":["<<minecraft_feet.x<<','<<minecraft_feet.y<<','<<minecraft_feet.z<<"],\"origin\":[";
        out<<observed_origin.x<<','<<observed_origin.y<<','<<observed_origin.z;
        out<<"],\"viewAngles\":["<<observed_angles.x<<','<<observed_angles.y<<','<<observed_angles.z<<"],\"viewReady\":"<<(have_view?"true":"false")<<"}";
        frame_work_max_ms=view_frame_max_ms=0;
    }
}
int BindingMessage(const char*,int size,void* data) {
    try {
        if(size!=32) throw ProtocolError("invalid GCBind length");
        auto view=std::span(static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(size));
        Reader r(view); auto new_world=r.u64();auto slot=r.u32(),serial=r.u32(); r.key(); r.finish();
        if(new_world!=world){sections.clear();lamps.clear();ClearDynamic();ClearHud();player_presentations={};presentation_times={};view_interpolator.clear();atlas_generation=0;minecraft_control=false;pose_sequence=input_sequence=0;}
        if(new_world!=world||slot!=player_slot||serial!=player_serial){ReleaseHostUse();minecraft_life=server_player_life=0;minecraft_control=server_minecraft_form=false;view_interpolator.clear();}
        world=new_world;player_slot=slot;player_serial=serial;binding.assign(view.begin(),view.end());
        link.send(Type::client_binding,binding);
        if(!section_logged) Log("received server pairing identity for current map");
    } catch(const std::exception& e) { Log(e.what()); }
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
            ReleaseMinecraftKeys();
            camera_interpolator.clear();camera_revision=0;last_camera=0;
            form_menu.clear();
            ReleaseHostUse();ClearHud();view_interpolator.clear();minecraft_control=minecraft_menu=ui_active=false;
            gEngfuncs.pfnClientCmd(const_cast<char*>("-forward\n-back\n-moveleft\n-moveright\n-jump\n-duck\n-speed\n-attack\n-attack2\n-use\n-reload\n"));
        }
        server_player_life=life;server_minecraft_form=form!=0;
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
       ||!world||!atlas_texture||(replace_players&&replace_players->value==0)||Seconds()-last_entity_frame>0.5)return accepted;
    if(Seconds()-presentation_times[entity->index]>3)return accepted;
    for(const auto& avatar:rendered_players)if(avatar.slot==static_cast<unsigned>(entity->index)&&replaces_player(avatar,player_presentations[entity->index])) {
        ++suppressed_models;return 0;
    }
    return accepted;
}
void InitHud() {
    gExportfuncs.HUD_Init();
    host_ui::install(api,[](int down,int key,const char* binding_text){
        if(gEngfuncs.Con_IsVisible()||(binding_text&&std::strcmp(binding_text,"toggleconsole")==0))return false;
        if(form_menu.active(Seconds())||DefaultFormMenuKey(key,binding_text)){
            if(host_ui::state().keyboard)return false;
            KeyEvent(down,key,binding_text);return true;
        }
        if(ui_active&&HudFresh()){KeyEvent(down,key,binding_text);return true;}
        // Plus-commands must reach the native command builder (movement/use).
        // Intercept other gameplay keys here exactly once, before native menus.
        if(HasControl()&&!host_ui::state().keyboard&&(!binding_text||binding_text[0]!='+')){
            KeyEvent(down,key,binding_text);return true;
        }
        return false;
    });
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
    gEngfuncs.pfnAddCommand("+goldcraft_use",BeginHostUse);
    gEngfuncs.pfnAddCommand("-goldcraft_use",EndHostUse);
    view_smoothing=gEngfuncs.pfnRegisterVariable("goldcraft_view_smoothing","1",0);
    replace_players=gEngfuncs.pfnRegisterVariable("goldcraft_replace_players","1",0);
    minecraft_hud=gEngfuncs.pfnRegisterVariable("goldcraft_hud","1",0);
    gui_scale=gEngfuncs.pfnRegisterVariable("goldcraft_gui_scale","0",0);
    host_viewmodel=gEngfuncs.pfnGetCvarPointer("r_drawviewmodel");
    render_particles=gEngfuncs.pfnRegisterVariable("goldcraft_particles","1",0);
    gEngfuncs.pfnHookUserMsg("GCBind",BindingMessage);
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
    Log("client HUD hooks registered");
}
int VidInit() {
    sections.clear();ClearDynamic();ClearHud();
    next_scene_request=0;
    if(hud_texture&&render::owns_context())glDeleteTextures(1,&hud_texture);
    hud_texture=0;hud_width=hud_height=0;
    if(atlas_texture&&render::owns_context())glDeleteTextures(1,&atlas_texture);
    atlas_texture=0;atlas_generation=0;atlas_width=atlas_height=0;view_interpolator.clear();
    have_view=false;
    const int result=gExportfuncs.HUD_VidInit();
    render::initialize(&scene_callbacks,Log);
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
    observed_origin={params->simorg[0],params->simorg[1],params->simorg[2]};
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
void ReadAtlas(Reader& r) {
    const auto epoch=r.u64(),generation=r.u64();if(epoch!=world)return;
    auto width=r.u32(),height=r.u32();
    if(!generation||!width||!height||width>4096||height>4096||std::uint64_t(width)*height*4!=r.remaining()) throw ProtocolError("invalid atlas dimensions");
    auto pixels=r.bytes(r.remaining());
    render::upload_texture(atlas_texture,atlas_width,atlas_height,width,height,pixels);
    atlas_generation=generation;++atlas_uploads;last_atlas_animation=0;
    if(!atlas_logged){Log("uploaded Minecraft block atlas "+std::to_string(width)+"x"+std::to_string(height));atlas_logged=true;}
}
void ReadAtlasPatches(Reader& r) {
    const auto epoch=r.u64(),generation=r.u64();
    if(epoch!=world||!atlas_generation||generation!=atlas_generation)return;
    const auto patches=read_atlas_patches(r,atlas_width,atlas_height);
    render::upload_atlas_patches(atlas_texture,patches);
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
    }
    r.finish();
    if(count&&existing!=sections.end())section.gpu=existing->second.gpu;
    sections[key]=std::move(section);
    if(count && !section_logged){section_logged=true;Log("received first nonempty Minecraft section mesh, vertices="+std::to_string(count));}
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
void ReadEntityMesh(Reader& r) {
    auto epoch=r.u64(),revision=r.u64();auto count=r.u32(),blocks=r.u32(),groups=r.u32();if(epoch!=world||revision<=entity_revision)return;
    // HUD_VidInit can invalidate textures while IPC is still delivering the old scene.
    // The complete scene replay sends its atlas, then textures, then meshes in order.
    if(!atlas_generation){++pending_scene_meshes;return;}
    if(count>128||blocks>128||groups>128)throw ProtocolError("invalid dynamic scene count");
    std::vector<EntityBatch> next;std::size_t vertices=0;
    for(unsigned i=0;i<groups;i++) {
        auto texture=r.u32(),flags=r.u32(),n=r.u32();vertices+=n;
        if(texture>256||(flags&~3u)||n%4||vertices>299592||std::size_t(n)*28>r.remaining())throw ProtocolError("invalid dynamic scene vertices");
        EntityBatch batch{texture,flags,{}};batch.vertices.reserve(n);
        if(i<entity_batches.size()&&entity_batches[i].texture==texture&&entity_batches[i].flags==flags)batch.gpu=entity_batches[i].gpu;
        for(unsigned j=0;j<n;j++){Vec3 mc{r.f32(),r.f32(),r.f32()};auto gs=to_goldsrc(mc);batch.vertices.push_back({gs.x,gs.y,gs.z,r.f32(),r.f32(),r.u32(),r.u32()});}
        next.push_back(std::move(batch));
    }
    auto avatars=read_rendered_players(r);
    if(avatars.size()>count)throw ProtocolError("Avatar count exceeds rendered entities");
    for(const auto& batch:next)if(batch.texture&&!entity_textures.contains(batch.texture))throw ProtocolError("Missing dynamic mesh texture");
    const double now=Seconds();if(last_entity_frame)entity_interval_ms=(now-last_entity_frame)*1000;
    entity_batches=std::move(next);rendered_players=std::move(avatars);last_entity_frame=now;entity_revision=revision;entity_count=count;block_entity_count=blocks;++entity_frames;
    if(entity_capture.is_open()){
        if(now>entity_capture_end)entity_capture.close();
        else for(const auto& batch:entity_batches)if(!batch.vertices.empty()){
            double x=0,y=0,z=0;
            for(const auto& vertex:batch.vertices){x+=vertex.x;y+=vertex.y;z+=vertex.z;}
            const auto count=batch.vertices.size();
            entity_capture<<now-entity_capture_start<<','<<revision<<','<<batch.texture<<','<<batch.flags<<','<<count
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
    if(size>64*1024*1024)throw ProtocolError("Particle texture memory budget exceeded");
    if(generation!=particle_generation){ClearParticles();particle_generation=generation;}
    auto& texture=particle_textures[key];render::upload_texture(texture.id,texture.width,texture.height,width,height,r.bytes(r.remaining()));
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
    if(!atlas_generation){++pending_scene_meshes;return;}
    if(!current_particles(snapshot,world,particle_generation,particle_revision,minecraft_life))return;
    for(const auto& batch:snapshot.batches)if(batch.texture&&(snapshot.generation!=particle_generation||!particle_textures.contains(batch.texture)))
        throw ProtocolError("Missing current-generation particle texture");
    if(snapshot.generation!=particle_generation){ClearParticles();particle_generation=snapshot.generation;}
    std::vector<EntityBatch> next;unsigned vertices=0;
    for(const auto& source:snapshot.batches){
        EntityBatch batch{source.texture,source.flags,{}};batch.vertices.reserve(source.vertices.size());
        auto i=next.size();if(i<particle_batches.size()&&particle_batches[i].texture==batch.texture&&particle_batches[i].flags==batch.flags)batch.gpu=particle_batches[i].gpu;
        for(const auto& v:source.vertices){const auto p=to_goldsrc(v.position);batch.vertices.push_back({p.x,p.y,p.z,v.u,v.v,v.color});}
        vertices+=static_cast<unsigned>(source.vertices.size());next.push_back(std::move(batch));
    }
    particle_batches=std::move(next);particle_revision=snapshot.revision;particle_count=snapshot.count;particle_vertices=vertices;particle_life=snapshot.life;
    const double now=Seconds();if(last_particle_frame)particle_interval_ms=(now-last_particle_frame)*1000;last_particle_frame=now;
    ++particle_frames;particle_bytes+=vertices*24;
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
    else if(action=="hud"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("goldcraft_hud",static_cast<float>(enabled));}
    else if(action=="hud_scale"){int scale=0;if(input>>scale&&scale>=0&&scale<=8)gEngfuncs.Cvar_SetValue("goldcraft_gui_scale",static_cast<float>(scale));}
    else if(action=="particles"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("goldcraft_particles",static_cast<float>(enabled));}
    else if(action=="slot"){int slot=0;if(input>>slot&&slot>=1&&slot<=9)SendControl(slot);}
    else if(action=="ui_move"){float x=0,y=0;if(input>>x>>y&&x>=0&&x<=1&&y>=0&&y<=1){ui_x=x;ui_y=y;SendUi(1);}}
    else if(action=="ui_click"){int button=0;if(input>>button&&button>=0&&button<=2){SendUi(2,button,1);SendUi(2,button,0);}}
    else if(action=="ui_scroll"){float amount=0;if(input>>amount&&std::abs(amount)<=16)SendUi(3,0,0,amount);}
    else if(action=="ui_key"){int key=0,mods=0;if(input>>key>>mods&&key>=32&&key<=348&&mods>=0&&mods<=7){const auto before=ui_modifiers;ui_modifiers=mods;SendUi(4,key,1);SendUi(4,key,0);ui_modifiers=before;}}
    else if(action=="ui_char"){int character=0;if(input>>character&&character>=32&&character<=65535)SendUi(5,character);}
    else if(action=="kill")gEngfuncs.pfnClientCmd(const_cast<char*>("kill\n"));
    else if(action=="team"){int team=0;if(input>>team&&team>=1&&team<=2){std::string command="jointeam "+std::to_string(team)+"\njoinclass 1\n";gEngfuncs.pfnClientCmd(command.data());}}
    else if(action=="disconnect")gEngfuncs.pfnClientCmd(const_cast<char*>("disconnect\n"));
    else if(action=="reconnect")gEngfuncs.pfnClientCmd(const_cast<char*>("retry\n"));
    else if(action=="avatars"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("goldcraft_replace_players",static_cast<float>(enabled));}
    else if(action=="smoothing"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("goldcraft_view_smoothing",static_cast<float>(enabled));}
    else if(action=="shadows"){int enabled=1;if(input>>enabled&&enabled>=0&&enabled<=1)gEngfuncs.Cvar_SetValue("r_shadow",static_cast<float>(enabled));}
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
    const double began=Seconds();
    gExportfuncs.HUD_Frame(time);
    TestCommands();
    link.poll();
    static double next_ready=0;
    if(have_view&&link.connected()&&Seconds()>=next_ready){
        gEngfuncs.pfnServerCmd("goldcraft_ready\n");next_ready=Seconds()+1;
    }
    if(link.error()!=last_error) { last_error=link.error(); if(!last_error.empty()) Log(last_error); }
    if(link.connected()&&link.generation()!=last_link_generation) {
        ReleaseHostUse();
        last_link_generation=link.generation(); sections.clear();ClearDynamic();ClearHud();view_interpolator.clear();atlas_generation=0;minecraft_control=false;section_logged=draw_logged=atlas_logged=false;
        if(!binding.empty()) link.send(Type::client_binding,binding);
        Log("Fabric client connected with an isolated session");
    }
    const auto diagnostic_time=std::chrono::steady_clock::now();
    if(diagnostic_time>=next_diagnostic){next_diagnostic=diagnostic_time+std::chrono::milliseconds(std::getenv("GOLDCRAFT_TEST_COMMAND")?100:2000);WriteDiagnostics();}
    for(auto& message : link.take_messages()) {
        try {
            Reader r(message.payload);
            if(message.type==Type::atlas) ReadAtlas(r);
            else if(message.type==Type::atlas_patches)ReadAtlasPatches(r);
            else if(message.type==Type::entity_texture)ReadEntityTexture(r);
            else if(message.type==Type::entity_mesh)ReadEntityMesh(r);
            else if(message.type==Type::particle_texture)ReadParticleTexture(r);
            else if(message.type==Type::particle_mesh)ReadParticles(r);
            else if(message.type==Type::block_feedback)ReadBlockFeedback(r);
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
            else if(message.type==Type::scene_reset){auto epoch=r.u64();r.finish();if(epoch==world){sections.clear();lamps.clear();ClearDynamic();atlas_generation=0;}}
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
            else if(message.type==Type::lights) {
                auto epoch=r.u64();auto count=r.u32();if(epoch!=world)continue;
                if(count>12||r.remaining()!=count*20)throw ProtocolError("invalid light snapshot");
                std::vector<Lamp> next;for(unsigned i=0;i<count;i++) {
                    Vec3 p{r.f32(),r.f32(),r.f32()};float radius=r.f32();auto color=r.u32();
                    if(radius<0||radius>768)throw ProtocolError("invalid light radius");next.push_back({to_goldsrc(p),radius,color});
                }
                r.finish();lamps=std::move(next);last_lamps=std::chrono::steady_clock::now();
            }
            else if(message.type==Type::section_mesh) ReadSection(r);
            else if(message.type==Type::remove_section) {
                auto epoch=r.u64(); std::array<std::int32_t,4> key{r.i32(),r.i32(),r.i32(),0}; r.finish();
                if(epoch==world)for(int layer=0;layer<4;++layer){key[3]=layer;sections.erase(key);}
            }
        } catch(const std::exception& e) { Log(e.what()); }
    }
    SendViewport();UpdateUi();UpdateViewModel();UpdateHostUse();
    // A video/context reset invalidates the local atlas without reconnecting IPC.
    // Request a complete scene instead of relying on periodic full-texture readbacks.
    if(link.connected()&&world&&!atlas_generation&&Seconds()>=next_scene_request){
        Writer request;request.u64(world);link.send(Type::scene_reset,request.data);next_scene_request=Seconds()+1;
    }
    const bool fresh_lamps=std::chrono::steady_clock::now()-last_lamps<std::chrono::milliseconds(500);
    // Every exported emitter needs host/MC occlusion. Leaving lights 3..12
    // unshadowed made them illuminate through warehouse walls and floors.
    for(unsigned i=0;i<12;++i)render::set_light_shadow(0x47430000+i,fresh_lamps&&i<lamps.size()?256:0);
    if(gEngfuncs.pEfxAPI&&gEngfuncs.pEfxAPI->CL_AllocDlight&&fresh_lamps) {
        for(unsigned i=0;i<lamps.size();++i) {
            auto* light=gEngfuncs.pEfxAPI->CL_AllocDlight(0x47430000+i);if(!light)continue;const auto& source=lamps[i];
            light->origin[0]=source.origin.x;light->origin[1]=source.origin.y;light->origin[2]=source.origin.z;
            light->radius=source.radius;light->color.r=source.color&255;light->color.g=(source.color>>8)&255;light->color.b=(source.color>>16)&255;
            light->die=gEngfuncs.GetClientTime()+0.2f;light->decay=0;light->minlight=0;light->dark=0;
        }
    }
    frame_work_ms=(Seconds()-began)*1000;frame_work_max_ms=std::max(frame_work_max_ms,frame_work_ms);
}
void CreateMove(float frame_time,usercmd_t* cmd,int active) {
    // GoldSrc's active argument describes a game connection, not OS focus.
    // Keep each paired MC runtime's input/audio lease tied to its own window.
    DWORD foreground_process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
    window_focused=foreground_process==GetCurrentProcessId();
    UpdateUi();
    // Keep GoldSrc's normal relative mouse capture, converting its look delta to a GUI cursor.
    // A temporary zero angle avoids losing cursor motion at pitch/yaw clamps.
    if(ui_active){float zero[3]{};gEngfuncs.SetViewAngles(zero);}
    gExportfuncs.CL_CreateMove(frame_time,cmd,active);
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
    if(form_menu.active(Seconds())){cmd->buttons=0;cmd->forwardmove=cmd->sidemove=cmd->upmove=0;cmd->impulse=0;}
    const bool input_active=active&&window_focused;
    if(!input_active||!HasControl()||ui_active||form_menu.active(Seconds())||gEngfuncs.Con_IsVisible()||host_ui::state().game_menu)ReleaseMinecraftKeys();
    // Preserve the user's actual +use binding; only its destination changes
    // while MC owns movement. R/+reload and all CS-form commands remain native.
    if(HasControl()&&input_active&&(cmd->buttons&IN_USE)&&!ui_active)host_use_sources|=2;
    else host_use_sources&=~2u;
    if(!input_active)ReleaseHostUse();else UpdateHostUse();
    last_input_active=input_active;last_input_buttons=input_active?cmd->buttons:0;last_input_forward=input_active?cmd->forwardmove:0;last_input_side=input_active?cmd->sidemove:0;
    if(!link.connected()||!world) return;
    Writer w; w.u64(world);w.u64(++input_sequence); w.f32(frame_time); w.u32(input_active?1:0); w.u32(last_input_buttons);
    input_times[input_sequence%input_times.size()]={input_sequence,std::chrono::steady_clock::now()};
    w.f32(cmd->viewangles[0]); w.f32(cmd->viewangles[1]); w.f32(last_input_forward); w.f32(last_input_side); w.f32(input_active?cmd->upmove:0);
    link.send(Type::input,w.data);
    if(HasControl()){cmd->forwardmove=cmd->sidemove=cmd->upmove=0;cmd->buttons=0;cmd->impulse=0;}
}
void PlayerMove(playermove_t* move,int server){
    prediction_objects=0;
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
    gExportfuncs.HUD_PlayerMove(move,server);
}
int KeyEvent(int down,int key,const char* binding_text) {
    ++key_events;last_key=key;last_key_down=down;
    if(DefaultFormMenuKey(key,binding_text)){if(down)OpenFormMenu();return 0;}
    if(form_menu.active(Seconds())){
        if(down){
            if(key==K_ESCAPE)SelectFormMenu(10);
            else if(const int selection=form_menu.selection(key,Seconds());selection>0)SelectFormMenu(selection);
        }
        return 0;
    }
    unsigned modifier=key==K_SHIFT?1:key==K_CTRL?2:key==K_ALT?4:0;
    if(modifier){if(down)ui_modifiers|=modifier;else ui_modifiers&=~modifier;}
    if(ui_active&&HudFresh()){
        if(key>=K_MOUSE1&&key<=K_MOUSE3){SendUi(2,key-K_MOUSE1,down?1:0);return 0;}
        if(key==K_MWHEELUP||key==K_MWHEELDOWN){if(down)SendUi(3,0,0,key==K_MWHEELUP?1.0f:-1.0f);return 0;}
        const int glfw=glfw_key(key);
        if(glfw>=32&&glfw<=348)SendUi(4,glfw,down?1:0);
        if(down&&key>=32&&key<127&&(ui_modifiers&6)==0){
            int character=key;
            if(ui_modifiers&1){
                if(key>='a'&&key<='z')character=key-'a'+'A';
                else {const char* plain="1234567890-=[]\\;',./`",*shift="!@#$%^&*()_+{}|:\"<>?~";const char* found=std::strchr(plain,key);if(found)character=shift[found-plain];}
            }
            SendUi(5,character);
        }
        return 0;
    }
    if(HasControl()&&!host_ui::state().keyboard) {
        if(key=='i'||key=='I'){if(down)SendControl(10);return 0;}
        const bool native_use=binding_text&&std::strcmp(binding_text,"+use")==0;
        if(!native_use&&ForwardKey(down,key)&&(!binding_text||binding_text[0]!='+'))return 0;
    }
    return gExportfuncs.HUD_Key_Event?gExportfuncs.HUD_Key_Event(down,key,binding_text):1;
}
void Draw(bool transparent,std::uint32_t flags) {
    if(!link.connected()||!atlas_texture||!world) return;
    {
    render::Pass pass(transparent,flags);if(!pass.ready())return;
    pass.texture(atlas_texture);
    for(const auto& [key,section] : sections) {
        if(((section.flags&1)!=0)!=transparent || section.vertices.empty()) continue;
        pass.emissive((section.flags&2)!=0);
        section.gpu->draw(section.vertices,section.revision,false);
    }
    pass.emissive(false);
    for(const auto& batch:entity_batches) {
        if(Seconds()-last_entity_frame>0.5)break;
        if(((batch.flags&1)!=0)!=transparent||batch.vertices.empty())continue;
        auto texture=entity_textures.find(batch.texture);if(batch.texture&&texture==entity_textures.end())continue;
        pass.depth_write((batch.flags&2)!=0);
        pass.texture(batch.texture?texture->second.id:atlas_texture);batch.gpu->draw(batch.vertices,entity_revision,true);
        if(flags==0&&entity_drawn_revision!=entity_revision){entity_drawn_revision=entity_revision;++entity_presented_frames;}
    }
    // Particle billboards are camera-specific transient geometry; never put them in a shadow/GBuffer pass.
    if(transparent&&flags==0&&link.connected()&&particle_life==server_player_life&&Seconds()-last_particle_frame<0.25
        &&(!render_particles||render_particles->value!=0)){
        pass.emissive(true);
        for(const auto& batch:particle_batches){
            const auto texture=batch.texture?particle_textures.find(batch.texture)->second.id:atlas_texture;
            pass.depth_write((batch.flags&1)!=0);pass.texture(texture);batch.gpu->draw(batch.vertices,particle_revision,true);++particle_draws;
        }
    }
    // Feedback uses the current scene depth; never include outlines/cracks in lighting or shadow maps.
    if(transparent&&flags==0&&feedback_life==minecraft_life&&feedback_life==server_player_life&&Seconds()-last_feedback<0.25){
        pass.texture(atlas_texture);pass.feedback(1);
        for(const auto& batch:crack_batches){batch.gpu->draw(batch.vertices,feedback_revision,true);++feedback_draws;}
        if(HasControl()&&!minecraft_menu&&!outline_vertices.empty()){
            pass.feedback(2);outline_gpu->draw(outline_vertices,feedback_revision,true,true);++feedback_draws;
        }
    }
    }
    last_gl_error=render::statistics().error;++frames_drawn;
    if(section_logged&&!draw_logged){draw_logged=true;Log("Minecraft scene draw reached OpenGL, error="+std::to_string(last_gl_error));}
}
void DrawFormMenu(){
    if(!form_menu.active(Seconds())||gEngfuncs.Con_IsVisible())return;
    SCREENINFO screen{};screen.iSize=sizeof(screen);gEngfuncs.pfnGetScreenInfo(&screen);
    if(screen.iWidth<320||screen.iHeight<240)return;
    std::vector<std::pair<std::string,char>> lines;
    std::istringstream input(form_menu.text());std::string line;char color='w';int width=240,font_height=14;
    while(std::getline(input,line)){
        std::string text;
        for(std::size_t i=0;i<line.size();++i){
            if(line[i]=='\\'&&i+1<line.size()&&std::strchr("wrydg",line[i+1])){color=line[++i];continue;}
            text+=line[i];
        }
        int measured=0,height=0;gEngfuncs.pfnDrawConsoleStringLen(text.c_str(),&measured,&height);
        width=std::max(width,measured);font_height=std::max(font_height,height);
        lines.emplace_back(std::move(text),color);
    }
    const int step=font_height+4,panel_width=std::min(width+32,screen.iWidth-32);
    const int panel_height=std::min(static_cast<int>(lines.size())*step+32,screen.iHeight-32);
    const int x=(screen.iWidth-panel_width)/2,y=(screen.iHeight-panel_height)/2;
    gEngfuncs.pfnFillRGBA(x,y,panel_width,panel_height,12,17,23,235);
    int row=y+16;
    for(const auto& [text,tint]:lines){
        if(row+font_height>y+panel_height)break;
        if(tint=='y')gEngfuncs.pfnDrawSetTextColor(1.0f,0.8f,0.25f);
        else if(tint=='d')gEngfuncs.pfnDrawSetTextColor(0.55f,0.55f,0.55f);
        else gEngfuncs.pfnDrawSetTextColor(1,1,1);
        gEngfuncs.pfnDrawConsoleString(x+16,row,const_cast<char*>(text.c_str()));row+=step;
    }
    gEngfuncs.pfnDrawSetTextColor(1,1,1);++form_menu_draws;
}
int Redraw(float time,int intermission) {
    const double began=Seconds();
    const bool visible=HudFresh();
    const bool native_hud=!visible||(hud_flags&8);
    int result=1;
    if(native_hud)result=gExportfuncs.HUD_Redraw(time,intermission);
    if(visible){
        if(render::draw_hud(hud_texture,ui_x,ui_y,ui_active,hud_gui_width,hud_gui_height))++hud_draws;
        else if(!native_hud)result=gExportfuncs.HUD_Redraw(time,intermission);
    }
    // The Minecraft compositor suppresses the native HUD, including ShowMenu.
    // Repaint this server-owned menu with the engine's native-resolution text.
    if(visible&&!native_hud)DrawFormMenu();
    hud_work_ms=(Seconds()-began)*1000;last_gl_error=render::statistics().error;
    const char* path=std::getenv("GOLDCRAFT_CAPTURE_PATH");
    if(!path||!capture_requested)return result;
    capture_requested=false;
    render::capture(path);
    return result;
}
void DrawNormal() { gExportfuncs.HUD_DrawNormalTriangles(); if(!render::scene_active())Draw(false); }
void DrawTransparent() { gExportfuncs.HUD_DrawTransparentTriangles(); Draw(true); }
}

void IPluginsV4::Init(metahook_api_t* pApi,mh_interface_t*,mh_enginesave_t*) {
    api=pApi;
    if(const char* path=std::getenv("GOLDCRAFT_CLIENT_LOG")) log_file.open(path,std::ios::app);
    Log("plugin Init, protocol "+std::to_string(protocol_version));
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* engine) {
    gEngfuncs=*engine;
    try {
        if(auto config=goldcraft::environment_config("GOLDCRAFT_CLIENT",goldcraft::Role::host_client,goldcraft::Role::fabric_client)) {
            link.start(*config); Log("loopback endpoint listening on "+std::to_string(link.port()));
        } else Log("no instance environment: bridge disabled");
    } catch(const std::exception& e) { Log(e.what()); }
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* functions) {
    gExportfuncs=*functions;
    functions->HUD_Init=InitHud; functions->HUD_VidInit=VidInit; functions->HUD_Frame=Frame;
    functions->HUD_Redraw=Redraw;
    functions->V_CalcRefdef=CalcRefDef;
    functions->HUD_UpdateClientData=UpdateClientData;
    functions->CL_CreateMove=CreateMove;
    functions->HUD_PlayerMove=PlayerMove;
    functions->HUD_Key_Event=KeyEvent;
    functions->HUD_AddEntity=AddEntity;
    functions->HUD_DrawNormalTriangles=DrawNormal; functions->HUD_DrawTransparentTriangles=DrawTransparent;
    Log("LoadClient complete, engine build "+std::to_string(api->GetEngineBuildnum()));
}
void IPluginsV4::ExitGame(int) { host_ui::shutdown();link.stop(); ResetWorld(); render::shutdown(); Log("ExitGame"); }
void IPluginsV4::Shutdown() { host_ui::shutdown();link.stop(); ResetWorld(); render::shutdown(); Log("Shutdown"); log_file.close(); }
const char* IPluginsV4::GetVersion() { static const auto version="GoldCraft dev protocol "+std::to_string(goldcraft::protocol_version);return version.c_str(); }
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);
