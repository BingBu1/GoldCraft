// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "game_rules_gate.h"
#include "game_mode.h"
#include "scenario_session.h"
#include "scenario_gameplay.h"
#include "scenario_player_runtime.h"
#include "scenario_recovery_notice.h"
#include "scenario_drops.h"
#include "scenario_entity_lookup.h"
#include "scenario_runtime.h"
#include "scenario_death_services.h"
#include "scenario_monster_services.h"
#include "scenario_monster_damage.h"
#include "scenario_wave_services.h"
#include "scenario_breakable.h"
#include "scenario_trigger.h"
#include <list>
#include <vector>
#include "characters.h"
#include "../scenario/round_settings.hpp"
#include "../scenario/map_catalog.hpp"
#include "interface.h"
#include "../../include/goldcraft/csol_scenario_admin_api.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <bit>

CCSOLScenarioRuntime::CCSOLScenarioRuntime() : session(std::make_shared<CCSOLScenarioSession>()) {}

void CCSOLScenarioRuntime::SetArchiveRoot(std::string directory)
{
    if (m_closing) return;
    m_archiveRoot = directory.empty() ? std::string{} : std::filesystem::absolute(directory).lexically_normal().string();
}

bool CCSOLScenarioRuntime::SetClearPointProtection(bool enabled)
{
    const auto owner = players;
    const auto mapSession = session;
    if (m_closing || !mapSession || !owner || !owner->Active()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(mapSession.get()));
    if (!sdk.Scenario() || !sdk.Current()) return false;
    // CSO does not refresh players or targets when the global value matches.
    if (owner->ClearPointProtectionEnabled() == enabled) return true;
    const auto access = mapSession->Map().EntityAccess();
    const auto generation = m_generation;
    const auto round = mapSession->Map().RoundGeneration();
    const auto operation = owner->ClearPointProtectionRevision() + 1;
    const auto current = [&]()
    {
        return sdk.Current() && !m_closing && m_generation == generation && session == mapSession && players == owner &&
            access->Allowed() && mapSession->Map().EntityAccess() == access &&
            mapSession->Map().RoundGeneration() == round && owner->Active() &&
            owner->ClearPointProtectionRevision() == operation;
    };
    if (!owner->SetPlayerClearPointProtection(enabled) || !current()) return false;
    CBaseEntity* entity{};
    while (true)
    {
        // Resolve only after the player-effects phase, including entities
        // created or changed by callbacks during that phase.
        const auto found = CSOL_LookupScenarioEntity(entity, "zbstarget", access);
        if (!current() || !found.valid) return false;
        entity = found.entity;
        if (!entity) return true;
        auto* target = dynamic_cast<CCSOLScenarioTarget*>(entity);
        if (!target || !target->ValidConfiguration() || (target->pev->flags & FL_KILLME)) return false;
        target->SetClearPointProtection(enabled);
    }
}

void CCSOLScenarioRuntime::RequestClose()
{
    // No callbacks: retire owned render references while edicts still belong
    // to this map. Once access is revoked, finalization only drops records.
    if (!m_closing && players) players->ReleaseProtectionState();
    // Revoke even when a previous close is still waiting for its callers.
    session->RevokeEntityAccess();
    if (m_closing) return;
    m_closing = true;
    ++m_generation;
}

bool CCSOLScenarioRuntime::ReadyForArchive() const
{
    return m_closing && !m_operations && session && !session->Busy() &&
        !session->Monsters().RegisteredCount() && (!gameplay || !gameplay->Busy()) &&
        (!drops || (!drops->Busy() && !drops->RegisteredCount()));
}

bool CCSOLScenarioRuntime::TryFinalize()
{
    if (m_finalized) return true;
    if (m_finalizing || !ReadyForArchive()) return false;
    if (std::chrono::steady_clock::now() < m_archiveRetry) return false;
    m_finalizing = true;
    struct FinalizingScope { bool& flag; ~FinalizingScope() { flag = false; } } scope{m_finalizing};
    try
    {
        if (!m_shutdownPrepared)
        {
            if (!m_archiveMap)
            {
                const auto* tables = session->Map().Tables();
                m_archiveMap = tables ? CSOLScenarioArchiveMap{tables->mapName, tables->mapId} : CSOLScenarioArchiveMap{};
            }
            if (drops) drops->Deactivate();
            // Retire components before taking the immutable snapshot. Their
            // last callbacks must not append values after successful storage.
            if (players) players->Invalidate();
            if (gameplay && !gameplay->Deactivate()) return false;
            if (!ReadyForArchive()) return false;
            gameplay.reset();
            waveEvents.reset();
            monsterServices.reset();
            monsterDamageServices.reset();
            recoveryNotice.reset();
            if (!session->Deactivate()) return false;
            if (!ReadyForArchive()) return false;
            m_shutdownPrepared = true;
        }
        // No old edict is read from here. Save local values before releasing
        // their owners. Unknown rewards retain their pending status on disk.
        if (!m_archiveSaved)
        {
            const auto now = std::chrono::steady_clock::now();
            std::string error;
            if (!CSOL_ArchiveScenarioShutdown(*this, m_archiveRoot, m_archiveDirectory, error))
            {
                m_archiveRetry = now + std::chrono::seconds(5);
                if (error != m_archiveError)
                {
                    m_archiveError = std::move(error);
                    ALERT(at_console, "[CSOL] Retaining scenario records: %s\n", m_archiveError.c_str());
                }
                return false;
            }
            m_archiveSaved = true;
            m_archiveError.clear();
        }
        deathServices.reset();
        drops.reset();
        players.reset();
        m_finalized = true;
        return true;
    }
    catch (...)
    {
        // No lifecycle allocation failure may escape an SDK retirement edge.
        // All unarchived values remain owned; a later collector can retry.
        m_archiveRetry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        return false;
    }
}

struct CSOLScenarioRulesRecord
{
    CGameRules* rules{};
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    bool retired{}, deleting{};
};

