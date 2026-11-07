// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario_active_skill.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_death_owner.h"
#include "../scenario/core/scenario_entity_lookup.h"
#include "host_notifications.hpp"
#include "host.hpp"
#include "area_expanded.hpp"
#include "../../scenario/core/periodic_damage.hpp"
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
        : m_players(players), m_state(std::move(state)), m_session(std::move(session)),
          m_life(m_state->skillLifeSerial) {}
    bool Current() const override
    { return m_players.Active() && m_players.Find(m_state->player) == m_state && m_state->skillLifeSerial == m_life; }
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
    std::uint64_t m_life{};
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

float CCSOLScenarioPlayerRuntime::SkillAbsoluteSpeed(const EHANDLE& player, float now) const
{
    const auto state = Resolve(player);
    if (!state) return 0;
    float speed{};
    for (const auto& buff : state->buffs)
        if (buff.Active(now) && buff.absoluteSpeed > speed) speed = buff.absoluteSpeed;
    return speed;
}

float CCSOLScenarioPlayerRuntime::SkillTemporaryHealth(const EHANDLE& handle, float now) const
{
    const auto state = Resolve(handle);
    return state && !(now > state->skillTemporaryHealthUntil) ? state->skillTemporaryHealth : 0;
}

bool CCSOLScenarioPlayerRuntime::InstallSkillTemporaryHealth(const EHANDLE& handle,
    float amount, float duration, csol::scenario::ConfigError& error)
{
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player || !gpGlobals || !std::isfinite(amount) || !std::isfinite(duration)) return false;
    const auto life = state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player &&
        state->skillLifeSerial == life; };
    float base{};
    const bool read = ReadUpgradeStat(handle, csol::scenario::UpgradeStat::Health, base);
    if (!current()) return false;
    if (!read)
    { error.message = "temporary character health has no authoritative base"; return false; }
    const float maximum = base + amount;
    if (!std::isfinite(maximum) || maximum < 1)
    { error.message = "temporary character health maximum is invalid"; return false; }
    const float health = player->pev->health + (maximum - player->pev->max_health);
    state->skillTemporaryHealth = amount;
    state->skillTemporaryHealthUntil = gpGlobals->time + duration;
    player->pev->max_health = maximum;
    player->pev->health = std::clamp(health, 1.0f, maximum);
    return true;
}

bool CCSOLScenarioPlayerRuntime::RefreshSkillAmmo(const EHANDLE& handle,
    bool expandClip, bool forcePublish, csol::scenario::ConfigError& error)
{
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player || !gpGlobals) return false;
    const auto life = state->skillLifeSerial;
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
    if (!weapon) return true;
    const EHANDLE weaponHandle(weapon);
    const auto current = [&]
    { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player && state->skillLifeSerial == life &&
        weaponHandle.Get<CBasePlayerWeapon>() == weapon && weapon->m_pPlayer == player && player->m_pActiveItem == weapon; };
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
    bool enabled = std::any_of(state->buffs.begin(), state->buffs.end(),
        [](const auto& buff) { return buff.Active(gpGlobals->time) && buff.infiniteAmmo; });
    const int oldReserve = player->m_rgAmmo[ammo];
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
    // Original 114D13D7 tests the pre-refresh reserve, even after restoration.
    if (oldReserve >= 30000) { player->m_rgAmmo[ammo] = 32767; enabled = true; }
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
    const auto life = state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player &&
        state->skillLifeSerial == life && weaponHandle.Get<CBasePlayerWeapon>() == &weapon &&
        weapon.m_pPlayer == player && player->m_pActiveItem == &weapon; };
    const int slot = weapon.iItemSlot();
    if (!current()) return false;
    if (slot != PRIMARY_WEAPON_SLOT && slot != PISTOL_SLOT) return true;
    const float extra = SumStat(*state, gpGlobals->time, &CSOLScenarioStatBuff::extraClip);
    if (extra <= 0) return true;
    const int base = weapon.iMaxClip();
    if (!current()) return false;
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

