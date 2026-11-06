// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wall_upgrade_services.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_entity_lookup.h"
#include "game_rules_gate.h"
#include <limits>
#include <cstring>

CCSOLScenarioWallUpgradeServices::CCSOLScenarioWallUpgradeServices(
    std::weak_ptr<CCSOLScenarioRuntime> runtime) : m_runtime(std::move(runtime))
{
    if (const auto owner = m_runtime.lock(); owner && owner->session)
        m_access = owner->session->Map().EntityAccess();
}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLScenarioWallUpgradeServices::Pin() const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->wallUpgradeServices.get() != this ||
        !runtime->session || !m_access || !m_access->Allowed() ||
        runtime->session->Map().EntityAccess() != m_access ||
        runtime->session->State() != CSOLScenarioSessionState::Active) return {};
    return runtime;
}

bool CCSOLScenarioWallUpgradeServices::Current() const { return bool(Pin()); }

bool CCSOLScenarioWallUpgradeServices::PlayerCurrent(const EHANDLE& handle) const
{
    if (!Current()) return false;
    const auto* player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    return player && !player->has_disconnected && !(player->pev->flags & (FL_KILLME | FL_DORMANT));
}

bool CCSOLScenarioWallUpgradeServices::ReadAccount(const EHANDLE& handle, int& account)
{
    if (!PlayerCurrent(handle)) return false;
    account = handle.Get<CBasePlayer>()->m_iAccount;
    return true;
}

bool CCSOLScenarioWallUpgradeServices::ReadEligibility(const EHANDLE& player,
    std::int16_t& ctWins, std::uint8_t& participation)
{
    const auto runtime = Pin();
    if (!runtime || !PlayerCurrent(player)) return false;
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(runtime->session.get()));
    auto* rules = dynamic_cast<CHalfLifeMultiplay*>(scope.Rules());
    if (!scope.Current() || !rules || !runtime->session->WallUpgradeParticipation(player, participation)) return false;
    if (!scope.Current() || Pin() != runtime || !PlayerCurrent(player)) return false;
    ctWins = rules->m_iNumCTWins;
    return true;
}

bool CCSOLScenarioWallUpgradeServices::WriteAccount(const EHANDLE& handle, int account)
{
    const auto runtime = Pin();
    if (!runtime || !PlayerCurrent(handle)) return false;
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(runtime->session.get()));
    if (!scope.Current()) return false;
    auto* player = handle.Get<CBasePlayer>();
    const auto delta = std::int64_t(account) - player->m_iAccount;
    if (delta < std::numeric_limits<int>::min() || delta > std::numeric_limits<int>::max()) return false;
    // Use the native account/ReAPI owner so its authoritative money HUD and
    // scenario cap remain consistent. The rule already made its second read.
    player->AddAccount(static_cast<int>(delta), RT_NONE, true);
    return scope.Current() && Pin() == runtime && PlayerCurrent(handle);
}

bool CCSOLScenarioWallUpgradeServices::LevelChanged(std::int8_t level)
{
    const auto runtime = Pin();
    if (!runtime) return false;
    const auto walls = runtime->session->WallRewards();
    if (!walls || !walls->EntityAccessAllowed() || walls->EntityAccess() != m_access) return false;
    walls->SetMonsterLevel(level);
    return true;
}

bool CCSOLScenarioWallUpgradeServices::PublishState(bool enabled, std::int8_t level)
{
    const auto runtime = Pin();
    if (!runtime) return false;
    const auto messages = runtime->session->Messages();
    const bool sent = messages.SendWallUpgradeState(enabled, level);
    return Pin() == runtime && sent;
}

