// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../scenario_player_runtime.h"
#include "../scenario_runtime.h"
#include "../scenario_entity_lookup.h"
#include "../game_rules_gate.h"
#include "host_notifications.hpp"
#include "area_expanded.hpp"
#include "effects.hpp"
#include <algorithm>
#include <cmath>

namespace
{
// One reusable attachment scan for a server frame, shared by all cloaked
// players. Never perform maxEntities work once per affected player.
struct FollowCache
{
    std::weak_ptr<const CSOLScenarioEntityAccess> access;
    float time{-1};
    std::vector<EHANDLE> followers;
} followCache;

template<typename Check, typename Render>
bool RenderFollowers(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    CBasePlayer& player, Check&& current, Render&& render)
{
    auto* ownerEdict = player.edict();
    if (followCache.access.lock() != access || followCache.time != gpGlobals->time)
    {
        followCache.access = access;
        followCache.time = gpGlobals->time;
        followCache.followers.clear();
        for (int index = gpGlobals->maxClients + 1; index < gpGlobals->maxEntities; ++index)
        {
            auto* entity = INDEXENT(index);
            if (!current()) { followCache.time = -1; return false; }
            if (entity && !entity->free && entity->v.aiment && entity->v.movetype == MOVETYPE_FOLLOW)
                followCache.followers.emplace_back(entity);
        }
    }
    for (const auto& handle : followCache.followers)
        if (auto* entity = handle.Get(); entity && entity->v.aiment == ownerEdict && entity->v.movetype == MOVETYPE_FOLLOW)
            render(entity->v);
    return current();
}
}

bool CCSOLScenarioPlayerRuntime::ReleaseSkillCloakRender(const EHANDLE& handle)
{
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player) return false;
    if (!state->skillCloakRenderHeld) return true;
    const auto life = state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state && handle.Get<CBasePlayer>() == player &&
        state->skillLifeSerial == life; };
    const auto render = [](entvars_t& vars)
    {
        vars.rendermode = kRenderNormal; vars.renderfx = kRenderFxNone;
        vars.renderamt = 0; vars.rendercolor = Vector(0, 0, 0);
    };
    render(*player->pev);
    if (!gpGlobals || !RenderFollowers(m_access, *player, current, render)) return false;
    state->skillCloakRenderHeld = false;
    return true;
}

bool CCSOLScenarioPlayerRuntime::StartCloakSkill(const EHANDLE& handle, int key,
    std::uint64_t identity, csol::scenario::ConfigError& error, int subtype)
{
    const auto state = Resolve(handle);
    if (!state || !gpGlobals || !identity || (key != 11 && key != 12 && key != 13 && key != 14) ||
        (key == 11 && subtype != 22 && subtype != 23) || (key == 14 && subtype != 43))
    { error.message = "character cloaking effect has invalid ownership"; return false; }
    if (!state->displayAbilities) state->displayAbilities = std::make_shared<CSOLScenarioDisplayAbilities>();
    if (!state->displayAbilities->feature3) state->displayAbilities->feature3 = std::make_shared<csol::scenario::Feature3>();
    const auto abilities = state->displayAbilities;
    const auto feature = abilities->feature3;
    const auto life = state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state && state->skillLifeSerial == life &&
        state->displayAbilities == abilities && abilities->feature3 == feature && feature->Valid(); };
    const float now = gpGlobals->time;
    // These feature durations are independent of ClassMod value2.
    const float hold = key == 11 || key == 14 ? 8.0f : key == 12 ? 7.0f : 5.0f;
    csol::scenario::Feature3Record record;
    record.timing = {0.2f, 0.2f, now, now + ((0.2f + hold) + 0.2f)};
    record.intermediateDuration = hold;
    if (!feature->Put(key, std::move(record)))
    { error.message = "character cloaking feature could not be installed"; return false; }
    if (const auto old = state->skillCloaks.find(key); old != state->skillCloaks.end())
        RemoveSkillLabel(handle, old->second.label);
    CSOLScenarioSkillCloak effect;
    effect.identity = identity;
    effect.subtype = subtype;
    effect.opacity = 50;
    effect.endingSpeed = key == 12 ? 110 : 0;
    effect.endingSpeedDuration = key == 12 ? 7 : 0;
    effect.recoveryFraction = key == 12 ? 0.3f : 0;
    effect.recoveryRadius = (key == 12 ? 5.0f : 8.0f) * 39.369998931884766f;
    effect.label = InstallSkillLabel(handle, 10, 0, (0.2f + hold) + 0.2f);
    if (!effect.label) { error.message = "character cloaking label could not be installed"; return false; }
    state->skillCloaks.insert_or_assign(key, effect);
    if (!NotifySkillCloaking(handle, true, handle, error) || !current()) return false;
    // SmokeZone supplies protection only while the player is in its area;
    // the eight-second key11 cloak does not install invulnerability.
    if (key == 11) return true;
    CSOLScenarioStatBuff buff;
    buff.group = 3;
    buff.skillIdentity = identity;
    buff.source = 10;
    buff.subsource = key == 12 ? 29 : key == 14 ? 43 : 39;
    buff.expires = now + hold;
    buff.protection = true;
    buff.mask = key == 12 ? 1 : 0;
    buff.periodicRecovery = key == 12 ? 50 : 0;
    buff.periodicInterval = key == 12 ? 1 : 0;
    effect.buff = InstallStatBuff(handle, std::move(buff));
    if (!effect.buff)
    { error.message = "character cloaking status could not be installed"; return false; }
    if ((key == 13 || key == 14) && (!HoldSkillProtectionRender(handle, effect.buff, error) || !current())) return false;
    const auto installed = state->skillCloaks.find(key);
    if (installed == state->skillCloaks.end() || installed->second.identity != identity) return false;
    installed->second.buff = effect.buff;
    return current();
}

