// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/monsters/golden.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
void Require(bool value, const char* label)
{
    Check(value, label);
    if (!value) throw std::runtime_error(label);
}
constexpr std::string_view Json = R"({"ZBS_GoldenZombie":{
  "monster_info":{"name":"golden","color":[255,190,5],"probability":100,"delay":10.9,
    "health":1000,"armor":40,"speed":200,"attackDamage":70,"attackRange":0,
    "attackInterval":2,"exp":777,"dropItemID":800,"bonusMoney":500},
  "difficulty":[{"level":-1,"person":99},{"level":0,"person":1},
    {"level":3,"probability":20.9,"person":2,"health":200,"armor":-40,"speed":-220},
    {"level":3,"probability":21.9,"person":3,"dropItemID":900,"health":300,"armor":-40,"speed":-220},
    {"level":8,"person":8}]}})";

std::shared_ptr<GoldenSettings> Settings()
{
    auto result = std::make_shared<GoldenSettings>(); ConfigError error;
    Require(ReadGoldenSettings(Json, *result, error), "configured Golden JSON parses");
    return result;
}

struct File
{
    inline static File* current{};
    std::optional<std::string> contents;
    int releases{};
    bool reenter{}, nested{};
    GoldenResources* resource{};
    static unsigned char* Load(const char* path, int* size)
    {
        Require(std::strcmp(path, "maps/ZBS_GoldenZombie_dedi.json") == 0, "dedicated filename is exact");
        if (!current->contents) return nullptr;
        *size = static_cast<int>(current->contents->size());
        auto* data = new unsigned char[*size + 1];
        std::memcpy(data, current->contents->c_str(), *size + 1);
        return data;
    }
    static void Release(void* data)
    {
        delete[] static_cast<unsigned char*>(data); ++current->releases;
        if (current->reenter)
        { ResourceError error; current->nested = current->resource->Load({Load, Release}, error); }
    }
};

void Parser()
{
    auto settings = Settings();
    Check(settings->Select(-1) == nullptr, "negative thresholds are never selected");
    Check(settings->Select(2)->person == 1 && settings->Select(7)->person == 3,
        "greatest <= threshold, with last equal level winning");
    Check(settings->Select(8)->person == 8 && settings->Select(100)->person == 8,
        "top threshold continues above final configured level");
    Check(settings->Select(3)->probability == 21, "difficulty probability truncates float32");
    GoldenSettings precision; ConfigError error;
    Require(ReadGoldenSettings(R"({"ZBS_GoldenZombie":{"monster_info":{"name":"x","color":[0,0,0]},
        "difficulty":[{"level":0,"probability":16777217.75}]}})", precision, error), "float32 probability case parses");
    Check(precision.Select(0)->probability == 16777218, "float32 conversion precedes integer truncation");
    Check(precision.monster.health == 0 && precision.Select(0)->person == 0,
        "absent numeric fields retain original zero defaults");
    for (const auto bad : {R"({"ZBS_GoldenZombie":{"monster_info":[]}})",
        R"({"ZBS_GoldenZombie":{"monster_info":{"name":"x","color":[1,2]}}})",
        R"({"ZBS_GoldenZombie":{"monster_info":{"name":"x","color":[0,0,0]},"difficulty":[{"probability":2147483648}]}})"})
        Check(!ReadGoldenSettings(bad, precision, error) && precision.Select(0)->probability == 16777218,
            "malformed schemas leave the previous settings intact");
    {
        GoldenResources resource; ResourceError resourceError;
        File file{std::string(Json)}; File::current = &file; file.resource = &resource; file.reenter = true;
        Require(resource.Load({File::Load, File::Release}, resourceError), "nonempty resource now loads");
        Check(resource.State() == GoldenConfiguration::Configured && resource.Snapshot() && !resource.EmptyPlan(),
            "configured resource is distinguishable from an empty plan");
        Check(file.releases == 1 && !file.nested, "release happens exactly once and cannot reenter loader");
        Check(!resource.Load({File::Load, File::Release}, resourceError), "configured owner cannot reload in place");
    }
    for (const auto text : {"", "null", "{}", "{", R"({"ZBS_GoldenZombie":{}})"})
    {
        GoldenResources resource; ResourceError resourceError; File file{std::string(text)}; File::current = &file;
        Check(resource.Load({File::Load, File::Release}, resourceError) && resource.EmptyPlan() &&
            !resource.Snapshot() && file.releases == 1, "optional empty and parse-error states remain empty");
    }
    File::current = nullptr;
}

