// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/server/scenario/core/scenario_consumables.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace csol::scenario::consumables;
namespace db = csol::persistence;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d: %s\n", checks, name); }
}
db::EventId Id(unsigned value)
{
    db::EventId result;
    result.bytes[0] = static_cast<std::uint8_t>(value);
    result.bytes[1] = 0x81;
    return result;
}
struct Database final : Ledger
{
    std::vector<db::Request> requests;
    std::vector<db::EventId> acknowledged;
    db::Submission submission{db::Submission::Queued};
    bool ackReady{true};
    db::Submission Submit(db::Request request) override
    {
        if (submission == db::Submission::Queued) requests.push_back(std::move(request));
        return submission;
    }
    bool Acknowledge(const db::EventId& event) override
    {
        if (ackReady) acknowledged.push_back(event);
        return ackReady;
    }
};
struct Game final : Host
{
    struct Hud { Stamp stamp; Item item; float cooldown; std::int16_t count; };
    struct Clock { bool automatic; int seconds; };
    std::array<Player, 33> players{};
    std::vector<Effect> effects;
    std::vector<Hud> hud;
    std::vector<Clock> clocks;
    std::vector<db::Outcome> notices;
    db::Identities* identities{};
    std::function<void()> duringApply;
    bool prepared{true}, indeterminate{};
    int hints{}, unavailable{};
    bool Read(unsigned slot, Player& out) const override
    {
        if (!slot || slot > 32 || !players[slot].scenario) return false;
        out = players[slot];
        return true;
    }
    bool Prepare(const Effect& effect) const override
    {
        return prepared && players[effect.stamp.owner.slot].stamp == effect.stamp &&
            (effect.item != Item::Retry || players[effect.stamp.owner.slot].retryPhase == effect.retryPhase);
    }
    Applied Apply(const Effect& effect) noexcept override
    {
        effects.push_back(effect);
        if (duringApply) duringApply();
        if (indeterminate) return Applied::Indeterminate;
        auto& player = players[effect.stamp.owner.slot];
        if (effect.item == Item::Recovery30 || effect.item == Item::Recovery100)
            player.health = std::min(player.maximumHealth, player.health + effect.amount);
        else if (effect.item == Item::Revival)
        {
            player.deadflag = 0;
            player.health = player.maximumHealth;
            ++player.stamp.owner.life;
            const auto& owner = player.stamp.owner;
            identities->Life(owner.slot, owner.userid, owner.connection, owner.life);
        }
        else for (auto& member : players)
        {
            if (member.retryPhase != effect.retryPhase) continue;
            member.retryPhase = 0;
            if (member.retriesRemaining > 0) --member.retriesRemaining;
        }
        return Applied::Yes;
    }
    void Inventory(const Stamp& stamp, Item item, float cooldown, std::int16_t count) override
    { hud.push_back({stamp, item, cooldown, count}); }
    void Countdown(const Stamp&, bool automatic, int seconds) override
    { clocks.push_back({automatic, seconds}); }
    void RevivalHint(const Stamp&, bool) override { ++hints; }
    void RespawnUnavailable(const Stamp&) override { ++unavailable; }
    void StorageNotice(const Stamp&, db::Outcome result) override { notices.push_back(result); }
};
struct Fixture
{
    db::Identities identities;
    std::unique_ptr<db::EffectClaims> claims{std::make_unique<db::EffectClaims>()};
    Database database;
    Game game;
    Service service{game, database, identities, *claims, Id(1)};
    Fixture() { game.identities = &identities; Add(1); }
    void Add(unsigned slot, bool authenticated = true)
    {
        auto& player = game.players[slot];
        player = {};
        player.stamp = {{authenticated ? 76561198000000000ULL + slot : 0,
            100 + slot, 1, static_cast<int>(slot), static_cast<std::uint8_t>(slot)}, 10, 20};
        player.scenario = player.useItemsEnabled = player.deathThinkAllowed = player.respawnClockEnabled = true;
        player.health = 100; player.maximumHealth = 1000;
        player.now = 100; player.deathTime = 95;
        player.team = 2; player.subtype = 1; player.baseTotalLevel = 1; player.option1C4 = 0;
        player.difficultyColumn6 = 1; player.cooldown.difficultyColumn7 = 1;
        const auto& owner = player.stamp.owner;
        if (authenticated)
        {
            Check(identities.Approve(slot, owner.userid, owner.connection, owner.account, false, false, false), "real identity registry approval");
            Check(identities.Life(slot, owner.userid, owner.connection, owner.life), "real life registry update");
        }
    }
    db::Notice Result(const db::Request& request, db::Outcome outcome = db::Outcome::Applied,
        std::int64_t count = 9, std::int64_t revision = 2) const
    {
        db::Notice notice;
        notice.request = request;
        notice.reply.outcome = outcome;
        notice.reply.itemCount = count;
        notice.reply.revision = revision;
        notice.kind = db::NoticeKind::Finished;
        notice.effectEligible = request.operation == db::Operation::DebitItem && outcome == db::Outcome::Applied;
        return notice;
    }
    void Inventory(unsigned slot = 1, std::int64_t count = 10)
    {
        const auto first = database.requests.size();
        Check(service.Refresh(slot), "authenticated inventory refresh queued");
        Check(database.requests.size() == first + 4, "four item snapshots");
        for (std::size_t index = first; index < first + 4; ++index)
            Check(service.Receipt(Result(database.requests[index], db::Outcome::Applied, count, 1)) == ReceiptResult::Handled, "snapshot accepted");
    }
    void NextLife(unsigned slot = 1)
    {
        auto& owner = game.players[slot].stamp.owner;
        ++owner.life;
        Check(identities.Life(slot, owner.userid, owner.connection, owner.life), "next life registry update");
    }
};

