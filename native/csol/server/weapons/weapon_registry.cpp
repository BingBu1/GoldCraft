// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapon_slots.h"
#include "weapon_slot_position.h"
#include "weapon_service.h"
#include "weapons.h"
#include "wpn_beamgun.h"
#include "wpn_balrog11.h"
#include "wpn_skull3.h"
#include "../../weapons/m249ex.hpp"
#include "../../weapons/skull1.hpp"
#include "../../weapons/skull2.hpp"
#include "../../weapons/skull3.hpp"
#include "../../weapons/skull5.hpp"
#include "../../weapons/balrog11.hpp"
#include "../../weapons/weapon_slots.hpp"
#include "../../weapons/k3.hpp"
#include "../../weapons/cannon.hpp"
#include "../../render_settings.hpp"
#include "../../../guest/cof/catalog.hpp"

namespace
{
ItemInfo extendedInfo[csol::WeaponIdLimit - csol::LegacyWeaponLimit]{};
WeaponSlotInfo extendedSlots[csol::WeaponIdLimit - csol::LegacyWeaponLimit]{};
int definitionMessage{}, inventoryMessage{}, currentMessage{}, pickupMessage{}, ammoPickupMessage{}, renderMessage{};
// Host adaptation: force the client Renderer gamma-space blending the original
// CSO engine uses. Read once per map when the sign-on buffer is written.
cvar_t csol_gamma_blend{"csol_gamma_blend", "1", FCVAR_SERVER};
struct ClientInventory
{
    int userid{};
    bool initialized{};
    csol::WeaponOwnership owned{};
    csol::WeaponCurrent current{};
    unsigned short generation{};
};
std::array<ClientInventory,33> clients;
bool haveExtendedDefinitions{};

void Send(int destination,int message,entvars_t* player,const std::vector<std::byte>& bytes)
{
    if(!message || bytes.empty() || bytes.size()>192)return;
    // The entvars_t overload dereferences ENT(player), including for MSG_INIT.
    // Sign-on definitions have no recipient; use the edict/no-recipient form.
    if (player) MESSAGE_BEGIN(destination,message,nullptr,player);
    else MESSAGE_BEGIN(destination,message);
    for(const auto b:bytes)WRITE_BYTE(std::to_integer<unsigned char>(b));
    MESSAGE_END();
}
void InitMessages()
{
    if(definitionMessage)return;
    definitionMessage=REG_USER_MSG(csol::WeaponDefinitionMessage,-1);
    inventoryMessage=REG_USER_MSG(csol::WeaponInventoryMessage,-1);
    currentMessage=REG_USER_MSG(csol::WeaponCurrentMessage,10);
    pickupMessage=REG_USER_MSG(csol::WeaponPickupMessage,3);
    ammoPickupMessage=REG_USER_MSG(csol::AmmoPickupMessage,6);
    renderMessage=REG_USER_MSG(csol::render::Message,csol::render::MessageBytes);
}
void WriteRenderSettings()
{
    if(!renderMessage)return;
    auto value=csol::render::EncodeGammaBlend(csol_gamma_blend.value);
    if(!value){ALERT(at_error,"[CSOL] csol_gamma_blend must be -1, 0 or 1; clients are not forced\n");value=csol::render::NoForce;}
    MESSAGE_BEGIN(MSG_INIT,renderMessage);WRITE_BYTE(*value);MESSAGE_END();
}
}
unsigned int CSOL_LegacyWeaponMask(int id){return csol::LegacyWeaponMask(id);}
void CSOL_BeforeWeaponDrop(CBasePlayerItem* item)
{
    if(auto* lifecycle=dynamic_cast<CSOLWeaponLifecycle*>(item))lifecycle->BeforeWeaponDrop();
}
void CSOL_WeaponReloadCompleted(CBasePlayerWeapon& weapon,int loaded)
{
    if(auto* lifecycle=dynamic_cast<CSOLWeaponLifecycle*>(&weapon))lifecycle->WeaponReloadCompleted(loaded);
}
bool CSOL_ExtendedWeapon(int id){return csol::ExtendedWeaponId(id);}
bool CSOL_CanUseExtendedWeapons(CBasePlayer* player)
{
    if(!player || (player->pev->flags&FL_FAKECLIENT))return false;
    const char* version=GET_KEY_VALUE(GET_INFO_BUFFER(player->edict()),"csol_protocol");
    return version && !Q_strcmp(version,"1");
}
int CSOL_PredictionSlot(int id,bool active){return csol::PredictionSlot(id,active);}
void CSOL_RememberWeaponSlotPosition(CBasePlayerItem& item)
{
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(&item);
    if (!weapon || !weapon->m_pPlayer) return;
    byte position{};
    if (CSOL_IsSkull3(*weapon))
        position = csol::skull3::Position(weapon->m_iId == csol::skull3::DualId);
    else
    {
        // Original HW records, verified in CATALOG-05 / POSITION-07. The host
        // assigns distinct HUD columns to stock guns; CSO position is one.
        // MP5's host classname differs from the original weapon_mp5.
        struct Entry { int id; const char* classname; };
        static constexpr Entry primary[]{
            {WEAPON_AUG, "weapon_aug"}, {WEAPON_SG552, "weapon_sg552"},
            {WEAPON_GALIL, "weapon_galil"}, {WEAPON_FAMAS, "weapon_famas"},
            {WEAPON_M3, "weapon_m3"}, {WEAPON_XM1014, "weapon_xm1014"},
            {WEAPON_MP5N, "weapon_mp5navy"}, {WEAPON_TMP, "weapon_tmp"},
            {WEAPON_P90, "weapon_p90"}, {WEAPON_MAC10, "weapon_mac10"},
            {WEAPON_UMP45, "weapon_ump45"}, {WEAPON_SCOUT, "weapon_scout"},
            {WEAPON_AWP, "weapon_awp"}, {WEAPON_G3SG1, "weapon_g3sg1"},
            {WEAPON_SG550, "weapon_sg550"}, {WEAPON_M249, "weapon_m249"},
            {WEAPON_M4A1, "weapon_m4a1"}, {WEAPON_AK47, "weapon_ak47"},
            {csol::m249ex::Id, csol::m249ex::Classname},
            {csol::k3::Id, csol::k3::Classname}, {csol::cannon::Id, "weapon_cannon"},
            {csol::skull5::Id, csol::skull5::Classname},
            {csol::balrog11::Id, csol::balrog11::Classname},
            // HW 37A4F2D0 -> 37A90D70 -> MP item slot/position getters.
            {csol::BeamgunId, "weapon_beamgun"}, {csol::BeamgunLEId, "weapon_beamgunle"}
        };
        for (const auto& entry : primary)
            if (weapon->m_iId == entry.id && FClassnameIs(weapon->pev, entry.classname))
            { position = 1; break; }
    }
    // Unknown weapon metadata must not overwrite a verified remembered mode.
    if (position)
        weapon->m_pPlayer->m_csolSlotPos[PRIMARY_WEAPON_SLOT - 1] =
            weapon->m_pPlayer->m_csolValidSlotPos[PRIMARY_WEAPON_SLOT - 1] = position;
}
bool CSOL_HasWeapon(CBasePlayer* player,int id)
{
    return player && csol::ValidWeaponId(id) && player->GetItemById(static_cast<WeaponIdType>(id));
}
bool CSOL_HasAnyWeapon(CBasePlayer* player,const CBasePlayerItem* excluded)
{
    if(!player)return false;
    for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
        for(auto* item=player->m_rgpPlayerItems[slot];item;item=item->m_pNext)
            if(item!=excluded && item->IsWeapon())return true;
    return false;
}
ItemInfo* CSOL_ItemInfo(int id)
{
    if(id<0 || id>=csol::WeaponIdLimit)return nullptr;
    return id<csol::LegacyWeaponLimit?&CBasePlayerItem::m_ItemInfoArray[id]:&extendedInfo[id-csol::LegacyWeaponLimit];
}
int CSOL_ExtendedWeaponHudSlot(int id)
{
    const auto* info = CSOL_ExtendedWeapon(id) ? CSOL_ItemInfo(id) : nullptr;
    return info && info->pszName ? info->iSlot : -1;
}
WeaponSlotInfo* CSOL_WeaponSlot(int id)
{
    if (!CSOL_ExtendedWeapon(id)) return nullptr;
    auto& slot = extendedSlots[id - csol::LegacyWeaponLimit];
    return slot.weaponName ? &slot : nullptr;
}
WeaponSlotInfo* CSOL_WeaponSlot(const char* name)
{
    if (!name) return nullptr;
    for (auto& slot : extendedSlots)
        if (slot.weaponName && !Q_stricmp(slot.weaponName, name)) return &slot;
    return nullptr;
}
bool CSOL_RegisterItemInfo(const ItemInfo& info)
{
    auto* entry=CSOL_ItemInfo(info.iId);
    if(!entry || !csol::ValidWeaponId(info.iId) || !info.pszName || info.iSlot<0 || info.iSlot>=MAX_ITEM_TYPES ||
        (entry->pszName && Q_strcmp(entry->pszName,info.pszName)))
    {
        ALERT(at_error,"[CSOL] Invalid or conflicting weapon registration: %d\n",info.iId);
        return false;
    }
    *entry=info;
    if(CSOL_ExtendedWeapon(info.iId)) {
        // HUD slots start at zero; the inventory reserves zero for NONE_SLOT.
        extendedSlots[info.iId-csol::LegacyWeaponLimit] = {
            static_cast<WeaponIdType>(info.iId), static_cast<InventorySlotType>(info.iSlot+1), info.pszName};
        haveExtendedDefinitions=true;
    }
    return true;
}
int CSOL_MaxAmmoCarry(const char* ammo)
{
    if(!ammo)return -1;
    for(const auto& info:extendedInfo)
    {
        if(info.pszAmmo1 && !Q_strcmp(info.pszAmmo1,ammo))return info.iMaxAmmo1;
        if(info.pszAmmo2 && !Q_strcmp(info.pszAmmo2,ammo))return info.iMaxAmmo2;
    }
    return -1;
}
void CSOL_ResetWeaponRegistry()
{
    CSOL_ResetWeaponService();
    Q_memset(extendedSlots,0,sizeof(extendedSlots));
    Q_memset(extendedInfo,0,sizeof(extendedInfo));clients={};haveExtendedDefinitions=false;InitMessages();
}
void CSOL_InitRenderSettings(){CVAR_REGISTER(&csol_gamma_blend);}
void CSOL_WriteWeaponDefinitions()
{
    // Every new connection replays the sign-on buffer, including map changes.
    WriteRenderSettings();
    if(!haveExtendedDefinitions)return;
    for(const auto& info:extendedInfo)
    {
        if(!info.iId)continue;
        csol::WeaponDefinition def{info.iId,info.iSlot,info.iPosition,
            CBasePlayer::GetAmmoIndex(info.pszAmmo1),CBasePlayer::GetAmmoIndex(info.pszAmmo2),
            info.iMaxAmmo1,info.iMaxAmmo2,info.iMaxClip,info.iFlags,info.pszName};
        const auto bytes=csol::EncodeWeaponDefinition(def);
        if(bytes.empty())ALERT(at_error,"[CSOL] Weapon definition cannot be encoded: %d\n",info.iId);
        else Send(MSG_INIT,definitionMessage,nullptr,bytes);
    }
}
void CSOL_UpdateWeaponInventory(CBasePlayer* player)
{
    if(!haveExtendedDefinitions || !CSOL_CanUseExtendedWeapons(player))return;
    CSOL_UpdateBeamGuardHUD(player);
    const int index=player->entindex();if(index<1 || index>=int(clients.size()))return;
    auto& cached=clients[index];const int userid=GETPLAYERUSERID(player->edict());
    if(cached.userid!=userid){cached={};cached.userid=userid;}
    csol::WeaponOwnership owned{};
    for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
        for(auto* item=player->m_rgpPlayerItems[slot];item;item=item->m_pNext)
            if(csol::ValidWeaponId(item->m_iId))owned[item->m_iId]=true;
    // ResetHUD/demo re-send forces a fresh complete snapshot as well.
    if(!cached.initialized || !player->m_fWeapon || owned!=cached.owned)
    {
        for(const auto& bytes:csol::EncodeWeaponInventory(owned,++cached.generation))
            Send(MSG_ONE,inventoryMessage,player->pev,bytes);
        cached.owned=owned;
    }
    csol::WeaponCurrent current;
    auto* source = player;
    if (player->pev->iuser1 == OBS_IN_EYE && player->m_hObserverTarget)
        source = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(player->m_hObserverTarget->entindex()));
    if(source && source->IsAlive() && source->m_pActiveItem)
    {
        auto* weapon=static_cast<CBasePlayerWeapon*>(source->m_pActiveItem->GetWeaponPtr());
        if(weapon && csol::ValidWeaponId(weapon->m_iId))
        {
            current.id=weapon->m_iId;current.state=1;current.clip=weapon->m_iClip;
            const auto ammo=[source](int id){return id>=0&&id<MAX_AMMO_SLOTS?source->m_rgAmmo[id]:0;};
            current.reserve=ammo(weapon->m_iPrimaryAmmoType);current.secondary=ammo(weapon->m_iSecondaryAmmoType);
            // Earned flame shots belong to the weapon, not buckbcs reserve.
            if (CSOL_Balrog11EmptyMonsterKillCallbacks(*weapon)) current.secondary=CSOL_Balrog11Charges(weapon);
        }
    }
    if(!cached.initialized || !player->m_fWeapon || current!=cached.current)
    {
        Send(MSG_ONE,currentMessage,player->pev,csol::EncodeWeaponCurrent(current));
        cached.current=current;
    }
    cached.initialized=true;
}
bool CSOL_SendWeaponPickup(CBasePlayer* player,int id)
{
    if(!CSOL_ExtendedWeapon(id))return false;
    InitMessages();
    MESSAGE_BEGIN(MSG_ONE,pickupMessage,nullptr,player->pev);
        WRITE_BYTE(csol::WeaponProtocolVersion);WRITE_SHORT(id);
    MESSAGE_END();
    return true;
}
bool CSOL_AmmoPurchase(int id,int& price,int& count)
{
    if(const auto* guest=::guest::cof::FindPistol(id)) {
        price=guest->ammunitionPrice;count=guest->ammunitionCount;return true;
    }
    if (id == csol::m249ex::Id) { price=csol::m249ex::AmmoCost;count=csol::m249ex::AmmoBuyAmount;return true; }
    if (id == csol::skull1::Id || id == csol::skull2::Id) { price=csol::skull1::AmmoCost;count=csol::skull1::AmmoBuyAmount;return true; }
    if (csol::skull3::IsWeapon(id)) { price=csol::skull3::AmmoCost;count=csol::skull3::AmmoBuyAmount;return true; }
    if (id == csol::skull5::Id) { price=csol::skull5::AmmoCost;count=csol::skull5::AmmoBuyAmount;return true; }
    if (id == csol::balrog11::Id) { price=csol::balrog11::AmmoCost;count=csol::balrog11::AmmoBuyAmount;return true; }
    // Original HW ammo record 11: 556NatoBox, cost 60, box of 30.
    if (id == csol::k3::Id) { price=60;count=30;return true; }
    if (id == 144) { price=180;count=1;return true; }
    // Original HW beamgun/beamgunle records: price 100, buy amount 20.
    if (id == csol::BeamgunId || id == csol::BeamgunLEId) { price=100;count=20;return true; }
    // Original HW ammo record "tknife": price 30, buy amount 1.
    if(id!=csol::ThrowingKnifeId)return false;
    price=30;count=1;return true;
}
bool CSOL_SendAmmoPickup(CBasePlayer* player,const char* ammo,int amount)
{
    if(!ammo || !CSOL_CanUseExtendedWeapons(player))return false;
    const char* version=GET_KEY_VALUE(GET_INFO_BUFFER(player->edict()),"csol_ammo_protocol");
    if(!version || Q_strcmp(version,"1"))return false;
    // Shared ordinary ammunition retains the stock pickup path.
    for(const auto& info:CBasePlayerItem::m_ItemInfoArray)
        if((info.pszAmmo1 && !Q_strcmp(info.pszAmmo1,ammo)) || (info.pszAmmo2 && !Q_strcmp(info.pszAmmo2,ammo)))return false;
    for(const auto& info:extendedInfo)
    {
        const bool primary=info.pszAmmo1 && !Q_strcmp(info.pszAmmo1,ammo);
        const bool secondary=info.pszAmmo2 && !Q_strcmp(info.pszAmmo2,ammo);
        if(!primary && !secondary)continue;
        InitMessages();
        Send(MSG_ONE,ammoPickupMessage,player->pev,csol::EncodeAmmoPickup({info.iId,primary?0:1,amount}));
        return true;
    }
    return false;
}
