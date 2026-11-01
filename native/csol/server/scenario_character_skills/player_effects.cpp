// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../scenario_player_runtime.h"
#include "../scenario_active_skill.h"
#include "../scenario_runtime.h"
#include "../scenario_death_owner.h"
#include "host_notifications.hpp"
#include <algorithm>
#include <cmath>

namespace
{
using csol::scenario::ConfigError;
using namespace csol::scenario::skills;

class HealingHost final : public CSOLScenarioSkillHealingHost
{
public:
    HealingHost(CCSOLScenarioPlayerRuntime& players,
        std::shared_ptr<CSOLScenarioPlayerRuntimeState> state,
        std::shared_ptr<CCSOLScenarioSession> session)
        : m_players(players), m_state(std::move(state)), m_session(std::move(session)) {}
    bool Current() const override
    { return m_players.Active() && m_players.Find(m_state->player) == m_state; }
    bool ReadTime(float& now, ConfigError& error) const override
    {
        if (!Current()) return false;
        if (!gpGlobals || !std::isfinite(gpGlobals->time))
        { error.message = "character healing clock is invalid"; return false; }
        now = gpGlobals->time;
        return true;
    }
    bool ReadHealth(float& health, float& maximum, ConfigError&) const override
    {
        auto* player = Current() ? m_state->player.Get<CBasePlayer>() : nullptr;
        if (!player) return false;
        health = player->pev->health;
        maximum = player->pev->max_health;
        return true;
    }
    bool WriteHealth(float health, ConfigError&) override
    {
        auto* player = Current() ? m_state->player.Get<CBasePlayer>() : nullptr;
        if (!player) return false;
        player->pev->health = health;
        return true;
    }
    bool NotifyRecoveryPulse(ConfigError& error) override
    {
        auto* player = Current() ? m_state->player.Get<CBasePlayer>() : nullptr;
        if (!player) return false;
        const auto origin = player->pev->origin;
        const int index = player->entindex();
        if (!Current()) return false;
        if (!m_session->Messages().SendMonsterHealing(index, origin, false))
        { error.message = "character healing pulse notification failed"; return false; }
        return Current();
    }
private:
    CCSOLScenarioPlayerRuntime& m_players;
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> m_state;
    std::shared_ptr<CCSOLScenarioSession> m_session;
};

float SumStat(const CSOLScenarioPlayerRuntimeState& state, float now,
    float CSOLScenarioStatBuff::* field)
{
    float result{};
    for (std::size_t first = 0; first < state.buffs.size();)
    {
        const int group = state.buffs[first].group;
        float value{};
        bool found{};
        auto next = first;
        for (; next < state.buffs.size() && state.buffs[next].group == group; ++next)
        {
            const auto& buff = state.buffs[next];
            if (!buff.Active(now)) continue;
            const float candidate = buff.*field;
            if (group == 1) value += candidate;
            else if (!found || candidate > value) value = candidate;
            found = true;
        }
        result += value;
        first = next;
    }
    return result;
}
}

bool CCSOLScenarioPlayerRuntime::InstallSkillHealing(const EHANDLE& player,
    float duration, float interval, float ratio, csol::scenario::ConfigError& error)
{
    const auto state = Resolve(player);
    if (!state) return false;
    HealingHost host(*this, state, m_session);
    return CSOL_InstallScenarioSkillHealing(state->skillHealing, duration, interval, ratio, host, error);
}

float CCSOLScenarioPlayerRuntime::SkillSpeed(const EHANDLE& player, float now) const
{
    const auto state = Resolve(player);
    return state ? SumStat(*state, now, &CSOLScenarioStatBuff::speed) : 0;
}

float CCSOLScenarioPlayerRuntime::SkillCooldownReduction(const EHANDLE& player, float now) const
{
    const auto state = Resolve(player);
    return state ? SumStat(*state, now, &CSOLScenarioStatBuff::cooldownReduction) : 0;
}

bool CCSOLScenarioPlayerRuntime::RefreshSkillAmmo(const EHANDLE& handle,
    bool expandClip, bool forcePublish, csol::scenario::ConfigError& error)
{
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player || !gpGlobals) return false;
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
    if (!weapon) return true;
    const EHANDLE weaponHandle(weapon);
    const auto current = [&]
    { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player &&
        weaponHandle.Get<CBasePlayerWeapon>() == weapon && player->m_pActiveItem == weapon; };
    const int slot = weapon->iItemSlot();
    if (!current()) return false;
    if (slot != PRIMARY_WEAPON_SLOT && slot != PISTOL_SLOT) return true;
    const int baseClip = weapon->iMaxClip();
    if (!current()) return false;
    const float extra = SumStat(*state, gpGlobals->time, &CSOLScenarioStatBuff::extraClip);
    if (expandClip && extra > 0 && baseClip > 0 && !state->expandedSkillWeapons.contains(weapon->m_iId))
    {
        const double clip = std::trunc(static_cast<float>(baseClip) * (1.0f + extra));
        if (!std::isfinite(clip) || clip < 0 || clip > INT_MAX)
        { error.message = "character skill clip expansion is invalid"; return false; }
        state->expandedSkillWeapons.insert(weapon->m_iId);
        weapon->m_iClip = static_cast<int>(clip);
    }
    const int ammo = weapon->PrimaryAmmoIndex();
    if (!current()) return false;
    if (ammo < 0 || ammo >= MAX_AMMO_SLOTS) return true;
    const bool enabled = std::any_of(state->buffs.begin(), state->buffs.end(),
        [](const auto& buff) { return buff.Active(gpGlobals->time) && buff.infiniteAmmo; });
    if (enabled)
    {
        const int maximum = weapon->iMaxAmmo1(); // Actual scenario-adjusted capacity.
        if (!current()) return false;
        state->savedSkillAmmo.try_emplace(ammo, player->m_rgAmmo[ammo]);
        player->m_rgAmmo[ammo] = maximum;
    }
    else if (const auto saved = state->savedSkillAmmo.find(ammo); saved != state->savedSkillAmmo.end())
    {
        player->m_rgAmmo[ammo] = saved->second;
        state->savedSkillAmmo.erase(saved);
    }
    state->skillAmmoWeapon = weaponHandle;
    if (enabled == state->skillInfiniteAmmo && !forcePublish) return true;
    state->skillInfiniteAmmo = enabled;
    return PublishInfiniteAmmo(*m_session, handle, enabled, error) && current();
}

