// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>

class CGameRules;
class CHalfLifeMultiplay;
class CBasePlayer;
class CBaseEntity;
class CCSOLScenarioMap;
class CCSOLMonsterPool;
class CCSOLScenarioSession;
class CCSOLScenarioPlayerRuntime;
class CSOLMonsterDeathCall;
namespace csol::scenario { struct ConfigError; }
struct edict_s;
struct CSOLScenarioRulesRecord;
class CSOLScenarioEntityAccess;

// Covers the complete SDK/ReAPI call, including plugin post hooks. Destruction
// only releases a pin; collection is restricted to later engine boundaries.
class CSOLScenarioSdkScope final
{
public:
    CSOLScenarioSdkScope();
    explicit CSOLScenarioSdkScope(CGameRules* rules);
    ~CSOLScenarioSdkScope();
    CSOLScenarioSdkScope(const CSOLScenarioSdkScope&) = delete;
    CSOLScenarioSdkScope& operator=(const CSOLScenarioSdkScope&) = delete;
    bool Current() const;
    bool Scenario() const { return static_cast<bool>(m_record); }
    CGameRules* Rules() const { return m_rules; }
    std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess() const { return m_access; }
    void RequestClose();

private:
    friend class CSOLScenarioWorldScope;
    void Bind(CGameRules* rules);
    CGameRules* m_rules{};
    std::shared_ptr<CSOLScenarioRulesRecord> m_record;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
};

// A world dispatch can legitimately replace its rules and entity-access
// token. Only this explicit installation scope may bind the new instance.
class CSOLScenarioWorldScope final
{
public:
    CSOLScenarioWorldScope(CSOLScenarioSdkScope& scope, bool world, const edict_s* entity,
        bool attachToDispatch = false);
    ~CSOLScenarioWorldScope();
    CSOLScenarioWorldScope(const CSOLScenarioWorldScope&) = delete;
    CSOLScenarioWorldScope& operator=(const CSOLScenarioWorldScope&) = delete;
    bool Current() const;
    bool Install();
private:
    CSOLScenarioSdkScope& m_scope;
    CSOLScenarioWorldScope* m_previous{};
    CSOLScenarioWorldScope* m_parent{};
    const edict_s* m_entity{};
    std::uint64_t m_serial{};
    bool m_world{}, m_installing{}, m_installed{}, m_failed{};
};
bool CSOL_InstallWorldRules();
bool CSOL_ScenarioWorldCurrent();
bool CSOL_ScenarioInventoryCleanupAllowed();
CGameRules* CSOL_ScenarioOwnerRules(const CCSOLMonsterPool* pool);
CGameRules* CSOL_ScenarioOwnerRules(const CCSOLScenarioSession* session);
CGameRules* CSOL_ScenarioOwnerRules(const CCSOLScenarioMap* map);
CGameRules* CSOL_ScenarioOwnerRules(const CSOLScenarioEntityAccess* access);
// Trusted SDK/manager callers retain the player service for exactly this
// current map token. This does not install combat services or bypass Ready.
std::shared_ptr<CCSOLScenarioPlayerRuntime> CSOL_ScenarioPlayerAuthority(
    const CSOLScenarioEntityAccess* access);

// Consumes registered scenario instances only. Immediately detaches the slot;
// no address of the caller's pointer is retained for deferred deletion.
bool CSOL_RetireScenarioRules(CGameRules** rules);
// Call before entering a top-level StartFrame/ServerActivate SDK scope, or
// after the engine's FreeAllEntPrivateData in OnGameShutdown. Never OnFree.
void CSOL_CollectRetiredScenarioRules();
void CSOL_ShutdownScenarioRules();

// InstallGameRules starts one map's decision after game.cfg. This consumes the
// AMXX precache gate's locked mode; it never confirms plugin isolation itself.
CGameRules* CSOL_CreateGameRules();
void CSOL_PrecacheGameRules();
bool CSOL_ActivateGameRules();
bool CSOL_UsesScenarioRules();
bool CSOL_ScenarioCanBuy();
bool CSOL_ScenarioSelecting();
void CSOL_ApplyScenarioMovement(CBasePlayer& player, float weaponSpeed);
bool CSOL_ScenarioProtectionBlocks(CBasePlayer& player);
bool CSOL_UpdateScenarioPlayerBuffs(CBasePlayer& player);
bool CSOL_ScenarioPlayerPostThink(CBasePlayer& player);
std::uint32_t CSOL_ScenarioPassMask(int playerIndex);

// Called inside ReGameDLL's existing ReAPI original-function wrappers. A
// true result consumes only the scenario branch; other modes remain native.
bool CSOL_ScenarioThink(CHalfLifeMultiplay& rules);
// Read the current map's stored rules multiplier, never recalculate on hit.
bool CSOL_ReadScenarioDamageScale(const CCSOLScenarioMap& map, float& value);
// Called after root character effects and current-weapon statistics, before
// frags/drops. This does not install the still-incomplete death service.
bool CSOL_CompleteScenarioMonsterClearPoint(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error);
bool CSOL_ScenarioHeGrenadeCreated(CBaseEntity& grenade);
int CSOL_ScenarioProjectileSource(const CBaseEntity& entity);
bool CSOL_ScenarioRestartRound(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioCheckWinConditions(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioPrepareSpawn(CHalfLifeMultiplay& rules, CBasePlayer& player);
bool CSOL_ScenarioCanRespawn(CHalfLifeMultiplay& rules, CBasePlayer& player, bool& allowed);
bool CSOL_ScenarioTeamFull(CHalfLifeMultiplay& rules, int team, bool& full);
bool CSOL_ScenarioHoldSpawn(CBasePlayer& player);
void CSOL_ScenarioCharacterSelected(CBasePlayer& player);
bool CSOL_ScenarioSelectSpawn(CBasePlayer& player, edict_s*& spot);
void CSOL_ScenarioPlayerDisconnected(CBasePlayer& player);
bool CSOL_ShowScenarioTeamMenu(CBasePlayer& player);
bool CSOL_ScenarioAllowAppearance(CBasePlayer& player);

// A rejection survives service EndMap and client disconnect callbacks. Only
// creating rules for a new map clears it; changing a CVAR cannot unlock a
// partially loaded map. This is not a global engine pause facility.
bool CSOL_RulesRejected();
const char* CSOL_RulesRejectionReason();
void CSOL_RejectGameRules(const char* reason);
void CSOL_FreezeRejectedEntity(edict_s* entity);
void CSOL_HoldRejectedPlayer(CBasePlayer& player);
bool CSOL_PrepareRejectedPlayer(edict_s* entity);