void Parameters()
{
    GoldenParameters parameters; ConfigError error;
    using Packet = std::vector<std::uint8_t>;
    const auto apply = [&](const Packet& packet)
    { return parameters.ApplyPacket(std::as_bytes(std::span(packet)), error); };
    const auto snapshotIs = [&](float probability, float persons, float second)
    {
        const auto snapshot = parameters.Snapshot();
        return snapshot.probability == probability && snapshot.persons == persons &&
            snapshot.secondPlanProbability == second;
    };
    for (const std::uint8_t key : {0, 1, 2, 4, 255})
        Check(parameters.Parameter(key) == 0, "fresh parameter owner returns zero for absent keys");
    Check(snapshotIs(0, 0, 0), "fresh owner does not manufacture Golden adjustments");

    // Fixed little-endian float32 bytes, independent of any production encoder.
    const Packet baseline{
        0, 4,
        1,   0x00, 0x00, 0x48, 0x41, // 12.5
        2,   0x00, 0x00, 0x10, 0xc0, // -2.25
        4,   0x00, 0x00, 0x40, 0x3f, // 0.75
        255, 0x00, 0x00, 0x60, 0xc0, // -3.5
    };
    Require(apply(baseline), "subtype zero accepts byte keys and little-endian float32 values");
    Check(snapshotIs(12.5f, -2.25f, 0.75f), "keys 1/2/4 retain fractions and signs in the snapshot");
    Check(parameters.Parameter(255) == -3.5f && parameters.Parameter(3) == 0,
        "high byte key is retained while unrelated absent key remains zero");
    Require(apply({0, 1, 2, 0x00, 0x00, 0x40, 0x3f}), "next update replaces the parameter table");
    Check(snapshotIs(0, 0.75f, 0) && parameters.Parameter(255) == 0,
        "replacement drops omitted keys instead of accumulating values");
    Require(apply({0, 4,
        1, 0x00, 0x00, 0x60, 0x40, // 3.5
        1, 0x00, 0x00, 0x00, 0xc1, // -8
        4, 0x00, 0x00, 0x00, 0x00, // 0
        4, 0x00, 0x00, 0x80, 0x3f}), "duplicate byte keys parse successfully");
    Check(snapshotIs(3.5f, 0, 0), "first duplicate wins even when its value is zero");

    Require(apply(baseline), "restore known values before ignored and rejected packets");
    const auto baselineRetained = [&]
    { return snapshotIs(12.5f, -2.25f, 0.75f) && parameters.Parameter(255) == -3.5f &&
        parameters.Parameter(3) == 0; };
    for (const Packet& packet : {Packet{1}, Packet{255},
        Packet{1, 255, 4, 0x00, 0x00, 0xc0, 0x7f}})
    {
        Check(apply(packet), "nonzero subtype succeeds without parsing its remaining bytes");
        Check(error.message.empty() && baselineRetained(), "ignored subtype preserves every old parameter");
    }

    struct InvalidPacket { const char* label; Packet bytes; };
    const InvalidPacket invalid[]{
        {"missing subtype", {}},
        {"missing count", {0}},
        {"missing entry", {0, 1}},
        {"missing float", {0, 1, 1}},
        {"truncated float", {0, 1, 1, 0x00, 0x00, 0x80}},
        {"count exceeds body", {0, 2, 1, 0x00, 0x00, 0x80, 0x3f}},
        {"empty update has trailing data", {0, 0, 1}},
        {"complete entry has trailing data", {0, 1, 1, 0x00, 0x00, 0x80, 0x3f, 0}},
        {"NaN after valid entry", {0, 2, 1, 0x00, 0x00, 0x80, 0x3f,
            4, 0x00, 0x00, 0xc0, 0x7f}},
        {"positive infinity after valid entry", {0, 2, 1, 0x00, 0x00, 0x80, 0x3f,
            4, 0x00, 0x00, 0x80, 0x7f}},
        {"negative infinity after valid entry", {0, 2, 1, 0x00, 0x00, 0x80, 0x3f,
            4, 0x00, 0x00, 0x80, 0xff}},
        {"NaN in duplicate entry", {0, 2, 1, 0x00, 0x00, 0x80, 0x3f,
            1, 0x00, 0x00, 0xc0, 0x7f}},
    };
    for (const auto& packet : invalid)
    {
        Check(!apply(packet.bytes), packet.label);
        Check(!error.message.empty(), "rejected parameter packet explains the failure");
        const auto label = std::string(packet.label) + " preserves the complete previous table";
        Check(baselineRetained(), label.c_str());
    }

    Packet highCount{0, 128};
    for (unsigned key = 128; key != 256; ++key)
        highCount.insert(highCount.end(), {static_cast<std::uint8_t>(key), 0x00, 0x00, 0x80, 0x3f});
    Require(apply(highCount), "BYTE count 128 accepts all complete entries after a rejected update");
    Check(error.message.empty() && parameters.Parameter(128) == 1 && parameters.Parameter(255) == 1 &&
        snapshotIs(0, 0, 0), "unsigned count and keys reach the final entry and replace old values");
    Require(apply({0, 0}), "zero-count update succeeds");
    Check(parameters.Parameter(128) == 0 && parameters.Parameter(255) == 0 && snapshotIs(0, 0, 0),
        "zero-count update clears the whole table");
}

