// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit resource admission, registry, label publication and empty pool
// teardown boundaries. No private-member macros or replacement SDK layouts.
bool CCSOLScenarioPlayerRuntime::Active()const{return m_active&&bomb_test::active;}
bool CCSOLScenarioPlayerRuntime::Activate(){m_active=true;return true;}
bool CCSOLScenarioPlayerRuntime::SetProfile(CBasePlayer& player,CSOLScenarioPlayerProfile profile)
{
    auto state=Resolve(EHANDLE(&player));
    if(!state){state=std::make_shared<CSOLScenarioPlayerRuntimeState>();state->player=EHANDLE(&player);
        state->generation=++bomb_test::generation;m_players[1]=state;}
    state->profile=std::move(profile);++state->profileRevision;return true;
}
bool CCSOLScenarioPlayerRuntime::PublishLabels(const std::shared_ptr<CSOLScenarioPlayerRuntimeState>&,float){return true;}
bool CCSOLScenarioPlayerRuntime::RemoveProtectionAt(CSOLScenarioPlayerRuntimeState&,CBasePlayer&,std::size_t)
{throw std::runtime_error("unrelated stat buff expiration");}
bool CCSOLScenarioSession::Precache(std::string_view,std::uint16_t,CSOLScenarioResourceScope)
{
    m_weapons=std::make_shared<CCSOLScenarioWeaponState>();csol::scenario::ResourceError error;
    return m_weapons->Load({},error)&&m_equipmentResources.Load({},error);
}
CCSOLScenarioSession::~CCSOLScenarioSession()=default;
CCSOLMonsterPool::~CCSOLMonsterPool()=default;
bool CCSOLScenarioWeaponState::Load(csol::scenario::ResourceFileApi,csol::scenario::ResourceError&){m_loaded=true;return true;}
bool csol::scenario::EquipmentResources::Load(csol::scenario::ResourceFileApi,csol::scenario::ResourceError&)
{m_tables=std::shared_ptr<const EquipmentTables>(&bomb_test::tables,[](const EquipmentTables*){});return true;}

#define BOMB_UNUSED(ret,owner,name,args) ret owner::name args {throw std::runtime_error("unexpected " #owner "::" #name);}
BOMB_UNUSED(int,CBasePlayerItem,Save,(CSave&))
BOMB_UNUSED(int,CBasePlayerItem,Restore,(CRestore&))
BOMB_UNUSED(void,CBasePlayerItem,SetObjectCollisionBox,())
BOMB_UNUSED(CBaseEntity*,CBasePlayerItem,Respawn,())
BOMB_UNUSED(int,CBasePlayerItem,AddToPlayer,(CBasePlayer*))
BOMB_UNUSED(void,CBasePlayerItem,Holster,(int))
BOMB_UNUSED(void,CBasePlayerItem,Drop,())
BOMB_UNUSED(void,CBasePlayerItem,Kill,())
BOMB_UNUSED(void,CBasePlayerItem,AttachToPlayer,(CBasePlayer*))
BOMB_UNUSED(void,CBasePlayerWeapon,Spawn,())
BOMB_UNUSED(int,CBasePlayerWeapon,Save,(CSave&))
BOMB_UNUSED(int,CBasePlayerWeapon,Restore,(CRestore&))
BOMB_UNUSED(int,CBasePlayerWeapon,AddToPlayer,(CBasePlayer*))
BOMB_UNUSED(int,CBasePlayerWeapon,AddDuplicate,(CBasePlayerItem*))
BOMB_UNUSED(BOOL,CBasePlayerWeapon,CanDeploy,())
BOMB_UNUSED(void,CBasePlayerWeapon,Holster,(int))
BOMB_UNUSED(void,CBasePlayerWeapon,ItemPostFrame,())
BOMB_UNUSED(int,CBasePlayerWeapon,PrimaryAmmoIndex,())
BOMB_UNUSED(int,CBasePlayerWeapon,SecondaryAmmoIndex,())
BOMB_UNUSED(int,CBasePlayerWeapon,UpdateClientData,(CBasePlayer*))
BOMB_UNUSED(int,CBasePlayerWeapon,ExtractAmmo,(CBasePlayerWeapon*))
BOMB_UNUSED(int,CBasePlayerWeapon,ExtractClipAmmo,(CBasePlayerWeapon*))
BOMB_UNUSED(BOOL,CBasePlayerWeapon,PlayEmptySound,())
BOMB_UNUSED(void,CBasePlayerWeapon,ResetEmptySound,())
BOMB_UNUSED(void,CBasePlayerWeapon,SendWeaponAnim,(int,int))
BOMB_UNUSED(BOOL,CBasePlayerWeapon,IsUseable,())
BOMB_UNUSED(void,CBasePlayerWeapon,RetireWeapon,())
#undef BOMB_UNUSED
