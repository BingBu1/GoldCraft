// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "../../native/csol/server/game_rules_gate.h"
#include <cstdlib>

// Only the actual SDK speed body's rules query and routing edge are controlled.
// This is not a replacement for later complete GameRules/Session validation.
bool g_bIsCzeroGame{};
int gmsgMoney=77;
cvar_t maxmoney={const_cast<char*>("mp_maxmoney"),const_cast<char*>("16000"),0,16000};
CReGameHookchains g_ReGameHookchains;
namespace
{
[[noreturn]] void Forbidden(const char* name)
{std::cerr<<"UNEXPECTED UPGRADE SDK BOUNDARY "<<name<<'\n';std::abort();}
struct Rules final : CGameRules
{
    BOOL IsMultiplayer() override {return TRUE;}
    void Think() override {Forbidden("Think");}
    BOOL IsAllowedToSpawn(CBaseEntity*) override {Forbidden("IsAllowedToSpawn");}
    BOOL FAllowFlashlight() override {Forbidden("FAllowFlashlight");}
    BOOL FShouldSwitchWeapon(CBasePlayer*,CBasePlayerItem*) override {Forbidden("FShouldSwitchWeapon");}
    BOOL GetNextBestWeapon(CBasePlayer*,CBasePlayerItem*) override {Forbidden("GetNextBestWeapon");}
    BOOL IsDeathmatch() override {Forbidden("IsDeathmatch");}
    BOOL IsCoOp() override {Forbidden("IsCoOp");}
    BOOL ClientConnected(edict_t*,const char*,const char*,char*) override {Forbidden("ClientConnected");}
    void InitHUD(CBasePlayer*) override {Forbidden("InitHUD");}
    void ClientDisconnected(edict_t*) override {Forbidden("ClientDisconnected");}
    float FlPlayerFallDamage(CBasePlayer*) override {Forbidden("FlPlayerFallDamage");}
    void PlayerSpawn(CBasePlayer*) override {Forbidden("PlayerSpawn");}
    void PlayerThink(CBasePlayer*) override {Forbidden("PlayerThink");}
    BOOL FPlayerCanRespawn(CBasePlayer*) override {Forbidden("FPlayerCanRespawn");}
    float FlPlayerSpawnTime(CBasePlayer*) override {Forbidden("FlPlayerSpawnTime");}
    int IPointsForKill(CBasePlayer*,CBasePlayer*) override {Forbidden("IPointsForKill");}
    void PlayerKilled(CBasePlayer*,entvars_t*,entvars_t*) override {Forbidden("PlayerKilled");}
    void DeathNotice(CBasePlayer*,entvars_t*,entvars_t*) override {Forbidden("DeathNotice");}
    void PlayerGotWeapon(CBasePlayer*,CBasePlayerItem*) override {Forbidden("PlayerGotWeapon");}
    int WeaponShouldRespawn(CBasePlayerItem*) override {Forbidden("WeaponShouldRespawn");}
    float FlWeaponRespawnTime(CBasePlayerItem*) override {Forbidden("FlWeaponRespawnTime");}
    float FlWeaponTryRespawn(CBasePlayerItem*) override {Forbidden("FlWeaponTryRespawn");}
    Vector VecWeaponRespawnSpot(CBasePlayerItem*) override {Forbidden("VecWeaponRespawnSpot");}
    BOOL CanHaveItem(CBasePlayer*,CItem*) override {Forbidden("CanHaveItem");}
    void PlayerGotItem(CBasePlayer*,CItem*) override {Forbidden("PlayerGotItem");}
    int ItemShouldRespawn(CItem*) override {Forbidden("ItemShouldRespawn");}
    float FlItemRespawnTime(CItem*) override {Forbidden("FlItemRespawnTime");}
    Vector VecItemRespawnSpot(CItem*) override {Forbidden("VecItemRespawnSpot");}
    void PlayerGotAmmo(CBasePlayer*,char*,int) override {Forbidden("PlayerGotAmmo");}
    int AmmoShouldRespawn(CBasePlayerAmmo*) override {Forbidden("AmmoShouldRespawn");}
    float FlAmmoRespawnTime(CBasePlayerAmmo*) override {Forbidden("FlAmmoRespawnTime");}
    Vector VecAmmoRespawnSpot(CBasePlayerAmmo*) override {Forbidden("VecAmmoRespawnSpot");}
    float FlHealthChargerRechargeTime() override {Forbidden("FlHealthChargerRechargeTime");}
    int DeadPlayerWeapons(CBasePlayer*) override {Forbidden("DeadPlayerWeapons");}
    int DeadPlayerAmmo(CBasePlayer*) override {Forbidden("DeadPlayerAmmo");}
    const char* GetTeamID(CBaseEntity*) override {Forbidden("GetTeamID");}
    int PlayerRelationship(CBasePlayer*,CBaseEntity*) override {Forbidden("PlayerRelationship");}
    BOOL FAllowMonsters() override {Forbidden("FAllowMonsters");}
};
}
std::unique_ptr<CGameRules> upgrade_test::MakeRules(){return std::make_unique<Rules>();}
void Sys_Error(const char*,...){Forbidden("Sys_Error");}
void CGameRules::RefreshSkillData(){Forbidden("RefreshSkillData");}
BOOL CGameRules::CanHaveAmmo(CBasePlayer*,const char*,int){Forbidden("CanHaveAmmo");}
BOOL CGameRules::CanHavePlayerItem(CBasePlayer*,CBasePlayerItem*){Forbidden("CanHavePlayerItem");}
edict_t* CGameRules::GetPlayerSpawnSpot(CBasePlayer*){Forbidden("GetPlayerSpawnSpot");}

void CSOL_ApplyScenarioMovement(CBasePlayer& player,float base)
{
    auto& engine=*upgrade_test::Engine::active;++engine.movementCalls;
    auto state=engine.movement.lock();
    upgrade_test::Require(state&&state->WallState()->player.Get<CBaseEntity>()==&player,"actual SDK movement callback routes bound player");
    if(!state->ApplyMovement(base))engine.rejected=true;
}
bool CSOL_RulesRejected(){return upgrade_test::Engine::active->rejected;}
