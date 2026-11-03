// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_skull5.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "skull_ballistics.h"
#include "../skull5.hpp"
#include "../skull5_resources.hpp"
#include "../skull_balrog_damage.hpp"
#include <typeinfo>

namespace {
bool precached{}, damageLoaded{};
std::array<float,3> damage{};
bool LoadDamage()
{
    int size{};
    auto* bytes=LOAD_FILE_FOR_ME("csol/weapons/skull5_damage.json",&size);
    if(!bytes){ALERT(at_error,"CSOL: missing skull5_damage.json\n");return false;}
    std::string error;
    const bool valid=size>0 && size<=65536 && csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes),std::size_t(size)},csol::skull5::DamageFields,damage,error);
    FREE_FILE(bytes);
    if(!valid){ALERT(at_error,"CSOL: invalid skull5_damage.json: %s\n",error.c_str());return false;}
    damageLoaded=true;
    return true;
}
void ReloadDamage(){if(CMD_ARGC()==1)LoadDamage();}
int Damage()
{
    const auto mode=CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode()?
        CSOLWeaponDamageMode::Zombie:CSOLWeaponDamageMode::Normal);
    return int(damage[mode==CSOLWeaponDamageMode::Scenario?2:mode==CSOLWeaponDamageMode::Zombie?1:0]);
}
}

class CSkull5 final:public CBasePlayerWeapon
{
public:
    int iItemSlot() override{return PRIMARY_WEAPON_SLOT;}
    float GetMaxSpeed() override{return m_pPlayer && m_pPlayer->m_iFOV!=90?
        csol::skull5::ZoomSpeed:csol::skull5::MaxSpeed;}
    BOOL UseDecrement() override{return TRUE;}
    BOOL CanDeploy() override{return CSOL_WeaponsReady() && CSOL_Skull5Ready();}
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal=0) override{m_deployed=false;CBasePlayerWeapon::Holster(skiplocal);}
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void WeaponIdle() override;
    bool DeploymentComplete() const{return m_deployed;}
private:
    bool m_deployed{};
    unsigned short m_fireEvent{};
};
LINK_ENTITY_TO_CLASS(weapon_skull5,CSkull5,CCSPlayerWeapon)

