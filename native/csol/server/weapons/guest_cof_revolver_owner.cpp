// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_revolver_owner.hpp"
#include <limits>
#include <cmath>

namespace csol::server::cof
{
namespace
{
bool Same(const OwnerBinding& a,const OwnerBinding& b) noexcept
{
    return a.runtime==b.runtime&&a.generation==b.generation&&a.map==b.map&&a.life==b.life&&
        a.model==b.model&&a.slot==b.slot&&a.serial==b.serial;
}
}
void RevolverOwnerView::Invalidate() noexcept
{
    ready_=false;canonical_={};player_=nullptr;entity_=nullptr;animation_.Reset();
    if(generation_)generation_=generation_==std::numeric_limits<std::uint64_t>::max()?0:generation_+1;
}
bool RevolverOwnerView::Local(RevolverOwnerBinding key) const noexcept
{
    return key.view==this&&key.generation&&key.generation==generation_&&ready_&&player_&&entity_&&
        !entity_->free&&entity_->serialnumber==serial_&&entity_->pvPrivateData==player_&&
        player_->pev==&entity_->v&&player_->pev->modelindex==modelIndex_&&player_->m_iNumSpawns==spawn_&&
        player_->pev->deadflag==DEAD_NO&&player_->pev->health>0;
}
bool RevolverOwnerView::Current(RevolverOwnerBinding key) const noexcept
{
    if(!Local(key))return false;
    const auto shared=canonical_;
    return shared.runtime&&shared.runtime->Current(shared)&&Local(key)&&Same(shared,canonical_);
}
bool RevolverOwnerView::Prepare(CBasePlayer& player,const OwnerBackendView& shared,NativeOwnerRules rules,RevolverOwnerState seed,
    guest::cof::server::PistolAnimationRig rig,std::span<const std::byte> model,
    guest::cof::PlayerActionLabelCompare compare,RevolverOwnerBackendView& output,std::string& error,
    guest::cof::PlayerModelExtent extent)
{
    Invalidate();const RevolverOwnerBinding key{this,generation_};
    if(!generation_||!player.pev||!shared.binding.runtime||!compare||model.empty()||
       rules.bulletDamage<0||rules.meleeDamage<0||(rules.reserveLimit!=15&&rules.reserveLimit!=50)||
       !std::isfinite(seed.cylinderAngle)||seed.cylinderAngle<0||seed.cylinderAngle>360||!std::isfinite(seed.cylinderGate))
    {error="Revolver owner requires a canonical native life, explicit weapon rules and validated model";return false;}
    OwnerBackendView verified{};
    if(!shared.binding.runtime->Backend(shared.binding.slot,player,verified)||!Same(shared.binding,verified.binding)||
       key.generation!=generation_)
    {error="Revolver canonical owner was replaced before preparation";return false;}
    player_=&player;entity_=player.edict();serial_=entity_->serialnumber;modelIndex_=player.pev->modelindex;
    spawn_=player.m_iNumSpawns;canonical_=verified.binding;rules_=rules;state_=seed;ready_=true;
    const auto plan=guest::cof::server::RecommendedPistolAnimationPlan(guest::cof::Pistol::Revolver,rig);
    if(!plan||!Current(key)||!animation_.Prepare(player,model,{canonical_.map,canonical_.model},
        guest::cof::Pistol::Revolver,*plan,compare,error,extent)||!Current(key))
    {if(key.generation==generation_)Invalidate();return false;}
    output={key,&animation_,{canonical_.map,canonical_.model}};
    return true;
}
bool RevolverOwnerView::Read(RevolverOwnerBinding key,guest::cof::server::RevolverOwnerFrame& output) const noexcept
{
    if(!Current(key))return false;
    guest::cof::server::GlockOwnerFrame shared{};
    if(!canonical_.runtime->Read(canonical_,shared)||!Current(key))return false;
    guest::cof::server::RevolverOwnerFrame value{};
    value.activityBlock=shared.activityBlock;value.pendingGateTime=shared.pendingGateTime;
    value.condition3AA=shared.condition3AA;value.pendingGate=shared.pendingGate;
    value.toggleAim=rules_.toggleAim;value.infiniteAmmo=shared.infiniteAmmo;value.quietAudio=shared.quietAudio;
    value.firstDrawn=state_.firstDrawn;value.cylinderAngle=state_.cylinderAngle;value.cylinderGate=state_.cylinderGate;
    value.originalFlags=shared.originalFlags;
    value.pressed=shared.pressed;value.released=shared.released;value.randomSeed=shared.randomSeed;
    value.bulletDamage=rules_.bulletDamage;value.meleeDamage=rules_.meleeDamage;value.reserveLimit=rules_.reserveLimit;
    value.sourceMode=shared.sourceMode;output=value;return true;
}
bool RevolverOwnerView::PersistentState(RevolverOwnerBinding key,RevolverOwnerState& value) const noexcept
{
    if(!Current(key))return false;
    value=state_;return true;
}
bool RevolverOwnerView::FirstDraw(RevolverOwnerBinding key,bool value) noexcept
{
    if(!Current(key))return false;
    state_.firstDrawn=value;return true;
}
bool RevolverOwnerView::Cylinder(RevolverOwnerBinding key,const guest::cof::server::RevolverEvent& event) noexcept
{
    using guest::cof::server::RevolverEffect;
    if(event.kind!=RevolverEffect::Cylinder||!std::isfinite(event.a)||event.a<0||event.a>360||
       (event.enabled&&!std::isfinite(event.b))||!Current(key))return false;
    state_.cylinderAngle=event.a;
    if(event.enabled)state_.cylinderGate=event.b;
    return true;
}
bool RevolverOwnerView::BackendCurrent(void* opaque) noexcept
{
    if(!opaque)return false;
    const auto key=*static_cast<const RevolverOwnerBinding*>(opaque);
    return key.view&&key.view->Current(key);
}
bool RevolverOwnerView::BackendFrame(void* opaque,guest::cof::server::RevolverOwnerFrame& output) noexcept
{
    if(!opaque)return false;
    const auto key=*static_cast<const RevolverOwnerBinding*>(opaque);
    return key.view&&key.view->Read(key,output);
}
}
