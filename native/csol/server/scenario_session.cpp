// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_session.h"
#include "scenario_drops.h"

#include <utility>

namespace
{
struct SessionOperation
{
    bool& busy;
    explicit SessionOperation(bool& value) : busy(value) { busy = true; }
    ~SessionOperation() { busy = false; }
};
}

CCSOLScenarioSession::~CCSOLScenarioSession()
{
    if (m_weapons) m_weapons->Invalidate();
    if (m_hitRewards) m_hitRewards->Invalidate();
    if (m_equipment) m_equipment->Invalidate();
}

bool CCSOLScenarioSession::Fail(csol::scenario::ResourceError error)
{
    if (m_weapons) m_weapons->Invalidate();
    if (m_hitRewards) m_hitRewards->Invalidate();
    if (m_equipment) m_equipment->Invalidate();
    m_error = std::move(error);
    m_state = CSOLScenarioSessionState::Failed;
    return false;
}

bool CCSOLScenarioSession::Precache(std::string_view mapName, std::uint16_t difficulty,
    CSOLScenarioResourceScope scope)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Empty) return false;
    SessionOperation operation(m_busy);
    m_error = {};
    m_scope = scope;
    if (!m_map.BeginMap(mapName)) return Fail(m_map.Error());

    const csol::scenario::ResourceFileApi files{g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile};
    csol::scenario::ResourceError resourceError;
    // The original manager reads drops from the room value before the mode
    // captures its difficulty parameter table. Both use this same selection.
    if (!m_dropItems.Load(difficulty, files, resourceError))
        return Fail(std::move(resourceError));
    if (!m_difficulty.BeginMap(mapName, difficulty, files, resourceError))
        return Fail(std::move(resourceError));
    // Row zero is the table header, not a selectable gameplay difficulty.
    // Map-specific menu limits remain the responsibility of the rules policy.
    if (!difficulty || difficulty >= m_difficulty.Table()->source.records.size())
        return Fail({std::string(m_difficulty.SourcePath()), {0, "selected scenario difficulty row is unavailable"}});
    if (!m_upgrades.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_equipmentResources.Load(files, resourceError)) return Fail(std::move(resourceError));
    m_weapons = std::make_shared<CCSOLScenarioWeaponState>();
    if (!m_weapons->Load(files, resourceError)) return Fail(std::move(resourceError));

    if (!m_messages.Register())
        return Fail({"", {0, "scenario MPToCL message registration failed"}});
    // Selection uses the real map tables, current spawn group and character
    // upgrade data. AI models/NAV/pool belong to combat startup, and may only
    // be precached during a map load with actual combat services installed.
    if (m_scope == CSOLScenarioResourceScope::Selection)
    {
        m_state = CSOLScenarioSessionState::Precached;
        return true;
    }
    csol::scenario::ConfigError error;
    if (!m_models.Precache(m_map.Tables()->monsterTypes, error))
        return Fail({"", std::move(error)});
    CCSOLScenarioDrops::Precache();
    std::optional<std::uint32_t> bspSize;
    if (g_engfuncs.pfnGetFileSize)
    {
        const auto path = "maps/" + std::string(mapName) + ".bsp";
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (size >= 0) bspSize = static_cast<std::uint32_t>(size);
    }
    m_navigation = CCSOLScenarioNavigation::Load(mapName, files, resourceError, bspSize,
        [](const char* name) -> Place
        {
            return TheBotPhrases ? TheBotPhrases->NameToID(name) : UNDEFINED_PLACE;
        });
    if (!m_navigation) return Fail(std::move(resourceError));
    // Every monster's initial Spawn calls Precache, so allocation belongs to
    // world loading, not the first round/Think after loading has finished.
    if (!m_monsters.Initialize())
        return Fail({"", {0, m_monsters.Error()}});

    m_state = CSOLScenarioSessionState::Precached;
    return true;
}

bool CCSOLScenarioSession::Activate()
{
    if (m_busy) return false;
    if (m_state == CSOLScenarioSessionState::Active) return true;
    if (m_state != CSOLScenarioSessionState::Precached) return false;
    SessionOperation operation(m_busy);
    if (!m_map.CollectEntities()) return Fail(m_map.Error());
    csol::scenario::ConfigError wallError;
    if (!m_walls.Collect(wallError)) return Fail({"", std::move(wallError)});
    csol::scenario::ConfigError navigationError;
    const auto navigation = m_navigation;
    if (m_scope == CSOLScenarioResourceScope::Combat &&
        (!navigation || !navigation->BuildLadders(navigationError)))
        return Fail({"", navigation ? std::move(navigationError) :
            csol::scenario::ConfigError{0, "scenario navigation owner is unavailable"}});
    m_state = CSOLScenarioSessionState::Active;
    m_error = {};
    return true;
}