namespace
{
std::array<char, 256> rejection{};
std::list<std::shared_ptr<CSOLScenarioRulesRecord>> scenarioRules;
std::shared_ptr<CSOLScenarioRulesRecord> entityRules;
std::size_t sdkDepth{};
bool collectingRules{};
std::uint64_t worldSerial{};
CSOLScenarioWorldScope* installingWorld{};

std::shared_ptr<CSOLScenarioRulesRecord> FindScenarioRules(const CGameRules* rules)
{
    if (!rules) return {};
    for (const auto& record : scenarioRules)
        if (record->rules == rules) return record;
    return {};
}

// Keep the real multiplayer layout: CSGameRules() and ReAPI use it directly.
// A failed map must not execute the ordinary round/auto-changelevel machinery.
class CCSOLRejectedRules : public CHalfLifeMultiplay
{
public:
    CCSOLRejectedRules()
    {
        m_bGameOver = true;
        m_bGameStarted = false;
        m_bFreezePeriod = TRUE;
        m_bRoundTerminating = true;
    }
    void Think() override {}
    const char* GetGameDescription() override { return "GoldCraft - map unavailable"; }
    BOOL ClientConnected(edict_t*, const char*, const char*, char* reason) override
    {
        Q_strncpy(reason, CSOL_RulesRejectionReason(), 128);
        reason[127] = '\0';
        return FALSE;
    }
    void PlayerSpawn(CBasePlayer* player) override { CSOL_HoldRejectedPlayer(*player); }
    void PlayerThink(CBasePlayer* player) override { CSOL_HoldRejectedPlayer(*player); }
    BOOL FPlayerCanRespawn(CBasePlayer*) override { return FALSE; }
    BOOL FPlayerCanTakeDamage(CBasePlayer*, CBaseEntity*) override { return FALSE; }
    BOOL CanHavePlayerItem(CBasePlayer*, CBasePlayerItem*) override { return FALSE; }
    BOOL CanHaveItem(CBasePlayer*, CItem*) override { return FALSE; }
    BOOL CanHaveAmmo(CBasePlayer*, const char*, int) override { return FALSE; }
    void CheckWinConditions() override {}
    void RestartRound() override {}
    void GoToIntermission() override {}
    void ChangeLevel() override {}
};

// Keep the multiplayer layout and ReAPI wrappers. Their original functions
// dispatch to the scenario methods below, without running CS elimination,
// team balancing or the stock cleanup-before-respawn restart sequence.
class CCSOLScenarioRules final : public CHalfLifeMultiplay, public csol::scenario::TriggerWaveEvents
{
public:
    enum class Phase { Loading, Selecting, Waiting, Starting, Playing, Advancing, Completing, Failed, Complete, FreePlay };
    CCSOLScenarioRules()
    {
        m_bGameOver = false;
        m_bGameStarted = false;
        m_bFreezePeriod = TRUE;
        m_bRoundTerminating = false;
    }
    const char* GetGameDescription() override { return "GoldCraft - Zombie Scenario"; }
    std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess() const { return m_runtime->session->Map().EntityAccess(); }
    bool HasCharacterClass(int classId) const
    {
        if (!SelectionReady()) return false;
        const auto upgrades = m_runtime->session->Upgrades();
        return upgrades && upgrades->classes.Find(classId);
    }
    std::shared_ptr<CCSOLScenarioRuntime> Runtime() const { return m_runtime; }
    void Precache()
    {
        if (!OwnsRules()) return;
        if (g_engfuncs.pfnGetGameDir)
        {
            char directory[512]{};
            GET_GAME_DIR(directory);
            directory[sizeof(directory) - 1] = '\0';
            if (!OwnsRules()) return;
            m_runtime->SetArchiveRoot(directory);
        }
        if (!m_runtime->monsterDamageServices)
            m_runtime->monsterDamageServices = std::make_shared<CCSOLScenarioDamageServices>(m_runtime);
        if (!m_runtime->waveEvents)
            m_runtime->waveEvents = std::make_shared<CCSOLScenarioWaveServices>(m_runtime, m_runtime->session);
        m_boundWaveEvents = m_runtime->waveEvents;
        std::uint16_t difficulty{};
        if (!CSOL_RequestedScenarioDifficulty(difficulty))
        {
            CSOL_RejectGameRules("Invalid csol_scenario_difficulty; expected a positive integer row.");
            return;
        }
        if (!CSOL_RequestedScenarioShortFinalDelay(m_shortFinalDelay))
        {
            CSOL_RejectGameRules("Invalid csol_scenario_short_final_delay; expected 0 or 1.");
            return;
        }
        const auto scope = m_runtime->monsterDamageServices && m_runtime->waveEvents ? CSOLScenarioResourceScope::Combat :
            CSOLScenarioResourceScope::Selection;
        if (!m_runtime->session->Precache(STRING(gpGlobals->mapname), difficulty, scope))
        {
            FailSession();
            return;
        }
        if (!OwnsRules()) return;
        csol::scenario::ConfigError settingsError;
        const auto* tables = m_runtime->session->Map().Tables();
        if (!tables || !csol::scenario::ReadRewardRound(tables->rounds, m_rewardRound, settingsError))
        {
            CSOL_RejectGameRules("Scenario reward-round metadata is unavailable.");
            return;
        }
        // A shared Runtime already exists here. Its concrete death service
        // resolves Players dynamically after Activate; never capture null in
        // the constructor or guess map IDs before resource loading.
        m_runtime->deathServices = std::make_shared<CCSOLScenarioDeathServices>(m_runtime);
        m_runtime->monsterServices = std::make_shared<CCSOLScenarioMonsterServices>(m_runtime,
            m_runtime->session, m_runtime->deathServices);
        // Golden plans are combat resources. An unsupported nonempty plan
        // must not reject a map which currently enables selection only.
        if (scope != CSOLScenarioResourceScope::Combat) return;
        m_runtime->recoveryNotice = std::make_shared<CCSOLScenarioRecoveryNotice>(m_runtime);
        const auto deaths = m_runtime->deathServices;
        csol::scenario::ResourceError error;
        const bool loaded = deaths->Precache({g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile}, error);
        if (!OwnsRules()) return;
        if (!loaded)
        {
            CSOL_RejectGameRules(error.detail.message.empty() ? "Scenario death resources are unavailable." :
                error.detail.message.c_str());
            return;
        }
        if (!deaths->Golden().Diagnostic().empty())
            ALERT(at_console, "[CSOL] Golden JSON parse error; fresh empty plan retained: %s\n",
                deaths->Golden().Diagnostic().c_str());
        if (!OwnsRules()) return;
        csol::scenario::ConfigError assemblyError;
        if (!m_runtime->monsterServices->Assemble(m_runtime->monsterDamageServices, assemblyError) && OwnsRules())
            CSOL_RejectGameRules(assemblyError.message.empty() ? "Scenario monster services could not assemble." :
                assemblyError.message.c_str());
        if (OwnsRules()) m_boundGameplay = m_runtime->gameplay;
    }
    bool Activate()
    {
        if (!OwnsRules() || CSOL_RulesRejected()) return false;
        if (!m_runtime->session->Activate())
        {
            FailSession();
            return false;
        }
        if (!OwnsRules()) return false;
        m_runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(m_runtime->session);
        if (!m_runtime->players->Activate())
        {
            if (OwnsRules()) CSOL_RejectGameRules(m_runtime->players->Error().message.c_str());
            return false;
        }
        if (!OwnsRules()) return false;
        m_runtime->drops = std::make_shared<CCSOLScenarioDrops>(m_runtime->session, m_runtime->players);
        if (!m_runtime->drops->Activate())
        {
            if (OwnsRules()) CSOL_RejectGameRules("Scenario drop owner could not activate.");
            return false;
        }
        if (!OwnsRules()) return false;
        if (!UpdateSpawnCapacity(0)) return false;
        // A valid map can accept team/character selection before combat is
        // installed. Keep that state explicit: it cannot start a round or
        // respawn a combatant, and never claims that the map is playable.
        if (!m_runtime->gameplay || !m_runtime->waveEvents)
        {
            if (m_runtime->gameplay || m_runtime->session->ResourceScope() == CSOLScenarioResourceScope::Combat)
            {
                CSOL_RejectGameRules("Scenario combat services are only partially installed.");
                return false;
            }
            m_phase = Phase::Selecting;
            m_bGameStarted = false;
            m_bFreezePeriod = TRUE;
            m_bRoundTerminating = false;
            m_flRestartRoundTime = 0;
            ALERT(at_console, "[CSOL] Scenario character selection ready: CT capacity %d; combat unavailable.\n",
                m_iSpawnPointCount_CT);
            return true;
        }
        if (!m_runtime->gameplay->Activate()) return FailGameplay();
        m_phase = Phase::Waiting;
        m_bGameOver = false;
        m_bGameStarted = false;
        m_bRoundTerminating = false;
        m_flRestartRoundTime = 0;
        return true;
    }
    void ServerDeactivate() override
    {
        CSOLScenarioSdkScope scope(this);
        m_runtime->RequestClose();
        // No resource destruction inside a callback. The engine does not wait
        // for gameplay to unwind before beginning its private-data frees.
        CHalfLifeMultiplay::ServerDeactivate();
    }
    void ScenarioThink()
    {
        if (!SelectionReady() || m_completing || m_restarting || m_applyingFinish) return;
        m_VoiceGameMgr.Update(gpGlobals->frametime);
        if (!Ready()) return;
        CheckWinConditions();
        if (!Ready()) return;
        // MP runs the manager (including a due end-round drain batch) before
        // testing final-result and ordinary restart deadlines. Do not discard
        // that final batch by immediately rebuilding the next round here.
        if (m_phase == Phase::Playing || m_phase == Phase::Advancing || m_phase == Phase::Completing ||
            m_phase == Phase::FreePlay)
        {
            if (!m_runtime->gameplay->Update(*this, gpGlobals->time, m_aliveCt, *this))
            {
                FailGameplay();
                return;
            }
            if (!Ready()) return;
        }
        // ReAPI callbacks may schedule a new round. Enter its normal wrapper,
        // not this method recursively from a wave/trigger callback.
        if (m_flRestartRoundTime > 0 && gpGlobals->time >= m_flRestartRoundTime)
        {
            if (m_phase == Phase::Starting || m_phase == Phase::Advancing)
            {
                RestartRound();
                return;
            }
            if (m_phase == Phase::Completing)
            {
                ApplyFinishPolicy();
                return;
            }
        }
        if (!Ready() || (m_phase != Phase::Playing && m_phase != Phase::Advancing &&
            m_phase != Phase::Completing)) return;
        // MP's manager runs before thawing the rules. A freeze ending this
        // frame therefore produces its first wave tick on the following one.
        if (!m_bRoundTerminating && m_bFreezePeriod && gpGlobals->time >= m_freezeUntil)
        {
            const auto runtime = m_runtime;
            const auto session = runtime->session;
            const auto access = session->Map().EntityAccess();
            const auto round = session->Map().RoundGeneration();
            const auto current = [&]()
            {
                return Ready() && m_runtime == runtime && runtime->session == session && access->Allowed() &&
                    session->Map().EntityAccess() == access && session->Map().RoundGeneration() == round &&
                    !m_bRoundTerminating && !m_bFreezePeriod;
            };
            m_bFreezePeriod = FALSE;
            m_fRoundStartTime = m_fRoundStartTimeReal = gpGlobals->time;
            m_iRoundTimeSecs = 0; // Scenario time is sent by its own HUD owner.
            ForPlayers([&](CBasePlayer& player)
            {
                if (!current()) return;
                const EHANDLE handle(&player);
                const bool spawnable = Spawnable(player);
                if (!current() || handle.Get<CBaseEntity>() != &player || !spawnable) return;
                player.m_bCanShoot = true;
                player.ResetMaxSpeed();
                if (!current() || handle.Get<CBaseEntity>() != &player) return;
                player.SyncRoundTimer();
            });
        }
    }
    void ScenarioCheckWinConditions()
    {
        if (!SelectionReady() || m_completing || m_applyingFinish) return;
        const auto runtime = m_runtime;
        int aliveT{}, aliveCt{}, deadT{}, deadCt{};
        InitializePlayerCounts(aliveT, aliveCt, deadT, deadCt);
        if (!OwnsRules() || m_runtime != runtime) return;
        m_aliveCt = aliveCt;
        runtime->ctCounts = {aliveCt, aliveCt + deadCt, true};
        if (!Ready()) return;
        m_iNumSpawnableCT = 0;
        ForPlayers([&](CBasePlayer& player) { if (Spawnable(player)) ++m_iNumSpawnableCT; });
        if (!Ready()) return;
        if (m_phase == Phase::Waiting && m_iNumSpawnableCT > 0)
        {
            // The first eligible CT is still waiting to spawn. Testing alive
            // count here would prevent the first round from ever starting.
            m_phase = Phase::Starting;
            m_bCompleteReset = true;
            m_bRoundTerminating = true;
            m_flRestartRoundTime = gpGlobals->time + 3.0f;
        }
        else if (m_phase == Phase::Playing && !m_bFreezePeriod && m_aliveCt == 0)
        {
            // This is a scenario failure, never a native terrorist victory.
            // Continuation/final-result policy is owned by the scenario mode.
            m_phase = Phase::Failed;
            m_bRoundTerminating = true;
            m_flRestartRoundTime = 0;
        }
    }
    void UpdateDamageScale()
    {
        if (!SelectionReady() || !IsGameStarted() || !(gpGlobals->time > m_damageScaleAt)) return;
        // MP 10828E7C: after the root frame, independently of freeze/result
        // gates. Schedule once from now rather than replaying missed minutes.
        m_damageScaleAt = gpGlobals->time + 60.0f;
        const auto session = m_runtime->session;
        const auto roundGeneration = session->Map().RoundGeneration();
        const auto players = session->Players();
        int count{};
        const bool counted = players && players->CountDamageScalePlayers(count);
        if (g_pGameRules != this || !SelectionReady() || m_runtime->session != session ||
            session->Map().RoundGeneration() != roundGeneration || session->Players() != players) return;
        if (!counted)
        {
            CSOL_RejectGameRules("Scenario damage scaling requires the player registry.");
            return;
        }
        m_damageScale = count < 2 ? 1.0f : count < 4 ? 0.97f : count < 7 ? 0.94f : 0.91f;
    }
    bool ReadDamageScale(const CCSOLScenarioMap& map, float& value) const
    {
        if (!Ready() || &m_runtime->session->Map() != &map) return false;
        value = m_damageScale;
        return true;
    }
    void ScenarioRestartRound()
    {
        if (!Ready() || m_restarting || m_completing || m_applyingFinish || m_phase == Phase::FreePlay || m_phase == Phase::Complete ||
            m_phase == Phase::Completing || m_runtime->gameplay->Busy()) return;
        struct RestartScope { bool& flag; ~RestartScope() { flag = false; } } scope{m_restarting};
        m_restarting = true;
        if (m_phase == Phase::Advancing && !m_pendingRound)
        {
            CSOL_RejectGameRules("Scenario next stage has no committed completion.");
            return;
        }
        const auto runtime = m_runtime;
        const auto session = runtime->session;
        const auto gameplay = runtime->gameplay;
        const auto events = runtime->waveEvents;
        const auto access = session->Map().EntityAccess();
        const auto previousRound = session->Map().RoundGeneration();
        const auto pending = m_pendingRound;
        const auto nextRound = m_phase == Phase::Advancing ? *pending : m_roundIndex;
        const auto ownerCurrent = [&]()
        {
            return Ready() && m_runtime == runtime && runtime->session == session &&
                runtime->gameplay == gameplay && runtime->waveEvents == events &&
                session->Map().EntityAccess() == access && access->Allowed();
        };
        const auto oldRoundCurrent = [&]()
        { return ownerCurrent() && session->Map().RoundGeneration() == previousRound && m_pendingRound == pending; };
        ReadMultiplayCvars();
        if (!oldRoundCurrent()) return;
        const float freeze = static_cast<float>(m_iIntroRoundTime);
        const float now = gpGlobals->time;
        if (!std::isfinite(now) || !std::isfinite(freeze) || freeze < 0)
        {
            CSOL_RejectGameRules("Invalid scenario round clock.");
            return;
        }
        m_bGameOver = false;
        m_bGameStarted = true;
        m_bFreezePeriod = TRUE;
        m_bRoundTerminating = false;
        m_iRoundWinStatus = WINSTATUS_NONE;
        m_flRestartRoundTime = 0;
        m_fRoundStartTime = m_fRoundStartTimeReal = now;
        m_iRoundTimeSecs = m_iIntroRoundTime;
        m_freezeUntil = now + freeze;
        if (!UpdateSpawnCapacity(nextRound) || !oldRoundCurrent()) return;
        const bool started = gameplay->BeginRound(nextRound, now, freeze, *this);
        if (!ownerCurrent()) return;
        if (!started)
        {
            FailGameplay();
            return;
        }
        if (session->Map().RoundGeneration() != previousRound + 1 ||
            session->Map().RoundIndex() != nextRound || m_pendingRound != pending) return;
        m_roundIndex = nextRound;
        m_pendingRound.reset();
        m_completionLocked = false;
        m_phase = Phase::Playing;
        const auto round = session->Map().RoundGeneration();
        const auto current = [&]()
        { return ownerCurrent() && session->Map().RoundGeneration() == round && m_phase == Phase::Playing; };
        // BeginRound has now installed PlayerSpawn/TRG/waves. Use the real
        // player round transaction; teleporting alone leaves old life state.
        ForPlayers([&](CBasePlayer& player)
        {
            if (!current()) return;
            const EHANDLE handle(&player);
            const bool spawnable = Spawnable(player);
            if (!current() || handle.Get<CBaseEntity>() != &player || !spawnable) return;
            if (!PreparePlayer(player) || !current() || handle.Get<CBaseEntity>() != &player) return;
            player.m_iNumSpawns = 0;
            player.m_bTeamChanged = false;
            player.RoundRespawn();
        });
        if (!current()) return;
        // CSO cleans/restarts map entities AFTER player respawns. Never call
        // CHalfLifeMultiplay::RestartRound, whose FIXES order is the reverse.
        CleanUpMap();
        if (!current()) return;
        const bool unprotected = runtime->SetClearPointProtection(false);
        if (!current()) return;
        if (!unprotected)
        {
            if (Ready()) CSOL_RejectGameRules("Scenario completion protection could not be removed.");
            return;
        }
        m_bCompleteReset = false;
        CheckWinConditions();
        if (!current()) return;
        // The original post-restart stage runs after respawns/map cleanup.
        m_runtime->monsterLogicStopped = false;
        // Its first interval is five minutes; subsequent intervals are one.
        m_damageScale = 1.0f;
        m_damageScaleAt = gpGlobals->time + 300.0f;
    }
    bool PrepareSpawn(CBasePlayer& player)
    {
        // ClientPutInServer calls Spawn before the player has a team/model.
        // Consume that equipment stage without requiring a combat loadout.
        if (player.m_bJustConnected || player.m_iTeam == UNASSIGNED || player.m_iTeam == SPECTATOR)
            return true;
        if (!Ready() || !PreparePlayer(player)) return true;
        const auto players = m_runtime->session->Players();
        const auto upgrades = players ? players->Upgrades(EHANDLE(&player)) : nullptr;
        if (!upgrades || !upgrades->ApplySpawnHealth())
        {
            if (OwnsRules()) CSOL_RejectGameRules("Scenario spawn health could not be applied.");
            return true;
        }
        return false; // Continue the native equip/model stage inside its hook.
    }
    bool CanRespawn(CBasePlayer& player) const
    {
        if (!Ready()) return false;
        const auto phase = m_phase;
        const auto round = m_runtime->session->Map().RoundGeneration();
        const EHANDLE handle(&player);
        const bool spawnable = Spawnable(player);
        if (!Ready() || m_phase != phase || m_runtime->session->Map().RoundGeneration() != round ||
            handle.Get<CBaseEntity>() != &player || !spawnable) return false;
        return phase == Phase::FreePlay || (phase == Phase::Playing && !m_bRoundTerminating &&
            (m_restarting || (m_bFreezePeriod && player.m_iNumSpawns == 0)));
    }
    edict_t* ObserverSpawn()
    {
        // An observer only needs a camera location. Do not run combat
        // occupancy clearing or consume a player-spawn selection attempt.
        const auto* tables = m_runtime->session->Map().Tables();
        csol::scenario::RoundSettings settings;
        csol::scenario::ConfigError error;
        const auto round = m_runtime->session->Map().RoundIndex().value_or(0);
        if (!tables || !csol::scenario::ReadRoundSettings(tables->rounds, round, settings, error))
        {
            CSOL_RejectGameRules("Scenario observer has no current spawn group.");
            return nullptr;
        }
        const auto session = m_runtime->session;
        auto* spot = session->Map().SpawnPoints().SelectPlayerSpawn(CT, settings.playerSpawn, 0);
        if (g_pGameRules != this || !SelectionReady()) return nullptr;
        if (!spot) CSOL_RejectGameRules("Scenario observer spawn is unavailable.");
        return spot ? spot->edict() : nullptr;
    }
    edict_t* SelectSpawn(CBasePlayer& player)
    {
        if (!SelectionReady()) return nullptr;
        if (!Ready() || player.m_bJustConnected || player.m_iTeam == UNASSIGNED ||
            player.m_iTeam == SPECTATOR || (player.pev->flags & FL_SPECTATOR) ||
            m_runtime->session->Map().State() == CSOLScenarioMapState::EntitiesCollected)
        {
            // ReGameDLL also asks for a spot when GetIntoGame enters observer
            // mode before the first round. This is a camera location, not a
            // combat respawn: do not require BeginRound or clear monsters.
            return ObserverSpawn();
        }
        auto* spot = m_runtime->gameplay->SelectSpawn(player);
        if (!spot) { FailGameplay(); return nullptr; }
        return spot->edict();
    }
    bool HoldSpawn(CBasePlayer& player)
    {
        if (!SelectionReady() || m_phase != Phase::Selecting ||
            player.m_iTeam == UNASSIGNED) return false;
        // Normal GetIntoGame uses FPlayerCanRespawn and StartObserver. Keep
        // direct ReAPI/forced Spawn calls from bypassing the same combat gate.
        const EHANDLE handle(&player);
        auto* spot = ObserverSpawn();
        if (g_pGameRules != this || !SelectionReady()) return true;
        if (!spot || handle.Get<CBaseEntity>() != &player) return true;
        Vector origin = spot->v.origin, angles = spot->v.angles;
        player.pev->deadflag = DEAD_RESPAWNABLE;
        player.pev->flags &= (FL_PROXY | FL_FAKECLIENT);
        player.pev->flags |= FL_CLIENT | FL_SPECTATOR;
        player.m_bNotKilled = false;
        player.StartObserver(origin, angles);
        return true;
    }
    void CharacterSelected(CBasePlayer& player)
    {
        if (!SelectionReady() || m_phase != Phase::Selecting) return;
        ClientPrint(player.pev, HUD_PRINTCENTER,
            "角色已选择；大灾变战斗尚未就绪，当前仅可观察地图。");
    }
    bool TeamFull(int team) const
    {
        if (!SelectionReady() || team != CT) return true;
        // Native cached counts are updated by CheckWinConditions. A selection
        // can arrive between those calls, so include provisional CT selections
        // directly, including players still choosing their appearance.
        const auto session = m_runtime->session;
        int count{};
        for (int index = 1; index <= gpGlobals->maxClients; ++index)
        {
            auto* entity = INDEXENT(index);
            if (g_pGameRules != this || !SelectionReady()) return true;
            if (!entity || entity->free || (entity->v.flags & (FL_DORMANT | FL_KILLME))) continue;
            auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity));
            if (player && !player->has_disconnected && player->m_iTeam == CT) ++count;
        }
        return count >= m_iSpawnPointCount_CT;
    }
    bool Selecting() const { return SelectionReady() && m_phase == Phase::Selecting; }
    bool CanBuyThroughoutScenario() const
    {
        if (!Ready() || m_restarting || m_completing || m_applyingFinish) return false;
        return m_phase == Phase::Waiting || m_phase == Phase::Starting ||
            m_phase == Phase::Playing || m_phase == Phase::Advancing ||
            m_phase == Phase::Completing || m_phase == Phase::FreePlay;
    }
    std::uint32_t PassMask(int playerIndex) const
    {
        if (!SelectionReady() || playerIndex < 1 || playerIndex > gpGlobals->maxClients) return 0;
        const auto access = EntityAccess();
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(playerIndex, access));
        if (!access->Allowed() || !player) return 0;
        const EHANDLE handle(player);
        const bool alive = player->IsAlive() != FALSE;
        if (!access->Allowed() || handle.Get<CBaseEntity>() != player || player->has_disconnected || !alive ||
            player->m_iTeam != CT || (player->pev->flags & (FL_SPECTATOR | FL_DORMANT | FL_KILLME))) return 0;
        std::uint32_t peers{};
        for (int index = 1; index <= gpGlobals->maxClients && index <= 32; ++index)
        {
            if (index == playerIndex) continue;
            auto* peer = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
            if (!access->Allowed() || handle.Get<CBaseEntity>() != player) return 0;
            if (!peer) continue;
            const EHANDLE peerHandle(peer);
            const bool peerAlive = peer->IsAlive() != FALSE;
            if (!access->Allowed() || handle.Get<CBaseEntity>() != player) return 0;
            if (peerHandle.Get<CBaseEntity>() == peer && !peer->has_disconnected && peer->m_iTeam == player->m_iTeam && peerAlive &&
                !(peer->pev->flags & (FL_SPECTATOR | FL_DORMANT | FL_KILLME)))
                peers |= std::uint32_t{1} << (index - 1);
        }
        return peers;
    }
    void RemovePlayer(CBasePlayer& player)
    {
        if (m_runtime->players) m_runtime->players->Remove(EHANDLE(&player));
        else m_runtime->session->RemovePlayer(EHANDLE(&player));
    }
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!OwnsRules() || !m_runtime->session->Map().EntityAccessAllowed()) { error = {}; return false; }
        if (CSOL_RulesRejected()) { error.message = CSOL_RulesRejectionReason(); return false; }
        const auto gameplay = m_runtime->gameplay;
        if (gameplay != m_boundGameplay.lock()) { error = {}; return false; }
        if (gameplay && !gameplay->Error().message.empty()) { error = gameplay->Error(); return false; }
        if (gameplay && !gameplay->Active()) { error = {}; return false; }
        const auto events = m_runtime->waveEvents;
        if (!events || events != m_boundWaveEvents.lock()) { error = {}; return false; }
        return events->Check(error);
    }
    void WaveActivated(std::uint16_t id) override
    {
        const auto events = m_runtime->waveEvents;
        csol::scenario::ConfigError status;
        if (Check(status)) events->WaveActivated(id);
    }
    void BeforeSpatialWaveEnd(std::uint16_t id) override
    {
        const auto events = m_runtime->waveEvents;
        csol::scenario::ConfigError status;
        if (Check(status)) events->BeforeSpatialWaveEnd(id);
    }
    void WavesChanged(std::span<const std::uint16_t> active) override
    {
        const auto events = m_runtime->waveEvents;
        csol::scenario::ConfigError status;
        if (Check(status)) events->WavesChanged(active);
    }
    void GoalReached() override
    {
        csol::scenario::ConfigError status;
        if (!Check(status) || !Ready() || m_restarting || m_completing ||
            m_phase != Phase::Playing || m_completionLocked) return;
        const auto runtime = m_runtime;
        const auto session = runtime->session;
        const auto gameplay = runtime->gameplay;
        const auto events = runtime->waveEvents;
        const auto access = session->Map().EntityAccess();
        const auto round = session->Map().RoundGeneration();
        const auto* tables = m_runtime->session->Map().Tables();
        if (!tables || !tables->mapId || m_roundIndex >= tables->rounds.rows.size() || !gpGlobals ||
            !std::isfinite(gpGlobals->time))
        {
            CSOL_RejectGameRules("Scenario completion has no current round.");
            return;
        }
        const auto current = [&]()
        {
            csol::scenario::ConfigError error;
            return Ready() && m_runtime == runtime && runtime->session == session &&
                runtime->gameplay == gameplay && runtime->waveEvents == events &&
                session->Map().EntityAccess() == access && access->Allowed() &&
                session->Map().RoundGeneration() == round && session->Map().Tables() == tables && Check(error);
        };
        const auto accepted = [&](bool value, const char* error)
        {
            if (!current()) return false;
            if (!value) CSOL_RejectGameRules(error);
            return value;
        };
        m_completionLocked = true;
        m_completing = true;
        struct CompletionScope { bool& flag; ~CompletionScope() { flag = false; } } completing{m_completing};
        const auto messages = session->Messages();
        if (!accepted(messages.SendRoundClearSound(), "Scenario round-clear sound message failed.")) return;
        m_iNumCTWins = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(m_iNumCTWins) + 1u));
        const bool final = m_roundIndex + 1 >= tables->rounds.rows.size();
        const auto nextRound = std::min(m_roundIndex + 1, tables->rounds.rows.size() - 1);
        float delay = 7.0f;
        switch (*tables->mapId)
        {
        case 101: case 119: case 134: case 255: case 152: delay = 30.0f; break;
        default: break;
        }
        if (final) delay = m_shortFinalDelay ? 5.0f : 30.0f;
        m_phase = final ? Phase::Completing : Phase::Advancing;
        if (final && !accepted(messages.SendAllRoundsClear(gmsgTextMsg),
            "Scenario final-clear text message failed.")) return;
        TerminateRound(delay, WINSTATUS_CTS);
        // The loaded Map/TRG remains on the old generation until the deadline.
        // Only a non-final completion publishes a pending BeginRound target.
        if (!final) m_pendingRound = nextRound;
        if (!accepted(messages.SendNativeTeamScore(gmsgTeamScore, true, m_iNumCTWins), "Scenario CT score message failed.")) return;
        if (!accepted(messages.SendNativeTeamScore(gmsgTeamScore, false, m_iNumTerroristWins), "Scenario T score message failed.")) return;
        const bool reward = m_rewardRound != 0 && static_cast<std::int64_t>(nextRound) == m_rewardRound;
        const bool notified = reward ? messages.SendWaveEvent(std::array<std::uint8_t, 4>{0x1B, 7, 0x83, 0}) :
            messages.SendRoundOutcome(true);
        if (!accepted(notified, "Scenario success message failed.")) return;
        if (!accepted(gameplay->ScheduleRoundDrain(gpGlobals->time), "Scenario completion could not schedule monster removal.")) return;
        const bool pastReward = m_rewardRound != 0 && static_cast<std::int64_t>(nextRound) + 1 > m_rewardRound;
        if (!accepted(messages.SendRoundRewardState(pastReward), "Scenario reward-state message failed.")) return;
        // Original113FC5C0 walks living apache entities through Killed. Keep
        // an edict cursor, not a C++ pointer, across arbitrary death callbacks.
        if (!g_engfuncs.pfnFindEntityByString || !g_engfuncs.pfnEntOffsetOfPEntity)
        {
            accepted(false, "Scenario round cleanup cannot enumerate target entities.");
            return;
        }
        edict_t* cursor{};
        for (;;)
        {
            // ReHLDS FindEntityByString/NUM_FOR_EDICT only use this address
            // as a slot cursor and resume at index+1, even after private free.
            auto* found = FIND_ENTITY_BY_STRING(cursor, "classname", "monster_apache");
            if (!current()) return;
            if (!found) break;
            cursor = found;
            const EHANDLE handle(found);
            if (found->free) continue;
            const int offset = OFFSET(found);
            if (!current()) return;
            if (handle.Get() != found || found->free) continue;
            if (!offset) break;
            auto* target = CBaseEntity::Instance(found);
            if (!target) { accepted(false, "Scenario round cleanup found a target without private data."); return; }
            if (target->pev->health > 0 && !(target->pev->flags & FL_KILLME))
            {
                target->Killed(nullptr, GIB_NORMAL);
                if (!current()) return;
            }
        }
        // FinishRound belongs to these rules, not the wave-observer registry.
        // Original final HUD/voice/account consumers and special collections
        // remain separate work; no successful account award is manufactured.
    }
    bool CompleteMonsterClearPoint(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error)
    {
        const auto runtime = m_runtime;
        const auto session = runtime->session;
        auto& map = session->Map();
        const auto access = map.EntityAccess();
        const auto round = map.RoundGeneration();
        const auto current = [&]()
        {
            return Ready() && m_runtime == runtime && runtime->session == session &&
                map.EntityAccess() == access && access->Allowed() && map.RoundGeneration() == round &&
                call.BelongsTo(map) && call.Current();
        };
        if (!current() || !Check(error)) return false;
        // The final drain still runs full deaths, but free play cannot fire
        // map progress again or restore the just-removed clearpoint shield.
        if (m_phase == Phase::FreePlay) return true;
        const auto fail = [&](const char* reason)
        {
            if (current() && error.message.empty()) error.message = reason;
            return false;
        };
        // Root effects and current-weapon statistics may replace the type.
        // Resolve it here, after both callbacks, when the death service calls.
        const auto* type = call.Current()->Type();
        bool reached{};
        if (!type || !map.AddMonsterClearPoint(type->clearPoint, reached))
            return fail("scenario death progress is invalid");
        if (!reached) return true;
        const auto found = CSOL_LookupScenarioEntity(nullptr, "trigger_clearpoint", access);
        if (!current()) return false;
        if (!found.valid) return fail("scenario clearpoint lookup was cancelled");
        auto* entity = found.entity;
        if (!entity)
        {
            // zs_deadend takes this branch. FinishRound owns its completion
            // lock; progress remains at 100, including on repeated deaths.
            GoalReached();
            return current() && Check(error);
        }
        auto* trigger = dynamic_cast<CCSOLScenarioClearPoint*>(entity);
        if (!trigger || (trigger->pev->flags & FL_KILLME))
            return fail("scenario clearpoint classname has no valid trigger implementation");
        const EHANDLE handle(trigger);
        const auto revision = trigger->ControlRevision();
        auto* attacker = call.Attacker();
        if (attacker)
        {
            auto* edict = attacker->pev ? attacker->pev->pContainingEntity : nullptr;
            if (!edict) attacker = nullptr;
            else
            {
                const EHANDLE actor(edict);
                if (!g_engfuncs.pfnEntOffsetOfPEntity)
                    return fail("scenario clearpoint activator validation is unavailable");
                const bool hasOffset = OFFSET(edict) != 0;
                if (!current()) return false;
                if (handle.Get<CBaseEntity>() != trigger || (trigger->pev->flags & FL_KILLME) ||
                    trigger->ControlRevision() != revision) return false;
                // 1152C520 uses a bit test for dormancy here, unlike the
                // protection toggle's exact-word player exclusion.
                if (!hasOffset || actor.Get<CBaseEntity>() != attacker || call.Attacker() != attacker)
                    attacker = nullptr;
                else if (!attacker->pev || attacker->pev->pContainingEntity != edict ||
                    (attacker->pev->flags & (FL_DORMANT | FL_KILLME))) attacker = nullptr;
            }
        }
        const bool protect = trigger->OnControls(attacker ? attacker->pev : nullptr) != FALSE;
        if (!current()) return false;
        // Destruction or explicit re-Spawn cancels the old invocation.
        if (handle.Get<CBaseEntity>() != trigger || (trigger->pev->flags & FL_KILLME) ||
            trigger->ControlRevision() != revision) return false;
        if (protect)
        {
            const bool applied = runtime->SetClearPointProtection(true);
            if (!current()) return false;
            if (!applied) return fail("scenario clearpoint protection could not be applied");
        }
        // Presence of the trigger consumes progress even with nogodmode.
        // Do not re-test progress after nested valid death callbacks.
        return map.ConsumeMonsterClearPoint(round);
    }
    void ApplyMovement(CBasePlayer& player, float weaponSpeed)
    {
        // GetIntoGame resets speed before JOINED and before the real Spawn;
        // the initial UNASSIGNED Spawn also has no selected character yet.
        if (player.m_bJustConnected || player.m_iTeam == UNASSIGNED || player.m_iTeam == SPECTATOR ||
            player.m_iJoiningState != JOINED || (player.pev->flags & FL_SPECTATOR)) return;
        const auto players = m_runtime->session->Players();
        const auto state = players ? players->Upgrades(EHANDLE(&player)) : nullptr;
        const bool applied = state && state->ApplyMovement(weaponSpeed);
        if (g_pGameRules == this && !applied)
            CSOL_RejectGameRules("Scenario player movement upgrade state is unavailable.");
    }
