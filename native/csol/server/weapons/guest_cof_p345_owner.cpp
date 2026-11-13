// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_p345_owner.hpp"
#include <limits>

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
void P345OwnerView::Invalidate() noexcept
{
    ready_=false;canonical_={};player_=nullptr;entity_=nullptr;animation_.Reset();
    if(generation_)generation_=generation_==std::numeric_limits<std::uint64_t>::max()?0:generation_+1;
}
bool P345OwnerView::Local(P345OwnerBinding key) const noexcept
{
    return key.view==this&&key.generation&&key.generation==generation_&&ready_&&player_&&entity_&&
        !entity_->free&&entity_->serialnumber==serial_&&entity_->pvPrivateData==player_&&
        player_->pev==&entity_->v&&player_->pev->modelindex==modelIndex_&&player_->m_iNumSpawns==spawn_&&
        player_->pev->deadflag==DEAD_NO&&player_->pev->health>0;
}
bool P345OwnerView::Current(P345OwnerBinding key) const noexcept
{
    if(!Local(key))return false;
    const auto shared=canonical_;
    return shared.runtime&&shared.runtime->Current(shared)&&Local(key)&&Same(shared,canonical_);
}
bool P345OwnerView::Prepare(CBasePlayer& player,const OwnerBackendView& shared,NativeOwnerRules rules,
    guest::cof::server::PistolAnimationRig rig,std::span<const std::byte> model,
    guest::cof::PlayerActionLabelCompare compare,P345OwnerBackendView& output,std::string& error,
    guest::cof::PlayerModelExtent extent)
{
    Invalidate();const P345OwnerBinding key{this,generation_};
    if(!generation_||!player.pev||!shared.binding.runtime||!compare||model.empty()||
       rules.bulletDamage<0||rules.meleeDamage<0||(rules.reserveLimit!=24&&rules.reserveLimit!=80))
    {error="P345 owner requires a canonical native life, explicit weapon rules and validated model";return false;}
    OwnerBackendView verified{};
    if(!shared.binding.runtime->Backend(shared.binding.slot,player,verified)||!Same(shared.binding,verified.binding)||
       key.generation!=generation_)
    {error="P345 canonical owner was replaced before preparation";return false;}
    player_=&player;entity_=player.edict();serial_=entity_->serialnumber;modelIndex_=player.pev->modelindex;
    spawn_=player.m_iNumSpawns;canonical_=verified.binding;rules_=rules;ready_=true;
    const auto plan=guest::cof::server::RecommendedPistolAnimationPlan(guest::cof::Pistol::P345,rig);
    if(!plan||!Current(key)||!animation_.Prepare(player,model,{canonical_.map,canonical_.model},
        guest::cof::Pistol::P345,*plan,compare,error,extent)||!Current(key))
    {if(key.generation==generation_)Invalidate();return false;}
    output={key,&animation_,{canonical_.map,canonical_.model}};
    return true;
}
bool P345OwnerView::Read(P345OwnerBinding key,guest::cof::server::P345OwnerFrame& output) const noexcept
{
    if(!Current(key))return false;
    guest::cof::server::GlockOwnerFrame shared{};
    if(!canonical_.runtime->Read(canonical_,shared)||!Current(key))return false;
    guest::cof::server::P345OwnerFrame value{};
    value.activityBlock=shared.activityBlock;value.pendingGateTime=shared.pendingGateTime;
    value.condition3AA=shared.condition3AA;value.pendingGate=shared.pendingGate;
    value.toggleAim=rules_.toggleAim;value.infiniteAmmo=shared.infiniteAmmo;value.quietAudio=shared.quietAudio;
    value.eventOwner2814=shared.eventOwner2814;value.originalFlags=shared.originalFlags;
    value.pressed=shared.pressed;value.released=shared.released;value.randomSeed=shared.randomSeed;
    value.bulletDamage=rules_.bulletDamage;value.meleeDamage=rules_.meleeDamage;value.reserveLimit=rules_.reserveLimit;
    value.sourceMode=shared.sourceMode;output=value;return true;
}
bool P345OwnerView::BackendCurrent(void* opaque) noexcept
{
    if(!opaque)return false;
    const auto key=*static_cast<const P345OwnerBinding*>(opaque);
    return key.view&&key.view->Current(key);
}
bool P345OwnerView::BackendFrame(void* opaque,guest::cof::server::P345OwnerFrame& output) noexcept
{
    if(!opaque)return false;
    const auto key=*static_cast<const P345OwnerBinding*>(opaque);
    return key.view&&key.view->Read(key,output);
}
}
