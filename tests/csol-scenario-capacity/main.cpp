// SPDX-License-Identifier: GPL-3.0-or-later
// Actual functions from production; readiness/engine message services are fixtures.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>
#define EXT_FUNC
#define __API_HOOK(name) name
#define REGAMEDLL_ADD
#define REGAMEDLL_FIXES
#define BOOL int
#define TRUE 1
#define FALSE 0
#define Q_strcmp std::strcmp
#define Q_stricmp _stricmp
#define Q_min std::min
#define EMIT_SOUND(...) ((void)0)
#define MESSAGE_BEGIN(...) ((void)0)
#define MESSAGE_END() ((void)0)
#define WRITE_BYTE(...) ((void)0)
#define WRITE_STRING(...) ((void)0)
constexpr int MAX_NORMAL_BATTERY = 100, MAX_AMMO_SLOTS = 32, FL_SPECTATOR = 1;
constexpr int FL_FAKECLIENT = 2;
constexpr int ARMOR_NONE = 0, ARMOR_KEVLAR = 1, ARMOR_VESTHELM = 2;
constexpr int ITEM_KEVLAR = 1, ITEM_ASSAULT = 2, ITEM_TYPE_TOUCHED = 0;
constexpr int CSOL_WEAPON_CANNON = 144, RT_PLAYER_BOUGHT_SOMETHING = 0;
constexpr int at_console = 0, HUD_PRINTCENTER = 0, EVENT_PLAYER_BOUGHT_SOMETHING = 0;
using WeaponIdType = int;
namespace csol::scenario { enum class UpgradeStat { Ammo, Armor }; }
struct Vars { int flags{}; float armorvalue{}; };
struct CBasePlayerItem;
struct CBasePlayerWeapon;
int CSOL_PlayerAmmoCapacity(struct CBasePlayer*, const char*, int);
int CSOL_PlayerArmorCapacity(struct CBasePlayer*);
struct CBasePlayer
{
    Vars vars{}, *pev = &vars;
    bool ready = true, canBuy = true, restricted{}, refuseAmmo{};
    float ammoValue = 500, armorValue = 100;
    int m_rgAmmo[32]{}, m_iAccount = 10000, m_iKevlar = ARMOR_NONE;
    CBasePlayerItem* owned{};
    int GiveAmmo(int, const char*, int);
    int GetAmmoIndex(const char* name) const
    {
        return !std::strcmp(name,"TKnife") ? 14 : !std::strcmp(name,"cannon") ? 17 : 2;
    }
    int AmmoInventory(int i) const { return m_rgAmmo[i]; }
    CBasePlayerItem* GetItemById(int) { return owned; }
    void TabulateAmmo() {}
    bool CanPlayerBuy(bool) const { return canBuy; }
    bool HasRestrictItem(int, int) const { return restricted; }
    void AddAccount(int amount, int) { m_iAccount += amount; }
    void* edict() { return nullptr; }
};
int mode = 2;
bool CSOL_UsesScenarioRules() { return mode == 2; }
bool ReadStat(CBasePlayer& p, csol::scenario::UpgradeStat stat, float& value)
{
    value = stat == csol::scenario::UpgradeStat::Ammo ? p.ammoValue : p.armorValue;
    return p.ready && std::isfinite(value) && value >= 0 && value < 2147483647.0;
}
struct CGameRules { BOOL CanHaveAmmo(CBasePlayer*, const char*, int); } rules, *g_pGameRules = &rules;
struct ItemInfo { const char* pszAmmo1 = "762Nato"; int iMaxAmmo1 = 90; };
struct CBasePlayerItem
{
    CBasePlayer* m_pPlayer{};
    ItemInfo m_ItemInfoEx;
    int m_iId = 28, m_iClip{}, ammoIndex = 2;
    Vars vars{}, *pev = &vars;
    bool IsWeapon() const { return true; }
    CBasePlayerItem* GetWeaponPtr() { return this; }
    int PrimaryAmmoIndex() const { return ammoIndex; }
    int iMaxAmmo1() const;
    const char* pszAmmo1() const { return m_ItemInfoEx.pszAmmo1; }
};
struct CBasePlayerWeapon : CBasePlayerItem {};
bool CSOL_ClipAmmo(CBasePlayerWeapon* w) { return w && w->m_iId == CSOL_WEAPON_CANNON; }
int CSOL_GiveClipAmmo(CBasePlayer*, CBasePlayerWeapon* w, int amount)
{
    const int added = std::clamp(amount,0,20-w->m_iClip); w->m_iClip += added; return added;
}
int MaxAmmoCarry(const char*) { return 90; }
std::vector<int> pickups;
bool CSOL_SendAmmoPickup(CBasePlayer*, const char*, int count) { pickups.push_back(count); return true; }
bool CSOL_AmmoPurchase(int id, int& price, int& amount)
{
    if(id != 725) return false; price = 100; amount = 20; return true;
}
struct WeaponInfoStruct { int clipCost = 60, buyClipSize = 30; } info;
WeaponInfoStruct* GetWeaponInfo(int id) { return id == 28 ? &info : nullptr; }
bool g_bClientPrintEnable = true, g_bItemCreatedByBuying{};
int gmsgAmmoPickup = 1;
void ALERT(int, const char*) {}
void ClientPrint(Vars*, int, const char*) {}
void BlinkAccount(CBasePlayer*) {}
struct Tutor { void OnEvent(int, CBasePlayer*) {} } *TheTutor = nullptr;
struct CItemKevlar { BOOL MyTouch(CBasePlayer*); };
struct CItemAssaultSuit { BOOL MyTouch(CBasePlayer*); };
#include "actual_capacity.inc"

