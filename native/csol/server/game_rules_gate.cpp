// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "game_rules_gate.h"
#include "game_mode.h"
#include "scenario_session.h"
#include "scenario_gameplay.h"
#include "scenario_player_runtime.h"
#include "scenario_drops.h"
#include "characters.h"
#include "../scenario/round_settings.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace
{
std::array<char, 256> rejection{};

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
    enum class Phase { Loading, Selecting, Waiting, Starting, Playing, Advancing, Completing, Failed, Complete };
    CCSOLScenarioRules()
    {
        m_bGameOver = false;
        m_bGameStarted = false;
        m_bFreezePeriod = TRUE;
        m_bRoundTerminating = false;
    }
    const char* GetGameDescription() override { return "GoldCraft - Zombie Scenario"; }
    bool HasCharacterClass(int classId) const
    {
        if (!SelectionReady()) return false;
        const auto upgrades = m_session->Upgrades();
        return upgrades && upgrades->classes.Find(classId);
    }
    ~CCSOLScenarioRules() override
    {
        if (m_drops) m_drops->Deactivate();
        if (m_playerRuntime) m_playerRuntime->Invalidate();
    }
    void Precache()
    {
        std::uint16_t difficulty{};
        if (!CSOL_RequestedScenarioDifficulty(difficulty))
        {
            CSOL_RejectGameRules("Invalid csol_scenario_difficulty; expected a positive integer row.");
            return;
        }
        const auto scope = m_gameplay && m_waveEvents ? CSOLScenarioResourceScope::Combat :
            CSOLScenarioResourceScope::Selection;
        if (!m_session->Precache(STRING(gpGlobals->mapname), difficulty, scope))
            FailSession();
    }
    bool Activate()
    {
        if (CSOL_RulesRejected()) return false;
        if (!m_session->Activate())
        {
            FailSession();
            return false;
        }
        m_playerRuntime = std::make_shared<CCSOLScenarioPlayerRuntime>(m_session);
        if (!m_playerRuntime->Activate())
        {
            CSOL_RejectGameRules(m_playerRuntime->Error().message.c_str());
            return false;
        }
        m_drops = std::make_shared<CCSOLScenarioDrops>(m_session, m_playerRuntime);
        if (!m_drops->Activate())
        {
            CSOL_RejectGameRules("Scenario drop owner could not activate.");
            return false;
        }
        if (!UpdateSpawnCapacity(0)) return false;
        // A valid map can accept team/character selection before combat is
        // installed. Keep that state explicit: it cannot start a round or
        // respawn a combatant, and never claims that the map is playable.
        if (!m_gameplay || !m_waveEvents)
        {
            if (m_gameplay || m_waveEvents)
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
        if (!m_gameplay->Activate()) return FailGameplay();
        m_phase = Phase::Waiting;
        m_bGameOver = false;
        m_bGameStarted = false;
        m_bRoundTerminating = false;
        m_flRestartRoundTime = 0;
        return true;
    }
    void ServerDeactivate() override
    {
        if (m_drops) m_drops->Deactivate();
        if (m_playerRuntime) m_playerRuntime->Invalidate();
        if (m_gameplay && !m_gameplay->Deactivate())
        {
            FailGameplay();
            return; // Do not release services beneath a running game callback.
        }
        if (!m_session->Deactivate()) FailSession();
        CHalfLifeMultiplay::ServerDeactivate();
    }
    void ScenarioThink()
    {
        if (!SelectionReady()) return;
        m_VoiceGameMgr.Update(gpGlobals->frametime);
        if (!Ready()) return;
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
                m_phase = Phase::Complete;
                m_flRestartRoundTime = 0;
                m_bGameOver = true;
                return; // A scenario result policy decides whether to change maps.
            }
        }
        CheckWinConditions();
        if (!Ready() || (m_phase != Phase::Playing && m_phase != Phase::Advancing &&
            m_phase != Phase::Completing)) return;
        // MP's manager runs before thawing the rules. A freeze ending this
        // frame therefore produces its first wave tick on the following one.
        if (!m_gameplay->Update(*this, gpGlobals->time, m_aliveCt, *this))
        {
            FailGameplay();
            return;
        }
        if (!Ready()) return;
        if (m_bFreezePeriod && gpGlobals->time >= m_freezeUntil)
        {
            m_bFreezePeriod = FALSE;
            m_fRoundStartTime = m_fRoundStartTimeReal = gpGlobals->time;
            m_iRoundTimeSecs = 0; // Scenario time is sent by its own HUD owner.
            ForPlayers([&](CBasePlayer& player)
            {
                if (!Spawnable(player)) return;
                player.m_bCanShoot = true;
                player.ResetMaxSpeed();
                player.SyncRoundTimer();
            });
        }
    }
    void ScenarioCheckWinConditions()
    {
        if (!SelectionReady()) return;
        int aliveT{}, deadT{}, deadCt{};
        InitializePlayerCounts(aliveT, m_aliveCt, deadT, deadCt);
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
    void ScenarioRestartRound()
    {
        if (!Ready() || m_restarting || m_phase == Phase::Complete) return;
        struct RestartScope { bool& flag; ~RestartScope() { flag = false; } } scope{m_restarting};
        m_restarting = true;
        const auto nextRound = m_phase == Phase::Advancing ? m_roundIndex + 1 : m_roundIndex;
        ReadMultiplayCvars();
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
        if (!UpdateSpawnCapacity(nextRound)) return;
        if (!m_gameplay->BeginRound(nextRound, now, freeze, *this))
        {
            FailGameplay();
            return;
        }
        m_roundIndex = nextRound;
        m_completionLocked = false;
        m_phase = Phase::Playing;
        // BeginRound has now installed PlayerSpawn/TRG/waves. Use the real
        // player round transaction; teleporting alone leaves old life state.
        ForPlayers([&](CBasePlayer& player)
        {
            if (!Spawnable(player)) return;
            if (!PreparePlayer(player)) return;
            player.m_iNumSpawns = 0;
            player.m_bTeamChanged = false;
            player.RoundRespawn();
        });
        if (!Ready()) return;
        // CSO cleans/restarts map entities AFTER player respawns. Never call
        // CHalfLifeMultiplay::RestartRound, whose FIXES order is the reverse.
        CleanUpMap();
        if (!Ready()) return;
        m_bCompleteReset = false;
        CheckWinConditions();
    }
    bool PrepareSpawn(CBasePlayer& player)
    {
        // ClientPutInServer calls Spawn before the player has a team/model.
        // Consume that equipment stage without requiring a combat loadout.
        if (player.m_bJustConnected || player.m_iTeam == UNASSIGNED || player.m_iTeam == SPECTATOR)
            return true;
        if (!Ready() || !PreparePlayer(player)) return true;
        const auto players = m_session->Players();
        const auto upgrades = players ? players->Upgrades(EHANDLE(&player)) : nullptr;
        if (!upgrades || !upgrades->ApplySpawnHealth())
        {
            CSOL_RejectGameRules("Scenario spawn health could not be applied.");
            return true;
        }
        return false; // Continue the native equip/model stage inside its hook.
    }
    bool CanRespawn(CBasePlayer& player) const
    {
        return Ready() && m_phase == Phase::Playing && !m_bRoundTerminating &&
            Spawnable(player) && (m_restarting || (m_bFreezePeriod && player.m_iNumSpawns == 0));
    }
    edict_t* ObserverSpawn()
    {
        // An observer only needs a camera location. Do not run combat
        // occupancy clearing or consume a player-spawn selection attempt.
        const auto* tables = m_session->Map().Tables();
        csol::scenario::RoundSettings settings;
        csol::scenario::ConfigError error;
        const auto round = m_session->Map().RoundIndex().value_or(0);
        if (!tables || !csol::scenario::ReadRoundSettings(tables->rounds, round, settings, error))
        {
            CSOL_RejectGameRules("Scenario observer has no current spawn group.");
            return nullptr;
        }
        const auto session = m_session;
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
            m_session->Map().State() == CSOLScenarioMapState::EntitiesCollected)
        {
            // ReGameDLL also asks for a spot when GetIntoGame enters observer
            // mode before the first round. This is a camera location, not a
            // combat respawn: do not require BeginRound or clear monsters.
            return ObserverSpawn();
        }
        auto* spot = m_gameplay->SelectSpawn(player);
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
        const auto session = m_session;
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
    std::uint32_t PassMask(int playerIndex) const
    {
        if (!SelectionReady() || playerIndex < 1 || playerIndex > gpGlobals->maxClients) return 0;
        auto* player = dynamic_cast<CBasePlayer*>(UTIL_PlayerByIndex(playerIndex));
        if (!player || player->has_disconnected || !player->IsAlive() ||
            player->m_iTeam != CT || (player->pev->flags & (FL_SPECTATOR | FL_DORMANT | FL_KILLME))) return 0;
        std::uint32_t peers{};
        for (int index = 1; index <= gpGlobals->maxClients && index <= 32; ++index)
        {
            if (index == playerIndex) continue;
            auto* peer = dynamic_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
            if (peer && !peer->has_disconnected && peer->m_iTeam == player->m_iTeam && peer->IsAlive() &&
                !(peer->pev->flags & (FL_SPECTATOR | FL_DORMANT | FL_KILLME)))
                peers |= std::uint32_t{1} << (index - 1);
        }
        return peers;
    }
    void RemovePlayer(CBasePlayer& player)
    {
        if (m_playerRuntime) m_playerRuntime->Remove(EHANDLE(&player));
        else m_session->RemovePlayer(EHANDLE(&player));
    }
    void WaveActivated(std::uint16_t id) override
    {
        const auto events = m_waveEvents;
        if (Ready()) events->WaveActivated(id);
    }
    void BeforeSpatialWaveEnd(std::uint16_t id) override
    {
        const auto events = m_waveEvents;
        if (Ready()) events->BeforeSpatialWaveEnd(id);
    }
    void WavesChanged(std::span<const std::uint16_t> active) override
    {
        const auto events = m_waveEvents;
        if (Ready()) events->WavesChanged(active);
    }
    void GoalReached() override
    {
        if (!Ready() || m_phase != Phase::Playing || m_completionLocked) return;
        const auto* tables = m_session->Map().Tables();
        if (!tables || m_roundIndex >= tables->rounds.rows.size())
        {
            CSOL_RejectGameRules("Scenario completion has no current round.");
            return;
        }
        m_completionLocked = true;
        m_phase = m_roundIndex + 1 < tables->rounds.rows.size() ? Phase::Advancing : Phase::Completing;
        m_flRestartRoundTime = gpGlobals->time + 7.0f;
        const auto events = m_waveEvents;
        events->GoalReached();
    }
    void ApplyMovement(CBasePlayer& player, float weaponSpeed)
    {
        // GetIntoGame resets speed before JOINED and before the real Spawn;
        // the initial UNASSIGNED Spawn also has no selected character yet.
        if (player.m_bJustConnected || player.m_iTeam == UNASSIGNED || player.m_iTeam == SPECTATOR ||
            player.m_iJoiningState != JOINED || (player.pev->flags & FL_SPECTATOR)) return;
        const auto players = m_session->Players();
        const auto state = players ? players->Upgrades(EHANDLE(&player)) : nullptr;
        const bool applied = state && state->ApplyMovement(weaponSpeed);
        if (g_pGameRules == this && !applied)
            CSOL_RejectGameRules("Scenario player movement upgrade state is unavailable.");
    }
private:
    bool SelectionReady() const
    {
        return g_pGameRules == this && !CSOL_RulesRejected() && m_phase != Phase::Loading &&
            m_session->State() == CSOLScenarioSessionState::Active && m_playerRuntime &&
            m_playerRuntime->Active() && m_iSpawnPointCount_CT > 0;
    }
    bool Ready() const
    {
        return SelectionReady() && m_phase != Phase::Selecting &&
            m_gameplay && m_gameplay->Active() && m_waveEvents;
    }
    bool Spawnable(CBasePlayer& player) const
    {
        int classId{};
        return player.m_iTeam == CT && player.m_iJoiningState == JOINED &&
            !player.m_bJustConnected && !player.has_disconnected &&
            !(player.pev->flags & (FL_DORMANT | FL_KILLME)) &&
            player.m_iMenu != Menu_ChooseAppearance && CSOL_CharacterScenarioClass(&player, classId) &&
            g_pGameRules == this && HasCharacterClass(classId);
    }
    template<class Action> void ForPlayers(Action action)
    {
        for (int index = 1; index <= gpGlobals->maxClients && Ready(); ++index)
        {
            auto* player = dynamic_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
            if (player && !(player->pev->flags & (FL_DORMANT | FL_KILLME))) action(*player);
        }
    }
    bool PreparePlayer(CBasePlayer& player)
    {
        const auto runtime = m_playerRuntime;
        const bool prepared = runtime && runtime->Prepare(player);
        if (g_pGameRules != this || m_playerRuntime != runtime) return false;
        if (prepared) return true;
        CSOL_RejectGameRules(runtime ? runtime->Error().message.c_str() :
            "Scenario player authority is unavailable.");
        return false;
    }
    bool UpdateSpawnCapacity(std::size_t round)
    {
        const auto* tables = m_session->Map().Tables();
        csol::scenario::RoundSettings settings;
        csol::scenario::ConfigError error;
        if (!tables || !csol::scenario::ReadRoundSettings(tables->rounds, round, settings, error))
        {
            CSOL_RejectGameRules("Scenario team capacity has no current round settings.");
            return false;
        }
        const auto count = m_session->Map().SpawnPoints().PlayerPointCount(CT, settings.playerSpawn);
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
        return !CSOL_RulesRejected();
    }
    bool FailGameplay()
    {
        CSOL_RejectGameRules(m_gameplay ? m_gameplay->Error().message.c_str() :
            "Scenario gameplay owner is unavailable.");
        return false;
    }
    void FailSession()
    {
        const auto& error = m_session->Error();
        ALERT(at_error, "CSOL scenario resource '%s': %s\n",
            error.path.c_str(), error.detail.message.c_str());
        CSOL_RejectGameRules(error.detail.message.c_str());
    }
    std::shared_ptr<CCSOLScenarioSession> m_session{std::make_shared<CCSOLScenarioSession>()};
    std::shared_ptr<CCSOLScenarioPlayerRuntime> m_playerRuntime;
    std::shared_ptr<CCSOLScenarioDrops> m_drops;
    std::shared_ptr<CCSOLScenarioGameplay> m_gameplay;
    std::shared_ptr<csol::scenario::TriggerWaveEvents> m_waveEvents;
    Phase m_phase{Phase::Loading};
    std::size_t m_roundIndex{};
    int m_aliveCt{};
    float m_freezeUntil{};
    bool m_restarting{}, m_completionLocked{};
};
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
    if (mode == 2) return new CCSOLScenarioRules;
    return new CHalfLifeMultiplay;
}

