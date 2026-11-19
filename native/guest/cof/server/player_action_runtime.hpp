// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "player_think.hpp"
#include "../stamina_cycle.hpp"
#include "interface.h"
#include "native/include/goldcraft/player_think_stage_api.hpp"
#include <array>

namespace guest::cof::server
{
struct PlayerActionBackend
{
    void* context{};
    bool (*current)(void*, CBasePlayer&, std::uint64_t map, std::uint64_t life) noexcept {};
    bool (*dispatch)(void*, CBasePlayer&, PlayerThinkKind) noexcept {};
    StaminaCycleHost stamina;
    unsigned preparedTasks{};
};
// User-selected host adaptation: the current CSOL stamina service owns its
// amount, recovery and HUD. The task owner only reads it; it never seeds,
// restores or advances a second CoF stamina cycle.
struct SharedPlayerActionBackend
{
    void* context{};
    bool (*current)(void*, CBasePlayer&, std::uint64_t map, std::uint64_t life) noexcept {};
    bool (*dispatch)(void*, CBasePlayer&, PlayerThinkKind) noexcept {};
    bool (*stamina)(void*, float& amount, bool& infinite) noexcept {};
    unsigned preparedTasks{};
};
enum class PlayerCycleResult : unsigned char { Applied, NotReady, Cancelled, MissingBackend, InvalidInput };
class PlayerActionRuntime;
class PlayerActionBinding;

// Host adaptation: one authenticated life, outside model/weapon bindings. The
// seed and original movement flags must come from real authoritative producers.
class PlayerActionLife final
{
private:
    friend class PlayerActionRuntime;
    friend class PlayerActionBinding;
    bool Current() noexcept;
    bool Read(StaminaCycleState&, StaminaCycleOwner&) noexcept;
    bool ReadStamina(float&, bool&) noexcept;
    bool SetStamina(float) noexcept;
    bool SetOwner(const StaminaCycleOwner&) noexcept;
    PlayerThinkSlot* Think() noexcept { return Current() ? &think_ : nullptr; }
    bool BeginCommand() noexcept;
    PlayerCycleResult Spawn() noexcept;
    PlayerCycleResult PostThink() noexcept;
    bool EndSharedCommand() noexcept;
    void Retire() noexcept;
    bool CaptureAndRetire(StaminaCycleState&) noexcept;
    PlayerThinkResult LastThink() const noexcept { return lastThink_; }

