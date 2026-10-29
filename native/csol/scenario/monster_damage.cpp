// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_damage.hpp"

#include <cmath>
#include <random>

namespace csol::scenario
{
double WeaponAttributeRandom(std::uint32_t seed)
{
    std::mt19937 engine(seed);
    return WeaponAttributeRandom(engine);
}

double WeaponAttributeRandom(std::mt19937& engine)
{
    const auto first = static_cast<std::uint32_t>(engine());
    const auto second = static_cast<std::uint32_t>(engine());
    // CSO's old distribution puts the second word in the high 32 bits of
    // this 53-bit integer. std::uniform_real_distribution is not a portable
    // substitute: its word order and rounding are implementation-defined.
    const std::uint64_t value = (std::uint64_t{second} << 21) + (first >> 11);
    return static_cast<double>(value) * 0x1p-53;
}

namespace
{
float WeaponMinimum(int weapon)
{
    switch (weapon)
    {
    case 3: return 225;
    case 18: case 47: case 51: case 53: case 71:
    case 311: case 415: case 447: return 345;
    case 40: return 48;
    case 66: return 330;
    default: return 0;
    }
}

bool Commit(float value, float& output)
{
    // Malformed/nonfinite input is a host rejection, not an assertion about
    // how the original unvalidated SSE/x87 path handles every NaN/overflow.
    if (!std::isfinite(value)) return false;
    output = value;
    return true;
}
}

bool ResolveMonsterTraceDamage(float input, int traceHitgroup, float& output)
{
    if (!std::isfinite(input)) return false;
    float damage = input;
    switch (traceHitgroup)
    {
    case 1: damage = input * 3.0f; break;
    case 2: case 3: damage = static_cast<float>(static_cast<double>(input) * 1.5); break;
    case 6: case 7: damage = static_cast<float>(static_cast<double>(input) * 0.75); break;
    default: break;
    }
    return Commit(damage, output);
}

bool ResolveOrdinaryMonsterDamage(float input, int hitgroup, int directPlayerWeapon,
    float& output)
{
    if (!std::isfinite(input)) return false;
    const float minimum = WeaponMinimum(directPlayerWeapon);
    // MAXSS selects its second operand on equal values, including -0/+0.
    float damage = input > minimum ? input : minimum;
    switch (hitgroup)
    {
    case 1: damage *= 3.0f; break;
    case 3: damage = static_cast<float>(static_cast<double>(damage) * 1.25); break;
    case 6: case 7: damage = static_cast<float>(static_cast<double>(damage) * 0.75); break;
    case 8: damage = static_cast<float>(static_cast<double>(damage) * 0.4); break;
    case 9: damage += damage; break;
    default: break;
    }
    return Commit(damage, output);
}

bool ResolveMonsterArmorDamage(float input, float armor, std::uint32_t damageBits,
    MonsterArmorDamage& output)
{
    if (!std::isfinite(input) || !std::isfinite(armor)) return false;
    MonsterArmorDamage candidate{input, armor};
    if (armor != 0.0f)
    {
        const float costFactor = (damageBits & 0x01000040u) ? 1.0f : 0.5f;
        candidate.healthDamage = input * 0.5f;
        float cost = (input - candidate.healthDamage) * costFactor;
        if (cost > armor)
        {
            candidate.remainingArmor = 0;
            candidate.healthDamage = input - (1.0f / costFactor) * armor;
        }
        else
        {
            if (cost < 0) cost = 1;
            candidate.remainingArmor = armor - cost;
            if (candidate.remainingArmor <= 0) candidate.remainingArmor = 0;
        }
    }
    if (!std::isfinite(candidate.healthDamage) || !std::isfinite(candidate.remainingArmor)) return false;
    output = candidate;
    return true;
}
}