private:
    void ApplyFinishPolicy()
    {
        if (!Ready() || m_phase != Phase::Completing || m_applyingFinish || m_runtime->gameplay->Busy()) return;
        const auto runtime = m_runtime;
        const auto session = runtime->session;
        const auto gameplay = runtime->gameplay;
        const auto access = session->Map().EntityAccess();
        const auto round = session->Map().RoundGeneration();
        const auto current = [&]()
        {
            return Ready() && m_runtime == runtime && runtime->session == session && runtime->gameplay == gameplay &&
                session->Map().EntityAccess() == access && access->Allowed() && session->Map().RoundGeneration() == round;
        };
        m_applyingFinish = true;
        struct FinishScope { bool& flag; ~FinishScope() { flag = false; } } scope{m_applyingFinish};
        // Consume the deadline before any engine callback. FreePlay and
        // Complete both reject RestartRound and repeated completion.
        m_phase = Phase::Complete;
        m_flRestartRoundTime = 0;
        CSOLScenarioFinishPolicy policy;
        const char* warning{};
        if (!CSOL_RequestedScenarioFinishPolicy(policy))
            warning = "Invalid finish policy or next mode; retaining this scenario for free play.";
        else if (policy.action == CSOLScenarioFinishAction::ChangeMap)
        {
            // A syntactically valid filename is insufficient: ordinary maps
            // and unimplemented scenario managers cannot be automatic targets.
            bool valid{};
            if (g_engfuncs.pfnLoadFileForMe && g_engfuncs.pfnFreeFile &&
                g_engfuncs.pfnIsMapValid && g_engfuncs.pfnChangeLevel)
            {
                int length{};
                const auto release = g_engfuncs.pfnFreeFile;
                std::unique_ptr<unsigned char, decltype(release)> bytes(
                    g_engfuncs.pfnLoadFileForMe("resource/maplist.csv", &length), release);
                if (!current()) return;
                if (bytes && length > 0 && length <= 8 * 1024 * 1024)
                {
                    csol::scenario::MapCatalog catalog;
                    csol::scenario::ConfigError error;
                    int subtype{};
                    valid = csol::scenario::ReadMapCatalog(
                        std::string_view(reinterpret_cast<const char*>(bytes.get()), length), catalog, error) &&
                        catalog.ResolveScenarioType(policy.nextMap.data(), subtype, error) &&
                        catalog.UniqueMapId(policy.nextMap.data()).has_value() &&
                        CCSOLScenarioGameplay::SupportsMap(policy.nextMap.data(), subtype);
                }
            }
            if (!current()) return;
            if (valid)
            {
                const bool exists = IS_MAP_VALID(policy.nextMap.data()) != FALSE;
                if (!current()) return;
                if (exists)
                {
                    // ReHLDS queues this once per spawncount. Its void return
                    // is a request, not evidence that a new map has loaded.
                    m_bGameOver = true;
                    CHANGE_LEVEL(policy.nextMap.data(), nullptr);
                    return;
                }
            }
            warning = "Next scenario map is unavailable or unsupported; retaining this map for free play.";
        }
        if (!current()) return;
        m_phase = Phase::FreePlay;
        m_bGameOver = false;
        m_bFreezePeriod = FALSE;
        m_bGameStarted = true;
        // Retain the end-round gate: Gameplay drains old monsters without
        // running TRG/waves, and their ordinary damage remains suppressed.
        m_bRoundTerminating = true;
        const bool unprotected = runtime->SetClearPointProtection(false);
        if (!current()) return;
        if (!unprotected)
        {
            CSOL_RejectGameRules("Scenario free-play protection could not be removed.");
            return;
        }
        ForPlayers([&](CBasePlayer& player)
        {
            if (!current() || m_phase != Phase::FreePlay) return;
            const EHANDLE handle(&player);
            const bool spawnable = Spawnable(player);
            if (!current() || handle.Get<CBaseEntity>() != &player || !spawnable) return;
            if (player.pev->deadflag != DEAD_NO)
            {
                if (!PreparePlayer(player) || !current() || handle.Get<CBaseEntity>() != &player) return;
                player.RoundRespawn();
                return;
            }
            player.m_bCanShoot = true;
            player.ResetMaxSpeed();
            if (!current() || handle.Get<CBaseEntity>() != &player) return;
            player.SyncRoundTimer();
        });
        if (!current()) return;
        if (warning && g_engfuncs.pfnAlertMessage) ALERT(at_console, "[CSOL] %s\n", warning);
    }
    bool OwnsRules() const { return g_pGameRules == this && !m_runtime->Closing(); }
    bool SelectionReady() const
    {
        return !m_runtime->Closing() && g_pGameRules == this && !CSOL_RulesRejected() && m_phase != Phase::Loading &&
            m_runtime->session->State() == CSOLScenarioSessionState::Active && m_runtime->players &&
            m_runtime->players->Active() && m_iSpawnPointCount_CT > 0;
    }
    bool Ready() const
    {
        return SelectionReady() && m_phase != Phase::Selecting &&
            m_runtime->gameplay && m_runtime->gameplay->Active() && m_runtime->waveEvents;
    }
    bool Spawnable(CBasePlayer& player) const
    {
        int classId{};
        return player.m_iTeam == CT && player.m_iJoiningState == JOINED &&
            !player.m_bJustConnected && !player.has_disconnected &&
            !(player.pev->flags & (FL_DORMANT | FL_KILLME)) &&
            player.m_iMenu != Menu_ChooseAppearance && CSOL_CharacterScenarioClass(&player, classId, EntityAccess()) &&
            g_pGameRules == this && HasCharacterClass(classId);
    }
    template<class Action> void ForPlayers(Action action)
    {
        if (!Ready() || !gpGlobals) return;
        const auto runtime = m_runtime;
        const auto session = runtime->session;
        const auto players = runtime->players;
        const auto gameplay = runtime->gameplay;
        const auto access = session->Map().EntityAccess();
        const auto round = session->Map().RoundGeneration();
        const int clientCount = gpGlobals->maxClients;
        const auto current = [&]()
        {
            return Ready() && m_runtime == runtime && runtime->session == session && runtime->players == players &&
                runtime->gameplay == gameplay && access->Allowed() && session->Map().EntityAccess() == access &&
                session->Map().RoundGeneration() == round;
        };
        for (int index = 1; index <= clientCount && current(); ++index)
        {
            auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
            if (!current()) return;
            if (player && !(player->pev->flags & (FL_DORMANT | FL_KILLME))) action(*player);
        }
    }
    bool PreparePlayer(CBasePlayer& player)
    {
        const auto runtime = m_runtime;
        const auto players = runtime->players;
        const auto session = runtime->session;
        const auto access = session->Map().EntityAccess();
        const auto round = session->Map().RoundGeneration();
        const EHANDLE handle(&player);
        const bool prepared = players && players->Prepare(player);
        if (!OwnsRules() || m_runtime != runtime || runtime->players != players || runtime->session != session ||
            !access->Allowed() || session->Map().EntityAccess() != access || session->Map().RoundGeneration() != round ||
            handle.Get<CBaseEntity>() != &player) return false;
        if (prepared) return true;
        if (!players) CSOL_RejectGameRules("Scenario player authority is unavailable.");
        else if (!players->Error().message.empty()) CSOL_RejectGameRules(players->Error().message.c_str());
        return false;
    }
    bool UpdateSpawnCapacity(std::size_t round)
    {
        const auto* tables = m_runtime->session->Map().Tables();
        csol::scenario::RoundSettings settings;
        csol::scenario::ConfigError error;
        if (!tables || !csol::scenario::ReadRoundSettings(tables->rounds, round, settings, error))
        {
            CSOL_RejectGameRules("Scenario team capacity has no current round settings.");
            return false;
        }
        const auto count = m_runtime->session->Map().SpawnPoints().PlayerPointCount(CT, settings.playerSpawn);
        if (!count || count > 128)
        {
            CSOL_RejectGameRules("Scenario CT spawn group is empty or exceeds its point limit.");
            return false;
        }
        // A spawn group is a set of reusable locations, not a player limit.
        // The scenario selector tries clear points then its original fallback;
        // allied movement is handled independently on server and client.
        m_iSpawnPointCount_CT = std::min(gpGlobals->maxClients, 32);
        m_iSpawnPointCount_Terrorist = 0;
        m_bMapHasCameras = UTIL_CountEntities("trigger_camera");
        m_bLevelInitialized = true;
        return OwnsRules() && !CSOL_RulesRejected();
    }
    bool FailGameplay()
    {
        if (!OwnsRules()) return false;
        if (!m_runtime->gameplay)
        {
            CSOL_RejectGameRules("Scenario gameplay owner is unavailable.");
            return false;
        }
        // Busy entry and a callback which retires the current operation return
        // false without an error. They must stop this caller, not reject the
        // still-current rules or poison a replacement dispatcher generation.
        const auto& error = m_runtime->gameplay->Error().message;
        if (!error.empty()) CSOL_RejectGameRules(error.c_str());
        return false;
    }
    void FailSession()
    {
        if (!OwnsRules()) return;
        const auto& error = m_runtime->session->Error();
        ALERT(at_error, "CSOL scenario resource '%s': %s\n",
            error.path.c_str(), error.detail.message.c_str());
        if (!OwnsRules()) return;
        CSOL_RejectGameRules(error.detail.message.c_str());
    }
    std::shared_ptr<CCSOLScenarioRuntime> m_runtime{std::make_shared<CCSOLScenarioRuntime>()};
    std::weak_ptr<csol::scenario::TriggerWaveEvents> m_boundWaveEvents;
    std::weak_ptr<CCSOLScenarioGameplay> m_boundGameplay;
    Phase m_phase{Phase::Loading};
    std::size_t m_roundIndex{};
    std::optional<std::size_t> m_pendingRound;
    std::int32_t m_rewardRound{};
    bool m_shortFinalDelay{};
    int m_aliveCt{};
    float m_freezeUntil{};
    float m_damageScale{1.0f}, m_damageScaleAt{};
    bool m_restarting{}, m_completionLocked{}, m_completing{}, m_applyingFinish{};
};
}