bool CCSOLScenarioPlayerRuntime::CompleteSkillReload(CBasePlayerWeapon& weapon,
    csol::scenario::ConfigError& error)
{
    auto* player = weapon.m_pPlayer;
    if (!player) return true;
    const EHANDLE handle(player), weaponHandle(&weapon);
    const auto state = Resolve(handle);
    if (!state || !gpGlobals) return true;
    const int slot = weapon.iItemSlot();
    if (Resolve(handle) != state || weaponHandle.Get<CBasePlayerWeapon>() != &weapon) return false;
    if (slot != PRIMARY_WEAPON_SLOT && slot != PISTOL_SLOT) return true;
    const float extra = SumStat(*state, gpGlobals->time, &CSOLScenarioStatBuff::extraClip);
    if (extra <= 0) return true;
    const int base = weapon.iMaxClip();
    if (Resolve(handle) != state || weaponHandle.Get<CBasePlayerWeapon>() != &weapon) return false;
    if (base <= 0) return true;
    const double add = std::trunc(static_cast<float>(base) * extra);
    const double total = static_cast<double>(weapon.m_iClip) + add;
    if (!std::isfinite(total) || total < INT_MIN || total > INT_MAX)
    { error.message = "character skill reload expansion is invalid"; return false; }
    weapon.m_iClip = static_cast<int>(total);
    return RefreshSkillAmmo(handle, false, false, error);
}

bool CCSOLScenarioPlayerRuntime::ActivateCharacterSkill(CSOLPlayerRecoveryCall& call,
    bool& activated, csol::scenario::ConfigError& error)
{
    activated = false;
    auto* player = call.Current();
    const auto state = player ? Resolve(EHANDLE(player)) : nullptr;
    if (!state) return false;
    const auto skill = std::dynamic_pointer_cast<CSOLScenarioControlledSkill>(state->activeSkill);
    if (!skill) return true; // Prepared unavailable selection has no usable skill.
    const bool result = skill->TryActivate(call.Runtime(), activated, error);
    if (!call.Current() || Resolve(state->player) != state || state->activeSkill != skill) return call.Cancel();
    return result;
}

bool CCSOLScenarioPlayerRuntime::UpdateCharacterSkill(CSOLPlayerRecoveryCall& call,
    csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    const auto state = player ? Resolve(EHANDLE(player)) : nullptr;
    if (!state) return false;
    if (state->updatingCharacterSkill) return true;
    state->updatingCharacterSkill = true;
    struct Scope { bool& value; ~Scope() { value = false; } } scope{state->updatingCharacterSkill};
    const auto current = [&] { return call.Current() && Resolve(state->player) == state; };
    const auto skill = std::dynamic_pointer_cast<CSOLScenarioControlledSkill>(state->activeSkill);
    if (skill && !skill->Update(call.Runtime(), error)) return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!player->IsAlive()) return current();
    if (!current()) return call.Cancel();
    // The action controller ticks every frame; player status uses the original
    // strict half-second gate and never catches up multiple healing pulses.
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "character skill status clock is invalid"; return false; }
    if (state->skillAmmoWeapon.Get<CBasePlayerItem>() != player->m_pActiveItem &&
        !RefreshSkillAmmo(state->player, true, false, error)) return current() ? false : call.Cancel();
    if (!(gpGlobals->time > state->nextSkillStatus)) return true;
    HealingHost healing(*this, state, m_session);
    bool healed{};
    if (!CSOL_UpdateScenarioSkillHealing(state->skillHealing, healing, healed, error))
        return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!RefreshSkillAmmo(state->player, false, false, error)) return current() ? false : call.Cancel();
    // Each StatBuff group has one schedule and one best recovery producer.
    // Copy selected values before calling real recovery, which may mutate buffs.
    std::map<int, std::pair<float, float>> recoveries;
    for (const auto& buff : state->buffs)
    {
        if (!buff.Active(gpGlobals->time)) continue;
        const auto found = recoveries.find(buff.group);
        if (found == recoveries.end() || (buff.periodicRecovery > 0 && buff.periodicInterval > 0 &&
            (found->second.second <= 0 || buff.periodicRecovery / buff.periodicInterval >
                found->second.first / found->second.second)))
            recoveries[buff.group] = {buff.periodicRecovery, buff.periodicInterval};
    }
    for (const auto& [group, recovery] : recoveries)
    {
        if (gpGlobals->time < state->nextBuffRecovery[group] || recovery.first <= 0) continue;
        CSOLMonsterDeathContext context;
        context.bytes.fill(0);
        context.bytes[4] = 1;
        context.bytes[18] = context.bytes[19] = context.bytes[21] = 1;
        bool applied{};
        if (!ApplyRecoveryContext(call, recovery.first, applied, context, error))
            return current() ? false : call.Cancel();
        if (!current()) return call.Cancel();
        state->nextBuffRecovery[group] = gpGlobals->time + recovery.second;
    }
    state->nextSkillStatus = gpGlobals->time + 0.5f;
    return true;
}
