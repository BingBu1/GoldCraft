// SPDX-License-Identifier: GPL-3.0-or-later
// No ammo, pickup or acceptance implementation is mirrored here. All selected
// functions are extracted unchanged from the candidate (or explicit one-method override).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define EXT_FUNC
#define __API_HOOK(x) x
#define Q_min std::min
#define Q_max std::max
#define Q_strcmp std::strcmp
#define STRING(value) (value)
#define ENT(value) (value)
#define EMIT_SOUND(...) ((void)0)
#define MESSAGE_BEGIN(...) ((void)0)
#define MESSAGE_END() ((void)0)
#define WRITE_BYTE(...) ((void)0)
using BOOL=int;using WeaponIdType=int;
constexpr int TRUE=1,FALSE=0,MAX_AMMO_SLOTS=32,FL_SPECTATOR=1<<26,WEAPON_NOCLIP=-1,
    CSOL_WEAPON_CANNON=144,PRIMARY_WEAPON_SLOT=1,HITGROUP_SHIELD_ENABLED=8,
    HIDEHUD_WEAPONS=1,DEAD_NO=0,WEAPON_KNIFE=29,USE_TOGGLE=3;
int gmsgAmmoPickup=1;
struct Vars {int flags{},gamestate{},deadflag{};const char* classname{"weapon_cannon"};};
struct CBasePlayerItem;struct CBasePlayerWeapon;struct CBasePlayer;
bool CSOL_ClipAmmo(const CBasePlayerWeapon*);
int CSOL_GiveClipAmmo(CBasePlayer*,CBasePlayerWeapon*,int);
bool FClassnameIs(const Vars* v,const char* name){return !std::strcmp(v->classname,name);}
struct CBaseEntity {Vars vars{},*pev=&vars;virtual bool IsPlayer(){return false;}virtual BOOL AddPlayerItem(CBasePlayerItem*){return FALSE;}};
struct CBasePlayer:CBaseEntity {
    int m_rgAmmo[32]{},m_iHideHUD{},tabulates{};
    CBasePlayerItem* m_rgpPlayerItems[6]{};CBasePlayerItem* m_pActiveItem{};
    bool m_bHasPrimary{},m_bShieldDrawn{},m_bIsVIP{};
    bool IsPlayer()override{return true;}bool IsBot(){return false;}bool HasShield(){return false;}
    BOOL AddPlayerItem(CBasePlayerItem*)override;
    int GiveAmmo(int,const char*,int);
    static int GetAmmoIndex(const char* name){return !std::strcmp(name,"cannon")?17:2;}
    int AmmoInventory(int i){return m_rgAmmo[i];}
    CBasePlayerItem* GetItemById(int);
    bool HasPlayerItem(CBasePlayerItem*);
    void TabulateAmmo(){++tabulates;}void SwitchWeapon(CBasePlayerItem* item){m_pActiveItem=item;}
    Vars* edict(){return pev;}
};
struct CBasePlayerItem:CBaseEntity {
    CBasePlayer* m_pPlayer{};CBasePlayerItem* m_pNext{};
    int m_iId{144},m_iClip{},m_iDefaultAmmo{},m_iPrimaryAmmoType{17};
    bool killed{},attached{};int refreshes{},targetUses{},respawnChecks{};
    virtual int AddDuplicate(CBasePlayerItem*)=0;
    int iItemSlot(){return PRIMARY_WEAPON_SLOT;}bool IsWeapon(){return true;}CBasePlayerItem* GetWeaponPtr(){return this;}
    int PrimaryAmmoIndex(){return m_iPrimaryAmmoType;}int iMaxClip(){return 20;}int iMaxAmmo1(){return 20;}
    int iMaxAmmo2(){return -1;}const char* pszAmmo1(){return "cannon";}const char* pszAmmo2(){return nullptr;}
    void CheckRespawn(){++respawnChecks;}void UpdateItemInfo(){++refreshes;}void Kill(){killed=true;}
    bool AddToPlayer(CBasePlayer* p){m_pPlayer=p;return true;}void AttachToPlayer(CBasePlayer*){attached=true;}
    void SUB_UseTargets(CBaseEntity*,int,float){++targetUses;}void DefaultTouch(CBaseEntity*);
};
struct CBasePlayerWeapon:CBasePlayerItem {
    BOOL AddPrimaryAmmo(int,char*,int,int);BOOL AddSecondaryAmmo(int,char*,int){return FALSE;}
    int ExtractAmmo(CBasePlayerWeapon*);int ExtractClipAmmo(CBasePlayerWeapon*);int AddDuplicate(CBasePlayerItem*)override;
};
CBasePlayerItem* CBasePlayer::GetItemById(int id){for(auto* i:m_rgpPlayerItems)if(i&&i->m_iId==id)return i;return nullptr;}
bool CBasePlayer::HasPlayerItem(CBasePlayerItem* item){return GetItemById(item->m_iId)!=nullptr;}
struct CGameRules {
    int gotWeapons{};
    BOOL CanHaveAmmo(CBasePlayer*,const char*,int);BOOL CanHavePlayerItem(CBasePlayer*,CBasePlayerItem*);
    void PlayerGotWeapon(CBasePlayer*,CBasePlayerItem*){++gotWeapons;}
    bool FShouldSwitchWeapon(CBasePlayer*,CBasePlayerItem*){return false;}
} rules;
CGameRules* g_pGameRules=&rules;
struct BotFixture {bool IsWeaponUseable(CBasePlayerItem*){return true;}};
BotFixture* TheCSBots(){return nullptr;}
bool IsSecondaryWeapon(int){return false;}bool IsGrenadeWeapon(int){return false;}
int inventoryUpdates{};
void CSOL_UpdateWeaponInventory(CBasePlayer*){++inventoryUpdates;}
std::vector<int> pickups;
bool CSOL_SendAmmoPickup(CBasePlayer*,const char*,int n){pickups.push_back(n);return true;}
bool CSOL_ExtendedWeapon(int i){return i>=32;}
int MaxAmmoCarry(const char*){return 20;}
#include "pickup.inc"

