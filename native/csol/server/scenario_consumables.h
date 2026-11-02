// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "persistence/worker.hpp"
#include "../scenario/continuation.hpp"
#include <array>
#include <memory>
#include <limits>
#include <string_view>

namespace csol::scenario::consumables
{
enum class Item : std::uint8_t { Recovery30, Recovery100, Revival, Retry };
constexpr std::array<std::uint16_t, 4> ItemIds{258, 259, 260, 300};
constexpr std::array<std::string_view, 4> Assets{
    "csol_item_258", "csol_item_259", "csol_item_260", "csol_item_300"};
constexpr unsigned ReloadButton = 1u << 13;

// MP 10829100 / 10829690: these are map subtypes, not difficulty rows.
bool HasTimedRevival(int subtype);
bool AutomaticDelay(int baseTotalLevel, float difficultyColumn6, float& seconds);

struct CooldownModifiers
{
    float difficultyColumn7{std::numeric_limits<float>::quiet_NaN()};
    // The actual component12 getter, not an inferred character feature.
    std::optional<float> recoverySecondsReduction;
    //107AA640 returns the transformed 180*difficulty value. Its producer
    // must be the verified installed ability, never an invented multiplier.
    std::optional<float> revivalAbilityResult;
    // Account service addon16/17/18: present zero differs from absent.
    std::array<std::optional<float>, 3> equipmentPercent;
};
struct Cooldown
{
    float duration{}, hudDuration{};
};
bool ComputeCooldown(Item item, int subtype, const CooldownModifiers&, Cooldown&);

struct Stamp
{
    persistence::Owner owner;
    std::uint64_t map{}, round{};
    bool operator==(const Stamp&) const = default;
};

// Values come from a synchronous, read-only server snapshot. Defaults deny
// gameplay. An account of zero is permitted for ordinary automatic respawn;
// paid uses additionally require Identities::Current, never parsed AuthID.
struct Player
{
    Stamp stamp;
    float now{}, health{}, maximumHealth{}, deathTime{};
    int team{}, deadflag{}, subtype{}, baseTotalLevel{}, option1C4{-1};
    float difficultyColumn6{std::numeric_limits<float>::quiet_NaN()};
    bool scenario{}, terminating{}, useItemsEnabled{};
    // Common1082928B..108292F9 gate and root+27C. Adapter must use the
    // actual map/life/observer state; alive/dead alone does not imply either.
    bool deathThinkAllowed{}, respawnClockEnabled{}, fixedTenSecondRespawn{};
    // Manager +1348 >=0 and mode retry limit. Phase changes on each failure,
    // including another failure in the same round. Zero is not a valid lease.
    std::uint64_t retryPhase{};
    int retriesRemaining{};
    CooldownModifiers cooldown;
};

struct Effect
{
    Stamp stamp;
    Item item{};
    float amount{};
    Cooldown cooldown;
    std::uint64_t retryPhase{};
    bool automatic{};
};
enum class Applied { Yes, Indeterminate };

class Host
{
public:
    virtual ~Host() = default;
    virtual bool Read(unsigned slot, Player&) const = 0;
    // No callbacks or changes to gameplay in Prepare. Validate the exact
    // stamp, phase, real recovery service / spawn location / retry owner.
    virtual bool Prepare(const Effect&) const = 0;
    // Called immediately after Claim, on the main thread. Use the existing
    // ApplyRecovery path, actual player Spawn, or continuation lifecycle.
    // Must not call BeginRound/reload TRG for Retry. After entering Apply,
    // uncertainty is retained for reconciliation; it never earns a refund.
    virtual Applied Apply(const Effect&) noexcept = 0;
    virtual void Inventory(const Stamp&, Item, float cooldown, std::int16_t count) = 0;
    // true: AutoRespawnWait TextMsg; false: ZBSVer2 0x1C LONG (channel6
    // revival-not-cooled text), not the older RespawnWait TextMsg route.
    virtual void Countdown(const Stamp&, bool automatic, int seconds) = 0;
    virtual void RevivalHint(const Stamp&, bool retry) = 0;
    virtual void RespawnUnavailable(const Stamp&) = 0;
    virtual void StorageNotice(const Stamp&, persistence::Outcome) = 0;
};

class Ledger
{
public:
    virtual ~Ledger() = default;
    virtual persistence::Submission Submit(persistence::Request) = 0;
    virtual bool Acknowledge(const persistence::EventId&) = 0;
};
class WorkerLedger final : public Ledger
{
public:
    explicit WorkerLedger(persistence::Worker& worker) : worker_(worker) {}
    persistence::Submission Submit(persistence::Request request) override
    { return worker_.Submit(std::move(request)); }
    bool Acknowledge(const persistence::EventId& event) override
    { return worker_.Acknowledge(event); }
private:
    persistence::Worker& worker_;
};

enum class UseResult { Ignored, Rejected, Queued };
enum class ReceiptResult { Unrelated, Handled, Busy };

// Process-owned alongside Worker and EffectClaims. Do not destroy it on a
// map change: confirmed stale debits still need compensation and ACK.
class Service final
{
public:
    Service(Host&, Ledger&, persistence::Identities&, persistence::EffectClaims&,
        persistence::EventId ledger);
    UseResult Command(unsigned slot, std::string_view command, std::string_view argument);
    // Poll/reload R stays the engine's IN_RELOAD. Never rewrite native input.
    // A Gate with its own free revival Clock passes automatic=false. Paid R
    // retains the original game-start/deadline gates and cooldown behavior.
    void DeathFrame(unsigned slot, unsigned buttons, bool automatic = true);
    // Refreshes authoritative counts; HUD reset can call Replay afterwards.
    bool Refresh(unsigned slot);
    bool Replay(unsigned slot);
    bool Count(unsigned slot, Item, std::int64_t& quantity);
    bool RetryPending(std::uint64_t map, std::uint64_t round, std::uint64_t phase) const;
    ReceiptResult Receipt(const persistence::Notice&);
    // Bounded nonblocking retries of refunds/ACK. The single persistence
    // dispatcher calls Receipt; this service never steals other consumers' Poll.
    void Pump();
    std::size_t Pending() const;

private:
    enum class Stage { Empty, Waiting, RefundReady, RefundWaiting, Ack, Uncertain };
    struct Transaction
    {
        persistence::Request request, refund;
        Stamp stamp;
        Item item{};
        std::uint64_t retryPhase{};
        Stage stage{};
        bool snapshot{};
    };
    struct InventoryState
    {
        std::int64_t count{}, revision{-1};
        bool known{};
    };
    struct PlayerState
    {
        Stamp stamp;
        std::array<InventoryState, 4> inventory;
        std::array<float, 3> until{};
        float submittedUntil{}, nextCountdown{}, nextHint{}, nextUnavailable{};
        persistence::EventId pending;
        std::uint64_t automaticLife{};
    };
    bool Read(unsigned slot, Player&);
    PlayerState& Observe(const Player&);
    bool Eligible(const Player&, Item, bool receipt) const;
    bool MakeEffect(const Player&, Item, Effect&) const;
    UseResult Use(const Player&, Item);
    bool SubmitSnapshot(const Player&, Item);
    Transaction* Empty();
    void UpdateInventory(const persistence::Notice&, Item);
    void Publish(const Player&, Item, float cooldown);
    void Release(Transaction&);
    void Refund(Transaction&);
    void Settle(Transaction&, const persistence::Notice&);
    bool SameConnection(const persistence::Owner&, const persistence::Owner&) const;
    bool RetryLocked(const Player&) const;
    Host& host_;
    Ledger& ledger_;
    persistence::Identities& identities_;
    persistence::EffectClaims& claims_;
    persistence::EventId ledgerId_;
    std::array<PlayerState, 33> players_{};
    std::array<Transaction, 128> transactions_{};
    bool busy_{};
};
}
