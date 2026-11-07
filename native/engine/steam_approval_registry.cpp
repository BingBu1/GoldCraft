// SPDX-License-Identifier: GPL-3.0-or-later
#include "steam_approval_registry.h"
#include <limits>

namespace goldcraft::steamapproval {
bool ValidPublicAccount(std::uint64_t account)
{
    return (account >> 56) == 1 && ((account >> 52) & 15) == 1 &&
        ((account >> 32) & 0xfffff) == 1 && static_cast<std::uint32_t>(account) != 0;
}

std::uint64_t Registry::Begin(unsigned slot, int userid)
{
    if (!slot || slot >= entries_.size()) return 0;
    entries_[slot] = {};
    if (userid <= 0 || serial_ == std::numeric_limits<std::uint64_t>::max()) return 0;
    const auto connection = ++serial_;
    entries_[slot].identity = {sizeof(Identity), slot, userid, 0, 0, connection};
    return connection;
}

std::uint64_t Registry::Connection(unsigned slot, int userid) const
{
    if (!slot || slot >= entries_.size() || userid <= 0) return 0;
    const auto& identity = entries_[slot].identity;
    return identity.userid == userid ? identity.connection : 0;
}

bool Registry::TicketAccepted(unsigned slot, int userid, std::uint64_t connection,
    std::uint64_t account)
{
    if (!connection || Connection(slot, userid) != connection || !ValidPublicAccount(account)) return false;
    auto& entry = entries_[slot];
    if (entry.ticketAccepted || entry.approved) return false;
    entry.identity.account = account;
    entry.ticketAccepted = true;
    return true;
}

bool Registry::Approved(unsigned slot, int userid, std::uint64_t connection,
    std::uint64_t account)
{
    if (!connection || Connection(slot, userid) != connection || !ValidPublicAccount(account)) return false;
    auto& entry = entries_[slot];
    if (!entry.ticketAccepted || entry.identity.account != account) return false;
    entry.approved = true;
    return true;
}

void Registry::Revoke(unsigned slot)
{
    if (slot && slot < entries_.size()) entries_[slot] = {};
}

void Registry::RevokeAccount(std::uint64_t account)
{
    if (!account) return;
    for (auto& entry : entries_)
        if (entry.identity.account == account) entry = {};
}

void Registry::Clear() { entries_ = {}; }

bool Registry::Get(unsigned slot, int userid, std::uint64_t currentAccount, Identity& output) const
{
    output = {};
    if (!Connection(slot, userid)) return false;
    const auto& entry = entries_[slot];
    if (!entry.approved || entry.identity.account != currentAccount || !ValidPublicAccount(currentAccount)) return false;
    output = entry.identity;
    return true;
}
}
