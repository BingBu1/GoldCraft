// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingKickFixtureMain
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/csol/server/custom/gameplay_settings.h"
#include "../../native/csol/scenario/custom/settings.hpp"
#include <filesystem>
#include <fstream>

namespace refund_sdk
{
using namespace kick_test;
namespace custom = csol::scenario::custom;
int randoms{};
float rewardFirst{}, rewardSecond{};
void Settings()
{
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Set(custom::Feature::DamageRefundCap, true, error) &&
        draft.Set(custom::Setting::DamageRefundMaximumPerMonster, 100, error) && draft.Finish(snapshot, error),
        "real grouped JSON config enables per-pair cap100");
    const auto path = std::filesystem::current_path() / "cstrike/addons/goldcraft/config/gameplay.json";
    Require(!std::filesystem::exists(path), "each SDK run owns a fresh game directory");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary); stream << custom::Serialize(*snapshot);
    Require(bool(stream), "actual settings input saved before world initialization");
}
custom::RefundPair Pair(Case& c, CCSOLScenarioMonster& monster)
{
    const auto profile = c.f.players->Find(EHANDLE(&c.player));
    Require(bool(profile), "actual runtime registration exists");
    const auto player = c.f.session->DamageRefundPlayer(static_cast<unsigned>(c.player.entindex()), profile);
    Require(bool(player), "map ledger resolves actual runtime registration identity");
    return {monster.Slot(), monster.Pool()->Lease(monster), *player};
}
void MoneyRandom(Case& c)
{
    randoms = 0;
    csol::scenario::MonsterHitRewardSettings settings;
    csol::scenario::MonsterHitRewardArguments arguments;
    csol::scenario::ConfigError error;
    Require(csol::scenario::ReadMonsterHitRewardSettings(c.f.session->Difficulty(), settings, error) &&
        csol::scenario::PrepareMonsterHitReward(settings, 10, arguments), "actual reward RNG arguments resolve");
    rewardFirst = arguments.randomFirst; rewardSecond = arguments.randomSecond;
    g_engfuncs.pfnRandomFloat = [](float first, float second)
    {
        if (first == rewardFirst && second == rewardSecond) { ++randoms; return 80.0f; }
        return first + (second - first) * .5f;
    };
}
void Flow()
{
    Case c([](std::map<std::string, std::string>& files)
    {
        // The kick fixture never needed a gun item directory. Supply the same
        // explicitly controlled AK row as the ordinary damage SDK fixture.
        files.at("resource/item.csv") += "1036,AK47,AK47,Equipment,2,28\r\n";
    });
    const auto weapons = c.f.session->Weapons();
    const auto attributes = weapons->CreateAttributes(WEAPON_AK47, 0);
    Require(attributes && weapons->SetAttributeKey(*c.weapon, *attributes),
        "real attribute directory registers the controlled AK instance");
    auto& monster = c.Target(Vector(35, 0, 28));
    const auto cap = c.f.session->DamageRefunds();
    Require(bool(cap), "real Precache pins the enabled map policy");
    const auto pair = Pair(c, monster);
    Require(cap->Remaining(pair) == 100, "real factory lease/player registration selects new budget");
    c.player.m_iAccount = 0;
    const auto upgrades = c.f.session->Players()->Upgrades(EHANDLE(&c.player));
    Require(upgrades && upgrades->State(), "actual upgrade progress is observable");
    auto experience = [&] { const auto* state = upgrades->State(); return state->level * 100 + state->experience; };
    MoneyRandom(c);
    for (int hit = 0; hit < 3; ++hit)
    {
        const auto before = monster.pev->health;
        const int beforeExperience = experience();
        const auto applied = monster.TakeDamage(c.player.pev, c.player.pev, 10, DMG_BULLET | DMG_NEVERGIB);
        std::cout << "hit=" << hit << " applied=" << applied << " money=" << c.player.m_iAccount
            << " health=" << monster.pev->health << " error=" << monster.Behavior()->Failure() << '\n';
        Expect(monster.pev->health < before, "actual ordinary TakeDamage continues after the original reward stage");
        Expect(c.player.m_iAccount == (hit == 0 ? 80 : 100), "actual reward AddAccount obeys this pair's cumulative cap");
        Expect(randoms == hit + 1, "full budget still performs the original reward draw exactly once per hit");
        Expect(experience() > beforeExperience, "actual upgrade XP still increases after the damage money cap is full");
        Expect(!CSOL_RulesRejected() && monster.Behavior()->Failure().empty(), "cap exhaustion does not reject ordinary combat");
    }
    Expect(cap->Remaining(pair) == 0, "third hit retains exhausted budget");
    // The fixture type's authored independent kill bonus is1; the damage
    // money draw is still80. A saturated damage channel must leave it intact.
    monster.pev->health = 1;
    monster.TakeDamage(c.player.pev, c.player.pev, 10, DMG_BULLET | DMG_NEVERGIB);
    Expect(c.player.m_iAccount == 101 && randoms == 4, "real fatal damage preserves independent kill money after cap exhaustion");
    Expect(!CSOL_RulesRejected(), "fatal capped hit completes the native death owner");
}
void Owners()
{
    Case c;
    auto& monster = c.Target(Vector(35, 0, 28));
    const auto cap = c.f.session->DamageRefunds();
    Require(bool(cap), "real map owns refund ledger");
    const auto pair = Pair(c, monster);
    CSOLMonsterSpawnCall call(c.f.session->Map(), c.f.session->Monsters(),
        c.f.runtime->gameplay->Deaths().Requests(), monster, monster.Behavior());
    const EHANDLE player(&c.player);
    MoneyRandom(c); c.player.m_iAccount = 0;
    auto first = c.f.session->HitRewards();
    Require(first->Credit(call, player, player, 10, false, 0, nullptr, nullptr, pair.player) == CSOLMonsterRewardResult::Awarded,
        "real hit rewards grant initial typed credit");
    csol::scenario::ConfigError error;
    auto replacement = CCSOLMonsterHitRewards::Create(c.f.session->Difficulty(), c.f.session->Players(), error, cap);
    Require(bool(replacement), "replacement reward owner shares the map ledger");
    Require(replacement->Credit(call, player, player, 10, false, 0, nullptr, nullptr, pair.player) == CSOLMonsterRewardResult::Awarded,
        "replacement reward owner performs actual draw/XP/account");
    Expect(c.player.m_iAccount == 100 && cap->Remaining(pair) == 0, "reward-owner replacement cannot reset the same lease's budget");
    c.player.pev->health = 0; c.player.pev->deadflag = DEAD_DEAD;
    Require(replacement->Credit(call, player, player, 10, false, 0, nullptr, nullptr, pair.player) == CSOLMonsterRewardResult::Awarded,
        "valid dead player remains eligible for in-flight hit rewards");
    Expect(randoms == 3 && c.player.m_iAccount == 100, "dead owner preserves RNG but cannot refill budget");
    first->Invalidate();
    Expect(first->Credit(call, player, player, 10, false, 0) == CSOLMonsterRewardResult::Cancelled && first->Error().message.empty(),
        "retired reward owner cancels before interpreting absent refund identity");
    const int money = c.player.m_iAccount;
    const bool current = true;
    Require(replacement->CreditEntityHit(player, player,
        [](const csol::scenario::MonsterHitRewardSettings&, void*, std::int32_t& experience, std::int32_t& money)
        { experience = 0; money = 80; return true; }, nullptr,
        [](const void* value) { return *static_cast<const bool*>(value); }, &current) == CSOLMonsterRewardResult::Awarded,
        "map-entity reward retains its explicit non-monster channel");
    Expect(c.player.m_iAccount == money + 80 && cap->Remaining(pair) == 0,
        "CreditEntityHit does not inherit the ordinary-monster cap");
}
void Cancellation()
{
    Case c; auto& monster = c.Target(Vector(35, 0, 28));
    const auto cap = c.f.session->DamageRefunds();
    Require(bool(cap), "map cap exists for callback cancellation");
    const auto pair = Pair(c, monster);
    CSOLMonsterSpawnCall call(c.f.session->Map(), c.f.session->Monsters(),
        c.f.runtime->gameplay->Deaths().Requests(), monster, monster.Behavior());
    const auto rewards = c.f.session->HitRewards();
    const EHANDLE player(&c.player);
    MoneyRandom(c); c.player.m_iAccount = 0;
    bool fired{};
    c.callback = [&](std::string_view event)
    {
        if (event == "message" && c.pending.id == gmsgMoney)
        {
            fired = true;
            Expect(cap->Remaining(pair) == 20, "ledger commits before actual engine Money callback");
            c.f.runtime->RequestClose();
        }
    };
    const auto result = rewards->Credit(call, player, player, 10, false, 0,
        [](const void* value) { return !static_cast<const CCSOLScenarioRuntime*>(value)->Closing(); },
        c.f.runtime.get(), pair.player);
    c.callback = {};
    Expect(fired && result == CSOLMonsterRewardResult::Cancelled && c.player.m_iAccount == 80,
        "map retirement cancels continuation after completed native money credit");
    Expect(cap->Remaining(pair) == 20, "cancelling the hit after Money never refunds paid budget");
}
void Replay()
{
    Case c; auto& monster = c.Target(Vector(35, 0, 28));
    const auto cap = c.f.session->DamageRefunds();
    Require(bool(cap), "map cap exists before replay");
    const auto pair = Pair(c, monster);
    Require(cap->Remaining(pair) == 100 && cap->Commit(pair, 100), "old borrowed monster exhausts its own budget");
    csol::scenario::ConfigError error;
    const auto plan = c.f.session->PrepareReplaySettings(c.f.session->Difficulty().SelectedRow(), error);
    Require(bool(plan), error.message.c_str());
    Require(c.f.runtime->gameplay->BeginRound(0, c.f.engine.globals.time, 0, *c.f.runtime->waveEvents, plan.get()),
        "actual gameplay replay drains monsters and installs new difficulty rewards");
    Expect(c.f.session->DamageRefunds() == cap, "same-map replay retains its pinned policy and ledger owner");
    Expect(cap->Remaining(pair) == 0, "replacing reward services does not clear old lease accounting");
}
void Registration()
{
    Case c; auto& monster = c.Target(Vector(35, 0, 28));
    const auto cap = c.f.session->DamageRefunds();
    const auto pair = Pair(c, monster);
    Require(cap && cap->Remaining(pair) == 100 && cap->Commit(pair, 100), "old registration exhausts budget");
    const auto original = c.f.players->Find(EHANDLE(&c.player));
    const auto life = original->nativeSpawnSerial;
    c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));
    Expect(original->nativeSpawnSerial != life && Pair(c, monster).player.generation == pair.player.generation,
        "native life change retains map registration token");
    // Different registry objects can both number their first registration1.
    // The map-owned weak registration cache must distinguish their objects.
    auto repeated = std::make_shared<CSOLScenarioPlayerRuntimeState>();
    repeated->generation = original->generation;
    const auto newer = c.f.session->DamageRefundPlayer(pair.player.slot, repeated);
    Require(bool(newer), "new registration resolves even when its numeric generation repeats");
    const custom::RefundPair next{pair.monsterSlot, pair.lease, *newer};
    Expect(newer->generation > pair.player.generation && cap->Remaining(next) == 100 && !cap->Remaining(pair),
        "registry replacement has a distinct monotonic map token and retires the old budget identity");
}
}
int main(int argc, char** argv)
{
    try
    {
        sdk_test::Require(argc == 2, "one explicit refund SDK group");
        refund_sdk::Settings();
        const std::string_view group(argv[1]);
        if (group == "flow") refund_sdk::Flow();
        else if (group == "owners") refund_sdk::Owners();
        else if (group == "cancel") refund_sdk::Cancellation();
        else if (group == "replay") refund_sdk::Replay();
        else if (group == "registration") refund_sdk::Registration();
        else throw std::runtime_error("unknown refund SDK group");
    }
    catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
