// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace csol::scenario::map_vote
{
// GoldCraft's multiplayer policy; these are not inferred CSO constants.
inline constexpr std::size_t MaxPlayers = 32, MaxChoices = 16;
inline constexpr unsigned MinimumSeconds = 10, MaximumSeconds = 120, DefaultSeconds = 20;
inline constexpr char MessageName[] = "CSOMapVote";
inline constexpr char CommandName[] = "csol_scenario_vote";
enum class Phase : std::uint8_t { Idle, Open, Finished, Cancelled };
enum class Ballot { Accepted, Unchanged, NotOpen, Stale, Ineligible, InvalidChoice };
struct Choice { std::uint16_t id{}; std::string mapName; };
struct Count { std::uint16_t id{}; std::uint8_t votes{}; bool operator==(const Count&) const = default; };
struct Snapshot
{
    std::uint32_t generation{}, revision{};
    Phase phase{};
    std::uint16_t seconds{}, selection{}, winner{};
    std::uint8_t eligible{}, count{};
    std::array<Count, MaxChoices> choices{};
    bool operator==(const Snapshot&) const = default;
};
bool ValidMapName(std::string_view name);
std::vector<std::byte> Encode(const Snapshot& value);
bool Decode(std::span<const std::byte> bytes, Snapshot& output);

// Local deadline projection. Repeated/delayed packets can shorten this fixed
// ballot clock, but never extend it. Tally revisions are tracked separately.
class Countdown final
{
public:
    bool Observe(const Snapshot& snapshot,double now);
    unsigned Remaining(double now) const;
    void Reset(){*this={};}
private:
    std::uint32_t generation_{},revision_{};
    Phase phase_{};
    double deadline_{},lastTime_{};
};

class Vote final
{
public:
    // The caller supplies only server-validated playable destinations. Begin
    // rejects duplicate identities/current-map entries; it never adds a map.
    bool Begin(std::uint32_t generation, std::span<const Choice> choices,
        std::string_view currentMap, double now, unsigned seconds);
    // slot is 1..32; token is the engine's current connection userid, never
    // client input. Slot reuse and departure remove the previous ballot.
    // Register after Begin. Terminal snapshots are immutable; a new Begin
    // clears registration and the adapter re-registers current connections.
    bool Connect(unsigned slot, std::uint32_t token);
    bool Disconnect(unsigned slot, std::uint32_t token);
    Ballot Cast(unsigned slot, std::uint32_t token, std::uint32_t generation,
        std::uint16_t choice, double now);
    // Ties (including zero ballots) use one server-supplied random value over
    // tied entries. The deadline is never shortened when everyone has voted.
    std::optional<Choice> Finish(double now, std::uint32_t tieValue);
    void Cancel();
    void Reset();
    Snapshot View(unsigned slot, std::uint32_t token, double now) const;
    Phase State() const { return phase_; }
    std::uint32_t Generation() const { return generation_; }
    std::uint32_t Revision() const { return revision_; }
private:
    struct Peer { std::uint32_t token{}; std::uint16_t choice{}; };
    std::array<Peer,MaxPlayers> peers_{};
    std::array<Choice,MaxChoices> choices_{};
    std::array<std::uint8_t,MaxChoices> votes_{};
    std::size_t count_{};
    std::uint32_t generation_{},revision_{};
    std::uint16_t winner_{};
    double deadline_{};
    Phase phase_{};
    std::optional<std::size_t> Index(std::uint16_t id) const;
    void Remove(Peer& peer);
    void Changed();
};
}
