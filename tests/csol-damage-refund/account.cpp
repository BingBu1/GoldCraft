// SPDX-License-Identifier: GPL-3.0-or-later
// Appended to the unchanged production account/guard/hook-registry adapter.
#include <iostream>
#include <stdexcept>
using namespace csol::scenario::custom;
namespace
{
enum class Action { Normal, Increase, Decrease, Cancel, Redirect, Double, Bonus, NestedHit, RetireBefore, RetireAfter, Recreate };
Action action{};
int checks{}, depth{};
bool live{true}, direct{};
RefundCap* ledger{};
RefundPair pair{0, 1, {1, 1}};
CBasePlayer replacement;
void Check(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
bool Current(const void*) { return live; }
void Hit(CBasePlayer& player, int amount)
{
    RefundAwardScope award(ledger, pair, &player, Current, nullptr);
    player.AddAccount(amount, RT_NONE, true);
}
void Hook(AccountChain* chain, CBasePlayer* player, int amount, RewardType type, bool track)
{
    if (!depth)
    {
        if (action == Action::Cancel) return;
        if (action == Action::Increase) amount = 5000;
        if (action == Action::Decrease) amount = 37;
        if (action == Action::RetireBefore) live = false;
        if (action == Action::Recreate) ++player->serial;
        if (action == Action::Bonus || action == Action::NestedHit)
        {
            ++depth;
            if (action == Action::Bonus) player->AddAccount(20, RT_NONE, true);
            else Hit(*player, 80);
            --depth;
        }
    }
    auto* recipient = action == Action::Redirect ? &replacement : player;
    if (direct) chain->callOriginal(recipient, amount, type, track);
    else chain->callNext(recipient, amount, type, track);
    if (action == Action::Double) chain->callOriginal(recipient, amount, type, track);
    if (action == Action::RetireAfter) live = false;
}
void Reset()
{
    scenarioMode = true;
    rulesStorage.access = std::make_shared<CSOLScenarioEntityAccess>();
    g_pGameRules = &rulesStorage;
    replacement = {};
    replacement.pev = &replacement.vars;
    messages.clear(); onAccountMessage = {}; live = true; depth = 0;
}
void Native()
{
    g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(Hook, 192);
    for (bool original : {false, true})
        for (Action test : {Action::Normal, Action::Increase, Action::Decrease, Action::Cancel, Action::Redirect,
            Action::Double, Action::Bonus, Action::NestedHit, Action::RetireBefore, Action::RetireAfter, Action::Recreate})
        {
            Reset(); RefundCap cap(100); ledger = &cap; direct = original; action = test;
            CBasePlayer player;
            Hit(player, 50);
            const int granted = test == Action::Cancel || test == Action::Redirect || test == Action::RetireBefore ||
                test == Action::Recreate ? 0 : test == Action::Increase || test == Action::NestedHit ? 100 :
                test == Action::Decrease ? 37 : 50;
            const int bonus = test == Action::Bonus ? 20 : 0;
            Check(player.m_iAccount == granted + bonus, "actual post-hook native account amount");
            Check(cap.Remaining(pair) == 100 - granted, "only the typed actual credit consumes budget");
            Check(replacement.m_iAccount == 0, "prehook cannot redirect a tagged award into a different player");
            const unsigned packets = !granted ? 0 : test == Action::NestedHit || test == Action::Bonus ? 2 : 1;
            Check(messages.size() == packets, "actual Money callback count respects cancellation and one-shot original");
            Check(activePins == 0 && CSOLBreakableScope::Current(nullptr), "native ownership guards restore after callbacks");
        }
    Reset(); RefundCap cap(100); ledger = &cap; action = Action::Normal;
    CBasePlayer player; player.m_iAccount = 31990;
    Hit(player, 500);
    Check(player.m_iAccount == 32000 && cap.Remaining(pair) == 90, "native wallet clamp commits ten before message");
    player.m_iAccount = 0;
    bool observed{};
    onAccountMessage = [&]
    {
        observed = true;
        Check(cap.Remaining(pair) == 0, "complete credit is already counted inside the first Money callback");
        live = false;
    };
    Hit(player, 500);
    Check(observed && player.m_iAccount == 90 && cap.Remaining(pair) == 0, "message retirement cannot undo completed credit");
    onAccountMessage = {}; live = true;
    player.AddAccount(500, RT_NONE, true);
    Check(player.m_iAccount == 590 && cap.Remaining(pair) == 0, "independent RT_NONE kill bonus remains unlimited by the damage cap");
    Hit(player, 100);
    Check(player.m_iAccount == 590 && messages.back().size() == 5, "exhausted cap retains the native zero-money observation");
    Hit(player, -50);
    Check(player.m_iAccount == 540 && cap.Remaining(pair) == 0, "negative typed request preserves native debit without budget refund");
    Reset(); scenarioMode = false; action = Action::Normal; ledger = nullptr;
    CBasePlayer ordinary; ordinary.m_iAccount = 100;
    ordinary.AddAccount(50, RT_NONE, true);
    Check(ordinary.m_iAccount == 150 && messages.back().size() == 5,
        "ordinary CS wrapper retains native money and message behavior");
    Hit(ordinary, 20000);
    Check(ordinary.m_iAccount == 16000 && activePins == 0,
        "disabled refund channel preserves ordinary CS wallet maximum and scope cleanup");
    g_ReGameHookchains.m_CBasePlayer_AddAccount.unregisterHook(Hook);
}
}
int main()
{
    try { Native(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    std::cout << "native damage refund: " << checks << " checks passed\n";
}
