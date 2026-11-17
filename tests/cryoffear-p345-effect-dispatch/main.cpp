// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_effects.hpp"
#include "native/csol/server/weapons/guest_cof_p345_owner.hpp"
#include "native/guest/cof/presentation_protocol.hpp"
#include "native/guest/cof/particle_protocol.hpp"
#include "native/guest/cof/crosshair_protocol.hpp"
#include "native/guest/cof/sound_protocol.hpp"
#include "native/guest/cof/control_protocol.hpp"
#include "native/guest/cof/server/effects_dispatch.hpp"
#include "native/guest/cof/first_draw_lifecycle.hpp"
#include "native/guest/cof/server/player_think.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <map>

namespace dispatch_fixture
{
using namespace csol::server::cof;
bool valid=true,retireBegin{};
unsigned inventoryCalls{},tabulates{},viewAnimations{},playbacks{};
unsigned revolverShellEvents{},revolverFireEvents{},shotgunBallisticsCalls{};
std::map<std::string,int> messages;
struct Packet{int destination{},id{};edict_t* target{};std::vector<std::byte> bytes;};
std::vector<Packet> packets;
bool PacketCurrent(void*) noexcept{return valid;}
void Byte(int value){packets.back().bytes.push_back(std::byte(value&255));}
void Short(int value){Byte(value);Byte(value>>8);}
void Coord(float value){Short(int(double(value)*8));}
void String(const char* value){do{Byte(static_cast<unsigned char>(*value));}while(*value++);}
void Engine()
{
    g_engfuncs.pfnRegUserMsg=[](const char* name,int){return messages.try_emplace(name,int(messages.size())+100).first->second;};
    g_engfuncs.pfnMessageBegin=[](int kind,int id,const float*,edict_t* target){
        packets.push_back({kind,id,target,{}});if(retireBegin)valid=false;
    };
    g_engfuncs.pfnMessageEnd=[]{};
    g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;g_engfuncs.pfnWriteCoord=Coord;
    g_engfuncs.pfnWriteAngle=[](float angle){Byte(int(angle*256.F/360.F));};
    g_engfuncs.pfnWriteString=String;
    g_engfuncs.pfnGetPlayerUserId=[](edict_t*){return 1;};
    g_engfuncs.pfnGetInfoKeyBuffer=[](edict_t*)->char*{static char value[]="";return value;};
    g_engfuncs.pfnInfoKeyValue=[](char*,const char*)->char*{static char value[]="1";return value;};
    g_engfuncs.pfnPEntityOfEntIndex=[](int index)->edict_t*{return index==1?player.edict():nullptr;};
    g_engfuncs.pfnPlaybackEvent=[](int flags,const edict_t* owner,unsigned short index,float delay,
        float* origin,float* angles,float a,float b,int i,int j,int c,int d){
        Check(owner==player.edict()&&a==0&&b==0&&c==0&&d==0);
        if(index==37)
        {
            Check(flags==0&&delay==1.25F&&i==5&&j==0&&origin[0]==11&&origin[1]==12&&origin[2]==13&&
                angles[0]==0&&angles[1]==0&&angles[2]==0);++revolverShellEvents;return;
        }
        Check(delay==0);
        if(flags==FEV_HOSTONLY)Check(index==36&&i==0&&j==0&&origin[0]==angles[0]);
        else if(i==21){Check(flags==0&&index==35&&j==19);++revolverFireEvents;return;}
        else Check(flags==1&&index==35&&(i==7||i==19||i==14||i==27||i==9||i==26||i==4)&&j==19);
        ++playbacks;
    };
}
PresentationState State()
{
    const auto& packet=packets.back();Check(packet.id==messages.at(StateMessage)&&packet.destination==MSG_ONE&&packet.target==player.edict());
    PresentationState value{};Check(DecodeState(packet.bytes,value));return value;
}
PresentationState LatestState()
{
    const auto found=std::find_if(packets.rbegin(),packets.rend(),[](const auto& p){return p.id==messages.at(StateMessage);});
    Check(found!=packets.rend());PresentationState result{};Check(DecodeState(found->bytes,result));return result;
}
struct M16Context
{
    GlockEffects& effects;const M16Resources& resources;OwnerBinding binding;
    M16OwnerFrame frame;
};
bool M16Current(void* p) noexcept
{return GlockOwnerRuntime::BackendCurrent(&static_cast<M16Context*>(p)->binding)&&valid;}
bool M16Frame(void* p,M16OwnerFrame& frame) noexcept
{if(!M16Current(p))return false;frame=static_cast<M16Context*>(p)->frame;return true;}
bool M16Dispatch(void* p,CBasePlayer& actor,CBasePlayerWeapon& item,const M16Event& event) noexcept
{
    auto& value=*static_cast<M16Context*>(p);
    // Native sound and shot latch are separate, already-tested providers.
    // Their controlled callbacks must not stand in for a presentation result.
    constexpr auto native=(1u<<unsigned(M16Effect::EmptyClick))|(1u<<unsigned(M16Effect::Sleeve))|
        (1u<<unsigned(M16Effect::PickupSound))|(1u<<unsigned(M16Effect::AmmoPickupSound))|
        (1u<<unsigned(M16Effect::FireSound))|(1u<<unsigned(M16Effect::ShotLatch));
    if((1u<<unsigned(event.kind))&native)return M16Current(p);
    return value.effects.Dispatch(value.resources,value.binding,actor,item,event);
}
}
void CBasePlayer::TabulateAmmo(){Check(this==&player);++dispatch_fixture::tabulates;}
void CSOL_UpdateWeaponInventory(CBasePlayer* actor){Check(actor==&player);++dispatch_fixture::inventoryCalls;}
void CBasePlayerWeapon::SendWeaponAnim(int animation,int body)
{Check(m_pPlayer==&player&&body==0&&animation>=0&&animation<=42);++dispatch_fixture::viewAnimations;}

