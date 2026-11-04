// SPDX-License-Identifier: GPL-3.0-or-later
// Run the complete production Session::Precache against the matched SDK.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_session.h"
#include "../../native/csol/boss_health.hpp"
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

namespace {
using namespace sdk_test;
namespace fs = std::filesystem;
fs::path server, resources;
std::vector<std::string> expected;
std::string denied;
std::map<std::string, int> advertised;
std::set<std::string> observed;
std::shared_ptr<CCSOLScenarioSession> current;
int mode{}, assertions{}, failed{}, queries{}, registrations{}, bossQueries{};
int revokeQueries{}, revokeRegistrations{};
bool callbackSeen{}, emptyFile{};

void Expect(bool value, const char* label)
{ ++assertions; if (!value) { ++failed; std::cerr << "ASSERT " << label << '\n'; } }
bool Boss(std::string_view path)
{ return std::find(expected.begin(), expected.end(), path) != expected.end(); }
std::string Utf8(const fs::path& path)
{ const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
fs::path Resolve(const char* name)
{
    const fs::path relative(name);
    Require(!relative.is_absolute(), "resource path is relative");
    for (const auto& part : relative) Require(part != "..", "resource stays within roots");
    // BOSS images are frozen copies of the extracted originals. Missing and
    // empty cases use independent test files; no live resource is modified.
    if (denied == name) return emptyFile ? resources / "empty-file" : resources / "absent" / relative;
    if (Boss(name)) return resources / relative;
    const auto primary = server / relative;
    if (fs::is_regular_file(primary)) return primary;
    return server.parent_path() / "valve" / relative;
}
void Boundary(bool generic)
{
    if (callbackSeen || mode < 17 || mode > 20 || generic != (mode == 18 || mode == 20)) return;
    callbackSeen = true;
    if (mode <= 18)
    {
        Expect(current->Busy(), "resource callback sees busy Session owner");
        Expect(!current->Precache("zs_deadend", 1, CSOLScenarioResourceScope::Combat), "recursive Precache is refused");
        Expect(!current->Activate(), "busy Session cannot activate during a resource callback");
        Expect(!current->Deactivate(), "busy Session cannot destroy its borrowed resource tables");
    }
    else
    {
        current->RevokeEntityAccess();
        revokeQueries = queries; revokeRegistrations = registrations;
    }
}
int Size(const char* name)
{
    ++queries;
    if (Boss(name)) { ++bossQueries; Boundary(false); }
    const auto path = Resolve(name);
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) return -1;
    Require(size <= std::numeric_limits<int>::max(), "resource fits engine size");
    if (observed.emplace(name).second)
        std::cout << "FILE\t" << size << '\t' << name << '\t' << Utf8(path) << '\n';
    return static_cast<int>(size);
}
unsigned char* Load(const char* name, int* size)
{
    *size = Size(name);
    if (*size < 0) { *size = 0; return nullptr; }
    const auto path = Resolve(name);
    const auto stamp = fs::last_write_time(path);
    auto bytes = std::make_unique<unsigned char[]>(static_cast<std::size_t>(*size) + 1);
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "real resource opens");
    input.read(reinterpret_cast<char*>(bytes.get()), *size);
    Require(input.gcount() == *size && fs::last_write_time(path) == stamp, "disk read is complete and unchanged");
    bytes[*size] = 0;
    auto* result = bytes.get(); Engine::self->buffers.emplace(result, std::move(bytes)); return result;
}
int Generic(const char* name)
{
    ++registrations; ++advertised[name];
    if (Boss(name)) Boundary(true);
    return int(advertised.size());
}
void Run()
{
    for (const auto* path : csol::boss_health::Resources) expected.emplace_back(path);
    for (const auto* path : csol::boss_health::AbilityResources) expected.emplace_back(path);
    Require(expected.size() == 8, "five main and three ability images are in scope");
    if (mode >= 0 && mode < 16) { denied = expected.at(mode % 8); emptyFile = mode >= 8; }
    auto engine = std::make_unique<Engine>();
    engine->files.clear();
    engine->globals.mapname = Engine::String("zs_deadend");
    g_engfuncs.pfnLoadFileForMe = Load;
    g_engfuncs.pfnGetFileSize = Size;
    g_engfuncs.pfnPrecacheGeneric = Generic;
    engine->Prepare("2"); LinkUserMessages();
    g_pGameRules = InstallGameRules(); Require(g_pGameRules && !CSOL_RulesRejected(), "real scenario rules registered");
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    { CSOLScenarioSdkScope scope; runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
      Require(scope.Current() && runtime, "actual registered scenario runtime"); }
    current = runtime->session;
    Require(bool(current), "registered runtime owns Session");
    const auto scope = mode == 16 ? CSOLScenarioResourceScope::Selection : CSOLScenarioResourceScope::Combat;
    const bool ready = current->Precache("zs_deadend", 1, scope);
    if (!ready) std::cout << "SESSION\t" << current->Error().path << '\t' << current->Error().detail.message << '\n';
    if (mode >= 0 && mode < 16)
    {
        Expect(!ready && current->State() == CSOLScenarioSessionState::Failed, "missing or empty BOSS resource rejects actual Combat Precache");
        Expect(current->Error().path == denied, "failure identifies exact absent BOSS HUD resource");
        Expect(current->Error().detail.message == "required scenario HUD resource is missing or empty", "failure uses actual required-resource check");
        Expect(!advertised.contains(denied), "absent BOSS resource is never advertised");
        Expect(current->Weapons() && !current->Weapons()->Current(), "failed Session invalidates its weapon owner");
        const auto beforeQueries = queries, beforeRegistrations = registrations;
        Expect(!current->Activate(), "failed Session cannot activate");
        Expect(!current->Precache("zs_deadend", 1, scope), "failed Session cannot silently retry");
        Expect(queries == beforeQueries && registrations == beforeRegistrations, "failed-state calls do not advertise additional resources");
        // Already advertised earlier assets are an irreversible engine side
        // effect; this test deliberately does not assert a fictitious rollback.
    }
    else if (mode == 19 || mode == 20)
    {
        Expect(callbackSeen, "resource callback actually retired the map");
        Expect(!ready && !current->Map().EntityAccessAllowed(), "retired map aborts Precache");
        Expect(current->State() != CSOLScenarioSessionState::Precached && current->State() != CSOLScenarioSessionState::Active,
            "retired Session never publishes a usable state");
        Expect(queries == revokeQueries && registrations == revokeRegistrations, "no resource calls follow retirement boundary");
        Expect(!current->Activate(), "retired Session cannot activate");
    }
    else
    {
        Expect(ready && current->State() == CSOLScenarioSessionState::Precached, "actual full Session Precache succeeds");
        for (const auto& path : expected)
            Expect(mode == 16 ? !advertised.contains(path) : advertised[path] == 1,
                mode == 16 ? "Selection does not advertise Combat BOSS assets" : "Combat advertises each BOSS HUD asset exactly once");
        if (mode == 16) Expect(bossQueries == 0, "Selection never checks Combat BOSS resources");
        if (mode == 17 || mode == 18) Expect(callbackSeen, "actual resource boundary exercised reentrant calls");
        const auto beforeQueries = queries, beforeRegistrations = registrations;
        Expect(!current->Precache("zs_deadend", 1, scope), "already precached Session rejects repeated Precache");
        Expect(queries == beforeQueries && registrations == beforeRegistrations, "repeated Precache cannot duplicate downloads");
    }
    Expect(engine->buffers.empty(), "every file load released before returning");
    if (mode == 21)
    {
        const auto weapons = current->Weapons();
        Expect(current->Deactivate() && current->State() == CSOLScenarioSessionState::Empty, "orderly shutdown restores Empty state");
        Expect(weapons && !weapons->Current(), "prior map weapon owner remains invalidated");
        advertised.clear(); observed.clear();
        Expect(current->Precache("zs_deadend", 1, scope), "new map precaches after orderly shutdown");
        for (const auto& path : expected) Expect(advertised[path] == 1, "new map independently advertises every BOSS resource once");
        Expect(engine->buffers.empty(), "new map also releases every file buffer");
    }
    for (const auto& [path, count] : advertised) std::cout << "GENERIC\t" << count << '\t' << path << '\n';
    engine->Clear(); current.reset(); runtime.reset();
}
}
int wmain(int argc, wchar_t** argv)
{
    try { Require(argc == 4, "case/server/frozen-resource arguments required");
          mode = std::stoi(argv[1]); server = argv[2]; resources = argv[3]; Run(); }
    catch (const std::exception& error) { ++failed; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "boss-resources-sdk assertions=" << assertions << " failures=" << failed
              << " SDK checks=" << sdk_test::checks << " SDK failures=" << sdk_test::failures << '\n';
    return failed || sdk_test::failures ? 1 : 0;
}