void CSOL_PrecacheGameRules()
{
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->Precache();
}

bool CSOL_ActivateGameRules()
{
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) return rules->Activate();
    return !CSOL_RulesRejected();
}

bool CSOL_UsesScenarioRules()
{
    return dynamic_cast<CCSOLScenarioRules*>(g_pGameRules) != nullptr;
}

void CSOL_ApplyScenarioMovement(CBasePlayer& player, float weaponSpeed)
{
    if (CSOL_RulesRejected()) return;
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->ApplyMovement(player, weaponSpeed);
}

bool CSOL_ScenarioThink(CHalfLifeMultiplay& rules)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioThink();
    return true;
}

bool CSOL_ScenarioRestartRound(CHalfLifeMultiplay& rules)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioRestartRound();
    return true;
}

bool CSOL_ScenarioCheckWinConditions(CHalfLifeMultiplay& rules)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    scenario->ScenarioCheckWinConditions();
    return true;
}

bool CSOL_ScenarioPrepareSpawn(CHalfLifeMultiplay& rules, CBasePlayer& player)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    return scenario && scenario->PrepareSpawn(player);
}

bool CSOL_ScenarioCanRespawn(CHalfLifeMultiplay& rules, CBasePlayer& player, bool& allowed)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    allowed = scenario->CanRespawn(player);
    return true;
}

