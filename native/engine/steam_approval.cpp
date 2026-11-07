// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "steam_approval.h"
#include "steam_approval_registry.h"

namespace {
namespace approval = goldcraft::steamapproval;
approval::Registry approvals;

unsigned Slot(const client_t* client)
{
    if (!client || !g_psvs.clients) return 0;
    for (int index = 0; index < g_psvs.maxclients && index < 32; ++index)
        if (&g_psvs.clients[index] == client) return static_cast<unsigned>(index + 1);
    return 0;
}

bool Eligible(const client_t* client, bool lan, bool connected)
{
    return client && !lan && sv_lan.value == 0 && !client->fakeclient && !client->proxy &&
        client->userid > 0 && client->network_userid.idtype == AUTH_IDTYPE_STEAM &&
        (!connected || client->connected || client->active || client->spawned) &&
        approval::ValidPublicAccount(client->network_userid.m_SteamID);
}

int GC_STEAM_APPROVAL_CALL GetApproved(std::uint32_t slot, std::int32_t userid,
    approval::Identity* output, std::uint32_t outputBytes)
{
    if (!output || outputBytes != sizeof(*output)) return 0;
    *output = {};
    if (!slot || slot > 32 || slot > static_cast<unsigned>(g_psvs.maxclients) || !g_psvs.clients) return 0;
    auto* steam = Steam3Server();
    const auto* client = &g_psvs.clients[slot - 1];
    if (!steam || !steam->BLoggedOn() || !Eligible(client, steam->BLanOnly(), true) || client->userid != userid)
        return 0;
    return approvals.Get(slot, userid, client->network_userid.m_SteamID, *output) ? 1 : 0;
}

int GC_STEAM_APPROVAL_CALL IsCurrent(const approval::Identity* identity, std::uint32_t identityBytes)
{
    if (!identity || identityBytes != sizeof(*identity) || identity->size != sizeof(*identity) || identity->reserved)
        return 0;
    approval::Identity current;
    return GetApproved(identity->slot, identity->userid, &current, sizeof(current)) && current == *identity;
}

approval::Api api{approval::ApiVersion, sizeof(approval::Api), GetApproved, IsCurrent};
}

void GC_SteamApprovalInit()
{
    if (!GC_SteamApprovalEnabled()) return;
    approvals.Clear();
    g_RehldsApiFuncs.RegisterPluginApi(approval::ApiName, &api);
}

void GC_SteamApprovalClear() { approvals.Clear(); }

void GC_SteamApprovalBegin(client_t* client)
{
    const auto slot = Slot(client);
    if (slot) approvals.Begin(slot, client->userid);
}

void GC_SteamApprovalTicket(client_t* client, std::uint64_t account, bool accepted, bool lan)
{
    const auto slot = Slot(client);
    if (!slot) return;
    if (!accepted || !Eligible(client, lan, false) || client->network_userid.m_SteamID != account ||
        !approvals.TicketAccepted(slot, client->userid, approvals.Connection(slot, client->userid), account))
        approvals.Revoke(slot);
}

void GC_SteamApprovalApproved(client_t* client, std::uint64_t account, bool lan)
{
    const auto slot = Slot(client);
    if (!slot) return;
    if (!Eligible(client, lan, true) || client->network_userid.m_SteamID != account ||
        !approvals.Approved(slot, client->userid, approvals.Connection(slot, client->userid), account))
        approvals.Revoke(slot);
}

void GC_SteamApprovalRevoke(client_t* client) { approvals.Revoke(Slot(client)); }
void GC_SteamApprovalDeny(std::uint64_t account) { approvals.RevokeAccount(account); }
