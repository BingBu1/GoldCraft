// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_shotgun_owner.hpp"
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
void ShotgunOwnerView::Invalidate() noexcept
{
    ready_ = false; canonical_ = {}; player_ = nullptr; entity_ = nullptr;
    actions_ = {}; state_ = {}; animation_.Reset();
    if (generation_) generation_ = generation_ == std::numeric_limits<std::uint64_t>::max() ? 0 : generation_ + 1;
}
bool ShotgunOwnerView::Local(ShotgunOwnerBinding key) const noexcept
{
    return key.view == this && key.generation && key.generation == generation_ && ready_ && player_ && entity_ &&
        !entity_->free && entity_->serialnumber == serial_ && entity_->pvPrivateData == player_ &&
        player_->pev == &entity_->v && player_->pev->modelindex == modelIndex_ && player_->m_iNumSpawns == spawn_ &&
        !player_->has_disconnected && player_->pev->deadflag == DEAD_NO && player_->pev->health > 0;
}
bool ShotgunOwnerView::Current(ShotgunOwnerBinding key) const noexcept
{
    if (!Local(key)) return false;
    const auto canonical = canonical_; const auto actions = actions_; auto* player = player_;
    return canonical.runtime && canonical.runtime->Current(canonical) && Local(key) &&
        actions.PreparedFor(*player, canonical.map, canonical.life) && Local(key) && Same(canonical, canonical_);
}
bool ShotgunOwnerView::Prepare(CBasePlayer& player, const OwnerBackendView& shared, NativeOwnerRules rules,
    guest::cof::server::PlayerActionBinding actions, ShotgunOwnerState state, guest::cof::server::PistolAnimationRig rig,
    std::span<const std::byte> model, guest::cof::PlayerActionLabelCompare compare,
    ShotgunOwnerBackendView& output, std::string& error, guest::cof::PlayerModelExtent extent)
{
    if (preparing_ || !generation_ || !player.pev || !shared.binding.runtime || !compare || model.empty() ||
        rules.bulletDamage < 0 || (rules.reserveLimit != 15 && rules.reserveLimit != 50) ||
        (state.reloadUntil && !std::isfinite(*state.reloadUntil)))
    { error = "Shotgun owner requires canonical input, matching player actions, explicit rules and a validated model"; return false; }
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
    if (!matches()) { error = "Shotgun input and player actions do not belong to the same current life"; return false; }
    const auto plan = guest::cof::server::RecommendedLongGunAnimationPlan(guest::cof::server::LongGun::Shotgun, rig);
    guest::cof::server::PlayerAnimationAdapter candidate;
    if (!plan || !candidate.Prepare(player, model, {requested.map, requested.model},
        guest::cof::server::LongGun::Shotgun, *plan, compare, error, extent) || !matches()) return false;
    auto* think = actions.Think();
    if (!think || !matches()) return false;
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
    { error = "Shotgun owner generation exhausted"; return false; }
    // Commit only after all cold work succeeds. An unrelated bad request must
    // not retire a working owner. This is host recovery, not a CoF default.
    ++generation_; canonical_ = canonical.binding; rules_ = rules; actions_ = actions; state_ = state;
    player_ = &player; entity_ = player.edict(); serial_ = entity_->serialnumber;
    modelIndex_ = player.pev->modelindex; spawn_ = player.m_iNumSpawns;
    animation_ = std::move(candidate); ready_ = true;
    output = {{this, generation_}, &animation_, {canonical_.map, canonical_.model}, think};
    return true;
}
bool ShotgunOwnerView::Read(ShotgunOwnerBinding key, guest::cof::server::ShotgunOwnerFrame& output) const noexcept
{
    if (!Local(key)) return false;
    const auto canonical = canonical_; const auto actions = actions_;
    guest::cof::server::GlockOwnerFrame shared;
    float stamina{}; bool infiniteStamina{};
    if (!canonical.runtime || !canonical.runtime->Read(canonical, shared) || !Local(key)) return false;
    const bool staminaAvailable=actions.ReadStamina(stamina,infiniteStamina);
    if(!Local(key)||!actions.Current()||!Same(canonical,canonical_))return false;
    guest::cof::server::ShotgunOwnerFrame value;
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
bool ShotgunOwnerView::BackendCurrent(void* opaque) noexcept
{
    if (!opaque) return false;
    const auto key = *static_cast<const ShotgunOwnerBinding*>(opaque);
    return key.view && key.view->Current(key);
}
bool ShotgunOwnerView::BackendFrame(void* opaque, guest::cof::server::ShotgunOwnerFrame& output) noexcept
{
    if (!opaque) return false;
    const auto key = *static_cast<const ShotgunOwnerBinding*>(opaque);
    return key.view && key.view->Read(key, output);
}
bool ShotgunOwnerView::PersistentState(ShotgunOwnerBinding key, ShotgunOwnerState& output) const noexcept
{
    if (!Current(key)) return false;
    output = state_; return true;
}
bool ShotgunOwnerView::ReloadStage(ShotgunOwnerBinding key, const guest::cof::server::ShotgunEvent& event) noexcept
{
    using namespace guest::cof::server;
    if (event.kind != ShotgunEffect::ReloadStage || event.value < 0 || event.value > 4 || !gpGlobals ||
        !std::isfinite(gpGlobals->time) || !Current(key)) return false;
    const auto track = animation_.Select({canonical_.map, canonical_.model}, LongGun::Shotgun,
        PlayerAction::Reload, (player_->pev->flags & FL_DUCKING) != 0);
    if (!track) return false;
    const float now = gpGlobals->time;
    float until;
    if (event.value < 2)
    {
        until = now + 1.F;
        if (event.a != until) return false;
    }
    else
    {
        if (event.a != 0) return false;
        until = static_cast<float>(double(now) + double(track->sdkDuration));
    }
    if (!std::isfinite(until) || !Current(key)) return false;
    state_.reloadUntil = until; return true;
}
std::optional<int> ShotgunOwnerView::ReloadSequence(ShotgunOwnerBinding key, int playerAnimation) const noexcept
{
    using namespace guest::cof::server;
    if ((playerAnimation != PLAYER_IDLE && playerAnimation != PLAYER_WALK) || !gpGlobals ||
        !std::isfinite(gpGlobals->time) || !Current(key) || !state_.reloadUntil ||
        !(*state_.reloadUntil > gpGlobals->time) || (player_->pev->flags & FL_FROZEN) ||
        player_->m_flFlinchTime > gpGlobals->time || player_->m_Activity == ACT_SWIM ||
        player_->m_Activity == ACT_DIESIMPLE || player_->m_Activity == ACT_HOVER ||
        player_->m_Activity == ACT_HOP || player_->m_Activity == ACT_LEAP ||
        player_->m_Activity == ACT_FLINCH || player_->m_Activity == ACT_LARGE_FLINCH) return std::nullopt;
    const auto* item = dynamic_cast<const ShotgunWeapon*>(player_->m_pActiveItem);
    if (!item || item->m_pPlayer != player_ || item->m_iId != ShotgunHostId) return std::nullopt;
    const auto track = animation_.Select({canonical_.map, canonical_.model}, LongGun::Shotgun,
        PlayerAction::Reload, (player_->pev->flags & FL_DUCKING) != 0);
    if (!track || player_->pev->sequence != track->sequence || !Current(key)) return std::nullopt;
    return track->sequence;
}

}
