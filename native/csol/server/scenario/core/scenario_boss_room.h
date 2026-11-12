// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"

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
