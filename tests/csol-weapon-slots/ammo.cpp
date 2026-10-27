// SPDX-License-Identifier: GPL-3.0-or-later
// Engine boundaries are fixtures; included functions are extracted unchanged
// from the actual ReGameDLL client/player/weapons sources by extract_ammo.py.
#include <algorithm>
#include <cstring>
#include <vector>
#include <iostream>
#include <cstdlib>
#include "../../native/csol/cannon.hpp"
#define EXT_FUNC
#define __API_HOOK(x) x
#define REGAMEDLL_FIXES
#define Q_min std::min
#define Q_strcmp std::strcmp
#define BOOL int
#define TRUE 1
#define FALSE 0
#define EMIT_SOUND(...) ((void)0)
#define MESSAGE_BEGIN(...) ((void)0)
#define MESSAGE_END() ((void)0)
#define WRITE_BYTE(...) ((void)0)
constexpr int MAX_AMMO_SLOTS=32,FL_SPECTATOR=1,WEAPON_NOCLIP=-1;
constexpr int RT_PLAYER_BOUGHT_SOMETHING=0,at_console=0,HUD_PRINTCENTER=0;
constexpr int CSOL_WEAPON_CANNON=144;
using WeaponIdType=int;
int gmsgAmmoPickup=1;bool g_bClientPrintEnable=true;
struct Vars{int flags{};const char* classname="weapon_tknifeex2";};
struct CBasePlayerItem;
struct CBasePlayerWeapon;
bool CSOL_ClipAmmo(const CBasePlayerWeapon*);
int CSOL_GiveClipAmmo(struct CBasePlayer*,CBasePlayerWeapon*,int);
bool FClassnameIs(const Vars* v,const char* name){return !std::strcmp(v->classname,name);}
struct CBasePlayer
{
    Vars vars{},*pev=&vars;int m_rgAmmo[32]{},m_iAccount=300;
    CBasePlayerItem* owned{};
    bool canBuy=true;int GiveAmmo(int,const char*,int);
    static int GetAmmoIndex(const char* name){return !std::strcmp(name,"TKnife")?14:!std::strcmp(name,"cannon")?17:2;}
    CBasePlayerItem* GetItemById(int);
    bool CanPlayerBuy(bool){return canBuy;}bool HasPlayerItem(CBasePlayerItem*){return false;}
    void TabulateAmmo(){};void AddAccount(int amount,int){m_iAccount+=amount;}
};
struct CBasePlayerItem
{
    CBasePlayer* m_pPlayer{};int m_iId=220,m_iClip{},m_iPrimaryAmmoType=14,m_iDefaultAmmo{};
    Vars vars{},*pev=&vars;
    int PrimaryAmmoIndex(){return m_iPrimaryAmmoType;}
    int iMaxAmmo1(){return m_iId==144?20:m_iId==220?29:90;}
    int iMaxClip(){return m_iId==144?20:m_iId==220?1:30;}
    int iMaxAmmo2(){return -1;}const char* pszAmmo2(){return nullptr;}
    const char* pszAmmo1(){return m_iId==144?"cannon":m_iId==220?"TKnife":"556Nato";}
    bool IsWeapon(){return true;}CBasePlayerItem* GetWeaponPtr(){return this;}
};
struct CBasePlayerWeapon:CBasePlayerItem{
    BOOL AddPrimaryAmmo(int,char*,int,int);
    int ExtractAmmo(CBasePlayerWeapon*);int ExtractClipAmmo(CBasePlayerWeapon*);int AddDuplicate(CBasePlayerItem*);
    BOOL AddSecondaryAmmo(int,char*,int){return 0;}
};
CBasePlayerItem* CBasePlayer::GetItemById(int id){return owned&&owned->m_iId==id?owned:nullptr;}
bool IsGrenadeWeapon(int){return false;}
int inventoryUpdates{};
void CSOL_UpdateWeaponInventory(CBasePlayer*){++inventoryUpdates;}
struct Rules{bool allowed=true;bool CanHaveAmmo(CBasePlayer* p,const char* name,int cap){return allowed&&p->m_rgAmmo[p->GetAmmoIndex(name)]<cap;}} rules;
Rules* g_pGameRules=&rules;
struct WeaponInfoStruct{int clipCost=60,buyClipSize=30;const char* ammoName1="ammo_556nato";};
WeaponInfoStruct* GetWeaponInfo(int id){static WeaponInfoStruct w;return id==28?&w:nullptr;}
bool CSOL_AmmoPurchase(int id,int& price,int& count){if(id!=220&&id!=144)return false;price=id==144?180:30;count=1;return true;}
bool CSOL_ExtendedWeapon(int id){return id>=32;}
std::vector<int> pickups;
bool CSOL_SendAmmoPickup(CBasePlayer*,const char*,int amount){pickups.push_back(amount);return true;}
int MaxAmmoCarry(const char*){return 29;}
void ALERT(int,const char*){};void ClientPrint(Vars*,int,const char*){};void BlinkAccount(CBasePlayer*){};
#include "actual_ammo.inc"
int checks{};
void Check(bool b){++checks;if(!b){std::cerr<<"FAIL ammo "<<checks<<'\n';std::exit(1);}}
int main()
{
    CBasePlayer p;CBasePlayerWeapon w;w.m_pPlayer=&p;
    char ammo[]="TKnife";
    Check(w.AddPrimaryAmmo(30,ammo,1,29));
    Check(w.m_iClip==1&&p.m_rgAmmo[14]==29&&pickups==std::vector<int>({29,1}));
    pickups.clear();Check(!BuyGunAmmo(&p,&w,true));Check(p.m_iAccount==300&&pickups.empty());
    p.m_rgAmmo[14]=28;Check(BuyGunAmmo(&p,&w,true));Check(p.m_rgAmmo[14]==29&&p.m_iAccount==270&&pickups==std::vector<int>({1}));
    pickups.clear();p.m_rgAmmo[14]=20;p.m_iAccount=29;Check(!BuyGunAmmo(&p,&w,true));Check(p.m_rgAmmo[14]==20&&p.m_iAccount==29&&pickups.empty());
    p.m_iAccount=300;p.canBuy=false;Check(!BuyGunAmmo(&p,&w,true));Check(p.m_iAccount==300&&pickups.empty());p.canBuy=true;
    rules.allowed=false;Check(!BuyGunAmmo(&p,&w,true));Check(p.m_iAccount==300&&pickups.empty());rules.allowed=true;
    p.vars.flags=FL_SPECTATOR;Check(!BuyGunAmmo(&p,&w,true));Check(p.m_iAccount==300&&pickups.empty());p.vars.flags=0;
    w.m_iClip=0;p.m_rgAmmo[14]=29;Check(!BuyGunAmmo(&p,&w,true));Check(p.m_iAccount==300); // Pending reload cannot create a 31st round.
    w.m_iClip=1;p.m_rgAmmo[14]=27;Check(w.AddPrimaryAmmo(30,ammo,1,29));Check(pickups==std::vector<int>({2})&&p.m_rgAmmo[14]+w.m_iClip==30);
    pickups.clear();w.m_iId=28;w.m_iPrimaryAmmoType=2;p.m_rgAmmo[2]=80;Check(BuyGunAmmo(&p,&w,true));Check(p.m_rgAmmo[2]==90&&p.m_iAccount==240);
    w.m_iPrimaryAmmoType=32;Check(!BuyGunAmmo(&p,&w,true));
    CBasePlayer cannonPlayer;CBasePlayerWeapon cannon;cannon.m_pPlayer=&cannonPlayer;
    cannonPlayer.owned=&cannon;cannon.m_iId=144;cannon.vars.classname="weapon_cannon";cannon.m_iDefaultAmmo=20;
    pickups.clear();inventoryUpdates=0;
    Check(cannon.ExtractAmmo(&cannon));
    Check(cannon.m_iClip==20&&cannon.m_iDefaultAmmo==0&&cannon.m_iPrimaryAmmoType==17);
    Check(cannonPlayer.m_rgAmmo[17]==0&&pickups==std::vector<int>({20})&&inventoryUpdates==1);
    pickups.clear();Check(!BuyGunAmmo(&cannonPlayer,&cannon,true));
    Check(cannonPlayer.m_iAccount==300&&pickups.empty());
    for(int initial:{0,1,18,19,20})for(int amount:{-1,0,1,2,20,25}){
        cannon.m_iClip=initial;pickups.clear();inventoryUpdates=0;
        const int expected=std::clamp(amount,0,20-initial);
        Check(cannonPlayer.GiveAmmo(amount,"cannon",20)==(expected?17:-1));
        Check(cannon.m_iClip==initial+expected&&cannonPlayer.m_rgAmmo[17]==0);
        Check(pickups==(expected?std::vector<int>{expected}:std::vector<int>{}));
        Check(inventoryUpdates==(expected?1:0));
    }
    for(int initial:{0,19,20})for(int money:{179,180,1000}){
        cannon.m_iClip=initial;cannonPlayer.m_iAccount=money;pickups.clear();
        const bool can=initial<20&&money>=180;
        Check(BuyGunAmmo(&cannonPlayer,&cannon,true)==can);
        Check(cannon.m_iClip==initial+int(can)&&cannonPlayer.m_iAccount==money-(can?180:0));
        Check(cannonPlayer.m_rgAmmo[17]==0&&pickups==(can?std::vector<int>{1}:std::vector<int>{}));
    }
    cannon.m_iClip=5;cannonPlayer.m_iAccount=1000;
    cannonPlayer.canBuy=false;Check(!BuyGunAmmo(&cannonPlayer,&cannon,true));cannonPlayer.canBuy=true;
    rules.allowed=false;Check(!BuyGunAmmo(&cannonPlayer,&cannon,true));rules.allowed=true;
    cannonPlayer.vars.flags=FL_SPECTATOR;Check(!BuyGunAmmo(&cannonPlayer,&cannon,true));cannonPlayer.vars.flags=0;
    Check(cannon.m_iClip==5&&cannonPlayer.m_iAccount==1000);
    CBasePlayerWeapon dropped;dropped.m_iId=144;dropped.vars.classname="weapon_cannon";dropped.m_iClip=7;
    pickups.clear();Check(dropped.ExtractClipAmmo(&cannon)>0);
    Check(cannon.m_iClip==12&&cannonPlayer.m_rgAmmo[17]==0&&pickups==std::vector<int>{7});
    // A freshly granted duplicate supplies its default charge directly to the held cannon.
    CBasePlayerWeapon fresh;fresh.m_iId=144;fresh.vars.classname="weapon_cannon";fresh.m_iDefaultAmmo=20;
    pickups.clear();Check(fresh.AddDuplicate(&cannon));Check(cannon.m_iClip==20&&fresh.m_iDefaultAmmo==0&&pickups==std::vector<int>{8});
    cannon.m_iClip=7;cannon.m_iDefaultAmmo=0;pickups.clear();
    Check(!cannon.ExtractAmmo(&cannon));Check(cannon.m_iClip==7&&pickups.empty());
    cannon.vars.classname="weapon_ak47";Check(!CSOL_ClipAmmo(&cannon));
    std::cout<<"PASS actual ammo "<<checks<<" checks\n";
}
