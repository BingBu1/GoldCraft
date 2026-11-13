// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace csol::scenario::custom
{
struct RefundPlayer
{
    unsigned slot{};
    std::uint64_t generation{};
};
struct RefundPair
{
    std::size_t monsterSlot{};
    std::uint64_t lease{};
    RefundPlayer player;
};

// One map owns this bounded ledger, independently of replaceable hit/upgrade
// services. Native lives and character changes are deliberately not keys.
class RefundCap final
{
public:
    static constexpr std::size_t MonsterCount = 45, PlayerCount = 33;
    explicit RefundCap(int limit) : limit_(limit), valid_(limit >= 0 && limit <= 32000) {}
    void Invalidate() { valid_ = false; }
    std::optional<int> Remaining(const RefundPair&);
    bool Commit(const RefundPair&, int credited);
private:
    struct Player { std::uint64_t generation{}; int credited{}; };
    struct Monster { std::uint64_t lease{}; std::array<Player, PlayerCount> players{}; };
    bool Valid(const RefundPair&) const;
    std::array<Monster, MonsterCount> monsters_{};
    int limit_{};
    bool valid_{};
};
}