CSOLScenarioSdkScope::CSOLScenarioSdkScope() : CSOLScenarioSdkScope(g_pGameRules)
{
    // FreeGameRules may already have unpublished the map. Entry without a raw
    // global pointer must still remember that its entity permission is closed.
    if (!g_pGameRules && entityRules)
    {
        m_record = entityRules;
        m_rules = m_record->rules;
        ++m_record->runtime->m_operations;
        m_access = m_record->runtime->session->Map().EntityAccess();
    }
}

CSOLScenarioSdkScope::CSOLScenarioSdkScope(CGameRules* rules)
{
    ++sdkDepth;
    Bind(rules);
}

void CSOLScenarioSdkScope::Bind(CGameRules* rules)
{
    if (m_record) --m_record->runtime->m_operations;
    m_rules = rules;
    m_record = FindScenarioRules(rules);
    m_access.reset();
    if (m_record)
    {
        ++m_record->runtime->m_operations;
        m_access = m_record->runtime->session->Map().EntityAccess();
    }
}

CSOLScenarioSdkScope::~CSOLScenarioSdkScope()
{
    if (m_record) --m_record->runtime->m_operations;
    --sdkDepth;
    // In particular, OnFree returns BEFORE the engine releases private data.
    // Never turn a scope destructor into a collection boundary.
}