bool CCSOLScenarioPlayerRuntime::NotifySkillCloaking(const EHANDLE& handle, bool started,
    const EHANDLE& source, csol::scenario::ConfigError& error)
{
    const auto state = Resolve(handle);
    const auto runtime = CSOL_ScenarioRuntimeForAccess(m_access.get());
    if (!state || !runtime) return false;
    CSOLPlayerRecoveryCall call(runtime, handle, nullptr, nullptr, source);
    if (!call.Current()) return false;
    if (m_componentDispatchDepth >= 32)
    { error.message = "scenario nested cloaking notification limit exceeded"; return false; }
    ++m_componentDispatchDepth;
    struct Scope { std::size_t& depth; ~Scope() { --depth; } } scope{m_componentDispatchDepth};
    std::uint64_t previous{};
    std::size_t count{};
    for (;;)
    {
        const auto next = std::find_if(state->killComponents.begin(), state->killComponents.end(),
            [previous](const auto& entry) { return entry.token > previous; });
        if (next == state->killComponents.end()) return call.Current() != nullptr;
        previous = next->token;
        if (!next->enabled) continue;
        if (++count > 4096)
        { error.message = "scenario cloaking notification registration limit exceeded"; return false; }
        bool success{};
        {
            const auto component = next->component;
            auto* observer = dynamic_cast<CSOLScenarioTimedCloakingObserver*>(component.get());
            if (!observer)
            { error.message = "scenario player module has no timed cloaking notification implementation"; return false; }
            success = started ? observer->CloakingStarted(call, source, error) : observer->CloakingEnded(call, error);
        }
        if (!call.Current()) return call.Cancel();
        if (!success || !error.message.empty()) return false;
    }
}

