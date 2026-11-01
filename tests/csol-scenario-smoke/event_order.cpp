// SPDX-License-Identifier: GPL-3.0-or-later
// Actual engine/event and weapon precache bodies, with explicit controlled
// filesystem/entity edges. This does not execute the full ServerActivate.
#include "native/csol/scenario_smoke.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define EXT_FUNC
#define REHLDS_FIXES
#define MAX_PATH 260
constexpr int MAX_EVENTS = 256;
constexpr int ss_loading = 1;
constexpr int ss_active = 2;
struct event_s { const char* filename{}; int filesize{}; char* pszScript{}; int index{}; };
struct { int state{}; event_s event_precache[MAX_EVENTS]{}; } g_psv;
std::deque<std::string> eventNames;
const char* ED_NewString(const char* name)
{ eventNames.emplace_back(name); return eventNames.back().c_str(); }
[[noreturn]] void Host_Error(const char*, ...) { throw std::runtime_error("engine Host_Error"); }
bool PR_IsEmptyString(const char* value) { return *value == 0; }
int Q_stricmp(const char* a, const char* b)
{
    // Active lookup must find an already registered event before empty slots.
    if (!a || !b) throw std::runtime_error("unexpected null event lookup");
    while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b)))
        ++a, ++b;
    return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}
#define Q_snprintf std::snprintf
#define Q_memset std::memset
void COM_FixSlashes(char* text) { for (; *text; ++text) if (*text == '\\') *text = '/'; }
int fileLoads{};
char* COM_LoadFile(const char*, int, int* size)
{ ++fileLoads; *size = 1; static char data[] = "\n"; return data; }
#include "actual_engine_event.inc"

std::function<void()> afterEvent;
unsigned short EventEdge(int type, const char* path)
{
    const auto result = EV_Precache_internal(type, path);
    if (afterEvent) afterEvent();
    return result;
}
int fileSize = 1;
std::function<void()> afterFileSize;
std::string fileRequested;
int FileSize(const char* path)
{ fileRequested = path; if (afterFileSize) afterFileSize(); return fileSize; }
struct {
    int (*pfnGetFileSize)(const char*) = FileSize;
    unsigned short (*pfnPrecacheEvent)(int, const char*) = EventEdge;
} g_engfuncs;
#define PRECACHE_EVENT(type, path) g_engfuncs.pfnPrecacheEvent(type, path)
int PRECACHE_MODEL(const char*) { return 1; }
int PRECACHE_SOUND(const char*) { return 1; }
void CSOL_ResetWeaponRegistry() {}
struct CBasePlayerItem { static inline int m_ItemInfoArray[64]{}, m_AmmoInfoArray[32]{}; };
int giAmmoIndex{};
struct Rules { bool IsDeathmatch() const { return true; } } rules;
auto* g_pGameRules = &rules;
void UTIL_PrecacheOther(const char*) {}
#include "fixture_stock_globals.inc"
#include "fixture_stock_declarations.inc"
#include "actual_stock_precaches.inc"
void UTIL_PrecacheOtherWeapon(const char* name)
{
#include "fixture_stock_dispatch.inc"
}
#include "actual_stock_world_precache.inc"

