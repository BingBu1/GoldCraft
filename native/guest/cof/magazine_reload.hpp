// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace guest::cof
{
// Ordinary magazine completion only. Revolver inserts and original ID 23 use
// different paths. Starting a reload, selecting its model sequence and input
// arbitration belong to the weapon, not this arithmetic helper.
struct MagazineReload
{
    int clip{}, reserve{};
    float nextAttack{};
    bool loading{};
};

enum class ReloadCommitStatus : unsigned char { Pending, Committed, Invalid };
struct ReloadCommit
{
    ReloadCommitStatus status{ReloadCommitStatus::Pending};
    int rounds{};
};

// The original notifies its owner AFTER transferring ammunition and BEFORE
// clearing loading. A typed observer preserves that ordering without owning
// an entity, allocating a callback, or storing a process pointer in a packet.
using ReloadObserver = void (*)(void*, const MagazineReload&) noexcept;

// hl.dll 0036b91c, VA 1011D91F..1011D9B6; domain evidence is recorded before
// implementation in docs/2026-11-11_接入-CryOfFear武器-report.md.
// nextAttack and weaponTime use the same authoritative weapon clock.
// Invalid host data is reported without mutation; it is never a success.
ReloadCommit CommitMagazineReload(MagazineReload& state, int capacity,
    std::uint32_t itemFlags, float weaponTime,
    ReloadObserver observer = nullptr, void* context = nullptr) noexcept;
}
