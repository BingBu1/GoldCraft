// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The actual shotgun and native packet senders run here. Native sounds,
// reload owner and target damage are controlled and validated separately.
namespace dispatch_fixture
{
struct ShotgunContext
{
    GlockEffects& effects;const ShotgunResources& resources;OwnerBinding binding;ShotgunOwnerFrame frame;
};
bool ShotgunCurrent(void* p) noexcept
{return GlockOwnerRuntime::BackendCurrent(&static_cast<ShotgunContext*>(p)->binding)&&valid;}
bool ShotgunFrame(void* p,ShotgunOwnerFrame& frame) noexcept
{if(!ShotgunCurrent(p))return false;frame=static_cast<ShotgunContext*>(p)->frame;return true;}
bool ShotgunDispatch(void* p,CBasePlayer& actor,CBasePlayerWeapon& item,const ShotgunEvent& event) noexcept
{
    auto& value=*static_cast<ShotgunContext*>(p);
    constexpr auto controlled=(1u<<unsigned(ShotgunEffect::EmptyClick))|(1u<<unsigned(ShotgunEffect::Sleeve))|
        (1u<<unsigned(ShotgunEffect::PickupSound))|(1u<<unsigned(ShotgunEffect::AmmoPickupSound))|
        (1u<<unsigned(ShotgunEffect::FireSound))|(1u<<unsigned(ShotgunEffect::ReloadStage))|
        (1u<<unsigned(ShotgunEffect::AmmoWarning));
    if((1u<<unsigned(event.kind))&controlled)return ShotgunCurrent(p);
    return value.effects.Dispatch(value.resources,value.binding,actor,item,event);
}
void CheckShotgunEffects(GlockEffects& effects,const GlockResources& glock)
{
    const auto before=checks;
    ShotgunResources resources{};resources.generation=glock.generation;resources.events=glock.events;
    resources.viewModelString=prepared.viewModelString;resources.models[unsigned(ShotgunModel::Shell)]=31;
    ShotgunPrepared cold;Check(effects.FillPrepared(resources,cold));
    Check(!cold.pushbackReady&&cold.world==MeleeWorld::Multiplayer&&cold.particleReady&&cold.deployPreludeReady);
    const auto capabilities=effects.ShotgunCapabilities();
    Check(capabilities&(1u<<unsigned(ShotgunEffect::CameraZoom)));
    Check(!(capabilities&(1u<<unsigned(ShotgunEffect::FireSound))));
    Check(!(capabilities&(1u<<unsigned(ShotgunEffect::ReloadStage))));
    Check(!(capabilities&(1u<<unsigned(ShotgunEffect::AmmoWarning))));
    ShotgunContext context{effects,resources,runtimeView.binding,{}};
    context.frame.reserveLimit=15;context.frame.stamina=100;context.frame.toggleAim=true;
    context.frame.sourceMode=PistolSource::NormalMuzzle;context.frame.bulletDamage=31;
    PlayerAnimationAdapter animations;std::string error;
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::Shotgun,PistolAnimationRig::Stock);
    Check(plan&&animations.Prepare(player,model,runtimeView.animationGeneration,LongGun::Shotgun,*plan,_stricmp,error));
    PlayerThinkSlot think;
    Check(think.Bind(player,{&context,[](void* p,CBasePlayer&,std::uint64_t,std::uint64_t) noexcept{return ShotgunCurrent(p);},
        [](void*,CBasePlayer&,PlayerThinkKind) noexcept -> bool {std::abort();},runtimeView.binding.map,runtimeView.binding.life,
        PlayerThinkBit(PlayerThinkKind::Pushback)}));
    ShotgunWeapon gun;edict_t entity{};entity.serialnumber=104;entity.pvPrivateData=&gun;
    entity.v.pContainingEntity=&entity;gun.pev=&entity.v;gun.m_iPrimaryAmmoType=1;gun.m_iClip=5;
    cold.sequences.present=(std::uint64_t{1}<<20)-1;cold.sequences.duration.fill(1);
    cold.ballistics.bubbleModel=prepared.ballistics.bubbleModel;cold.pushbackReady=true;
    cold.modelsReady=cold.soundsReady=cold.aimMetadataReady=cold.inventoryReady=cold.pickupSoundReady=true;
    cold.viewModelString=prepared.viewModelString;cold.playerModelString=prepared.playerModelString;
    cold.worldModelString=prepared.worldModelString;cold.inventoryItemString=prepared.inventoryItemString;
    cold.shellModel=31;cold.hostSlot=ShotgunHudSlot;cold.hostPosition=ShotgunHudPosition;
    cold.aimOffset=prepared.aimOffset;cold.aimZoom=50;
    ShotgunBackends backend{&context,ShotgunCurrent,ShotgunFrame,ShotgunDispatch,ShotgunRequiredEffects,&animations,
        runtimeView.animationGeneration,runtimeView.binding.life,&think};
    Check(gun.Bind(player,cold,backend)==ShotgunStatus::Ready);player.m_pActiveItem=&gun;
    globals.time=900;player.pev->button=0;Check(gun.Deploy());
    auto initial=LatestState();Check(initial.originalWeapon==4&&!initial.aimEnabled&&!initial.m16Burst&&
        !initial.revolverAngle&&initial.zoomSpeedEighths==0);
    const auto stable=packets.size();for(unsigned i=0;i<1000;++i)Check(effects.SynchronizeSelection(player));
    Check(packets.size()==stable);
    globals.time=gun.ActionState().primary+1;player.pev->button=0x40;context.frame.pressed=0x40;
    const auto beforeAim=packets.size();gun.ItemPostFrame();
    Check(gun.Status()==ShotgunStatus::Applied&&gun.ActionState().aiming);
    std::array<PresentationState,2> aimStates{};unsigned aimCount{};
    for(std::size_t i=beforeAim;i<packets.size();++i)if(packets[i].id==messages.at(StateMessage))
    {Check(aimCount<aimStates.size()&&DecodeState(packets[i].bytes,aimStates[aimCount]));++aimCount;}
    Check(aimCount==2&&aimStates[0].aimEnabled&&aimStates[0].fovMode==50&&aimStates[0].zoomSpeedEighths==0);
    Check(aimStates[1].aimEnabled&&aimStates[1].zoomSpeedEighths==2800&&
        aimStates[1].key==aimStates[0].key&&aimStates[1].revision>aimStates[0].revision);
    const auto random=g_engfuncs.pfnRandomLong;g_engfuncs.pfnRandomLong=[](int low,int high){Check(low<=high);return low;};
    globals.time=gun.ActionState().primary+1;player.pev->button=IN_ATTACK;context.frame.pressed=IN_ATTACK;
    const auto beforeFire=packets.size(),beforeBullets=shotgunBallisticsCalls;gun.PrimaryAttack();Check(gun.Status()==ShotgunStatus::Applied&&gun.m_iClip==4);
    unsigned smoke{},shotState{};
    for(std::size_t i=beforeFire;i<packets.size();++i)
    {
        if(packets[i].id==messages.at(ParticleMessage))
        {
            ParticleMessageData value;Check(DecodeParticle(packets[i].bytes,value)&&value.originalWeapon==4&&value.attachment==1&&
                value.owner==19&&packets[i].bytes[0]==std::byte{2}&&packets[i].destination==MSG_ONE);++smoke;
        }
        if(packets[i].id==messages.at(ControlMessage))
        {
            ControlData value;Check(DecodeControl(packets[i].bytes,value));
            if(value.operation==ControlOperation::ShotState){Check(value.body==5);++shotState;}
        }
    }
    Check(smoke==1&&shotState==1&&shotgunBallisticsCalls==beforeBullets+1);
    ShotgunEvent shell{ShotgunEffect::Shell};shell.first={1,2,3};shell.second={4,5,6};shell.a=90;shell.value=31;shell.other=2;
    Check(effects.Dispatch(resources,runtimeView.binding,player,gun,shell));
    const auto& packet=packets.back();Check(packet.destination==MSG_PVS&&packet.id==SVC_TEMPENTITY&&!packet.target);
    Check(packet.bytes.size()==18&&packet.bytes[0]==std::byte{TE_MODEL}&&packet.bytes[16]==std::byte{2}&&packet.bytes[17]==std::byte{25});
    shell.other=1;const auto beforeWrongShell=packets.size();
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,shell)&&packets.size()==beforeWrongShell);
    ShotgunEvent view{ShotgunEffect::ViewAnimation};view.value=19;Check(effects.Dispatch(resources,runtimeView.binding,player,gun,view));
    view.value=20;Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,view));
    ShotgunEvent state{ShotgunEffect::ShotState};state.a=3;Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,state));
    state.a=5;Check(effects.Dispatch(resources,runtimeView.binding,player,gun,state));
    ShotgunEvent wrongSmoke{ShotgunEffect::Gunsmoke};wrongSmoke.enabled=true;wrongSmoke.value=19;wrongSmoke.modelString=resources.viewModelString;
    const auto noPacket=packets.size();Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,wrongSmoke)&&packets.size()==noPacket);
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,{ShotgunEffect::FireSound}));
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,{ShotgunEffect::ReloadStage}));
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,{ShotgunEffect::AmmoWarning}));
    ShotgunEvent camera{ShotgunEffect::CameraZoom};camera.a=350;camera.value=1;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera));camera.value=0;camera.enabled=true;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera));camera.enabled=false;
    const auto writer=g_engfuncs.pfnWriteByte;g_engfuncs.pfnWriteByte=nullptr;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera)&&ShotgunCurrent(&context));
    g_engfuncs.pfnWriteByte=writer;Check(effects.Dispatch(resources,runtimeView.binding,player,gun,camera));
    Check(State().aimEnabled&&State().zoomSpeedEighths==2800);
    gun.Holster();Check(gun.ActionState().aiming&&LatestState().aimEnabled&&LatestState().zoomSpeedEighths==2800);
    // Original ordinary Holster keeps ADS; releasing selection is a separate host stage.
    auto mismatch=resources;++mismatch.generation;Check(!effects.Dispatch(mismatch,runtimeView.binding,player,gun,camera));
    player.m_pActiveItem=nullptr;Check(effects.SynchronizeSelection(player)&&State().originalWeapon==0);
    think.Invalidate();g_engfuncs.pfnRandomLong=random;
    std::cout<<"870 actual presentation/effect consumers: "<<checks-before<<" SDK checks; sound/reload owner/target endpoints controlled\n";
}
}
