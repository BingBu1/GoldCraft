// SPDX-License-Identifier: GPL-3.0-or-later
// Engine services and entity graph fixture for the production registry adapter.
#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
constexpr int MAX_WEAPONS=32,MAX_ITEM_TYPES=6,MAX_AMMO_SLOTS=32,FL_FAKECLIENT=8192;
constexpr int MSG_ONE=1,MSG_INIT=3,at_error=0,OBS_IN_EYE=4;
using WeaponIdType=int;
enum InventorySlotType { NONE_SLOT, PRIMARY_WEAPON_SLOT, PISTOL_SLOT, KNIFE_SLOT, GRENADE_SLOT, C4_SLOT };
struct WeaponSlotInfo {WeaponIdType id;InventorySlotType slot;const char* weaponName;};
struct entvars_t{int flags{},iuser1{};};
struct edict_t{int userid=1;std::string version="1";};
struct ItemInfo{int iSlot{},iPosition{};const char* pszAmmo1{};int iMaxAmmo1{};const char* pszAmmo2{};int iMaxAmmo2{};const char* pszName{};int iMaxClip{},iId{},iFlags{},iWeight{};};
struct CBasePlayerItem
{
    static ItemInfo m_ItemInfoArray[32];
    int m_iId{};CBasePlayerItem* m_pNext{};
    virtual ~CBasePlayerItem()=default;
    virtual bool IsWeapon(){return true;}
    virtual CBasePlayerItem* GetWeaponPtr(){return this;}
};
struct CBasePlayerWeapon:CBasePlayerItem{int m_iClip=80,m_iPrimaryAmmoType=15,m_iSecondaryAmmoType=16;};
struct CBasePlayer
{
    CBasePlayer* m_hObserverTarget{};
    entvars_t vars{};entvars_t* pev=&vars;edict_t entity{};int index=1;bool alive=true,m_fWeapon{};
    CBasePlayerItem* m_rgpPlayerItems[6]{};CBasePlayerItem* m_pActiveItem{};int m_rgAmmo[32]{};
    int entindex(){return index;}edict_t* edict(){return &entity;}bool IsAlive(){return alive;}
    static int GetAmmoIndex(const char* s){return !s?-1:!std::strcmp(s,"Beam")?15:16;}
    CBasePlayerItem* GetItemById(int id){for(auto* head:m_rgpPlayerItems)for(auto* p=head;p;p=p->m_pNext)if(p->m_iId==id)return p;return nullptr;}
};
extern CBasePlayer* observedPlayer;
inline CBasePlayer* UTIL_PlayerByIndex(int){return observedPlayer;}
inline int Q_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
inline int Q_stricmp(const char* a,const char* b){return _stricmp(a,b);}
#define Q_memset std::memset
inline char* GET_INFO_BUFFER(edict_t* e){return reinterpret_cast<char*>(e);}
inline const char* GET_KEY_VALUE(char* p,const char*){return reinterpret_cast<edict_t*>(p)->version.c_str();}
inline int GETPLAYERUSERID(edict_t* e){return e->userid;}
void ALERT(int,const char*,...);
int REG_USER_MSG(const char*,int);
void MESSAGE_BEGIN(int,int,const void*,entvars_t*);
void MESSAGE_BEGIN(int,int);
void WRITE_BYTE(int);void WRITE_SHORT(int);void MESSAGE_END();