bool CSOLScenarioSdkScope::Current() const
{
    if (g_pGameRules != m_rules) return false;
    if (!m_record) return true;
    return !m_record->retired && !m_record->runtime->Closing() && m_access &&
        m_access->Allowed() && m_access == m_record->runtime->session->Map().EntityAccess();
}

void CSOLScenarioSdkScope::RequestClose()
{
    if (m_record) m_record->runtime->RequestClose();
}

CSOLScenarioWorldScope::CSOLScenarioWorldScope(CSOLScenarioSdkScope& scope, bool world, const edict_s* entity,
    bool attachToDispatch)
    : m_scope(scope), m_entity(entity), m_world(world)
{
    if (!m_world) return;
    if (attachToDispatch && installingWorld && installingWorld->m_entity == entity)
    {
        m_parent = installingWorld;
        return; // DispatchSpawn -> CWorld::Spawn -> Precache, same transaction.
    }
    m_serial = ++worldSerial;
    m_previous = installingWorld;
    installingWorld = this;
}

CSOLScenarioWorldScope::~CSOLScenarioWorldScope()
{
    if (m_world && !m_parent) installingWorld = m_previous;
}

bool CSOLScenarioWorldScope::Current() const
{
    if (!m_world) return m_scope.Current();
    if (m_parent) return m_parent->Current();
    return !m_failed && m_serial == worldSerial && (!m_installed || m_scope.Current());
}

