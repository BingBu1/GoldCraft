// SPDX-License-Identifier: GPL-3.0-or-later
#include "refund_cap.hpp"

namespace csol::scenario::custom
{
bool RefundCap::Valid(const RefundPair& pair) const
{
    return valid_ && pair.monsterSlot < MonsterCount && pair.lease &&
        pair.player.slot > 0 && pair.player.slot < PlayerCount && pair.player.generation;
}
std::optional<int> RefundCap::Remaining(const RefundPair& pair)
{
    if (!Valid(pair)) return {};
    auto& monster = monsters_[pair.monsterSlot];
    if (pair.lease < monster.lease) return {};
    if (pair.lease != monster.lease)
    {
        monster = {};
        monster.lease = pair.lease;
    }
    auto& player = monster.players[pair.player.slot];
    if (pair.player.generation < player.generation) return {};
    if (pair.player.generation != player.generation) player = {pair.player.generation, 0};
    return limit_ - player.credited;
}
bool RefundCap::Commit(const RefundPair& pair, int credited)
{
    if (!Valid(pair) || credited < 0) return false;
    auto& monster = monsters_[pair.monsterSlot];
    auto& player = monster.players[pair.player.slot];
    if (monster.lease != pair.lease || player.generation != pair.player.generation ||
        credited > limit_ - player.credited) return false;
    player.credited += credited;
    return true;
}
}