MapTables Tables()
{
    MapTables tables;
    MonsterType ordinary; ordinary.id = 1; ordinary.appearance = "zombie";
    MonsterType boss = ordinary; boss.id = 2; boss.priority = 2;
    MonsterType excluded = ordinary; excluded.id = 3; excluded.appearance = "zombieclimber";
    tables.monsterTypes.rows = {ordinary, boss, excluded};
    tables.monsterTypes.byId = {{1,0},{2,1},{3,2}};
    for (int i = 0; i < 3; ++i)
    {
        WaveRow row; row.waveId = i + 10; row.spawnPoint = 1; row.types = {i + 1};
        tables.waves.groups[i + 10][1] = {tables.waves.rows.size()}; tables.waves.rows.push_back(row);
    }
    tables.rounds.rows = {{0,99,{10,10,11,12,77}},{1,1,{10,65546}}};
    return tables;
}

void Candidates()
{
    auto tables = Tables(); std::vector<GoldenCandidate> candidates; ConfigError error;
    Require(CollectGoldenCandidates(tables, 0, {}, candidates, error), "normal rounds collect");
    Check(candidates == std::vector<GoldenCandidate>{{0,10},{0,10},{1,10},{1,10}},
        "source duplicates and low16 wave aliases retained, unavailable/Boss/excluded types skipped");
    Require(CollectGoldenCandidates(tables, 1, {}, candidates, error), "future-only round collection");
    Check(candidates.size() == 2 && candidates.front().round == 1, "round vector position beats CSV round label");
    Require(CollectGoldenCandidates(tables, -1, {}, candidates, error), "negative current round accepts empty");
    Check(candidates.empty(), "negative signed round fails original unsigned range gate");
    for (const auto name : {"zombieclimber", "flydrone", "elite_forceC", "elite_forceT"})
    { MonsterType type; type.appearance = name; Check(!GoldenCandidateType(type), "four original appearances excluded"); }
    MonsterType negative; negative.priority = -1;
    Check(GoldenCandidateType(negative), "candidate predicate rejects priority>0, not every nonzero priority");
    tables.scenarioType = 1; tables.rounds.rows = {{0,0,{5}},{1,1,{5}}};
    tables.triggerRules.emplace(); TriggerRule rule; rule.group = 5; rule.command = 'w'; rule.waves = {10,10};
    TriggerRule other = rule; other.command = 'g';
    tables.triggerRules->rows = {other, rule}; tables.triggerRules->groups[5] = {0,1};
    const GoldenWaveStatus ended[]{ {10, -1, false} };
    Require(CollectGoldenCandidates(tables, 0, ended, candidates, error), "trigger rounds collect");
    Check(candidates == std::vector<GoldenCandidate>{{1,10},{1,10}},
        "only current round is suppressed by trigger cooldown -1 and no pending runtime row");
    const GoldenWaveStatus running[]{ {10, -1, true} };
    Require(CollectGoldenCandidates(tables, 0, running, candidates, error), "runtime with a pending row clock");
    Check(candidates.size() == 4, "pending row clock restores current trigger candidates");
    const GoldenWaveStatus absent[]{ {10, std::nullopt, false} };
    Require(CollectGoldenCandidates(tables, 0, absent, candidates, error), "explicit absent same-ID trigger record");
    Check(candidates.size() == 4, "missing trigger record imposes no exhausted-wave filter");
    Check(!CollectGoldenCandidates(tables, 0, {}, candidates, error) && candidates.size() == 4,
        "unknown host snapshot is a failure, preserving old result");
}