bool CSOL_IsSkull5(const CBasePlayerWeapon& weapon)
{
    return typeid(weapon)==typeid(CSkull5) && weapon.m_iId==csol::skull5::Id;
}
bool CSOL_Skull5DeploymentComplete(const CBasePlayerWeapon& weapon)
{
    return CSOL_IsSkull5(weapon) && static_cast<const CSkull5&>(weapon).DeploymentComplete();
}
void CSOL_PackSkull5Weapon(const CBasePlayerWeapon* weapon,weapon_data_t* data)
{
    if(!weapon || !data || !CSOL_IsSkull5(*weapon))return;
    data->fuser1=weapon->m_flAccuracy;
    data->fuser2=csol::skull5::PackFireAge(weapon->m_flLastFire,gpGlobals->time);
    data->m_iWeaponState=weapon->m_flLastFire!=0?1:0;
}
void CSkull5::Precache()
{
    for(const char* path:csol::Skull5Resources)
    {
        const std::string_view resource(path);
        if(resource.starts_with("models/") || resource.ends_with(".spr"))
            PRECACHE_MODEL(const_cast<char*>(path));
        else if(resource.starts_with("sound/")) PRECACHE_SOUND(const_cast<char*>(path+6));
        else if(!resource.starts_with("events/")) PRECACHE_GENERIC(const_cast<char*>(path));
    }
    m_fireEvent=PRECACHE_EVENT(1,csol::skull5::Event);
}
void CSkull5::Spawn()
{
    if(!CanDeploy()){UTIL_Remove(this);return;}
    Precache();m_iId=csol::skull5::Id;SET_MODEL(edict(),csol::skull5::WorldModel);
    m_iDefaultAmmo=csol::skull5::Clip;m_flLastFire=0;
    FallInit();CBasePlayerWeapon::Spawn();
}
int CSkull5::GetItemInfo(ItemInfo* info)
{
    info->pszName=csol::skull5::Classname;info->pszAmmo1=csol::skull5::AmmoName;
    info->iMaxAmmo1=csol::skull5::Reserve;info->pszAmmo2=nullptr;info->iMaxAmmo2=-1;
    info->iMaxClip=csol::skull5::Clip;info->iSlot=0;info->iPosition=1;
    info->iId=m_iId=csol::skull5::Id;info->iFlags=0;info->iWeight=20;return 1;
}
BOOL CSkull5::Deploy()
{
    m_deployed=false;m_flAccuracy=csol::skull5::InitialAccuracy;
    if(!DefaultDeploy(const_cast<char*>(csol::skull5::ViewModel),const_cast<char*>(csol::skull5::PlayerModel),
        csol::skull5::Draw,"rifle",UseDecrement()))return FALSE;
    m_pPlayer->m_flNextAttack=UTIL_WeaponTimeBase()+csol::skull5::DeployTime;
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull5::DeployIdle;
    m_deployed=true;return TRUE;
}
void CSkull5::PrimaryAttack()
{
    const bool zoomed=m_pPlayer->m_iFOV!=90;
    const float base=csol::skull5::SpreadBase((m_pPlayer->pev->flags&FL_ONGROUND)!=0,
        m_pPlayer->pev->velocity.Length2D(),(m_pPlayer->pev->flags&FL_DUCKING)!=0);
    m_flAccuracy=csol::skull5::AccuracyAfterAttempt(m_flAccuracy,m_flLastFire,gpGlobals->time);
    m_flLastFire=gpGlobals->time;
    const float spread=csol::skull5::Spread(base,m_flAccuracy,zoomed);
    if(m_iClip<=0)
    {
        if(m_fFireOnEmpty){PlayEmptySound();m_flNextPrimaryAttack=UTIL_WeaponTimeBase()+csol::skull5::EmptyInterval;}
        if(TheBots)TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY,m_pPlayer);
        return;
    }
    --m_iClip;m_pPlayer->pev->effects|=EF_MUZZLEFLASH;m_pPlayer->SetAnimation(PLAYER_ATTACK1);
    m_pPlayer->m_iWeaponVolume=BIG_EXPLOSION_VOLUME;m_pPlayer->m_iWeaponFlash=NORMAL_GUN_FLASH;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle);
    const Vector direction=CSOL_Fire556AZ(*m_pPlayer,m_pPlayer->GetGunPosition(),gpGlobals->v_forward,
        spread,csol::skull5::Distance,csol::skull5::Penetration,Damage(),csol::skull5::RangeModifier,m_pPlayer->random_seed);
    float zero[3]{};
    // Original event passes bparam1=5. The wire field is one bit, so only
    // its low bit reaches remote clients; use the same value for prediction.
    PLAYBACK_EVENT_FULL(FEV_NOTHOST,m_pPlayer->edict(),m_fireEvent,0,zero,zero,
        direction.x,direction.y,int(m_pPlayer->pev->punchangle.x*100),
        int(m_pPlayer->pev->punchangle.y*100),TRUE,FALSE);
    m_flNextPrimaryAttack=m_flNextSecondaryAttack=UTIL_WeaponTimeBase()+
        (zoomed?csol::skull5::ZoomInterval:csol::skull5::Interval);
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull5::FireIdle;
    if(!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]<=0)
        m_pPlayer->SetSuitUpdate("!HEV_AMO0",SUIT_SENTENCE,SUIT_REPEAT_OK);
    const auto kick=csol::skull5::Recoil(zoomed);
    const float vertical=UTIL_SharedRandomFloat(m_pPlayer->random_seed+4,kick.verticalMin,kick.verticalMax);
    const float horizontal=UTIL_SharedRandomFloat(m_pPlayer->random_seed+5,kick.horizontalMin,kick.horizontalMax);
    const auto punch=csol::skull5::ApplyRecoil({m_pPlayer->pev->punchangle.x,m_pPlayer->pev->punchangle.y},vertical,horizontal);
    m_pPlayer->pev->punchangle.x=punch[0];m_pPlayer->pev->punchangle.y=punch[1];
}
void CSkull5::SecondaryAttack()
{
    m_pPlayer->m_iFOV=int(csol::skull5::NextFov(float(m_pPlayer->m_iFOV)));
    m_pPlayer->pev->fov=float(m_pPlayer->m_iFOV);m_flAccuracy=csol::skull5::InitialAccuracy;
    m_pPlayer->ResetMaxSpeed();
    if(TheBots)TheBots->OnEvent(EVENT_WEAPON_ZOOMED,m_pPlayer);
    EMIT_SOUND_DYN(m_pPlayer->edict(),CHAN_ITEM,"weapons/zoom.wav",.2f,2.4f,0,100);
    m_flNextSecondaryAttack=UTIL_WeaponTimeBase()+csol::skull5::ZoomTime;
}
void CSkull5::Reload()
{
    if(m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]<=0)return;
    if(DefaultReload(iMaxClip(),csol::skull5::Reload,csol::skull5::ReloadTime))
    {
        m_pPlayer->SetAnimation(PLAYER_RELOAD);
        if(m_pPlayer->pev->fov!=csol::skull5::NormalFov)
        {
            // Original reload chooses the fallback arm of the three-state
            // zoom function instead of cycling 80 -> 35.
            m_pPlayer->m_iFOV=15;m_pPlayer->pev->fov=15;SecondaryAttack();
        }
    }
}
void CSkull5::WeaponIdle()
{
    ResetEmptySound();m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if(m_flTimeWeaponIdle>UTIL_WeaponTimeBase() || !m_iClip)return;
    SendWeaponAnim(csol::skull5::Idle,UseDecrement());
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull5::IdleTime;
}
void CSOL_InitSkull5(){ADD_SERVER_COMMAND("csol_reload_skull5_damage",ReloadDamage);}
void CSOL_PrecacheSkull5()
{
    precached=false;if(!LoadDamage())return;UTIL_PrecacheOtherWeapon(csol::skull5::Classname);
    const auto* info=CSOL_ItemInfo(csol::skull5::Id);
    precached=info && info->pszName && !Q_strcmp(info->pszName,csol::skull5::Classname);
}
bool CSOL_Skull5Ready(){return precached && damageLoaded;}