bool CSOLScenarioWorldScope::Install()
{
    if (m_parent) return m_parent->Install();
    if (!m_world || !Current()) return false;
    if (m_installed || m_installing) { m_failed = true; return false; }
    m_installing = true;
    struct InstallResult
    {
        bool& failed; bool& installing; bool success{};
        ~InstallResult() { failed = !success; installing = false; }
    } result{m_failed, m_installing};
    FreeGameRules(&g_pGameRules);
    if (!Current() || g_pGameRules) return false;
    auto* candidate = InstallGameRules();
    if (!candidate || !Current() || g_pGameRules)
    {
        // A nested world owns its published rules. Dispose only the returned,
        // unpublished candidate; never overwrite the nested world's pointer.
        if (candidate != g_pGameRules) FreeGameRules(&candidate);
        return false;
    }
    g_pGameRules = candidate;
    entityRules = FindScenarioRules(candidate);
    m_scope.Bind(candidate);
    CSOL_PrecacheGameRules();
    const auto record = FindScenarioRules(candidate);
    if (m_failed || m_serial != worldSerial || g_pGameRules != candidate ||
        (record && record->runtime->Closing())) return false;
    // BeginMap created a fresh token during Precache. Bind it deliberately;
    // ordinary dispatches never refresh their captured token on cancellation.
    m_scope.Bind(candidate);
    m_installed = true;
    result.success = Current();
    return result.success;
}