void Plans()
{
    const GoldenCandidate one[]{ {0,10} }; ConfigError error; GoldenGateResult gate;
    auto settings = Settings(); settings->monster.probability = 100; settings->difficulties = {{0,0,1}};
    GoldenManager manager;
    Require(manager.Install(settings, 0, {}, 1, 5489, error), "fresh manager installs");
    Require(manager.Rebuild(one, error), "first plan builds");
    Check(manager.Plans().size() == 1 && manager.CompletedPlans() == 0, "planned count is independent from completion count");
    Require(manager.Gate(0, 10, 10.0f, one, gate, error), "exact integer delay gate");
    Check(!gate.allowed && gate.touchWaveRuntime && manager.DeferredPlans() == 1 && manager.Plans().empty(),
        "strict delay equality touches runtime then defers plan");
    Require(manager.Gate(0, 10, 10.01f, one, gate, error), "fractional original delay truncated to int");
    Check(gate.allowed && gate.touchWaveRuntime && manager.DeferredPlans() == 0,
        "elapsed greater than truncated delay recovers one deferred plan");
    Require(manager.Gate(0, -1, 50.0f, one, gate, error), "inactive wave gate");
    Check(!gate.allowed && !gate.touchWaveRuntime, "wave -1 skips all plan work");
    const GoldenCandidate next[]{ {2,20} };
    Require(manager.EndWave(1, 10, next, error), "wave end replaces first matured matching plan");
    Check(manager.Plans().size() == 1 && manager.Plans()[0].destination == next[0], "replacement uses current candidate snapshot");
    Require(manager.EndWave(1, 20, {}, error), "future plan not matched early");
    Check(manager.Plans().size() == 1, "future plan survives earlier round end");
    Require(manager.EndWave(2, 20, {}, error), "matured plan removed despite empty replacement");
    Check(manager.Plans().empty(), "empty choice appends nothing");
    Check(!manager.Gate(0, 10, std::numeric_limits<float>::quiet_NaN(), one, gate, error), "invalid clock is rejected");

    for (const float probability : {81.0f, 81.01f})
    {
        auto chanceSettings = std::make_shared<GoldenSettings>(*settings);
        chanceSettings->monster.probability = probability;
        GoldenManager chance;
        Require(chance.Install(chanceSettings, 0, {}, 2, 5489, error) && chance.Rebuild(one, error), "known MT seed chance");
        // MT19937 first word3499211612; multiply-high to range0..99 yields81.
        Check(chance.Plans().size() == (probability > 81 ? 1u : 0u), "strict chance>roll, not >= or modulo");
    }
    GoldenManager additional;
    const GoldenCandidate two[]{ {0,10}, {0,20} };
    Require(additional.Install(settings, 0, {0,1,100}, 3, 5489, error) && additional.Rebuild(two, error), "second-plan branch");
    Check(additional.Plans().size() == 2 && additional.Plans()[0].destination != additional.Plans()[1].destination &&
        additional.Plans()[0].remaining == 2, "extra person increases plan count, second plan must be distinct");
    GoldenManager duplicate;
    Require(duplicate.Install(settings, 0, {0,0,100}, 4, 5489, error) && duplicate.Rebuild(one, error), "bounded duplicate attempts");
    Check(duplicate.Plans().size() == 1, "second chance cannot append duplicate same-round wave");
    GoldenManager zero;
    Require(zero.Install(settings, 0, {0,-1.9f,100}, 5, 5489, error) && zero.Rebuild(two, error), "truncated extra cancels count");
    Check(zero.Plans().empty(), "zero adjusted count yields no plan");
    auto noPerson = std::make_shared<GoldenSettings>(*settings); noPerson->difficulties[0].person = 0;
    GoldenManager unavailable;
    Require(unavailable.Install(noPerson, 0, {0,5,100}, 6, 5489, error) && unavailable.Rebuild(two, error), "person-zero threshold");
    Check(unavailable.Plans().empty(), "extra persons cannot bypass original base-person zero gate");

    GoldenManager emptyRetry;
    Require(emptyRetry.Install(settings, 0, {}, 7, 5489, error) && emptyRetry.Rebuild(one, error), "deferred-empty fixture");
    Require(emptyRetry.Gate(0,10,0,one,gate,error) && emptyRetry.Gate(0,10,11,{},gate,error), "empty delayed candidate retry");
    Check(emptyRetry.DeferredPlans() == 0 && emptyRetry.Plans().empty(), "failed choice still consumes deferred request");
}

