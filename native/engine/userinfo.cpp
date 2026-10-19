#include "precompiled.h"
#include "userinfo.h"
#include "goldcraft/userinfo.hpp"
#include <array>

namespace {
struct Entry { char* legacy = nullptr; goldcraft::userinfo::Storage storage; };
std::array<Entry, 32> entries{};
std::array<unsigned, 32> pending{};
cvar_t capacityCvar = {"gc_userinfo_capacity", "1024", FCVAR_SERVER};

Entry* find(const char* buffer) {
    for (auto& entry : entries) if (entry.legacy == buffer && buffer) return &entry;
    return nullptr;
}
}

void GC_UserInfoInit() { Cvar_RegisterVariable(&capacityCvar); }
void GC_UserInfoForget(char* legacy) {
    if (auto* entry = find(legacy)) *entry = {};
    for (int i = 0; i < g_psvs.maxclients && i < 32; ++i) if (g_psvs.clients[i].userinfo == legacy) {
        pending[i] = 0;
        for (auto& receiver : pending) receiver &= ~(1u << i);
    }
}
void GC_UserInfoReset() { entries = {}; pending = {}; }
void GC_UserInfoBegin(char* legacy) {
    GC_UserInfoForget(legacy);
    if (Q_strcmp(Info_ValueForKey(legacy, "_gcu"), "1")) return;
    for (auto& entry : entries) if (!entry.legacy) {
        if (!entry.storage.assign(legacy)) return;
        entry.legacy = legacy;
        entry.storage.publish(legacy);
        return;
    }
}
bool GC_UserInfoExtended(const char* legacy) { return find(legacy) != nullptr; }
const char* GC_UserInfoRead(const char* legacy) {
    if (auto* entry = find(legacy)) {
        entry->storage.reconcile(legacy);
        return entry->storage.full.data();
    }
    return legacy;
}
GC_UserInfoMutation::GC_UserInfoMutation(char* input) : legacy(input), data(input) {
    if (auto* entry = find(input)) {
        entry->storage.reconcile(input);
        data = entry->storage.full.data();
    }
}
GC_UserInfoMutation::~GC_UserInfoMutation() {
    if (data != legacy) if (auto* entry = find(legacy)) entry->storage.publish(legacy);
}

bool GC_UserInfoDefer(client_t* source, sizebuf_t* message, std::size_t bytes) {
    if (message->cursize >= 0 && message->cursize <= message->maxsize &&
        bytes <= std::size_t(message->maxsize - message->cursize)) return false;
    const auto slot = source - g_psvs.clients;
    if (slot < 0 || slot >= 32) return false;
    for (int i = 0; i < g_psvs.maxclients && i < 32; ++i) {
        if (message == &g_psvs.clients[i].netchan.message) {
            // Coalesce to the most recent state; never retain an old user's
            // bytes after disconnect or reuse of the same player slot.
            pending[i] |= 1u << slot;
            return true;
        }
    }
    // Signon uses its own large fragment buffer, with stock overflow rules.
    return false;
}

void GC_UserInfoFlush() {
    auto* previous = host_client;
    for (int receiver = 0; receiver < g_psvs.maxclients && receiver < 32; ++receiver) {
        if (!pending[receiver]) continue;
        host_client = &g_psvs.clients[receiver];
        if (!host_client->connected || host_client->fakeclient) { pending[receiver] = 0; continue; }
        unsigned sent = 0;
        for (int source = 0; source < g_psvs.maxclients && source < 32 && sent < 2; ++source) {
            const auto bit = 1u << source;
            if (!(pending[receiver] & bit)) continue;
            pending[receiver] &= ~bit;
            SV_FullClientUpdate(&g_psvs.clients[source], &host_client->netchan.message);
            if (pending[receiver] & bit) break;
            ++sent;
        }
    }
    host_client = previous;
}