bool CSOL_InstallWorldRules()
{
    return installingWorld && installingWorld->Install();
}

bool CSOL_ScenarioWorldCurrent()
{
    return !installingWorld || installingWorld->Current();
}

bool CSOL_ScenarioInventoryCleanupAllowed()
{
    if (const auto record = FindScenarioRules(g_pGameRules))
        return !record->retired && !record->runtime->Closing();
    // Between teardown and world installation, ordinary weapon objects have
    // no owner token. Do not chase their old inventory when rules are absent.
    return g_pGameRules || scenarioRules.empty();
}

CGameRules* CSOL_ScenarioOwnerRules(const CCSOLMonsterPool* pool)
{
    for (const auto& record : scenarioRules)
        if (&record->runtime->session->Monsters() == pool) return record->rules;
    return nullptr;
}

CGameRules* CSOL_ScenarioOwnerRules(const CCSOLScenarioSession* session)
{
    for (const auto& record : scenarioRules)
        if (record->runtime->session.get() == session) return record->rules;
    return nullptr;
}

CGameRules* CSOL_ScenarioOwnerRules(const CCSOLScenarioMap* map)
{
    for (const auto& record : scenarioRules)
        if (&record->runtime->session->Map() == map) return record->rules;
    return nullptr;
}

CGameRules* CSOL_ScenarioOwnerRules(const CSOLScenarioEntityAccess* access)
{
    for (const auto& record : scenarioRules)
        if (record->runtime->session->Map().EntityAccess().get() == access) return record->rules;
    return nullptr;
}

bool CSOL_RetireScenarioRules(CGameRules** rules)
{
    if (!rules || !*rules) return false;
    const auto record = FindScenarioRules(*rules);
    if (!record) return false;
    // All slots are detached before the first callback. Never keep a pointer
    // to a caller's stack variable, and never publish retired rules again.
    *rules = nullptr;
    if (g_pGameRules == record->rules) entityRules = record;
    if (g_pGameRules == record->rules) g_pGameRules = nullptr;
    record->retired = true;
    record->runtime->RequestClose();
    return true;
}

void CSOL_CollectRetiredScenarioRules()
{
    if (sdkDepth || collectingRules) return;
    if (std::none_of(scenarioRules.begin(), scenarioRules.end(),
        [](const auto& record) { return record->retired && !record->deleting; })) return;
    collectingRules = true;
    struct CollectionScope { ~CollectionScope() { collectingRules = false; } } scope;
    // Callback-created records are left for the next boundary. A stable shared
    // record also prevents a cleanup callback from invalidating this iterator.
    const std::vector<std::shared_ptr<CSOLScenarioRulesRecord>> pending(scenarioRules.begin(), scenarioRules.end());
    for (const auto& record : pending)
    {
        if (!record->retired || record->deleting || !record->runtime->TryFinalize()) continue;
        record->deleting = true;
        auto* rules = record->rules;
        // The retirement record is the unique raw owner. Do not reenter the
        // FreeGameRules hook or repeat any caller-slot write during deletion.
        delete rules;
        record->rules = nullptr;
        scenarioRules.remove(record);
    }
}

void CSOL_ShutdownScenarioRules()
{
    // ReHLDS has returned from FreeAllEntPrivateData before this callback.
    // Normal shutdown does not otherwise call FreeGameRules for current rules.
    CSOL_RetireScenarioRules(&g_pGameRules);
    CSOL_CollectRetiredScenarioRules();
}

bool CSOL_RulesRejected() { return rejection[0] != '\0'; }
const char* CSOL_RulesRejectionReason() { return rejection.data(); }

void CSOL_RejectGameRules(const char* reason)
{
    if (CSOL_RulesRejected()) return;
    Q_strncpy(rejection.data(), reason && *reason ? reason : "Game mode initialization failed",
        rejection.size());
    rejection.back() = '\0';
    ALERT(at_error, "CSOL map rejected: %s. Fix the configuration and reload the map.\n",
        rejection.data());
}

CGameRules* CSOL_CreateGameRules()
{
    rejection = {};
    // The standalone training path has no multiplayer plugin policy.
    if (!gpGlobals->deathmatch) return new CHalfLifeTraining;
    const int mode = CSOL_LockedGameMode();
    if (mode < 0 || mode > 2)
        CSOL_RejectGameRules("Invalid or unprepared csol_game_mode (expected 0, 1 or 2)");
    else if (!CSOL_GameModeIsolationReady())
        CSOL_RejectGameRules("Game mode plugin isolation was not confirmed before worldspawn");
    if (CSOL_RulesRejected()) return new CCSOLRejectedRules;
    if (mode == 2)
    {
        auto* rules = new CCSOLScenarioRules;
        auto record = std::make_shared<CSOLScenarioRulesRecord>();
        record->rules = rules;
        record->runtime = rules->Runtime();
        scenarioRules.push_back(std::move(record));
        return rules;
    }
    return new CHalfLifeMultiplay;
}

void CSOL_PrecacheGameRules()
{
    CSOLScenarioSdkScope scope;
    if (!scope.Current()) return;
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->Precache();
}

bool CSOL_ActivateGameRules()
{
    CSOLScenarioSdkScope scope;
    if (!scope.Current()) return false;
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) return rules->Activate();
    return !CSOL_RulesRejected();
}

bool CSOL_UsesScenarioRules()
{
    return dynamic_cast<CCSOLScenarioRules*>(g_pGameRules) != nullptr;
}

bool CSOL_ScenarioCanBuy()
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current()) return false;
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    return rules && rules->CanBuyThroughoutScenario();
}

void CSOL_ApplyScenarioMovement(CBasePlayer& player, float weaponSpeed)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    if (CSOL_RulesRejected()) return;
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->ApplyMovement(player, weaponSpeed);
}

bool CSOL_ScenarioProtectionBlocks(CBasePlayer& player)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario()) return false;
    if (!scope.Current()) return true;
    const auto players = CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get());
    return players && players->ProtectionBlocksDamage(EHANDLE(&player), gpGlobals->time);
}

bool CSOL_UpdateScenarioPlayerBuffs(CBasePlayer& player)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario()) return true;
    if (!scope.Current()) return false;
    const auto players = CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get());
    if (!players || !players->Active()) return true; // Loading has no player effects.
    const EHANDLE handle(&player);
    const bool updated = players->UpdatePlayerBuffs(player, gpGlobals->time);
    if (!scope.Current() || handle.Get<CBaseEntity>() != &player) return false;
    if (!updated && !players->Error().message.empty())
        CSOL_RejectGameRules(players->Error().message.c_str());
    return updated;
}

bool CSOL_ScenarioPlayerPostThink(CBasePlayer& player)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario()) return true;
    if (!scope.Current()) return false;
    const auto record = FindScenarioRules(g_pGameRules);
    if (!record || record->retired || record->runtime->Closing()) return false;
    const auto runtime = record->runtime;
    const auto players = runtime->players;
    if (!players || !players->Active()) return true;
    const EHANDLE handle(&player);
    if (!players->Find(handle)) return true; // No module before player preparation.
    CSOLPlayerRecoveryCall call(runtime, handle,
        [](const void* value) { return static_cast<const CSOLScenarioSdkScope*>(value)->Current(); }, &scope);
    csol::scenario::ConfigError error;
    bool updated{};
    try { updated = players->UpdateBloodSucking(call, error); }
    catch (...)
    {
        if (!call.Current()) return false;
        error.message = "scenario player recovery update threw";
    }
    if (!call.Current() || !scope.Current()) return false;
    if (!updated)
    {
        if (error.message.empty()) error.message = "scenario player recovery update failed";
        CSOL_RejectGameRules(error.message.c_str());
    }
    return updated;
}

std::shared_ptr<CCSOLScenarioPlayerRuntime> CSOL_ScenarioPlayerAuthority(
    const CSOLScenarioEntityAccess* access)
{
    if (!access || !access->Allowed()) return {};
    const auto record = FindScenarioRules(g_pGameRules);
    if (!record || record->retired || record->runtime->Closing() ||
        record->runtime->session->Map().EntityAccess().get() != access) return {};
    const auto players = record->runtime->players;
    return players && players->Active() ? players : nullptr;
}

namespace
{
class ScenarioAdminService final : public csol::IScenarioAdminService
{
public:
    bool DifficultyInfo(csol::ScenarioDifficultyInfo& output) const override
    {
        CSOLScenarioSdkScope scope;
        if (!scope.Scenario() || !scope.Current() || CSOL_LockedGameMode() != 2 ||
            !CSOL_GameModeIsolationReady() || CSOL_RulesRejected()) return false;
        const auto record = FindScenarioRules(scope.Rules());
        if (!record || record->retired || record->runtime->Closing()) return false;
        const auto session = record->runtime->session;
        if (session->State() != CSOLScenarioSessionState::Precached &&
            session->State() != CSOLScenarioSessionState::Active) return false;
        const auto* tables = session->Map().Tables();
        const auto& difficulty = session->Difficulty();
        if (!tables || !tables->mapId || !tables->difficultyLimit ||
            difficulty.State() != csol::scenario::DifficultyState::Ready || !difficulty.Table()) return false;
        const int maximum = std::min(*tables->difficultyLimit,
            static_cast<int>(difficulty.Table()->source.records.size()) - 1);
        if (maximum < 1 || maximum > 7 || !difficulty.SelectedRow() || difficulty.SelectedRow() > maximum)
            return false;
        std::uint16_t requested{};
        if (!CSOL_RequestedScenarioDifficulty(requested) || requested > maximum) requested = 0;
        output = {*tables->mapId, difficulty.SelectedRow(), requested, maximum};
        return true;
    }

