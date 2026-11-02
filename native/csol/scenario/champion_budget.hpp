// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <bit>
#include <cstdint>
#include <memory>
#include <utility>

namespace csol::scenario
{
// MP11385C51 consumes on successful creation;1137D8F5 returns on Free,
// guarded by NODRAW. Retain a separate budget so an old lease cannot modify
// a new round's same-numbered champion after its factory has been reset.
struct ChampionBudget { std::int16_t available{}; };
class ChampionLease
{
public:
    explicit ChampionLease(std::shared_ptr<ChampionBudget> budget) : budget_(std::move(budget)) {}
    void Commit()
    {
        if (committed_ || returned_) return;
        committed_ = true;
        budget_->available = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(budget_->available - 1));
    }
    void Return()
    {
        if (returned_) return;
        returned_ = true;
        if (committed_)
            budget_->available = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(budget_->available + 1));
    }
private:
    std::shared_ptr<ChampionBudget> budget_;
    bool committed_{}, returned_{};
};
}
