// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_session.h"
#include "human_kick.h"
#include "../monsters/scenario_monster_skills.h"
#include "../monsters/scenario_zombie_bomb.h"
#include "../../../scenario/monsters/scenario_monster_effects.hpp"
#include "../../../scenario_part_effects.hpp"
#include "scenario_drops.h"
#include "../monsters/scenario_monster_resources.h"
#include "../../../scenario/bosses/oberon_hint.hpp"
#include "../bosses/scenario_oberon_resources.h"
#include "../../../scenario/core/status_icons.hpp"
#include "../../../scenario/core/scenario_kill.hpp"
#include "../../../scenario/core/scenario_healing.hpp"
#include "../../../scenario/core/scenario_smoke.hpp"
#include "../../../scenario/monsters/frozen_death.hpp"
#include "../../../scenario/monsters/frozen_smoke.hpp"
#include "../../../scenario/core/scenario_upgrades.hpp"
#include "../../../scenario/core/scenario_abilities.hpp"
#include "../../../scenario/core/buy_zone_hint.hpp"
#include "../../../scenario/bosses/boss_health.hpp"
#include "../../../scenario/core/scenario_round_hud.hpp"
#include "../../../scenario/core/scenario_respawn_resources.hpp"
#include "../../../scenario/core/dialogue_resources.hpp"
#include "../../../scenario/core/continuation.hpp"

#include <utility>
#include <set>
#include <cmath>

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
    if (m_wallUpgrade) m_wallUpgrade->Invalidate();
    if (m_weapons) m_weapons->Invalidate();
    if (m_hitRewards) m_hitRewards->Invalidate();
    if (m_equipment) m_equipment->Invalidate();
}