bool CCSOLScenarioWallUpgradeServices::RecordServiceEvent(const EHANDLE& handle, int event)
{
    const auto runtime = Pin();
    if (!runtime || !PlayerCurrent(handle) || event != csol::wall_upgrade::ExternalEvent ||
        m_nextRecord == std::numeric_limits<std::uint64_t>::max()) return false;
    auto* player = handle.Get<CBasePlayer>();
    const int index = player->entindex();
    if (Pin() != runtime || !PlayerCurrent(handle)) return false;
    const int userid = GETPLAYERUSERID(player->edict());
    if (Pin() != runtime || !PlayerCurrent(handle) || index < 1 || index > 32 || userid <= 0) return false;
    CSOLWallUpgradeRecord record;
    record.sequence = m_nextRecord + 1;
    record.mapGeneration = runtime->Generation();
    record.roundGeneration = runtime->session->Map().RoundGeneration();
    record.playerIndex = index;
    record.userid = userid;
    record.event = event;
    const char* name = STRING(player->pev->netname);
    if (Pin() != runtime || !PlayerCurrent(handle) || !name) return false;
    record.playerName = name;
    m_records.push_back(std::move(record));
    ++m_nextRecord;
    return true;
}

bool CCSOLScenarioWallUpgradeServices::PublishUpgrader(const EHANDLE& handle)
{
    const auto runtime = Pin();
    if (!runtime || !PlayerCurrent(handle)) return false;
    const int index = handle.Get<CBasePlayer>()->entindex();
    if (Pin() != runtime || !PlayerCurrent(handle)) return false;
    const auto messages = runtime->session->Messages();
    const bool sent = messages.SendWallUpgrader(index);
    return Pin() == runtime && PlayerCurrent(handle) && sent;
}

bool CCSOLScenarioWallUpgradeServices::PublishDenied(const EHANDLE& handle, const char* token)
{
    const auto runtime = Pin();
    if (!runtime || !PlayerCurrent(handle) || !token || std::strcmp(token, csol::wall_upgrade::DeniedText)) return false;
    const auto messages = runtime->session->Messages();
    const bool sent = messages.SendWallUpgradeDenied(handle.Get());
    return Pin() == runtime && PlayerCurrent(handle) && sent;
}

bool CCSOLScenarioWallUpgradeServices::Indicator(const EHANDLE& handle, bool remove)
{
    const auto runtime = Pin();
    auto* machine = handle.Get<CBaseEntity>();
    if (!runtime || !machine || (machine->pev->flags & FL_KILLME)) return false;
    const auto round = runtime->session->Map().RoundGeneration();
    const auto current = [&] { return Pin() == runtime && handle.Get<CBaseEntity>() == machine &&
        !(machine->pev->flags & FL_KILLME) && runtime->session->Map().RoundGeneration() == round; };
    const int machineIndex = machine->entindex();
    if (!current() || !gpGlobals) return false;
    const int maxClients = gpGlobals->maxClients;
    const auto messages = runtime->session->Messages();
    for (int index = 1; index <= maxClients; ++index)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, m_access));
        if (!current()) return false;
        // Original removal11535050 keeps connected dormant recipients; the
        // create path11534E10 additionally tests IsDormant11409720.
        if (!player || player->has_disconnected || (player->pev->flags & FL_KILLME) ||
            (!remove && (player->pev->flags & FL_DORMANT))) continue;
        const EHANDLE recipient(player);
        const auto offset = OFFSET(player->edict());
        if (!current() || recipient.Get<CBasePlayer>() != player) return false;
        if (!offset) continue;
        const bool sent = messages.SendWallUpgradeIndicator(player->edict(), machineIndex, remove);
        if (!current() || recipient.Get<CBasePlayer>() != player || !sent) return false;
    }
    return current();
}

bool CCSOLScenarioWallUpgradeServices::ShowIndicator(const EHANDLE& machine) { return Indicator(machine, false); }
bool CCSOLScenarioWallUpgradeServices::RemoveIndicator(const EHANDLE& machine) { return Indicator(machine, true); }

void CCSOLScenarioWallUpgradeServices::Failed(const char* reason)
{
    if (Current()) CSOL_RejectGameRules(reason);
}
