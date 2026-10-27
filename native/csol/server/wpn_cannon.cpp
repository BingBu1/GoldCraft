// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapons.h"
#include "wpn_cannon.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "../cannon.hpp"
#include "../cannon_resources.hpp"
#include <rapidjson/document.h>
#include <cmath>

namespace {
int effectMessage{};
bool precached{}, damageLoaded{};
float normalDamage{}, zombieDamage{}, scenarioDamage{}, escapeDamage{};
cvar_t damageMode={"csol_cannon_damage_mode","0",FCVAR_SERVER,0,nullptr};
bool LoadDamage()
{
    int size=0;
    auto* bytes=LOAD_FILE_FOR_ME("csol/weapons/cannon_damage.json",&size);
    if (!bytes) { ALERT(at_error,"CSOL: missing cannon_damage.json\n"); return false; }
    rapidjson::Document doc;
    if (size>0 && size<=65536) doc.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<char*>(bytes),size);
    FREE_FILE(bytes);
    const char* fields[]={"Damage","DamageZombieMode","DamageScenario","DamageZombieESCMode"};
    float values[4]{};
    bool valid=!doc.HasParseError() && doc.IsObject() && doc.MemberCount()==5 && doc.HasMember("version") &&
        doc["version"].IsInt() && doc["version"].GetInt()==1;
    for (int i=0; valid && i<4; ++i) {
        valid=doc.HasMember(fields[i]) && doc[fields[i]].IsNumber();
        if (valid) {
            const double value=doc[fields[i]].GetDouble();
            valid=std::isfinite(value) && value>=0 && value<=1000000;
            values[i]=float(value);
        }
    }
    if (!valid) { ALERT(at_error,"CSOL: invalid cannon_damage.json; keeping previous valid values\n"); return false; }
    normalDamage=values[0];zombieDamage=values[1];scenarioDamage=values[2];escapeDamage=values[3];damageLoaded=true;
    SERVER_PRINT("CSOL: loaded cannon_damage.json\n");return true;
}
void ReloadDamage() { if (CMD_ARGC()==1) LoadDamage(); }
csol::cannon::Point Point(const Vector& v) { return {v.x,v.y,v.z}; }
}

class CCannon : public CBasePlayerWeapon {
public:
    int iItemSlot() override { return PRIMARY_WEAPON_SLOT; }
    float GetMaxSpeed() override { return 235; }
    BOOL UseDecrement() override { return TRUE; }
    BOOL CanDeploy() override { return CSOL_WeaponsReady() && CSOL_CannonReady(); }
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void PrimaryAttack() override;
    void SecondaryAttack() override {} // Original gate requires enhancement >=6.
    void Reload() override {} // The firing animation contains the loading motion.
    void WeaponIdle() override;
};
LINK_ENTITY_TO_CLASS(weapon_cannon, CCannon, CCSPlayerWeapon)

