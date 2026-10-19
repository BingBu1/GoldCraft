// Real production HUD decode/upload/composition with the plugin entry points
// extracted verbatim. Native HUD and server overlays are callback counters;
// their text rendering is covered by the separate server_hud test fixture.
namespace plugin_hud_probe {
namespace render=goldcraft::render;
using goldcraft::Reader;
using goldcraft::Writer;
using goldcraft::Bytes;
using goldcraft::ProtocolError;
using goldcraft::read_hud_image;
enum class WorkStage {hud_redraw,native_hud_redraw,mc_hud_composite,server_hud_overlay,count};
std::array<unsigned,static_cast<unsigned>(WorkStage::count)> work{};
struct WorkScope {explicit WorkScope(WorkStage stage){++work[static_cast<unsigned>(stage)];}};
struct Link {bool online=true;bool connected()const{return online;}} link;
struct Cvar {float value=1;} enabled;
Cvar* minecraft_hud=&enabled;
std::uint64_t world=11,viewport_id=7,hud_revision{},hud_menu_id{},hud_frames{},hud_bytes{},hud_draws{},
    hud_blank_frames{},hud_blank_uploads{},hud_blank_composites_skipped{},form_menu_draws{};
unsigned viewport_width=320,viewport_height=240,hud_width{},hud_height{},hud_gui_width{},hud_gui_height{},
    hud_flags{},hud_life{},minecraft_life=3,server_player_life=3;
GLuint hud_texture{};
bool hud_blank=false,minecraft_menu=false,ui_active=false,control=false,capture_requested=false;
bool server_minecraft_form=false;
double minecraft_transition_until=0;
namespace client_vitals {bool living=false;bool alive(){return living;}}
float ui_x=.5f,ui_y=.5f,hud_health{},hud_experience{};
int hud_slot{},hud_food{},hud_armor{},hud_level{};
double now=20,last_hud{},next_viewport{},hud_interval_ms{},hud_work_ms{};
GLenum last_gl_error{};
unsigned native_draws{},overlay_draws{},overlay_menus{},fallbacks{},restores{},profile_ends{},cases{};
namespace hud_clock {unsigned updates{};void advance(float){++updates;}}
bool console_visible=false;
cl_enginefunc_t gEngfuncs{};
cl_exportfuncs_t gExportfuncs{};
struct Menu {} form_menu;
namespace host_ui {
struct State {bool game_menu=false;} current;
const State& state(){return current;}
}
namespace server_hud {
void draw(double,const Menu&,bool menu){++overlay_draws;if(menu)++overlay_menus;}
std::uint64_t menu_draws(){return overlay_menus;}
}
struct Trace {void hud_fallback(int,GLenum){++fallbacks;}} control_trace;
int SampleControl(){return 0;}
double Seconds(){return now;}
bool HasControl(){return control;}
void RestoreViewModel(){++restores;}
void NativeProfileFrameEnd(float){++profile_ends;}
#include "plugin_hud.inc"

struct Input {
    std::uint64_t epoch=11,revision=1,viewport=7,menu=0;
    unsigned screen_width=320,screen_height=240,width=64,height=48,flags=10,life=3,slot=2,food=17,armor=4,level=6;
    float health=18,experience=.25f;
    std::uint32_t pixel=0;
};
Bytes message(const Input& f){
    Writer w;w.u64(f.epoch);w.u64(f.revision);w.u64(f.viewport);w.u64(f.menu);
    for(auto value:{f.screen_width,f.screen_height,f.width,f.height,f.flags,f.life,f.slot})w.u32(value);
    w.f32(f.health);w.u32(f.food);w.u32(f.armor);w.u32(f.level);w.f32(f.experience);
    auto count=std::uint64_t((f.flags&2)?f.screen_width:f.width)*((f.flags&2)?f.screen_height:f.height);
    // Tests of malformed dimensions keep a small explicit payload, so the
    // builder itself never allocates according to an invalid remote size.
    if(count>8388608)count=1;
    while(count){const auto run=static_cast<unsigned>(std::min<std::uint64_t>(count,32768));w.u16(0x8000u|(run-1));w.u32(f.pixel);count-=run;}
    return std::move(w.data);
}
void receive(const Bytes& bytes){Reader input(bytes);ReadHud(input);}
auto committed(){
    return std::tuple{hud_texture,hud_width,hud_height,hud_gui_width,hud_gui_height,hud_revision,hud_menu_id,
        hud_flags,hud_life,last_hud,hud_interval_ms,hud_frames,hud_bytes,hud_slot,hud_health,hud_food,
        hud_armor,hud_level,hud_experience,hud_blank,hud_blank_frames,hud_blank_uploads};
}
void no_backend_work(){
    assert(probe::queries==0&&probe::locations==0&&probe::uniforms==0&&probe::bindings==0&&
        probe::texture_parameters==0&&probe::errors==0&&probe::texture_images==0&&
        probe::texture_updates==0&&probe::array_draws==0);
}
void unchanged(Resources& r,const Bytes& bytes,bool invalid){
    r.sentinel();const auto gpu=snapshot();const auto previous=committed();probe::reset();
    bool threw=false;try{receive(bytes);}catch(const ProtocolError&){threw=true;}
    assert(threw==invalid&&committed()==previous);no_backend_work();
    assert(snapshot()==gpu&&glGetError()==GL_NO_ERROR);++cases;
}
constexpr std::array<float,4> background{.25f,.5f,.75f,1};
void prepare(Resources& r){clear();glClearBufferfv(GL_COLOR,0,background.data());r.sentinel();}
Pixels redraw(Resources& r,unsigned native,unsigned composite,unsigned overlay,unsigned menu){
    prepare(r);const auto gpu=snapshot();const auto original=pixels();
    const auto previous_draws=hud_draws,previous_skips=hud_blank_composites_skipped;
    native_draws=overlay_draws=overlay_menus=fallbacks=0;work={};probe::reset();
    const auto clock_before=hud_clock::updates;
    const int result=Redraw(1,0);
    assert(hud_clock::updates==clock_before+(native?0u:1u));
    assert(native_draws==native&&overlay_draws==overlay&&overlay_menus==menu);
    assert(work[static_cast<unsigned>(WorkStage::hud_redraw)]==1&&
        work[static_cast<unsigned>(WorkStage::native_hud_redraw)]==native&&
        work[static_cast<unsigned>(WorkStage::mc_hud_composite)]==composite&&
        work[static_cast<unsigned>(WorkStage::server_hud_overlay)]==overlay);
    assert(hud_draws==previous_draws+composite&&result==(native?7:1)&&fallbacks==0);
    if(!composite){no_backend_work();assert(hud_blank_composites_skipped==previous_skips+(HudFresh()?1u:0u));}
    assert(snapshot()==gpu&&glGetError()==GL_NO_ERROR);++cases;
    const auto result_pixels=pixels();if(!composite)assert(result_pixels==original);return result_pixels;
}
void tests(Resources& r){
    cases=0;link.online=true;enabled.value=1;world=11;viewport_id=7;
    viewport_width=320;viewport_height=240;minecraft_life=server_player_life=3;
    hud_frames=hud_bytes=hud_draws=hud_blank_frames=hud_blank_uploads=hud_blank_composites_skipped=0;
    hud_texture=0;hud_width=hud_height=0;now=20;control=minecraft_menu=ui_active=console_visible=false;
    host_ui::current={};ClearHud();
    gEngfuncs.Con_IsVisible=[](){return console_visible?1:0;};
    gExportfuncs.HUD_Redraw=[](float,int){++native_draws;return 7;};

    Input f;auto bytes=message(f);r.sentinel();auto gpu=snapshot();probe::reset();receive(bytes);
    assert(snapshot()==gpu&&probe::texture_images==1&&probe::texture_updates==0);
    assert(HudFresh()&&hud_blank&&hud_texture&&hud_width==1&&hud_height==1&&hud_blank_frames==1&&hud_blank_uploads==1);
    assert(hud_gui_width==64&&hud_gui_height==48&&hud_revision==1&&hud_frames==1&&hud_bytes==320u*240u*4u);++cases;
    redraw(r,1,0,0,0); // Native weapon/ammo/AMXX path still runs with a blank MC chat layer.

    ++f.revision;f.width=80;f.height=60;f.health=9;f.slot=4;f.food=8;f.armor=12;f.level=10;f.experience=.75f;
    now+=.1;bytes=message(f);r.sentinel();gpu=snapshot();probe::reset();probe::count_allocations=true;
    receive(bytes);probe::count_allocations=false;no_backend_work();assert(probe::allocations==0);
    assert(snapshot()==gpu&&hud_blank_uploads==1&&hud_blank_frames==2&&hud_frames==2&&last_hud==now);
    assert(hud_gui_width==80&&hud_gui_height==60&&hud_slot==4&&hud_health==9&&hud_food==8&&hud_armor==12&&hud_level==10&&hud_experience==.75f);
    assert(hud_bytes==2u*320u*240u*4u&&hud_interval_ms>99&&hud_interval_ms<101);++cases;
    redraw(r,1,0,0,0);

    // MC mode must retain the server overlay callback despite no GPU composite.
    control=true;++f.revision;f.flags=6;receive(message(f));redraw(r,0,0,1,1);
    console_visible=true;redraw(r,0,0,0,0);console_visible=false;
    host_ui::current.game_menu=true;redraw(r,0,0,0,0);host_ui::current.game_menu=false;

    // A zero menu still draws the cursor. Compare all pixels with the old full
    // zero raster under the exact production shader and nondefault host state.
    ++f.revision;f.flags=7;f.menu=91;minecraft_menu=ui_active=true;receive(message(f));
    GLuint full_zero{};unsigned full_width{},full_height{};
    const Bytes zero_pixels(320u*240u*4u,0);upload_texture(full_zero,full_width,full_height,320,240,zero_pixels);
    for(const auto point:std::array<std::array<float,2>,3>{{{.5f,.5f},{.01f,.99f},{.99f,.01f}}}){
        ui_x=point[0];ui_y=point[1];prepare(r);gpu=snapshot();
        assert(draw_hud(full_zero,ui_x,ui_y,true,hud_gui_width,hud_gui_height));
        const auto reference=pixels();assert(snapshot()==gpu);
        assert(redraw(r,0,1,1,0)==reference);
    }
    ui_x=ui_y=.5f;
    ++f.revision;f.flags=11;receive(message(f));control=false;redraw(r,1,1,0,0);
    ++f.revision;f.flags=10;f.menu=0;receive(message(f));minecraft_menu=ui_active=false;redraw(r,1,0,0,0);

    // Alpha zero does not imply an identity operation under GL_ONE. Keep RGB
    // bytes and compare the full-raster composite with a direct GL reference.
    ++f.revision;f.pixel=0x003f1f11;bytes=message(f);probe::reset();receive(bytes);
    assert(!hud_blank&&hud_width==320&&hud_height==240&&probe::texture_images==1);
    prepare(r);const auto before=pixels();assert(draw_hud(hud_texture,ui_x,ui_y,false,hud_gui_width,hud_gui_height));
    const auto nonzero_reference=pixels();assert(nonzero_reference!=before);
    assert(redraw(r,1,1,0,0)==nonzero_reference);
    ++f.revision;f.pixel=0;probe::reset();receive(message(f));
    assert(hud_blank&&hud_width==1&&hud_height==1&&probe::texture_images==1);++cases;
    redraw(r,1,0,0,0);

    // A colored 1x1 HUD must be cleared on its first blank successor even
    // though its storage dimensions already match the blank representation.
    ++f.revision;f.flags=8;f.width=f.height=1;f.pixel=0xff010203;receive(message(f));
    assert(!hud_blank&&hud_width==1&&hud_height==1);
    ++f.revision;f.pixel=0;probe::reset();receive(message(f));
    assert(hud_blank&&probe::texture_images==0&&probe::texture_updates==1);++cases;
    ++f.revision;probe::reset();receive(message(f));no_backend_work();++cases;

    // Stale identity/revision and malformed state/RLE cannot publish a new
    // blank bit, freshness timestamp, metadata, counters or texture content.
    f.flags=10;f.width=64;f.height=48;++f.revision;
    for(unsigned kind=0;kind<6;++kind){auto bad=f;
        switch(kind){case 0:++bad.epoch;break;case 1:++bad.viewport;break;case 2:++bad.life;break;
            case 3:++bad.screen_width;break;case 4:++bad.screen_height;break;case 5:bad.revision=hud_revision;break;}
        unchanged(r,message(bad),false);
    }
    for(unsigned kind=0;kind<8;++kind){auto bad=f;
        switch(kind){case 0:bad.flags=16;break;case 1:bad.flags=12;break;case 2:bad.menu=99;break;
            case 3:bad.width=0;break;case 4:bad.slot=9;break;case 5:bad.health=-1;break;
            case 6:bad.food=21;break;case 7:bad.experience=2;break;}
        unchanged(r,message(bad),true);
    }
    bytes=message(f);bytes.pop_back();unchanged(r,bytes,true);
    bytes=message(f);bytes.push_back(0);unchanged(r,bytes,true);
    auto overflow=f;overflow.flags=8;overflow.width=overflow.height=1;
    bytes=message(overflow);bytes[80]=1;bytes[81]=0x80;unchanged(r,bytes,true);
    bytes=message(f);bytes.resize(81);unchanged(r,bytes,true);

    now=last_hud+.5;assert(!HudFresh());redraw(r,1,0,0,0);
    now=last_hud+.1;link.online=false;assert(!HudFresh());redraw(r,1,0,0,0);link.online=true;
    enabled.value=0;assert(!HudFresh());redraw(r,1,0,0,0);enabled.value=1;
    ++server_player_life;assert(!HudFresh());redraw(r,1,0,0,0);--server_player_life;
    control=false;hud_flags=0;assert(!HudFresh());redraw(r,1,0,0,0);

    // ClearHud is the actual lifecycle routine used on form/life/session and
    // viewport changes. Texture storage survives it, but old blank readiness
    // cannot; the next valid frame refreshes both contents and metadata.
    const auto restore_count=restores;ClearHud();
    assert(!hud_blank&&!HudFresh()&&hud_revision==0&&hud_menu_id==0&&hud_flags==0&&hud_life==0&&restores==restore_count+1);
    f=Input{};now+=1;probe::reset();receive(message(f));
    assert(HudFresh()&&hud_blank&&probe::texture_updates==1);++cases;
    // Recover if video/context reset has removed the texture. Both hidden GL
    // contexts run the whole suite; a stale numeric texture name is not reused.
    ClearHud();glDeleteTextures(1,&hud_texture);hud_texture=0;hud_width=hud_height=0;
    ++f.revision;probe::reset();receive(message(f));
    assert(HudFresh()&&hud_blank&&hud_texture&&probe::texture_images==1);++cases;

    // Preserve the existing native fallback when an actual composition fails.
    ++f.revision;f.pixel=0xff102030;receive(message(f));hud_flags=6;control=true;
    prepare(r);gpu=snapshot();auto* renderer_before=render::renderer;render::renderer=nullptr;
    work={};native_draws=overlay_draws=fallbacks=0;const auto actual_draws=hud_draws;
    const auto fallback_clock=hud_clock::updates;
    assert(Redraw(1,0)==7);render::renderer=renderer_before;
    assert(hud_clock::updates==fallback_clock);
    assert(native_draws==1&&overlay_draws==0&&fallbacks==1&&hud_draws==actual_draws&&snapshot()==gpu);
    assert(work[static_cast<unsigned>(WorkStage::mc_hud_composite)]==1&&work[static_cast<unsigned>(WorkStage::native_hud_redraw)]==1);++cases;

    // Server-confirmed MC spawn handoff must not flash the CS HUD/weapon
    // presentation. No old-life MC raster is reused, and death wins immediately.
    ClearHud();control=false;server_minecraft_form=true;client_vitals::living=true;
    minecraft_transition_until=now+.5;
    assert(MinecraftPresentationPending()&&!HasControl()&&!HudFresh());redraw(r,0,0,1,1);
    client_vitals::living=false;assert(!MinecraftPresentationPending());redraw(r,1,0,0,0);
    client_vitals::living=true;link.online=false;assert(MinecraftPresentationPending());redraw(r,0,0,1,1);
    link.online=true;now=minecraft_transition_until+10;assert(MinecraftPresentationPending());redraw(r,0,0,1,1);
    server_minecraft_form=false;assert(!MinecraftPresentationPending());redraw(r,1,0,0,0);
    server_minecraft_form=true;server_player_life=4;assert(MinecraftPresentationPending()&&!HudFresh());redraw(r,0,0,1,1);
    server_player_life=3;
    minecraft_transition_until=now+.5;control=true;f.flags=6;++f.revision;receive(message(f));
    assert(HudFresh()&&!MinecraftPresentationPending());redraw(r,0,1,1,1);
    server_minecraft_form=false;client_vitals::living=false;minecraft_transition_until=0;
    ++cases;

    ClearHud();glDeleteTextures(1,&hud_texture);hud_texture=0;hud_width=hud_height=0;glDeleteTextures(1,&full_zero);
    assert(glGetError()==GL_NO_ERROR);
    std::printf("{\"productionPluginHudGL\":true,\"cases\":%u,\"repeatedBlankAllocations\":0,\"repeatedBlankUploads\":0,\"blankComposites\":0,\"cursorPixelComparisons\":3,\"zeroAlphaRgbPreserved\":true,\"nativeAndServerCallbackRouting\":true,\"stateRestored\":true,\"foregroundFPSValidated\":false}\n",cases);
}
} // namespace plugin_hud_probe
