// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapons.h"
#include <cmath>
#include <cstdint>

namespace guest::cof::server
{
enum class PlayerThinkKind : unsigned char { Pushback, WeaponMelee };
constexpr unsigned PlayerThinkBit(PlayerThinkKind kind) noexcept { return 1U << unsigned(kind); }
struct PlayerThinkBackend
{
    void* context{};
    // Authenticate player connection/map/life only. Weapon selection, model
    // generation and an individual weapon's lifetime must not own this task.
    bool (*current)(void*, CBasePlayer&, std::uint64_t map, std::uint64_t life) noexcept {};
    bool (*dispatch)(void*, CBasePlayer&, PlayerThinkKind) noexcept {};
    std::uint64_t map{}, life{};
    unsigned prepared{};
};
enum class PlayerThinkResult : unsigned char { Dispatched, Deferred, Cancelled, Rejected, InvalidInput };

// One CoF think slot per authoritative player life, independent of CS's native
// think slot. The host must call Run at the original usercmd think stage after
// PlayerPreThink and before PM_Move, with the actual double clienttimebase.
// Calling from a map StartFrame or weapon PostFrame is not equivalent.
class PlayerThinkSlot final
{
public:
    bool Bind(CBasePlayer& player, PlayerThinkBackend backend) noexcept
    {
        Invalidate();
        if (!player.pev || !backend.current || !backend.dispatch || !backend.map || !backend.life ||
            !backend.prepared || (backend.prepared & ~3U)) return false;
        player_ = &player; entity_ = player.edict();
        if (!entity_) { Invalidate(); return false; }
        serial_ = entity_->serialnumber; spawn_ = player.m_iNumSpawns; backend_ = backend;
        if (Current()) return true;
        Invalidate(); return false;
    }
    void Invalidate() noexcept
    {
        ++binding_; player_ = nullptr; entity_ = nullptr; backend_ = {};
        deadline_ = internalDeadline_ = 0; installed_ = false;
    }
    bool Current() const noexcept
    {
        if (!LocalCurrent()) return false;
        const auto backend = backend_; auto* player = player_;
        const auto binding = binding_;
        return backend.current(backend.context, *player, backend.map, backend.life) &&
            LocalCurrent() && binding_ == binding && player_ == player &&
            backend_.context == backend.context && backend_.current == backend.current &&
            backend_.map == backend.map && backend_.life == backend.life;
    }
    bool PreparedFor(CBasePlayer& player, PlayerThinkKind kind,
                     std::uint64_t map, std::uint64_t life) const noexcept
    {
        return player_ == &player && backend_.map == map && backend_.life == life && unsigned(kind) <= unsigned(PlayerThinkKind::WeaponMelee) &&
            (backend_.prepared & PlayerThinkBit(kind)) && Current();
    }
    bool Schedule(CBasePlayer& player, PlayerThinkKind kind, float delay,
                  std::uint64_t map, std::uint64_t life) noexcept
    {
        if (!PreparedFor(player, kind, map, life) || !gpGlobals || !std::isfinite(gpGlobals->time) || !std::isfinite(delay))
            return false;
        // Original1011F680/1011AB20: store both deadlines, then the caller
        // installs its player callback. A new task replaces the previous one.
        const float next = static_cast<float>(double(gpGlobals->time) + double(delay));
        if (!std::isfinite(next)) return false;
        internalDeadline_ = deadline_ = next;
        kind_ = kind; installed_ = true;
        return true;
    }
    PlayerThinkResult Run(float frameTime, double clientTimebase) noexcept
    {
        const auto entryBinding = binding_;
        if (!Current())
        {
            if (binding_ == entryBinding) Invalidate();
            return PlayerThinkResult::Cancelled;
        }
        if (!gpGlobals || !std::isfinite(frameTime) || frameTime < 0 || !std::isfinite(clientTimebase))
            return PlayerThinkResult::InvalidInput;
        static_assert(std::uint32_t(FL_DORMANT) == 0x80000000U && FL_KILLME == 0x40000000U);
        // Engine1DEEE49 leaves dormant tasks pending. Native entity freeing
        // remains the host engine's responsibility; this adapter never frees a player.
        if ((std::uint32_t(player_->pev->flags) & (std::uint32_t(FL_DORMANT) | FL_KILLME)) ||
            !installed_ || deadline_ <= 0 || double(frameTime) + clientTimebase < double(deadline_))
            return PlayerThinkResult::Deferred;
        const float when = double(deadline_) < clientTimebase ? static_cast<float>(clientTimebase) : deadline_;
        deadline_ = 0; // Clear before callback; preserve a callback's rescheduling.
        gpGlobals->time = when;
        const auto backend = backend_; auto* player = player_;
        const auto binding = binding_; const auto kind = kind_;
        const bool accepted = backend.dispatch(backend.context, *player, kind);
        if (binding_ != binding) return PlayerThinkResult::Cancelled;
        if (!Current())
        {
            if (binding_ == binding) Invalidate();
            return PlayerThinkResult::Cancelled;
        }
        return accepted ? PlayerThinkResult::Dispatched : PlayerThinkResult::Rejected;
    }
    float Deadline() const noexcept { return deadline_; }
    float InternalDeadline() const noexcept { return internalDeadline_; }

private:
    bool LocalCurrent() const noexcept
    {
        return player_ && entity_ && !entity_->free && entity_->serialnumber == serial_ &&
            entity_->pvPrivateData == player_ && player_->pev == &entity_->v &&
            player_->m_iNumSpawns == spawn_ && !player_->has_disconnected &&
            player_->pev->deadflag == DEAD_NO && player_->pev->health > 0;
    }
    CBasePlayer* player_{};
    edict_t* entity_{};
    PlayerThinkBackend backend_{};
    std::uint64_t binding_{};
    int serial_{}, spawn_{};
    float deadline_{}, internalDeadline_{};
    PlayerThinkKind kind_{};
    bool installed_{};
};
}
