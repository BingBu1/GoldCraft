// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../resource_refresh.hpp"
#include "guest_cof_scene.hpp"
#include "guest_cof_decals.hpp"
#include "guest_cof_effects.hpp"
#include "guest_cof_muzzle.hpp"
#include "../../../guest/cof/client/point_light_render.hpp"
#include <IMetaRendererOpaqueScene.h>
#include <cmath>
#include <limits>
namespace csol::client {
namespace {
IMetaRendererOpaqueScene* opaque{};
model_s* preparedWorld{};
std::uint64_t preparedGeneration{};
std::uint64_t preparedEffectResources{};
bool preparedHl25{};
using namespace guest::cof::client;
std::unique_ptr<PointLightRenderer> pointRenderer;
PointLightState pointLights;
PointLightGamma pointGamma{};
std::array<PointLightBrush,4096> brushSnapshots{};
cvar_t *gamma{},*brightness{},*contrast{},*dynamic{},*brushLights{},*twoPass{},*noCombiners{};
bool pointPrepared{},pointSceneReady{};
DWORD pointThread{};
HGLRC pointContext{};
void ClearPoints() noexcept {
    if(opaque)opaque->SetBrushSnapshotCapture(false);
    InvalidateGuestCofMuzzle();
    pointSceneReady=pointPrepared=false;ClearPointLights(pointLights);
    if(pointRenderer) {
        pointRenderer->ClearMap();
        // A foreign context is not proof that the owning context was destroyed.
        // Retain its GL ownership until the real owner can release it.
        if(!pointRenderer->ReleaseGL())return;
    }
    pointThread=0;pointContext=nullptr;
}
cvar_t* Setting(const char* name,const char* value,int flags) {
    auto* existing=engine.pfnGetCvarPointer(name);
    return existing?existing:engine.pfnRegisterVariable(name,value,flags);
}
bool PreparePoints(model_s& model,bool hl25) {
    if(!GuestCofEffectsReady() || model.numsurfaces<=0)return false;
    if(!gamma) {
        gamma=Setting("cof_gl_gamma","1",FCVAR_ARCHIVE);
        brightness=Setting("cof_gl_brightness","0",FCVAR_ARCHIVE);
        contrast=Setting("cof_gl_contrast","1",FCVAR_ARCHIVE);
        dynamic=Setting("cof_gl_dynlight","1",0);
        brushLights=Setting("cof_gl_dynlight_ent","1",0);
        twoPass=Setting("cof_gl_twopassdyn","0",0);
        noCombiners=Setting("cof_gl_nocombs","0",0);
    }
    if(!gamma||!brightness||!contrast||!dynamic||!brushLights||!twoPass||!noCombiners)return false;
    // Original1005A110 captures these values at map preparation. Namespacing
    // isolates the CoF renderer's settings from ordinary CS rendering.
    if(contrast->value>2.0F)engine.Cvar_SetValue(contrast->name,2.0F);
    if(double(brightness->value)>.3)engine.Cvar_SetValue(brightness->name,.3F);
    pointGamma={gamma->value<0?1.0F:gamma->value,brightness->value,contrast->value};
    if(!pointRenderer)pointRenderer=std::make_unique<PointLightRenderer>();
    if(!pointRenderer->PrepareMap(model,hl25,std::size_t(model.numsurfaces)*2) || !pointRenderer->InitializeGL())return false;
    pointThread=GetCurrentThreadId();pointContext=wglGetCurrentContext();return true;
}
// Compatibility consumers operate on the exact resolved-world camera. They do
// not inherit whichever fixed-function matrices a prior client callback left.
struct WorldMatrices {
    GLint mode{}, cached{hostTextureCache?*hostTextureCache:-1};
    bool compatibility{};
    explicit WorldMatrices(const MetaRendererOpaqueFrame& frame,bool legacy) noexcept :compatibility(legacy) {
        if(!compatibility)return;
        glGetIntegerv(GL_MATRIX_MODE,&mode);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadMatrixf(frame.projection_matrix);
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadMatrixf(frame.world_matrix);
    }
    ~WorldMatrices() {
        if(compatibility){glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GLenum(mode));}
        if(hostTextureCache)*hostTextureCache=cached;
    }
};
}
void GuestCofSceneFrame() noexcept {
    if(!ResourceReadsAllowed())return;
    PrepareGuestCofDecalFrame();
    if(!opaque && api) {
        auto module=api->GetPluginModuleHandleByBaseFileName("Renderer");
        if(!module)module=api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
        auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(HMODULE(module),"CreateInterface")):nullptr;
        if(factory)opaque=static_cast<IMetaRendererOpaqueScene*>(factory(METARENDERER_OPAQUE_SCENE_INTERFACE_VERSION,nullptr));
    }
    const auto state=GuestCofDecalsState();
    if(!state.resourcesPrepared){preparedWorld=nullptr;if(pointPrepared)ClearPoints();return;}
    MetaRendererOpaqueWorld world{};world.struct_size=sizeof(world);
    if(!opaque || !opaque->QueryWorld(world) || !world.world)return;
    const auto effects=GuestCofEffectsResourceSerial();
    if(preparedWorld==world.world && preparedGeneration==state.generation &&
       preparedEffectResources==effects && preparedHl25==(world.hl25!=0))return;
    // One attempt for each admitted world/resource generation. A failed map is
    // observable through GuestCofDecalsState, never retried in the draw path.
    preparedWorld=world.world;preparedGeneration=state.generation;preparedHl25=world.hl25!=0;
    preparedEffectResources=effects;
    PrepareGuestCofDecalMap(*world.world,preparedHl25);
    ClearPoints();
    try {
        pointPrepared=PreparePoints(*world.world,preparedHl25)&&opaque->SetBrushSnapshotCapture(true);
        if(pointPrepared)PrepareGuestCofMuzzle();
    }
    catch(...) {ClearPoints();}
}
void DrawGuestCofOpaqueScene() noexcept {
    pointSceneReady=false;
    const auto state=GuestCofDecalsState();
    if(!opaque || !state.resourcesPrepared || !state.mapPrepared)return;
    MetaRendererOpaqueFrame actual{};actual.struct_size=sizeof(actual);
    if(!opaque->QueryFrame(actual) || !actual.world)return;
    guest::cof::client::CustomDecalFrame frame{};
    frame.matchingOpaqueDepth=true;frame.visibilityEstablished=true;
    frame.originalColorSpace=(actual.flags&META_OPAQUE_GAMMA)!=0;
    frame.visibleSurfaceWords={actual.visible_surface_words,actual.visible_surface_word_count};
    frame.authenticatedMatrices=true;std::copy_n(actual.world_matrix,16,frame.worldMatrix.begin());std::copy_n(actual.projection_matrix,16,frame.projectionMatrix.begin());
    WorldMatrices camera(actual,!state.coreProfile);
    if(state.marks)DrawGuestCofDecals(*actual.world,frame);
    if(pointPrepared&&actual.world==preparedWorld&&actual.drawn_brush_count<=brushSnapshots.size()&&
       state.generation==preparedGeneration&&pointThread==GetCurrentThreadId()&&pointContext==wglGetCurrentContext()) {
        for(unsigned i=0;i<actual.drawn_brush_count;++i) {
            const auto& src=actual.drawn_brushes[i];auto& dst=brushSnapshots[i];dst.model=src.model;
            std::copy_n(src.model_to_world,16,dst.modelToWorld.begin());
            std::copy_n(src.origin,3,dst.origin.begin());std::copy_n(src.forward,3,dst.forward.begin());
            std::copy_n(src.right,3,dst.right.begin());std::copy_n(src.up,3,dst.up.begin());
            std::copy_n(src.local_view,3,dst.localView.begin());dst.renderfx=src.renderfx;dst.rotated=src.rotated!=0;
        }
        PointLightFrame lights{};lights.stage=PointLightStage::OpaqueBeforeTransparency;
        lights.space=(actual.flags&META_OPAQUE_GAMMA)?PointLightSpace::OriginalGamma:PointLightSpace::Unsupported;
        lights.matchingOpaqueDepth=lights.visibilityEstablished=lights.brushLifecycleEstablished=true;
        lights.drawnBrushes={brushSnapshots.data(),actual.drawn_brush_count};
        lights.visibleSurfaceWords={actual.visible_surface_words,actual.visible_surface_word_count};
        lights.visibleNodeWords={actual.visible_node_words,actual.visible_node_word_count};
        lights.authenticatedMatrices=true;lights.worldMatrix=frame.worldMatrix;lights.projectionMatrix=frame.projectionMatrix;
        lights.time=engine.GetClientTime();lights.gamma=pointGamma;lights.glBmodels=brushLights->value;
        lights.forceTwoPass=twoPass->value;lights.disableNvCombiners=noCombiners->value;
        pointSceneReady=pointRenderer->Ready(lights);
        if(pointSceneReady&&dynamic->value!=0.0F) {
            const auto result=pointRenderer->Draw(lights,pointLights);
            pointSceneReady=result==PointLightDrawResult::Drawn||result==PointLightDrawResult::NoLights;
        }
        AdvancePointLights(pointLights,engine.GetClientTime());
    }
}
void ClearGuestCofScene() noexcept {
    ClearPoints();ClearGuestCofDecalMap();preparedWorld=nullptr;preparedGeneration=preparedEffectResources=0;preparedHl25=false;opaque=nullptr;
}
bool GuestCofPointLightsReady() noexcept {
    if(!pointPrepared||!pointSceneReady)return false;
    const auto state=GuestCofDecalsState();
    return pointPrepared&&pointSceneReady&&GuestCofEffectsReady()&&state.resourcesPrepared&&state.mapPrepared&&
        preparedGeneration==state.generation&&pointThread==GetCurrentThreadId()&&pointContext==wglGetCurrentContext();
}
PointLight* AllocateGuestCofPointLight(int key) noexcept {
    return GuestCofPointLightsReady()?&AllocatePointLight(pointLights,key,engine.GetClientTime()):nullptr;
}
bool GuestCofMainStudioView() noexcept {return opaque&&opaque->MainStudioView();}
}