namespace guest::cof
{
BallisticsResult FireShotgunBallistics(CBaseEntity& actor,const PistolBallistics& shot,
    const BallisticsEffects& effects,const BallisticsCall& call) noexcept
{
    Check(&actor==&player&&CheckPistolBallistics(shot,effects,call)==BallisticsStatus::Ready);
    Check(Bits(shot.spread.x)==0x3D8EDE55&&Bits(shot.spread.y)==0x3D8EDE55&&Bits(shot.spread.z)==0x3D8EDE55);
    ++dispatch_fixture::shotgunBallisticsCalls;return {BallisticsStatus::Fired,{}};
}
}

#include "../cryoffear-g43-effects/scenarios.hpp"
#include "../cryoffear-shotgun-effects/scenarios.hpp"

int main(int argc,char** argv)
{
    if(argc<2||argc>4)return 1;
    const bool g43=argc>2&&std::string_view(argv[2])=="g43";
    const bool shotgun=argc==4&&std::string_view(argv[3])=="shotgun";
    if((argc==3&&!g43)||(argc==4&&(!g43||!shotgun)))return 1;
    using namespace dispatch_fixture;
    std::ifstream input(argv[1],std::ios::binary);Check(input.good());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());RuntimeInitialize();Engine();
    GlockResources glock{};glock.generation=runtimeView.binding.map;glock.events={35,36};glock.viewModelString=prepared.viewModelString;
    glock.models[unsigned(GlockModel::Shell)]=31;
    P345Resources resources{};resources.generation=glock.generation;resources.events=glock.events;
    resources.viewModelString=100;resources.models[unsigned(P345Model::Shell)]=31;
    GlockEffects effects;Check(effects.Prepare({nullptr,PacketCurrent,glock.generation},glock,{true,true}));
    Check(effects.Capabilities()!=0&&effects.P345Capabilities()&(1u<<unsigned(P345Effect::Shell)));
    const auto particleMessage=messages.at(ParticleMessage);
    for(bool immediate:{false,true})
    {
        Check(SendKillLocalParticles(player,immediate,particleMessage,{nullptr,PacketCurrent})==WeaponEffectStatus::Issued);
        ParticleMessageData killed{};
        Check(DecodeParticle(packets.back().bytes,killed)&&killed.operation==ParticleOperation::KillLocal&&
            killed.immediate==immediate&&!killed.originalWeapon&&!killed.owner);
        Check(packets.back().target==player.edict()&&packets.back().id==particleMessage&&
            packets.back().destination==MSG_ONE_UNRELIABLE);
    }
    const auto beforeKillNegative=packets.size();
    Check(SendKillLocalParticles(player,true,0,{nullptr,PacketCurrent})==WeaponEffectStatus::MissingMessage);
    Check(SendKillLocalParticles(player,true,256,{nullptr,PacketCurrent})==WeaponEffectStatus::MissingMessage);
    Check(SendKillLocalParticles(player,true,particleMessage,{})==WeaponEffectStatus::InvalidInput);
    valid=false;
    Check(SendKillLocalParticles(player,true,particleMessage,{nullptr,PacketCurrent})==WeaponEffectStatus::Cancelled);
    valid=true;Check(packets.size()==beforeKillNegative);
    retireBegin=true;
    Check(SendKillLocalParticles(player,true,particleMessage,{nullptr,PacketCurrent})==WeaponEffectStatus::Cancelled);
    Check(packets.size()==beforeKillNegative+1&&packets.back().bytes.size()==ParticleBytes);
    retireBegin=false;valid=true;
    GlockEvent glockAim{GlockEffect::Aim};glockAim.a=350;
    Check(effects.Dispatch(glock,runtimeView.binding,player,weapon,glockAim));const auto first=State();Check(first.originalWeapon==3);

    P345OwnerView pview;P345OwnerBackendView pv{};std::string error;
    Check(pview.Prepare(player,runtimeView,{true,61,11,24},PistolAnimationRig::Stock,model,_stricmp,pv,error));
    P345Weapon pistol;edict_t entity{};entity.serialnumber=71;entity.pvPrivateData=&pistol;entity.v.pContainingEntity=&entity;
    pistol.pev=&entity.v;pistol.m_iPrimaryAmmoType=1;pistol.m_iClip=8;
    P345Prepared cold{};cold.sequences=prepared.sequences;cold.ballistics=prepared.ballistics;cold.melee=prepared.melee;
    cold.aimOffset=prepared.aimOffset;cold.aimZoom=55;cold.viewModelString=prepared.viewModelString;
    cold.playerModelString=prepared.playerModelString;cold.worldModelString=prepared.worldModelString;
    cold.inventoryItemString=prepared.inventoryItemString;cold.shellModel=31;cold.hostSlot=1;cold.hostPosition=1;
    cold.modelsReady=cold.soundsReady=cold.particleReady=cold.aimMetadataReady=cold.inventoryReady=cold.pickupSoundReady=cold.deployPreludeReady=true;
    cold.world=MeleeWorld::Multiplayer;
    P345Backends backend{&pv.binding,P345OwnerView::BackendCurrent,P345OwnerView::BackendFrame,
        [](void*,CBasePlayer&,CBasePlayerWeapon&,const P345Event&) noexcept{return true;},P345RequiredEffects,pv.animation,pv.animationGeneration};
    Check(pistol.Bind(player,cold,backend)==P345Status::Ready);player.m_pActiveItem=&pistol;Check(pistol.Deploy());
    P345Event aim{P345Effect::Aim};aim.enabled=true;aim.value=55;aim.a=350;
    Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,aim));const auto second=State();
    Check(second.originalWeapon==7&&second.aimEnabled&&second.fovMode==55&&second.revision>first.revision&&second.key.generation>first.key.generation);
    Check(effects.SynchronizeSelection(player)&&packets.back().id==messages.at(StateMessage));
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));
    guest::cof::CrosshairSnapshot scalar{};Check(DecodeCrosshair(packets.back().bytes,scalar)&&scalar.key==second.key&&scalar.presentationRevision==second.revision);
    P345Event smoke{P345Effect::Gunsmoke};smoke.value=19;smoke.enabled=true;smoke.modelString=resources.viewModelString;
    Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,smoke));
    ParticleMessageData particle{};Check(DecodeParticle(packets.back().bytes,particle)&&particle.originalWeapon==7&&particle.owner==19);
    P345Event shell{P345Effect::Shell};shell.first={1,2,3};shell.second={4,5,6};shell.value=31;shell.other=1;
    Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,shell)&&packets.back().id==SVC_TEMPENTITY&&packets.back().destination==MSG_PVS);
    Check(std::to_integer<unsigned>(packets.back().bytes.back())==25);
    const auto before=packets.size();shell.value=32;Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,shell)&&packets.size()==before);
    P345Event fire{P345Effect::FireEvent};fire.value=7;fire.other=1;
    Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,fire)&&playbacks==1);
    P345Event recoil{P345Effect::Recoil};recoil.first={1,3,3};Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,recoil)&&playbacks==2);
    Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,{P345Effect::FireSound}));
    SoundMessageData sound{};Check(DecodeSound(packets.back().bytes,sound)&&sound.sample=="weapons_new/p345/p345_fire.wav");
    P345Event view{P345Effect::ViewAnimation};view.value=33;Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,view)&&viewAnimations==1);
    view.value=34;Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,view)&&viewAnimations==1);
    P345Event inventory{P345Effect::InventoryChanged};Check(effects.Dispatch(resources,runtimeView.binding,player,pistol,inventory)&&tabulates==1&&inventoryCalls==1);
    auto wrongResources=resources;++wrongResources.events[0];Check(!effects.Dispatch(wrongResources,runtimeView.binding,player,pistol,aim));
    Check(!effects.Dispatch(resources,runtimeView.binding,player,weapon,aim));

    player.m_pActiveItem=&weapon;Check(effects.Dispatch(glock,runtimeView.binding,player,weapon,glockAim));
    const auto third=State();Check(third.originalWeapon==3&&third.revision>second.revision&&third.key.generation>second.key.generation);
    player.m_pActiveItem=nullptr;Check(effects.SynchronizeSelection(player));const auto release=State();
    Check(release.originalWeapon==0&&release.revision>third.revision&&release.key==third.key);

    RevolverResources revolverResources{};revolverResources.generation=glock.generation;
    revolverResources.events={35,36,37};revolverResources.viewModelString=100;
    revolverResources.models[unsigned(RevolverModel::Shell)]=31;
    const auto revolverCapabilities=effects.RevolverCapabilities(revolverResources);
    Check((revolverCapabilities&(1u<<unsigned(RevolverEffect::EjectEvent)))&&
        (revolverCapabilities&(1u<<unsigned(RevolverEffect::Cylinder)))&&
        !(revolverCapabilities&(1u<<unsigned(RevolverEffect::ReloadStage))));
    RevolverPrepared unsupported{};unsupported.reloadStagesReady=true;
    Check(effects.FillPrepared(revolverResources,unsupported)&&!unsupported.reloadStagesReady&&
        unsupported.cylinderReady&&unsupported.ejectEventReady&&unsupported.ammoXReady);
    RevolverWeapon revolver;edict_t revolverEntity{};revolverEntity.serialnumber=81;
    revolverEntity.pvPrivateData=&revolver;revolverEntity.v.pContainingEntity=&revolverEntity;
    revolver.pev=&revolverEntity.v;revolver.m_iId=898;revolver.m_iPrimaryAmmoType=1;revolver.m_pPlayer=&player;
    player.m_pActiveItem=&revolver;
    RevolverOwnerState revolverState{true,72,12.5F};
    RevolverEvent revAim{RevolverEffect::Aim};revAim.enabled=true;revAim.value=55;revAim.a=350;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,revAim,revolverState));
    const auto revolverPresentation=State();
    Check(revolverPresentation.originalWeapon==21&&revolverPresentation.revolverAngle==72&&
        revolverPresentation.aimEnabled&&revolverPresentation.key.generation>third.key.generation);
    RevolverEvent cylinder{RevolverEffect::Cylinder};cylinder.a=72;cylinder.b=15;cylinder.enabled=true;
    revolverState.cylinderGate=15;
    const auto beforeSameCylinder=packets.size();
    for(int i=0;i<1000;++i)
    {
        cylinder.b=16+float(i);revolverState.cylinderGate=cylinder.b;
        Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,cylinder,revolverState));
    }
    Check(packets.size()==beforeSameCylinder); // Gate state advances without redundant visual payloads.
    cylinder.a=60;revolverState.cylinderAngle=60;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,cylinder,revolverState)&&packets.size()==beforeSameCylinder+1);
    const auto movedCylinder=State();
    Check(movedCylinder.revolverAngle==60&&movedCylinder.aimEnabled&&movedCylinder.fovMode==55&&
        movedCylinder.zoomSpeedEighths==2800&&movedCylinder.revision==revolverPresentation.revision+1);
    cylinder.a=48;const auto beforeBad=packets.size();
    Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,cylinder,revolverState)&&packets.size()==beforeBad);
    Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,pistol,revAim,revolverState));
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,{RevolverEffect::FirstDraw},revolverState));
    auto undrawn=revolverState;undrawn.firstDrawn=false;
    Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,{RevolverEffect::FirstDraw},undrawn));
    Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,{RevolverEffect::ReloadStage},revolverState));
    GlockEffects timedEffects;
    Check(timedEffects.Prepare({nullptr,PacketCurrent,glock.generation},glock,{true,true,true}));
    RevolverPrepared timedCold{};Check(timedEffects.FillPrepared(revolverResources,timedCold)&&timedCold.reloadStagesReady);
    RevolverEvent stage{RevolverEffect::ReloadStage};globals.time=20;stage.a=22;
    Check(!timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,revolverState));
    auto committedStage=revolverState;committedStage.reloadUntil=22;
    Check(timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,committedStage));
    stage.a=23;Check(!timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,committedStage));
    stage.value=1;stage.a=21;committedStage.reloadUntil=21;
    Check(timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,committedStage));
    stage.value=2;stage.a=0;committedStage.reloadUntil=22.0333F;
    Check(timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,committedStage));
    committedStage.reloadUntil=19;Check(!timedEffects.Dispatch(revolverResources,runtimeView.binding,player,revolver,stage,committedStage));
    timedEffects.EndMap();
    RevolverEvent ammo{RevolverEffect::AmmoX};ammo.value=1;ammo.other=254;player.m_rgAmmo[1]=500;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,ammo,revolverState));
    const std::vector<std::byte> expectedAmmo{std::byte{1},std::byte{254}};
    Check(packets.back().id==messages.at("AmmoX")&&packets.back().bytes==expectedAmmo&&player.m_rgAmmo[1]==500);
    ammo.other=255;Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,ammo,revolverState));
    RevolverEvent shotState{RevolverEffect::ShotState};shotState.a=4;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,shotState,revolverState));
    ControlData shot{};Check(DecodeControl(packets.back().bytes,shot)&&shot.operation==ControlOperation::ShotState&&shot.body==4);
    shotState.a=3;Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,shotState,revolverState));
    RevolverEvent shellSound{RevolverEffect::EjectEvent};shellSound.first={11,12,13};shellSound.a=1.25F;shellSound.value=5;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,shellSound,revolverState)&&revolverShellEvents==1);
    RevolverEvent revolverFire{RevolverEffect::FireEvent};revolverFire.value=21;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,revolverFire,revolverState)&&revolverFireEvents==1);
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,{RevolverEffect::FireSound},revolverState));
    Check(DecodeSound(packets.back().bytes,sound)&&sound.sample=="weapons_new/revolver/revolver_fire.wav");
    RevolverEvent revView{RevolverEffect::ViewAnimation};revView.value=20;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,revView,revolverState));
    revView.value=21;Check(!effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,revView,revolverState));
    const auto oldSerial=revolverEntity.serialnumber;++revolverEntity.serialnumber;
    Check(effects.Dispatch(revolverResources,runtimeView.binding,player,revolver,revAim,revolverState));
    const auto newInstance=State();Check(newInstance.key.generation>movedCylinder.key.generation);
    revolverEntity.serialnumber=oldSerial;
    VP70Resources vp70Resources{};vp70Resources.generation=glock.generation;vp70Resources.events=glock.events;
    vp70Resources.viewModelString=100;vp70Resources.models[unsigned(VP70Model::Shell)]=31;
    VP70Prepared vp70Cold{};Check(effects.FillPrepared(vp70Resources,vp70Cold));
    Check((effects.VP70Capabilities()&(1u<<unsigned(VP70Effect::Shell)))&&
        (effects.VP70Capabilities()&(1u<<unsigned(VP70Effect::FirstDraw)))&&
        !(effects.VP70Capabilities()&(1u<<unsigned(VP70Effect::FireSound))));
    VP70Weapon vp70;edict_t vp70Entity{};vp70Entity.serialnumber=91;vp70Entity.pvPrivateData=&vp70;
    vp70Entity.v.pContainingEntity=&vp70Entity;vp70.pev=&vp70Entity.v;vp70.m_iId=899;
    vp70.m_iPrimaryAmmoType=1;vp70.m_iClip=18;vp70.m_pPlayer=&player;player.m_pActiveItem=&vp70;
    VP70Event vpAim{VP70Effect::Aim};vpAim.enabled=true;vpAim.value=55;vpAim.a=350;
    Check(effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,vpAim,true));
    const auto vpPresentation=State();Check(vpPresentation.originalWeapon==19&&vpPresentation.aimEnabled&&
        !vpPresentation.revolverAngle&&vpPresentation.key.generation>newInstance.key.generation);
    const auto beforeFirstDraw=packets.size();
    Check(effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,{VP70Effect::FirstDraw},true));
    Check(!effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,{VP70Effect::FirstDraw},false)&&packets.size()==beforeFirstDraw);
    Check(!effects.Dispatch(vp70Resources,runtimeView.binding,player,pistol,{VP70Effect::FirstDraw},true));
    Check(!effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,{VP70Effect::FireSound},true)&&packets.size()==beforeFirstDraw);
    VP70Event vpShell{VP70Effect::Shell};vpShell.first={1,2,3};vpShell.second={4,5,6};
    vpShell.a=7;vpShell.value=31;vpShell.other=1;
    Check(effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,vpShell,true)&&
        packets.back().bytes[0]==std::byte(TE_MODEL));
    VP70Event vpFire{VP70Effect::FireEvent};vpFire.value=19;vpFire.other=1;
    const auto beforeVpFire=playbacks;
    Check(effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,vpFire,true)&&playbacks==beforeVpFire+1);
    VP70Event vpView{VP70Effect::ViewAnimation};vpView.value=22;
    Check(effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,vpView,true));
    vpView.value=23;Check(!effects.Dispatch(vp70Resources,runtimeView.binding,player,vp70,vpView,true));
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));
    CrosshairSnapshot vpSpread{};Check(DecodeCrosshair(packets.back().bytes,vpSpread)&&vpSpread.scalar==0&&vpSpread.key==vpPresentation.key);
    TMPResources tmpResources{};tmpResources.generation=glock.generation;tmpResources.events=glock.events;
    tmpResources.viewModelString=100;tmpResources.models[unsigned(TMPModel::Shell)]=31;
    TMPPrepared tmpCold{};Check(effects.FillPrepared(tmpResources,tmpCold));
    Check(tmpCold.ballistics.customDecalMessage&&tmpCold.melee.shakeMessage&&tmpCold.particleReady&&
        tmpCold.deployPreludeReady&&tmpCold.world==MeleeWorld::Multiplayer);
    const auto tmpCapabilities=effects.TMPCapabilities();
    Check((tmpCapabilities&(1u<<unsigned(TMPEffect::Shell)))&&
        (tmpCapabilities&(1u<<unsigned(TMPEffect::FirstDraw)))&&
        (tmpCapabilities&(1u<<unsigned(TMPEffect::KillLocalParticles)))&&
        !(tmpCapabilities&(1u<<unsigned(TMPEffect::FireSound)))&&
        !(tmpCapabilities&(1u<<unsigned(TMPEffect::AutomaticSound))));
    TMPWeapon tmp;edict_t tmpEntity{};tmpEntity.serialnumber=92;tmpEntity.pvPrivateData=&tmp;
    tmpEntity.v.pContainingEntity=&tmpEntity;tmp.pev=&tmpEntity.v;tmp.m_iId=900;
    tmp.m_iPrimaryAmmoType=1;tmp.m_iClip=30;tmp.m_pPlayer=&player;player.m_pActiveItem=&tmp;
    TMPEvent tmpAim{TMPEffect::Aim};tmpAim.enabled=true;tmpAim.value=50;tmpAim.a=350;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpAim,true));
    const auto tmpPresentation=State();Check(tmpPresentation.originalWeapon==14&&tmpPresentation.aimEnabled&&
        !tmpPresentation.revolverAngle&&tmpPresentation.key.generation>vpPresentation.key.generation);
    const auto beforeTmpDraw=packets.size();
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,{TMPEffect::FirstDraw},true));
    Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,{TMPEffect::FirstDraw},false)&&packets.size()==beforeTmpDraw);
    Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,vp70,{TMPEffect::FirstDraw},true));
    Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,{TMPEffect::FireSound},true));
    Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,{TMPEffect::AutomaticSound},true));
    for(bool immediate:{false,true})
    {
        TMPEvent kill{TMPEffect::KillLocalParticles};kill.enabled=immediate;
        Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,kill,true));
        ParticleMessageData killed{};Check(DecodeParticle(packets.back().bytes,killed)&&
            killed.operation==ParticleOperation::KillLocal&&killed.immediate==immediate&&
            packets.back().destination==MSG_ONE_UNRELIABLE);
    }
    TMPEvent tmpShot{TMPEffect::ShotState};tmpShot.a=1;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpShot,true));
    Check(DecodeControl(packets.back().bytes,shot)&&shot.operation==ControlOperation::ShotState&&shot.body==1);
    tmpShot.a=3;Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpShot,true));
    TMPEvent tmpSmoke{TMPEffect::Gunsmoke};tmpSmoke.value=19;tmpSmoke.enabled=true;tmpSmoke.modelString=100;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpSmoke,true));
    Check(DecodeParticle(packets.back().bytes,particle)&&particle.originalWeapon==14);
    TMPEvent tmpShell{TMPEffect::Shell};tmpShell.first={1,2,3};tmpShell.second={4,5,6};tmpShell.value=31;tmpShell.other=1;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpShell,true)&&packets.back().bytes[0]==std::byte(TE_MODEL));
    TMPEvent tmpView{TMPEffect::ViewAnimation};tmpView.value=15;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpView,true));
    tmpView.value=16;Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpView,true));
    TMPEvent tmpFire{TMPEffect::FireEvent};tmpFire.value=14;tmpFire.other=1;const auto tmpPlays=playbacks;
    Check(effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,tmpFire,true)&&playbacks==tmpPlays+1);
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));
    CrosshairSnapshot tmpSpread{};Check(DecodeCrosshair(packets.back().bytes,tmpSpread)&&tmpSpread.key==tmpPresentation.key&&tmpSpread.scalar==0);
    auto wrongTMP=tmpResources;++wrongTMP.generation;
    const auto beforeWrongTMP=packets.size();TMPEvent kill{TMPEffect::KillLocalParticles};kill.enabled=true;
    Check(!effects.Dispatch(wrongTMP,runtimeView.binding,player,tmp,kill,true)&&packets.size()==beforeWrongTMP);
    ++wrongTMP.events[0];wrongTMP.generation=tmpResources.generation;
    Check(!effects.Dispatch(wrongTMP,runtimeView.binding,player,tmp,kill,true)&&packets.size()==beforeWrongTMP);
    retireBegin=true;Check(!effects.Dispatch(tmpResources,runtimeView.binding,player,tmp,kill,true));
    Check(packets.back().bytes.size()==ParticleBytes);retireBegin=false;valid=true;
    MP5Resources mp5Resources{};mp5Resources.generation=glock.generation;mp5Resources.events=glock.events;
    mp5Resources.viewModelString=100;mp5Resources.models[unsigned(MP5Model::Shell)]=31;
    TMPPrepared mp5Cold{};Check(effects.FillPrepared(mp5Resources,mp5Cold));
    Check(mp5Cold.ballistics.customDecalMessage&&mp5Cold.melee.shakeMessage&&mp5Cold.particleReady&&
        mp5Cold.deployPreludeReady&&mp5Cold.world==MeleeWorld::Multiplayer);
    Check(effects.MP5Capabilities()==(effects.TMPCapabilities()&~(1u<<unsigned(TMPEffect::FirstDraw))));
    MP5Weapon mp5;edict_t mp5Entity{};mp5Entity.serialnumber=93;mp5Entity.pvPrivateData=&mp5;
    mp5Entity.v.pContainingEntity=&mp5Entity;mp5.pev=&mp5Entity.v;mp5.m_iId=901;
    mp5.m_iPrimaryAmmoType=1;mp5.m_iClip=30;mp5.m_pPlayer=&player;player.m_pActiveItem=&mp5;
    Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,tmpAim));
    const auto mp5Presentation=State();Check(mp5Presentation.originalWeapon==27&&mp5Presentation.aimEnabled&&
        mp5Presentation.key.generation>tmpPresentation.key.generation&&!mp5Presentation.revolverAngle);
    const auto noFirstDraw=packets.size();
    Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,{TMPEffect::FirstDraw})&&packets.size()==noFirstDraw);
    Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,{TMPEffect::AutomaticSound}));
    for(bool immediate:{false,true})
    {
        TMPEvent killed{TMPEffect::KillLocalParticles};killed.enabled=immediate;
        Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,killed));
        ParticleMessageData packet{};Check(DecodeParticle(packets.back().bytes,packet)&&
            packet.operation==ParticleOperation::KillLocal&&packet.immediate==immediate);
    }
    Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,tmpSmoke));
    Check(DecodeParticle(packets.back().bytes,particle)&&particle.originalWeapon==27);
    tmpShot.a=1;Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,tmpShot));
    Check(DecodeControl(packets.back().bytes,shot)&&shot.operation==ControlOperation::ShotState&&shot.body==1);
    tmpShot.a=3;Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,tmpShot));
    Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,tmpShell)&&packets.back().bytes[0]==std::byte(TE_MODEL));
    for(int sequence:{0,7,8,11,19,20})
    {TMPEvent view{TMPEffect::ViewAnimation};view.value=sequence;Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,view));}
    TMPEvent invalidView{TMPEffect::ViewAnimation};invalidView.value=21;
    Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,invalidView));
    TMPEvent mp5Fire{TMPEffect::FireEvent};mp5Fire.value=27;mp5Fire.other=1;const auto beforeMp5Fire=playbacks;
    Check(effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,mp5Fire)&&playbacks==beforeMp5Fire+1);
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));
    CrosshairSnapshot mp5Spread{};Check(DecodeCrosshair(packets.back().bytes,mp5Spread)&&mp5Spread.key==mp5Presentation.key&&mp5Spread.scalar==0);
    const auto noCrossIdentity=packets.size();
    Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,tmp,kill)&&packets.size()==noCrossIdentity);
    auto staleMP5=mp5Resources;++staleMP5.generation;
    Check(!effects.Dispatch(staleMP5,runtimeView.binding,player,mp5,kill)&&packets.size()==noCrossIdentity);
    retireBegin=true;Check(!effects.Dispatch(mp5Resources,runtimeView.binding,player,mp5,kill));
    Check(packets.back().bytes.size()==ParticleBytes);retireBegin=false;valid=true;
    M16Resources m16Resources{};m16Resources.generation=glock.generation;m16Resources.events=glock.events;
    m16Resources.viewModelString=prepared.viewModelString;m16Resources.models[unsigned(M16Model::Shell)]=31;
    M16Prepared m16Cold{};Check(effects.FillPrepared(m16Resources,m16Cold));
    Check(!m16Cold.pushbackReady&&m16Cold.world==MeleeWorld::Multiplayer&&m16Cold.particleReady&&m16Cold.deployPreludeReady);
    Check((effects.M16Capabilities()&(1u<<unsigned(M16Effect::Aim)))&&
        !(effects.M16Capabilities()&(1u<<unsigned(M16Effect::FireSound))));
    M16Context m16Context{effects,m16Resources,runtimeView.binding,{}};
    m16Context.frame.reserveLimit=60;m16Context.frame.stamina=100;
    m16Context.frame.sourceMode=PistolSource::NormalMuzzle;m16Context.frame.bulletDamage=65;
    PlayerAnimationAdapter m16Animations;
    const auto m16Plan=RecommendedLongGunAnimationPlan(LongGun::M16,PistolAnimationRig::Stock);
    Check(m16Plan&&m16Animations.Prepare(player,model,runtimeView.animationGeneration,LongGun::M16,*m16Plan,_stricmp,error));
    PlayerThinkSlot m16Think;
    Check(m16Think.Bind(player,{&m16Context,[](void* p,CBasePlayer&,std::uint64_t,std::uint64_t) noexcept{return M16Current(p);},
        [](void*,CBasePlayer&,PlayerThinkKind) noexcept -> bool {std::abort();},runtimeView.binding.map,runtimeView.binding.life,
        PlayerThinkBit(PlayerThinkKind::Pushback)}));
    M16Weapon m16;edict_t m16Entity{};m16Entity.serialnumber=97;m16Entity.pvPrivateData=&m16;
    m16Entity.v.pContainingEntity=&m16Entity;m16.pev=&m16Entity.v;m16.m_iPrimaryAmmoType=1;m16.m_iClip=20;
    m16Cold.sequences.present=(std::uint64_t{1}<<38)-1;m16Cold.sequences.duration.fill(1);
    m16Cold.ballistics.bubbleModel=prepared.ballistics.bubbleModel;m16Cold.pushbackReady=true;
    m16Cold.modelsReady=m16Cold.soundsReady=m16Cold.aimMetadataReady=m16Cold.inventoryReady=m16Cold.pickupSoundReady=true;
    m16Cold.viewModelString=prepared.viewModelString;m16Cold.playerModelString=prepared.playerModelString;
    m16Cold.worldModelString=prepared.worldModelString;m16Cold.inventoryItemString=prepared.inventoryItemString;
    m16Cold.shellModel=31;m16Cold.hostSlot=M16HudSlot;m16Cold.hostPosition=M16HudPosition;
    m16Cold.aimOffset=prepared.aimOffset;m16Cold.aimZoom=50;
    M16Backends m16Backend{&m16Context,M16Current,M16Frame,M16Dispatch,M16RequiredEffects,&m16Animations,
        runtimeView.animationGeneration,runtimeView.binding.life,&m16Think};
    Check(m16.Bind(player,m16Cold,m16Backend)==M16Status::Ready);player.m_pActiveItem=&m16;
    globals.time=700;Check(m16.Deploy());
    auto m16Presentation=LatestState();Check(m16Presentation.originalWeapon==9&&m16Presentation.m16Burst==true&&
        !m16Presentation.revolverAngle&&m16Presentation.key.generation>mp5Presentation.key.generation);
    const auto stableM16=packets.size();for(unsigned i=0;i<1000;++i)Check(effects.SynchronizeSelection(player));
    Check(packets.size()==stableM16); // No repeated reliable traffic for unchanged mode.
    globals.time=m16.ActionState().primary+1;m16.ToggleFireMode();Check(!m16.BurstMode());
    Check(effects.SynchronizeSelection(player));auto singleM16=State();
    Check(singleM16.m16Burst==false&&singleM16.revision>m16Presentation.revision&&singleM16.key==m16Presentation.key);
    Check(!effects.Dispatch(m16Resources,runtimeView.binding,player,m16,{M16Effect::FireSound}));
    M16Event m16View{M16Effect::ViewAnimation};m16View.value=37;Check(effects.Dispatch(m16Resources,runtimeView.binding,player,m16,m16View));
    m16View.value=38;Check(!effects.Dispatch(m16Resources,runtimeView.binding,player,m16,m16View));
    M16Event m16Shot{M16Effect::ShotState};m16Shot.a=3;Check(effects.Dispatch(m16Resources,runtimeView.binding,player,m16,m16Shot));
    globals.time=m16.ActionState().primary+1;m16.ToggleFireMode();Check(m16.BurstMode()&&m16.Status()==M16Status::Applied);
    const auto writer=g_engfuncs.pfnWriteByte;g_engfuncs.pfnWriteByte=nullptr;
    const auto beforeModeRetry=packets.size();Check(!effects.SynchronizeSelection(player)&&packets.size()==beforeModeRetry);
    g_engfuncs.pfnWriteByte=writer;Check(effects.SynchronizeSelection(player));
    Check(State().m16Burst==true);const auto deliveredMode=packets.size();
    Check(effects.SynchronizeSelection(player)&&packets.size()==deliveredMode);
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));
    CrosshairSnapshot m16Spread{};Check(DecodeCrosshair(packets.back().bytes,m16Spread)&&m16Spread.key==singleM16.key);
    m16.Holster();player.m_pActiveItem=nullptr;g_engfuncs.pfnWriteByte=nullptr;
    const auto beforeReleaseRetry=packets.size();Check(!effects.SynchronizeSelection(player)&&packets.size()==beforeReleaseRetry);
    g_engfuncs.pfnWriteByte=writer;Check(effects.SynchronizeSelection(player));
    Check(State().originalWeapon==0&&!State().m16Burst);const auto releasedMode=packets.size();
    Check(effects.SynchronizeSelection(player)&&packets.size()==releasedMode);
    player.m_pActiveItem=&m16;Check(m16.Deploy());Check(LatestState().m16Burst==true);
    m16.Holster();player.m_pActiveItem=nullptr;g_engfuncs.pfnWriteByte=nullptr;Check(!effects.SynchronizeSelection(player));
    g_engfuncs.pfnWriteByte=writer;player.m_pActiveItem=&m16;Check(m16.Deploy());
    const auto supersededRelease=packets.size();Check(effects.SynchronizeSelection(player)&&packets.size()==supersededRelease);
    Check(LatestState().originalWeapon==9); // New current ownership replaces a failed old release.
    if(g43)CheckG43Effects(effects,glock);
    if(shotgun)CheckShotgunEffects(effects,glock);
    for(unsigned slot=1;slot<=32;++slot)
    {
        FirstDrawLifecycle ledger;FirstDrawConnection key{1,slot,0,int(slot),slot};bool drawn=true;
        Check(ledger.Bind(key,drawn)&&!drawn&&ledger.MarkDrawn(key));
        Check(ledger.Bind(key,drawn)&&drawn); // Ordinary death/respawn/model keeps the connection.
        auto replacement=key;++replacement.userid;
        Check(ledger.Bind(replacement,drawn)&&!drawn&&!ledger.MarkDrawn(key));
        auto invalid=replacement;invalid.map=0;drawn=true;
        Check(!ledger.Bind(invalid,drawn)&&drawn);
        ledger.Reset();Check(!ledger.MarkDrawn(replacement)&&ledger.Bind(replacement,drawn)&&!drawn);
    }
    player.m_pActiveItem=nullptr;Check(effects.SynchronizeSelection(player));
    Check(State().originalWeapon==0&&!State().revolverAngle);
    player.m_pActiveItem=&pistol;retireBegin=true;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,aim));
    Check(packets.back().bytes.size()==StateBytes);retireBegin=false;valid=true;
    effects.EndMap();Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,aim));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;player.m_pActiveItem=nullptr;runtime.EndMap();
    std::cout<<"P345/Revolver/VP70 actual shared presentation/effect dispatch: "<<checks<<" SDK checks, zero failures; engine packets are captured, no network/gameplay acceptance\n";
}
