// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CSOLMonsterDamageCall;
class CSOLMonsterSpawnCall;
namespace csol::scenario { struct ConfigError; }

// Verified ordinary/Oberon v158 is pass-through; elite v158 modifies damage
// before armor, separately from v154's hitgroup/source modifiers.
class CCSOLMonsterDamageBehavior
{
public:
    virtual ~CCSOLMonsterDamageBehavior() = default;
    virtual bool ResolveHitDamage(CSOLMonsterDamageCall& call, int directPlayerWeapon,
        csol::scenario::ConfigError& error) = 0;
    virtual bool ResolvePreArmorDamage(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error);
    virtual bool DamageFlinch(CSOLMonsterSpawnCall& call,
        csol::scenario::ConfigError& error) = 0;
};

// The real block_missile implementation supplies weapon267's current
// parameter. Classname alone cannot establish its C++ layout or authority.
class CSOLBlockMissileDamageSource
{
public:
    virtual bool OberonDamageMultiplier(CSOLMonsterDamageCall& call, float& value,
        csol::scenario::ConfigError& error) const = 0;
protected:
    ~CSOLBlockMissileDamageSource() = default;
};

// Actual AT4 variant446 carries the original source+CF9 byte. Its two Boss
// multipliers differ; neither the owner's current gun nor pev.fuser fields
// are a valid substitute for this launch/impact state.
class CSOLAt4RocketDamageState
{
public:
    virtual bool AlternateBossDamage() const = 0;
protected:
    ~CSOLAt4RocketDamageState() = default;
};

// Original Oberon v154=113E7030, including common113EE690 before its source
// modifiers. This helper is restricted to the implemented first-map mode15
// contract; mode28's attacker-region gate is a separate rule implementation.
bool CSOL_ResolveOberonHitDamage(CSOLMonsterDamageCall& call, int directPlayerWeapon,
    csol::scenario::ConfigError& error);
