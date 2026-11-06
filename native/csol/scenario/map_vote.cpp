// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_vote.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace csol::scenario::map_vote
{
namespace
{
bool Time(double value) { return std::isfinite(value) && value >= 0; }
bool Valid(const Snapshot& value)
{
    if (!value.generation || !value.revision || value.phase == Phase::Idle ||
        value.phase > Phase::Cancelled || !value.count || value.count > MaxChoices ||
        value.eligible > MaxPlayers || value.seconds > MaximumSeconds ||
        (value.phase != Phase::Open && value.seconds) ||
        (value.phase != Phase::Finished && value.winner)) return false;
    unsigned total{};
    bool selection = value.selection == 0, winner = value.winner == 0;
    unsigned maximum{}, winningVotes{};
    for (unsigned i = 0; i < value.count; ++i)
    {
        const auto& item = value.choices[i];
        if (!item.id || item.votes > MaxPlayers) return false;
        for (unsigned j = 0; j < i; ++j) if (value.choices[j].id == item.id) return false;
        total += item.votes;
        selection |= item.id == value.selection && item.votes != 0;
        winner |= item.id == value.winner;
        maximum = std::max(maximum,unsigned(item.votes));
        if (item.id == value.winner) winningVotes = item.votes;
    }
    return selection && winner && total <= value.eligible &&
        (value.phase != Phase::Finished || (value.winner != 0 && winningVotes == maximum));
}
}

bool ValidMapName(std::string_view name)
{
    return !name.empty() && name.size() < 32 && std::all_of(name.begin(),name.end(),[](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::vector<std::byte> Encode(const Snapshot& value)
{
    if (!Valid(value)) return {};
    std::vector<std::byte> bytes;
    bytes.reserve(18 + value.count * 3);
    const auto write = [&](std::uint32_t n,unsigned length)
        { for (unsigned i = 0; i < length; ++i) bytes.push_back(std::byte(n >> (i * 8))); };
    write(1,1);write(unsigned(value.phase),1);write(value.generation,4);write(value.revision,4);
    write(value.seconds,2);write(value.selection,2);write(value.winner,2);
    write(value.eligible,1);write(value.count,1);
    for (unsigned i = 0; i < value.count; ++i)
    { write(value.choices[i].id,2);write(value.choices[i].votes,1); }
    return bytes;
}

bool Decode(std::span<const std::byte> bytes,Snapshot& output)
{
    if (bytes.size() < 18 || bytes.size() > 18 + MaxChoices * 3 || bytes[0] != std::byte{1}) return false;
    std::size_t cursor = 1;
    const auto read = [&](unsigned length)
    {
        std::uint32_t value{};
        for (unsigned i = 0; i < length; ++i) value |= std::uint32_t(bytes[cursor++]) << (i * 8);
        return value;
    };
    Snapshot value;
    value.phase = Phase(read(1));value.generation = read(4);value.revision = read(4);
    value.seconds = std::uint16_t(read(2));value.selection = std::uint16_t(read(2));value.winner = std::uint16_t(read(2));
    value.eligible = std::uint8_t(read(1));value.count = std::uint8_t(read(1));
    if (value.count > MaxChoices || bytes.size() != 18 + value.count * 3) return false;
    for (unsigned i = 0; i < value.count; ++i)
    { value.choices[i].id = std::uint16_t(read(2));value.choices[i].votes = std::uint8_t(read(1)); }
    if (!Valid(value)) return false;
    output = value;
    return true;
}

bool Countdown::Observe(const Snapshot& value,double now)
{
    if(!Valid(value)||!Time(now)||!Time(now+value.seconds)||
        value.generation<generation_||(value.generation==generation_&&value.revision<revision_))return false;
    if(generation_&&now<lastTime_){phase_=Phase::Cancelled;return false;}
    const double projected=now+value.seconds;
    deadline_=value.generation==generation_?std::min(deadline_,projected):projected;
    generation_=value.generation;revision_=value.revision;phase_=value.phase;lastTime_=now;return true;
}
unsigned Countdown::Remaining(double now) const
{
    if(phase_!=Phase::Open||!Time(now)||now<lastTime_)return 0;
    return unsigned(std::clamp(std::ceil(deadline_-now),0.,double(MaximumSeconds)));
}

std::optional<std::size_t> Vote::Index(std::uint16_t id) const
{
    for (std::size_t i = 0; i < count_; ++i) if (choices_[i].id == id) return i;
    return {};
}
void Vote::Changed()
{
    ++revision_;
    // Even 32 players changing every frame cannot exhaust this during a
    // bounded vote; retain a nonzero identity for explicit cancellation.
    if (!revision_) ++revision_;
}
void Vote::Remove(Peer& peer)
{
    if (const auto index = Index(peer.choice); index && votes_[*index]) --votes_[*index];
    peer.choice = 0;
}

bool Vote::Begin(std::uint32_t generation,std::span<const Choice> choices,
    std::string_view currentMap,double now,unsigned seconds)
{
    if (phase_ == Phase::Open || !generation || generation <= generation_ || !Time(now) ||
        seconds < MinimumSeconds || seconds > MaximumSeconds || !Time(now + seconds) ||
        choices.empty() || choices.size() > MaxChoices || !ValidMapName(currentMap)) return false;
    for (std::size_t i = 0; i < choices.size(); ++i)
    {
        if (!choices[i].id || !ValidMapName(choices[i].mapName)) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (choices[j].id == choices[i].id || choices[j].mapName == choices[i].mapName) return false;
    }
    std::copy(choices.begin(),choices.end(),choices_.begin());
    count_ = choices.size();votes_ = {};winner_ = 0;generation_ = generation;revision_ = 1;
    peers_ = {};
    deadline_ = now + seconds;phase_ = Phase::Open;
    return true;
}

bool Vote::Connect(unsigned slot,std::uint32_t token)
{
    if (phase_ != Phase::Open || slot < 1 || slot > MaxPlayers || !token) return false;
    auto& peer = peers_[slot - 1];
    if (peer.token == token) return false;
    Remove(peer);peer.token = token;
    if (phase_ == Phase::Open) Changed();
    return true;
}
bool Vote::Disconnect(unsigned slot,std::uint32_t token)
{
    if (phase_ != Phase::Open || slot < 1 || slot > MaxPlayers || !token || peers_[slot - 1].token != token) return false;
    auto& peer = peers_[slot - 1];Remove(peer);peer.token = 0;
    if (phase_ == Phase::Open) Changed();
    return true;
}
Ballot Vote::Cast(unsigned slot,std::uint32_t token,std::uint32_t generation,std::uint16_t choice,double now)
{
    if (phase_ != Phase::Open || !Time(now) || now >= deadline_) return Ballot::NotOpen;
    if (generation != generation_) return Ballot::Stale;
    if (slot < 1 || slot > MaxPlayers || !token || peers_[slot - 1].token != token) return Ballot::Ineligible;
    const auto index = Index(choice);
    if (!index) return Ballot::InvalidChoice;
    auto& peer = peers_[slot - 1];
    if (peer.choice == choice) return Ballot::Unchanged;
    Remove(peer);peer.choice = choice;++votes_[*index];Changed();
    return Ballot::Accepted;
}
std::optional<Choice> Vote::Finish(double now,std::uint32_t tieValue)
{
    if (phase_ != Phase::Open || !Time(now) || now < deadline_) return {};
    const auto maximum = *std::max_element(votes_.begin(),votes_.begin()+count_);
    std::array<std::size_t,MaxChoices> ties{};std::size_t count{};
    for (std::size_t i = 0; i < count_; ++i) if (votes_[i] == maximum) ties[count++] = i;
    const auto& result = choices_[ties[tieValue % count]];
    winner_ = result.id;phase_ = Phase::Finished;Changed();
    return result;
}
void Vote::Cancel()
{
    if (phase_ == Phase::Open) { phase_ = Phase::Cancelled;winner_ = 0;Changed(); }
}
void Vote::Reset() { *this = {}; }
Snapshot Vote::View(unsigned slot,std::uint32_t token,double now) const
{
    Snapshot result;
    result.generation = generation_;result.revision = revision_;result.phase = phase_;result.winner = winner_;
    result.count = std::uint8_t(count_);
    if (phase_ == Phase::Open && Time(now))
        result.seconds = std::uint16_t(std::clamp(std::ceil(deadline_ - now),0.0,double(MaximumSeconds)));
    if (slot >= 1 && slot <= MaxPlayers && token && peers_[slot - 1].token == token)
        result.selection = peers_[slot - 1].choice;
    for (const auto& peer : peers_) if (peer.token) ++result.eligible;
    for (std::size_t i = 0; i < count_; ++i) result.choices[i] = {choices_[i].id,votes_[i]};
    return result;
}
}
