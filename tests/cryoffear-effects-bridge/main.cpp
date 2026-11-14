// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/csol/client/weapons/guest_cof_audio.hpp"
#include "../../native/guest/cof/resource_catalog.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <event_api.h>
#include <event_args.h>
#include <cstdio>
#include <cstring>
#include <thread>

namespace csol::client { metahook_api_t* api{}; cl_enginefunc_t engine{}; engine_studio_api_t studio{}; }
namespace
{
using namespace csol::client;
using namespace guest::cof::client;
using namespace guest::cof::resources;
unsigned checks{},failures{},hooks{},removes{},modelLookups{},shells{},commands{},audioRandom{},audioVolume{},nativeSounds{};
std::uint64_t generation{1};
bool symbolAvailable{true},missingShell{},missingGlock{},missingFlash{},missingCheapSmoke{},changeAtModel{},hookFailure{},registrationFailure{};
bool revolverAvailable{};
unsigned char* patch{};
void* replacement{};
void (*viewPunch)(event_args_s*){};
cl_entity_t vm{},other{};
model_t glock{},p345{},revolver{},shell{},flash{},cheapSmoke{},ordinary{};
event_args_t impulse{};
ref_params_t* sampleRef{};
alignas(16) unsigned char beforeFx[512]{},afterFx[512]{};
unsigned expectedFlags{},actualFlags{};
float shellOrigin[3]{},shellVelocity[3]{};
char command[40]{};
ViewPunchState oracle;
void Check(bool value,int line){++checks;if(!value){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(x) Check((x),__LINE__)
void* Client(){return reinterpret_cast<void*>(0x10000000);}
mh_gamesymbol_status_t Resolve(void* module,const char* name,mh_gamesymbol_kind_t kind,void** out)
{
    CHECK(module==Client() && !std::strcmp(name,"GoldCraftCof_AfterNativePunch") && kind==MH_GAMESYMBOL_KIND_PATCH);
    if(!symbolAvailable)return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    *out=patch;return MH_GAMESYMBOL_OK;
}
std::uint64_t Generation(){return generation;}
int MaxClients(){return 32;}
void RegisterEvent(const char* path,void (*handler)(event_args_s*))
{
    CHECK(!std::strcmp(path,GlockEvents[1].host));
    if(registrationFailure) throw std::runtime_error("test event registration failure");
    viewPunch=handler;
}
pfnUserMsgHook HookMessage(const char*,pfnUserMsgHook){return nullptr;}
int RegisterMessage(const char*,pfnUserMsgHook){return 1;}
float Volume(const char*){++audioVolume;return 0;}
float Random(float,float){++audioRandom;return 0;}
void Remote(int,float*,int,const char*,float,float,int,int){++nativeSounds;}
bool Precached(void*,std::string_view)noexcept{return true;}
cl_entity_t* View(){return &vm;}
int Find(const char* path)
{
    ++modelLookups;
    if(changeAtModel){changeAtModel=false;++generation;}
    if(!std::strcmp(path,GlockModels[unsigned(GlockModel::View)].host))return missingGlock?0:7;
    if(!std::strcmp(path,P345Models[unsigned(P345Model::View)].host))return 8;
    if(!std::strcmp(path,RevolverModels[unsigned(RevolverModel::View)].host))return revolverAvailable?12:0;
    if(!std::strcmp(path,GlockModels[unsigned(GlockModel::Flash)].host))return missingFlash?0:10;
    if(!std::strcmp(path,GlockModels[unsigned(GlockModel::CheapSmoke)].host))return missingCheapSmoke?0:11;
    CHECK(!std::strcmp(path,GlockModels[unsigned(GlockModel::Shell)].host));return missingShell?0:9;
}
model_t* Model(int index){return index==7?&glock:index==8?&p345:index==9?&shell:index==10?&flash:index==11?&cheapSmoke:index==12?&revolver:nullptr;}
TEMPENTITY* Shell(float* origin,float* velocity,float* angles,float life,int index,int sound)
{
    ++shells;CHECK(index==9 && sound==1 && life==2.5f);
    CHECK(angles[0]==0 && angles[1]==0 && angles[2]==0);
    std::memcpy(shellOrigin,origin,sizeof(shellOrigin));std::memcpy(shellVelocity,velocity,sizeof(shellVelocity));
    static TEMPENTITY result;return &result;
}
int Command(const char* text){++commands;std::strcpy(command,text);return 0;}
std::array<std::byte,44+88200> Wave()
{
    std::array<std::byte,44+88200> out{};
    auto tag=[&](unsigned at,const char* text){std::memcpy(out.data()+at,text,4);};
    auto u16=[&](unsigned at,unsigned n){out[at]=std::byte(n&255);out[at+1]=std::byte(n>>8);};
    auto u32=[&](unsigned at,unsigned n){u16(at,n&65535);u16(at+2,n>>16);};
    tag(0,"RIFF");u32(4,unsigned(out.size())-8);tag(8,"WAVE");tag(12,"fmt ");u32(16,16);u16(20,1);u16(22,1);
    u32(24,22050);u32(28,44100);u16(32,2);u16(34,16);tag(36,"data");u32(40,88200);return out;
}
// Execute native engine punch exactly once, then the production hook and the
// original first instruction of its continuation. Observe the FP/SIMD state
// across that hook, before any subsequent host stair/smoothing/global work.
__declspec(naked) void ExecuteStage()
{
    __asm {
        pushfd
        pushad
        mov esi, dword ptr [sampleRef]
        movss xmm0, dword ptr [esi+0A0h]
        addss xmm0, dword ptr [esi+0Ch]
        movss dword ptr [esi+0Ch], xmm0
        movss xmm0, dword ptr [esi+0A4h]
        addss xmm0, dword ptr [esi+10h]
        movss dword ptr [esi+10h], xmm0
        movss xmm0, dword ptr [esi+0A8h]
        addss xmm0, dword ptr [esi+14h]
        movss dword ptr [esi+14h], xmm0
        fninit
        fld1
        fldpi
        movups xmm1, xmm0
        movups xmm2, xmm0
        movups xmm3, xmm0
        movups xmm4, xmm0
        movups xmm5, xmm0
        movups xmm6, xmm0
        movups xmm7, xmm0
        fxsave [beforeFx]
        cmp dword ptr [esi+0C4h], 0
        pushfd
        pop eax
        mov dword ptr [expectedFlags], eax
        stc
        call dword ptr [replacement]
        pushfd
        pop eax
        mov dword ptr [actualFlags], eax
        fxsave [afterFx]
        fninit
        popad
        popfd
        ret
    }
}
void Stage(ref_params_t& ref,bool applied)
{
    auto expected=ref;
    for(unsigned i=0;i<3;++i)expected.viewangles[i]+=expected.punchangle[i];
    if(applied)CHECK(ViewPunchAtStage(oracle,expected,32)==PunchResult::Applied);
    sampleRef=&ref;ExecuteStage();
    CHECK(!std::memcmp(expected.viewangles,ref.viewangles,sizeof(ref.viewangles)));
    CHECK(!std::memcmp(beforeFx,afterFx,sizeof(beforeFx)));CHECK(actualFlags==expectedFlags);
}
void Impulse(bool attached=false,int mode=0)
{
    SetGuestCofEffectsCamera(attached,mode);
    const auto expected=ViewPunchEvent(oracle,impulse,attached,mode);
    viewPunch(&impulse);CHECK(GuestCofLastViewPunch()==expected);
}
}
namespace csol::client
{
hook_t* Hook(void* address,void* next,void** original)
{
    CHECK(address==patch);
    if(hookFailure) throw std::runtime_error("test hook installation failure");
    ++hooks;replacement=next;*original=patch;return reinterpret_cast<hook_t*>(1);
}
void RemoveHook(hook_t* handle){CHECK(handle==reinterpret_cast<hook_t*>(1));++removes;}
}
int main()
{
    using namespace csol::client;
    metahook_api_t host{};host.GetClientBase=Client;host.ResolveGameSymbol=Resolve;host.HookUserMsg=HookMessage;api=&host;
    event_api_t events{};events.EV_FindModelIndex=Find;events.EV_PlaySound=Remote;
    efx_api_t fx{};fx.R_TempModel=Shell;
    engine.pEventAPI=&events;engine.pEfxAPI=&fx;engine.GetMaxClients=MaxClients;engine.pfnHookEvent=RegisterEvent;
    engine.pfnServerCmd=Command;engine.GetViewModel=View;engine.pfnHookUserMsg=RegisterMessage;
    engine.pfnGetCvarFloat=Volume;engine.pfnRandomFloat=Random;studio.GetModelByIndex=Model;
    glock.type=p345.type=shell.type=flash.type=ordinary.type=mod_studio;cheapSmoke.type=mod_sprite;
    std::strcpy(glock.name,GlockModels[0].host);std::strcpy(p345.name,P345Models[0].host);
    std::strcpy(shell.name,GlockModels[unsigned(GlockModel::Shell)].host);std::strcpy(ordinary.name,"models/v_glock18.mdl");
    std::strcpy(flash.name,GlockModels[unsigned(GlockModel::Flash)].host);
    std::strcpy(cheapSmoke.name,GlockModels[unsigned(GlockModel::CheapSmoke)].host);
    vm.model=&glock;
    GoldCraftConnectionAPI provider{sizeof(GoldCraftConnectionAPI),1,Generation};
    patch=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));CHECK(patch);
    const unsigned char original[]{0x83,0xbe,0xc4,0,0,0,0,0xc3};std::memcpy(patch,original,sizeof(original));
    CHECK(!InitializeGuestCofEffects(nullptr));symbolAvailable=false;CHECK(!InitializeGuestCofEffects(&provider));
    CHECK(!PrepareGuestCofEffects() && !GuestCofEffectsReady());symbolAvailable=true;
    patch[0]=0;CHECK(!InitializeGuestCofEffects(&provider));patch[0]=original[0];CHECK(hooks==0);
    hookFailure=true;CHECK(!InitializeGuestCofEffects(&provider));
    CHECK(!PrepareGuestCofEffects() && !GuestCofEffectsReady() && hooks==0);hookFailure=false;
    registrationFailure=true;CHECK(!InitializeGuestCofEffects(&provider));
    CHECK(!PrepareGuestCofEffects() && !GuestCofEffectsReady() && hooks==1 && removes==1);registrationFailure=false;
    CHECK(InitializeGuestCofEffects(&provider));CHECK(viewPunch && hooks==2);
    CHECK(!PrepareGuestCofEffects());
    mstudioevent_t unavailable{};unavailable.event=5004;std::strcpy(unavailable.options,"weapons/glock/glock_magin.wav");
    CHECK(GuestCofStudioEvent(&unavailable,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::MissingBackend);
    auto wave=Wave();
    std::array<GuestCofSoundAsset,6> sounds{{
        {GlockAudioResources[1].localKey,GlockAudioResources[1].remoteKey,GlockAudioResources[1].remoteAsset,wave,32},
        {P345AudioResources[1].localKey,P345AudioResources[1].remoteKey,P345AudioResources[1].remoteAsset,wave,32},
        {RevolverAudioResources[0].localKey,RevolverAudioResources[0].remoteKey,RevolverAudioResources[0].remoteAsset,wave,32},
        {RevolverAudioResources[1].localKey,RevolverAudioResources[1].remoteKey,RevolverAudioResources[1].remoteAsset,wave,32},
        {RevolverAudioResources[2].localKey,RevolverAudioResources[2].remoteKey,RevolverAudioResources[2].remoteAsset,wave,32},
        {RevolverAudioResources[3].localKey,RevolverAudioResources[3].remoteKey,RevolverAudioResources[3].remoteAsset,wave,32}}};
    GuestCofAudioPlan plan{{6,192,1024*1024},sounds,nullptr,Precached};
    InitializeGuestCofAudio(&provider);CHECK(PrepareGuestCofAudio(plan)==AudioResult::Applied);
    // Counterexample with every cold audio/model backend available: failed
    // hook/registration must still block preparation on its own.
    CHECK(GuestCofAudioReady());ShutdownGuestCofEffects();
    hookFailure=true;CHECK(!InitializeGuestCofEffects(&provider));
    CHECK(GuestCofAudioReady() && !PrepareGuestCofEffects() && !GuestCofEffectsReady());hookFailure=false;
    registrationFailure=true;CHECK(!InitializeGuestCofEffects(&provider));
    CHECK(GuestCofAudioReady() && !PrepareGuestCofEffects() && !GuestCofEffectsReady());registrationFailure=false;
    CHECK(InitializeGuestCofEffects(&provider));const auto baselineRemoves=removes;
    missingShell=true;CHECK(!PrepareGuestCofEffects());missingShell=false;
    missingFlash=true;CHECK(!PrepareGuestCofEffects() && !GuestCofMuzzleEngine());missingFlash=false;
    missingCheapSmoke=true;CHECK(!PrepareGuestCofEffects() && !GuestCofMuzzleEngine());missingCheapSmoke=false;
    flash.type=mod_sprite;CHECK(!PrepareGuestCofEffects());flash.type=mod_studio;
    cheapSmoke.type=mod_studio;CHECK(!PrepareGuestCofEffects());cheapSmoke.type=mod_sprite;
    CHECK(PrepareGuestCofEffects());CHECK(GuestCofEffectsReady());SetGuestCofEffectsWeapon(Weapon::Glock);
    auto* muzzleEngine=GuestCofMuzzleEngine();CHECK(muzzleEngine);
    const auto coldLookups=modelLookups;
    int flashHandle=-1;
    CHECK(muzzleEngine->CL_LoadModel("models/flash1.mdl",&flashHandle)==&flash && flashHandle==10);
    CHECK(muzzleEngine->pEventAPI->EV_FindModelIndex("sprites/gunsmoke.spr")==11);
    CHECK(muzzleEngine->pEventAPI->EV_FindModelIndex("models/shell.mdl")==9);
    CHECK(!muzzleEngine->CL_LoadModel("models/unreviewed.mdl",&flashHandle) && flashHandle==10);
    CHECK(!muzzleEngine->CL_LoadModel("models/flash1.mdl",nullptr));
    CHECK(modelLookups==coldLookups);
    mstudioevent_t event{};event.event=5004;std::strcpy(event.options,"weapons/glock/glock_magin.wav");
    auto count=modelLookups;CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::Dispatched);
    CHECK(audioRandom==1 && audioVolume==1 && nativeSounds==0 && modelLookups==count);
    CHECK(PrepareGuestCofEffects());count=modelLookups; // Resource rebuild preserves authenticated ownership.
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::Dispatched);
    std::strcpy(event.options,"weapons/glock/unknown.wav");CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::BackendFailed);
    event.event=5006;vm.attachment[1][0]=1;vm.attachment[1][1]=-2;vm.attachment[1][2]=3;
    vm.attachment[2][0]=4;vm.attachment[2][1]=-6;vm.attachment[2][2]=8;
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(shells==1 && modelLookups==count);
    CHECK(shellOrigin[0]==1 && shellOrigin[1]==-2 && shellOrigin[2]==3);
    CHECK(shellVelocity[0]==30 && shellVelocity[1]==-40 && shellVelocity[2]==50);
    event.event=5080;std::strcpy(event.options," -7tail");CHECK(GuestCofStudioEvent(&event,&vm));CHECK(commands==1 && !std::strcmp(command,"zoominmdl -7"));
    other=vm;CHECK(!GuestCofStudioEvent(&event,&other));vm.model=&ordinary;CHECK(!GuestCofStudioEvent(&event,&vm));vm.model=&glock;
    event.event=5005;CHECK(!GuestCofStudioEvent(&event,&vm));
    event.event=5004;std::strcpy(event.options,"ambient/unmapped.wav");
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::MissingBackend);
    model_t alias=glock;vm.model=&alias;CHECK(GuestCofStudioEvent(&event,&vm));
    CHECK(GuestCofLastStudioEffect()==EffectResult::InvalidEntity);vm.model=&glock;
    SetGuestCofEffectsWeapon(Weapon::P345);vm.model=&p345;event.event=5004;std::strcpy(event.options,"weapons/p345/p345_magin.wav");
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::Dispatched && audioRandom==3);
    ref_params_t ref{};ref.frametime=.01f;ref.viewangles[0]=12;ref.viewangles[1]=-23;ref.viewangles[2]=34;
    ref.punchangle[0]=1;ref.punchangle[1]=2;ref.punchangle[2]=-3;
    Stage(ref,false);
    impulse.origin[0]=.2f;impulse.origin[1]=-.1f;impulse.origin[2]=.4f;
    Impulse(true,0);
    {GuestCofEffectsViewScope scope(ref);Stage(ref,false);}
    Impulse(false,0);
    for(unsigned i=0;i<24;++i)
    {
        ref.smoothing=int(i%3)-1;
        {GuestCofEffectsViewScope scope(ref);Stage(ref,true);}
    }
    ref.nextView=1;{GuestCofEffectsViewScope scope(ref);Stage(ref,false);}
    ref.nextView=2;{GuestCofEffectsViewScope scope(ref);Stage(ref,true);}ref.nextView=0;
    {
        GuestCofEffectsViewScope outer(ref);Stage(ref,true);
        auto secondary=ref;secondary.nextView=1;
        {GuestCofEffectsViewScope inner(secondary);Stage(secondary,false);}Stage(ref,true);
    }
    SetGuestCofEffectsWeapon(Weapon::None);
    {GuestCofEffectsViewScope scope(ref);Stage(ref,true);} // Original globals survive weapon release.
    InvalidateGuestCofEffects();CHECK(!GuestCofEffectsReady());
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::MissingBackend);
    {GuestCofEffectsViewScope scope(ref);Stage(ref,true);} // Cold asset invalidation does not reset punch.
    CHECK(PrepareGuestCofEffects());Impulse(true,1);
    {GuestCofEffectsViewScope scope(ref);Stage(ref,true);}
    std::thread worker([&]{CHECK(!InitializeGuestCofEffects(&provider));SetGuestCofEffectsWeapon(Weapon::None);ShutdownGuestCofEffects();});
    worker.join();
    {
        GuestCofEffectsViewScope scope(ref);++generation;oracle={};Stage(ref,false);
    }
    CHECK(!GuestCofEffectsReady());CHECK(!PrepareGuestCofEffects());
    InitializeGuestCofAudio(&provider);CHECK(PrepareGuestCofAudio(plan)==AudioResult::Applied);
    changeAtModel=true;CHECK(!PrepareGuestCofEffects());
    {
        GuestCofEffectsViewScope stale(ref);
        ShutdownGuestCofEffects();CHECK(removes==baselineRemoves+1);
        CHECK(InitializeGuestCofEffects(&provider));
    }
    oracle={};Stage(ref,false); // Old scope must not resurrect its previous view.
    CHECK(PrepareGuestCofAudio(plan)==AudioResult::Applied);CHECK(PrepareGuestCofEffects());
    revolverAvailable=true;revolver.type=mod_studio;std::strcpy(revolver.name,RevolverModels[0].host);
    CHECK(PrepareGuestCofEffects());SetGuestCofEffectsWeapon(Weapon::Revolver);vm.model=&revolver;
    CHECK(GuestCofEffectsOwnsWeapon(Weapon::Revolver)&&!GuestCofEffectsOwnsWeapon(Weapon::Glock));
    auto* preparedEngine=GuestCofMuzzleEngine();CHECK(preparedEngine);
    const auto revolverLookups=modelLookups,beforeRevolverShells=shells;
    const auto beforeRevolverSound=audioRandom;
    for(const auto& sound:RevolverAudioResources)
    {
        event.event=5004;std::strcpy(event.options,"weapons/");
        std::strcat(event.options,sound.localKey.data()+std::strlen("weapons_new/"));
        CHECK(GuestCofStudioEvent(&event,&vm)&&GuestCofLastStudioEffect()==EffectResult::Dispatched);
    }
    CHECK(audioRandom==beforeRevolverSound+4&&modelLookups==revolverLookups&&nativeSounds==0);
    event.event=5006;CHECK(GuestCofStudioEvent(&event,&vm)&&GuestCofLastStudioEffect()==EffectResult::Dispatched);
    CHECK(shells==beforeRevolverShells+1&&modelLookups==revolverLookups);
    SetGuestCofEffectsWeapon(Weapon::Glock);
    CHECK(GuestCofStudioEvent(&event,&vm)&&GuestCofLastStudioEffect()==EffectResult::InvalidEntity&&shells==beforeRevolverShells+1);
    SetGuestCofEffectsWeapon(Weapon::Vp70);CHECK(!GuestCofEffectsOwnsWeapon(Weapon::Vp70));
    SetGuestCofEffectsWeapon(Weapon::Revolver);
    ++generation;CHECK(!GuestCofEffectsOwnsWeapon(Weapon::Revolver));
    InitializeGuestCofAudio(&provider);CHECK(PrepareGuestCofAudio(plan)==AudioResult::Applied);CHECK(PrepareGuestCofEffects());
    SetGuestCofEffectsWeapon(Weapon::Glock);vm.model=&glock;event.event=5004;
    std::strcpy(event.options,"weapons/glock/glock_magin.wav");
    ShutdownGuestCofAudio();CHECK(!GuestCofEffectsReady());
    CHECK(GuestCofStudioEvent(&event,&vm));CHECK(GuestCofLastStudioEffect()==EffectResult::BackendFailed);
    ShutdownGuestCofEffects();CHECK(removes==baselineRemoves+2);ShutdownGuestCofEffects();CHECK(removes==baselineRemoves+2);
    ShutdownGuestCofAudio();VirtualFree(patch,0,MEM_RELEASE);
    std::printf("%u checks, %u failures; real XAudio2 prepared PCM; x86 punch stage; native audio fallbacks %u\n",checks,failures,nativeSounds);
    return failures?1:0;
}