bool CSOL_ScenarioTeamFull(CHalfLifeMultiplay& rules, int team, bool& full)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(&rules);
    if (!scenario) return false;
    full = scenario->TeamFull(team);
    return true;
}

bool CSOL_ScenarioHoldSpawn(CBasePlayer& player)
{
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    return rules && rules->HoldSpawn(player);
}

std::uint32_t CSOL_ScenarioPassMask(int playerIndex)
{
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
    if (auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) rules->CharacterSelected(player);
}

bool CSOL_ScenarioSelectSpawn(CBasePlayer& player, edict_t*& spot)
{
    auto* scenario = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    // ClientPutInServer initializes an unassigned player through Spawn too.
    // It needs the actual scenario camera point, never a stock CS spawn search.
    if (!scenario) return false;
    spot = scenario->SelectSpawn(player);
    return true;
}

void CSOL_ScenarioPlayerDisconnected(CBasePlayer& player)
{
    if (auto* scenario = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules)) scenario->RemovePlayer(player);
}

bool CSOL_ShowScenarioTeamMenu(CBasePlayer& player)
{
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
    auto* rules = dynamic_cast<CCSOLScenarioRules*>(g_pGameRules);
    if (!rules) return true;
    const EHANDLE handle(&player);
    int classId{};
    if (player.m_iTeam == CT)
    {
        bool selected = CSOL_CharacterScenarioClass(&player, classId);
        if (g_pGameRules != rules || handle.Get<CBaseEntity>() != &player) return false;
        if (!selected && CSOL_EnsureScenarioBotCharacter(&player))
            selected = CSOL_CharacterScenarioClass(&player, classId);
        if (g_pGameRules != rules || handle.Get<CBaseEntity>() != &player) return false;
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
