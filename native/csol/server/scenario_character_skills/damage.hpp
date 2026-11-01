// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog.hpp"
#include "../../scenario/config.hpp"
#include <cstdint>
#include <span>

namespace csol::scenario::skills
{
enum class DamageParameter { Recovery16, Recovery31, Recovery48, Recovery49, Recovery50 };
float DefaultDamageParameter(DamageParameter parameter) noexcept;

struct DamageState
{
    int installedBuffId{-1}; // Only the subtype31 installed StatBuff identity.
    float accumulated46{}; // Subtype46 +30, independently of activeUntil.
    float nextArea28{}; // Subtype28 +30; activation resets this to zero.
    float nextArea12{}; // Subtype12 +30; independent one-second area gate.
};

class DamageHost
{
public:
    virtual ~DamageHost() = default;
    virtual bool Current() const = 0;
    virtual bool Active(bool& value, ConfigError& error) = 0;
    virtual bool Alive(bool& value, ConfigError& error) = 0;
    // These are deliberately separate: original callbacks test OFFSET before
    // the borrowed context flags, and only then query a non-owner's v138.
    virtual bool SourceHasNonzeroOffset(bool& value, ConfigError& error) = 0;
    virtual bool SourceIsOwner(bool& value, ConfigError& error) = 0;
    virtual bool SourceWeapon(int& value, ConfigError& error) = 0;
    virtual bool StopCloaking(int key, ConfigError& error) = 0;
    virtual bool InstalledBuff(int identity, bool& value, ConfigError& error) = 0;
    virtual bool Parameter(DamageParameter parameter, float& value, ConfigError& error) = 0;
    virtual bool ReadHealth(float& value, float& maximum, ConfigError& error) = 0;
    virtual bool WriteHealth(float value, ConfigError& error) = 0;
    // Original v4C(owner pev, amount, 0). Do not replace with direct health
    // assignment; normal recovery hooks/limits and notifications belong here.
    virtual bool RecoverSelf(float amount, ConfigError& error) = 0;
};

// Subtype28 additionally spawns an area attack; it is not an empty callback
// and remains explicitly unavailable until that complete effect is supplied.
bool HasDamageImplementation(DamageCallback callback) noexcept;
bool ApplyDamageCallback(const SkillTraits& skill, DamageState& state, float damage,
    std::span<const std::uint8_t, 32> context, DamageHost& host, ConfigError& error);
}
