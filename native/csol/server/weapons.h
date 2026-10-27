// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Original CSO IDs. Inventory and prediction storage are separate from IDs.
constexpr int CSOL_WEAPON_TKNIFEEX2 = 220;
constexpr int CSOL_WEAPON_CANNON = 144;
constexpr int CSOL_WEAPON_BEAMGUN = 725;
constexpr int CSOL_WEAPON_BEAMGUNLE = 726;
class CBasePlayerWeapon;
class CBasePlayerItem;
class CBaseEntity;
class CBasePlayer;
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
int CSOL_DamageWeaponId(CBaseEntity* inflictor, CBasePlayer* attacker);
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
