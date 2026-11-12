// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "scenario_entity_access.h"

#include <cstdint>
#include <memory>

// MP 39f962f3..., factory 113EB060, Spawn 108F2260, KeyValue 107DCE30.
// The brush retains engine absmin/absmax for the shared boss-room lookup.
class CCSOLBossRoom final : public CBaseEntity
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    int RoomId() const noexcept { return m_id; }

private:
    int m_id{}; // Original factory initializes +B0 to zero before KeyValue.
};

enum class CSOLBossRoomResult { Found, Missing, Unavailable };

// MP113E6D70/113EA3C0 share one cached room across Boss controllers. Keep it
// within the actual host session, while retaining constructor reset timing.
class CCSOLBossRoomCache final
{
public:
    // 113E4940 resets the id/minimum only; cached maximum survives that reset.
    bool ControllerConstructed(std::shared_ptr<const CSOLScenarioEntityAccess> access);
    CSOLBossRoomResult Bounds(int id, Vector& minimum, Vector& maximum);

private:
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::uint64_t m_generation{};
    int m_id{-1};
    Vector m_minimum{}, m_maximum{};
    bool m_busy{};
};
