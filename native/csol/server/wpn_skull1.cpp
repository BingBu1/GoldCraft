// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_skull1.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "../skull1.hpp"
#include "../skull_balrog_damage.hpp"
#include <cstdlib>
#include <typeinfo>

namespace {
bool precached{},damageLoaded{};
std::array<float,6> damage{};
bool LoadDamage()
{
    int size{};auto* bytes=LOAD_FILE_FOR_ME("csol/weapons/skull1_damage.json",&size);
    if(!bytes){ALERT(at_error,"CSOL: missing skull1_damage.json\n");return false;}
    std::string error;
    const bool valid=size>0&&size<=65536&&csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes),std::size_t(size)},csol::skull1::DamageFields,damage,error);
    FREE_FILE(bytes);
    if(!valid){ALERT(at_error,"CSOL: invalid skull1_damage.json: %s\n",error.c_str());return false;}
    damageLoaded=true;return true;
}
void ReloadDamage(){if(CMD_ARGC()==1)LoadDamage();}
float Damage(bool rapid)
{
    const auto mode=CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode()?CSOLWeaponDamageMode::Zombie:CSOLWeaponDamageMode::Normal);
    const int index=mode==CSOLWeaponDamageMode::Scenario?2:mode==CSOLWeaponDamageMode::Zombie?1:0;
    return damage[(rapid?3:0)+index];
}
}

class CSkull1 final:public CBasePlayerWeapon
{
public:
    int iItemSlot() override{return PISTOL_SLOT;}
    float GetMaxSpeed() override{return csol::skull1::MaxSpeed;}
    BOOL UseDecrement() override{return TRUE;}
    BOOL IsPistol() override{return TRUE;}
    BOOL CanDeploy() override{return CSOL_WeaponsReady()&&CSOL_Skull1Ready();}
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal=0) override{m_deployed=false;CBasePlayerWeapon::Holster(skiplocal);}
    bool DeploymentComplete()const{return m_deployed;}
    void PrimaryAttack() override{Fire(false);}
    void SecondaryAttack() override{Fire(true);}
    void Reload() override;
    void WeaponIdle() override;
    BOOL PlayEmptySound() override;
    bool Rapid()const{return m_rapid;}
private:
    void Fire(bool rapid);
    unsigned short m_fireEvent{},m_rapidEvent{};
    bool m_rapid{};
    bool m_deployed{};
};
LINK_ENTITY_TO_CLASS(weapon_skull1,CSkull1,CCSPlayerWeapon)