    bool RequestDifficulty(int mapId, int difficulty) override
    {
        CSOLScenarioSdkScope scope;
        csol::ScenarioDifficultyInfo info;
        if (m_writing || !scope.Scenario() || !scope.Current() || !DifficultyInfo(info) ||
            info.mapId != mapId || difficulty < 1 || difficulty > info.maximum) return false;
        // A CvarSetString hook may retire the map or invoke this API again.
        // Keep the owner pinned and reject nested writes to this same setting.
        m_writing = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{m_writing};
        if (!CSOL_WriteScenarioDifficultyRequest(static_cast<std::uint16_t>(difficulty))) return false;
        return scope.Current() && DifficultyInfo(info) && info.mapId == mapId && info.requested == difficulty;
    }
private:
    bool m_writing{};
};
ScenarioAdminService scenarioAdmin;
using csol::IScenarioAdminService;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(ScenarioAdminService, IScenarioAdminService,
    csol::ScenarioAdminServiceVersion, scenarioAdmin);
}

bool CSOL_ScenarioThink(CHalfLifeMultiplay& rules)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioThink();
    if (g_pGameRules == scenario) scenario->UpdateDamageScale();
    return true;
}

bool CSOL_ReadScenarioDamageScale(const CCSOLScenarioMap& map, float& value)
{
    const auto* scenario = dynamic_cast<const CCSOLScenarioRules*>(g_pGameRules);
    return scenario && scenario->ReadDamageScale(map, value);
}

bool CSOL_CompleteScenarioMonsterClearPoint(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current()) return false;
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(scope.Rules());
    return rules && rules->CompleteMonsterClearPoint(call, error);
}

bool CSOL_ScenarioHeGrenadeCreated(CBaseEntity& grenade)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario()) return true;
    if (!scope.Current()) return false;
    const auto* rules = dynamic_cast<const CCSOLScenarioRules*>(scope.Rules());
    const auto weapons = rules ? rules->Runtime()->session->Weapons() : nullptr;
    if (weapons && weapons->RecordHeGrenade(grenade)) return true;
    CSOL_RejectGameRules("Scenario HE damage source could not be recorded.");
    return false;
}

int CSOL_ScenarioProjectileSource(const CBaseEntity& entity)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current()) return 0;
    const auto* rules = dynamic_cast<const CCSOLScenarioRules*>(scope.Rules());
    const auto weapons = rules ? rules->Runtime()->session->Weapons() : nullptr;
    return weapons ? weapons->ProjectileSource(entity) : 0;
}

bool CSOL_ScenarioRestartRound(CHalfLifeMultiplay& rules)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioRestartRound();
    return true;
}

bool CSOL_ScenarioCheckWinConditions(CHalfLifeMultiplay& rules)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioCheckWinConditions();
    return true;
}

bool CSOL_ScenarioPrepareSpawn(CHalfLifeMultiplay& rules, CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    return scenario && scenario->PrepareSpawn(player);
}

bool CSOL_ScenarioCanRespawn(CHalfLifeMultiplay& rules, CBasePlayer& player, bool& allowed)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) { allowed = false; return true; }
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    allowed = scenario->CanRespawn(player);
    return true;
}

bool CSOL_ScenarioTeamFull(CHalfLifeMultiplay& rules, int team, bool& full)
{
    CSOLScenarioSdkScope sdkScope(&rules);
    if (sdkScope.Scenario() && !sdkScope.Current()) { full = true; return true; }
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    full = scenario->TeamFull(team);
    return true;
}

bool CSOL_ScenarioHoldSpawn(CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    return rules && rules->HoldSpawn(player);
}

std::uint32_t CSOL_ScenarioPassMask(int playerIndex)
{
    CSOLScenarioSdkScope scope;
    if (!scope.Current()) return 0;
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    return rules ? rules->PassMask(playerIndex) : 0;
}

bool CSOL_ScenarioSelecting()
{
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    return rules && rules->Selecting();
}

void CSOL_ScenarioCharacterSelected(CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->CharacterSelected(player);
}

bool CSOL_ScenarioSelectSpawn(CBasePlayer& player, edict_t*& spot)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) { spot = nullptr; return true; }
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    // ClientPutInServer initializes an unassigned player through Spawn too.
    // It needs the actual scenario camera point, never a stock CS spawn search.
    if (!scenario) return false;
    spot = scenario->SelectSpawn(player);
    return true;
}

void CSOL_ScenarioPlayerDisconnected(CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    if (auto* scenario = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) scenario->RemovePlayer(player);
}

bool CSOL_ShowScenarioTeamMenu(CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return true;
    if (!CSOL_UsesScenarioRules()) return false;
    const bool spectator = (allow_spectators.value != 0 || (player.pev->flags & FL_PROXY)) &&
        (g_pGameRules->IsFreezePeriod() || player.pev->deadflag != DEAD_NO);
    const bool joined = player.m_iJoiningState == JOINED;
    const int keys = MENU_KEY_2 | (spectator ? MENU_KEY_6 : 0) | (joined ? MENU_KEY_0 : 0);
    char menuName[32];
    Q_strncpy(menuName,
        joined ? (spectator ? "#IG_Team_Select_Spect" : "#IG_Team_Select") :
            (spectator ? "#Team_Select_Spect" : "#Team_Select"), sizeof(menuName));
    ShowVGUIMenu(&player, VGUI_Menu_Team, keys, menuName);
    return true;
}

bool CSOL_ScenarioAllowAppearance(CBasePlayer& player)
{
    CSOLScenarioSdkScope sdkScope;
    if (sdkScope.Scenario() && !sdkScope.Current()) return false;
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    if (!rules) return true;
    const auto access = rules->EntityAccess();
    if (!access->Allowed()) return false;
    const EHANDLE handle(&player);
    int classId{};
    if (player.m_iTeam == CT)
    {
        bool selected = CSOL_CharacterScenarioClass(&player, classId, access);
        if (!access->Allowed() || g_pGameRules != rules || handle.Get<CBaseEntity>() != &player) return false;
        if (!selected && CSOL_EnsureScenarioBotCharacter(&player, access))
            selected = CSOL_CharacterScenarioClass(&player, classId, access);
        if (!access->Allowed() || g_pGameRules != rules || handle.Get<CBaseEntity>() != &player) return false;
        if (selected && rules->HasCharacterClass(classId)) return true;
    }
    // A stock joinclass command cannot create a missing CSOL character record
    // and must not turn one player's incomplete selection into a map failure.
    player.m_iMenu = Menu_ChooseAppearance;
    char menuName[] = "#CT_Select";
    ShowVGUIMenu(&player, VGUI_Menu_Class_CT,
        MENU_KEY_1 | MENU_KEY_2 | MENU_KEY_3 | MENU_KEY_4 | MENU_KEY_5, menuName);
    return false;
}

void CSOL_FreezeRejectedEntity(edict_t* entity)
{
    if (!entity || entity->free) return;
    auto& vars = entity->v;
    vars.velocity = vars.avelocity = vars.basevelocity = g_vecZero;
    vars.nextthink = 0;
    vars.movetype = MOVETYPE_NONE;
    vars.takedamage = DAMAGE_NO;
    vars.button = vars.impulse = 0;
    // Preserve world/brush solidity. Clients must not collide or issue attacks.
    if (vars.flags & (FL_CLIENT | FL_FAKECLIENT))
    {
        vars.flags |= FL_FROZEN | FL_SPECTATOR;
        vars.solid = SOLID_NOT;
        vars.effects |= EF_NODRAW;
        vars.deadflag = DEAD_DEAD;
        vars.maxspeed = 1;
    }
}

void CSOL_HoldRejectedPlayer(CBasePlayer& player)
{
    player.pev->flags |= FL_CLIENT;
    CSOL_FreezeRejectedEntity(player.edict());
    player.SetThink(nullptr);
    player.SetTouch(nullptr);
    player.m_iTeam = SPECTATOR;
    player.m_iJoiningState = JOINED;
    player.m_bJustConnected = false;
    player.m_bNotKilled = false;
    player.m_iMenu = Menu_OFF;
    player.m_afButtonPressed = player.m_afButtonReleased = player.m_afButtonLast = 0;
}

bool CSOL_PrepareRejectedPlayer(edict_t* entity)
{
    if (!entity || entity->free) return false;
    entity->v.flags |= FL_CLIENT;
    CSOL_FreezeRejectedEntity(entity);
    // GetClassPtr does not type-check an existing private object. In
    // particular, a plugin can have installed a non-player in a client slot.
    auto* player = entity->pvPrivateData ?
        dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity)) :
        GetClassPtr<CCSPlayer>((CBasePlayer *)&entity->v);
    if (!player) return false;
    CSOL_HoldRejectedPlayer(*player);
    return true;
}