int checks{},failures{};
void Check(bool value,const char* label){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
int main(){
    for(int initial:{0,7,19,20})for(int offered:{0,1,7,20}){
        CBasePlayer player;CBasePlayerWeapon held,ground;
        held.m_pPlayer=&player;held.m_iClip=initial;ground.m_iClip=offered;
        player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&held;player.m_pActiveItem=&held;
        pickups.clear();inventoryUpdates=0;rules.gotWeapons=0;
        // Uses actual DefaultTouch -> CanHavePlayerItem/CanHaveAmmo ->
        // AddPlayerItem -> AddDuplicate -> ExtractClipAmmo -> clip ammo service.
        ground.DefaultTouch(&player);
        const int added=std::min(20-initial,offered);
        Check(held.m_iClip==initial+added,"pickup transferred expected ammo");
        Check(ground.killed==(added>0),"ground cannon retained when zero ammo transferred");
        Check(rules.gotWeapons==(added>0?1:0),"got-weapon callback requires transfer");
        Check(pickups==(added>0?std::vector<int>{added}:std::vector<int>{}),"pickup HUD reflects actual transfer");
        Check(player.m_rgAmmo[17]==0,"cannon never creates reserve ammo");
        Check(!ground.attached&&ground.targetUses==1,"duplicate not attached and map targets retained");
    }
    for(bool wrongClass:{false,true}){
        CBasePlayer player;CBasePlayerWeapon unrelated;
        unrelated.m_pPlayer=&player;unrelated.m_iClip=3;unrelated.pev->classname="weapon_ak47";
        if(wrongClass)player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=&unrelated;
        pickups.clear();inventoryUpdates=0;
        Check(player.GiveAmmo(20,"cannon",20)==-1,"cannon ammo rejects missing or wrong-class held weapon");
        Check(player.m_rgAmmo[17]==0&&unrelated.m_iClip==3&&pickups.empty()&&inventoryUpdates==0,"invalid recipient cannot create hidden reserve");
    }
    std::printf("%s cannon pickup %d checks; %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
