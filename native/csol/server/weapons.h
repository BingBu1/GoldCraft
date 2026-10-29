// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

// Original CSO IDs. Inventory and prediction storage are separate from IDs.
constexpr int CSOL_WEAPON_TKNIFEEX2 = 220;
constexpr int CSOL_WEAPON_CANNON = 144;
constexpr int CSOL_WEAPON_BEAMGUN = 725;
constexpr int CSOL_WEAPON_BEAMGUNLE = 726;
class CBasePlayerWeapon;
class CBasePlayerItem;
class CBaseEntity;
class CBasePlayer;
class CCSOLScenarioMonster;
struct entvars_s;
// A projectile carries the launch weapon ID independently of its owner's
// active item. This is the CSO v138 contract, not a death-notice name.
class CSOLWeaponDamageSource
{
public:
    virtual int DamageWeaponId() const = 0;
protected:
    // GoldSrc owns entity private data and has only placement delete. This
    // non-owning view must not introduce a virtual deleting destructor.
    ~CSOLWeaponDamageSource() = default;
};
// Optional, explicit adapters for actual weapon implementations. GoldSrc owns
// private data; this non-owning interface must not add a deleting destructor.
class CSOLWeaponMonsterKillCallbacks
{
public:
    virtual void ActiveMonsterKilled(CCSOLScenarioMonster& monster, CBaseEntity* inflictor) = 0;
    virtual void SourceMonsterKilled(CCSOLScenarioMonster& monster, CBaseEntity* inflictor) = 0;
protected:
    ~CSOLWeaponMonsterKillCallbacks() = default;
};
// Original v230/v228 for the separate pre-death melee impulse. Implemented
// weapons supply real predicates/parameters; absence is not an enabled flag.
class CSOLWeaponMonsterMelee
{
public:
    virtual std::uint8_t MonsterMeleeType() = 0;
    virtual std::uint8_t MonsterMeleeParameters(std::array<float, 7>& values) = 0;
protected:
    ~CSOLWeaponMonsterMelee() = default;
};
int CSOL_DamageWeaponId(CBaseEntity* inflictor, CBasePlayer* attacker);
// Exact compiled types whose original v1E4/v1E8 are both ret 8. Each matcher
// lives with its class definition; IDs/classnames alone are not type proof.
bool CSOL_CannonEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_BeamEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_TKnifeEmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon);
bool CSOL_IsWeapon(const CBasePlayerWeapon* weapon);
const char* CSOL_DeathWeapon(const entvars_s* inflictor);
const char* CSOL_WorldModel(const CBasePlayerItem* item);
void CSOL_InitWeapons();
void CSOL_PrecacheWeapons();
bool CSOL_WeaponsReady();
#ifdef GOLDCRAFT_CSOL_WEAPON_TESTS
void CSOL_InitWeaponTests();
void CSOL_PrecacheWeaponTests();
void CSOL_InitBotTests();
void CSOL_ResetBotTests();
bool CSOL_BotTestCommand(edict_t* entity, const char* command, const char* argument);
#endif
