// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_effects.hpp"
#include "native/csol/server/weapons/guest_cof_p345_owner.hpp"
#include "native/guest/cof/presentation_protocol.hpp"
#include "native/guest/cof/particle_protocol.hpp"
#include "native/guest/cof/crosshair_protocol.hpp"
#include "native/guest/cof/sound_protocol.hpp"
#include "native/guest/cof/control_protocol.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <map>

namespace dispatch_fixture
{
using namespace csol::server::cof;
bool valid=true,retireBegin{};
unsigned inventoryCalls{},tabulates{},viewAnimations{},playbacks{};
unsigned revolverShellEvents{},revolverFireEvents{};
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
        else Check(flags==1&&index==35&&i==7&&j==19);
        ++playbacks;
    };
}
PresentationState State()
{
    const auto& packet=packets.back();Check(packet.id==messages.at(StateMessage)&&packet.destination==MSG_ONE&&packet.target==player.edict());
    PresentationState value{};Check(DecodeState(packet.bytes,value));return value;
}
}
void CBasePlayer::TabulateAmmo(){Check(this==&player);++dispatch_fixture::tabulates;}
void CSOL_UpdateWeaponInventory(CBasePlayer* actor){Check(actor==&player);++dispatch_fixture::inventoryCalls;}
void CBasePlayerWeapon::SendWeaponAnim(int animation,int body)
{Check(m_pPlayer==&player&&body==0&&animation>=0&&animation<=42);++dispatch_fixture::viewAnimations;}

int main(int argc,char** argv)
{
    if(argc!=2)return 1;
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
    player.m_pActiveItem=nullptr;Check(effects.SynchronizeSelection(player));
    Check(State().originalWeapon==0&&!State().revolverAngle);
    player.m_pActiveItem=&pistol;retireBegin=true;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,aim));
    Check(packets.back().bytes.size()==StateBytes);retireBegin=false;valid=true;
    effects.EndMap();Check(!effects.Dispatch(resources,runtimeView.binding,player,pistol,aim));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;player.m_pActiveItem=nullptr;runtime.EndMap();
    std::cout<<"P345/Revolver actual shared presentation/effect dispatch: "<<checks<<" SDK checks, zero failures; engine packets are captured, no network/gameplay acceptance\n";
}