void OriginalArithmetic()
{
    float delay{};
    Check(AutomaticDelay(1, 1, delay) && delay == 15, "level1 automatic15 seconds");
    Check(AutomaticDelay(10, 1, delay) && delay == 15, "level10 automatic15 seconds");
    Check(AutomaticDelay(11, 1, delay) && delay == 20, "level11 automatic20 seconds");
    Check(AutomaticDelay(91, 1, delay) && delay == 60, "level91 automatic cap60");
    Check(AutomaticDelay(100000, 2, delay) && delay == 120, "difficulty applied after cap");
    Check(!AutomaticDelay(0, 1, delay), "invalid level does not manufacture respawn");
    Check(!AutomaticDelay(1, -1, delay), "negative difficulty rejected");
    for (int subtype = 0; subtype <= 11; ++subtype)
        Check(HasTimedRevival(subtype) == (subtype == 1 || subtype == 2 || subtype == 3 || subtype == 4 || subtype == 6 || subtype == 10), "actual subtype table");
    Cooldown result;
    CooldownModifiers modifiers;
    Check(!ComputeCooldown(Item::Recovery30, 1, modifiers, result), "unknown difficulty not zero cooldown");
    modifiers.difficultyColumn7 = 2;
    modifiers.equipmentPercent[0] = 50;
    modifiers.recoverySecondsReduction = 7;
    Check(ComputeCooldown(Item::Recovery30, 1, modifiers, result) && result.duration == 53, "recovery equipment then difficulty then ability");
    modifiers.equipmentPercent[0] = 200;
    Check(ComputeCooldown(Item::Recovery30, 1, modifiers, result) && result.duration == 0, "recovery3 second clamp before difficulty and reduction");
    modifiers.revivalAbilityResult = 100;
    modifiers.equipmentPercent[2] = 50;
    Check(ComputeCooldown(Item::Revival, 1, modifiers, result) && result.duration == 50, "revival ability before equipment");
    Check(ComputeCooldown(Item::Revival, 5, modifiers, result) && result.duration == 0 && result.hudDuration == 180, "original subtype5 wire/deadline distinction");
}