int checks{};
void Check(bool value, std::string_view label)
{
    ++checks;
    if (!value) { std::cerr << "FAIL " << checks << ": " << label << '\n'; std::exit(1); }
}
int main()
{
    // Actual table levels 1,2,10,21,30. Values are level data, not a new cap formula.
    for (int capacity : {500,505,545,660,800}) for (int id : {28,725})
    {
        mode = 2; CBasePlayer p; p.ammoValue = float(capacity);
        CBasePlayerWeapon w; w.m_pPlayer = &p; w.m_iId = id;
        if(id == 725) { w.m_ItemInfoEx = {"beamgunle",200}; }
        Check(w.iMaxAmmo1() == capacity, "weapon getter consumes player table");
        p.m_rgAmmo[2] = capacity-5; pickups.clear();
        Check(BuyGunAmmo(&p,&w,true), "purchase above native limit");
        Check(p.m_rgAmmo[2] == capacity && pickups == std::vector<int>{5}, "delivery clamped to table");
        const int after = p.m_iAccount;
        Check(!BuyGunAmmo(&p,&w,true) && p.m_iAccount == after, "full dynamic inventory not charged");
        p.m_rgAmmo[2] = 100;
        Check(p.GiveAmmo(1000,w.pszAmmo1(),90) == 2 && p.m_rgAmmo[2] == capacity, "world pickup resolves recipient capacity");
        p.m_rgAmmo[2] = capacity+100;
        Check(!rules.CanHaveAmmo(&p,w.pszAmmo1(),90), "above cap cannot receive more");
        Check(p.GiveAmmo(30,w.pszAmmo1(),90) == -1 && p.m_rgAmmo[2] == capacity+100, "query never deletes existing excess");
    }
    CBasePlayer p;
    for (auto entry : {std::pair{"at4rocket",10}, {"M32grenades",36}, {"m32grenade_venom",36},
                      {"fglauncher_rocket",40}, {"tknife",30}, {"buckshot_dfpistol",540},
                      {"50bmg_lockongunma",200}, {"50bmg_lockongunmb",300},
                      {"50bmg_lockongunmc",400}, {"50bmg_lockongunmd",500}})
        Check(CSOL_PlayerAmmoCapacity(&p,entry.first,90) == entry.second, "verified original special capacity");
    for (auto entry : {std::pair{"TKnife",29}, {"cannon",20}, {"beamgun_c",100}, {"beamgunle_c",100},
                      {"HEGrenade",1}, {"Flashbang",2}, {"SmokeGrenade",1}, {"C4",1}})
        Check(CSOL_PlayerAmmoCapacity(&p,entry.first,entry.second) == entry.second, "existing inventory adapter preserved");
    Check(CSOL_PlayerAmmoCapacity(nullptr,"762Nato",90) == 90, "ownerless item definition unchanged");
    Check(CSOL_PlayerAmmoCapacity(&p,nullptr,90) == 90, "no-ammo item unchanged");
    Check(CSOL_PlayerAmmoCapacity(&p,"762Nato",-1) == -1, "unlimited sentinel unchanged");
    p.pev->flags=FL_FAKECLIENT;
    Check(CSOL_PlayerAmmoCapacity(&p,"762Nato",90)==600, "original bot reserve fallback");
    Check(CSOL_PlayerAmmoCapacity(&p,"tknife",90)==30, "named ammunition precedes bot fallback");
    p.pev->flags=0;
    for (int selectedMode : {0,1})
    {
        mode = selectedMode; p.armorValue = 245;
        Check(CSOL_PlayerAmmoCapacity(&p,"762Nato",90) == 90, "ordinary and ZP ammo unchanged");
        Check(CSOL_PlayerArmorCapacity(&p) == 100, "ordinary and ZP armor unchanged");
    }
    mode = 2;
    for (float capacity : {100.0f,105.0f,145.0f,210.0f,300.0f})
    {
        p.armorValue = capacity; p.pev->armorvalue = 0; p.m_iKevlar = ARMOR_NONE;
        CItemKevlar vest; CItemAssaultSuit helmet;
        Check(vest.MyTouch(&p) && p.pev->armorvalue == capacity && p.m_iKevlar == ARMOR_KEVLAR, "vest fills current table armor");
        Check(!vest.MyTouch(&p), "full armor pickup rejected");
        Check(helmet.MyTouch(&p) && p.pev->armorvalue == capacity && p.m_iKevlar == ARMOR_VESTHELM, "helmet upgrade retains armor");
        Check(!helmet.MyTouch(&p), "full helmet pickup rejected");
        p.pev->armorvalue = capacity+20;
        Check(CSOL_PlayerArmorCapacity(&p) == int(capacity)+20, "higher existing armor preserved");
        p.m_iKevlar = ARMOR_KEVLAR;
        Check(helmet.MyTouch(&p) && p.pev->armorvalue == capacity+20, "buying helmet does not lower excess armor");
    }
    p.ready = false;
    Check(CSOL_PlayerAmmoCapacity(&p,"762Nato",90) == 90 && CSOL_PlayerArmorCapacity(&p) == 100, "no prepared upgrade state uses native fallback");
    std::cout << "PASS actual capacity functions: " << checks << " checks\n";
}