bool CCSOLScenarioPlayerRuntime::UpdateSkillCloaks(CSOLPlayerRecoveryCall& call,
    csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    const auto state = player ? Resolve(EHANDLE(player)) : nullptr;
    if (!state || !gpGlobals) return false;
    if (state->skillCloaks.empty() && !state->skillCloakRenderHeld) return true;
    const auto abilities = state->displayAbilities;
    const auto feature = abilities ? abilities->feature3 : nullptr;
    if (!feature)
    { error.message = "character cloaking render owner is unavailable"; return false; }
    const auto current = [&]
    { return call.Current() && Resolve(state->player) == state && state->displayAbilities == abilities &&
        abilities->feature3 == feature && feature->Valid(); };
    const csol::scenario::Feature3Query query{
        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current,
        [](const void*, float& now) { if (!gpGlobals) return false; now = gpGlobals->time; return true; }, nullptr};
    float opacity = 255;
    bool active{};
    std::optional<int> next;
    if (!state->skillCloaks.empty()) next = state->skillCloaks.begin()->first;
    while (next)
    {
        const int key = *next;
        auto found = state->skillCloaks.find(key);
        if (found == state->skillCloaks.end()) return call.Cancel();
        const auto effect = found->second;
        int category{};
        if (!feature->Classify(key, query, category, error) || !current()) return call.Cancel();
        csol::scenario::Feature3Timing timing;
        if (!feature->ReadTiming(key, timing)) return call.Cancel();
        // Preserve phase order across a long frame. The key12 callback has
        // verified empty phases0..2 and performs its effects only in phase3.
        for (int phase = 0; phase <= category; ++phase)
        {
            found = state->skillCloaks.find(key);
            if (found == state->skillCloaks.end() || found->second.identity != effect.identity) return call.Cancel();
            if (found->second.phases & (1u << phase)) continue;
            if (phase == 3 && key == 11)
            {
                if (!csol::scenario::skills::EndSmokeCloakSkill(effect.subtype,
                    shared_from_this(), state, m_session, error) || !current())
                    return current() ? false : call.Cancel();
            }
            else if (phase == 3)
            {
                std::vector<std::uint64_t> retired;
                for (const auto& buff : state->buffs)
                    if (buff.skillIdentity == effect.identity) retired.push_back(buff.token);
                for (auto token : retired)
                    if (!RemoveStatBuff(state->player, token) || !current()) return current() ? false : call.Cancel();
                if (!current()) return call.Cancel();
                if (key == 12)
                {
                    CSOLScenarioStatBuff speed;
                    speed.group = 3; speed.source = 10; speed.subsource = 29;
                    speed.expires = gpGlobals->time + effect.endingSpeedDuration;
                    speed.speed = effect.endingSpeed;
                    if (!InstallStatBuff(state->player, std::move(speed)))
                    { error.message = "character cloak ending speed could not be installed"; return false; }
                    player->ResetMaxSpeed();
                    if (!current()) return call.Cancel();
                }
                const Vector origin = player->pev->origin;
                // Key14's original callback only removes its tagged buffs;
                // the independent v4C heals the caster when the skill ends.
                for (int index = 1; key != 14 && index <= gpGlobals->maxClients; ++index)
                {
                    auto* entity = CSOL_ScenarioPlayerEntity(index, m_access);
                    if (!current()) return call.Cancel();
                    auto* target = dynamic_cast<CBasePlayer*>(entity);
                    if (!target || target->has_disconnected || (target->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
                    const EHANDLE targetHandle(target);
                    const auto targetState = Find(targetHandle);
                    const auto targetLife = targetState ? targetState->skillLifeSerial : 0;
                    const bool alive = target->IsAlive();
                    if (!current() || targetHandle.Get<CBasePlayer>() != target || Find(targetHandle) != targetState ||
                        (targetState && targetState->skillLifeSerial != targetLife)) return call.Cancel();
                    if (!alive) continue;
                    const auto delta = target->pev->origin - (key == 13 ? origin : player->pev->origin);
                    const float distance = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
                    if (distance > effect.recoveryRadius * effect.recoveryRadius) continue;
                    if (key == 13)
                    {
                        CSOLScenarioStatBuff attack;
                        attack.group = 3; attack.source = 10; attack.subsource = 39;
                        attack.expires = gpGlobals->time + 4;
                        attack.attackRatio = target == player ? 0.3f : 0.25f;
                        const auto token = InstallStatBuff(targetHandle, std::move(attack));
                        if (!token || !current()) return current() ? false : call.Cancel();
                        if (!HoldSkillProtectionRender(targetHandle, token, error) || !current()) return current() ? false : call.Cancel();
                        if (targetHandle.Get<CBasePlayer>() != target || Find(targetHandle) != targetState ||
                            (targetState && targetState->skillLifeSerial != targetLife)) return call.Cancel();
                        if (!csol::scenario::skills::PublishStatusDuration(*m_session, targetHandle, 28,
                            4, {255,255,255}, error) || !current()) return current() ? false : call.Cancel();
                        continue;
                    }
                    CSOLPlayerRecoveryCall recovery(call.Runtime(), targetHandle,
                        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current,
                        state->player);
                    bool applied{};
                    if (!ApplyRecovery(recovery, effect.recoveryFraction * target->pev->max_health, applied, error))
                        return current() ? false : call.Cancel();
                    if (!current()) return call.Cancel();
                }
                if (key == 13 && (!csol::scenario::skills::EndCloak39MonsterDamage(
                    shared_from_this(), state, m_session, origin, error) || !current()))
                    return current() ? false : call.Cancel();
            }
            found = state->skillCloaks.find(key);
            if (found == state->skillCloaks.end() || found->second.identity != effect.identity) return call.Cancel();
            found->second.phases |= static_cast<std::uint8_t>(1u << phase);
        }
        if (category == 3)
        {
            if (!RemoveSkillLabel(state->player, effect.label)) return call.Cancel();
            feature->Remove(key);
            state->skillCloaks.erase(key);
            if (!NotifySkillCloaking(state->player, false, state->player, error) || !current())
                return current() ? false : call.Cancel();
        }
        else
        {
            active = true;
            float candidate = effect.opacity;
            const float now = gpGlobals->time;
            if (category == 0 && timing.a > 0)
                candidate = 255 + (effect.opacity - 255) * std::clamp((now - timing.c) / timing.a, 0.0f, 1.0f);
            else if (category == 2 && timing.b > 0)
                candidate = effect.opacity + (255 - effect.opacity) * std::clamp((now - (timing.d - timing.b)) / timing.b, 0.0f, 1.0f);
            opacity = std::min(opacity, candidate);
        }
        const auto successor = state->skillCloaks.upper_bound(key);
        next = successor == state->skillCloaks.end() ? std::nullopt : std::optional<int>(successor->first);
    }
    if (!current()) return call.Cancel();
    // Player's merged p-model shares these engine render fields. Independent
    // follow entities are processed from the actual SDK owner relation below.
    const auto render = [&](entvars_t& vars)
    {
        vars.rendermode = active ? kRenderTransTexture : kRenderNormal;
        vars.renderfx = kRenderFxNone;
        vars.renderamt = active ? opacity : 0;
        vars.rendercolor = Vector(0, 0, 0);
    };
    render(*player->pev);
    state->skillCloakRenderHeld = active;
    return RenderFollowers(m_access, *player, current, render) || call.Cancel();
}