void ContinuationTiming()
{
    Continuation clock;
    Check(!clock.Begin(100, 1, true), "uninitialized challenge cannot offer retry");
    Check(clock.InitializeBudget(2.9f) && clock.Initial() == 2, "column4 truncates once at challenge start");
    Check(!clock.Begin(100, 1, false), "no invented retry inventory");
    Check(!clock.Begin(100, 5, true), "subtype5 has no failure retry");
    Check(clock.Begin(100, 1, true), "qualified failure starts once");
    Check(!clock.Begin(100, 1, true), "repeat failure does not restart offer");
    Check(clock.Countdown() == -2 && clock.OfferedPhase() == 0, "first7 seconds not yet offered");
    Check(!clock.Tick(106.99f, false).offer, "original seven second delay");
    auto first = clock.Tick(107, false);
    Check(first.offer == 10 && first.hint && !first.expired, "original10 countdown and even hint");
    const auto phase = clock.OfferedPhase();
    Check(phase != 0 && !clock.Accept(phase + 1, 107), "wrong continuation generation rejected");
    Check(!clock.Tick(108, true).offer, "external pending postpones one second");
    Check(clock.Accept(phase, 108), "confirmed transaction accepts offered generation");
    Check(clock.Remaining() == 1 && clock.OfferedPhase() == 0, "accept decrements quota and cancels offer");
    Check(clock.InitializeBudget(9) && clock.Remaining() == 1 && clock.Initial() == 2,
        "new stage cannot refill or reselect challenge budget");
    Check(!clock.Accept(phase, 108), "duplicate accept cannot spend quota");
    Check(!clock.Tick(108, false).resume, "resume first decrement at current time");
    Check(!clock.Tick(109, false).resume && !clock.Tick(110, false).resume, "three second continuation delay");
    Check(clock.Tick(111, false).resume && !clock.Active(), "actual continuation after third second");
    Check(clock.InitializeBudget(-1, true) && clock.Begin(200, 1, true), "unlimited retry remains supported");
    for (int seconds = 0; seconds <= 10; ++seconds)
    {
        const auto step = clock.Tick(207.0f + seconds, false);
        Check(step.offer == 10 - seconds && step.expired == (seconds == 10), "one tick per original second through zero");
    }
    Check(!clock.Active() && clock.Remaining() == -1, "expired offer never consumes retry quota");
    Check(clock.Begin(230, 1, true), "new failure preserves infinite budget");
    Check(clock.Tick(237, false).offer == 10, "second failure offers actual quota");
    const auto stale = clock.OfferedPhase();
    Check(clock.InitializeBudget(1, true) && !clock.Accept(stale, 237), "same-map replay invalidates old receipt");
    Check(clock.Begin(240, 1, true) && clock.Tick(247, false).offer == 10, "new challenge can offer again");
    Check(!clock.Accept(stale, 247), "old phase cannot consume replacement challenge budget");
    Check(clock.Accept(clock.OfferedPhase(), 247) && clock.Remaining() == 0 && clock.Initial() == 1,
        "successful retry spends only the shared remaining budget");
    Check(!clock.InitializeBudget(-2, true) && clock.Remaining() == 0 && clock.Initial() == 1,
        "invalid replacement limit preserves live state");
    Check(clock.InitializeBudget(0, true) && !clock.Begin(260, 1, true), "zero limit denies retry");
}

void HealingAndReplay()
{
    Fixture f;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Rejected, "unknown inventory cannot grant healing");
    f.Inventory();
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Queued, "30 percent request queued");
    const auto request = f.database.requests.back();
    Check(f.game.players[1].health == 100 && f.game.effects.empty(), "queued is not applied");
    Check(request.asset == "csol_item_258" && request.amount == 1 && request.Valid(), "confirmed MP258 debit identity");
    Check(f.service.Command(1, "useitem", "zbs10") == UseResult::Rejected, "pending blocks second use");
    auto notice = f.Result(request);
    Check(f.service.Receipt(notice) == ReceiptResult::Handled, "confirmed debit consumed");
    Check(f.game.players[1].health == 400 && f.game.effects.size() == 1, "30 percent of maximum applied");
    Check(f.game.hud.back().cooldown == 60 && f.game.hud.back().count == 9, "authoritative cooldown and count");
    f.service.Receipt(notice);
    Check(f.game.effects.size() == 1 && f.database.requests.size() == 5, "duplicate neither heals nor refunds");
    f.database.ackReady = false;
    f.service.Pump();
    Check(f.service.Pending() == 1, "busy ACK retained");
    f.database.ackReady = true;
    f.service.Pump();
    Check(f.service.Pending() == 0 && f.database.acknowledged.size() == 1, "single successful ACK");
    auto& player = f.game.players[1];
    player.now = 160;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Rejected, "healing exact cooldown boundary is rejected");
    f.NextLife();
    player.now = 159;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Rejected, "life change preserves item cooldown");
    player.now = 160.01f;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Queued, "healing past cooldown allowed");
}