    struct CycleCall;
    enum class Phase : unsigned char { None, AwaitThink, AwaitPost };
    bool Admit(CBasePlayer&, std::uint64_t life, const StaminaCycleState&,
               const StaminaCycleOwner&, const PlayerActionBackend&,
               bool (*sharedStamina)(void*, float&, bool&) noexcept = nullptr) noexcept;
    bool LocalCurrent() const noexcept;
    bool LocalIdentityCurrent() const noexcept;
    void Stage(edict_t*, float, double) noexcept;
    PlayerCycleResult Cycle(bool spawn) noexcept;
    static bool TaskCurrent(void*, CBasePlayer&, std::uint64_t, std::uint64_t) noexcept;
    static bool TaskDispatch(void*, CBasePlayer&, PlayerThinkKind) noexcept;
    PlayerActionRuntime* runtime_{};
    CBasePlayer* player_{};
    edict_t* entity_{};
    int serial_{}, spawn_{};
    std::uint64_t binding_{}, life_{}, latestLife_{}, map_{}, command_{};
    PlayerActionBackend backend_{};
    bool (*sharedStamina_)(void*, float&, bool&) noexcept {};
    StaminaCycleState stamina_{};
    StaminaCycleOwner owner_{};
    StaminaCycleState* activeStamina_{};
    StaminaCycleOwner* activeOwner_{};
    PlayerThinkSlot think_;
    Phase phase_{};
    PlayerThinkResult lastThink_{PlayerThinkResult::Cancelled};
};

class PlayerActionBinding final
{
public:
    PlayerActionBinding() noexcept = default;
    explicit operator bool() const noexcept { return Current(); }
    bool HasBinding() const noexcept { return life_ != nullptr; }
    bool Current() const noexcept { return Matches() && life_->Current(); }
    bool PreparedFor(CBasePlayer& player, std::uint64_t map, std::uint64_t life) const noexcept
    {
        return Matches() && life_->player_ == &player && life_->map_ == map && life_->life_ == life &&
            life_->think_.PreparedFor(player, PlayerThinkKind::Pushback, map, life);
    }
    bool Read(StaminaCycleState& state, StaminaCycleOwner& owner) const noexcept
    { return Matches() && life_->Read(state, owner); }
    bool ReadStamina(float& amount, bool& infinite) const noexcept
    { return Matches() && life_->ReadStamina(amount, infinite); }
    bool SetStamina(float amount) const noexcept { return Matches() && life_->SetStamina(amount); }
    bool SetOwner(const StaminaCycleOwner& owner) const noexcept { return Matches() && life_->SetOwner(owner); }
    PlayerThinkSlot* Think() const noexcept { return Matches() ? life_->Think() : nullptr; }
    bool BeginCommand() const noexcept { return Matches() && life_->BeginCommand(); }
    PlayerCycleResult Spawn() const noexcept { return Matches() ? life_->Spawn() : PlayerCycleResult::Cancelled; }
    PlayerCycleResult PostThink() const noexcept { return Matches() ? life_->PostThink() : PlayerCycleResult::Cancelled; }
    // Explicit phase completion when stamina is externally owned; never runs
    // the original CoF recovery/voice/HUD consumer.
    bool EndSharedCommand() const noexcept { return Matches() && life_->EndSharedCommand(); }
    PlayerThinkResult LastThink() const noexcept
    { return Current() ? life_->LastThink() : PlayerThinkResult::Cancelled; }
    void Retire() const noexcept { if (Matches()) life_->Retire(); }
    // Native Killed may enter after health reaches zero. Capture only for this
    // same entity/spawn/map, then permanently revoke its action capability.
    // The host owns any later connection-keyed seed; this never grants a life.
    bool CaptureAndRetire(StaminaCycleState& state) const noexcept
    { return Matches() && life_->CaptureAndRetire(state); }

private:
    friend class PlayerActionRuntime;
    bool Matches() const noexcept { return life_ && life_->binding_ == generation_; }
    PlayerActionBinding(PlayerActionLife& life) noexcept : life_(&life), generation_(life.binding_) {}
    PlayerActionLife* life_{};
    std::uint64_t generation_{};
};

class PlayerActionRuntime final
{
public:
    PlayerActionRuntime() noexcept;
    ~PlayerActionRuntime() { Reset(); }
    PlayerActionRuntime(const PlayerActionRuntime&) = delete;
    PlayerActionRuntime& operator=(const PlayerActionRuntime&) = delete;
    bool Install(goldcraft::IPlayerThinkStage&, std::uint64_t map) noexcept;
    void Reset() noexcept;
    bool Current() const noexcept;
    // Life tokens increase within each map/slot. Repeated preparation of the
    // same living identity preserves its state, task, and original backend.
    PlayerActionBinding Prepare(int slot, CBasePlayer&, std::uint64_t life,
        const StaminaCycleState& seed, const StaminaCycleOwner&, const PlayerActionBackend&) noexcept;
    PlayerActionBinding PrepareShared(int slot, CBasePlayer&, std::uint64_t life,
        const SharedPlayerActionBackend&) noexcept;
    PlayerActionBinding Find(int slot, CBasePlayer&) noexcept;

private:
    friend class PlayerActionLife;
    static void Stage(void*, edict_t*, float, double) noexcept;
    std::array<PlayerActionLife, 33> players_;
    goldcraft::IPlayerThinkStage* stage_{};
    std::uint64_t token_{}, map_{};
    bool preparing_{};
};
}
