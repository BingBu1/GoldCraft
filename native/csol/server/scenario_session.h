// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_map.h"
#include "scenario_messages.h"
#include "scenario_music.h"
#include "scenario_monster_models.h"
#include "scenario_monster_pool.h"
#include "scenario_navigation.h"
#include "scenario_breakable.h"
#include "scenario_wall_rewards.h"
#include "scenario_player_upgrades.h"
#include "scenario_equipment.h"
#include "scenario_monster_rewards.h"
#include "scenario_weapon_state.h"
#include "../scenario/difficulty.hpp"
#include "../scenario/drop_items.hpp"
#include "../scenario/upgrade_tables.hpp"
#include "../scenario/announce.hpp"
#include "../scenario/damage_display_modes.hpp"
#include "../scenario/boss_skills.hpp"

enum class CSOLScenarioSessionState { Empty, Precached, Active, Failed };
enum class CSOLScenarioResourceScope { Selection, Combat };

// Owned by the selected GameRules. Precache must run after g_pGameRules has
// been assigned, while the engine still permits model precaching. In
// particular, spawning the pool from the rules constructor would make
// DispatchSpawn call the previous rules through g_pGameRules.
class CCSOLScenarioSession
{
public:
    ~CCSOLScenarioSession();
    bool Precache(std::string_view mapName, std::uint16_t difficulty = 1,
        CSOLScenarioResourceScope scope = CSOLScenarioResourceScope::Combat);
    // Called after ServerActivate has populated the classname hash table.
    bool Activate();
    bool BindWallPlayers(std::shared_ptr<CCSOLScenarioWallPlayers> players);
    bool BindUpgradeServices(std::shared_ptr<CCSOLScenarioUpgradeServices> services);
    bool BindEquipmentServices(CCSOLScenarioEquipment::CapEvent capEvent);
    bool PrepareEquippedPlayer(CBasePlayer& player, int classId, std::uint8_t feature,
        bool elevenLevelLimit, csol::scenario::EquipmentLoadout loadout);
    bool PreparePlayer(CBasePlayer& player, int classId, std::uint8_t feature, bool elevenLevelLimit);
    void RemovePlayer(const EHANDLE& player);
    // Formal round startup after the rules have reset the previous round and
    // loaded champion types. Continuation uses its separate original path.
    bool BeginRound(std::size_t roundIndex, float now, float freezeTime,
        csol::scenario::TriggerWaveEvents& events);
    // Irrevocable engine notification. This never waits for a callback and
    // never clears borrowed tables. OnDestroy can still unregister entities.
    void RevokeEntityAccess() { m_map.RevokeEntityAccess(); }
    // Orderly cleanup while the map is live, or after the engine has finished
    // all OnDestroy callbacks. Busy owners retain their borrowed type records.
    bool Deactivate();
    bool Busy() const { return m_busy || m_map.Busy() || m_monsters.TeardownBusy() || m_walls.Busy(); }

    CSOLScenarioSessionState State() const { return m_state; }
    CSOLScenarioResourceScope ResourceScope() const { return m_scope; }
    const csol::scenario::ResourceError& Error() const { return m_error; }
    CCSOLScenarioMap& Map() { return m_map; }
    const CCSOLScenarioMap& Map() const { return m_map; }
    CCSOLMonsterPool& Monsters() { return m_monsters; }
    CCSOLMonsterModels& Models() { return m_models; }
    const CCSOLScenarioMessages& Messages() const { return m_messages; }
    std::shared_ptr<CCSOLScenarioMusic> Music() const { return m_music; }
    unsigned short SmokeEvent() const { return m_smokeEvent; }
    const csol::scenario::DifficultyResources& Difficulty() const { return m_difficulty; }
    const csol::scenario::DropItemTable& DropRules() const { return m_dropItems.Rules(); }
    std::shared_ptr<const CCSOLMonsterNavigation> Navigation() const { return m_navigation; }
    std::shared_ptr<const CCSOLScenarioNavigation> ScenarioNavigation() const { return m_navigation; }
    CCSOLScenarioWalls& Walls() { return m_walls; }
    std::shared_ptr<CCSOLScenarioWallRewards> WallRewards() const { return m_wallRewards; }
    std::shared_ptr<const csol::scenario::UpgradeTables> Upgrades() const { return m_upgrades.Snapshot(); }
    std::shared_ptr<const csol::scenario::EquipmentTables> Equipment() const { return m_equipmentResources.Snapshot(); }
    std::shared_ptr<const csol::scenario::ClassModTable> ClassMods() const { return m_classMods.Snapshot(); }
    std::shared_ptr<const csol::scenario::ClassModPresetTable> ClassModPresets() const { return m_classModPresets.Snapshot(); }
    std::shared_ptr<CCSOLScenarioPlayers> Players() const { return m_players; }
    std::shared_ptr<CCSOLScenarioEquipment> EquipmentState() const { return m_equipment; }
    std::shared_ptr<CCSOLMonsterHitRewards> HitRewards() const { return m_hitRewards; }
    std::shared_ptr<CCSOLScenarioWeaponState> Weapons() const { return m_weapons; }
    std::shared_ptr<const csol::scenario::AnnounceTable> Announcements() const { return m_announcements.Snapshot(); }
    std::shared_ptr<const csol::scenario::DamageDisplayModes> DamageDisplayModes() const { return m_damageDisplayModes.Snapshot(); }
    std::shared_ptr<csol::scenario::BossSkills> BossSkills() const { return m_bossSkills.Snapshot(); }

private:
    bool Fail(csol::scenario::ResourceError error);

    // The pool is destroyed first, before the map's borrowed type table.
    CCSOLScenarioMap m_map;
    csol::scenario::DifficultyResources m_difficulty;
    csol::scenario::DropItemResources m_dropItems;
    csol::scenario::UpgradeResources m_upgrades;
    csol::scenario::EquipmentResources m_equipmentResources;
    csol::scenario::ClassModResources m_classMods;
    csol::scenario::ClassModPresetResources m_classModPresets;
    csol::scenario::AnnounceResources m_announcements;
    csol::scenario::DamageDisplayModeResources m_damageDisplayModes;
    csol::scenario::BossSkillResources m_bossSkills;
    CCSOLMonsterModels m_models;
    CCSOLScenarioMessages m_messages;
    std::shared_ptr<CCSOLScenarioMusic> m_music;
    unsigned short m_smokeEvent{};
    std::shared_ptr<CCSOLScenarioNavigation> m_navigation;
    CCSOLScenarioWalls m_walls;
    std::shared_ptr<CCSOLScenarioWallRewards> m_wallRewards;
    std::shared_ptr<CCSOLScenarioPlayers> m_players;
    std::shared_ptr<CCSOLScenarioUpgradeServices> m_upgradeServices;
    std::shared_ptr<CCSOLScenarioEquipment> m_equipment;
    std::shared_ptr<CCSOLMonsterHitRewards> m_hitRewards;
    std::shared_ptr<CCSOLScenarioWeaponState> m_weapons;
    std::uint64_t m_nextPlayerGeneration{};
    float m_experienceRatio{};
    CCSOLMonsterPool m_monsters;
    csol::scenario::ResourceError m_error;
    CSOLScenarioSessionState m_state{CSOLScenarioSessionState::Empty};
    CSOLScenarioResourceScope m_scope{CSOLScenarioResourceScope::Combat};
    bool m_busy{};
};
