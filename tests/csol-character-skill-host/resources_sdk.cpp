// SPDX-License-Identifier: GPL-3.0-or-later
// Execute PrecacheSkillHost from its complete production TU, with the real
// ReGameDLL SDK/session and disk resources. Only engine boundaries are controlled.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/scenario_session.h"
#include "../../native/csol/server/scenario_character_skills/host_notifications.hpp"
#include "../../native/csol/boss_health.hpp"
#include <filesystem>
#include <fstream>
#include <set>

namespace
{
using namespace sdk_test;
namespace fs = std::filesystem;
constexpr std::array WorldSprites{
    "sprites/zombiarmorheal_head.spr", "sprites/zb_skill_hpbuff.spr",
    "sprites/zbt_invincibility.spr", "sprites/DoubleSideA.spr", "sprites/DoubleSideB.spr"};
constexpr std::array ExistingModels{
    "models/v_emotion0.mdl", "models/v_emotion1.mdl",
    "models/ef_buffclassskill_heal.mdl", "models/ef_buffclassskill_damage.mdl",
    "sprites/ef_buffclass20s2.spr", "sprites/ef_zbs73_explosion.spr",
    "models/ef_buffclass26-3_skill.mdl",
    "sprites/ef_buffclass24s3tr_skill.spr", "sprites/ef_buffclass24s3ct_skill.spr",
    "models/ef_meatwall_wave.mdl", "sprites/ef_buffcalss24s4_skill.spr",
    "sprites/ef_buffclass24s1tr_skill.spr", "models/ef_scorpion_hole.mdl",
    "sprites/ef_buffclass23s3tr_skill01.spr", "sprites/ef_buffclass23s3ct_skill01.spr",
    "models/ef_buffclass22s4tr.mdl", "models/ef_buffclass22s4ct.mdl",
    "models/ef_buffskill.mdl", "models/ef_y21s3buffclassct.mdl", "sprites/ef_sbmine_debuff.spr"};
fs::path server, common;
std::string denied, rejected;
std::map<std::string, int> generic;
std::set<std::string> observed;
int assertions{}, failed{}, invalidIndex{};
void Expect(bool value, const char* label)
{ ++assertions; if (!value) { ++failed; std::cerr << "ASSERT " << label << '\n'; } }
std::string Utf8(const fs::path& path)
{ const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
fs::path Resolve(const char* name)
{
    const fs::path relative(name);
    Require(!relative.is_absolute(), "resource path is relative");
    for (const auto& part : relative) Require(part != "..", "resource stays within roots");
    // These known Boss HUD assets are not involved in the skill registration.
    for (std::size_t i = 0; i < 4; ++i)
        if (std::string_view(name) == csol::boss_health::Resources[i]) return common / relative;
    const auto primary = server / relative;
    if (fs::is_regular_file(primary)) return primary;
    // Match GoldSrc's real base-game fallback for stock SDK resources.
    return server.parent_path() / "valve" / relative;
}
int Size(const char* name)
{
    if (denied == name) return -1;
    const auto path = Resolve(name);
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) return -1;
    Require(size <= INT_MAX, "disk resource fits engine size");
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
    auto buffer = std::make_unique<unsigned char[]>(static_cast<std::size_t>(*size) + 1);
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "disk resource opens");
    input.read(reinterpret_cast<char*>(buffer.get()), *size);
    Require(input.gcount() == *size && fs::last_write_time(path) == stamp, "disk read complete and stable");
    buffer[*size] = 0;
    auto* result = buffer.get();
    Engine::self->buffers.emplace(result, std::move(buffer));
    return result;
}
int Lookup(const char* name)
{
    const auto found = Engine::self->models.find(name);
    return found == Engine::self->models.end() ? 0 : found->second;
}
int PrecacheModel(const char* name)
{
    if (rejected == name) return invalidIndex;
    const int size = Size(name);
    if (size <= 0) std::cerr << "MISSING_MODEL " << name << '\n';
    Require(size > 0, "engine never receives missing model");
    return Engine::Model(name);
}
void Run(int mode)
{
    auto engine = std::make_unique<Engine>();
    engine->files.clear();
    engine->globals.mapname = Engine::String("zs_deadend");
    g_engfuncs.pfnLoadFileForMe = Load;
    g_engfuncs.pfnGetFileSize = Size;
    g_engfuncs.pfnPrecacheGeneric = [](const char* path) { ++generic[path]; return int(generic.size()); };
    g_engfuncs.pfnPrecacheModel = PrecacheModel;
    g_engfuncs.pfnModelIndex = Lookup; // A lookup never registers a missing model.
    engine->Prepare("2");
    LinkUserMessages();
    g_pGameRules = InstallGameRules();
    Require(g_pGameRules && !CSOL_RulesRejected(), "real scenario rules registered");
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    {
        CSOLScenarioSdkScope scope;
        runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
        Require(scope.Current() && runtime, "registered runtime owns real session");
    }
    auto session = runtime->session;
    Require(bool(session), "registered runtime has a session");
    const bool precached = session->Precache("zs_deadend", 1, CSOLScenarioResourceScope::Combat);
    if (!precached) std::cerr << "SESSION " << session->Error().path << ' ' << session->Error().detail.message << '\n';
    Require(precached, "actual Combat session precaches before skill host");
    Require(session && session->Map().Tables(), "actual map tables loaded");
    // Isolate exactly the host registration: weapon precache may independently
    // register HUD640hud12, which must not conceal a skill-table mistake here.
    engine->models.clear(); engine->events.clear(); generic.clear();
    engine->models.emplace("preexisting-model-sentinel", 1);
    if (mode >= 0 && mode < 5) denied = WorldSprites[mode];
    if (mode >= 5) { rejected = WorldSprites[(mode - 5) % 5]; invalidIndex = mode < 10 ? 0 : 65536; }
    csol::scenario::ConfigError error;
    const bool ready = csol::scenario::skills::PrecacheSkillHost(session, error);
    if (mode < 0)
    {
        Expect(ready && csol::scenario::skills::SkillHostReady(*session), "actual skill host becomes ready");
        Expect(Lookup("preexisting-model-sentinel") == 1, "existing engine model index is stable");
        for (std::size_t i = 0; i < ExistingModels.size(); ++i)
            Expect(Lookup(ExistingModels[i]) == int(i) + 2, "old skill model index and registration order remain stable");
        for (std::size_t i = 0; i < WorldSprites.size(); ++i)
        {
            Expect(Lookup(WorldSprites[i]) == int(ExistingModels.size() + i + 2), "world sprite receives appended model index");
            Expect(!generic.contains(WorldSprites[i]), "world sprite no longer uses only generic download");
        }
        Expect(generic["sprites/csol/640hud12.spr"] == 1, "HUD sprite retains generic download");
        Expect(Lookup("sprites/csol/640hud12.spr") == 0, "HUD sprite does not consume skill model index");
        Expect(Lookup("sprites/unregistered-negative-control.spr") == 0, "lookup miss never registers a model");
    }
    else
    {
        Expect(!ready && !csol::scenario::skills::SkillHostReady(*session), "incomplete model registration cannot publish a ready skill host");
        const auto& path = mode < 5 ? denied : rejected;
        Expect(error.message.find(path) != std::string::npos, "registration failure identifies exact world sprite");
        Expect(Lookup(path.c_str()) == 0, "failed world sprite has no model index");
        if (mode < 5) Expect(engine->models.size() == 1 && generic.empty(), "missing resource fails before any registration");
    }
    Expect(engine->buffers.empty(), "all actual loaded file buffers released");
    for (const auto& [path, index] : engine->models) std::cout << "MODEL\t" << index << '\t' << path << '\n';
    for (const auto& [path, count] : generic) std::cout << "GENERIC\t" << count << '\t' << path << '\n';
    engine->Clear(); session.reset(); runtime.reset();
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(argc == 4, "case/server/common arguments required");
        server = argv[2]; common = argv[3]; Run(std::stoi(argv[1]));
    }
    catch (const std::exception& error) { ++failed; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "skill-resources-sdk assertions=" << assertions << " failures=" << failed
        << " SDK checks=" << sdk_test::checks << " SDK failures=" << sdk_test::failures << '\n';
    return failed || sdk_test::failures ? 1 : 0;
}
