// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/server/custom/damage_refund_account.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace csol::scenario::custom;
namespace
{
int checks{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) throw std::runtime_error(label);
}
bool Current(const void* value) { return *static_cast<const bool*>(value); }
void Ledger()
{
    RefundCap cap(1000);
    for (unsigned monster = 0; monster < RefundCap::MonsterCount; ++monster)
        for (unsigned player = 1; player < RefundCap::PlayerCount; ++player)
        {
            const RefundPair pair{monster, monster + 1, {player, player}};
            Check(cap.Remaining(pair) == 1000, "every player/monster pair starts independently");
            Check(cap.Commit(pair, 999) && cap.Remaining(pair) == 1, "committed credits accumulate");
            Check(!cap.Commit(pair, 2) && cap.Remaining(pair) == 1, "over-budget commits cannot alter state");
            Check(cap.Commit(pair, 1) && cap.Remaining(pair) == 0, "the last unit can be credited exactly");
        }
    const RefundPair original{0, 1, {1, 1}}, reconnect{0, 1, {1, 2}}, reborrow{0, 100, {1, 2}};
    Check(cap.Remaining(original) == 0, "ordinary life/character/reward-owner changes have no reset operation");
    Check(cap.Remaining(reconnect) == 1000, "new registration in the same slot gets its own budget");
    Check(!cap.Remaining(original) && !cap.Commit(original, 0), "old registration cannot reset a replacement");
    Check(cap.Commit(reconnect, 80), "replacement registration is current");
    Check(cap.Remaining(reborrow) == 1000, "reborrow has a new monster budget");
    Check(!cap.Remaining(reconnect) && !cap.Commit(reconnect, 0), "old lease cannot reset a replacement");
    Check(!cap.Remaining({45, 1, {1, 1}}) && !cap.Remaining({0, 0, {1, 1}}) &&
        !cap.Remaining({0, 1, {0, 1}}) && !cap.Remaining({0, 1, {33, 1}}) &&
        !cap.Remaining({0, 1, {1, 0}}), "invalid identities never index the ledger");
    Check(!cap.Commit(reborrow, -1), "negative commits never restore budget");
    cap.Invalidate();
    Check(!cap.Remaining(reborrow) && !cap.Commit(reborrow, 0), "retired maps cannot spend or prepare");
    RefundCap zero(0), bad(-1), tooLarge(32001);
    Check(zero.Remaining(original) == 0 && zero.Commit(original, 0), "zero is an intentional valid cap");
    Check(!bad.Remaining(original) && !tooLarge.Remaining(original), "invalid policies are not unlimited fallbacks");
}
void Transactions()
{
    const RefundPair pair{0, 1, {1, 1}};
    bool live = true;
    int actor{}, other{};
    RefundCap cap(100);
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope account(&actor);
        int amount = 1000;
        Check(RefundAccountScope::Prepare(&actor, amount) && amount == 100, "cap sees the final post-hook amount");
        RefundAccountScope::Commit(&actor, 31990, 32000);
        Check(cap.Remaining(pair) == 90, "native wallet clamping spends only the actual credited ten");
        RefundAccountScope::Commit(&actor, 0, 100);
        Check(cap.Remaining(pair) == 90, "duplicate commit cannot count twice");
        amount = 1;
        Check(!RefundAccountScope::Prepare(&actor, amount), "repeated callOriginal is not a second award");
    }
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope cancelled(&actor);
    }
    Check(cap.Remaining(pair) == 90, "a superceded original does not consume budget");
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope account(&actor);
        int amount = 20;
        Check(!RefundAccountScope::Prepare(&other, amount), "prehook pthis redirection is refused");
        Check(!RefundAccountScope::Prepare(&actor, amount), "redirected tag cannot be retried as ordinary money");
    }
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope account(&actor);
        live = false;
        int amount = 20;
        Check(!RefundAccountScope::Prepare(&actor, amount), "retirement in prehook prevents mutation");
        live = true;
    }
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope outer(&actor);
        {
            RefundAccountScope nested(&actor);
            int bonus = 4000;
            Check(RefundAccountScope::Prepare(&actor, bonus) && bonus == 4000,
                "nested independent kill bonus does not inherit the refund tag");
            RefundAccountScope::Commit(&actor, 0, 4000);
        }
        int amount = 20;
        Check(RefundAccountScope::Prepare(&actor, amount) && amount == 20, "outer tag returns after nested bonus");
        RefundAccountScope::Commit(&actor, 4000, 4020);
        live = false;
        Check(cap.Remaining(pair) == 70, "post-credit retirement never refunds completed payment");
        live = true;
    }
    {
        RefundAwardScope outerAward(&cap, pair, &actor, Current, &live);
        RefundAccountScope outer(&actor);
        {
            RefundAwardScope innerAward(&cap, pair, &actor, Current, &live);
            RefundAccountScope inner(&actor);
            int amount = 60;
            Check(RefundAccountScope::Prepare(&actor, amount) && amount == 60, "nested typed hit has its own tag");
            RefundAccountScope::Commit(&actor, 0, 60);
        }
        int amount = 60;
        Check(RefundAccountScope::Prepare(&actor, amount) && amount == 10,
            "outer hit rechecks remaining budget after nested typed credit");
        RefundAccountScope::Commit(&actor, 60, 70);
    }
    Check(cap.Remaining(pair) == 0, "nested hit credits sum to exactly the cap");
    for (const int request : {0, -10, std::numeric_limits<int>::min(), 999})
    {
        RefundAwardScope award(&cap, pair, &actor, Current, &live);
        RefundAccountScope account(&actor);
        int amount = request;
        Check(RefundAccountScope::Prepare(&actor, amount) && amount == (request > 0 ? 0 : request),
            "full budget preserves zero/negative calls and only clamps positive money");
        RefundAccountScope::Commit(&actor, -50, 0);
        Check(cap.Remaining(pair) == 0, "normalization is not another positive damage grant");
    }
    {
        RefundAwardScope disabled(nullptr, {}, nullptr, nullptr, nullptr);
        RefundAccountScope ordinary(&other);
        int amount = 50000;
        Check(RefundAccountScope::Prepare(&other, amount) && amount == 50000, "disabled channel leaves ordinary amounts untouched");
    }
    int amount = 1000;
    Check(RefundAccountScope::Prepare(&actor, amount) && amount == 1000, "all stacks restore after synchronous scopes");
}
}
int main()
{
    try { Ledger(); Transactions(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    std::cout << "damage refund: " << checks << " checks passed\n";
}
