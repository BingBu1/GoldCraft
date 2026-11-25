// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cbase.h"
#include "triggers.h"

#include <array>
#include <memory>

class CSOLScenarioEntityAccess;

// Fifth-season zs_evacuate1 entities (MP 39f962f3...). Evidence:
// docs/2026-11-16_取证-灾难先兆三图接入-report.md (section 3).

// classname trigger_nextmap (export 108F4200, size 0x190, vtable 117F3958,
// save name CNextMap): KeyValue 108EE3D0, Spawn 108F1CF0, Touch 108F2E50,
// Think 108EFA20, change 108ED0C0. CleanUpMap never restarts it.
class CCSOLNextMap final : public CBaseTrigger
{
public:
    void KeyValue(KeyValueData* data) override;
    void Spawn() override;
    void Restart() override { Spawn(); } // 108AC4A0 -> vt Spawn
    void EXPORT NextMapTouch(CBaseEntity* other);
    void EXPORT NextMapThink();
private:
    void Change();
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    float m_start{};             // +158
    bool m_done{};               // +15C
    int m_inside{};              // +160
    std::array<char, 32> m_map{}; // +164 "map"
    float m_duration{30.0f};     // +184
    bool m_clear{};              // +188 "clear"
};