bool CSOL_Skull1EmptyMonsterKillCallbacks(const CBasePlayerWeapon& weapon)
{
    // Original vtable1195377C v1E4/v1E8 are both10611B90 (ret8).
    return typeid(weapon)==typeid(CSkull1)&&weapon.m_iId==csol::skull1::Id;
}
bool CSOL_Skull1DeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_Skull1EmptyMonsterKillCallbacks(weapon) && static_cast<const CSkull1&>(weapon).DeploymentComplete();
}
bool CSOL_Skull1ScenarioKnockback(const CBasePlayerWeapon& weapon,std::array<float,5>& values)
{
    if(!CSOL_Skull1EmptyMonsterKillCallbacks(weapon))return false;
    // MP111A4C20 overrides the generic WeaponMgr record. Mode15 selects
    // the scenario properties; the rapid flag belongs to this weapon entity.
    // The ordinary-monster caller never consumes vD4's sixth (flinch) output.
    values={100,1,1,1,static_cast<const CSkull1&>(weapon).Rapid()?.3f:.2f};
    return true;
}
void CSOL_PackSkull1Weapon(const CBasePlayerWeapon* weapon,weapon_data_t* data)
{
    if(!weapon||!data||!CSOL_Skull1EmptyMonsterKillCallbacks(*weapon))return;
    data->fuser1=weapon->m_flAccuracy;
    // fuser2 is transmitted as a signed countdown. Zero previous-fire uses
    // the independent state bit because server time itself can also be zero.
    // After one second both original modes have fully recovered. Capping
    // the age preserves that result while fitting the installed delta.
    data->fuser2=csol::skull1::PackFireAge(weapon->m_flLastFire,gpGlobals->time);
    data->fuser3=csol::skull1::PackShots(weapon->m_iShotsFired);
    data->m_iWeaponState=(static_cast<const CSkull1*>(weapon)->Rapid()?1:0)|(weapon->m_flLastFire!=0?2:0);
}
void CSkull1::Precache()
{
    for(const char* path:{csol::skull1::ViewModel,csol::skull1::PlayerModel,csol::skull1::WorldModel,
        "models/pshell.mdl","sprites/csol/640hud64.spr","sprites/csol/640hud2.spr","sprites/csol/640hud4.spr",
        "sprites/pistol_smoke1.spr","sprites/pistol_smoke2.spr","sprites/wall_puff1.spr",
        "sprites/wall_puff2.spr","sprites/wall_puff3.spr","sprites/wall_puff4.spr"})
        PRECACHE_MODEL(const_cast<char*>(path));
    for(const char* path:{"weapons/skull1-1.wav","weapons/skull1-2.wav",
        "weapons/skull1_clipout.wav","weapons/skull1_clipin.wav",
        "weapons/skull1_draw.wav","weapons/dryfire_pistol.wav"})PRECACHE_SOUND(const_cast<char*>(path));
    PRECACHE_GENERIC("sprites/csol/weapon_skull1_v1.txt");
    m_fireEvent=PRECACHE_EVENT(1,csol::skull1::Event);
    m_rapidEvent=PRECACHE_EVENT(1,csol::skull1::RapidEvent);
}
void CSkull1::Spawn()
{
    if(!CanDeploy()){UTIL_Remove(this);return;}
    Precache();m_iId=csol::skull1::Id;SET_MODEL(edict(),csol::skull1::WorldModel);
    m_iDefaultAmmo=csol::skull1::Clip;m_flAccuracy=csol::skull1::Primary.accuracyMaximum;m_rapid=false;
    FallInit();CBasePlayerWeapon::Spawn();
}
int CSkull1::GetItemInfo(ItemInfo* info)
{
    info->pszName=csol::skull1::Classname;info->pszAmmo1=csol::skull1::AmmoName;
    info->iMaxAmmo1=csol::skull1::Reserve;info->pszAmmo2=nullptr;info->iMaxAmmo2=-1;
    info->iMaxClip=csol::skull1::Clip;info->iSlot=1;info->iPosition=1;
    info->iId=m_iId=csol::skull1::Id;info->iFlags=0;info->iWeight=6;return 1;
}
BOOL CSkull1::Deploy()
{
    m_deployed=false;
    m_flAccuracy=csol::skull1::Primary.accuracyMaximum;
    if(!DefaultDeploy(const_cast<char*>(csol::skull1::ViewModel),const_cast<char*>(csol::skull1::PlayerModel),
        csol::skull1::Draw,"onehanded",UseDecrement()))return FALSE;
    m_pPlayer->m_flNextAttack=UTIL_WeaponTimeBase()+csol::skull1::DeployTime;
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull1::DeployIdle;m_deployed=true;return TRUE;
}
void CSkull1::Fire(bool rapid)
{
    m_rapid=rapid;const auto& mode=csol::skull1::Parameters(rapid);
    m_flAccuracy=mode.accuracyMaximum;
    const bool ground=(m_pPlayer->pev->flags&FL_ONGROUND)!=0,duck=(m_pPlayer->pev->flags&FL_DUCKING)!=0;
    const float speed=m_pPlayer->pev->velocity.Length2D();
    const float spread=csol::skull1::Spread(rapid,ground,speed,duck);
    if(!csol::skull1::TriggerAllowed(rapid,m_iShotsFired))return;
    m_flAccuracy=csol::skull1::AccuracyAfterAttempt(rapid,m_flLastFire,gpGlobals->time);
    m_flLastFire=gpGlobals->time;
    if(m_iClip<=0){
        if(m_fFireOnEmpty){PlayEmptySound();m_flNextPrimaryAttack=UTIL_WeaponTimeBase()+.2f;}
        if(TheBots)TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY,m_pPlayer);return;
    }
    --m_iClip;m_pPlayer->pev->effects|=EF_MUZZLEFLASH;m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    m_pPlayer->m_iWeaponVolume=BIG_EXPLOSION_VOLUME;m_pPlayer->m_iWeaponFlash=NORMAL_GUN_FLASH;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle);
    // MP bullet27 (50AZ) has the same trace penetration/continuation contract
    // as stock50AE: power30, range1000, entity continuation75%. Ammo identity
    // and primary/rapid damage stay specific to this weapon.
    Vector source=m_pPlayer->GetGunPosition(),forward=gpGlobals->v_forward;
    const Vector direction=m_pPlayer->FireBullets3(source,forward,
        spread,csol::skull1::Distance,csol::skull1::Penetration,BULLET_PLAYER_50AE,
        int(Damage(rapid)),.8f,m_pPlayer->pev,true,m_pPlayer->random_seed);
    float zero[3]{};
    PLAYBACK_EVENT_FULL(FEV_NOTHOST,m_pPlayer->edict(),rapid?m_rapidEvent:m_fireEvent,0,zero,zero,
        direction.x,direction.y,int(m_pPlayer->pev->punchangle.x*100),int(m_pPlayer->pev->punchangle.y*100),m_iClip==0,FALSE);
    m_flNextPrimaryAttack=m_flNextSecondaryAttack=UTIL_WeaponTimeBase()+mode.interval;
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull1::FireIdle;
    if(!m_iClip&&m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]<=0)m_pPlayer->SetSuitUpdate("!HEV_AMO0",SUIT_SENTENCE,SUIT_REPEAT_OK);
    if(!rapid){m_pPlayer->pev->punchangle.x-=2;return;}
    std::array<int,6> random{};for(auto& value:random)value=std::rand();
    const auto punch=csol::skull1::RapidRecoil(csol::skull1::Recoil(ground,speed>0,duck),m_iShotsFired,
        {m_pPlayer->pev->punchangle.x,m_pPlayer->pev->punchangle.y},random);
    m_pPlayer->pev->punchangle.x=punch[0];m_pPlayer->pev->punchangle.y=punch[1];
}
void CSkull1::Reload()
{
    if(DefaultReload(iMaxClip(),csol::skull1::Reload,csol::skull1::ReloadTime)){
        m_pPlayer->SetAnimation(PLAYER_RELOAD);m_flAccuracy=csol::skull1::Primary.accuracyMaximum;
    }
}
void CSkull1::WeaponIdle()
{
    ResetEmptySound();m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if(m_flTimeWeaponIdle>UTIL_WeaponTimeBase()||!m_iClip)return;
    SendWeaponAnim(csol::skull1::Idle,UseDecrement());m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull1::IdleTime;
}
BOOL CSkull1::PlayEmptySound()
{
    // MP11545C10 selects by WeaponMgr flags&0x400. Original ID130 is0x413;
    // the stock engine's six-ID switch cannot classify this extended ID.
    if(m_iPlayEmptySound)EMIT_SOUND(ENT(m_pPlayer->pev),CHAN_WEAPON,"weapons/dryfire_pistol.wav",.8f,ATTN_NORM);
    return FALSE;
}
void CSOL_InitSkull1(){ADD_SERVER_COMMAND("csol_reload_skull1_damage",ReloadDamage);}
void CSOL_PrecacheSkull1()
{
    precached=false;if(!LoadDamage())return;UTIL_PrecacheOtherWeapon(csol::skull1::Classname);
    const auto* info=CSOL_ItemInfo(csol::skull1::Id);precached=info&&info->pszName&&!Q_strcmp(info->pszName,csol::skull1::Classname);
}
bool CSOL_Skull1Ready(){return precached&&damageLoaded;}
