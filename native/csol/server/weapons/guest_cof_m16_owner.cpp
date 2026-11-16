// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_m16_owner.hpp"
#include <limits>
#include <utility>

namespace csol::server::cof
{
namespace
{
bool Same(const OwnerBinding& a, const OwnerBinding& b) noexcept
{
    return a.runtime == b.runtime && a.generation == b.generation && a.map == b.map && a.life == b.life &&
        a.model == b.model && a.slot == b.slot && a.serial == b.serial;
}
}
void M16OwnerView::Invalidate() noexcept
{
    ready_ = false; canonical_ = {}; player_ = nullptr; entity_ = nullptr;
    actions_ = {}; animation_.Reset();
    if (generation_) generation_ = generation_ == std::numeric_limits<std::uint64_t>::max() ? 0 : generation_ + 1;
}
bool M16OwnerView::Local(M16OwnerBinding key) const noexcept
{
    return key.view == this && key.generation && key.generation == generation_ && ready_ && player_ && entity_ &&
        !entity_->free && entity_->serialnumber == serial_ && entity_->pvPrivateData == player_ &&
        player_->pev == &entity_->v && player_->pev->modelindex == modelIndex_ && player_->m_iNumSpawns == spawn_ &&
        !player_->has_disconnected && player_->pev->deadflag == DEAD_NO && player_->pev->health > 0;
}
bool M16OwnerView::Current(M16OwnerBinding key) const noexcept
{
    if (!Local(key)) return false;
    const auto canonical = canonical_; const auto actions = actions_; auto* player = player_;
    return canonical.runtime && canonical.runtime->Current(canonical) && Local(key) &&
        actions.PreparedFor(*player, canonical.map, canonical.life) && Local(key) && Same(canonical, canonical_);
}
bool M16OwnerView::Prepare(CBasePlayer& player, const OwnerBackendView& shared, NativeOwnerRules rules,
    guest::cof::server::PlayerActionBinding actions, guest::cof::server::PistolAnimationRig rig,
    std::span<const std::byte> model, guest::cof::PlayerActionLabelCompare compare,
    M16OwnerBackendView& output, std::string& error, guest::cof::PlayerModelExtent extent)
{
    if (preparing_ || !generation_ || !player.pev || !shared.binding.runtime || !compare || model.empty() ||
        rules.bulletDamage < 0 || (rules.reserveLimit != 60 && rules.reserveLimit != 200))
    { error = "M16 owner requires canonical input, matching player actions, explicit rules and a validated model"; return false; }
    preparing_ = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{preparing_};
    const auto generation = generation_; const auto requested = shared.binding;
    OwnerBackendView canonical{};
    const auto matches = [&]
    {
        return generation_ == generation && requested.runtime->Backend(requested.slot, player, canonical) &&
            Same(requested, canonical.binding) &&
            actions.PreparedFor(player, requested.map, requested.life) && generation_ == generation;
    };
    if (!matches()) { error = "M16 input and player actions do not belong to the same current life"; return false; }
    const auto plan = guest::cof::server::RecommendedLongGunAnimationPlan(guest::cof::server::LongGun::M16, rig);
    guest::cof::server::PlayerAnimationAdapter candidate;
    if (!plan || !candidate.Prepare(player, model, {requested.map, requested.model},
        guest::cof::server::LongGun::M16, *plan, compare, error, extent) || !matches()) return false;
    auto* think = actions.Think();
    if (!think || !matches()) return false;
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
    { error = "M16 owner generation exhausted"; return false; }
    // Commit only after all cold work succeeds. An unrelated bad request must
    // not retire a working owner. This is host recovery, not a CoF default.
    ++generation_; canonical_ = canonical.binding; rules_ = rules; actions_ = actions;
    player_ = &player; entity_ = player.edict(); serial_ = entity_->serialnumber;
    modelIndex_ = player.pev->modelindex; spawn_ = player.m_iNumSpawns;
    animation_ = std::move(candidate); ready_ = true;
    output = {{this, generation_}, &animation_, {canonical_.map, canonical_.model}, think};
    return true;
}
bool M16OwnerView::Read(M16OwnerBinding key, guest::cof::server::M16OwnerFrame& output) const noexcept
{
    if (!Local(key)) return false;
    const auto canonical = canonical_; const auto actions = actions_;
    guest::cof::server::GlockOwnerFrame shared;
    float stamina{}; bool infiniteStamina{};
    if (!canonical.runtime || !canonical.runtime->Read(canonical, shared) || !Local(key)) return false;
    const bool staminaAvailable=actions.ReadStamina(stamina,infiniteStamina);
    if(!Local(key)||!actions.Current()||!Same(canonical,canonical_))return false;
    guest::cof::server::M16OwnerFrame value;
    value.activityBlock = shared.activityBlock; value.pendingGateTime = shared.pendingGateTime;
    value.condition3AA = shared.condition3AA; value.pendingGate = shared.pendingGate;
    value.toggleAim = rules_.toggleAim; value.infiniteAmmo = shared.infiniteAmmo; value.quietAudio = shared.quietAudio;
    value.eventOwner2814 = shared.eventOwner2814;
    if(staminaAvailable)value.stamina=stamina;
    value.originalFlags = shared.originalFlags; value.pressed = shared.pressed;
    value.released = shared.released; value.randomSeed = shared.randomSeed;
    value.bulletDamage = rules_.bulletDamage; value.reserveLimit = rules_.reserveLimit;
    value.sourceMode = shared.sourceMode; output = value; return true;
}
bool M16OwnerView::BackendCurrent(void* opaque) noexcept
{
    if (!opaque) return false;
    const auto key = *static_cast<const M16OwnerBinding*>(opaque);
    return key.view && key.view->Current(key);
}
bool M16OwnerView::BackendFrame(void* opaque, guest::cof::server::M16OwnerFrame& output) noexcept
{
    if (!opaque) return false;
    const auto key = *static_cast<const M16OwnerBinding*>(opaque);
    return key.view && key.view->Read(key, output);
}
}
