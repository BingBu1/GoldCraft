// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only constructors and unrelated vtable targets. Exact SDK object
// layouts and inline accessors remain real; full GameRules lifecycle is not run.
#include "precompiled.h"
#include <cstdlib>
#include <iostream>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
namespace { [[noreturn]] void UnusedRule(const char* name) { std::cerr << "UNEXPECTED RULES STUB: " << name << "\n"; std::abort(); } }
CGameRules::CGameRules() { m_bFreezePeriod = FALSE; m_bBombDropped = FALSE; m_bGameOver = false; m_GameDesc = nullptr; }
CGameRules::~CGameRules() {}
CVoiceGameMgr::CVoiceGameMgr() : m_msgPlayerVoiceMask(0), m_msgRequestState(0), m_pHelper(nullptr), m_nMaxPlayers(0), m_UpdateInterval(0) {}
CVoiceGameMgr::~CVoiceGameMgr() {}
// Exact SDK static storage referenced by the two unrelated inline account-rule virtual methods.
RewardAccount CHalfLifeMultiplay::m_rgRewardAccountRules[RR_END]{};
CHalfLifeMultiplay::CHalfLifeMultiplay() { m_bFreezePeriod = FALSE; m_bGameStarted = true; m_bRoundTerminating = false; }
void CGameRules::RefreshSkillData() { UnusedRule("CGameRules::RefreshSkillData"); }
const char *CGameRules::GetGameDescription() { UnusedRule("CGameRules::GetGameDescription"); }
edict_t *CGameRules::GetPlayerSpawnSpot(CBasePlayer *pPlayer) { UnusedRule("CGameRules::GetPlayerSpawnSpot"); }
BOOL CGameRules::CanHavePlayerItem(CBasePlayer *pPlayer, CBasePlayerItem *pItem) { UnusedRule("CGameRules::CanHavePlayerItem"); }
BOOL CGameRules::CanHaveAmmo(CBasePlayer *pPlayer, const char *pszAmmoName, int iMaxCarry) { UnusedRule("CGameRules::CanHaveAmmo"); }
void CHalfLifeMultiplay::RefreshSkillData() { UnusedRule("CHalfLifeMultiplay::RefreshSkillData"); }
void CHalfLifeMultiplay::Think() { UnusedRule("CHalfLifeMultiplay::Think"); }
BOOL CHalfLifeMultiplay::IsAllowedToSpawn(CBaseEntity *pEntity) { UnusedRule("CHalfLifeMultiplay::IsAllowedToSpawn"); }
BOOL CHalfLifeMultiplay::FAllowFlashlight() { UnusedRule("CHalfLifeMultiplay::FAllowFlashlight"); }
BOOL CHalfLifeMultiplay::FShouldSwitchWeapon(CBasePlayer *pPlayer, CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::FShouldSwitchWeapon"); }
BOOL CHalfLifeMultiplay::GetNextBestWeapon(CBasePlayer *pPlayer, CBasePlayerItem *pCurrentWeapon) { UnusedRule("CHalfLifeMultiplay::GetNextBestWeapon"); }
BOOL CHalfLifeMultiplay::IsMultiplayer() { UnusedRule("CHalfLifeMultiplay::IsMultiplayer"); }
BOOL CHalfLifeMultiplay::IsDeathmatch() { UnusedRule("CHalfLifeMultiplay::IsDeathmatch"); }
BOOL CHalfLifeMultiplay::IsCoOp() { UnusedRule("CHalfLifeMultiplay::IsCoOp"); }
BOOL CHalfLifeMultiplay::ClientConnected(edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[128]) { UnusedRule("CHalfLifeMultiplay::ClientConnected"); }
void CHalfLifeMultiplay::InitHUD(CBasePlayer *pl) { UnusedRule("CHalfLifeMultiplay::InitHUD"); }
void CHalfLifeMultiplay::ClientDisconnected(edict_t *pClient) { UnusedRule("CHalfLifeMultiplay::ClientDisconnected"); }
void CHalfLifeMultiplay::UpdateGameMode(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::UpdateGameMode"); }
float CHalfLifeMultiplay::FlPlayerFallDamage(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::FlPlayerFallDamage"); }
BOOL CHalfLifeMultiplay::FPlayerCanTakeDamage(CBasePlayer *pPlayer, CBaseEntity *pAttacker) { UnusedRule("CHalfLifeMultiplay::FPlayerCanTakeDamage"); }
void CHalfLifeMultiplay::PlayerSpawn(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::PlayerSpawn"); }
void CHalfLifeMultiplay::PlayerThink(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::PlayerThink"); }
BOOL CHalfLifeMultiplay::FPlayerCanRespawn(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::FPlayerCanRespawn"); }
float CHalfLifeMultiplay::FlPlayerSpawnTime(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::FlPlayerSpawnTime"); }
edict_t *CHalfLifeMultiplay::GetPlayerSpawnSpot(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::GetPlayerSpawnSpot"); }
BOOL CHalfLifeMultiplay::AllowAutoTargetCrosshair() { UnusedRule("CHalfLifeMultiplay::AllowAutoTargetCrosshair"); }
BOOL CHalfLifeMultiplay::ClientCommand_DeadOrAlive(CBasePlayer *pPlayer, const char *pcmd) { UnusedRule("CHalfLifeMultiplay::ClientCommand_DeadOrAlive"); }
BOOL CHalfLifeMultiplay::ClientCommand(CBasePlayer *pPlayer, const char *pcmd) { UnusedRule("CHalfLifeMultiplay::ClientCommand"); }
void CHalfLifeMultiplay::ClientUserInfoChanged(CBasePlayer *pPlayer, char *infobuffer) { UnusedRule("CHalfLifeMultiplay::ClientUserInfoChanged"); }
int CHalfLifeMultiplay::IPointsForKill(CBasePlayer *pAttacker, CBasePlayer *pKilled) { UnusedRule("CHalfLifeMultiplay::IPointsForKill"); }
void CHalfLifeMultiplay::PlayerKilled(CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor) { UnusedRule("CHalfLifeMultiplay::PlayerKilled"); }
void CHalfLifeMultiplay::DeathNotice(CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor) { UnusedRule("CHalfLifeMultiplay::DeathNotice"); }
BOOL CHalfLifeMultiplay::CanHavePlayerItem(CBasePlayer *pPlayer, CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::CanHavePlayerItem"); }
void CHalfLifeMultiplay::PlayerGotWeapon(CBasePlayer *pPlayer, CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::PlayerGotWeapon"); }
int CHalfLifeMultiplay::WeaponShouldRespawn(CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::WeaponShouldRespawn"); }
float CHalfLifeMultiplay::FlWeaponRespawnTime(CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::FlWeaponRespawnTime"); }
float CHalfLifeMultiplay::FlWeaponTryRespawn(CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::FlWeaponTryRespawn"); }
Vector CHalfLifeMultiplay::VecWeaponRespawnSpot(CBasePlayerItem *pWeapon) { UnusedRule("CHalfLifeMultiplay::VecWeaponRespawnSpot"); }
BOOL CHalfLifeMultiplay::CanHaveItem(CBasePlayer *pPlayer, CItem *pItem) { UnusedRule("CHalfLifeMultiplay::CanHaveItem"); }
void CHalfLifeMultiplay::PlayerGotItem(CBasePlayer *pPlayer, CItem *pItem) { UnusedRule("CHalfLifeMultiplay::PlayerGotItem"); }
int CHalfLifeMultiplay::ItemShouldRespawn(CItem *pItem) { UnusedRule("CHalfLifeMultiplay::ItemShouldRespawn"); }
float CHalfLifeMultiplay::FlItemRespawnTime(CItem *pItem) { UnusedRule("CHalfLifeMultiplay::FlItemRespawnTime"); }
Vector CHalfLifeMultiplay::VecItemRespawnSpot(CItem *pItem) { UnusedRule("CHalfLifeMultiplay::VecItemRespawnSpot"); }
void CHalfLifeMultiplay::PlayerGotAmmo(CBasePlayer *pPlayer, char *szName, int iCount) { UnusedRule("CHalfLifeMultiplay::PlayerGotAmmo"); }
int CHalfLifeMultiplay::AmmoShouldRespawn(CBasePlayerAmmo *pAmmo) { UnusedRule("CHalfLifeMultiplay::AmmoShouldRespawn"); }
float CHalfLifeMultiplay::FlAmmoRespawnTime(CBasePlayerAmmo *pAmmo) { UnusedRule("CHalfLifeMultiplay::FlAmmoRespawnTime"); }
Vector CHalfLifeMultiplay::VecAmmoRespawnSpot(CBasePlayerAmmo *pAmmo) { UnusedRule("CHalfLifeMultiplay::VecAmmoRespawnSpot"); }
float CHalfLifeMultiplay::FlHealthChargerRechargeTime() { UnusedRule("CHalfLifeMultiplay::FlHealthChargerRechargeTime"); }
float CHalfLifeMultiplay::FlHEVChargerRechargeTime() { UnusedRule("CHalfLifeMultiplay::FlHEVChargerRechargeTime"); }
int CHalfLifeMultiplay::DeadPlayerWeapons(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::DeadPlayerWeapons"); }
int CHalfLifeMultiplay::DeadPlayerAmmo(CBasePlayer *pPlayer) { UnusedRule("CHalfLifeMultiplay::DeadPlayerAmmo"); }
int CHalfLifeMultiplay::PlayerRelationship(CBasePlayer *pPlayer, CBaseEntity *pTarget) { UnusedRule("CHalfLifeMultiplay::PlayerRelationship"); }
void CHalfLifeMultiplay::ChangePlayerTeam(CBasePlayer *pPlayer, const char *pTeamName, BOOL bKill, BOOL bGib) { UnusedRule("CHalfLifeMultiplay::ChangePlayerTeam"); }
BOOL CHalfLifeMultiplay::FAllowMonsters() { UnusedRule("CHalfLifeMultiplay::FAllowMonsters"); }
void CHalfLifeMultiplay::ServerDeactivate() { UnusedRule("CHalfLifeMultiplay::ServerDeactivate"); }
void CHalfLifeMultiplay::CheckMapConditions() { UnusedRule("CHalfLifeMultiplay::CheckMapConditions"); }
void CHalfLifeMultiplay::CleanUpMap() { UnusedRule("CHalfLifeMultiplay::CleanUpMap"); }
void CHalfLifeMultiplay::RestartRound() { UnusedRule("CHalfLifeMultiplay::RestartRound"); }
void CHalfLifeMultiplay::CheckWinConditions() { UnusedRule("CHalfLifeMultiplay::CheckWinConditions"); }
void CHalfLifeMultiplay::RemoveGuns() { UnusedRule("CHalfLifeMultiplay::RemoveGuns"); }
CBasePlayer *CHalfLifeMultiplay::GiveC4() { UnusedRule("CHalfLifeMultiplay::GiveC4"); }
void CHalfLifeMultiplay::ChangeLevel() { UnusedRule("CHalfLifeMultiplay::ChangeLevel"); }
void CHalfLifeMultiplay::GoToIntermission() { UnusedRule("CHalfLifeMultiplay::GoToIntermission"); }
void CHalfLifeMultiplay::InitializePlayerCounts(int &NumAliveTerrorist, int &NumAliveCT, int &NumDeadTerrorist, int &NumDeadCT) { UnusedRule("CHalfLifeMultiplay::InitializePlayerCounts"); }
void CHalfLifeMultiplay::BalanceTeams() { UnusedRule("CHalfLifeMultiplay::BalanceTeams"); }
void CHalfLifeMultiplay::SwapAllPlayers() { UnusedRule("CHalfLifeMultiplay::SwapAllPlayers"); }
void CHalfLifeMultiplay::UpdateTeamScores() { UnusedRule("CHalfLifeMultiplay::UpdateTeamScores"); }
void CHalfLifeMultiplay::EndRoundMessage(const char *sentence, ScenarioEventEndRound event) { UnusedRule("CHalfLifeMultiplay::EndRoundMessage"); }
BOOL CHalfLifeMultiplay::IsThereABomber() { UnusedRule("CHalfLifeMultiplay::IsThereABomber"); }
BOOL CHalfLifeMultiplay::IsThereABomb() { UnusedRule("CHalfLifeMultiplay::IsThereABomb"); }
TeamName CHalfLifeMultiplay::SelectDefaultTeam() { UnusedRule("CHalfLifeMultiplay::SelectDefaultTeam"); }
bool CHalfLifeMultiplay::HasRoundTimeExpired() { UnusedRule("CHalfLifeMultiplay::HasRoundTimeExpired"); }
bool CHalfLifeMultiplay::IsBombPlanted() { UnusedRule("CHalfLifeMultiplay::IsBombPlanted"); }
void CHalfLifeMultiplay::SendDeathMessage(CBaseEntity *pKiller, CBasePlayer *pVictim, CBasePlayer *pAssister, entvars_t *pevInflictor, const char *killerWeaponName, int iDeathMessageFlags, int iRarityOfKill) { UnusedRule("CHalfLifeMultiplay::SendDeathMessage"); }
#pragma clang diagnostic pop
