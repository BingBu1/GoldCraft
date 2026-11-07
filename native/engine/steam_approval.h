// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
struct client_s;

constexpr bool GC_SteamApprovalEnabled() {
#if defined(GOLDCRAFT_STEAM_APPROVAL) && GOLDCRAFT_STEAM_APPROVAL
    return true;
#else
    return false;
#endif
}
void GC_SteamApprovalInit();
void GC_SteamApprovalClear();
void GC_SteamApprovalBegin(client_s* client);
void GC_SteamApprovalTicket(client_s* client, std::uint64_t account, bool accepted, bool lan);
void GC_SteamApprovalApproved(client_s* client, std::uint64_t account, bool lan);
void GC_SteamApprovalRevoke(client_s* client);
void GC_SteamApprovalDeny(std::uint64_t account);
