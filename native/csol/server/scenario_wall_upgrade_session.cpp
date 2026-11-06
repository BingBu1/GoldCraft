// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_session.h"
#include "scenario_entity_lookup.h"
#include "game_rules_gate.h"

bool CCSOLScenarioSession::BindWallUpgradeServices(std::shared_ptr<CCSOLWallUpgradeServices> services)
{
    if (Busy() || m_state != CSOLScenarioSessionState::Active || !m_map.EntityAccessAllowed() ||
        !services || m_wallUpgradeServices || m_wallUpgradeInitialized) return false;
    m_wallUpgradeServices = std::move(services);
    return true;
}

bool CCSOLScenarioSession::InitializeWallUpgradeMachine()
{
    if (m_wallUpgradeInitialized) return m_wallUpgrade && m_wallUpgrade->Current();
    if (!m_wallUpgradeServices || !m_map.EntityAccessAllowed()) return false;
    const auto access = m_map.EntityAccess();
    const auto services = m_wallUpgradeServices;
    const auto current = [&] { return access->Allowed() && m_map.EntityAccess() == access &&
        m_state == CSOLScenarioSessionState::Active && m_wallUpgradeServices == services; };
    // Original manager+278 is set before scanning/creating. Reentrant initial
    // HUD callbacks must not produce a second machine during DispatchSpawn.
    m_wallUpgradeInitialized = true;
    bool enabled{};
    CBaseEntity* previous{};
    for (;;)
    {
        const auto found = CSOL_LookupScenarioEntity(previous, "zbsbreak", access);
        if (!current()) return false;
        if (!found.valid) return Fail({"", {0, "scenario wall upgrade scan failed"}});
        if (!found.entity) break;
        auto* wall = dynamic_cast<CCSOLScenarioBreakable*>(found.entity);
        if (!wall || !wall->ValidConfiguration())
            return Fail({"", {0, "scenario wall upgrade encountered an invalid wall"}});
        if (wall->AllowsMonsterWallUpgrade()) { enabled = true; break; }
        previous = wall;
    }
    const auto marker = CSOL_LookupScenarioEntity(nullptr, "zbsbreak_dontupgrade", access);
    if (!current()) return false;
    if (!marker.valid) return Fail({"", {0, "scenario wall upgrade marker scan failed"}});
    if (marker.entity) enabled = false;
    auto state = std::make_shared<CCSOLScenarioWallUpgrade>(enabled, services, access);
    m_wallUpgrade = state;
    if (!enabled) return true;
    if (!m_wallRewards) return Fail({"", {0, "scenario wall upgrade requires wall damage rules"}});
    const auto name = ALLOC_STRING(csol::wall_upgrade::Classname);
    if (!current()) return false;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!current()) return false;
    const EHANDLE created(edict);
    const bool missing = FNullEnt(edict);
    if (!current()) return false;
    if (missing || created.Get() != edict)
        return Fail({"", {0, "scenario wall upgrade entity allocation failed"}});
    m_wallUpgradeMachine = created;
    edict->v.origin = Vector(csol::wall_upgrade::Origin.data());
    edict->v.angles = Vector(csol::wall_upgrade::Angles.data());
    const int spawned = DispatchSpawn(edict);
    if (!current()) return false;
    auto* machine = dynamic_cast<CCSOLScenarioWallUpgradeMachine*>(created.Get<CBaseEntity>());
    if (spawned < 0 || !machine || !machine->Spawned() || !machine->Bind(state))
        return Fail({"", {0, "scenario wall upgrade entity could not bind"}});
    UTIL_SetOrigin(machine->pev, machine->pev->origin);
    return current() && created.Get<CBaseEntity>() == machine && state->Current();
}

bool CCSOLScenarioSession::InitializeWallUpgradePlayer(CBasePlayer& player)
{
    if (Busy() || m_state != CSOLScenarioSessionState::Active || !m_map.EntityAccessAllowed() ||
        !m_wallUpgradeServices || player.has_disconnected) return false;
    const auto access = m_map.EntityAccess();
    const EHANDLE handle(&player);
    const auto current = [&] { return access->Allowed() && m_map.EntityAccess() == access &&
        m_state == CSOLScenarioSessionState::Active && handle.Get<CBasePlayer>() == &player &&
        !player.has_disconnected && !(player.pev->flags & (FL_KILLME | FL_DORMANT)); };
    const int index = player.entindex();
    if (!current() || index < 1 || index >= int(m_wallParticipants.size())) return false;
    const int userid = GETPLAYERUSERID(player.edict());
    if (!current() || userid <= 0) return false;
    struct BusyScope { bool& busy; ~BusyScope() { busy = false; } } scope{m_busy};
    m_busy = true;
    if (!InitializeWallUpgradeMachine() || !current()) return false;
    auto& participant = m_wallParticipants[index];
    if (participant.player.Get() != handle.Get() || participant.userid != userid)
        participant = {handle, userid, 0};
    const auto state = m_wallUpgrade;
    const bool sent = m_messages.SendWallUpgradeState(state->Enabled(), state->Level(), player.edict());
    if (!current() || m_wallUpgrade != state || !state->Current()) return false;
    return sent || Fail({"", {0, "scenario wall upgrade initial state could not be sent"}});
}