bool CCSOLScenarioPlayerRuntime::ResetCharacterSkillState(const EHANDLE& handle,
    csol::scenario::ConfigError& error)
{
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player) return true;
    if (state->retiringCharacterSkillEffects) return false;
    const auto oldLife = state->skillLifeSerial;
    const auto oldSkill = state->activeSkill;
    const bool retired = RetireCharacterSkillEffects(handle, error);
    if (Resolve(handle) != state || handle.Get<CBasePlayer>() != player ||
        state->skillLifeSerial != oldLife || state->activeSkill != oldSkill) return false;
    const auto life = ++state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player &&
        state->skillLifeSerial == life && state->activeSkill == oldSkill; };
    CancelCharacterAction(*player, false);
    if (!current()) return false;
    state->actionKnockbackBlocked = false;
    if (const auto skill = std::dynamic_pointer_cast<CSOLScenarioControlledSkill>(state->activeSkill))
        skill->CancelPendingEffects();
    if (!current()) return false;
    state->skillHealing = {};
    state->skillShield = {};
    if (state->skillTemporaryHealth != 0 && player->pev->health > 0 && player->pev->deadflag == DEAD_NO)
        if (!InstallSkillTemporaryHealth(handle, 0, 0, error) || !current()) return false;
    state->skillTemporaryHealth = 0;
    state->skillTemporaryHealthUntil = 0;
    state->nextBuffRecovery.clear();
    state->nextBuffDamage.clear();
    // The cast's cooldown remains with the same character object. Only this
    // life's target effects and pending animation are retired.
    auto retiredCloaks = std::move(state->skillCloaks);
    state->skillCloaks.clear();
    for (const auto& [key, cloak] : retiredCloaks)
    {
        RemoveSkillLabel(handle, cloak.label);
        if (!current()) return false;
        if (state->displayAbilities && state->displayAbilities->feature3)
            state->displayAbilities->feature3->Remove(key);
        if (!current()) return false;
    }
    if (!ReleaseSkillCloakRender(handle) || !current()) return false;
    for (std::size_t index = 0; index < state->buffs.size();)
    {
        if (state->buffs[index].source == 10)
        { if (!RemoveProtectionAt(*state, *player, index) || !current()) return false; }
        else ++index;
    }
    for (const auto& [index, ammo] : state->savedSkillAmmo)
        if (index >= 0 && index < MAX_AMMO_SLOTS) player->m_rgAmmo[index] = ammo;
    state->savedSkillAmmo.clear();
    state->expandedSkillWeapons.clear();
    state->skillAmmoWeapon = {};
    if (state->skillInfiniteAmmo)
    {
        state->skillInfiniteAmmo = false;
        if (!PublishInfiniteAmmo(*m_session, handle, false, error)) return false;
    }
    return current() && retired;
}

void CCSOLScenarioPlayerRuntime::DisableCharacterSkill(const EHANDLE& handle, const char* reason)
{
    const auto state = Resolve(handle);
    if (!state || state->disablingCharacterSkill) return;
    state->disablingCharacterSkill = true;
    struct Scope { bool& value; ~Scope() { value = false; } } scope{state->disablingCharacterSkill};
    csol::scenario::ConfigError error;
    error.message = reason && *reason ? reason : "character skill auxiliary effect failed";
    if (const auto skill = std::dynamic_pointer_cast<CSOLScenarioControlledSkill>(state->activeSkill))
        skill->Disable(error);
    else if (state->skillFailure.empty())
    {
        state->skillFailure = error.message;
        if (g_engfuncs.pfnAlertMessage)
            ALERT(at_console, "[CSOL] Character effects disabled: %s\n", state->skillFailure.c_str());
    }
    if (Resolve(handle) != state) return;
    error = {};
    // Retire this life's transient effects even if their optional message
    // transport failed. The failure must never become the map's persistent
    // error or interrupt a monster's already committed health/death path.
    ResetCharacterSkillState(handle, error);
}

bool CCSOLScenarioPlayerRuntime::UpdateCharacterSkill(CSOLPlayerRecoveryCall& call,
    csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    const auto state = player ? Resolve(EHANDLE(player)) : nullptr;
    if (!state) return false;
    if (state->updatingCharacterSkill || state->retiringCharacterSkillEffects) return true;
    state->updatingCharacterSkill = true;
    struct Scope { bool& value; ~Scope() { value = false; } } scope{state->updatingCharacterSkill};
    const auto activeSkill = state->activeSkill;
    const auto current = [&] { return call.Current() && Resolve(state->player) == state && state->activeSkill == activeSkill; };
    const auto skill = std::dynamic_pointer_cast<CSOLScenarioControlledSkill>(state->activeSkill);
    if (skill && !skill->Update(call.Runtime(), error)) return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!UpdateSkillCloaks(call, error)) return current() ? false : call.Cancel();
    if (!player->IsAlive()) return current();
    if (!current()) return call.Cancel();
    // Action and StatBuff callbacks run on each player update. The separate
    // healing/status path below retains its strict half-second gate.
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "character skill status clock is invalid"; return false; }
    if (state->skillTemporaryHealth != 0 && gpGlobals->time > state->skillTemporaryHealthUntil)
        if (!InstallSkillTemporaryHealth(state->player, 0, 0, error)) return false;
    std::vector<EnergyShield::Retired> retiredShields;
    state->skillShield.Update(gpGlobals->time, &retiredShields);
    if (!RetireSkillShields(state->player, retiredShields, error)) return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (state->skillAmmoWeapon.Get<CBasePlayerItem>() != player->m_pActiveItem &&
        !RefreshSkillAmmo(state->player, true, false, error)) return current() ? false : call.Cancel();
    if (!UpdatePeriodicBuffs(call, error)) return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!(gpGlobals->time > state->nextSkillStatus)) return true;
    HealingHost healing(*this, state, m_session);
    bool healed{};
    if (!CSOL_UpdateScenarioSkillHealing(state->skillHealing, healing, healed, error))
        return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!RefreshSkillAmmo(state->player, false, false, error)) return current() ? false : call.Cancel();
    state->nextSkillStatus = gpGlobals->time + 0.5f;
    return true;
}

