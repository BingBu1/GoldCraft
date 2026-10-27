// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string>
#include <string_view>

enum class CSOLBeamVariant { Beamgun, BeamgunLE, Count };
enum class CSOLDamageMode { Normal, Zombie, Scenario, Count };
enum class CSOLBeamDamage
{
    ShootA,
    ShootAExplosion,
    ShootAFullExplosion,
    FullChargeExplosion,
    ShootB1,
    ShootB2,
    ChargeShootA,
    ChargeShootBExplosion,
    WingManAttack1,
    WingManAttack2,
    KillEvasionExplosion,
    Count
};

// Server-owned values before hitgroup, armor and game-mode damage processing.
// A failed parse leaves the previous configuration intact. The caller must
// refuse weapon availability if the initial load fails.
class CCSOLBeamDamageConfig
{
public:
    bool Parse(std::string_view json, std::string& error);
    bool IsLoaded() const;
    float Get(CSOLBeamVariant weapon, CSOLDamageMode mode, CSOLBeamDamage attack) const;

private:
    using DamageSet = std::array<float, static_cast<unsigned>(CSOLBeamDamage::Count)>;
    using ModeSet = std::array<DamageSet, static_cast<unsigned>(CSOLDamageMode::Count)>;
    std::array<ModeSet, static_cast<unsigned>(CSOLBeamVariant::Count)> m_values{};
    bool m_loaded = false;
};
