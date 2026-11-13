// SPDX-License-Identifier: GPL-3.0-or-later
// Actual server adapter, Store/worker/JSON and CreateInterface registry. Only
// named engine/mode edges and the kick-policy output are controlled here.
#include "precompiled.h"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include "goldcraft/csol_gameplay_settings_api.hpp"

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
// Exact SDK globals.cpp:13 default consumed by precompiled inline bot guards.
// The settings fixture never installs/executes the bot subsystem.
bool g_bAllowedCSBot = false;
namespace test
{
namespace custom = csol::scenario::custom;
namespace fs = std::filesystem;
int checks{}, failures{}, mode{2}, registrations{}, sets{}, userQueries{}, callbackQueries{};
DWORD ownerThread{};
fs::path root;
globalvars_t globals{};
std::array<edict_t, 33> edicts;
std::array<int, 33> userids;
struct Cvar { cvar_t* native{}; std::string text; };
std::map<std::string, Cvar> cvars;
std::vector<bool> policies;
std::vector<std::string> alerts;
std::function<void()> onCvarSet, onUserid;
csol::IGameplaySettingsService* service{};
void Check(bool value, const char* text)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
void Require(bool value, const char* text)
{ Check(value, text); if (!value) throw std::runtime_error(text); }
void EngineThread()
{ Check(GetCurrentThreadId() == ownerThread, "engine callback is never invoked by the disk worker"); }
std::string Bytes(const fs::path& path)
{ std::ifstream file(path, std::ios::binary); Require(bool(file), "actual settings file exists"); return {std::istreambuf_iterator<char>(file), {}}; }
fs::path Target() { return root / "addons/goldcraft/config/gameplay.json"; }
void Alert(ALERT_TYPE, const char* format, ...)
{
    EngineThread(); char text[2048]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    alerts.emplace_back(text);
}
void Set(const char* name, const char* text)
{
    EngineThread(); ++sets;
    auto found = cvars.find(name); Require(found != cvars.end(), "only actual registered CVARs can be changed");
    auto& record = found->second; record.text = text;
    record.native->string = record.text.data(); record.native->value = std::strtof(record.text.c_str(), nullptr);
    const auto callback = onCvarSet; if (callback) callback();
}
void Prepare(bool existing)
{
    ownerThread = GetCurrentThreadId();
    if (existing) Require(fs::is_directory(root), "reopen process uses the exact existing isolated root");
    else { Require(!fs::exists(root), "fresh isolated test game root is required"); fs::create_directories(root); }
    globals.maxClients = 32; globals.time = 10; gpGlobals = &globals;
    for (int i = 0; i <= 32; ++i)
    { edicts[i] = {}; edicts[i].free = TRUE; edicts[i].serialnumber = 1; edicts[i].v.pContainingEntity = &edicts[i]; }
    edicts[1].free = FALSE; edicts[1].v.flags = FL_CLIENT; edicts[1].v.health = 0; userids[1] = 71;
    g_engfuncs.pfnGetGameDir = [](char* output)
    { EngineThread(); const auto text = root.string(); Require(text.size() < 4096, "controlled engine directory fits actual adapter buffer"); std::strcpy(output, text.c_str()); };
    g_engfuncs.pfnCVarRegister = [](cvar_t* variable)
    {
        EngineThread(); ++registrations;
        Require(variable && variable->name && variable->string, "actual CVAR registration has stable name/default storage");
        auto [found, added] = cvars.try_emplace(variable->name, Cvar{variable, variable->string});
        Require(added, "every descriptor registers exactly one CVAR");
        variable->string = found->second.text.data(); variable->value = std::strtof(variable->string, nullptr);
    };
    g_engfuncs.pfnCVarSetString = Set;
    g_engfuncs.pfnCVarSetFloat = [](const char* name, float value)
    { char text[64]; std::snprintf(text, sizeof(text), "%.9g", double(value)); Set(name, text); };
    g_engfuncs.pfnAlertMessage = Alert;
    g_engfuncs.pfnPEntityOfEntIndex = [](int index) -> edict_t*
    { EngineThread(); return index >= 0 && index <= 32 ? &edicts[index] : nullptr; };
    g_engfuncs.pfnGetPlayerUserId = [](edict_t* entity)
    {
        EngineThread(); ++userQueries;
        Require(entity >= edicts.data() && entity < edicts.data() + edicts.size(), "userid query uses a real controlled edict");
        const int result = userids[entity - edicts.data()]; const auto callback = onUserid; if (callback) callback(); return result;
    };
    int code{};
    Require(CreateInterface("CSOLGameplaySettings999", &code) == nullptr && code == IFACE_FAILED, "real factory rejects an unknown interface version");
    service = static_cast<csol::IGameplaySettingsService*>(CreateInterface(csol::GameplaySettingsServiceVersion, &code));
    Require(service && code == IFACE_OK, "actual InterfaceReg resolves the production settings service");
}
csol::GameplaySettingsStatus Status()
{
    csol::GameplaySettingsStatus status;
    Require(service->Status(status), status.error); return status;
}
void CheckMirrors()
{
    const auto current = CSOL_CustomSettings(); Require(bool(current), "actual adapter owns a published immutable snapshot");
    for (const auto& descriptor : custom::Features())
    {
        const auto found = cvars.find(std::string(descriptor.cvar));
        Require(found != cvars.end(), "feature CVAR uses actual descriptor identity");
        Check(found->second.native->value == (current->Enabled(descriptor.id) ? 1 : 0), "feature mirror matches published snapshot");
    }
    for (const auto& descriptor : custom::Settings())
    {
        const auto found = cvars.find(std::string(descriptor.cvar)); Require(found != cvars.end(), "numeric CVAR uses actual descriptor identity");
        const auto number = current->Number(descriptor.id);
        Check(found->second.native->value == (number ? static_cast<float>(*number) : -1.0f), "numeric mirror/unknown sentinel matches actual snapshot");
    }
}
custom::SnapshotPtr Disk()
{
    custom::SnapshotPtr result; custom::Error error;
    Require(custom::Parse(Bytes(Target()), result, error), "actual persisted JSON validates through the production parser"); return result;
}
void Init()
{
    csol::GameplaySettingsStatus unavailable;
    Check(!service->Status(unavailable), "registered service is unavailable before startup");
    CSOL_InitGameplaySettings();
    const auto state = Status();
    Require(state.count == int(custom::FeatureCount + custom::SettingCount) && state.revision == 1 && !state.busy,
        "actual store initialization exposes the complete descriptor count and first revision");
    Check(registrations == int(custom::FeatureCount + custom::SettingCount), "all11 feature plus37 numeric CVARs register once");
    const int before = registrations; CSOL_InitGameplaySettings();
    Check(registrations == before, "duplicate initialization does not recreate file owner or CVARs");
    CheckMirrors();
    for (int id = 0; id < state.count; ++id)
    {
        csol::GameplaySetting field; Require(service->Read(id, field), "real service exposes each typed descriptor");
        Check(field.id == id && field.key[0] && field.cvar[0] && field.comment[0], "every service field retains identity and explanation");
        if (id < int(custom::FeatureCount)) Check(field.available == (id == int(custom::Feature::Kick)), "only integrated feature capability is reported available");
    }
    csol::GameplaySetting field;
    Check(!service->Read(-1, field) && !service->Read(state.count, field), "unknown field indices are rejected");
    Check(Disk()->Enabled(custom::Feature::Kick) == CSOL_CustomSettings()->Enabled(custom::Feature::Kick), "sole gameplay file and initial snapshot agree");
}
void Wait(std::uint32_t previous, bool success)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline)
    {
        CSOL_PollGameplaySettings(); const auto state = Status();
        if (!state.busy && state.result != csol::GameplayEditResult::Queued)
        {
            Check(state.result == (success ? csol::GameplayEditResult::Saved : csol::GameplayEditResult::Failed), "real worker result reaches adapter without forced success");
            Check(state.revision == previous + (success ? 1 : 0), "revision advances only after a successful real file commit"); return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("actual store worker did not complete within bounded fixture wait");
}
void Negative()
{
    const auto before = Status(); const auto snapshot = CSOL_CustomSettings(); const auto bytes = Bytes(Target());
    using Result = csol::GameplayEditResult;
    for (const int actor : {0, -1, 33}) Check(service->Edit(actor, 71, before.revision, 0, 0) == Result::Failed, "invalid/console actor cannot edit");
    for (const int userid : {0, -1, 72}) Check(service->Edit(1, userid, before.revision, 0, 0) == Result::Failed, "invalid or stale userid cannot edit");
    Check(service->Edit(1, 71, before.revision + 1, 0, 0) == Result::Stale, "CAS rejects stale revision before submission");
    for (const int id : {-1, before.count}) Check(service->Edit(1, 71, before.revision, id, 0) == Result::Failed, "unknown entry cannot edit");
    for (const double value : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), .5})
        Check(service->Edit(1, 71, before.revision, 0, value) == Result::Failed, "boolean edit rejects nonfinite and nonboolean inputs");
    Check(service->Edit(1, 71, before.revision, int(custom::Feature::TimeCycle), 1) == Result::Unavailable,
        "uninstalled feature refuses activation instead of reporting gameplay ready");
    Check(service->Edit(1, 71, before.revision, int(custom::FeatureCount) + int(custom::Setting::MiddaySeconds), 1.5) == Result::Failed,
        "real adapter preserves pure parser integer requirement");
    mode = 1; Check(service->UserId(1) == 0 && service->Edit(1, 71, before.revision, 0, 0) == Result::Failed, "other mode cannot enter custom edits"); mode = 2;
    for (const int flag : {FL_FAKECLIENT, FL_DORMANT, FL_KILLME})
    { edicts[1].v.flags |= flag; Check(service->UserId(1) == 0, "fake dormant and retiring entities are excluded"); edicts[1].v.flags &= ~flag; }
    edicts[1].free = TRUE; Check(service->UserId(1) == 0, "free player slot has no authoritative userid"); edicts[1].free = FALSE;
    userids[1] = 0; Check(service->Edit(1, 71, before.revision, 0, 0) == Result::Failed, "native userid zero cannot match a claimed live actor"); userids[1] = 71;
    onUserid = [] { if (++callbackQueries == 1) userids[1] = 72; };
    Check(service->Edit(1, 71, before.revision, 0, 0) == Result::Failed, "second real engine userid query rejects reuse before Store Submit");
    onUserid = {}; userids[1] = 71;
    const auto after = Status();
    Check(after.revision == before.revision && !after.busy && CSOL_CustomSettings() == snapshot && Bytes(Target()) == bytes,
        "all rejected requests preserve real disk snapshot revision and idle state");
}
void Writer()
{
    Init(); Negative();
    const auto originalFile = Bytes(Target());
    Set("csol_custom_kick", "0"); CSOL_PollGameplaySettings();
    Check(!CSOL_CustomFeatureEnabled(custom::Feature::Kick) && !policies.back() && CSOL_CustomSettings()->Enabled(custom::Feature::Kick) && Bytes(Target()) == originalFile,
        "temporary console CVAR disables policy without silently persisting it");
    Set("csol_custom_kick", "1"); CSOL_PollGameplaySettings();
    Check(policies.back() && CSOL_CustomFeatureEnabled(custom::Feature::Kick), "temporary console override is reversible");
    const auto previous = Status(); const auto snapshot = CSOL_CustomSettings();
    bool reentered{}, busyObserved{};
    onCvarSet = [&]
    {
        if (reentered) return; reentered = true;
        busyObserved = service->Edit(1, 71, previous.revision, 0, 1) == csol::GameplayEditResult::Busy;
    };
    Require(service->Edit(1, 71, previous.revision, 0, 0) == csol::GameplayEditResult::Queued, "actual typed edit queues real asynchronous file persistence");
    Check(CSOL_CustomSettings() == snapshot && policies.back() && cvars.at("csol_custom_kick").native->value == 1,
        "worker cannot publish snapshot or engine policy before owner Poll");
    Wait(previous.revision, true); onCvarSet = {};
    Check(reentered && busyObserved, "actual publication CVAR callback cannot reenter an edit transaction");
    Check(!Disk()->Enabled(custom::Feature::Kick) && !CSOL_CustomSettings()->Enabled(custom::Feature::Kick) &&
        !CSOL_CustomFeatureEnabled(custom::Feature::Kick) && !policies.back(), "disk snapshot CVAR and real policy output all reflect committed off");
    CheckMirrors();
    const auto next = Status();
    Require(service->Edit(1, 71, next.revision, int(custom::FeatureCount) + int(custom::Setting::MiddaySeconds), 240) == csol::GameplayEditResult::Queued,
        "typed numeric edit uses same real store transaction");
    Wait(next.revision, true);
    Check(Disk()->Integer(custom::Setting::MiddaySeconds) == 240 && CSOL_CustomSettings()->Integer(custom::Setting::MiddaySeconds) == 240,
        "real serialized numeric value agrees with published snapshot");
    Check(fs::exists(Target().parent_path() / "gameplay.json.bak1") && fs::exists(Target().parent_path() / "gameplay.json.bak2"), "actual Store creates bounded prior-file backups");
    CSOL_ShutdownGameplaySettings();
    Check(!CSOL_CustomSettings() && !policies.back(), "explicit cold shutdown drains worker and revokes policy");
}
void Reader()
{
    Init();
    Check(!CSOL_CustomSettings()->Enabled(custom::Feature::Kick) && !CSOL_CustomFeatureEnabled(custom::Feature::Kick) && !policies.back(),
        "independent second process initializes policy from persisted off value");
    Check(CSOL_CustomSettings()->Integer(custom::Setting::MiddaySeconds) == 240 &&
        cvars.at("csol_custom_midday_seconds").native->value == 240, "second process restores actual persisted numeric configuration");
    Check(Disk()->Integer(custom::Setting::MiddaySeconds) == 240, "second process verifies the same sole target file");
}
void Failure()
{
    Init(); const auto previous = Status(); const auto snapshot = CSOL_CustomSettings(); const auto bytes = Bytes(Target());
    const int previousSets = sets;
    HANDLE block = CreateFileW(Target().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(block != INVALID_HANDLE_VALUE, "real deny-delete file handle controls actual disk failure");
    Require(service->Edit(1, 71, previous.revision, 0, 0) == csol::GameplayEditResult::Queued, "candidate enters real worker even when destination is denied replacement");
    Wait(previous.revision, false); CloseHandle(block);
    Check(CSOL_CustomSettings() == snapshot && Bytes(Target()) == bytes && cvars.at("csol_custom_kick").native->value == 1 &&
        policies.back() && sets == previousSets, "failed persistence publishes neither candidate snapshot CVAR nor policy");
    Check(!alerts.empty(), "actual adapter reports the file failure through its engine alert boundary");
}
void InvalidStartup()
{
    fs::create_directories(Target().parent_path());
    { std::ofstream file(Target(), std::ios::binary); file << "{invalid operator configuration}"; }
    CSOL_InitGameplaySettings(); csol::GameplaySettingsStatus status;
    Check(!service->Status(status) && status.error[0] && registrations == 0 && !CSOL_CustomSettings() && !policies.back(),
        "invalid existing file leaves service unavailable and kick policy disabled");
    Check(Bytes(Target()) == "{invalid operator configuration}", "invalid startup file is retained without forced defaults");
}
}
int CSOL_LockedGameMode() { return test::mode; }
void CSOL_SetHumanKickPolicy(bool value)
{
    test::EngineThread(); test::policies.push_back(value);
    test::Check(value == CSOL_CustomFeatureEnabled(csol::scenario::custom::Feature::Kick), "controlled kick output equals actual effective policy");
}
int main(int argc, char** argv)
{
    static_assert(sizeof(void*) == 4);
    try
    {
        test::Require(argc == 3, "explicit group and isolated root required");
        const std::string group = argv[1]; test::root = std::filesystem::absolute(argv[2]);
        test::Prepare(group == "reader");
        if (group == "writer") test::Writer();
        else if (group == "reader") test::Reader();
        else if (group == "failure") test::Failure();
        else if (group == "invalid") test::InvalidStartup();
        else if (group == "no-engine")
        {
            g_engfuncs.pfnGetGameDir = nullptr; CSOL_InitGameplaySettings(); csol::GameplaySettingsStatus status;
            test::Check(!test::service->Status(status) && status.error[0] && test::registrations == 0 && !test::policies.back(),
                "missing real game-directory API fails startup without enabling kick");
        }
        else throw std::runtime_error("unknown adapter fixture group");
        CSOL_ShutdownGameplaySettings();
    }
    catch (const std::exception& error)
    { ++test::failures; std::cerr << "ERROR " << error.what() << '\n'; CSOL_ShutdownGameplaySettings(); }
    std::cout << "pid=" << GetCurrentProcessId() << " checks=" << test::checks << " failures=" << test::failures << '\n';
    return test::failures ? 1 : 0;
}
