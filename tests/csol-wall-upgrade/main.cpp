// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../../native/csol/server/scenario/core/scenario_wall_upgrade.h"
#include "../../native/csol/scenario/core/wall_damage.hpp"
#include "../../native/csol/scenario/core/difficulty.hpp"
#include <functional>
#include <iostream>
#include <string>
#include <vector>

bool g_bAllowedCSBot{};
// This component links the actual wall-damage TU. Its separate CSV loader is
// outside this test and must never be reached by a wall upgrade operation.
bool csol::scenario::DifficultyResources::Parameter(int, std::uint32_t, float&, ConfigError&) const
{
    std::abort();
}

namespace
{
int checks{}, failures{};
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
using Upgrade = CCSOLScenarioWallUpgrade;
using Result = Upgrade::Result;
struct Services final : CCSOLWallUpgradeServices
{
    std::shared_ptr<CSOLScenarioEntityAccess> access = std::make_shared<CSOLScenarioEntityAccess>();
    edict_t player{};
    int money{150000}, reads{}, errors{}, level{1}, records{}, notices{}, indicators{}, removals{};
    std::int16_t wins{};
    std::uint8_t participation{};
    bool active{true};
    std::vector<std::string> calls;
    std::function<void(std::string_view)> callback;
    std::string failAt;
    Services() { player.serialnumber = 1; }
    EHANDLE Player() { return EHANDLE(&player); }
    bool Step(const char* name)
    {
        calls.emplace_back(name);
        const auto next = callback;
        if (next) next(name);
        return failAt != name;
    }
    bool Current() const override { return active; }
    bool PlayerCurrent(const EHANDLE& handle) const override
    { return active && handle.Get() == &player; }
    bool ReadAccount(const EHANDLE&, int& account) override
    { ++reads; const bool ok = Step(reads % 2 ? "read1" : "read2"); account = money; return ok; }
    bool ReadEligibility(const EHANDLE&, std::int16_t& ctWins, std::uint8_t& phase) override
    { ctWins = wins; phase = participation; return Step("eligibility"); }
    bool WriteAccount(const EHANDLE&, int account) override
    { money = account; return Step("debit"); }
    bool LevelChanged(std::int8_t value) override { level = value; return Step("level"); }
    bool PublishState(bool enabled, std::int8_t value) override
    { Check(enabled && value == level, "publish follows real damage-level update"); return Step("state"); }
    bool RecordServiceEvent(const EHANDLE&, int event) override
    { Check(event == 51, "opaque original external event51"); ++records; return Step("service"); }
    bool PublishUpgrader(const EHANDLE&) override { ++notices; return Step("upgrader"); }
    bool PublishDenied(const EHANDLE&, const char* token) override
    { Check(std::string_view(token) == "#CSO_ZBS_CantUpgradeWall", "original denial key"); return Step("denied"); }
    bool ShowIndicator(const EHANDLE&) override { ++indicators; return Step("indicator"); }
    bool RemoveIndicator(const EHANDLE&) override { ++removals; return Step("remove"); }
    void Failed(const char*) override { ++errors; }
};
struct Fixture
{
    std::shared_ptr<Services> services = std::make_shared<Services>();
    std::shared_ptr<Upgrade> state = std::make_shared<Upgrade>(true, services, services->access);
    Result Use() { return state->Use(services->Player()); }
};

void UpgradeCases()
{
    Fixture f;
    Check(f.state->Level() == 1, "constructor level1");
    Check(f.state->ShowIndicator({}) && f.services->indicators == 1, "below max indicator");
    f.services->calls.clear();
    for (int level = 2; level <= 28; ++level)
    {
        Check(f.Use() == Result::Upgraded, "accepted purchase");
        Check(f.state->Level() == level && f.services->money == 150000 - (level - 1) * 5000,
            "27 charged upgrades with authoritative level");
        float damage{};
        Check(csol::scenario::ResolveMonsterWallDamage(100, 3, f.services->level, damage),
            "existing real wall damage consumer accepts new level");
        Check(std::fabs(damage - (300.0f - (level - 1) * 10.0f)) < .0001f,
            "upgrade changes real monster damage, not wall health");
    }
    const std::vector<std::string> expected{"read1", "eligibility", "read2", "debit", "level", "state", "service", "upgrader"};
    Check(f.services->calls.size() == expected.size() * 27, "one original event sequence per debit");
    for (std::size_t i = 0; i < f.services->calls.size(); ++i)
        Check(f.services->calls[i] == expected[i % expected.size()], "original operation order");
    const auto count = f.services->calls.size();
    Check(f.Use() == Result::Ignored && f.services->calls.size() == count, "max returns before account read");
    Check(f.state->ShowIndicator({}) && f.services->indicators == 1, "max stops new indicators");
    Check(f.state->RemoveIndicator({}) && f.services->removals == 1, "max use can remove attachment");
    Check(f.services->records == 27 && f.services->notices == 27, "service submission and UI distinct");
}

void GateCases()
{
    {
        Fixture f; f.services->money = 4999; f.services->wins = 1;
        Check(f.Use() == Result::Ignored && f.services->calls == std::vector<std::string>{"read1"},
            "insufficient account before participation without notice");
    }
    for (int wins : {-1, 0, 1, 20}) for (int phase : {0, 1, 2, 3, 255})
    {
        Fixture f; f.services->wins = wins; f.services->participation = phase;
        const bool allowed = wins == 0 || phase == 2;
        Check(f.Use() == (allowed ? Result::Upgraded : Result::Denied), "CT score/participation gate");
        Check(f.services->money == (allowed ? 145000 : 150000), "denial preserves money");
        Check(f.state->Level() == (allowed ? 2 : 1), "denial preserves level");
    }
    // These table entries are copied from the saved original11382E38 bytes,
    // independently of the implementation's switch cases.
    const unsigned char originalMap[]{0,0,0,0,1,0,1,1,1,0};
    for (int subtype = -2; subtype <= 12; ++subtype) for (int phase = 0; phase < 256; ++phase)
    {
        const bool direct = subtype >= 1 && subtype <= 10 && originalMap[subtype - 1] == 0;
        const auto expected = direct ? 2 : phase == 0 ? 1 : phase == 1 ? 2 : phase;
        Check(csol::wall_upgrade::BeginRound(phase, subtype) == expected, "original participation table");
    }
}

void CancellationCases()
{
    const char* stages[]{"read1", "eligibility", "read2", "debit", "level", "state", "service", "upgrader"};
    for (const auto* stage : stages) for (int method = 0; method < 3; ++method)
    {
        Fixture f;
        f.services->callback = [&](std::string_view at) {
            if (at != stage) return;
            if (method == 0) f.state->Invalidate();
            else if (method == 1) f.services->access->Revoke();
            else ++f.services->player.serialnumber;
        };
        Check(f.Use() == Result::Cancelled, "retired map/player callback cancels remaining operations");
        Check(f.services->calls.back() == stage, "no service after cancellation boundary");
        Check(f.services->errors == 0, "cancellation does not report unsupported implementation");
    }
    for (const auto* stage : stages)
    {
        Fixture f; f.services->failAt = stage;
        Check(f.Use() == Result::Failed && !f.state->Current(), "provider failure retires controller");
        Check(f.services->errors == 1 && f.services->calls.back() == stage, "one explicit failure and no suffix");
        Check(f.Use() == Result::Cancelled, "failure cannot debit again");
    }
    for (const auto* stage : stages)
    {
        Fixture f; int nested{};
        f.services->callback = [&](std::string_view at) {
            if (at == stage) { ++nested; Check(f.Use() == Result::Ignored, "nested Use suppressed"); }
        };
        Check(f.Use() == Result::Upgraded && nested == 1 && f.services->money == 145000,
            "reentrant callback still charges once");
    }
    {
        Fixture f;
        f.services->callback = [&](std::string_view at) { if (at == "read2") f.services->money = 20000; };
        Check(f.Use() == Result::Upgraded && f.services->money == 15000, "second original account read consumed");
    }
    {
        Fixture f; auto* state = f.state.get();
        std::weak_ptr<Upgrade> weak = f.state;
        f.services->callback = [&](std::string_view at) { if (at == "debit") f.state.reset(); };
        Check(state->Use(f.services->Player()) == Result::Upgraded, "operation retains controller through owner release");
        Check(weak.expired(), "operation releases retained controller on return");
    }
    for (bool remove : {false, true})
    {
        Fixture f; f.services->failAt = remove ? "remove" : "indicator";
        Check(!(remove ? f.state->RemoveIndicator({}) : f.state->ShowIndicator({})) &&
            !f.state->Current() && f.services->errors == 1, "missing attachment service explicitly fails");
    }
}
}

int main()
{
    UpgradeCases(); GateCases(); CancellationCases();
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
