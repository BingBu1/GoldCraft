// SPDX-License-Identifier: GPL-3.0-or-later
// Actual scene/admission orchestration and point-light renderer on real WGL.
#include "../../native/csol/client/weapons/guest_cof_scene.cpp"
#include "../../native/csol/client/weapons/guest_cof_admission.cpp"
#include "../../native/guest/cof/client/core_gl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace fixture {
using namespace csol::client;
unsigned checks{},failures{},writes{},preparations{};
std::uint64_t connectionGeneration{1},effectSerial{7};
bool effectsReady{true},muzzlePrepared{};
GuestCofDecalState decals{GuestCofDecalStatus::Prepared,1,0,0,
    guest::cof::client::CustomDecalResult::NoSurface,
    guest::cof::client::CustomDecalDrawResult::MissingResources,true,true,true};
GuestCofResourceState guestResources;
cvar_t protocolSetting{},maskSetting{};
std::array<cvar_t,7> settings{};
constexpr const char* names[]{"cof_gl_gamma","cof_gl_brightness","cof_gl_contrast",
    "cof_gl_dynlight","cof_gl_dynlight_ent","cof_gl_twopassdyn","cof_gl_nocombs"};
void Check(bool ok,int line){++checks;if(!ok){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) fixture::Check((__VA_ARGS__),__LINE__)
std::uint64_t Generation(){return connectionGeneration;}
cvar_t* SettingLookup(const char* name){
    if(!std::strcmp(name,"cof_protocol"))return &protocolSetting;
    if(!std::strcmp(name,"cof_weapons"))return &maskSetting;
    for(unsigned i=0;i<settings.size();++i)if(!std::strcmp(name,names[i]))return &settings[i];
    return nullptr;
}
cvar_t* Register(const char*,const char*,int){CHECK(false);return nullptr;}
void Publish(const char* name,float value){auto* setting=SettingLookup(name);CHECK(setting);if(setting)setting->value=value;++writes;}
struct World {
    model_t model{};msurface_t surface{};mplane_t plane{};mtexinfo_t texinfo{};mnode_t node{},leaves[2]{};
    std::vector<std::byte> storage{offsetof(glpoly_t,verts)+4*7*sizeof(float)};
    std::uint8_t sample{};
    World(){
        auto* poly=reinterpret_cast<glpoly_t*>(storage.data());poly->numverts=4;
        const float points[4][3]{{-.75F,-.75F,0},{.75F,-.75F,0},{.75F,.75F,0},{-.75F,.75F,0}};
        for(unsigned i=0;i<4;++i){std::memcpy(poly->verts[i],points[i],12);}
        plane.normal[2]=1;plane.type=2;texinfo.vecs[0][0]=texinfo.vecs[1][1]=1;
        surface.plane=&plane;surface.texinfo=&texinfo;surface.polys=poly;surface.samples=&sample;
        leaves[0].contents=leaves[1].contents=-1;node.plane=&plane;node.children[0]=&leaves[0];node.children[1]=&leaves[1];node.numsurfaces=1;
        for(unsigned i=0;i<3;++i){node.minmaxs[i]=-1;node.minmaxs[i+3]=1;}
        model.type=mod_brush;model.surfaces=&surface;model.numsurfaces=model.nummodelsurfaces=1;model.nodes=&node;
    }
};
struct OpaqueHost final:IMetaRendererOpaqueScene {
    model_t* world{};MetaRendererOpaqueFrame frame{};bool available{true},capture{};
    std::uint64_t visibility{1};
    explicit OpaqueHost(model_t* model):world(model){
        frame.struct_size=sizeof(frame);frame.flags=META_OPAQUE_GAMMA;frame.world=model;
        const auto identity=guest::cof::client::core_gl::Identity;
        std::copy(identity.begin(),identity.end(),frame.world_matrix);std::copy(identity.begin(),identity.end(),frame.projection_matrix);
        frame.visible_surface_words=frame.visible_node_words=&visibility;
        frame.visible_surface_word_count=frame.visible_node_word_count=1;
    }
    bool QueryWorld(MetaRendererOpaqueWorld& out) const noexcept override{out.world=world;out.hl25=0;return world!=nullptr;}
    bool QueryFrame(MetaRendererOpaqueFrame& out) const noexcept override{if(!available)return false;out=frame;return true;}
    bool MainStudioView() const noexcept override{return available;}
    bool SetBrushSnapshotCapture(bool value) noexcept override{capture=value;return true;}
};
}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};int* hostTextureCache{};
void PrepareGuestCofDecalFrame() noexcept{}
GuestCofDecalState GuestCofDecalsState() noexcept{return fixture::decals;}
bool PrepareGuestCofDecalMap(model_s&,bool) noexcept{fixture::decals.mapPrepared=true;return true;}
void ClearGuestCofDecalMap() noexcept{fixture::decals.mapPrepared=false;}
void DrawGuestCofDecals(const model_s&,const guest::cof::client::CustomDecalFrame&) noexcept{CHECK(false);}
bool GuestCofEffectsReady() noexcept{return fixture::effectsReady;}
std::uint64_t GuestCofEffectsResourceSerial() noexcept{return fixture::effectsReady?fixture::effectSerial:0;}
void InvalidateGuestCofMuzzle() noexcept{fixture::muzzlePrepared=false;}
bool PrepareGuestCofMuzzle() noexcept{++fixture::preparations;return fixture::muzzlePrepared=true;}
bool GuestCofMuzzleReady() noexcept{return fixture::muzzlePrepared&&GuestCofPointLightsReady();}
bool GuestCofMuzzleReady(unsigned) noexcept{return GuestCofMuzzleReady();}
GuestCofResourceState GuestCofResourcesState() noexcept{return fixture::guestResources;}
bool GuestCofParticlesReady() noexcept{return true;}
bool GuestCofControlReady() noexcept{return true;}
bool GuestCofIronsightReady() noexcept{return true;}
bool GuestCofViewmodelReady() noexcept{return true;}
bool GuestCofMouseReady() noexcept{return true;}
void GuestCofMouseFrame() noexcept{}
const char* GuestCofMouseInitialization() noexcept{return "fixture";}
bool GuestCofCrosshairReady() noexcept{return true;}
void GuestCofCrosshairFrame() noexcept{}
bool GuestCofInputReady() noexcept{return true;}
void ResetGuestCofInput() noexcept{}
bool HudStyleWeaponResourcesReady(int,unsigned) noexcept{return true;}
bool PrepareBuyMenuItem(int){return true;}
}
int main(){
    using namespace csol::client;using namespace fixture;
    const auto module=GetModuleHandleA(nullptr);WNDCLASSA wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcA;wc.hInstance=module;wc.lpszClassName="CoFSceneAdmission";
    CHECK(RegisterClassA(&wc));auto window=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,64,64,nullptr,nullptr,module,nullptr);CHECK(window);
    auto dc=GetDC(window);PIXELFORMATDESCRIPTOR pixel{};pixel.nSize=sizeof(pixel);pixel.nVersion=1;
    pixel.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pixel.iPixelType=PFD_TYPE_RGBA;pixel.cColorBits=32;pixel.cAlphaBits=8;pixel.cDepthBits=24;pixel.cStencilBits=8;
    const auto format=ChoosePixelFormat(dc,&pixel);CHECK(format&&SetPixelFormat(dc,format,&pixel));
    auto context=wglCreateContext(dc);CHECK(context&&wglMakeCurrent(dc,context));
    using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);auto create=guest::cof::client::core_gl::Proc<Create>("wglCreateContextAttribsARB");CHECK(create);
    const int attributes[]{0x2091,4,0x2092,4,0x9126,1,0};auto core=create(dc,nullptr,attributes);CHECK(core);
    CHECK(wglMakeCurrent(nullptr,nullptr)&&wglDeleteContext(context)&&wglMakeCurrent(dc,core));
    for(auto& setting:settings)setting.value=1;settings[1].value=settings[5].value=settings[6].value=0;
    engine.GetClientTime=[]()->float{return 1;};engine.pfnGetCvarPointer=SettingLookup;engine.pfnRegisterVariable=Register;engine.Cvar_SetValue=Publish;
    protocolSetting.flags=maskSetting.flags=FCVAR_USERINFO;
    guestResources.status=GuestCofResourceStatus::Prepared;guestResources.generation=connectionGeneration;
    guestResources.studioAndRecoilPrepared=guestResources.decalsPrepared=true;guestResources.weapons=31;
    World world;OpaqueHost host(&world.model);opaque=&host;
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};InitializeGuestCofAdmission(&provider);
    GuestCofSceneFrame();CHECK(pointPrepared&&host.capture&&preparations==1);
    GuestCofAdmissionFrame();CHECK(maskSetting.value==0&&!GuestCofPointLightsReady());
    DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(GuestCofPointLightsReady()&&maskSetting.value==31);
    const auto expectedWrites=writes;
    auto* light=AllocateGuestCofPointLight(13);CHECK(light);light->origin={0,0,.5F};light->color={1,.5F,.25F};light->radius=1.5F;light->die=2;
    // Missing/secondary/linear frames reject drawing, not a prepared backend.
    host.available=false;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();
    CHECK(maskSetting.value==31&&GuestCofPointLightsReady()&&light->key==13&&light->radius==1.5F);
    host.available=true;host.frame.flags=META_OPAQUE_LINEAR;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    model_t other{};host.frame.world=&other;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    host.frame.world=&world.model;host.frame.flags=META_OPAQUE_GAMMA;host.frame.drawn_brush_count=4097;
    DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    host.frame.drawn_brush_count=0;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31&&writes==expectedWrites);
    for(int i=0;i<10000;++i){host.available=(i%3)==0;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();}
    CHECK(maskSetting.value==31&&writes==expectedWrites&&preparations==1);
    // Genuine ownership/resource retirement still withdraws every firearm.
    bool foreign=true;std::thread thread([&]{foreign=GuestCofPointLightsReady();});thread.join();CHECK(!foreign);
    CHECK(wglMakeCurrent(nullptr,nullptr));GuestCofAdmissionFrame();CHECK(maskSetting.value==0);
    CHECK(wglMakeCurrent(dc,core));GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    ++effectSerial;GuestCofSceneFrame();GuestCofAdmissionFrame();CHECK(maskSetting.value==0);
    host.available=true;DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    ++connectionGeneration;decals.generation=guestResources.generation=connectionGeneration;
    GuestCofSceneFrame();GuestCofAdmissionFrame();CHECK(maskSetting.value==0);
    DrawGuestCofOpaqueScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==31);
    effectsReady=false;GuestCofAdmissionFrame();CHECK(maskSetting.value==0);effectsReady=true;
    ClearGuestCofScene();GuestCofAdmissionFrame();CHECK(maskSetting.value==0&&!GuestCofPointLightsReady());
    ShutdownGuestCofAdmission();pointRenderer.reset();
    std::printf("scene/admission checks=%u failures=%u GPU=%s; realWGL=yes gameplay=no performance=no\n",checks,failures,glGetString(GL_RENDERER));
    CHECK(glGetError()==GL_NO_ERROR);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(core);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassA(wc.lpszClassName,module);
    return failures?1:0;
}
