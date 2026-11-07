// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "wpn_skull2.h"
#include "weapons.h"
#include "weapon_slots.h"
#include "weapon_service.h"
#include "weapon_damage_mode.h"
#include "../../weapons/skull2.hpp"
#include "../../weapons/skull2_resources.hpp"
#include "../../weapons/skull_balrog_damage.hpp"
#include <typeinfo>

#ifndef REGAMEDLL_API
#error SKULL2 requires per-instance ItemInfo for its dynamic magazine capacity
#endif

namespace {
bool precached{},damageLoaded{};
std::array<float,6> damage{};
bool LoadDamage()
{
    int size{};
    auto* bytes=LOAD_FILE_FOR_ME("csol/weapons/skull2_damage.json",&size);
    if(!bytes){ALERT(at_error,"CSOL: missing skull2_damage.json\n");return false;}
    std::string error;
    const bool valid=size>0&&size<=65536&&csol::skull_balrog::ParseDamage(
        {reinterpret_cast<const char*>(bytes),std::size_t(size)},csol::skull2::DamageFields,damage,error);
    FREE_FILE(bytes);
    if(!valid){ALERT(at_error,"CSOL: invalid skull2_damage.json: %s\n",error.c_str());return false;}
    damageLoaded=true;return true;
}
void ReloadDamage(){if(CMD_ARGC()==1)LoadDamage();}
float Damage(bool dual)
{
    const auto mode=CSOL_CurrentWeaponDamageMode(CSOL_ZombieMode()?CSOLWeaponDamageMode::Zombie:CSOLWeaponDamageMode::Normal);
    const int index=mode==CSOLWeaponDamageMode::Scenario?2:mode==CSOLWeaponDamageMode::Zombie?1:0;
    return damage[(dual?3:0)+index];
}
}

class CSkull2 final:public CBasePlayerWeapon,public CSOLWeaponLifecycle
{
public:
    int iItemSlot() override{return PISTOL_SLOT;}
    float GetMaxSpeed() override{return csol::skull2::MaxSpeed;}
    BOOL UseDecrement() override{return TRUE;}
    BOOL IsPistol() override{return TRUE;}
    BOOL CanDeploy() override{return CSOL_WeaponsReady()&&CSOL_Skull2Ready();}
    void Spawn() override;
    void Precache() override;
    int GetItemInfo(ItemInfo* info) override;
    BOOL Deploy() override;
    void Holster(int skiplocal=0) override;
    void PrimaryAttack() override;
    void SecondaryAttack() override;
    void Reload() override;
    void WeaponIdle() override;
    void ItemPostFrame() override;
    BOOL PlayEmptySound() override;
    int Save(CSave& save) override;
    int Restore(CRestore& restore) override;
    static TYPEDESCRIPTION m_SaveData[];
    void BeforeWeaponDrop() override;
    void WeaponReloadCompleted(int loaded) override;
    void Pack(weapon_data_t& data)const;
    bool DeploymentComplete()const{return m_deployed;}
    CSOLSkull2PurchaseState PurchaseState()const{return {m_modeFlags,m_iClip,m_switchDeadline,true};}
    void RestorePurchaseState(const CSOLSkull2PurchaseState& state)
    {
        m_modeFlags=state.flags;m_iClip=state.clip;m_switchDeadline=state.deadline;SyncCapacity();
        if(m_pPlayer&&m_pPlayer->m_pActiveItem==this)UpdatePlayerModel();
    }
private:
    void SyncCapacity(){CSPlayerItem()->m_ItemInfo.iMaxClip=csol::skull2::MaxClip(m_modeFlags);}
    void UpdatePlayerModel();
    // CS1.6 uses bit32 for shields. Original CSO mode bits belong to this
    // weapon instance, not the stock m_iWeaponState consumed by base code.
    int m_modeFlags{csol::skull2::Dual};
    float m_switchDeadline{};
    unsigned short m_singleEvent{},m_leftEvent{},m_rightEvent{};
    bool m_deployed{};
};
LINK_ENTITY_TO_CLASS(weapon_skull2,CSkull2,CCSPlayerWeapon)

