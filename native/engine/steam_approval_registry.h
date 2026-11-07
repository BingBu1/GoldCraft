// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../include/goldcraft/steam_approval_api.hpp"
#include <array>

namespace goldcraft::steamapproval {
bool ValidPublicAccount(std::uint64_t account);

// Main-thread engine state. Begin/TicketAccepted are insufficient: Approved
// must only follow the native successful GSClientApprove branch. The public
// plugin API deliberately exposes none of these mutation methods.
class Registry final {
public:
    std::uint64_t Begin(unsigned slot, int userid);
    bool TicketAccepted(unsigned slot, int userid, std::uint64_t connection,
        std::uint64_t account);
    bool Approved(unsigned slot, int userid, std::uint64_t connection,
        std::uint64_t account);
    void Revoke(unsigned slot);
    void RevokeAccount(std::uint64_t account);
    void Clear(); // Connection serial is preserved across Steam restarts.
    std::uint64_t Connection(unsigned slot, int userid) const;
    bool Get(unsigned slot, int userid, std::uint64_t currentAccount, Identity& output) const;
private:
    struct Entry { Identity identity{}; bool ticketAccepted{}, approved{}; };
    std::array<Entry, 33> entries_{};
    std::uint64_t serial_{};
};
}