void RefundAfterLifecycle()
{
    for (int kind = 0; kind < 4; ++kind)
    {
        Fixture f; f.Inventory();
        Check(f.service.Command(1, "useitem", "zbs10") == UseResult::Queued, "stale scenario debit queued");
        const auto debit = f.database.requests.back();
        auto& player = f.game.players[1];
        if (kind == 0) f.NextLife();
        else if (kind == 1) ++player.stamp.map;
        else if (kind == 2)
            f.identities.Disconnect(1, player.stamp.owner.userid, player.stamp.owner.connection);
        else player.health = player.maximumHealth;
        f.service.Receipt(f.Result(debit));
        Check(f.game.effects.empty(), "stale/filled target not affected");
        const auto refund = f.database.requests.back();
        Check(refund.operation == db::Operation::RefundDebit && refund.reference == debit.event &&
            refund.account == debit.account && refund.asset == debit.asset && refund.amount == 1, "exact atomic compensation");
        Check(f.database.acknowledged.empty(), "not ACK before confirmed refund");
        f.service.Receipt(f.Result(refund, db::Outcome::Applied, 10, 3));
        f.service.Pump();
        Check(f.database.acknowledged.size() == 1 && f.database.acknowledged[0] == debit.event, "ACK after compensation only");
    }
}

void FailuresAndOrdering()
{
    Fixture f; f.Inventory();
    f.database.submission = db::Submission::Busy;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Rejected && f.service.Pending() == 0, "Busy queues no debit or effect");
    f.database.submission = db::Submission::Queued;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Queued, "retry queue once ready");
    const auto request = f.database.requests.back();
    auto outage = f.Result(request, db::Outcome::Unavailable);
    outage.reply.commitUnknown = true;
    f.service.Receipt(outage);
    f.game.players[1].now += 10;
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Rejected && f.game.effects.empty(), "unknown commit remains pending");
    f.service.Receipt(f.Result(request, db::Outcome::Insufficient));
    f.service.Pump();
    Check(f.game.effects.empty() && f.service.Pending() == 0, "insufficient inventory cannot heal");
    Check(f.service.Command(1, "useitem", "zbs3") == UseResult::Queued, "resolved failed debit unlocks owner");
    const auto next = f.database.requests.back();
    auto result = f.Result(next, db::Outcome::Applied, 4, 10);
    f.service.Receipt(result);
    f.service.Pump();
    Check(f.service.Refresh(1), "new inventory snapshot");
    const auto old = f.Result(f.database.requests[f.database.requests.size() - 4], db::Outcome::Applied, 100, 9);
    f.service.Receipt(old);
    f.service.Replay(1);
    bool found = false;
    for (auto it = f.game.hud.rbegin(); it != f.game.hud.rend(); ++it)
        if (it->item == Item::Recovery30) { found = it->count == 4; break; }
    Check(found, "old revision cannot increase count");
}