void CCannon::Precache()
{
    for (const auto* path:csol::CannonResources) {
        if (!Q_strncmp(path,"sound/",6)) PRECACHE_SOUND(const_cast<char*>(path+6));
        else if (Q_strstr(path,".txt")) PRECACHE_GENERIC(const_cast<char*>(path));
        else PRECACHE_MODEL(const_cast<char*>(path));
    }
}
void CCannon::Spawn()
{
    if (!CanDeploy()) { UTIL_Remove(this); return; }
    Precache();m_iId=csol::cannon::Id;
    SET_MODEL(edict(),"models/w_cannon.mdl");
    m_iDefaultAmmo=csol::cannon::Capacity;
    FallInit();CBasePlayerWeapon::Spawn();
}
int CCannon::GetItemInfo(ItemInfo* info)
{
    info->pszName="weapon_cannon";info->pszAmmo1="cannon";
    info->iMaxAmmo1=csol::cannon::Capacity;info->pszAmmo2=nullptr;info->iMaxAmmo2=-1;
    info->iMaxClip=csol::cannon::Capacity;info->iSlot=0;info->iPosition=1;
    info->iId=csol::cannon::Id;info->iWeight=25;info->iFlags=0;
    return 1;
}
BOOL CCannon::Deploy()
{
    m_fMaxSpeed=235;m_flAccuracy=0;m_iShotsFired=0;m_bDelayFire=false;m_fInReload=FALSE;
    const char* pose=m_pPlayer->LookupSequence("ref_aim_carbine")>=0 &&
        m_pPlayer->LookupSequence("ref_shoot_carbine")>=0 ? "carbine" : "m249";
    return DefaultDeploy("models/v_cannon.mdl","models/p_cannon.mdl",2,const_cast<char*>(pose),UseDecrement());
}
void CCannon::PrimaryAttack()
{
    if (m_pPlayer->pev->button & IN_ATTACK2) return;
    if (m_iClip<=0) {
        PlayEmptySound();m_flNextPrimaryAttack=m_flNextSecondaryAttack=csol::cannon::EmptyInterval;return;
    }
    --m_iClip;
    m_pPlayer->pev->effects|=EF_MUZZLEFLASH;
    m_pPlayer->m_iWeaponVolume=2048;m_pPlayer->m_iWeaponFlash=BRIGHT_GUN_FLASH;
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    const Vector origin=m_pPlayer->GetGunPosition();
    const Vector angles=m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle;
    // MP area attack rebuilds its direction from v_angle; the client fire
    // event separately uses v_angle + punchangle for the flame cloud.
    UTIL_MakeVectors(m_pPlayer->pev->v_angle);const Vector forward=gpGlobals->v_forward;
    const int mode=int(damageMode.value);
    const float base=mode==2?scenarioDamage:mode==3?escapeDamage:(mode==1 || CSOL_ZombieMode())?zombieDamage:normalDamage;
    const bool water=UTIL_PointContents(origin)==CONTENTS_WATER;
    CBaseEntity* target=nullptr;
    while ((target=UTIL_FindEntityInSphere(target,origin,csol::cannon::Distance))) {
        if (target==m_pPlayer || target->pev->takedamage==DAMAGE_NO || base<=0 ||
            (water && target->pev->waterlevel==0) || (!water && target->pev->waterlevel==3)) continue;
        const Vector body=target->BodyTarget(origin);
        const auto normalized=csol::cannon::Normalize(Point(body-origin));
        const Vector direction(normalized[0],normalized[1],normalized[2]);
        if (DotProduct(direction,forward)<0 || !csol::cannon::Intersects(Point(origin),Point(forward),
            Point(target->pev->absmin),Point(target->pev->absmax))) continue;
        TraceResult trace;
        UTIL_TraceLine(origin,body,ignore_monsters,m_pPlayer->edict(),&trace);
        if (trace.flFraction<1 && trace.pHit!=target->edict()) continue;
        trace.pHit=target->edict();trace.vecEndPos=body;trace.iHitgroup=HITGROUP_GENERIC;
        ClearMultiDamage();
        // MP 11421df8 seeds the accumulator before TraceAttack(DMG_BURN).
        // Keep the bullet/never-gib flags for armor, friendly fire and glass.
        gMultiDamage.type=DMG_BULLET|DMG_NEVERGIB;
        target->TraceAttack(m_pPlayer->pev,csol::cannon::Damage(base,csol::cannon::Length(Point(body-origin))),direction,&trace,DMG_BURN);
        ApplyMultiDamage(m_pPlayer->pev,m_pPlayer->pev);
    }
    MESSAGE_BEGIN(MSG_ALL,effectMessage);
        WRITE_BYTE(2);WRITE_BYTE(m_pPlayer->entindex());WRITE_BYTE((m_pPlayer->pev->flags&FL_DUCKING)!=0);
        for (int i=0;i<3;++i) WRITE_COORD(origin[i]);
        for (int i=0;i<3;++i) WRITE_COORD(angles[i]);
        for (int i=0;i<3;++i) WRITE_COORD(m_pPlayer->pev->velocity[i]);
        WRITE_LONG(m_pPlayer->random_seed);
    MESSAGE_END();
    SendWeaponAnim(1,UseDecrement());
    m_flNextPrimaryAttack=m_flNextSecondaryAttack=m_flTimeWeaponIdle=csol::cannon::Interval;
    if (!(m_pPlayer->pev->flags&FL_ONGROUND)) KickBack(13,5,1.85f,.55f,15,5.7f,2);
    else if (m_pPlayer->pev->velocity.Length2D()>0) KickBack(13,2.25f,1.45f,.7f,12,10,1);
    else if (m_pPlayer->pev->flags&FL_DUCKING) KickBack(9,2.1f,1.25f,.5f,15,5.5f,1);
    else KickBack(13,3.2f,1.5f,.5f,15,10,2);
}
void CCannon::WeaponIdle()
{
    ResetEmptySound();
    if (m_flTimeWeaponIdle>0) return;
    SendWeaponAnim(0,UseDecrement());m_flTimeWeaponIdle=20;
}
void CSOL_InitCannon() { CVAR_REGISTER(&damageMode);ADD_SERVER_COMMAND("csol_reload_cannon_damage",ReloadDamage); }
void CSOL_PrecacheCannon()
{
    precached=false;
    if (!effectMessage) effectMessage=REG_USER_MSG(csol::cannon::EffectMessage,csol::cannon::EffectBytes);
    if (!LoadDamage()) return;
    UTIL_PrecacheOtherWeapon("weapon_cannon");
    const auto* info=CSOL_ItemInfo(csol::cannon::Id);precached=info && info->pszName;
}
bool CSOL_CannonReady() { return precached && damageLoaded; }
bool CSOL_ClipAmmo(const CBasePlayerWeapon* weapon)
{
    return weapon && weapon->m_iId==csol::cannon::Id && FClassnameIs(weapon->pev,"weapon_cannon");
}
int CSOL_GiveClipAmmo(CBasePlayer* player,CBasePlayerWeapon* weapon,int count)
{
    if (!player || !CSOL_ClipAmmo(weapon)) return 0;
    const int added=csol::cannon::AddAmmo(weapon->m_iClip,count);
    if (added) {CSOL_SendAmmoPickup(player,"cannon",added);CSOL_UpdateWeaponInventory(player);}
    return added;
}
