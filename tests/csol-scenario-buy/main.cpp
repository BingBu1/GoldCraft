// SPDX-License-Identifier: GPL-3.0-or-later
// Actual production functions are included unchanged. Engine, rule-phase and
// inventory services below are fixtures; this is not a live-server test.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#define BIT(n) (1 << (n))
#include "../../external/ReGameDLL_CS/regamedll/dlls/unisignals.h"
#define EXT_FUNC
#define __API_HOOK(name) name
#define REGAMEDLL_FIXES
#define EMIT_SOUND(...) ((void)0)

constexpr int DEAD_NO = 0, CT = 2, TERRORIST = 1;
constexpr int FL_SPECTATOR = 1, FL_DORMANT = 2, FL_KILLME = 4;
constexpr int HUD_PRINTCENTER = 0, MAX_AMMO_SLOTS = 32;
constexpr int RT_PLAYER_BOUGHT_SOMETHING = 0, at_console = 0;
#ifdef REGAMEDLL_FIXES
constexpr int MIN_BUY_TIME = 0;
#else
constexpr int MIN_BUY_TIME = 15;
#endif
struct Cvar { float value{}; } buytime{1.5f}, buy_anywhere{}, plant_c4_anywhere{};
struct Globals { float time{}; } globals, *gpGlobals = &globals;
struct Vars { int deadflag{}, flags{}; float health = 100; };
struct PlayerApi { bool m_bPlantC4Anywhere{}; };
struct CBasePlayer;
struct Rules
{
    virtual ~Rules() = default;
    bool multiplayer = true, m_bCTCantBuy{}, m_bTCantBuy{};
    bool m_bMapHasBuyZone = true, m_bMapHasBombZone = true, m_bMapHasRescueZone = true;
    float m_fRoundStartTime{};
    bool IsMultiplayer() const { return multiplayer; }
    bool CanPlayerBuy(CBasePlayer*);
} rules, *g_pGameRules = &rules;
Rules* CSGameRules() { return &rules; }
int mode{}, cvarWrites{}, prints{}, blinks{}, zoneChecks{}, buyIconSets{}, buyIconClears{};
bool scenarioReady = true, rejected{}, trainingAllowed = true, insideSpawn{};
bool scenarioScopeCurrent = true;
bool CSOL_UsesScenarioRules() { return mode == 2; }
bool CSOL_RulesRejected() { return rejected; }
struct CCSOLScenarioRules : Rules
{
    // Runtime readiness is a fixture; phase and transaction gates are extracted.
    bool Ready() const { return scenarioReady && !rejected; }
#include "actual_phase.inc"
    Phase m_phase = Phase::Playing;
    bool m_restarting{}, m_completing{}, m_applyingFinish{};
} scenarioRules;
struct CSOLScenarioSdkScope
{
    bool Scenario() const { return mode == 2; }
    bool Current() const { return scenarioScopeCurrent; }
};
void CVAR_SET_FLOAT(const char*, float value) { buytime.value = value; ++cvarWrites; }
const char* UTIL_dtos1(int) { return "seconds"; }
void ClientPrint(Vars*, int, const char*, const char* = nullptr) { ++prints; }
void BlinkAccount(CBasePlayer*) { ++blinks; }
void ALERT(int, const char*) {}
bool g_bClientPrintEnable = true;
struct CBasePlayer
{
    Vars vars{}, *pev = &vars;
    PlayerApi api{};
    CUnifiedSignals m_signals;
    int m_iTeam = CT, m_iAccount = 300, m_rgAmmo[32]{};
    bool m_bIsVIP{}, m_bHasC4{}, has_disconnected{}, refuseAmmo{};
    bool IsAlive() const { return pev->deadflag == DEAD_NO && pev->health > 0; }
    PlayerApi* CSPlayer() { return &api; }
    bool CanPlayerBuy(bool);
    void HandleSignals();
    int GiveAmmo(int amount, const char*, int maximum)
    {
        if (refuseAmmo) return -1;
        m_rgAmmo[2] = std::min(maximum, m_rgAmmo[2] + amount);
        return 2;
    }
    void AddAccount(int amount, int) { m_iAccount += amount; }
};
bool Rules::CanPlayerBuy(CBasePlayer* p)
{
    return !((m_bCTCantBuy && p->m_iTeam == CT) || (m_bTCantBuy && p->m_iTeam == TERRORIST));
}
struct CHalfLifeTraining { static bool PlayerCanBuy(CBasePlayer*) { return trainingAllowed; } };
void OLD_CheckBuyZone(CBasePlayer* p) { ++zoneChecks; if (insideSpawn) p->m_signals.Signal(SIGNAL_BUY); }
void OLD_CheckBombTarget(CBasePlayer*) {}
void OLD_CheckRescueZone(CBasePlayer*) {}
void BuyZoneIcon_Set(CBasePlayer*) { ++buyIconSets; }
void BuyZoneIcon_Clear(CBasePlayer*) { ++buyIconClears; }
void BombTargetFlash_Set(CBasePlayer*) {}
void BombTargetFlash_Clear(CBasePlayer*) {}
void RescueZoneIcon_Set(CBasePlayer*) {}
void RescueZoneIcon_Clear(CBasePlayer*) {}
void EscapeZoneIcon_Set(CBasePlayer*) {}
void EscapeZoneIcon_Clear(CBasePlayer*) {}
void VIP_SafetyZoneIcon_Set(CBasePlayer*) {}
void VIP_SafetyZoneIcon_Clear(CBasePlayer*) {}
struct CBasePlayerItem
{
    int m_iId = 28, m_iClip{}, ammoIndex = 2;
    Vars vars{}, *pev = &vars;
    int PrimaryAmmoIndex() const { return ammoIndex; }
    int iMaxAmmo1() const { return 90; }
    const char* pszAmmo1() const { return "556Nato"; }
    CBasePlayerItem* GetWeaponPtr() { return this; }
};
struct CBasePlayerWeapon : CBasePlayerItem {};
bool CSOL_ClipAmmo(CBasePlayerWeapon*) { return false; }
bool CSOL_AmmoPurchase(int id, int& cost, int& size)
{
    if (id != 725) return false;
    cost = 70; size = 10; return true;
}
struct WeaponInfoStruct { int clipCost = 60, buyClipSize = 30; } info;
WeaponInfoStruct* GetWeaponInfo(int id) { return id == 28 ? &info : nullptr; }
#include "actual_buy.inc"

