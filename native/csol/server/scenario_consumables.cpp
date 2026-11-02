// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_consumables.h"
#include "../scenario/automatic_respawn.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace csol::scenario::consumables
{
namespace
{
constexpr std::size_t Index(Item item) { return static_cast<std::size_t>(item); }
bool FiniteNonnegative(float value) { return std::isfinite(value) && value >= 0; }
bool ValidStamp(const Stamp& stamp)
{
    return stamp.map && stamp.round && stamp.owner.slot > 0 && stamp.owner.slot <= 32 &&
        stamp.owner.userid > 0 && stamp.owner.connection && stamp.owner.life;
}
struct Busy
{
    bool& flag;
    explicit Busy(bool& value) : flag(value) { flag = true; }
    ~Busy() { flag = false; }
};
int Seconds(float value)
{
    if (!FiniteNonnegative(value)) return 0;
    if (value >= static_cast<float>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(value); // Original cvttss2si truncates; never ceil.
}
std::int16_t DisplayCount(std::int64_t count)
{ return static_cast<std::int16_t>(std::clamp<std::int64_t>(count, 0, 32767)); }
}

bool HasTimedRevival(int subtype)
{
    return respawn::TimedSubtype(subtype);
}

bool AutomaticDelay(int baseTotalLevel, float difficultyColumn6, float& seconds)
{
    return respawn::Delay(baseTotalLevel, difficultyColumn6, seconds);
}

bool ComputeCooldown(Item item, int subtype, const CooldownModifiers& modifiers, Cooldown& result)
{
    result = {};
    if (item == Item::Retry) return true;
    if (Index(item) >= 3 || !FiniteNonnegative(modifiers.difficultyColumn7)) return false;
    const auto percent = modifiers.equipmentPercent[Index(item)];
    if (percent && !std::isfinite(*percent)) return false;
    if (item == Item::Revival)
    {
        //114C0331: the no-cooldown subtype branch zeros the deadline but
        // leaves the local HUD duration at180. Preserve that wire behavior.
        if (!HasTimedRevival(subtype)) { result.hudDuration = 180; return true; }
        float value = 180.0f * modifiers.difficultyColumn7;
        if (!FiniteNonnegative(value)) return false;
        if (modifiers.revivalAbilityResult) value = *modifiers.revivalAbilityResult;
        if (!std::isfinite(value)) return false;
        value = std::max(value, 0.0f);
        if (percent) value = std::max(3.0f, value - (*percent / 100.0f) * value);
        if (!FiniteNonnegative(value)) return false;
        result.duration = result.hudDuration = value;
        return true;
    }
    //114C060C/114C084B: equipment first, then difficulty, then component12.
    float value = 60.0f;
    if (percent) value = std::max(3.0f, value - (*percent / 100.0f) * value);
    value *= modifiers.difficultyColumn7;
    if (!std::isfinite(value)) return false;
    if (modifiers.recoverySecondsReduction)
    {
        if (!std::isfinite(*modifiers.recoverySecondsReduction)) return false;
        value -= *modifiers.recoverySecondsReduction;
    }
    if (!std::isfinite(value)) return false;
    result.duration = result.hudDuration = std::max(value, 0.0f);
    return true;
}

Service::Service(Host& host, Ledger& ledger, persistence::Identities& identities,
    persistence::EffectClaims& claims, persistence::EventId ledgerId)
    : host_(host), ledger_(ledger), identities_(identities), claims_(claims), ledgerId_(ledgerId) {}

bool Service::SameConnection(const persistence::Owner& a, const persistence::Owner& b) const
{
    return a.slot == b.slot && a.userid == b.userid && a.connection == b.connection && a.account == b.account;
}

bool Service::Read(unsigned slot, Player& player)
{
    if (slot < 1 || slot > 32 || !host_.Read(slot, player) || !player.scenario ||
        player.stamp.owner.slot != slot || !ValidStamp(player.stamp) ||
        !FiniteNonnegative(player.now)) return false;
    Observe(player);
    return true;
}

Service::PlayerState& Service::Observe(const Player& player)
{
    auto& state = players_[player.stamp.owner.slot];
    if (!SameConnection(state.stamp.owner, player.stamp.owner)) state = {};
    else if (state.stamp.map != player.stamp.map)
    {
        // Inventory belongs to the account; engine deadlines belong to a map.
        const auto inventory = state.inventory;
        state = {};
        state.inventory = inventory;
    }
    else if (state.stamp.owner.life != player.stamp.owner.life || state.stamp.round != player.stamp.round)
    {
        state.nextCountdown = state.nextHint = state.nextUnavailable = 0;
        state.automaticLife = 0;
        // Do not erase cooldowns on death, character change or respawn.
    }
    state.stamp = player.stamp;
    return state;
}

bool Service::RetryLocked(const Player& player) const
{
    return std::any_of(transactions_.begin(), transactions_.end(), [&](const Transaction& transaction)
    {
        return transaction.stage != Stage::Empty && !transaction.snapshot &&
            transaction.item == Item::Retry && transaction.stamp.map == player.stamp.map &&
            transaction.stamp.round == player.stamp.round && transaction.retryPhase == player.retryPhase;
    });
}

bool Service::Eligible(const Player& player, Item item, bool receipt) const
{
    if (!identities_.Current(player.stamp.owner)) return false;
    const auto& state = players_[player.stamp.owner.slot];
    if (!receipt)
    {
        if (!ledgerId_.Valid() || state.pending.Valid() || player.now <= state.submittedUntil) return false;
        const auto& inventory = state.inventory[Index(item)];
        if (!inventory.known || inventory.count <= 0) return false;
    }
    //108291AD checks team retry BEFORE the ordinary terminating/deadflag
    // gates. A Failed phase must still accept its pending retry offer.
    if (item == Item::Retry)
        return player.team == 2 && player.retryPhase &&
            (player.retriesRemaining > 0 || player.retriesRemaining == -1) &&
            (receipt || !RetryLocked(player));
    if (player.terminating) return false;
    if (item == Item::Recovery30 || item == Item::Recovery100)
        return player.useItemsEnabled && player.deadflag == 0 &&
            std::isfinite(player.health) && std::isfinite(player.maximumHealth) &&
            player.health > 0 && player.maximumHealth > player.health &&
            player.now > state.until[Index(item)];
    if (player.team != 2) return false;
    return player.deathThinkAllowed && player.deadflag == 2 && player.option1C4 >= 0 && player.option1C4 != 0x320 &&
        ((HasTimedRevival(player.subtype) && player.respawnClockEnabled) || player.subtype == 5) &&
        (player.subtype == 5 || player.now >= state.until[Index(Item::Revival)]);
}

bool Service::MakeEffect(const Player& player, Item item, Effect& effect) const
{
    effect = {};
    effect.stamp = player.stamp;
    effect.item = item;
    effect.retryPhase = player.retryPhase;
    if (!ComputeCooldown(item, player.subtype, player.cooldown, effect.cooldown)) return false;
    if (item == Item::Recovery30) effect.amount = player.maximumHealth * 0.3f;
    else if (item == Item::Recovery100) effect.amount = player.maximumHealth - player.health;
    return FiniteNonnegative(effect.amount) && FiniteNonnegative(player.now + effect.cooldown.duration);
}

Service::Transaction* Service::Empty()
{
    for (auto& transaction : transactions_) if (transaction.stage == Stage::Empty) return &transaction;
    return nullptr;
}

UseResult Service::Use(const Player& player, Item item)
{
    if (!Eligible(player, item, false)) return UseResult::Rejected;
    Effect effect;
    if (!MakeEffect(player, item, effect) || !host_.Prepare(effect)) return UseResult::Rejected;
    auto* transaction = Empty();
    const auto event = persistence::EventId::Generate();
    if (!transaction || !event) return UseResult::Rejected;
    persistence::Request request;
    request.ledger = ledgerId_;
    request.event = *event;
    request.owner = player.stamp.owner;
    request.account = request.owner.account;
    request.operation = persistence::Operation::DebitItem;
    request.asset = Assets[Index(item)];
    request.amount = 1;
    // Reserving precedes submission. Reentrant commands/receipts cannot race
    // another user into the same team retry while Submit is on the stack.
    *transaction = {};
    transaction->request = request;
    transaction->stamp = player.stamp;
    transaction->item = item;
    transaction->retryPhase = player.retryPhase;
    transaction->stage = Stage::Waiting;
    const auto submission = ledger_.Submit(request);
    if (submission != persistence::Submission::Queued)
    {
        *transaction = {};
        host_.StorageNotice(player.stamp, persistence::Outcome::Unavailable);
        return UseResult::Rejected;
    }
    auto& state = players_[player.stamp.owner.slot];
    state.pending = request.event;
    state.submittedUntil = player.now + 2.0f;
    return UseResult::Queued;
}

UseResult Service::Command(unsigned slot, std::string_view command, std::string_view argument)
{
    if (busy_ || command != "useitem" || (argument != "zbs3" && argument != "zbs10"))
        return UseResult::Ignored;
    Busy guard(busy_);
    Player player;
    if (!Read(slot, player)) return UseResult::Ignored;
    return Use(player, argument == "zbs3" ? Item::Recovery30 : Item::Recovery100);
}

void Service::DeathFrame(unsigned slot, unsigned buttons, bool automatic)
{
    if (busy_) return;
    Busy guard(busy_);
    Player player;
    if (!Read(slot, player) || player.team != 2) return;
    auto& state = players_[slot];
    if ((buttons & ReloadButton) && player.retryPhase && Use(player, Item::Retry) == UseResult::Queued) return;
    if (player.terminating) return;
    if ((player.deadflag != 2 && player.deadflag != 3) || !player.deathThinkAllowed) return;
    const bool timed = HasTimedRevival(player.subtype);
    if ((!timed || !player.respawnClockEnabled) && player.subtype != 5)
    {
        if (!timed && (buttons & ReloadButton) && player.now >= state.nextUnavailable)
        {
            state.nextUnavailable = player.now + 3.0f;
            host_.RespawnUnavailable(player.stamp);
        }
        return;
    }
    float delay = 10.0f;
    if (player.subtype != 5 && !player.fixedTenSecondRespawn &&
        !AutomaticDelay(player.baseTotalLevel, player.difficultyColumn6, delay)) return;
    const float deadline = player.deathTime + delay;
    if (!FiniteNonnegative(player.deathTime) || !FiniteNonnegative(deadline)) return;
    if (player.now >= deadline)
    {
        if (!automatic) return;
        if (state.automaticLife == player.stamp.owner.life) return;
        Effect effect;
        effect.stamp = player.stamp;
        effect.item = Item::Revival;
        effect.automatic = true;
        if (!host_.Prepare(effect)) return;
        state.automaticLife = player.stamp.owner.life;
        // Automatic respawn is an original free lifecycle path, never an
        // inventory fallback. Its failure cannot trigger repeated Spawn calls.
        if (host_.Apply(effect) == Applied::Indeterminate)
            host_.StorageNotice(player.stamp, persistence::Outcome::Cancelled);
        return;
    }
    if (automatic && player.now >= state.nextCountdown)
    {
        state.nextCountdown = player.now + 1.0f;
        host_.Countdown(player.stamp, true, Seconds(deadline - player.now));
    }
    const auto& inventory = state.inventory[Index(Item::Revival)];
    if (player.deadflag != 2 || !inventory.known || inventory.count <= 0) return;
    if (player.subtype != 5 && player.now < state.until[Index(Item::Revival)])
    {
        if ((buttons & ReloadButton) && player.now >= state.nextHint)
        {
            state.nextHint = player.now + 2.0f;
            host_.Countdown(player.stamp, false, Seconds(state.until[Index(Item::Revival)] - player.now));
        }
        return;
    }
    if (player.now >= state.nextHint)
    {
        state.nextHint = player.now + 2.0f;
        host_.RevivalHint(player.stamp, false);
    }
    if (buttons & ReloadButton) Use(player, Item::Revival);
}

bool Service::SubmitSnapshot(const Player& player, Item item)
{
    for (const auto& transaction : transactions_)
        if (transaction.stage != Stage::Empty && transaction.snapshot && transaction.item == item &&
            SameConnection(transaction.stamp.owner, player.stamp.owner)) return true;
    auto* transaction = Empty();
    const auto event = persistence::EventId::Generate();
    if (!transaction || !event) return false;
    persistence::Request request;
    request.ledger = ledgerId_;
    request.event = *event;
    request.owner = player.stamp.owner;
    request.account = request.owner.account;
    request.operation = persistence::Operation::Snapshot;
    request.asset = Assets[Index(item)];
    if (ledger_.Submit(request) != persistence::Submission::Queued) return false;
    *transaction = {};
    transaction->request = std::move(request);
    transaction->stamp = player.stamp;
    transaction->item = item;
    transaction->stage = Stage::Waiting;
    transaction->snapshot = true;
    return true;
}

bool Service::Refresh(unsigned slot)
{
    if (busy_) return false;
    Busy guard(busy_);
    Player player;
    if (!Read(slot, player) || !ledgerId_.Valid() || !identities_.Current(player.stamp.owner)) return false;
    bool result = true;
    for (unsigned index = 0; index < 4; ++index)
        if (!SubmitSnapshot(player, static_cast<Item>(index))) result = false;
    return result;
}

void Service::Publish(const Player& player, Item item, float cooldown)
{
    const auto& inventory = players_[player.stamp.owner.slot].inventory[Index(item)];
    if (inventory.known) host_.Inventory(player.stamp, item, cooldown, DisplayCount(inventory.count));
}

bool Service::Replay(unsigned slot)
{
    if (busy_) return false;
    Busy guard(busy_);
    Player player;
    if (!Read(slot, player)) return false;
    for (unsigned index = 0; index < 4; ++index)
    {
        const float remaining = index < 3 ? std::max(0.0f, players_[slot].until[index] - player.now) : 0.0f;
        Publish(player, static_cast<Item>(index), remaining);
    }
    return true;
}

bool Service::Count(unsigned slot, Item item, std::int64_t& quantity)
{
    if (busy_ || Index(item) >= 4) return false;
    Busy guard(busy_);
    Player player;
    if (!Read(slot, player) || !identities_.Current(player.stamp.owner)) return false;
    const auto& inventory = players_[slot].inventory[Index(item)];
    if (!inventory.known) return false;
    quantity = inventory.count;
    return true;
}

bool Service::RetryPending(std::uint64_t map, std::uint64_t round, std::uint64_t phase) const
{
    return phase && std::any_of(transactions_.begin(), transactions_.end(), [&](const Transaction& transaction)
    {
        return transaction.stage == Stage::Waiting && !transaction.snapshot && transaction.item == Item::Retry &&
            transaction.stamp.map == map && transaction.stamp.round == round && transaction.retryPhase == phase;
    });
}

void Service::UpdateInventory(const persistence::Notice& notice, Item item)
{
    // SQL also returns the current locked balance on Insufficient. It may be
    // newer than a prior snapshot because another server consumed inventory.
    if ((notice.reply.outcome != persistence::Outcome::Applied && notice.reply.outcome != persistence::Outcome::Insufficient) || notice.reply.itemCount < 0 ||
        notice.reply.itemCount > persistence::MaximumBalance || notice.reply.revision < 0) return;
    Player player;
    if (!Read(notice.request.owner.slot, player) ||
        !SameConnection(notice.request.owner, player.stamp.owner) ||
        !identities_.Current(player.stamp.owner)) return;
    auto& inventory = players_[player.stamp.owner.slot].inventory[Index(item)];
    if (notice.reply.revision < inventory.revision) return;
    if (notice.reply.revision == inventory.revision && inventory.known) return;
    inventory = {notice.reply.itemCount, notice.reply.revision, true};
    Publish(player, item, 0); // Zero changes quantity only in the original HUD.
}

void Service::Release(Transaction& transaction)
{
    auto& state = players_[transaction.stamp.owner.slot];
    if (state.pending == transaction.request.event) state.pending = {};
}

void Service::Refund(Transaction& transaction)
{
    Release(transaction);
    transaction.stage = Stage::RefundReady;
    // The refund's UUID survives Submit Busy. Never generate a different
    // refund ID on each frame or ACK the debit before the refund is Applied.
    if (!transaction.refund.event.Valid())
    {
        const auto event = persistence::EventId::Generate();
        if (!event) return;
        transaction.refund = transaction.request;
        transaction.refund.event = *event;
        transaction.refund.reference = transaction.request.event;
        transaction.refund.operation = persistence::Operation::RefundDebit;
    }
    if (ledger_.Submit(transaction.refund) == persistence::Submission::Queued)
        transaction.stage = Stage::RefundWaiting;
}

void Service::Settle(Transaction& transaction, const persistence::Notice& notice)
{
    if (notice.reply.outcome != persistence::Outcome::Applied)
    {
        // Unavailable is a progress notice: Worker keeps retrying the same
        // durable debit, including commitUnknown. Do not release or re-submit.
        if (notice.reply.outcome == persistence::Outcome::Unavailable) return;
        if (notice.reply.commitUnknown) { transaction.stage = Stage::Uncertain; return; }
        Release(transaction);
        transaction.stage = Stage::Ack;
        host_.StorageNotice(transaction.stamp, notice.reply.outcome);
        return;
    }
    if (notice.recovered)
    {
        transaction.stage = Stage::Uncertain;
        return;
    }
    Player player;
    Effect effect;
    if (!Read(transaction.stamp.owner.slot, player) ||
        player.stamp != transaction.stamp || !Eligible(player, transaction.item, true) ||
        (transaction.item == Item::Retry && player.retryPhase != transaction.retryPhase) ||
        !MakeEffect(player, transaction.item, effect) || !host_.Prepare(effect))
    {
        Refund(transaction);
        return;
    }
    const auto claim = claims_.Claim(notice, identities_);
    if (claim == persistence::ClaimResult::Duplicate)
    {
        // This request was already claimed; a repeated receipt cannot refund
        // or repeat gameplay. Retain it if the prior disposition was unknown.
        transaction.stage = Stage::Uncertain;
        return;
    }
    if (claim != persistence::ClaimResult::Granted) { Refund(transaction); return; }
    // Nothing that can invoke gameplay runs between Claim and Apply.
    const auto applied = host_.Apply(effect);
    transaction.stage = applied == Applied::Yes ? Stage::Ack : Stage::Uncertain;
    if (applied != Applied::Yes)
    {
        host_.StorageNotice(transaction.stamp, persistence::Outcome::Cancelled);
        return;
    }
    Release(transaction);
    // Spawn legitimately changes life. Same connection/map still receives
    // the committed cooldown; a reused player slot never inherits it.
    Player current;
    if (Read(transaction.stamp.owner.slot, current) && current.stamp.map == transaction.stamp.map &&
        SameConnection(current.stamp.owner, transaction.stamp.owner))
    {
        if (Index(transaction.item) < 3)
            players_[current.stamp.owner.slot].until[Index(transaction.item)] =
                effect.cooldown.duration ? player.now + effect.cooldown.duration : 0;
        Publish(current, transaction.item, effect.cooldown.hudDuration);
    }
}

ReceiptResult Service::Receipt(const persistence::Notice& notice)
{
    if (notice.request.ledger != ledgerId_) return ReceiptResult::Unrelated;
    Transaction* found = nullptr;
    bool refund = false;
    for (auto& transaction : transactions_)
    {
        if (transaction.stage == Stage::Empty) continue;
        if (transaction.request.event == notice.request.event) { found = &transaction; break; }
        if (transaction.refund.event.Valid() && transaction.refund.event == notice.request.event)
        { found = &transaction; refund = true; break; }
    }
    if (!found) return ReceiptResult::Unrelated;
    if (busy_) return ReceiptResult::Busy;
    Busy guard(busy_);
    const auto& expected = refund ? found->refund : found->request;
    if (!expected.SameMutation(notice.request) || expected.owner != notice.request.owner)
        return ReceiptResult::Unrelated;
    if (notice.kind == persistence::NoticeKind::Durable) return ReceiptResult::Handled;
    if (notice.kind == persistence::NoticeKind::JournalFailed)
    {
        if (refund && found->stage == Stage::RefundWaiting) found->stage = Stage::RefundReady;
        else if (!refund && found->stage == Stage::Waiting) { Release(*found); *found = {}; }
        return ReceiptResult::Handled;
    }
    UpdateInventory(notice, found->item);
    if (refund)
    {
        if (found->stage != Stage::RefundWaiting) return ReceiptResult::Handled;
        if (notice.reply.outcome == persistence::Outcome::Applied) found->stage = Stage::Ack;
        else if (notice.reply.outcome != persistence::Outcome::Unavailable) found->stage = Stage::Uncertain;
        return ReceiptResult::Handled;
    }
    if (found->snapshot)
    {
        if (notice.reply.outcome != persistence::Outcome::Unavailable) *found = {};
        return ReceiptResult::Handled;
    }
    if (found->stage == Stage::Waiting) Settle(*found, notice);
    return ReceiptResult::Handled;
}

void Service::Pump()
{
    if (busy_) return;
    Busy guard(busy_);
    unsigned attempts = 0;
    for (auto& transaction : transactions_)
    {
        if (attempts >= 8) break;
        if (transaction.stage == Stage::RefundReady) { ++attempts; Refund(transaction); }
        else if (transaction.stage == Stage::Ack)
        {
            ++attempts;
            if (ledger_.Acknowledge(transaction.request.event)) transaction = {};
        }
    }
}

std::size_t Service::Pending() const
{
    return std::count_if(transactions_.begin(), transactions_.end(),
        [](const Transaction& transaction) { return transaction.stage != Stage::Empty; });
}
}
