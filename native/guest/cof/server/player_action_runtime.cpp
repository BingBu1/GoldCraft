// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "player_action_runtime.hpp"

namespace guest::cof::server
{
namespace
{
bool Finite(const StaminaCycleState& s) noexcept
{
    return std::isfinite(s.amount) && std::isfinite(s.lastSent) && std::isfinite(s.nextCycle) &&
        std::isfinite(s.auxiliaryTime) && std::isfinite(s.exhaustionActionDeadline) &&
        std::isfinite(s.voiceDeadline) && std::isfinite(s.exhaustionAudioDeadline) && std::isfinite(s.hudPulse);
}
bool Finite(const StaminaCycleOwner& o) noexcept
{ return std::isfinite(o.horizontalSpeed) && std::isfinite(o.voiceVariant); }
bool Prepared(const PlayerActionBackend& b) noexcept
{
    const auto& h = b.stamina;
    return b.current && b.dispatch && b.preparedTasks && !(b.preparedTasks & ~3U) &&
        h.current && h.clock && h.multiplayer && h.random && h.voice && h.hud;
}
PlayerCycleResult Result(StaminaCycleResult r) noexcept
{
    switch (r)
    {
    case StaminaCycleResult::Applied: return PlayerCycleResult::Applied;
    case StaminaCycleResult::Cancelled: return PlayerCycleResult::Cancelled;
    case StaminaCycleResult::MissingBackend: return PlayerCycleResult::MissingBackend;
    case StaminaCycleResult::InvalidInput: return PlayerCycleResult::InvalidInput;
    }
    return PlayerCycleResult::InvalidInput;
}
}

PlayerActionRuntime::PlayerActionRuntime() noexcept
{ for (auto& p : players_) p.runtime_ = this; }
bool PlayerActionRuntime::Install(goldcraft::IPlayerThinkStage& stage, std::uint64_t map) noexcept
{
    Reset();
    if (!map || !g_engfuncs.pfnIndexOfEdict) return false;
    stage_ = &stage; map_ = map;
    token_ = stage.Register(this, Stage);
    if (Current()) return true;
    Reset(); return false;
}
bool PlayerActionRuntime::Current() const noexcept
{ return stage_ && token_ && map_ && stage_->Current(token_); }
void PlayerActionRuntime::Reset() noexcept
{
    auto* stage = stage_; const auto token = token_;
    stage_ = nullptr; token_ = map_ = 0;
    for (auto& p : players_) { p.Retire(); p.latestLife_ = 0; }
    if (stage && token) stage->Release(token);
}
PlayerActionBinding PlayerActionRuntime::Prepare(int slot, CBasePlayer& player, std::uint64_t life,
    const StaminaCycleState& seed, const StaminaCycleOwner& owner, const PlayerActionBackend& backend) noexcept
{
    if (preparing_ || slot < 1 || slot > 32 || !life || !Current() || !Finite(seed) ||
        !Finite(owner) || !Prepared(backend) || !player.pev || !player.edict() ||
        g_engfuncs.pfnIndexOfEdict(player.edict()) != slot) return {};
    const auto token = token_;
    auto& p = players_[slot];
    if (p.player_ == &player && p.life_ == life && p.Current()) return PlayerActionBinding(p);
    if (token != token_ || !Current() || life <= p.latestLife_) return {};
    preparing_ = true;
    const bool accepted = p.Admit(player, life, seed, owner, backend);
    preparing_ = false;
    return accepted ? PlayerActionBinding(p) : PlayerActionBinding{};
}
PlayerActionBinding PlayerActionRuntime::Find(int slot, CBasePlayer& player) noexcept
{
    if (slot < 1 || slot > 32) return {};
    auto& p = players_[slot];
    return p.player_ == &player && p.Current() ? PlayerActionBinding(p) : PlayerActionBinding{};
}
void PlayerActionRuntime::Stage(void* context, edict_t* entity, float frameTime, double timebase) noexcept
{
    auto& self = *static_cast<PlayerActionRuntime*>(context);
    if (!self.Current() || !entity || !g_engfuncs.pfnIndexOfEdict) return;
    const int slot = g_engfuncs.pfnIndexOfEdict(entity);
    if (slot >= 1 && slot <= 32) self.players_[slot].Stage(entity, frameTime, timebase);
}

bool PlayerActionLife::LocalIdentityCurrent() const noexcept
{
    return runtime_ && runtime_->map_ == map_ && runtime_->Current() && player_ && entity_ &&
        !entity_->free && entity_->serialnumber == serial_ && entity_->pvPrivateData == player_ &&
        player_->pev == &entity_->v && player_->m_iNumSpawns == spawn_ && !player_->has_disconnected;
}
bool PlayerActionLife::LocalCurrent() const noexcept
{ return LocalIdentityCurrent() && player_->pev->deadflag == DEAD_NO && player_->pev->health > 0; }
bool PlayerActionLife::Current() noexcept
{
    if (!LocalCurrent()) { Retire(); return false; }
    const auto b = backend_; auto* player = player_; const auto binding = binding_;
    const bool valid = b.current(b.context, *player, map_, life_);
    if (binding != binding_) return false;
    if (valid && LocalCurrent()) return true;
    Retire(); return false;
}
void PlayerActionLife::Retire() noexcept
{
    ++binding_; think_.Invalidate();
    player_ = nullptr; entity_ = nullptr; backend_ = {};
    life_ = map_ = 0; activeStamina_ = nullptr; activeOwner_ = nullptr;
    stamina_ = {}; owner_ = {}; phase_ = Phase::None; lastThink_ = PlayerThinkResult::Cancelled;
}
bool PlayerActionLife::CaptureAndRetire(StaminaCycleState& output) noexcept
{
    const auto binding = binding_;
    const bool current = LocalIdentityCurrent();
    // The engine stage's Current callback may synchronously replace this life.
    if (binding != binding_) return false;
    if (!current) { Retire(); return false; }
    const auto state = activeStamina_ ? *activeStamina_ : stamina_;
    Retire();
    if (!Finite(state)) return false;
    output = state;
    return true;
}
bool PlayerActionLife::Admit(CBasePlayer& player, std::uint64_t life, const StaminaCycleState& seed,
    const StaminaCycleOwner& owner, const PlayerActionBackend& backend) noexcept
{
    Retire(); latestLife_ = life; life_ = life; map_ = runtime_->map_;
    player_ = &player; entity_ = player.edict(); serial_ = entity_->serialnumber; spawn_ = player.m_iNumSpawns;
    backend_ = backend; stamina_ = seed; owner_ = owner;
    if (think_.Bind(player, {this, TaskCurrent, TaskDispatch, map_, life_, backend.preparedTasks})) return true;
    Retire(); return false;
}
bool PlayerActionLife::TaskCurrent(void* context, CBasePlayer& player, std::uint64_t map, std::uint64_t life) noexcept
{
    auto& self = *static_cast<PlayerActionLife*>(context);
    return self.player_ == &player && self.map_ == map && self.life_ == life && self.Current();
}
bool PlayerActionLife::TaskDispatch(void* context, CBasePlayer& player, PlayerThinkKind kind) noexcept
{
    auto& self = *static_cast<PlayerActionLife*>(context);
    if (!self.Current()) return false;
    const auto backend = self.backend_;
    return backend.dispatch(backend.context, player, kind);
}
bool PlayerActionLife::Read(StaminaCycleState& state, StaminaCycleOwner& owner) noexcept
{
    if (!Current()) return false;
    state = activeStamina_ ? *activeStamina_ : stamina_;
    owner = activeOwner_ ? *activeOwner_ : owner_;
    return true;
}
bool PlayerActionLife::SetStamina(float value) noexcept
{
    if (!std::isfinite(value) || !Current()) return false;
    (activeStamina_ ? *activeStamina_ : stamina_).amount = value;
    return true; // Original debit can be negative until the PostThink clamp.
}
bool PlayerActionLife::SetOwner(const StaminaCycleOwner& owner) noexcept
{
    if (!Finite(owner) || !Current()) return false;
    (activeOwner_ ? *activeOwner_ : owner_) = owner; return true;
}
bool PlayerActionLife::BeginCommand() noexcept
{
    if (activeStamina_ || !Current()) return false;
    ++command_;
    phase_ = Phase::AwaitThink; return true;
}
void PlayerActionLife::Stage(edict_t* entity, float frameTime, double timebase) noexcept
{
    if (entity_ != entity || phase_ != Phase::AwaitThink || !Current() ||
        !std::isfinite(frameTime) || frameTime < 0 || !std::isfinite(timebase)) return;
    const auto binding = binding_, command = command_;
    phase_ = Phase::None;
    const auto result = think_.Run(frameTime, timebase);
    if (binding == binding_ && command == command_)
    { lastThink_ = result; phase_ = Phase::AwaitPost; }
}

struct PlayerActionLife::CycleCall
{
    PlayerActionLife& life;
    std::uint64_t binding;
    StaminaCycleHost backend;
    static bool Current(void* context) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        return c.binding == c.life.binding_ && c.life.Current() &&
            c.backend.current(c.backend.context) && c.binding == c.life.binding_ && c.life.LocalCurrent();
    }
    static float Clock(void* context) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        if (!Current(context)) return NAN;
        const float value = c.backend.clock(c.backend.context);
        return Current(context) ? value : NAN;
    }
    static bool Multiplayer(void* context, bool& value) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        return Current(context) && c.backend.multiplayer(c.backend.context, value) && Current(context);
    }
    static bool Random(void* context, int low, int high, int& value) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        return Current(context) && c.backend.random(c.backend.context, low, high, value) && Current(context);
    }
    static bool Voice(void* context, const StaminaVoiceEvent& event) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        return Current(context) && c.backend.voice(c.backend.context, event) && Current(context);
    }
    static bool Hud(void* context, const StaminaHudEvent& event) noexcept
    {
        auto& c = *static_cast<CycleCall*>(context);
        return Current(context) && c.backend.hud(c.backend.context, event) && Current(context);
    }
};
PlayerCycleResult PlayerActionLife::Cycle(bool spawn) noexcept
{
    if (activeStamina_) return PlayerCycleResult::NotReady;
    if (!Current()) return PlayerCycleResult::Cancelled;
    const auto binding = binding_;
    auto state = stamina_; auto owner = owner_;
    CycleCall call{*this, binding, backend_.stamina};
    activeStamina_ = &state; activeOwner_ = &owner;
    const StaminaCycleHost bridge{&call, CycleCall::Current, CycleCall::Clock, CycleCall::Multiplayer,
        CycleCall::Random, CycleCall::Voice, CycleCall::Hud};
    const auto result = spawn ? SpawnStaminaCycle(state, bridge) : AdvanceStaminaCycle(state, owner, bridge);
    if (binding != binding_) return PlayerCycleResult::Cancelled;
    activeStamina_ = nullptr; activeOwner_ = nullptr;
    if (!Current()) return PlayerCycleResult::Cancelled;
    stamina_ = state; owner_ = owner;
    return Result(result);
}
PlayerCycleResult PlayerActionLife::Spawn() noexcept { return Cycle(true); }
PlayerCycleResult PlayerActionLife::PostThink() noexcept
{
    if (!Current()) return PlayerCycleResult::Cancelled;
    if (phase_ != Phase::AwaitPost || activeStamina_) return PlayerCycleResult::NotReady;
    phase_ = Phase::None;
    return Cycle(false);
}
}