void RespawnAndRetry()
{
    Fixture f; f.Inventory();
    auto& p = f.game.players[1];
    f.service.DeathFrame(1, ReloadButton);
    Check(f.game.effects.empty() && f.database.requests.size() == 4, "alive R remains reload");
    p.deadflag = 2; p.health = 0;
    f.service.DeathFrame(1, ReloadButton);
    Check(f.database.requests.size() == 5 && f.database.requests.back().asset == "csol_item_260", "dead R uses revival260");
    auto request = f.database.requests.back();
    f.service.Receipt(f.Result(request));
    Check(p.deadflag == 0 && p.health == p.maximumHealth && p.stamp.owner.life == 2, "paid revival performs one real lifecycle request");
    Check(f.game.hud.back().cooldown == 180, "revival180 cooldown published after spawn");
    f.service.Pump();
    f.NextLife(); p.deadflag = 2; p.health = 0; p.deathTime = 102; p.now = 103;
    f.service.DeathFrame(1, ReloadButton);
    Check(f.database.requests.size() == 5 && !f.game.clocks.back().automatic && f.game.clocks.back().seconds == 177, "dead R reports retained cooldown");
    p.now = 117;
    f.service.DeathFrame(1, 0);
    Check(f.game.effects.size() == 2 && f.game.effects.back().automatic && f.database.requests.size() == 5, "automatic lifecycle does not debit inventory");

    Fixture team; team.Add(2); team.Inventory(); team.Inventory(2);
    for (unsigned slot : {1u, 2u})
    {
        team.game.players[slot].deadflag = 2;
        team.game.players[slot].terminating = true;
        team.game.players[slot].deathThinkAllowed = false;
        team.game.players[slot].retryPhase = 99;
        team.game.players[slot].retriesRemaining = 2;
    }
    team.service.DeathFrame(1, ReloadButton);
    const auto retry = team.database.requests.back();
    Check(retry.asset == "csol_item_300", "team retry uses item300");
    team.service.DeathFrame(2, ReloadButton);
    Check(team.database.requests.size() == 9, "team lease rejects second player's duplicate retry");
    Check(team.game.players[1].retriesRemaining == 2, "pending transaction does not spend retry quota");
    team.service.Receipt(team.Result(retry));
    Check(team.game.effects.size() == 1 && team.game.effects[0].retryPhase == 99 && team.game.players[1].retriesRemaining == 1, "committed retry passes exact continuation lease");
}

void FailClosedAndReentrancy()
{
    Fixture f; f.Add(2, false);
    Check(!f.service.Refresh(2), "unapproved account cannot read paid inventory");
    Check(f.service.Command(2, "useitem", "zbs3") == UseResult::Rejected, "unapproved account gets no free item");
    auto& p = f.game.players[2]; p.deadflag = 2; p.deathTime = 85;
    f.service.DeathFrame(2, 0);
    Check(f.game.effects.size() == 1 && f.game.effects.back().automatic, "automatic respawn independent of paid account");
    f.Inventory();
    Check(f.service.Command(1, "useitem", "zbs10") == UseResult::Queued, "reentrant fixture queued");
    const auto request = f.database.requests.back();
    const auto notice = f.Result(request);
    f.game.duringApply = [&] { Check(f.service.Receipt(notice) == ReceiptResult::Busy, "reentrant receipt explicitly returned to dispatcher"); };
    f.game.indeterminate = true;
    f.service.Receipt(notice);
    f.service.Receipt(notice);
    f.service.Pump();
    Check(f.database.acknowledged.empty() && f.database.requests.size() == 5, "uncertain application neither refunded nor silently ACKed");

    Fixture recovered; recovered.Inventory();
    recovered.service.Command(1, "useitem", "zbs3");
    auto old = recovered.Result(recovered.database.requests.back()); old.recovered = true; old.effectEligible = false;
    recovered.service.Receipt(old); recovered.service.Pump();
    Check(recovered.game.effects.empty() && recovered.database.acknowledged.empty() && recovered.database.requests.size() == 5, "process recovery requires reconciliation not free effects/refund");
}

