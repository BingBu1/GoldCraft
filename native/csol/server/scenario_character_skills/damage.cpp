// SPDX-License-Identifier: GPL-3.0-or-later
#include "damage.hpp"
#include <cmath>

namespace csol::scenario::skills
{
float DefaultDamageParameter(DamageParameter parameter) noexcept
{
    // Original parameter constructors: 1003B810, 1003DD90, 10041260,
    // 10041DA0 and 10041AD0. All are float32 values.
    return parameter == DamageParameter::Recovery31 ? 0.02f : 0.01f;
}

bool HasDamageImplementation(DamageCallback callback) noexcept
{
    switch (callback)
    {
    case DamageCallback::Empty:
    case DamageCallback::Type16:
    case DamageCallback::StopCloaking11:
    case DamageCallback::StopCloaking12:
    case DamageCallback::Type31:
    case DamageCallback::StopCloaking13:
    case DamageCallback::Accumulate46:
    case DamageCallback::Recover48:
    case DamageCallback::Recover49:
    case DamageCallback::Recover50: return true;
    default: return false;
    }
}

bool ApplyDamageCallback(const SkillTraits& skill, DamageState& state, float damage,
    std::span<const std::uint8_t, 32> context, DamageHost& host, ConfigError& error)
{
    #pragma clang fp contract(off)
    if (!host.Current() || !error.message.empty()) return false;
    const auto step = [&](bool result)
    { return host.Current() && result && error.message.empty(); };
    if (skill.damageCallback == DamageCallback::Empty) return true;
    if (!HasDamageImplementation(skill.damageCallback))
    {
        error.message = "scenario character damage callback requires its area-attack implementation";
        return false;
    }

    if (skill.damageCallback == DamageCallback::StopCloaking11 ||
        skill.damageCallback == DamageCallback::StopCloaking12 ||
        skill.damageCallback == DamageCallback::StopCloaking13 ||
        skill.damageCallback == DamageCallback::Accumulate46)
    {
        bool value{};
        if (!step(host.SourceHasNonzeroOffset(value, error))) return false;
        if (!value) return true;
        // The original key11 and subtype46 paths do not inspect these flags.
        if ((skill.damageCallback == DamageCallback::StopCloaking12 ||
             skill.damageCallback == DamageCallback::StopCloaking13) &&
            (context[2] || context[20] || context[7])) return true;
        if (!step(host.SourceIsOwner(value, error))) return false;
        if (!value)
        {
            int weapon{};
            if (!step(host.SourceWeapon(weapon, error))) return false;
            if (!weapon) return true;
        }
        if (skill.damageCallback == DamageCallback::Accumulate46)
        {
            // 106E29D0 has neither an active-time nor a positive-damage gate.
            const float accumulated = state.accumulated46 + damage;
            if (!std::isfinite(damage) || !std::isfinite(accumulated))
            { error.message = "scenario character accumulated damage is invalid"; return false; }
            state.accumulated46 = accumulated;
            return true;
        }
        const int key = skill.damageCallback == DamageCallback::StopCloaking11 ? 11 :
            skill.damageCallback == DamageCallback::StopCloaking12 ? 12 : 13;
        return step(host.StopCloaking(key, error));
    }

    bool value{};
    if (skill.damageCallback == DamageCallback::Type31)
    {
        // 106E2830 is gated by the owned StatBuff ID, not v10 activeUntil.
        if (!step(host.InstalledBuff(state.installedBuffId, value, error))) return false;
    }
    else if (!step(host.Active(value, error))) return false;
    if (!value) return true;

    DamageParameter parameter{};
    switch (skill.damageCallback)
    {
    case DamageCallback::Type16:
        if (!step(host.Alive(value, error))) return false;
        if (!value) return true;
        parameter = DamageParameter::Recovery16;
        break;
    case DamageCallback::Type31: parameter = DamageParameter::Recovery31; break;
    case DamageCallback::Recover48: parameter = DamageParameter::Recovery48; break;
    case DamageCallback::Recover49: parameter = DamageParameter::Recovery49; break;
    case DamageCallback::Recover50: parameter = DamageParameter::Recovery50; break;
    default:
        error.message = "scenario character damage callback is not recognized";
        return false;
    }
    float ratio{};
    if (!step(host.Parameter(parameter, ratio, error))) return false;
    const float amount = damage * ratio;
    if (!std::isfinite(damage) || !std::isfinite(ratio) || !std::isfinite(amount))
    { error.message = "scenario character recovery amount is invalid"; return false; }
    if (skill.damageCallback != DamageCallback::Type16)
        return step(host.RecoverSelf(amount, error));

    // 106E8380 writes health directly and clamps only the upper limit. This
    // intentionally does not emit the separate TakeHealth recovery callbacks.
    float health{}, maximum{};
    if (!step(host.ReadHealth(health, maximum, error))) return false;
    const float added = health + amount;
    if (!std::isfinite(health) || !std::isfinite(maximum) || !std::isfinite(added))
    { error.message = "scenario character direct recovery health is invalid"; return false; }
    return step(host.WriteHealth(added > maximum ? maximum : added, error));
}
}