enum class CSOLScenarioResourceScope { Selection, Combat };
struct AccessMap
{
    bool allowed = true;
    int epoch = 1;
    bool EntityAccessAllowed() const { return allowed; }
    int EntityAccess() const { return epoch; }
};
struct Error { std::string path; struct Detail { int line; std::string reason; } detail; };
struct Session
{
    AccessMap m_map;
    CSOLScenarioResourceScope m_scope = CSOLScenarioResourceScope::Combat;
    unsigned short m_smokeEvent{};
    bool failed{};
    bool Fail(Error) { failed = true; return false; }
    bool Precheck()
    {
        const auto access = m_map.EntityAccess();
#include "actual_smoke_precache.inc"
        return true;
    }
    bool Activate()
    {
#include "actual_smoke_activate.inc"
        return true;
    }
};
int checks{};
void Check(bool result, const char* name) { ++checks; if (!result) throw std::runtime_error(name); }
std::vector<std::string> Table()
{
    std::vector<std::string> result{ "" };
    for (int i = 1; i < MAX_EVENTS && g_psv.event_precache[i].filename; ++i)
        result.emplace_back(g_psv.event_precache[i].filename);
    return result;
}
int Find(const std::vector<std::string>& table, const char* name)
{
    const auto it = std::find(table.begin(), table.end(), name);
    if (it == table.end()) throw std::runtime_error("missing registered event");
    return static_cast<int>(it - table.begin());
}
std::vector<std::string> Load(int mode, bool finishLoading = true)
{
    g_psv = {};
    eventNames.clear();
    g_psv.state = ss_loading;
    fileLoads = 0;
    Session session;
    if (mode == 2)
    {
        Check(session.Precheck(), "scenario resource precheck succeeds");
        Check(Table().size() == 1 && fileLoads == 0, "resource precheck cannot register any event");
        Check(fileRequested == csol::smoke::EventPath, "resource precheck checks the dedicated event");
    }
    WeaponsPrecache();
    const int loaded = fileLoads;
    const auto stock = Table();
    // ReHLDS invokes the game DLL ServerActivate before setting ss_active.
    if (mode == 2)
    {
        Check(session.Activate(), "scenario activation appends its event during loading");
        const auto appended = Table();
        Check(appended.size() == stock.size() + 1 &&
              std::equal(stock.begin(), stock.end(), appended.begin()), "scenario adds exactly one event after every stock entry");
        Check(appended.back() == "events/csol_scenario_smoke.sc", "new tail entry is dedicated scenario smoke");
        Check(session.m_smokeEvent == stock.size() &&
              session.m_smokeEvent != Find(stock, "events/createsmoke.sc"), "scenario index differs from native smoke");
        Check(fileLoads == loaded + 1, "scenario script loads exactly once");
        Check(session.Activate() && Table() == appended && fileLoads == loaded + 1,
              "repeated activation branch does not add or reload an event");
        if (finishLoading)
        {
            g_psv.state = ss_active;
            Check(session.Activate() && Table() == appended && fileLoads == loaded + 1,
                  "active lookup of an existing scenario event is stable");
        }
    }
    if (finishLoading) g_psv.state = ss_active;
    return Table();
}
int main()
try
{
    Check(std::strcmp(csol::smoke::EventPath, "events/csol_scenario_smoke.sc") == 0,
          "production event name does not reuse native smoke");
    const auto stock = Load(0);
    const int usp = Find(stock, "events/usp.sc");
    Check(Load(1) == stock, "controlled native and ZP precache paths have the same stock table");
    for (const int initial : {0, 1, 2})
    {
        const auto first = Load(initial);
        const int cachedUsp = Find(first, "events/usp.sc");
        for (const int mode : {2, 0, 1, 2, 2, 0})
        {
            const auto current = Load(mode);
            Check(current.size() == stock.size() + (mode == 2 ? 1 : 0), "only scenario sessions add a dedicated event");
            Check(std::equal(stock.begin(), stock.end(), current.begin()), "every native event index survives mode transitions");
            Check(current.at(cachedUsp) == "events/usp.sc" && cachedUsp == usp,
                  "persistent stock USP prediction index retains its callback name");
        }
    }
    for (const bool revoke : {false, true})
    {
        Load(0, false);
        const auto before = Table();
        Session cancelled;
        afterEvent = [&] { if (revoke) cancelled.m_map.allowed = false; else ++cancelled.m_map.epoch; };
        Check(!cancelled.Activate() && !cancelled.m_smokeEvent && !cancelled.failed,
              "map replacement or access revocation prevents committing event index");
        afterEvent = {};
        Check(Table().size() == before.size() + 1 && std::equal(before.begin(), before.end(), Table().begin()),
              "cancelled owner leaves native engine entries intact even after insertion");
    }
    Load(0, false);
    const auto beforeSelection = Table();
    Session selection;
    selection.m_scope = CSOLScenarioResourceScope::Selection;
    Check(selection.Activate() && !selection.m_smokeEvent && Table() == beforeSelection,
          "selection scope adds no combat event");
    Session missing;
    g_engfuncs.pfnPrecacheEvent = nullptr;
    Check(!missing.Precheck() && missing.failed, "precheck requires the event API");
    Session missingAtActivate;
    Check(!missingAtActivate.Activate() && missingAtActivate.failed, "activation rejects a missing event API");
    g_engfuncs.pfnPrecacheEvent = [](int, const char*) -> unsigned short { return 0; };
    Session emptyEvent;
    Check(!emptyEvent.Activate() && emptyEvent.failed && !emptyEvent.m_smokeEvent,
          "zero event result fails without committing an index");
    g_engfuncs.pfnPrecacheEvent = EventEdge;
    fileSize = 0;
    Session emptyFile;
    Check(!emptyFile.Precheck() && emptyFile.failed, "precheck rejects an empty event script");
    fileSize = 1;
    Session changedAtPrecheck;
    afterFileSize = [&] { ++changedAtPrecheck.m_map.epoch; };
    Check(!changedAtPrecheck.Precheck() && !changedAtPrecheck.failed, "resource precheck honors map replacement");
    afterFileSize = {};
    std::cout << "scenario-smoke-event-order " << checks << " checks 0 failures stock_events="
              << stock.size() - 1 << " cached_usp=" << usp << '\n';
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
