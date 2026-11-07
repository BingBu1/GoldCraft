// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>

class CGameRules;
class CHalfLifeMultiplay;
class CBasePlayer;
class CBaseEntity;
class CBasePlayerWeapon;
class CCSOLScenarioMap;
class CCSOLMonsterPool;
class CCSOLScenarioSession;
class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioRuntime;
class CSOLMonsterDeathCall;
namespace csol::scenario { struct ConfigError; }
struct edict_s;
struct entvars_s;
struct playermove_s;
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
bool CSOL_InjectScenarioPlayerClips(playermove_s& movement);
bool CSOL_ScenarioProtectionBlocks(CBasePlayer& player);
bool CSOL_ScenarioApplyProtectionDamage(CBasePlayer& player, int damageType, float& damage);
bool CSOL_UpdateScenarioPlayerBuffs(CBasePlayer& player);
bool CSOL_UpdateScenarioHud(CBasePlayer& player, bool force);
bool CSOL_ScenarioPlayerPostThink(CBasePlayer& player);
// Automatic revival also runs for late-join observers, before PreThink's
// observer return. False stops the old frame after Spawn or retirement.
bool CSOL_ScenarioPlayerRespawnThink(CBasePlayer& player);
bool CSOL_ScenarioPlayerDeathClock(CBasePlayer& player);
void CSOL_ScenarioPlayerSpawnStarted(CBasePlayer& player);
bool CSOL_ScenarioCharacterSkillCommand(CBasePlayer& player, const char* command);
bool CSOL_ScenarioAbsorbSkillDamage(CBasePlayer& player, entvars_s* attacker, float& damage);
bool CSOL_ScenarioDodgeAllowsDamage(CBasePlayer& player);
bool CSOL_ScenarioApplyIncomingBuffDamage(CBasePlayer& player, float& damage, int damageType);
// Call once after the original scenario rule adjustments, before any
// protection/energy-shield/armor rejection. This is distinct from v1B8 below.
bool CSOL_ScenarioNotifyBeforeProtectionSkillDamage(CBasePlayer& player,
    entvars_s* attacker, float& damage);
bool CSOL_ScenarioNotifyIncomingSkillDamage(CBasePlayer& player, entvars_s* attacker, float& damage);
bool CSOL_ScenarioSkillReload(CBasePlayerWeapon& weapon);
void CSOL_ScenarioPlayerSkillDied(CBasePlayer& player);
bool CSOL_ScenarioSkillMaximumHealth(CBasePlayer& player, float base, float& maximum);
std::shared_ptr<CCSOLScenarioRuntime> CSOL_ScenarioRuntimeForAccess(const CSOLScenarioEntityAccess* access);
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
// Purchase-only source identity, applied to the new unowned K3 before Touch.
bool CSOL_ScenarioSetK3Identity(CBasePlayerWeapon& weapon);
int CSOL_ScenarioProjectileSource(const CBaseEntity& entity);
bool CSOL_ScenarioRestartRound(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioCheckWinConditions(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioPrepareSpawn(CHalfLifeMultiplay& rules, CBasePlayer& player);
// Only the primary block of GiveDefaultItems, after its native inventory
// reset. Cancellation must stop the caller before any more equipment access.
enum class CSOLScenarioDefaultPrimary { Native, Granted, Cancelled };
CSOLScenarioDefaultPrimary CSOL_ScenarioGiveDefaultPrimary(CBasePlayer& player);
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
