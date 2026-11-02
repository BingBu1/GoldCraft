// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace csol::persistence {
struct EventId {
    std::array<std::uint8_t,16> bytes{};
    bool operator==(const EventId&) const = default;
    bool Valid() const;
    std::string Text() const;
    static std::optional<EventId> Parse(std::string_view);
    static std::optional<EventId> Generate();
};
struct EventHash { std::size_t operator()(const EventId&) const noexcept; };

// Steam public-universe individual accounts only. This format check does NOT
// establish authentication; the engine approval adapter owns that decision.
bool ValidSteamAccount(std::uint64_t value);
bool ValidAsset(std::string_view value);
constexpr std::int64_t MaximumBalance=9'000'000'000'000'000LL;
enum class Operation : std::uint8_t { Snapshot, CreditCoins, CreditItem, DebitItem, RefundDebit };
enum class Outcome : std::uint8_t {
    Applied, Insufficient, Conflict, Invalid, Unavailable, StorageFailure, WrongLedger, Cancelled
};

struct Owner {
    std::uint64_t account{}, connection{}, life{};
    std::int32_t userid{};
    std::uint8_t slot{};
    bool operator==(const Owner&) const = default;
};

// Main-thread identity registry. Approve must be called ONLY from the trusted
// engine's successful Steam-authentication callback, never userinfo, authid
// parsing, client commands, ClientConnect, or a client-supplied SteamID.
class Identities final {
public:
    bool Approve(unsigned slot,int userid,std::uint64_t connection,std::uint64_t account,
        bool lan,bool bot,bool proxy);
    bool Life(unsigned slot,int userid,std::uint64_t connection,std::uint64_t life);
    void Disconnect(unsigned slot,int userid,std::uint64_t connection);
    std::optional<Owner> Get(unsigned slot) const;
    bool Current(const Owner&) const;
private:
    std::array<Owner,33> players_{};
};

struct Request {
    EventId ledger,event,reference;
    std::uint64_t account{};
    Operation operation{};
    std::string asset;
    std::int64_t amount{};
    Owner owner;
    bool Valid() const;
    bool SameMutation(const Request&) const;
    bool Reward() const {return operation==Operation::CreditCoins||operation==Operation::CreditItem;}
};
struct Reply {
    Outcome outcome=Outcome::Unavailable;
    std::int64_t coins{},itemCount{},revision{};
    bool replayed{};
    // true only when the server may have committed before a transport failure.
    bool commitUnknown{};
};
enum class NoticeKind { Durable, Finished, JournalFailed };
struct Notice {
    Request request;
    Reply reply;
    NoticeKind kind{};
    bool recovered{};
    // Merely a transaction result. Gameplay still requires Claim on the main
    // thread; old-life, duplicate, or process-recovery receipts cannot revive.
    bool effectEligible{};
};

enum class ClaimResult {Granted,Duplicate,Stale,Invalid,Capacity};
class EffectClaims final {
public:
    ClaimResult Claim(const Notice&,const Identities&);
    // Keep for the entire server process, including map/round changes.
private:
    // Fixed memory bound; once full new uses must fail closed and be refunded
    // before any effect, rather than evicting an older effect receipt.
    static constexpr std::size_t Capacity=16384;
    std::array<EventId,Capacity> claims_{};
};
const char* Message(Outcome);
}