bool CCSOLScenarioPlayerRuntime::UpdatePeriodicBuffs(CSOLPlayerRecoveryCall& call,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    auto* player = call.Current();
    const auto state = player ? Resolve(EHANDLE(player)) : nullptr;
    if (!state || state->retiringCharacterSkillEffects || !error.message.empty()) return false;
    const auto skill = state->activeSkill;
    const auto current = [&]
    { return call.Current() == player && Resolve(state->player) == state && state->activeSkill == skill; };
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "periodic stat buff clock is invalid"; return false; }

    //107D1E70 runs107D1FA0 before107D2160 on every player update. Each
    // ordered group has independent recovery and damage deadlines. Re-select
    // from the live list after callbacks; never retain a vector iterator or
    // invoke a removed producer. No allocations are needed for inactive buffs.
    const auto nextGroup = [&](std::optional<std::int32_t> previous)
    {
        return previous ? std::upper_bound(state->buffs.begin(), state->buffs.end(), *previous,
            [](std::int32_t group, const auto& buff) { return group < buff.group; }) : state->buffs.begin();
    };
    std::optional<std::int32_t> previous;
    for (;;)
    {
        if (!current()) return call.Cancel();
        const auto first = nextGroup(previous);
        if (first == state->buffs.end()) break;
        const auto group = first->group;
        previous = group;
        const auto deadline = state->nextBuffRecovery.find(group);
        if (deadline != state->nextBuffRecovery.end() && gpGlobals->time < deadline->second) continue;
        const CSOLScenarioStatBuff* best{};
        for (auto it = first; it != state->buffs.end() && it->group == group; ++it)
        {
            // This original producer selector uses expiry only, not the
            // independent condition callback used by some stat consumers.
            if (it->expires > 0 && gpGlobals->time >= it->expires) continue;
            if (!best || (it->periodicRecovery > 0 && it->periodicInterval > 0 &&
                (!(best->periodicRecovery > 0 && best->periodicInterval > 0) ||
                it->periodicRecovery / it->periodicInterval > best->periodicRecovery / best->periodicInterval)))
                best = &*it;
        }
        if (!best || best->periodicRecovery <= 0) continue;
        const float amount = best->periodicRecovery, interval = best->periodicInterval;
        CSOLMonsterDeathContext context;
        context.bytes.fill(0);
        context.bytes[4] = 1;
        context.bytes[18] = context.bytes[19] = context.bytes[21] = 1;
        bool applied{};
        if (!ApplyRecoveryContext(call, amount, applied, context, error))
            return current() ? false : call.Cancel();
        if (!current()) return call.Cancel();
        const float next = gpGlobals->time + interval;
        if (!std::isfinite(next))
        { error.message = "periodic stat buff recovery deadline is invalid"; return false; }
        state->nextBuffRecovery[group] = next;
    }
    previous.reset();
    for (;;)
    {
        if (!current()) return call.Cancel();
        const auto first = nextGroup(previous);
        if (first == state->buffs.end()) break;
        const auto group = first->group;
        previous = group;
        const auto deadline = state->nextBuffDamage.find(group);
        if (deadline != state->nextBuffDamage.end() && gpGlobals->time < deadline->second) continue;
        const auto last = std::upper_bound(first, state->buffs.end(), group,
            [](std::int32_t value, const auto& buff) { return value < buff.group; });
        csol::scenario::PeriodicDamage selected;
        if (!csol::scenario::SelectPeriodicDamage(
            std::span<const CSOLScenarioStatBuff>(&*first, static_cast<std::size_t>(last - first)),
            gpGlobals->time, selected))
        { error.message = "periodic stat buff damage producer is invalid"; return false; }
        if (!selected.enabled) continue;
        if (!ApplyPeriodicBuffAreaDamage(keepAlive, state, m_session, selected.radius, selected.amount, error))
            return current() ? false : call.Cancel();
        if (!current()) return call.Cancel();
        const float next = gpGlobals->time + selected.interval;
        if (!std::isfinite(next))
        { error.message = "periodic stat buff damage deadline is invalid"; return false; }
        state->nextBuffDamage[group] = next;
    }
    return true;
}
