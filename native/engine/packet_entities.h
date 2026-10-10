#pragma once
#include "goldcraft/packet_entities.hpp"

// Included after the engine's precompiled header; no public engine structure
// grows. Unknown clients and proxies retain their original transport contract.
inline bool GoldCraft_PacketEntitiesEnabled(const client_t *client) {
    return client && !client->proxy && client->netchan.remote_address.type == NA_IP &&
        !Q_strcmp(Info_ValueForKey(client->userinfo,
        goldcraft::packet_entities::capability), goldcraft::packet_entities::capability_value);
}
inline bool GoldCraft_PacketChannelEnabled(const netchan_t *channel) {
    if (!channel || channel->sock != NS_SERVER || channel->player_slot < 1 ||
        channel->player_slot > g_psvs.maxclients || !g_psvs.clients) return false;
    const auto *client = &g_psvs.clients[channel->player_slot - 1];
    return &client->netchan == channel && GoldCraft_PacketEntitiesEnabled(client);
}
inline bool GoldCraft_PacketAddressEnabled(netadr_t address) {
    for (int i = 0; i < g_psvs.maxclients; ++i) {
        auto *client = &g_psvs.clients[i];
        if (client->active && NET_CompareAdr(client->netchan.remote_address, address) &&
            GoldCraft_PacketEntitiesEnabled(client)) return true;
    }
    return false;
}
// Reserve the maximum existing reliable payload plus both stream headers, so
// a complete snapshot and its same-frame identities fit one bounded message.
constexpr int GoldCraft_PacketDatagramBytes =
    int(goldcraft::packet_entities::max_message) - MAX_MSGLEN - HEADER_BYTES;
static_assert(GoldCraft_PacketDatagramBytes > MAX_DATAGRAM);
