// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_active_skill.h"
#include "scenario_monster_damage.h"
#include "scenario_death_owner.h"
#include "../scenario_abilities.hpp"
#include <algorithm>
#include <cmath>

namespace
{
class CharacterSkill final : public CSOLScenarioDamageSkill
{
public:
    CharacterSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
        const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
        const csol::scenario::ClassModDefinition& definition, float now)
        : m_players(players), m_state(state), m_player(state->player),
          m_generation(state->generation), m_type(definition.subtype),
          m_cooldown(definition.values[0]), m_duration(definition.values[1]),
          m_readyAt(now + m_cooldown) {}

    int Type() const override { return m_type; }
    bool HudSnapshot(float now, csol::abilities::Snapshot& output) const override
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        if (!players || !players->Active() || !state || state->generation != m_generation ||
            players->Find(m_player) != state || !std::isfinite(now) || m_cooldown < 0) return false;
        output = {static_cast<unsigned>(m_type), m_cooldown,
            std::clamp(m_cooldown - (m_readyAt - now), 0.0f, m_cooldown), 0};
        return true;
    }
    bool Ready(CSOLMonsterDeathCall&, bool& ready, csol::scenario::ConfigError& error) override
    {
        ready = false;
        // Neither supported type is the knife-kill type10 consumer. Keep the
        // unimplemented activation gates explicit instead of fabricating Ready.
        return Unsupported(error);
    }
    bool Activate(CSOLMonsterDeathCall&, csol::scenario::ConfigError& error) override
    { return Unsupported(error); }
    bool ActiveForMonsterHit(CSOLMonsterDamageCall& call, bool& active,
        csol::scenario::ConfigError&) override
    {
        active = false;
        if (!Current(call)) return call.Cancel();
        if (!gpGlobals) return false;
        active = m_activeUntil > gpGlobals->time;
        return true;
    }
    bool ActiveMonsterHit(CSOLMonsterDamageCall&, csol::scenario::ConfigError& error) override
    { return Unsupported(error); } // Only type12 uses this distinct virtual.
    bool AutoActivateOnBossDamage(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError&) override
    { enabled = false; return Current(call) || call.Cancel(); } // v14=105FFBE0.
    bool ActivateOnBossDamage(CSOLMonsterDamageCall&, csol::scenario::ConfigError& error) override
    { return Unsupported(error); }
    bool BossDamaged(CSOLMonsterDamageCall& call, float amount,
        csol::scenario::ConfigError& error) override
    { return MonsterDamaged(call, amount, error); } // type29 v20 delegates v24.
    bool MonsterDamaged(CSOLMonsterDamageCall& call, float,
        csol::scenario::ConfigError& error) override
    {
        if (!Current(call)) return call.Cancel();
        // Heal v20/v24 are the verified 10617DA0 ret16. Type29 instead stops
        // cloaking key12 with its own source/context gate. Stop owns v104.
        if (m_type == 1) return true;
        const auto players = m_players.lock();
        return players && players->StopCharacterCloaking(call, this, error);
    }
    bool UsesDamageProgress(CSOLMonsterDamageCall& call, bool& enabled,
        csol::scenario::ConfigError&) override
    { enabled = false; return Current(call) || call.Cancel(); } // v1C=105FFBE0.
    bool AdvanceDamageProgress(CSOLMonsterDamageCall&, csol::scenario::ConfigError& error) override
    { return Unsupported(error); }

private:
    bool Current(CSOLMonsterDamageCall& call) const
    {
        const auto players = m_players.lock();
        const auto state = m_state.lock();
        return players && players->Active() && state && state->generation == m_generation &&
            players->Find(m_player) == state && call.Current() && call.Player() &&
            call.Player() == m_player.Get<CBasePlayer>();
    }
    static bool Unsupported(csol::scenario::ConfigError& error)
    { error.message = "scenario character skill activation effects are not implemented"; return false; }
    std::weak_ptr<CCSOLScenarioPlayerRuntime> m_players;
    std::weak_ptr<CSOLScenarioPlayerRuntimeState> m_state;
    EHANDLE m_player;
    std::uint64_t m_generation{};
    int m_type{};
    // Original constructor/106E3380 state. ActiveUntil starts at -1; ordinary
    // damage never consumes or resets these timers.
    float m_cooldown{}, m_duration{}, m_readyAt{}, m_activeUntil{-1};
};
}

std::shared_ptr<CSOLScenarioActiveSkill> CSOL_CreateScenarioActiveSkill(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const csol::scenario::ClassModDefinition& definition,
    float now, csol::scenario::ConfigError& error)
{
    if (definition.type != 10 || (definition.subtype != 1 && definition.subtype != 29)) return {};
    if (!players || !state || !players->Active() || players->Find(state->player) != state ||
        !std::isfinite(now) || !std::isfinite(definition.values[0]) ||
        !std::isfinite(definition.values[1]) || !std::isfinite(now + definition.values[0]))
    { error.message = "scenario character skill initialization is invalid"; return {}; }
    return std::make_shared<CharacterSkill>(players, state, definition, now);
}