void GateOwnsAutomaticRespawn()
{
    Fixture f; f.Inventory();
    auto& p = f.game.players[1]; p.deadflag = 2; p.health = 0;
    f.service.DeathFrame(1, ReloadButton, false);
    Check(f.database.requests.size() == 5 && f.database.requests.back().asset == "csol_item_260",
        "paid-only mode retains timed-map R debit before free deadline");
    Check(f.game.clocks.empty() && f.game.hints == 1, "paid-only mode sends paid hint without free countdown");
    f.service.Receipt(f.Result(f.database.requests.back()));
    Check(f.game.effects.size() == 1 && !f.game.effects.back().automatic && p.deadflag == 0,
        "paid-only mode applies committed paid revival");
    f.service.Pump(); f.NextLife(); p.deadflag = 2; p.health = 0; p.deathTime = 102; p.now = 103;
    f.service.DeathFrame(1, ReloadButton, false);
    Check(f.game.clocks.size() == 1 && !f.game.clocks[0].automatic && f.game.clocks[0].seconds == 177,
        "paid-only mode retains original post-spawn cooldown warning");
    p.now = 117;
    f.service.DeathFrame(1, ReloadButton, false);
    f.service.DeathFrame(1, 0, false);
    Check(f.game.effects.size() == 1 && p.deadflag == 2 && f.database.requests.size() == 5,
        "deadline reached neither duplicates Gate Spawn nor starts late paid debit");
    Check(f.game.clocks.size() == 1, "paid-only deadline sends no duplicate free clock");
    f.service.DeathFrame(1, 0);
    Check(f.game.effects.size() == 2 && f.game.effects.back().automatic,
        "disabled automatic branch did not consume automatic-life marker");

    Fixture blocked; blocked.Inventory(); auto& b = blocked.game.players[1];
    b.deadflag = 2; b.health = 0; b.respawnClockEnabled = false;
    blocked.service.DeathFrame(1, ReloadButton, false);
    Check(blocked.database.requests.size() == 4 && blocked.game.effects.empty(),
        "paid-only mode does not bypass original IsGameStarted clock gate");
    b.subtype = 5;
    blocked.service.DeathFrame(1, ReloadButton, false);
    Check(blocked.database.requests.size() == 5 && blocked.database.requests.back().asset == "csol_item_260",
        "paid-only mode preserves original subtype5 exception");
}

void CompensationAndQuantity()
{
    Fixture f; f.Inventory(1, 1);
    f.service.Command(1, "useitem", "zbs10");
    auto debit = f.database.requests.back();
    f.NextLife();
    f.database.submission = db::Submission::Busy;
    f.service.Receipt(f.Result(debit, db::Outcome::Applied, 0));
    f.service.Pump();
    Check(f.database.requests.size() == 5 && f.database.acknowledged.empty(), "refund Busy retains original debit");
    f.database.submission = db::Submission::Queued;
    f.service.Pump();
    const auto refund = f.database.requests.back();
    f.service.Pump();
    Check(f.database.requests.size() == 6 && refund.reference == debit.event, "refund queued once with stable UUID");
    auto outage = f.Result(refund, db::Outcome::Unavailable);
    outage.reply.commitUnknown = true;
    f.service.Receipt(outage); f.service.Pump();
    Check(f.database.requests.size() == 6 && f.database.acknowledged.empty(), "unknown refund remains owned");
    f.service.Receipt(f.Result(refund, db::Outcome::Applied, 1, 3)); f.service.Pump();
    f.game.players[1].now = 105;
    f.service.Command(1, "useitem", "zbs10");
    debit = f.database.requests.back();
    f.service.Receipt(f.Result(debit, db::Outcome::Applied, 0, 4));
    Check(f.game.players[1].health == 1000, "full recovery fills actual maximum");
    std::int64_t quantity = -1;
    Check(f.service.Count(1, Item::Recovery100, quantity) && quantity == 0, "zero quantity is known and retained");
    f.game.players[1].health = 10; f.game.players[1].now = 1000;
    Check(f.service.Command(1, "useitem", "zbs10") == UseResult::Rejected, "zero inventory cannot be spent twice");

    Fixture claimed; claimed.Inventory();
    claimed.service.Command(1, "useitem", "zbs3");
    const auto notice = claimed.Result(claimed.database.requests.back());
    Check(claimed.claims->Claim(notice, claimed.identities) == db::ClaimResult::Granted, "existing effect claim fixture");
    claimed.service.Receipt(notice); claimed.service.Pump();
    Check(claimed.game.effects.empty() && claimed.database.requests.size() == 5 && claimed.database.acknowledged.empty(), "duplicate claim does not refund or apply");
}
}
int main()
{
    OriginalArithmetic(); ContinuationTiming(); HealingAndReplay(); RefundAfterLifecycle(); FailuresAndOrdering();
    RespawnAndRetry(); FailClosedAndReentrancy(); CompensationAndQuantity(); GateOwnsAutomaticRespawn();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
