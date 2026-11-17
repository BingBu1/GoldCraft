// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace guest::cof
{
struct ShotgunActionState
{
    std::int32_t clip{}, reserve{}, action{}, special{}, reloadStage{};
    std::int32_t emptyStart{}, reloadAbort{};
    bool aiming{}, pendingReload{};
    float primary{}, secondary{}, extra{}, idle{}, ownerAttack{};
    float playerAnimationDeadline{}, insertDeadline{};
};
enum class ShotgunControlEventKind : unsigned char
{
    ViewAnimation, PlayerReloadStart, PlayerReloadInsert, ScheduleShellInsert,
    SchedulePushback, PlayerWeaponMelee,
    PlayerReloadEndEmpty, PlayerReloadEndLoaded, PlayerReloadEndAbort
};
struct ShotgunControlEvent
{
    ShotgunControlEventKind kind;
    int sequence{};
    float delay{};
};
struct ShotgunControlHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const ShotgunControlEvent&) noexcept {};
};
enum class ShotgunControlResult : unsigned char
{
    Applied, Deferred, Ineligible, MissingHost, ConsumerRejected, Cancelled, InvalidInput,
    MissingSequence, UnsupportedState
};

// Matched MP1006E3E0 and1006E6F0. The insert callback belongs to the weapon;
// ordinary reload fills five rounds. SDK skill-expanded clips need a separate
// admission guard before starting an ordinary reload, without losing rounds.
ShotgunControlResult StartShotgunReload(ShotgunActionState&, const ShotgunControlHost&) noexcept;
void InsertShotgunShell(ShotgunActionState&) noexcept;
// Original PostFrame prefix observes pressed attack, not held attack.
void ObserveShotgunReloadAttack(ShotgunActionState&, bool attackPressed) noexcept;
// Only the original PostFrame1006D6BF..1006D857 reload subgraph. Its caller must
// retain the earlier pending-aim/input ordering and subsequent common frame.
ShotgunControlResult AdvanceShotgunReload(ShotgunActionState&, const ShotgunControlHost&) noexcept;
// Original1006E620 schedules the shared player Pushback task. This component
// neither owns a stamina recovery cycle nor changes any monster AI.
ShotgunControlResult StartShotgunPushback(ShotgunActionState&, float stamina,
    const ShotgunControlHost&) noexcept;
}
