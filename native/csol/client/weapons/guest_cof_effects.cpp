// SPDX-License-Identifier: GPL-3.0-or-later
#include "guest_cof_effects.hpp"
#include "guest_cof_audio.hpp"
#include "../runtime.hpp"
#include "../../../guest/cof/resource_catalog.hpp"
#include "../../../guest/cof/client/resource_groups.hpp"
#include "../../../guest/cof/client/forward_displacement.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <event_api.h>
#include <event_args.h>
#include <cstring>
#include <limits>

namespace csol::client
{
namespace
{
using namespace guest::cof::client;
using namespace guest::cof::resources;
const GoldCraftConnectionAPI* connection{};
std::uint64_t generation{}, preparedGeneration{}, scopeSerial{1},resourceSerial{1};
DWORD ownerThread{};
hook_t* punchHook{};
void* nativePunchStage{};
const ref_params_s* currentRef{};
ViewPunchState punch;
ForwardDisplacementState forwardDisplacement;
Weapon weapon{Weapon::None};
std::size_t weaponViewIndex{guest::cof::Pistols.size()};
bool attachedCamera{}, punchUsed{}, prepared{}, eventRegistered{};
int cameraMode{}, shellIndex{}, flashIndex{}, cheapSmokeIndex{};
model_t* flashModel{};
std::array<model_t*,guest::cof::Pistols.size()> viewModels{};
cl_enginefunc_t effectsEngine;
event_api_t effectsEvents;
EffectResult lastStudio{EffectResult::Unhandled};
PunchResult lastPunch{PunchResult::InvalidInput};

bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void ClearResources() noexcept
{
    if(resourceSerial)resourceSerial=resourceSerial==std::numeric_limits<std::uint64_t>::max()?0:resourceSerial+1;
    prepared=false; preparedGeneration=0; shellIndex=flashIndex=cheapSmokeIndex=0;
    flashModel=nullptr; viewModels={};
}
void AdvanceScope() noexcept
{
    if (scopeSerial) scopeSerial=scopeSerial==std::numeric_limits<std::uint64_t>::max() ? 0 : scopeSerial+1;
}
bool Synchronize() noexcept
{
    if (!Owner() || !connection) return false;
    const auto next=connection->generation();
    if (next!=generation)
    {
        AdvanceScope(); ClearResources(); weapon=Weapon::None; weaponViewIndex=viewModels.size(); generation=next; punch={}; punchUsed=false; currentRef=nullptr;
        attachedCamera=false; cameraMode=0;
        ResetForwardDisplacement(forwardDisplacement);
    }
    return next!=0 && scopeSerial!=0;
}
int ShellModel(const char* name)
{
    if (!name || !GuestCofEffectsReady()) return 0;
    if (!std::strcmp(name,"models/shell.mdl")) return shellIndex;
    return !std::strcmp(name,"sprites/gunsmoke.spr") ? cheapSmokeIndex : 0;
}
model_t* CachedMuzzleModel(const char* name,int* index)
{
    if (!name || !index || !GuestCofEffectsReady() || std::strcmp(name,"models/flash1.mdl")) return nullptr;
    *index=flashIndex;
    return flashModel;
}
bool Local(void*,const LocalSound& sound) noexcept { return PlayGuestCofLocalSound(sound); }
void ViewPunch(event_args_s* event)
{
    if (!event || !Synchronize() || !prepared || preparedGeneration!=generation) return;
    lastPunch=ViewPunchEvent(punch,*event,attachedCamera,cameraMode);
    if (lastPunch==PunchResult::Applied) punchUsed=true;
}
void __cdecl AtNativePunch(ref_params_s* ref) noexcept
{
    if (!Synchronize() || !punchUsed || ref!=currentRef) return;
    lastPunch=ViewPunchAtStage(punch,*ref,engine.GetMaxClients());
}
// Host client b434b1...: native punch1006B7D8..1006B809, then this
// patch1006B80E before stairs/smoothing and final global view-angle copies.
// Preserve all original GPR/flags and x87/SIMD state across the C++ call.
__declspec(naked) void AfterNativePunch()
{
    __asm {
        pushfd
        cmp dword ptr [currentRef], 0
        je native
        cmp byte ptr [punchUsed], 0
        je native
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        mov dword ptr [esp+512], eax
        fxsave [esp]
        push esi
        call AtNativePunch
        add esp, 4
        fxrstor [esp]
        mov esp, dword ptr [esp+512]
        popad
    native:
        popfd
        jmp dword ptr [nativePunchStage]
    }
}
model_t* Model(const char* path,std::uint64_t expectedGeneration,std::uint64_t expectedResources,
    modtype_t type=mod_studio,int* output=nullptr) noexcept
{
    if(!Synchronize()||generation!=expectedGeneration||resourceSerial!=expectedResources)return nullptr;
    const int index=engine.pEventAPI->EV_FindModelIndex(path);
    if(!Synchronize()||generation!=expectedGeneration||resourceSerial!=expectedResources)return nullptr;
    auto* model=index>0 ? studio.GetModelByIndex(index) : nullptr;
    if(!Synchronize()||generation!=expectedGeneration||resourceSerial!=expectedResources)return nullptr;
    if (!model || model->type!=type || std::strncmp(model->name,path,sizeof(model->name))) return nullptr;
    if (output) *output=index;
    return model;
}
}

bool InitializeGuestCofEffects() { return InitializeGuestCofEffects(GoldCraftConnectionInterface()); }
bool InitializeGuestCofEffects(const GoldCraftConnectionAPI* provider)
{
    if (!scopeSerial || (ownerThread && !Owner())) return false;
    if (punchHook) return eventRegistered && Synchronize();
    if (!provider || provider->size<sizeof(GoldCraftConnectionAPI) || provider->version!=1 ||
        !provider->generation || !api || !api->ResolveGameSymbol || !api->GetClientBase ||
        !engine.pfnHookEvent || !engine.GetMaxClients) return false;
    void* address{};
    if (api->ResolveGameSymbol(api->GetClientBase(),"GoldCraftCof_AfterNativePunch",
        MH_GAMESYMBOL_KIND_PATCH,&address)!=MH_GAMESYMBOL_OK || !address) return false;
    constexpr unsigned char expected[]{0x83,0xbe,0xc4,0,0,0,0};
    if (std::memcmp(address,expected,sizeof(expected))) return false;
    AdvanceScope();
    if (!scopeSerial) return false;
    ownerThread=GetCurrentThreadId(); connection=provider; generation=provider->generation();
    punch={}; punchUsed=false; currentRef=nullptr; attachedCamera=false; cameraMode=0; weapon=Weapon::None; weaponViewIndex=viewModels.size();
    ResetForwardDisplacement(forwardDisplacement);
    ClearResources();
    eventRegistered=false;
    try
    {
        punchHook=Hook(address,reinterpret_cast<void*>(AfterNativePunch),&nativePunchStage);
        if (!punchHook) { ShutdownGuestCofEffects(); return false; }
        engine.pfnHookEvent(GlockEvents[1].host,ViewPunch);
        // The SDK registration has no return value. This records completion
        // of that real call, not proof of a future engine dispatch.
        eventRegistered=true;
        return true;
    }
    catch (...) { ShutdownGuestCofEffects(); return false; }
}
void InvalidateGuestCofEffects() noexcept { if (Owner()) ClearResources(); }
bool PrepareGuestCofEffects() noexcept
{
    if (!punchHook || !eventRegistered || !Synchronize()) return false;
    ClearResources();
    const auto current=generation;
    const auto resources=resourceSerial;
    if(!resources)return false;
    if (!GuestCofAudioReady() || !engine.GetViewModel || !engine.pEventAPI ||
        !engine.pEventAPI->EV_FindModelIndex || !studio.GetModelByIndex ||
        !engine.pEfxAPI || !engine.pEfxAPI->R_TempModel || !engine.pfnServerCmd) return false;
    decltype(viewModels) cached{};
    bool anyWeapon{};
    for(const auto& group:PistolResourceGroups)
    {
        const auto* definition=guest::cof::FindPistol(group.identity);
        auto*& model=cached[static_cast<unsigned>(group.identity)];
        model=Model(definition->viewModel.data(),current,resources);
        if(!Synchronize()||generation!=current||resourceSerial!=resources)return false;
        anyWeapon|=model!=nullptr;
    }
    int flash{},smoke{};
    auto* muzzle=Model(GlockModels[unsigned(GlockModel::Flash)].host,current,resources,mod_studio,&flash);
    auto* cheap=Model(GlockModels[unsigned(GlockModel::CheapSmoke)].host,current,resources,mod_sprite,&smoke);
    int shell{};
    auto* shellModel=Model(GlockModels[unsigned(GlockModel::Shell)].host,current,resources,mod_studio,&shell);
    if (!anyWeapon || !muzzle || !cheap || !shellModel ||
        !Synchronize() || generation!=current || resourceSerial!=resources) return false;
    effectsEngine=engine; effectsEvents=*engine.pEventAPI;
    effectsEvents.EV_FindModelIndex=ShellModel; effectsEngine.pEventAPI=&effectsEvents;
    effectsEngine.CL_LoadModel=CachedMuzzleModel;
    // Non-weapons spatial Studio5004 assets have no verified namespace mapping.
    // Do not fall through to an unrelated native sample if one is encountered.
    effectsEngine.pfnPlaySoundByNameAtLocation=nullptr;
    shellIndex=shell; flashIndex=flash; cheapSmokeIndex=smoke; flashModel=muzzle;
    viewModels=cached; preparedGeneration=current; prepared=true;
    return true;
}
bool GuestCofEffectsReady() noexcept
{
    return punchHook && eventRegistered && Synchronize() && prepared &&
        resourceSerial&&generation==preparedGeneration && GuestCofAudioReady();
}
bool GuestCofEffectsOwnsWeapon(Weapon value) noexcept
{return GuestCofEffectsReady()&&weapon==value&&weaponViewIndex<viewModels.size()&&viewModels[weaponViewIndex];}
std::uint64_t GuestCofEffectsResourceSerial() noexcept {return GuestCofEffectsReady()?resourceSerial:0;}
bool ResetGuestCofForwardDisplacement() noexcept
{
    if(!punchHook||!Synchronize()||!engine.GetViewModel||!engine.pfnAngleVectors)return false;
    ResetForwardDisplacement(forwardDisplacement);return true;
}
void GuestCofEffectsVideoInit() noexcept {if(Owner())ResetForwardDisplacement(forwardDisplacement);}
void GuestCofForwardDisplacementViewStage(const ref_params_s& ref) noexcept
{
    if(!GuestCofEffectsReady()||weapon==Weapon::None||attachedCamera||cameraMode>0||ref.nextView==1||
       ref.spectator||ref.intermission||ref.paused||ref.viewentity!=ref.playernum+1||
       !engine.GetViewModel||!engine.pfnAngleVectors)return;
    const auto serial=scopeSerial,active=generation,resources=resourceSerial;
    const auto selected=weapon;
    const auto viewIndex=weaponViewIndex;
    auto* model=engine.GetViewModel();
    if(!Synchronize()||scopeSerial!=serial||generation!=active||resourceSerial!=resources||!prepared||!model||
       viewIndex>=viewModels.size()||model->model!=viewModels[viewIndex])return;
    std::array<float,3> forward;float right[3],up[3];
    engine.pfnAngleVectors(ref.cl_viewangles,forward.data(),right,up);
    if(!Synchronize()||scopeSerial!=serial||generation!=active||resourceSerial!=resources||!prepared||
       weapon!=selected||weaponViewIndex!=viewIndex||model->model!=viewModels[viewIndex])return;
    LimitForwardDisplacement(forwardDisplacement);
    ApplyForwardDisplacement(forwardDisplacement,std::span<float,3>{model->origin,3},forward);
}
cl_enginefuncs_s* GuestCofMuzzleEngine() noexcept
{
    return GuestCofEffectsReady() ? &effectsEngine : nullptr;
}
void SetGuestCofEffectsWeapon(Weapon value) noexcept
{
    if(!Synchronize())return;
    weapon=value;
    const auto* definition=guest::cof::FindOriginalPistol(static_cast<int>(value));
    weaponViewIndex=definition?static_cast<unsigned>(definition->identity):viewModels.size();
}
void SetGuestCofEffectsCamera(bool attached,int mode) noexcept
{
    if (Synchronize()) { attachedCamera=attached; cameraMode=mode; }
}
bool GuestCofStudioEvent(const mstudioevent_s* event,const cl_entity_s* entity) noexcept
{
    if (!event || !entity || !entity->model ||
        (event->event!=5004 && event->event!=5006 && event->event!=5080) ||
        !Synchronize() || !engine.GetViewModel || entity!=engine.GetViewModel()) return false;
    auto selected=viewModels.size();
    for(const auto& group:PistolResourceGroups)
    {
        const auto index=static_cast<unsigned>(group.identity);
        const auto* definition=guest::cof::FindPistol(group.identity);
        if((prepared&&entity->model==viewModels[index])||
            !std::strncmp(entity->model->name,definition->viewModel.data(),sizeof(entity->model->name)))
        {selected=index;break;}
    }
    if (selected>=viewModels.size()) return false; // Ordinary CS paths stay native.
    if (!prepared || preparedGeneration!=generation)
        lastStudio=EffectResult::MissingBackend;
    else if (entity->model!=viewModels[selected] ||
        weaponViewIndex!=selected)
        lastStudio=EffectResult::InvalidEntity;
    else lastStudio=WeaponEffects({&effectsEngine,nullptr,Local}).StudioEvent(*event,*entity,weapon);
    return true; // An owned failure cannot play an unrelated native sample.
}
EffectResult GuestCofLastStudioEffect() noexcept { return Owner() ? lastStudio : EffectResult::MissingBackend; }
PunchResult GuestCofLastViewPunch() noexcept { return Owner() ? lastPunch : PunchResult::InvalidInput; }
GuestCofEffectsViewScope::GuestCofEffectsViewScope(const ref_params_s& ref) noexcept
{
    if (!Synchronize()) return;
    serial_=scopeSerial; previous_=currentRef;
    currentRef=ref.nextView!=1 ? &ref : nullptr;
}
GuestCofEffectsViewScope::~GuestCofEffectsViewScope()
{
    if (Owner() && serial_==scopeSerial) currentRef=previous_;
}
void ShutdownGuestCofEffects() noexcept
{
    if (!Owner()) return;
    AdvanceScope(); currentRef=nullptr; ClearResources();
    if (punchHook) RemoveHook(punchHook);
    punchHook=nullptr; eventRegistered=false; nativePunchStage=nullptr; connection=nullptr; generation=0;
    punch={}; punchUsed=false; attachedCamera=false; cameraMode=0; ownerThread=0;weaponViewIndex=viewModels.size();
    ResetForwardDisplacement(forwardDisplacement);
}
}