bool CCSOLScenarioSession::WallUpgradeParticipation(const EHANDLE& player, std::uint8_t& value) const
{
    if (!m_map.EntityAccessAllowed() || m_state != CSOLScenarioSessionState::Active) return false;
    auto* entity = player.Get<CBasePlayer>();
    if (!entity || entity->has_disconnected || (entity->pev->flags & (FL_KILLME | FL_DORMANT))) return false;
    const auto access = m_map.EntityAccess();
    const int userid = GETPLAYERUSERID(entity->edict());
    if (!access->Allowed() || m_map.EntityAccess() != access || player.Get<CBasePlayer>() != entity) return false;
    for (const auto& participant : m_wallParticipants)
        if (participant.player.Get() == player.Get() && participant.userid == userid && userid > 0)
        { value = participant.rounds; return true; }
    return false;
}

bool CCSOLScenarioSession::AdvanceWallUpgradeRound()
{
    if (Busy() || m_state != CSOLScenarioSessionState::Active || !m_map.EntityAccessAllowed()) return false;
    const auto* tables = m_map.Tables();
    if (!tables || !gpGlobals) return false;
    const auto round = m_map.RoundGeneration();
    if (m_wallParticipationRound == round) return true;
    const int subtype = tables->scenarioType;
    const auto access = m_map.EntityAccess();
    const auto current = [&] { return access->Allowed() && m_map.EntityAccess() == access &&
        m_map.RoundGeneration() == round && m_state == CSOLScenarioSessionState::Active; };
    struct BusyScope { bool& busy; ~BusyScope() { busy = false; } } scope{m_busy};
    m_busy = true;
    m_wallParticipationRound = round;
    for (int index = 1; index <= gpGlobals->maxClients && index < int(m_wallParticipants.size()); ++index)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
        if (!current()) return false;
        if (!player || player->has_disconnected || player->m_iTeam == SPECTATOR ||
            (player->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(player);
        const int userid = GETPLAYERUSERID(player->edict());
        if (!current() || handle.Get<CBasePlayer>() != player) return false;
        if (userid <= 0) continue;
        auto& participant = m_wallParticipants[index];
        // A valid client has normally run InitHUD. Preserve initial-join zero
        // for engine-created bots whose first HUD is deferred to their spawn.
        if (participant.player.Get() != handle.Get() || participant.userid != userid)
            participant = {handle, userid, 0};
        participant.rounds = csol::wall_upgrade::BeginRound(participant.rounds, subtype);
    }
    return current();
}

bool CCSOLScenarioSession::ResetWallUpgradeChallenge()
{
    m_wallParticipationRound.reset();
    for (auto& participant : m_wallParticipants) participant.rounds = 0;
    if (!m_wallUpgradeInitialized) return true;
    if (!m_wallUpgrade || !m_wallUpgradeServices || !m_map.EntityAccessAllowed()) return false;
    const auto previous = m_wallUpgrade;
    const auto replacement = std::make_shared<CCSOLScenarioWallUpgrade>(previous->Enabled(),
        m_wallUpgradeServices, m_map.EntityAccess());
    previous->Invalidate();
    m_wallUpgrade = replacement;
    if (replacement->Enabled())
    {
        auto* machine = dynamic_cast<CCSOLScenarioWallUpgradeMachine*>(m_wallUpgradeMachine.Get<CBaseEntity>());
        if (!machine || !machine->Bind(replacement))
            return Fail({"", {0, "scenario wall upgrade replay lost its machine"}});
    }
    if (m_wallRewards) m_wallRewards->SetMonsterLevel(replacement->Level());
    const bool sent = m_messages.SendWallUpgradeState(replacement->Enabled(), replacement->Level());
    if (!m_map.EntityAccessAllowed() || m_wallUpgrade != replacement || !replacement->Current()) return false;
    return sent || Fail({"", {0, "scenario wall upgrade replay state could not be sent"}});
}

void CCSOLScenarioSession::ClearWallUpgrade()
{
    if (m_wallUpgrade) m_wallUpgrade->Invalidate();
    m_wallUpgrade.reset();
    m_wallUpgradeServices.reset();
    m_wallUpgradeInitialized = false;
    m_wallParticipants = {};
    m_wallParticipationRound.reset();
    const auto handle = m_wallUpgradeMachine;
    m_wallUpgradeMachine = static_cast<CBaseEntity*>(nullptr);
    // Engine shutdown already owns removals after access was revoked.
    if (!m_map.EntityAccessAllowed()) return;
    auto* machine = handle.Get<CBaseEntity>();
    if (machine && !(machine->pev->flags & FL_KILLME)) UTIL_Remove(machine);
}
