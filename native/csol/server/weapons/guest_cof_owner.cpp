// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_owner.hpp"

#include <cmath>

namespace csol::server::cof
{
namespace
{
constexpr std::uint32_t OriginalGround = 0x200u;
constexpr std::uint32_t OriginalDuck = 0x4000u;
static_assert(FL_ONGROUND == OriginalGround && FL_DUCKING == OriginalDuck);
static_assert(FL_IMMUNE_SLIME == 0x40000u);
}

bool GlockOwner::ModeSupported() const noexcept
{
    return mode_.movement == OwnerMovement::NativeCounterStrike &&
        mode_.freeActivity && mode_.phoneUnavailable && mode_.substanceFree &&
        mode_.finiteAmmo && mode_.firstPerson;
}

bool GlockOwner::LocalCurrent() const noexcept
{
    return player_ && entity_ && !entity_->free &&
        entity_->serialnumber == serial_ && entity_->pvPrivateData == player_ &&
        player_->pev == &entity_->v && player_->pev->deadflag == DEAD_NO &&
        player_->pev->health > 0 && ModeSupported();
}

bool GlockOwner::Current() const noexcept
{
    if (!LocalCurrent() || !authority_.current) return false;
    // Copy the lease because the authority may synchronously revoke it.
    auto* player = player_;
    auto* entity = entity_;
    const auto serial = serial_;
    const auto binding = binding_;
    const auto authority = authority_;
    return authority.current(authority.context, *player, authority.map, authority.life) &&
        LocalCurrent() && binding_ == binding && player_ == player && entity_ == entity && serial_ == serial &&
        authority_.context == authority.context && authority_.current == authority.current &&
        authority_.map == authority.map && authority_.life == authority.life;
}

bool GlockOwner::Bind(CBasePlayer& player, NativeOwnerAuthority authority,
    NativeOwnerMode mode, NativeOwnerRules rules, NativeOwnerPersistentState persistent) noexcept
{
    Invalidate();
    if (!player.pev || !authority.current || !authority.map || !authority.life ||
        rules.bulletDamage < 0 || rules.meleeDamage < 0 ||
        (rules.reserveLimit != 45 && rules.reserveLimit != 150)) return false;
    player_ = &player;
    entity_ = player.edict();
    if (!entity_) { Invalidate(); return false; }
    serial_ = entity_->serialnumber;
    authority_ = authority;
    mode_ = mode;
    rules_ = rules;
    persistent_ = persistent;
    if (!Current()) { Invalidate(); return false; }
    // Native-life isolation adaptation. This is not a claim that original
    // CoF Spawn resets3AA/3AC. Persistent draw/light state is explicitly supplied.
    jump_ = {};
    lastButtons_ = static_cast<std::uint32_t>(player.m_afButtonLast);
    edges_ = {static_cast<std::uint32_t>(player.m_afButtonPressed),
        static_cast<std::uint32_t>(player.m_afButtonReleased)};
    return true;
}

void GlockOwner::Invalidate() noexcept
{
    ++binding_;
    player_ = nullptr;
    entity_ = nullptr;
    serial_ = 0;
    authority_ = {};
    phase_ = Phase::Outside;
}

std::uint32_t GlockOwner::OriginalFlags() const noexcept
{
    // Explicit NativeCounterStrike mode excludes CoF sprint. Native slime
    // immunity is not CoF sprint; only verified same-meaning flags cross here.
    return static_cast<std::uint32_t>(player_->pev->flags) & (OriginalGround | OriginalDuck);
}

bool GlockOwner::BeginPreThink() noexcept
{
    if (!Current() || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        phase_ != Phase::Outside) return false;
    phase_ = Phase::Spread;
    return true;
}

bool GlockOwner::BeginInput() noexcept
{
    if (!Current() || phase_ != Phase::Spread) return false;
    edges_ = guest::cof::ComputeOwnerButtonEdges(lastButtons_,
        static_cast<std::uint32_t>(player_->pev->button));
    phase_ = Phase::Input;
    return true;
}

bool GlockOwner::AcceptedNativeJump() noexcept
{
    if (!Current() || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        phase_ != Phase::Input) return false;
    // Native Jump has no CoF CONTENTS exception: retain its stricter water gate.
    if (player_->pev->waterlevel >= 2 ||
        !guest::cof::CanBeginOwnerJump({0, static_cast<std::uint32_t>(player_->pev->flags),
            player_->pev->waterlevel, player_->pev->watertype, edges_.pressed,
            player_->pev->groundentity != nullptr})) return false;
    guest::cof::RecordAcceptedOwnerJump(jump_, gpGlobals->time);
    return true;
}

bool GlockOwner::AfterMovement() noexcept
{
    if (!Current() || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        phase_ != Phase::Input) return false;
    guest::cof::AdvanceOwnerJumpAfterMovement(jump_, OriginalFlags(), gpGlobals->time);
    phase_ = Phase::Weapons;
    return true;
}

bool GlockOwner::EndPostThink() noexcept
{
    if (!Current() || phase_ != Phase::Weapons) return false;
    lastButtons_ = static_cast<std::uint32_t>(player_->pev->button);
    phase_ = Phase::Outside;
    return true;
}
bool GlockOwner::CancelFrame() noexcept
{
    if (!Current()) return false;
    phase_ = Phase::Outside;
    return true;
}

bool GlockOwner::Read(guest::cof::server::GlockOwnerFrame& output) const noexcept
{
    if (!Current()) return false;
    guest::cof::server::GlockOwnerFrame frame{};
    // Each zero below follows a named, enforced host-mode invariant. These
    // are neither original defaults nor reads of CS's unrelated fuser3.
    frame.activityBlock = 0; // freeActivity
    frame.pendingGateTime = 0; frame.pendingGate = false; // phoneUnavailable
    frame.quietAudio = false; // substanceFree
    frame.infiniteAmmo = false; // finiteAmmo
    frame.eventOwner2814 = 0; // firstPerson
    frame.sourceMode = guest::cof::PistolSource::NormalMuzzle; // freeActivity
    frame.toggleAim = rules_.toggleAim;
    frame.bulletDamage = rules_.bulletDamage;
    frame.meleeDamage = rules_.meleeDamage;
    frame.reserveLimit = rules_.reserveLimit;
    frame.condition3AA = jump_.justJumped != 0;
    frame.firstDrawn = persistent_.firstDrawn;
    frame.lightOn = persistent_.lightOn;
    frame.originalFlags = OriginalFlags();
    frame.pressed = edges_.pressed;
    frame.released = edges_.released;
    frame.randomSeed = static_cast<std::uint32_t>(player_->random_seed);
    output = frame;
    return true;
}

bool GlockOwner::SetFirstDrawn(bool value) noexcept
{
    if (!Current()) return false;
    persistent_.firstDrawn = value;
    return true;
}

bool GlockOwner::SetLight(bool value) noexcept
{
    if (!Current()) return false;
    persistent_.lightOn = value;
    return true;
}

bool GlockOwner::PersistentState(NativeOwnerPersistentState& output) const noexcept
{
    if (!Current()) return false;
    output = persistent_;
    return true;
}
}
