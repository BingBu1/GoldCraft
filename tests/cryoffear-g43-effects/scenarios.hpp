// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Included after the actual shared SDK fixture. Native sound/latch and target
// ballistics are controlled boundaries; presentation/control/particle senders
// and the actual G43 class below execute production code.
namespace dispatch_fixture
{
struct G43Context
{
    GlockEffects& effects;const G43Resources& resources;OwnerBinding binding;G43OwnerFrame frame;
};
bool G43Current(void* p) noexcept
{return GlockOwnerRuntime::BackendCurrent(&static_cast<G43Context*>(p)->binding)&&valid;}
bool G43Frame(void* p,G43OwnerFrame& frame) noexcept
{if(!G43Current(p))return false;frame=static_cast<G43Context*>(p)->frame;return true;}
bool G43Dispatch(void* p,CBasePlayer& actor,CBasePlayerWeapon& item,const G43Event& event) noexcept
{
    auto& value=*static_cast<G43Context*>(p);
    constexpr auto external=(1u<<unsigned(G43Effect::EmptyClick))|(1u<<unsigned(G43Effect::Sleeve))|
        (1u<<unsigned(G43Effect::PickupSound))|(1u<<unsigned(G43Effect::AmmoPickupSound))|
        (1u<<unsigned(G43Effect::FireSound))|(1u<<unsigned(G43Effect::ShotLatch));
    if((1u<<unsigned(event.kind))&external)return G43Current(p);
    return value.effects.Dispatch(value.resources,value.binding,actor,item,event);
}
void CheckG43Effects(GlockEffects& effects,const GlockResources& glock)
{
    const auto before=checks;
    G43Resources resources{};resources.generation=glock.generation;resources.events=glock.events;
    resources.viewModelString=prepared.viewModelString;resources.models[unsigned(G43Model::Shell)]=31;
    G43Prepared cold;Check(effects.FillPrepared(resources,cold));
    Check(!cold.pushbackReady&&cold.world==MeleeWorld::Multiplayer&&cold.particleReady&&cold.deployPreludeReady);
    Check((effects.G43Capabilities()&(1u<<unsigned(G43Effect::CameraZoom)))&&
        !(effects.G43Capabilities()&(1u<<unsigned(G43Effect::FireSound))));
    G43Context context{effects,resources,runtimeView.binding,{}};
    context.frame.reserveLimit=30;context.frame.stamina=100;context.frame.toggleAim=true;
    context.frame.sourceMode=PistolSource::NormalMuzzle;context.frame.bulletDamage=85;
    PlayerAnimationAdapter animations;std::string error;
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::Rifle,PistolAnimationRig::Stock);
    Check(plan&&animations.Prepare(player,model,runtimeView.animationGeneration,LongGun::Rifle,*plan,_stricmp,error));
    PlayerThinkSlot think;
    Check(think.Bind(player,{&context,[](void* p,CBasePlayer&,std::uint64_t,std::uint64_t) noexcept{return G43Current(p);},
        [](void*,CBasePlayer&,PlayerThinkKind) noexcept -> bool {std::abort();},runtimeView.binding.map,runtimeView.binding.life,
        PlayerThinkBit(PlayerThinkKind::Pushback)}));
    G43Weapon gun;edict_t entity{};entity.serialnumber=103;entity.pvPrivateData=&gun;
    entity.v.pContainingEntity=&entity;gun.pev=&entity.v;gun.m_iPrimaryAmmoType=1;gun.m_iClip=10;
    cold.sequences.present=(std::uint64_t{1}<<38)-1;cold.sequences.duration.fill(1);
    cold.ballistics.bubbleModel=prepared.ballistics.bubbleModel;cold.pushbackReady=true;
    cold.modelsReady=cold.soundsReady=cold.aimMetadataReady=cold.inventoryReady=cold.pickupSoundReady=true;
    cold.viewModelString=prepared.viewModelString;cold.playerModelString=prepared.playerModelString;
    cold.worldModelString=prepared.worldModelString;cold.inventoryItemString=prepared.inventoryItemString;
    cold.shellModel=31;cold.hostSlot=G43HudSlot;cold.hostPosition=G43HudPosition;
    cold.aimOffset=prepared.aimOffset;cold.aimZoom=50;
    G43Backends backend{&context,G43Current,G43Frame,G43Dispatch,G43RequiredEffects,&animations,
        runtimeView.animationGeneration,runtimeView.binding.life,&think};
    Check(gun.Bind(player,cold,backend)==G43Status::Ready);player.m_pActiveItem=&gun;
    globals.time=800;player.pev->button=0;Check(gun.Deploy());
    auto initial=LatestState();Check(initial.originalWeapon==26&&!initial.aimEnabled&&!initial.m16Burst&&
        !initial.revolverAngle&&initial.zoomSpeedEighths==0);
    const auto stable=packets.size();for(unsigned i=0;i<1000;++i)Check(effects.SynchronizeSelection(player));
    Check(packets.size()==stable);
    globals.time=gun.ActionState().primary+1;player.pev->button=0x40;context.frame.pressed=0x40;
    const auto beforeAim=packets.size();gun.ItemPostFrame();Check(gun.Status()==G43Status::Applied&&gun.ActionState().aiming);
    std::array<PresentationState,2> aimStates{};unsigned aimCount{};
    for(std::size_t i=beforeAim;i<packets.size();++i)if(packets[i].id==messages.at(StateMessage))
    {Check(aimCount<aimStates.size()&&DecodeState(packets[i].bytes,aimStates[aimCount]));++aimCount;}
    Check(aimCount==2&&aimStates[0].aimEnabled&&aimStates[0].fovMode==50&&aimStates[0].zoomSpeedEighths==0);
    Check(aimStates[1].aimEnabled&&aimStates[1].fovMode==50&&aimStates[1].zoomSpeedEighths==2800&&
        aimStates[1].key==aimStates[0].key&&aimStates[1].revision>aimStates[0].revision);
    const auto random=g_engfuncs.pfnRandomLong;g_engfuncs.pfnRandomLong=[](int low,int high){Check(low<=high);return low;};
    globals.time=gun.ActionState().primary+1;player.pev->button=IN_ATTACK;context.frame.pressed=1;
    const auto beforeFire=packets.size();gun.PrimaryAttack();
    Check(gun.Status()==G43Status::Applied&&gun.m_iClip==9);
    unsigned smoke{},shotState{};
    for(std::size_t i=beforeFire;i<packets.size();++i)
    {
        if(packets[i].id==messages.at(ParticleMessage))
        {
            ParticleMessageData value;Check(DecodeParticle(packets[i].bytes,value)&&value.originalWeapon==26&&value.attachment==1&&
                value.owner==19&&packets[i].bytes[0]==std::byte{2}&&packets[i].destination==MSG_ONE);++smoke;
        }
        if(packets[i].id==messages.at(ControlMessage))
        {
            ControlData value;Check(DecodeControl(packets[i].bytes,value));
            if(value.operation==ControlOperation::ShotState){Check(value.body==6);++shotState;}
        }
    }
    Check(smoke==1&&shotState==1);
    Check(effects.CrosshairSnapshot(player,{nullptr,PacketCurrent}));CrosshairSnapshot spread;
    Check(DecodeCrosshair(packets.back().bytes,spread)&&spread.key==aimStates[1].key);
    G43Event view{G43Effect::ViewAnimation};view.value=37;Check(effects.Dispatch(resources,runtimeView.binding,player,gun,view));
    view.value=38;Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,view));
    G43Event state{G43Effect::ShotState};state.a=3;Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,state));
    G43Event wrongSmoke{G43Effect::Gunsmoke};wrongSmoke.enabled=true;wrongSmoke.value=19;wrongSmoke.modelString=resources.viewModelString;
    const auto noPacket=packets.size();Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,wrongSmoke)&&packets.size()==noPacket);
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,{G43Effect::FireSound}));
    G43Event camera{G43Effect::CameraZoom};camera.a=350;camera.value=1;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera));camera.value=0;camera.enabled=true;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera));camera.enabled=false;
    const auto writer=g_engfuncs.pfnWriteByte;g_engfuncs.pfnWriteByte=nullptr;
    Check(!effects.Dispatch(resources,runtimeView.binding,player,gun,camera)&&G43Current(&context));
    g_engfuncs.pfnWriteByte=writer;Check(effects.Dispatch(resources,runtimeView.binding,player,gun,camera));
    Check(State().aimEnabled&&State().zoomSpeedEighths==2800);
    gun.Holster();Check(!gun.ActionState().aiming&&!LatestState().aimEnabled&&LatestState().zoomSpeedEighths==2800);
    auto mismatch=resources;++mismatch.generation;
    Check(!effects.Dispatch(mismatch,runtimeView.binding,player,gun,camera));
    player.m_pActiveItem=nullptr;Check(effects.SynchronizeSelection(player)&&State().originalWeapon==0);
    think.Invalidate();g_engfuncs.pfnRandomLong=random;
    std::cout<<"G43 actual presentation/effect consumers: "<<checks-before<<" SDK checks; sound/latch/target endpoints controlled\n";
}
}