void Members()
{
    auto settings = Settings(); ConfigError error; GoldenManager manager;
    const GoldenCandidate one[]{ {0,10} };
    Require(manager.Install(settings, 3, {}, 42, 5489, error) && manager.Rebuild(one, error), "member fixture installs");
    auto original = std::make_shared<MonsterType>(); original->appearance = "zombie"; original->health = 70;
    original->experience = 90; original->dropItems = {5,6}; original->skill = 12; original->champion = true;
    original->column18 = true; original->column22 = 7; original->color = {1,2,3};
    const MonsterAttributes base{70,8,9,10,.5f,150,20};
    std::optional<GoldenReplacement> replacement;
    const GoldenEntityKey first{42,100,50}, second{42,101,51}, reused{42,999,50};
    Require(manager.Convert(first,0,10,original,base,70,11,25,replacement,error), "first live member conversion");
    Require(replacement.has_value(), "conversion produced a replacement");
    Check(replacement->type != original && original->color[0] == 1 && replacement->type->color[0] == 255,
        "color clone preserves immutable original");
    Check(replacement->type->health == 70 && replacement->type->experience == 90 &&
        replacement->type->dropItems == original->dropItems && replacement->type->champion &&
        replacement->type->column18 && replacement->type->column22 == 7,
        "type clone retains rewards, base attributes and unrelated extension fields");
    Check(replacement->attributes.health == 1300 && replacement->attributes.armor == 8 &&
        replacement->attributes.speed == 150 && replacement->attributes.attackDamage == 70 &&
        replacement->attributes.attackRange == 10 && replacement->attributes.attackInterval == 2 &&
        replacement->attributes.bonusMoney == 500 && replacement->overrideHealth && !replacement->overrideArmor,
        "only positive combined/runtime attributes override");
    Check(replacement->runtimeSkill == 11 && replacement->renderUntil == 1024 && replacement->renderFx == 19 &&
        replacement->renderAmount == 25, "original skill exception and glow deadline");
    Check(manager.MemberCount() == 1 && manager.CompletedPlans() == 0 && manager.Plans()[0].remaining == 2,
        "one member does not mean one completed plan");
    Check(!manager.Contains(reused) && !manager.Hit(reused,123) && !manager.BeginDeath(reused),
        "reused entity index cannot access an old lease");
    Check(!manager.Convert(reused,0,10,original,base,70,0,25,replacement,error), "unfinished old member prevents slot reuse");
    Check(manager.Hit(first,123456) && manager.Hit(first,123456) && manager.Hit(first,789), "contributor identities accepted once");
    Require(manager.EarlyReclaim(first,one,error), "early reclaim before common death");
    Check(manager.CompletedPlans() == -1 && manager.Contains(first), "early reclaim decrements without clamp or erase");
    Require(manager.EarlyReclaim(first,one,error), "duplicate reclaim is idempotent");
    Check(manager.CompletedPlans() == -1, "repeated callback cannot decrement again");
    const auto death = manager.BeginDeath(first);
    Require(death.has_value(), "death tail captures original even without attacker argument");
    Check(death->original == original && death->contributors == std::vector<std::int32_t>{123456,789},
        "death record restores original type and retains unique external identities");
    Check(!manager.BeginDeath(first) && !manager.Hit(first,555) && manager.Contains(first),
        "death dispatch is once-only while member remains until restoration");
    Require(manager.FinishDeath(first), "remove only after host restored original type");
    Check(!manager.FinishDeath(first) && !manager.Contains(first) && manager.CompletedPlans() == -1,
        "ordinary death removal does not decrement or rebuild");
    Require(manager.Convert(reused,0,10,original,base,70,12,30,replacement,error), "freed index can hold a new lease");
    Require(replacement.has_value(), "new lease converted");
    Check(replacement->runtimeSkill == 0, "other runtime skills clear without mutating type skill");
    auto negative = std::make_shared<MonsterType>(*original); negative->priority = -1;
    Require(manager.Convert(second,0,10,negative,base,70,13,30,replacement,error), "negative priority is valid nonreplacement result");
    Check(!replacement && !manager.Contains(second), "actual conversion requires exactly priority zero");
    Require(manager.Convert(second,0,10,original,base,0,13,30,replacement,error), "dead candidate skips conversion despite positive slot base health");
    Check(!replacement, "dead monster does not consume remaining plan");

    auto single = std::make_shared<GoldenSettings>(*settings); single->difficulties = {{0,0,1}};
    GoldenManager completion;
    Require(completion.Install(single,0,{},43,5489,error) && completion.Rebuild(one,error), "single plan fixture");
    Require(completion.Convert({43,1,50},0,10,original,base,70,13,0,replacement,error), "last plan member");
    Check(completion.CompletedPlans() == 1 && completion.Plans().empty() && completion.MemberCount() == 1,
        "last successful conversion consumes plan and increments independent counter");
    Require(completion.Rebuild(one,error), "completed count controls replan");
    Check(completion.Plans().empty(), "completed first plan suppresses ordinary first-chance replan");
}
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "one bounded Golden test group required");
        const std::string_view group = argv[1];
        if (group == "--parser") Parser(); else if (group == "--parameters") Parameters();
        else if (group == "--candidates") Candidates();
        else if (group == "--plans") Plans(); else if (group == "--members") Members();
        else throw std::runtime_error("unknown Golden test group");
    }
    catch (const std::exception& exception) { ++failures; std::cerr << exception.what() << '\n'; }
    std::cout << "Golden " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