bool CCSOLScenarioSession::Fail(csol::scenario::ResourceError error)
{
    if (m_wallUpgrade) m_wallUpgrade->Invalidate();
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
    if (!m_monsters.BindEntityAccess(m_map.EntityAccess()))
        return Fail({"", {0, "scenario monster pool could not bind map access"}});
    m_walls.BindEntityAccess(m_map.EntityAccess());

    const csol::scenario::ResourceFileApi files{g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile};
    csol::scenario::ResourceError resourceError;
    // The original manager reads drops from the room value before the mode
    // captures its difficulty parameter table. Both use this same selection.
    if (!m_dropItems.Load(difficulty, files, resourceError))
        return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    if (!m_difficulty.BeginMap(mapName, difficulty, files, resourceError))
        return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    // Row zero is the table header, not a selectable gameplay difficulty.
    if (!difficulty || difficulty >= m_difficulty.Table()->source.records.size())
        return Fail({std::string(m_difficulty.SourcePath()), {0, "selected scenario difficulty row is unavailable"}});
    const auto* mapTables = m_map.Tables();
    if (!mapTables || !mapTables->difficultyLimit || difficulty > *mapTables->difficultyLimit)
        return Fail({"resource/maplist.csv", {0, "selected scenario difficulty is not allowed by this map"}});
    if (!m_upgrades.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    if (!m_equipmentResources.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    m_weapons = std::make_shared<CCSOLScenarioWeaponState>(m_map.EntityAccess());
    if (!m_weapons->Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;

    if (!m_messages.Register())
        return Fail({"", {0, "scenario MPToCL message registration failed"}});
    if (!m_map.EntityAccessAllowed()) return false;
    m_music = std::make_shared<CCSOLScenarioMusic>(m_map.EntityAccess());
    if (!m_music->Precache(mapTables->rounds, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    // Selection uses the real map tables, current spawn group and character
    // upgrade data. AI models/NAV/pool belong to combat startup, and may only
    // be precached during a map load with actual combat services installed.
    if (m_scope == CSOLScenarioResourceScope::Selection)
    {
        m_state = CSOLScenarioSessionState::Precached;
        return true;
    }
    csol::scenario::ConfigError kickError;
    if (!CSOL_PrecacheHumanKick(m_map.EntityAccess(), kickError))
        return m_map.EntityAccessAllowed() ? Fail({"", std::move(kickError)}) : false;
    if (!m_map.EntityAccessAllowed()) return false;
    // The original creates the machine on first player initialization, after
    // loading. GoldSrc must have its actual model/sprite indices beforehand.
    for (const char* path : {csol::wall_upgrade::Model, csol::wall_upgrade::Sprite})
    {
        if (!g_engfuncs.pfnGetFileSize || !g_engfuncs.pfnPrecacheModel)
            return Fail({path, {0, "scenario wall upgrade precache is unavailable"}});
        const int size = g_engfuncs.pfnGetFileSize(path);
        if (!m_map.EntityAccessAllowed()) return false;
        if (size <= 0) return Fail({path, {0, "required scenario wall upgrade resource is missing or empty"}});
        const int index = PRECACHE_MODEL(const_cast<char*>(path));
        if (!m_map.EntityAccessAllowed()) return false;
        if (index <= 0) return Fail({path, {0, "scenario wall upgrade resource precache failed"}});
    }
    if (!m_equipmentResources.Snapshot()->weaponDropDifficulty || !mapTables->mapId)
        return Fail({"maps/ZBS.json", {0, "scenario monster disarm requires restrictWeaponDrop.default.Difficulty and a map ID"}});
    std::string championPath;
    if (!csol::scenario::LoadChampions(mapName, files, m_champions, championPath, resourceError))
        return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    if (!m_eliteSettings.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    const auto skillAccess = m_map.EntityAccess();
    const auto skillsCurrent = [&]
    {
        return m_map.EntityAccessAllowed() && m_map.EntityAccess() == skillAccess;
    };
    const bool skillsLoaded = m_bossSkills.Load(mapName, files,
        [](const void* context) { return (*static_cast<const decltype(skillsCurrent)*>(context))(); },
        &skillsCurrent, resourceError);
    if (!skillsCurrent()) return false;
    if (!skillsLoaded) return Fail(std::move(resourceError));
    if (!g_engfuncs.pfnPrecacheSound)
        return Fail({"sound/vox/roundclear.wav", {0, "scenario completion sound registration is unavailable"}});
    PRECACHE_SOUND("vox/roundclear.wav");
    if (!m_map.EntityAccessAllowed()) return false;
    if (!m_classMods.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    const auto access = m_map.EntityAccess();
    const auto weapons = m_weapons;
    const auto items = weapons->ItemCatalog();
    const auto presetsCurrent = [&]
    {
        return m_map.EntityAccessAllowed() && m_map.EntityAccess() == access &&
            m_weapons == weapons && weapons->Current() && weapons->ItemCatalog() == items;
    };
    const bool presetsLoaded = m_classModPresets.Load(files, items,
        [](const void* context) { return (*static_cast<const decltype(presetsCurrent)*>(context))(); },
        &presetsCurrent, resourceError);
    if (!presetsCurrent()) return false;
    if (!presetsLoaded) return Fail(std::move(resourceError));
    if (!m_announcements.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    if (!m_damageDisplayModes.Load(files, resourceError)) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    // Status labels and keyboard hints are generic downloadable TGAs.
    if (!g_engfuncs.pfnPrecacheGeneric)
        return Fail({csol::status::ProtectionImage, {0, "scenario status image download registration is unavailable"}});
    for (const auto& image : csol::scenario_kill::Resources)
    {
        if (!g_engfuncs.pfnGetFileSize)
            return Fail({image.path, {0, "scenario kill image filesystem check is unavailable"}});
        const int size = g_engfuncs.pfnGetFileSize(image.path);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0)
            return Fail({image.path, {0, "required scenario kill image is missing or empty"}});
        PRECACHE_GENERIC(image.path);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    // UI assets are part of the authoritative map download list, so a clean
    // client receives the same original images/font over the built-in HTTP
    // service. The two converted Bink strips retain their original RGBA frames.
    std::set<std::string> hudPaths{csol::upgrades::FontPath, csol::upgrades::DigitSpritePath};
    for (const auto& image : csol::upgrades::Resources) hudPaths.emplace(image.path);
    for (const auto* image : csol::abilities::DownloadResources) hudPaths.emplace(image);
    for (const auto* image : csol::boss_health::Resources) hudPaths.emplace(image);
    for (const auto* image : csol::boss_health::AbilityResources) hudPaths.emplace(image);
    for (std::size_t type = 0; type < csol::status::ImageCount; ++type)
        hudPaths.emplace(std::string(csol::status::ImagePrefix) + std::to_string(type) + ".tga");
    for (const auto* image : csol::buy_hint::Images) hudPaths.emplace(image);
    hudPaths.emplace(csol::buy_hint::Font);
    for (const auto& image : csol::scenario_hud::Resources) hudPaths.emplace(image.path);
    hudPaths.emplace(csol::scenario_hud::MessagePath);
    hudPaths.emplace(csol::scenario_hud::LocalizationPath);
    hudPaths.emplace(csol::scenario_hud::FontPath);
    for (const auto& image : csol::scenario_respawn_ui::Outcomes) hudPaths.emplace(image.path);
    for (const auto& image : csol::scenario_respawn_ui::RetryImages) hudPaths.emplace(image.path);
    hudPaths.emplace(csol::scenario_respawn_ui::ItemSprite);
    hudPaths.emplace(csol::scenario_respawn_ui::CenterBackground.path);
    hudPaths.emplace(csol::scenario_respawn_ui::LocalizationPath);
    csol::scenario::DialogueResources dialogue;
    const auto collectDialogue = [&]
    {
        int length{};
        std::unique_ptr<unsigned char, decltype(files.release)> encoded(
            files.load(csol::scenario_hud::MessagePath, &length), files.release);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (!encoded || length <= 0 || length > 8 * 1024 * 1024)
            return Fail({csol::scenario_hud::MessagePath, {0, "scenario dialogue table is missing or invalid"}});
        csol::scenario::MessageTable table;
        csol::scenario::ConfigError detail;
        if (!csol::scenario::ReadMessageTableResource(
            {reinterpret_cast<const std::byte*>(encoded.get()), std::size_t(length)}, table, detail) ||
            !csol::scenario::CollectDialogueResources(mapTables->waves, table, dialogue, detail))
            return Fail({csol::scenario_hud::MessagePath, std::move(detail)});
        return true;
    };
    const bool dialogueReady = collectDialogue();
    // FreeFile is an engine boundary too; a callback can retire this map.
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    if (!dialogueReady) return false;
    hudPaths.insert(dialogue.images.begin(), dialogue.images.end());
    for (const auto& path : hudPaths)
    {
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0)
            return Fail({path, {0, "required scenario HUD resource is missing or empty"}});
        PRECACHE_GENERIC(path.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    for (const auto* sound : csol::scenario_respawn_ui::RetrySounds)
    {
        const int size = g_engfuncs.pfnGetFileSize(sound);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0) return Fail({sound, {0, "required scenario retry sound is missing or empty"}});
        // Filesystem/download paths retain sound/, while the engine sound
        // precache API owns that root itself.
        PRECACHE_SOUND(sound + sizeof("sound/") - 1);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    const std::string upgradeSound = std::string("sound/") + csol::upgrades::SoundPath;
    const int upgradeSoundSize = g_engfuncs.pfnGetFileSize(upgradeSound.c_str());
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    if (upgradeSoundSize <= 0)
        return Fail({upgradeSound, {0, "required scenario upgrade sound is missing or empty"}});
    PRECACHE_SOUND(csol::upgrades::SoundPath);
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    for (const auto& sound : dialogue.sounds)
    {
        const std::string path = "sound/" + sound;
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0) return Fail({path, {0, "required scenario dialogue sound is missing or empty"}});
        PRECACHE_SOUND(sound.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    // PRECACHE_MODEL terminates ReHLDS on a missing file. Diagnose the whole
    // effective catalog and the pool's initial model before entering that
    // irreversible engine path.
    if (!g_engfuncs.pfnGetFileSize)
        return Fail({"", {0, "scenario model filesystem check is unavailable"}});
    std::set<std::string> modelPaths{CSOLScenarioInitialMonsterModel};
    std::shared_ptr<const csol::scenario::MonsterType> eliteType;
    csol::scenario::ConfigError eliteError;
    if (!m_eliteSettings.Snapshot() || !csol::scenario::MakeEliteMonsterType(
        *m_eliteSettings.Snapshot(), difficulty, eliteType, eliteError))
        return Fail({"maps/ZBS.json", std::move(eliteError)});
    const auto* eliteAppearance = csol::scenario::FindMonsterAppearance(eliteType->appearance);
    if (!eliteAppearance)
        return Fail({"maps/ZBS.json", {0, "scenario synthetic elite model is unavailable"}});
    modelPaths.emplace(eliteAppearance->model);
    modelPaths.emplace("sprites/320_pain.spr");
    modelPaths.emplace("sprites/640_pain.spr");
    for (const auto* asset : {CSOLMonsterTrapModel, CSOLMonsterDisarmModel,
        CSOLMonsterDisarmTrail, csol::monster_effects::ImpactSprite,
        CSOLZombieBombModel, csol::monster_effects::ZombieBombSprite, "models/grenade.mdl"}) modelPaths.emplace(asset);
    for (const auto* sprite : csol::healing::Sprites) modelPaths.emplace(sprite);
    for (const auto* sprite : csol::part_effects::Sprites) modelPaths.emplace(sprite);
    modelPaths.emplace(csol::smoke::Sprite);
    modelPaths.emplace(csol::frozen_death::IceModel);
    modelPaths.emplace(csol::frozen_smoke::SmokeSprite);
    const auto& types = m_map.Tables()->monsterTypes;
    // Original MP1155D720 precaches these with the boomer class on every
    // map. Host adaptation: require them only when a skill11/13 row exists.
    bool boomerEffects{};
    for (const auto& champion : m_champions.rows)
    {
        const auto* appearance = csol::scenario::FindMonsterAppearance(champion.type.appearance);
        if (!appearance) return Fail({championPath, {0, "unknown champion appearance"}});
        modelPaths.emplace(appearance->model);
        if (champion.type.skill == 11 || champion.type.skill == 13)
        {
            boomerEffects = true;
            for (const auto* asset : CSOLBoomerEffectAssets) modelPaths.emplace(asset);
        }
        if (!appearance->deathSound.empty()) modelPaths.emplace("sound/" + std::string(appearance->deathSound));
        if (!appearance->ambientSound.empty()) modelPaths.emplace("sound/" + std::string(appearance->ambientSound));
        if (appearance->name == "zombiedog") modelPaths.emplace("sound/zombi/zombiedog_howls.wav");
    }
    for (const auto& [id, row] : types.byId)
    {
        if (row >= types.rows.size() || types.rows[row].Key() != id)
            return Fail({"", {0, "scenario monster type index is invalid"}});
        const auto* appearance = csol::scenario::FindMonsterAppearance(types.rows[row].appearance);
        if (!appearance)
            return Fail({"", {0, "scenario monster appearance is not in the original catalog"}});
        modelPaths.emplace(appearance->model);
        if (types.rows[row].skill == 11 || types.rows[row].skill == 13)
        {
            boomerEffects = true;
            for (const auto* asset : CSOLBoomerEffectAssets) modelPaths.emplace(asset);
        }
        if (types.rows[row].id == CSOLOberonMonsterTypeId)
        {
            modelPaths.emplace(CSOLOberonBlackHoleModel);
            modelPaths.emplace(CSOLOberonKnifeEffectModel);
            modelPaths.emplace(csol::oberon_hint::Sprite);
            for (const auto* dependency : CSOLOberonBlackHoleDependencies) modelPaths.emplace(dependency);
            for (const auto* asset : CSOLOberonAttackModels) modelPaths.emplace(asset);
            // Check attack sounds before any engine model precache too.
            for (const auto* sound : CSOLOberonAttackSounds)
                modelPaths.emplace(std::string("sound/") + sound);
        }
    }
    for (const auto& path : modelPaths)
    {
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0)
            return Fail({path, {0, "required scenario monster resource is missing or empty: " + path}});
    }
    csol::scenario::ConfigError error;
    for (const auto* asset : {CSOLMonsterTrapModel, CSOLMonsterDisarmModel,
        CSOLMonsterDisarmTrail, csol::monster_effects::ImpactSprite,
        CSOLZombieBombModel, csol::monster_effects::ZombieBombSprite, "models/grenade.mdl",
        "sprites/320_pain.spr", "sprites/640_pain.spr"})
    {
        PRECACHE_MODEL(asset);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    for (const auto* sprite : csol::part_effects::Sprites)
    {
        PRECACHE_MODEL(sprite);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    if (boomerEffects)
        for (const auto* asset : CSOLBoomerEffectAssets)
        {
            PRECACHE_MODEL(asset);
            if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        }
    for (const auto* sound : {"zombi/Heavy_newTrap.wav", "zombi/deimos_skill_hit.wav",
        "zombi/Zombi_Trapped.wav", "zombi/Zombi_Trapped_Female.wav",
        csol::monster_effects::ImpactSound, CSOLZombieBombBounceSounds[0], CSOLZombieBombBounceSounds[1]})
    {
        const std::string path = std::string("sound/") + sound;
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (size <= 0) return Fail({path, {0, "required scenario monster skill sound is missing"}});
        PRECACHE_SOUND(sound);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    for (const auto* sprite : csol::healing::Sprites)
    {
        PRECACHE_MODEL(sprite);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    }
    PRECACHE_MODEL(csol::smoke::Sprite);
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    for (const auto* asset : {csol::frozen_death::IceModel, csol::frozen_smoke::SmokeSprite})
    {
        const int index = PRECACHE_MODEL(asset);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (index <= 0) return Fail({asset, {0, "frozen death resource precache failed"}});
    }
    constexpr auto smokeEvent = csol::smoke::EventPath;
    const int eventSize = g_engfuncs.pfnGetFileSize(smokeEvent);
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    if (eventSize <= 0 || !g_engfuncs.pfnPrecacheEvent)
        return Fail({smokeEvent, {0, "scenario smoke event is missing or cannot be registered"}});
    const int bombEventSize = g_engfuncs.pfnGetFileSize(CSOLZombieBombEvent);
    if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
    if (bombEventSize <= 0)
        return Fail({CSOLZombieBombEvent, {0, "scenario zombie bomb event is missing"}});
    // Register after WeaponsPrecache so the extra event cannot shift any
    // stock client's cached prediction indices after a mode/map change.
    // ServerActivate still runs while ReHLDS is loading the resource table.
    if (!m_models.Precache(m_map.Tables()->monsterTypes, error, eliteType.get(), &m_champions))
        return Fail({"", std::move(error)});
    if (!m_map.EntityAccessAllowed()) return false;
    CCSOLScenarioDrops::Precache();
    if (!m_map.EntityAccessAllowed()) return false;
    std::optional<std::uint32_t> bspSize;
    if (g_engfuncs.pfnGetFileSize)
    {
        const auto path = "maps/" + std::string(mapName) + ".bsp";
        const int size = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!m_map.EntityAccessAllowed()) return false;
        if (size >= 0) bspSize = static_cast<std::uint32_t>(size);
    }
    m_navigation = CCSOLScenarioNavigation::Load(mapName, files, resourceError, bspSize,
        [](const char* name) -> Place
        {
            return TheBotPhrases ? TheBotPhrases->NameToID(name) : UNDEFINED_PLACE;
        });
    if (!m_navigation) return Fail(std::move(resourceError));
    if (!m_map.EntityAccessAllowed()) return false;
    // Every monster's initial Spawn calls Precache, so allocation belongs to
    // world loading, not the first round/Think after loading has finished.
    if (!m_monsters.Initialize())
        return Fail({"", {0, m_monsters.Error()}});

    m_state = CSOLScenarioSessionState::Precached;
    return true;
}

bool CCSOLScenarioSession::Activate()
{
    if (!m_map.EntityAccessAllowed() || m_busy) return false;
    if (m_state == CSOLScenarioSessionState::Active) return true;
    if (m_state != CSOLScenarioSessionState::Precached) return false;
    SessionOperation operation(m_busy);
    if (m_scope == CSOLScenarioResourceScope::Combat)
    {
        const auto access = m_map.EntityAccess();
        constexpr auto smokeEvent = csol::smoke::EventPath;
        if (!g_engfuncs.pfnPrecacheEvent)
            return Fail({smokeEvent, {0, "scenario smoke event lookup is unavailable"}});
        // ServerActivate follows WeaponsPrecache and precedes ss_active.
        // Append our event without replacing the native smoke registration.
        const auto event = PRECACHE_EVENT(1, smokeEvent);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (!event) return Fail({smokeEvent, {0, "scenario smoke event lookup failed"}});
        m_smokeEvent = event;
        // Original constructor registers "createexplo". The canonical
        // GoldSrc resource path is appended after native weapon events.
        const auto bombEvent = PRECACHE_EVENT(1, CSOLZombieBombEvent);
        if (!m_map.EntityAccessAllowed() || m_map.EntityAccess() != access) return false;
        if (!bombEvent) return Fail({CSOLZombieBombEvent, {0, "scenario zombie bomb event lookup failed"}});
        m_zombieBombEvent = bombEvent;
    }
    if (!m_map.CollectEntities()) return Fail(m_map.Error());
    csol::scenario::ConfigError wallError;
    if (!m_walls.Collect(wallError)) return Fail({"", std::move(wallError)});
    if (!m_map.EntityAccessAllowed()) return false;
    csol::scenario::ConfigError navigationError;
    const auto navigation = m_navigation;
    if (m_scope == CSOLScenarioResourceScope::Combat &&
        (!navigation || !navigation->BuildLadders(navigationError)))
        return Fail({"", navigation ? std::move(navigationError) :
            csol::scenario::ConfigError{0, "scenario navigation owner is unavailable"}});
    if (!m_map.EntityAccessAllowed()) return false;
    m_state = CSOLScenarioSessionState::Active;
    m_error = {};
    return true;
}

bool CCSOLScenarioSession::Deactivate()
{
    if (m_busy) return false;
    SessionOperation operation(m_busy);
    ClearWallUpgrade();
    if (m_music) m_music->Reset();
    m_music.reset();
    m_smokeEvent = 0;
    m_zombieBombEvent = 0;
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
        return false; // In-flight cleanup / engine OnDestroy still owns slots.
    if (!m_map.EndMap())
        return false;
    m_navigation.reset();
    if (!m_models.Reset())
        return Fail({"", {0, "scenario model cache is busy during shutdown"}});
    if (!m_difficulty.Clear() || !m_dropItems.Clear() || !m_upgrades.Clear() ||
        !m_equipmentResources.Clear() || !m_classMods.Clear() || !m_classModPresets.Clear() ||
        !m_announcements.Clear() || !m_damageDisplayModes.Clear() || !m_bossSkills.Clear() ||
        !m_eliteSettings.Clear())
        return Fail({"", {0, "scenario difficulty resources are busy during shutdown"}});
    m_messages = {};
    m_champions = {};
    m_challengeTimeOrigin = 0;
    m_error = {};
    m_state = CSOLScenarioSessionState::Empty;
    return true;
}

bool CCSOLScenarioSession::BindWallPlayers(std::shared_ptr<CCSOLScenarioWallPlayers> players)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active || !m_map.Tables()) return false;
    SessionOperation operation(m_busy);
    csol::scenario::ConfigError error;
    // Survival maps such as lostcity have no zbsbreak entities and their
    // original round table has zero wall rewards. Do not install an unused
    // damage/reward loop; real walls still require a positive threshold.
    if (m_walls.Count() == 0)
    {
        if (!players || !m_walls.BindRules(nullptr)) return false;
        m_wallRewards.reset();
        m_error = {};
        return true;
    }
    auto rules = CCSOLScenarioWallRewards::Create(m_map.Tables()->rounds, m_difficulty, std::move(players), error);
    if (!rules) return Fail({"", std::move(error)});
    if (m_wallUpgrade) rules->SetMonsterLevel(m_wallUpgrade->Level());
    if (!m_walls.BindRules(rules)) return Fail({"", {0, "scenario wall rules could not be bound"}});
    m_wallRewards = std::move(rules);
    m_error = {};
    return true;
}

bool CCSOLScenarioSession::BindUpgradeServices(std::shared_ptr<CCSOLScenarioUpgradeServices> services)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active || !services || !m_upgrades.Snapshot()) return false;
    csol::scenario::ConfigError error;
    float ratio{}, joinLevelRatio{};
    if (!m_difficulty.Parameter(15, 0x12, ratio, error) ||
        !m_difficulty.Parameter(15, 8, joinLevelRatio, error)) return Fail({"", std::move(error)});
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
    m_joinLevelRatio = joinLevelRatio;
    return true;
}

bool CCSOLScenarioSession::BindEquipmentServices(CCSOLScenarioEquipment::CapEvent capEvent)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active || !capEvent || !m_equipmentResources.Snapshot()) return false;
    auto equipment = std::make_shared<CCSOLScenarioEquipment>(m_equipmentResources.Snapshot(), std::move(capEvent), m_map.EntityAccess());
    if (!BindUpgradeServices(equipment)) return false;
    m_equipment = std::move(equipment);
    return true;
}

bool CCSOLScenarioSession::PrepareEquippedPlayer(CBasePlayer& player, int classId,
    std::uint8_t feature, bool elevenLevelLimit, csol::scenario::EquipmentLoadout loadout)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active || !m_equipment) return false;
    const auto equipment = m_equipment;
    const EHANDLE handle(&player);
    if (!equipment->BindPlayer(player, classId, std::move(loadout))) return false;
    if (!m_map.EntityAccessAllowed()) return false;
    auto* current = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    if (!current || m_state != CSOLScenarioSessionState::Active || m_equipment != equipment || !equipment->Current()) return false;
    return PreparePlayer(*current, classId, feature, elevenLevelLimit);
}

bool CCSOLScenarioSession::PreparePlayer(CBasePlayer& player, int classId, std::uint8_t feature, bool elevenLevelLimit)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active || !m_players || !m_upgradeServices) return false;
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
    for (auto& participant : m_wallParticipants)
        if (participant.player.Get() == player.Get()) participant = {};
    if (m_equipment) m_equipment->RemovePlayer(player);
    if (m_players) m_players->Remove(player);
}

std::unique_ptr<CCSOLScenarioReplaySettings> CCSOLScenarioSession::PrepareReplaySettings(
    std::uint16_t difficulty, csol::scenario::ConfigError& error) const
{
    error = {};
    const auto* tables = m_map.Tables();
    if (Busy() || !m_map.EntityAccessAllowed() || m_state != CSOLScenarioSessionState::Active ||
        m_scope != CSOLScenarioResourceScope::Combat || !m_players || !m_upgradeServices ||
        !tables || !tables->difficultyLimit || difficulty < 1 || difficulty > *tables->difficultyLimit)
    {
        error.message = "scenario replay requires an idle active map and an allowed difficulty";
        return {};
    }
    auto candidate = std::make_unique<CCSOLScenarioReplaySettings>();
    if (!m_difficulty.PrepareSelection(difficulty, candidate->difficulty, error) ||
        !m_dropItems.PrepareSelection(difficulty, candidate->drops, error) ||
        !m_map.CheckDifficulty(candidate->difficulty, error) ||
        !candidate->difficulty.Parameter(15, 18, candidate->experienceRatio, error) ||
        !candidate->difficulty.Parameter(15, 8, candidate->joinLevelRatio, error)) return {};
    // These parameters are consumed later by deaths, items and incoming damage.
    // Reject a missing row before retiring any current gameplay state.
    for (const std::size_t column : {4u, 6u, 7u, 9u, 12u, 15u, 19u})
    {
        float parameter{};
        if (!candidate->difficulty.Parameter(15, column, parameter, error)) return {};
        if (column == 4 && !csol::scenario::consumables::Continuation::ValidLimit(parameter))
        {
            error.message = "scenario continuation limit is outside its signed integer range";
            return {};
        }
    }
    const auto elites = m_eliteSettings.Snapshot();
    std::shared_ptr<const csol::scenario::MonsterType> eliteType;
    if (!elites || !csol::scenario::MakeEliteMonsterType(*elites, difficulty, eliteType, error)) return {};
    if (m_walls.Count() != 0)
    {
        candidate->walls = CCSOLScenarioWallRewards::Create(tables->rounds,
            candidate->difficulty, m_players, error);
        if (!candidate->walls) return {};
    }
    candidate->hits = CCSOLMonsterHitRewards::Create(candidate->difficulty, m_players, error);
    if (!candidate->hits) return {};
    candidate->players = m_players;
    candidate->access = m_map.EntityAccess();
    candidate->round = m_map.RoundGeneration();
    return candidate;
}

bool CCSOLScenarioSession::CommitReplaySettings(CCSOLScenarioReplaySettings& settings, float now)
{
    if (Busy() || m_state != CSOLScenarioSessionState::Active || settings.consumed ||
        !std::isfinite(now) || now < 0 ||
        !settings.access || !settings.access->Allowed() || m_map.EntityAccess() != settings.access ||
        m_map.RoundGeneration() != settings.round || m_players != settings.players ||
        (!settings.walls && m_walls.Count() != 0) || !settings.hits) return false;
    SessionOperation operation(m_busy);
    if (!m_map.ApplyDifficulty(settings.difficulty)) return Fail(m_map.Error());
    if (!m_map.ResetChallengeCounters()) return Fail({"", {0, "scenario challenge counters could not be reset"}});
    if (!m_walls.BindRules(settings.walls)) return Fail({"", {0, "scenario replay wall rules could not be installed"}});
    if (m_hitRewards) m_hitRewards->Invalidate();
    m_hitRewards = std::move(settings.hits);
    m_wallRewards = std::move(settings.walls);
    m_difficulty = std::move(settings.difficulty);
    m_dropItems = std::move(settings.drops);
    m_experienceRatio = settings.experienceRatio;
    m_joinLevelRatio = settings.joinLevelRatio;
    m_challengeTimeOrigin = now;
    // A replay is a new challenge; ordinary stage changes retain this registry.
    // Character/loadout ownership stays in the runtime and is rebound on spawn.
    m_players->Clear();
    if (!ResetWallUpgradeChallenge()) return false;
    settings.consumed = true;
    m_error = {};
    return true;
}

bool CCSOLScenarioSession::BeginRound(std::size_t roundIndex, float now, float freezeTime,
    csol::scenario::TriggerWaveEvents& events)
{
    if (!m_map.EntityAccessAllowed() || m_busy || m_state != CSOLScenarioSessionState::Active) return false;
    if (m_scope != CSOLScenarioResourceScope::Combat)
        return Fail({"", {0, "scenario combat resources were not prepared for this map"}});
    SessionOperation operation(m_busy);
    if (!m_map.Tables() || roundIndex >= m_map.Tables()->rounds.rows.size())
        return Fail({"", {0, "scenario session round index is outside its table"}});
    const auto access = m_map.EntityAccess();
    csol::scenario::ConfigError status;
    if (!events.Check(status))
        return status.message.empty() ? false : Fail({"", std::move(status)});
    m_walls.CancelPending();
    if (!m_map.ApplyDifficulty(m_difficulty)) return Fail(m_map.Error());
    if (!m_map.BeginRound(roundIndex, now, freezeTime, events))
    {
        if (!access->Allowed() || m_map.EntityAccess() != access) return false;
        // Empty diagnostics are a cancelled caller, not failed map data.
        if (m_map.Error().detail.message.empty()) return false;
        return Fail(m_map.Error());
    }
    if (!access->Allowed() || m_map.EntityAccess() != access) return false;
    if (!events.Check(status))
        return status.message.empty() ? false : Fail({"", std::move(status)});
    m_error = {};
    return true;
}