int checks{};
void Check(bool condition, std::string_view description)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL " << checks << ": " << description << '\n'; std::exit(1); }
}
void Reset(int selectedMode)
{
    rules = {}; scenarioRules = {}; globals = {}; buytime.value = 1.5f;
    buy_anywhere.value = plant_c4_anywhere.value = 0;
    mode = selectedMode; scenarioReady = scenarioScopeCurrent = trainingAllowed = true;
    g_pGameRules = mode == 2 ? &scenarioRules : &rules;
    rejected = insideSpawn = false;
    cvarWrites = prints = blinks = zoneChecks = buyIconSets = buyIconClears = 0;
}
void BuySignal(CBasePlayer& p)
{
    p.m_signals.Signal(SIGNAL_BUY); p.m_signals.Update();
}
int main()
{
    using Phase = CCSOLScenarioRules::Phase;
    // The exact production phase predicate and owner-scope query execute here.
    for (auto phase : {Phase::Loading, Phase::Selecting, Phase::Waiting, Phase::Starting,
                       Phase::Playing, Phase::Advancing, Phase::Completing, Phase::Failed,
                       Phase::Complete, Phase::FreePlay})
    {
        Reset(2); CBasePlayer p; scenarioRules.m_phase = phase;
        const bool allowed = phase != Phase::Loading && phase != Phase::Selecting &&
                             phase != Phase::Failed && phase != Phase::Complete;
        Check(p.CanPlayerBuy(false) == allowed, "actual phase purchase gate");
        p.HandleSignals();
        Check(bool(p.m_signals.GetState() & SIGNAL_BUY) == allowed, "actual phase menu gate");
        scenarioRules.m_restarting = true;
        Check(!p.CanPlayerBuy(false), "synchronous round restart rejects");
        scenarioRules.m_restarting = false; scenarioRules.m_completing = true;
        Check(!p.CanPlayerBuy(false), "synchronous completion rejects");
        scenarioRules.m_completing = false; scenarioRules.m_applyingFinish = true;
        Check(!p.CanPlayerBuy(false), "synchronous finish policy rejects");
        scenarioRules.m_applyingFinish = false; scenarioScopeCurrent = false;
        Check(!p.CanPlayerBuy(false), "stale owner scope rejects");
    }
    // Long rounds and mp_buytime=0 must not disable scenario purchases or mutate CVARs.
    for (float duration : {0.0f, 1.5f, -1.0f})
    {
        Reset(2); CBasePlayer p; buytime.value = duration; globals.time = 36000;
        Check(p.CanPlayerBuy(true), "scenario late purchase without zone");
        p.HandleSignals();
        Check(p.m_signals.GetState() & SIGNAL_BUY, "scenario menu signal");
        Check(buyIconSets == 1 && zoneChecks == 0, "scenario uses rule instead of spawn zone");
        Check(cvarWrites == 0 && buytime.value == duration, "scenario does not alter buytime");
        p.HandleSignals(); Check(buyIconSets == 1, "stable menu signal does not repeat icon");
        scenarioReady = false;
        Check(!p.CanPlayerBuy(true), "unavailable phase rejects purchase");
        p.HandleSignals();
        Check(!(p.m_signals.GetState() & SIGNAL_BUY) && buyIconClears == 1, "unavailable phase removes menu signal");
    }
    // No new permissions for invalid/dead players, VIPs or map-restricted teams.
    for (int invalid = 0; invalid < 12; ++invalid)
    {
        Reset(2); CBasePlayer p;
        switch (invalid)
        {
        case 0: p.pev->deadflag = 1; break;
        case 1: p.pev->health = 0; break;
        case 2: p.m_iTeam = TERRORIST; break;
        case 3: p.m_iTeam = 0; break;
        case 4: p.m_iTeam = 3; break;
        case 5: p.m_bIsVIP = true; break;
        case 6: rules.m_bCTCantBuy = true; break;
        case 7: rejected = true; break;
        case 8: p.has_disconnected = true; break;
        case 9: p.pev->flags = FL_SPECTATOR; break;
        case 10: p.pev->flags = FL_DORMANT; break;
        case 11: p.pev->flags = FL_KILLME; break;
        }
        Check(!p.CanPlayerBuy(true), "scenario invalid buyer rejected");
        p.HandleSignals(); Check(!(p.m_signals.GetState() & SIGNAL_BUY), "invalid buyer no menu signal");
    }
    // Actual buy operation still checks funds/capacity and only charges after delivery.
    for (int selectedMode : {0, 1, 2}) for (int id : {28, 725})
    {
        Reset(selectedMode); CBasePlayer p; CBasePlayerWeapon w; w.m_iId = id;
        if (selectedMode != 2) BuySignal(p); else globals.time = 36000;
        const int cost = id == 28 ? 60 : 70, amount = id == 28 ? 30 : 10;
        p.m_iAccount = cost - 1;
        Check(!BuyGunAmmo(&p, &w, true), "insufficient funds rejected");
        Check(p.m_iAccount == cost - 1 && p.m_rgAmmo[2] == 0 && blinks == 1, "no cost or ammo on insufficient funds");
        p.m_iAccount = 300;
        Check(BuyGunAmmo(&p, &w, true), "funded purchase succeeds");
        Check(p.m_iAccount == 300 - cost && p.m_rgAmmo[2] == amount, "exact ammo delivery and single cost");
        p.refuseAmmo = true;
        Check(!BuyGunAmmo(&p, &w, true) && p.m_iAccount == 300 - cost, "inventory refusal is free");
        p.refuseAmmo = false; p.m_rgAmmo[2] = 90;
        Check(!BuyGunAmmo(&p, &w, true) && p.m_iAccount == 300 - cost, "full inventory is free");
    }
    // Ordinary CS and ZP keep native time/zone/team restrictions.
    for (int selectedMode : {0, 1})
    {
        Reset(selectedMode); CBasePlayer p;
        Check(!p.CanPlayerBuy(false), "native no zone rejects");
        BuySignal(p); globals.time = 90;
        Check(p.CanPlayerBuy(false), "native inclusive deadline");
        globals.time = 90.1f;
        Check(!p.CanPlayerBuy(false), "native expired deadline");
        buytime.value = -1;
#ifdef REGAMEDLL_ADD
        Check(p.CanPlayerBuy(false), "native minus one unlimited");
#else
        Check(!p.CanPlayerBuy(false), "native minimum time retained without ADD");
#endif
        globals.time = 0; p.m_bIsVIP = true;
        Check(!p.CanPlayerBuy(false), "native VIP restricted");
        p.m_bIsVIP = false; rules.m_bCTCantBuy = true;
        Check(!p.CanPlayerBuy(false), "native CT map restriction");
        p.m_iTeam = TERRORIST; rules.m_bCTCantBuy = false; rules.m_bTCantBuy = true;
        Check(!p.CanPlayerBuy(false), "native T map restriction");
        Reset(selectedMode); CBasePlayer q; rules.m_bMapHasBuyZone = false; insideSpawn = true;
        q.HandleSignals(); Check(q.m_signals.GetState() & SIGNAL_BUY, "native fallback spawn zone retained");
        insideSpawn = false; q.HandleSignals(); Check(!(q.m_signals.GetState() & SIGNAL_BUY), "native exit spawn zone retained");
#ifdef REGAMEDLL_ADD
        buytime.value = 0; insideSpawn = true;
        const int oldChecks = zoneChecks; q.HandleSignals();
        Check(zoneChecks == oldChecks && !(q.m_signals.GetState() & SIGNAL_BUY), "native zero disables zone search");
        buytime.value = 1.5f; buy_anywhere.value = 3; insideSpawn = false;
        q.HandleSignals(); Check(q.m_signals.GetState() & SIGNAL_BUY, "native buy_anywhere unchanged");
#endif
    }
    Reset(0); CBasePlayer training; rules.multiplayer = false;
    Check(training.CanPlayerBuy(false), "training delegates allowed");
    trainingAllowed = false; Check(!training.CanPlayerBuy(false), "training delegates rejected");
    std::cout << "PASS actual purchase functions: " << checks << " checks\n";
}