TYPEDESCRIPTION CSkull2::m_SaveData[]={
    DEFINE_FIELD(CSkull2,m_modeFlags,FIELD_INTEGER),
    DEFINE_FIELD(CSkull2,m_switchDeadline,FIELD_TIME),
};
int CSkull2::Save(CSave& save)
{
    return CBasePlayerWeapon::Save(save)&&save.WriteFields("CSkull2",this,m_SaveData,ARRAYSIZE(m_SaveData));
}
int CSkull2::Restore(CRestore& restore)
{
    if(!CBasePlayerWeapon::Restore(restore)||!restore.ReadFields("CSkull2",this,m_SaveData,ARRAYSIZE(m_SaveData)))return 0;
    SyncCapacity();Precache();m_deployed=false;return 1;
}

bool CSOL_IsSkull2(const CBasePlayerWeapon& weapon)
{return typeid(weapon)==typeid(CSkull2)&&weapon.m_iId==csol::skull2::Id;}
bool CSOL_Skull2DeploymentComplete(const CBasePlayerWeapon& weapon)
{return CSOL_IsSkull2(weapon)&&static_cast<const CSkull2&>(weapon).DeploymentComplete();}
CSOLSkull2PurchaseState CSOL_SaveSkull2PurchaseState(const CBasePlayerWeapon& weapon)
{return CSOL_IsSkull2(weapon)?static_cast<const CSkull2&>(weapon).PurchaseState():CSOLSkull2PurchaseState{};}
void CSOL_RestoreSkull2PurchaseState(CBasePlayerWeapon& weapon,const CSOLSkull2PurchaseState& state)
{
    if(state.valid&&CSOL_IsSkull2(weapon))static_cast<CSkull2&>(weapon).RestorePurchaseState(state);
}
bool CSOL_Skull2MonsterKnockback(const CBasePlayerWeapon& weapon,std::array<float,5>& values)
{
    if(!CSOL_IsSkull2(weapon))return false;
    // MP v22C=true, v230=false; ordinary monsters use the HW record.
    values={350,250,300,100,.6f};return true;
}
void CSOL_PackSkull2Weapon(const CBasePlayerWeapon* weapon,weapon_data_t* data)
{
    if(weapon&&data&&CSOL_IsSkull2(*weapon))static_cast<const CSkull2*>(weapon)->Pack(*data);
}
void CSkull2::Pack(weapon_data_t& data)const
{
    data.m_iWeaponState=m_modeFlags|(m_flLastFire!=0?csol::skull2::HasPreviousFire:0);
    data.fuser1=m_flAccuracy;
    data.fuser2=csol::skull2::PackFireAge(m_flLastFire,gpGlobals->time);
    data.fuser3=csol::skull2::PackShots(m_iShotsFired);
    // This field is transmitted at millisecond precision by the installed
    // delta; fuser4 and iuser2-4 are not. Base reload does not use this field.
    data.m_flNextReload=csol::skull2::IsSwitching(m_modeFlags)?m_switchDeadline-gpGlobals->time:0;
}
void CSkull2::Precache()
{
    for(const char* path:csol::Skull2Resources)
    {
        const std::string_view resource(path);
        if(resource.starts_with("models/")||resource.ends_with(".spr"))PRECACHE_MODEL(const_cast<char*>(path));
        else if(resource.starts_with("sound/"))PRECACHE_SOUND(const_cast<char*>(path+6));
        else if(!resource.starts_with("events/"))PRECACHE_GENERIC(const_cast<char*>(path));
    }
    m_singleEvent=PRECACHE_EVENT(1,csol::skull2::SingleEvent);
    m_leftEvent=PRECACHE_EVENT(1,csol::skull2::LeftEvent);
    m_rightEvent=PRECACHE_EVENT(1,csol::skull2::RightEvent);
}
void CSkull2::Spawn()
{
    if(!CanDeploy()){UTIL_Remove(this);return;}
    Precache();m_iId=csol::skull2::Id;SET_MODEL(edict(),csol::skull2::WorldModel);
    // Original Reset selects dual mode; the standard new-item pickup only
    // loads the seven default rounds. Deploy must not add another magazine.
    m_modeFlags=csol::skull2::Dual;m_switchDeadline=0;m_iDefaultAmmo=csol::skull2::Clip;
    m_flLastFire=0;m_flAccuracy=csol::skull2::DualMode.accuracyMaximum;
    FallInit();CBasePlayerWeapon::Spawn();SyncCapacity();
}
int CSkull2::GetItemInfo(ItemInfo* info)
{
    info->pszName=csol::skull2::Classname;info->pszAmmo1=csol::skull2::AmmoName;
    info->iMaxAmmo1=csol::skull2::Reserve;info->pszAmmo2=nullptr;info->iMaxAmmo2=-1;
    info->iMaxClip=csol::skull2::Clip;info->iSlot=1;info->iPosition=1;
    info->iId=m_iId=csol::skull2::Id;info->iFlags=0;info->iWeight=6;return 1;
}
void CSkull2::UpdatePlayerModel()
{
    int displayed=m_modeFlags;csol::skull2::CancelSwitch(displayed);
    const bool dual=csol::skull2::IsDual(displayed);
    Q_strlcpy(m_pPlayer->m_szAnimExtention,dual?"dualpistols":"onehanded");
    m_pPlayer->pev->weaponmodel=MAKE_STRING(dual?csol::skull2::DualPlayerModel:csol::skull2::PlayerModel);
}
BOOL CSkull2::Deploy()
{
    m_deployed=false;
    csol::skull2::NormalizeForDeploy(m_modeFlags,m_iClip);SyncCapacity();
    m_flAccuracy=csol::skull2::Parameters(m_modeFlags).accuracyMaximum;
    const bool dual=csol::skull2::IsDual(m_modeFlags);
    if(!DefaultDeploy(const_cast<char*>(csol::skull2::ViewModel),
        const_cast<char*>(dual?csol::skull2::DualPlayerModel:csol::skull2::PlayerModel),
        csol::skull2::DrawSequence(m_modeFlags),const_cast<char*>(dual?"dualpistols":"onehanded"),UseDecrement()))return FALSE;
    m_pPlayer->m_flNextAttack=UTIL_WeaponTimeBase()+csol::skull2::DeployTime;
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull2::DeployIdle;
    m_deployed=true;return TRUE;
}
void CSkull2::Holster(int skiplocal)
{
    m_deployed=false;csol::skull2::CancelSwitch(m_modeFlags);SyncCapacity();
    CBasePlayerWeapon::Holster(skiplocal);
}
void CSkull2::BeforeWeaponDrop()
{
    csol::skull2::PrepareDrop(m_modeFlags,m_iClip,m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]);
    SyncCapacity();
}
void CSkull2::WeaponReloadCompleted(int)
{
    // Original v1DC runs after the rule's completed-reload callback and
    // derives the alternating side from the FINAL clip, including A mode.
    if((m_iClip&1)==0)m_modeFlags|=csol::skull2::Left;
    else m_modeFlags&=~csol::skull2::Left;
}
void CSkull2::SecondaryAttack()
{
    const int animation=csol::skull2::BeginSwitch(m_modeFlags,gpGlobals->time,m_switchDeadline);
    if(animation<0)return;
    SyncCapacity();
    m_flNextPrimaryAttack=m_flNextSecondaryAttack=m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull2::SwitchTime;
    SendWeaponAnim(animation,UseDecrement());
}
void CSkull2::ItemPostFrame()
{
    auto* const owner=m_pPlayer;
    if(!owner)return;
    const EHANDLE weaponHandle(this),ownerHandle(owner);
    const int life=owner->m_iNumSpawns;
    CBasePlayerWeapon::ItemPostFrame();
    // Reload rules can remove the weapon or change its owner/life. Resolve
    // both handles before touching this entity after the base callbacks.
    if(weaponHandle.Get<CBaseEntity>()!=this || ownerHandle.Get<CBasePlayer>()!=owner)return;
    if(m_pPlayer!=owner || owner->m_pActiveItem!=this || owner->m_iNumSpawns!=life ||
        owner->has_disconnected || (pev->flags&FL_KILLME) ||
        (owner->pev->flags&(FL_KILLME|FL_DORMANT)) || !owner->IsAlive())return;
    if(csol::skull2::CompleteSwitch(m_modeFlags,m_iClip,owner->m_rgAmmo[m_iPrimaryAmmoType],
        gpGlobals->time,m_switchDeadline))
    {
        SyncCapacity();UpdatePlayerModel();
    }
}
void CSkull2::PrimaryAttack()
{
    const bool dual=csol::skull2::IsDual(m_modeFlags);
    const auto& mode=csol::skull2::Parameters(m_modeFlags);
    m_flAccuracy=mode.accuracyMaximum;
    const float spread=csol::skull2::Spread(m_modeFlags,(m_pPlayer->pev->flags&FL_ONGROUND)!=0,
        m_pPlayer->pev->velocity.Length2D(),(m_pPlayer->pev->flags&FL_DUCKING)!=0);
    if(!csol::skull2::TriggerAllowed(m_modeFlags,m_iShotsFired))return;
    const float fireAge=gpGlobals->time-m_flLastFire;
    m_flAccuracy=csol::skull2::AccuracyAfterAttempt(m_modeFlags,m_flLastFire,gpGlobals->time);
    m_flLastFire=gpGlobals->time;
    if(m_iClip<=0)
    {
        if(m_fFireOnEmpty){PlayEmptySound();m_flNextPrimaryAttack=UTIL_WeaponTimeBase()+.2f;}
        if(TheBots)TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY,m_pPlayer);
        return;
    }
    if(dual)m_flNextPrimaryAttack=m_flNextSecondaryAttack=UTIL_WeaponTimeBase()+mode.interval;
    --m_iClip;m_pPlayer->pev->effects|=EF_MUZZLEFLASH;
    m_pPlayer->m_iWeaponVolume=BIG_EXPLOSION_VOLUME;
    m_pPlayer->m_iWeaponFlash=dual?DIM_GUN_FLASH:BRIGHT_GUN_FLASH;
    UTIL_MakeVectors(m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle);
    Vector source=m_pPlayer->GetGunPosition();
    const bool left=(m_modeFlags&csol::skull2::Left)!=0;
    if(dual)
    {
        source+=gpGlobals->v_right*(left?-5.f:5.f);
        m_modeFlags^=csol::skull2::Left;
    }
    m_pPlayer->SetAnimation(dual&&!left?PLAYER_ATTACK2:PLAYER_ATTACK1);
    // Original A uses50AZ(type27), whose verified trace contract matches
    // the host50AE. B explicitly uses9mm(type1), with its own damage.
    const Vector direction=m_pPlayer->FireBullets3(source,gpGlobals->v_forward,spread,
        csol::skull2::Distance,csol::skull2::Penetration,dual?BULLET_PLAYER_9MM:BULLET_PLAYER_50AE,
        int(Damage(dual)),csol::skull2::RangeModifier,m_pPlayer->pev,true,m_pPlayer->random_seed);
    float zero[3]{};
    if(dual)
        PLAYBACK_EVENT_FULL(FEV_NOTHOST,m_pPlayer->edict(),left?m_leftEvent:m_rightEvent,0,zero,zero,
            fireAge,direction.x,int(direction.y*100),m_iClip,FALSE,FALSE);
    else
        PLAYBACK_EVENT_FULL(FEV_NOTHOST,m_pPlayer->edict(),m_singleEvent,0,zero,zero,
            direction.x,direction.y,int(m_pPlayer->pev->punchangle.x*100),
            int(m_pPlayer->pev->punchangle.y*100),m_iClip!=0,FALSE);
    if(!dual)m_flNextPrimaryAttack=m_flNextSecondaryAttack=UTIL_WeaponTimeBase()+mode.interval;
    m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+mode.fireIdle;
    m_pPlayer->pev->punchangle.x-=2;
}
void CSkull2::Reload()
{
    const auto& mode=csol::skull2::ReloadParameters(m_modeFlags);
    if(DefaultReload(iMaxClip(),csol::skull2::ReloadSequence(m_modeFlags),mode.reloadTime))
    {
        m_pPlayer->SetAnimation(PLAYER_RELOAD);m_flAccuracy=mode.accuracyMaximum;
    }
}
void CSkull2::WeaponIdle()
{
    ResetEmptySound();m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
    if(m_flTimeWeaponIdle>UTIL_WeaponTimeBase()||!m_iClip)return;
    const int animation=csol::skull2::IdleSequence(m_modeFlags,m_iClip);
    if(animation<0)return;
    SendWeaponAnim(animation,UseDecrement());m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+csol::skull2::IdleTime;
}
BOOL CSkull2::PlayEmptySound()
{
    if(m_iPlayEmptySound)EMIT_SOUND(ENT(m_pPlayer->pev),CHAN_WEAPON,"weapons/dryfire_pistol.wav",.8f,ATTN_NORM);
    return FALSE;
}
void CSOL_InitSkull2(){ADD_SERVER_COMMAND("csol_reload_skull2_damage",ReloadDamage);}
void CSOL_PrecacheSkull2()
{
    precached=false;if(!LoadDamage())return;
    UTIL_PrecacheOtherWeapon(csol::skull2::Classname);
    const auto* info=CSOL_ItemInfo(csol::skull2::Id);
    precached=info&&info->pszName&&!Q_strcmp(info->pszName,csol::skull2::Classname);
}
bool CSOL_Skull2Ready(){return precached&&damageLoaded;}