bool CCSOLScenarioSession::Deactivate()
{
    if (m_busy) return false;
    SessionOperation operation(m_busy);
    // Shutdown detaches the behavior/owner pointers before engine removals.
    // Do not clear the table first: behaviors still borrow its type records.
    if (m_navigation) m_navigation->Invalidate();
    if (m_weapons) m_weapons->Invalidate();
    m_weapons.reset();
    if (m_hitRewards) m_hitRewards->Invalidate();
    m_hitRewards.reset();
    if (m_equipment) m_equipment->Invalidate();
    m_equipment.reset();
    if (m_players) m_players->Clear();
    m_players.reset();
    m_upgradeServices.reset();
    m_walls.Reset();
    m_wallRewards.reset();
    if (!m_monsters.Shutdown() || !m_monsters.ForgetMap())
        return Fail({"", {0, "scenario monster pool is busy during map shutdown"}});
    if (!m_map.EndMap())
        return Fail({"", {0, "scenario map is busy during shutdown"}});
    m_navigation.reset();
    if (!m_models.Reset())
        return Fail({"", {0, "scenario model cache is busy during shutdown"}});
    if (!m_difficulty.Clear() || !m_dropItems.Clear() || !m_upgrades.Clear() || !m_equipmentResources.Clear())
        return Fail({"", {0, "scenario difficulty resources are busy during shutdown"}});
    m_messages = {};
    m_error = {};
    m_state = CSOLScenarioSessionState::Empty;
    return true;
}

bool CCSOLScenarioSession::BindWallPlayers(std::shared_ptr<CCSOLScenarioWallPlayers> players)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active || !m_map.Tables()) return false;
    SessionOperation operation(m_busy);
    csol::scenario::ConfigError error;
    auto rules = CCSOLScenarioWallRewards::Create(m_map.Tables()->rounds, m_difficulty, std::move(players), error);
    if (!rules) return Fail({"", std::move(error)});
    if (!m_walls.BindRules(rules)) return Fail({"", {0, "scenario wall rules could not be bound"}});
    m_wallRewards = std::move(rules);
    m_error = {};
    return true;
}

bool CCSOLScenarioSession::BindUpgradeServices(std::shared_ptr<CCSOLScenarioUpgradeServices> services)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active || !services || !m_upgrades.Snapshot()) return false;
    csol::scenario::ConfigError error;
    float ratio{};
    if (!m_difficulty.Parameter(15, 0x12, ratio, error)) return Fail({"", std::move(error)});
    auto players = std::make_shared<CCSOLScenarioPlayers>();
    auto rewards = CCSOLMonsterHitRewards::Create(m_difficulty, players, error);
    if (!rewards) return Fail({"", std::move(error)});
    if (!BindWallPlayers(players)) return false;
    if (m_hitRewards) m_hitRewards->Invalidate();
    if (m_players) m_players->Clear();
    if (m_equipment) m_equipment->Invalidate();
    m_equipment.reset();
    m_players = std::move(players);
    m_hitRewards = std::move(rewards);
    m_upgradeServices = std::move(services);
    m_experienceRatio = ratio;
    return true;
}

bool CCSOLScenarioSession::BindEquipmentServices(CCSOLScenarioEquipment::CapEvent capEvent)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active || !capEvent || !m_equipmentResources.Snapshot()) return false;
    auto equipment = std::make_shared<CCSOLScenarioEquipment>(m_equipmentResources.Snapshot(), std::move(capEvent));
    if (!BindUpgradeServices(equipment)) return false;
    m_equipment = std::move(equipment);
    return true;
}

bool CCSOLScenarioSession::PrepareEquippedPlayer(CBasePlayer& player, int classId,
    std::uint8_t feature, bool elevenLevelLimit, csol::scenario::EquipmentLoadout loadout)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active || !m_equipment) return false;
    const auto equipment = m_equipment;
    const EHANDLE handle(&player);
    if (!equipment->BindPlayer(player, classId, std::move(loadout))) return false;
    auto* current = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    if (!current || m_state != CSOLScenarioSessionState::Active || m_equipment != equipment || !equipment->Current()) return false;
    return PreparePlayer(*current, classId, feature, elevenLevelLimit);
}

bool CCSOLScenarioSession::PreparePlayer(CBasePlayer& player, int classId, std::uint8_t feature, bool elevenLevelLimit)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active || !m_players || !m_upgradeServices) return false;
    SessionOperation operation(m_busy);
    const EHANDLE handle(&player);
    auto state = m_players->Upgrades(handle);
    if (!state)
    {
        state = CCSOLScenarioPlayerUpgrades::Create(player, m_upgrades.Snapshot(), m_upgradeServices,
            m_messages, m_experienceRatio, ++m_nextPlayerGeneration);
        if (!state || !m_players->Bind(state)) return Fail({"", {0, "scenario player state could not be bound"}});
    }
    if (!state->Configure(classId, feature, elevenLevelLimit)) return Fail({"", state->Error()});
    return true;
}

void CCSOLScenarioSession::RemovePlayer(const EHANDLE& player)
{
    if (m_equipment) m_equipment->RemovePlayer(player);
    if (m_players) m_players->Remove(player);
}

bool CCSOLScenarioSession::BeginRound(std::size_t roundIndex, float now, float freezeTime,
    csol::scenario::TriggerWaveEvents& events)
{
    if (m_busy || m_state != CSOLScenarioSessionState::Active) return false;
    if (m_scope != CSOLScenarioResourceScope::Combat)
        return Fail({"", {0, "scenario combat resources were not prepared for this map"}});
    SessionOperation operation(m_busy);
    if (!m_map.Tables() || roundIndex >= m_map.Tables()->rounds.rows.size())
        return Fail({"", {0, "scenario session round index is outside its table"}});
    m_walls.CancelPending();
    if (!m_map.ApplyDifficulty(m_difficulty) || !m_map.BeginRound(roundIndex, now, freezeTime, events))
        return Fail(m_map.Error());
    m_error = {};
    return true;
}
