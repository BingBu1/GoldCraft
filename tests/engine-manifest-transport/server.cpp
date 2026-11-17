// SPDX-License-Identifier: GPL-3.0-or-later
// Actual ReHLDS types, writers, hook registries and native BZ2 fragment queue.
#include "precompiled.h"
#include "precache.h"
#include "reliable_message_trace.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <source_location>
#include <stdexcept>
#include "bit-storage-under-test.inc"

namespace fixture {
unsigned checks{}, sendHooks{}, consistencyHooks{};
bool sync{}, ready{true}, dropped{};
std::set<void*> allocations;
std::vector<goldcraft::resource_sync::Entry> entries;
struct Registries {
    CRehldsHookRegistry_SV_SendResources m_SV_SendResources;
    CRehldsHookRegistry_SV_ShouldSendConsistencyList m_SV_ShouldSendConsistencyList;
} hooks;
void Check(bool value, const std::source_location at = std::source_location::current()) {
    ++checks;
    if (!value) throw std::runtime_error("transport check at line " + std::to_string(at.line()));
}
}

GoldCraftPrecache gc_precache;
server_t g_psv{};
server_static_t g_psvs{};
client_t* host_client{};
cvar_t sv_downloadurl{};
double realtime{};
ALIGN16 bf_write_t bfwrite{};
static client_t* manifestClient{};
static unsigned manifestFirst{};

void Sys_Error(const char* text, ...) { throw std::runtime_error(text); }
void Con_Printf(const char*, ...) {}
void Con_DPrintf(const char*, ...) {}
void* Mem_ZeroMalloc(size_t count) {
    auto* memory = std::calloc(1, count);
    if (!memory) throw std::bad_alloc();
    fixture::Check(fixture::allocations.insert(memory).second);
    return memory;
}
void Mem_Free(void* memory) {
    fixture::Check(fixture::allocations.erase(memory) == 1);
    std::free(memory);
}
qboolean Netchan_CreateFileFragments_(qboolean, netchan_t*, const char*) {
    throw std::runtime_error("unexpected file stream in manifest queue");
}
bool GC_ResourceSyncClient(const client_t*) { return fixture::sync; }
// This fixture begins after the unchanged client capability handshake.
bool GC_PrecacheClient(const client_t* client) { return client != nullptr; }
bool GC_ResourceSyncReady() { return fixture::ready; }
std::uint64_t GC_ResourceSyncGeneration() { return 0x1122334455667788ull; }
const goldcraft::resource_sync::Entry* GC_ResourceSyncEntry(std::size_t index) {
    return index < fixture::entries.size() ? &fixture::entries[index] : nullptr;
}
void SV_DropClient(client_t*, qboolean, const char*, ...) { fixture::dropped = true; }
IGameClient* GetRehldsApiClient(client_t*) { return nullptr; }
bool SV_ShouldSendConsistencyList_mod(IGameClient*, bool) { return g_psv.num_consistency != 0; }

// Explicit host allocation/fatal-policy boundaries above are never used to
// replace a writer, queue, hook registry, compression codec or sender.
#define g_RehldsHookchains fixture::hooks
#include "wire-under-test.inc"
#include "netchan-under-test.inc"
#include "sender-under-test.inc"
#include "legacy-sender-under-test.inc"
#undef g_RehldsHookchains

namespace fixture {
using Bytes = std::vector<byte>;
using Packets = std::vector<Bytes>;

void ResourceHook(IRehldsHook_SV_SendResources* chain, sizebuf_t* message) {
    ++sendHooks;
    MSG_WriteByte(message, svc_nop);
    chain->callNext(message);
    MSG_WriteByte(message, svc_nop);
    MSG_WriteByte(&host_client->netchan.message, svc_stufftext);
    MSG_WriteString(&host_client->netchan.message, "resource hook\n");
}
bool ConsistencyHook(IRehldsHook_SV_ShouldSendConsistencyList* chain, IGameClient* client, bool force) {
    ++consistencyHooks;
    MSG_WriteByte(&host_client->netchan.message, svc_stufftext);
    MSG_WriteString(&host_client->netchan.message, "consistency hook\n");
    return chain->callNext(client, force);
}

void Fill(unsigned total, bool maximum) {
    gc_precache.resources.clear(); entries.clear();
    std::uint32_t random = 0x3475123;
    const auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return byte(random); };
    for (unsigned i = 0; i < total; ++i) {
        resource_t resource{};
        const auto number = std::to_string(i);
        const std::string name = "gfx/" + std::string((maximum ? 55u : 20u) - number.size(), 'a') + number + ".tga";
        Check(name.size() < sizeof(resource.szFileName));
        std::memcpy(resource.szFileName, name.c_str(), name.size() + 1);
        resource.type = t_generic;
        resource.nIndex = int(i + 65536);
        resource.nDownloadSize = i % 7 ? 127 : 0x1ffffff;
        resource.ucFlags = byte((i % 8) | (i % 97 == 0 ? RES_CHECKFILE : 0));
        for (auto& c : resource.rgucMD5_hash) c = next();
        if (maximum || i % 2) for (auto& c : resource.rguc_reserved) c = next();
        goldcraft::resource_sync::Entry entry;
        entry.required = (resource.ucFlags & RES_CUSTOM) == 0;
        entry.size = resource.nDownloadSize;
        for (auto& c : entry.sha256) c = next();
        gc_precache.resources.push_back(resource); entries.push_back(entry);
    }
    g_psv.num_resources = int(total);
    g_psv.num_consistency = total ? 1 : 0;
}

Packets Drain(netchan_t& channel) {
    Packets packets;
    for (;;) {
        Netchan_FragSend(&channel);
        auto* fragment = channel.fragbufs[FRAG_NORMAL_STREAM];
        if (!fragment) break;
        Bytes encoded;
        int ordinal = 0;
        while (fragment) {
            Check(fragment->bufferid == ++ordinal && fragment->frag_message.cursize <= 1024);
            encoded.insert(encoded.end(), fragment->frag_message.data,
                           fragment->frag_message.data + fragment->frag_message.cursize);
            auto* next = fragment->next; Mem_Free(fragment); fragment = next;
        }
        Check(ordinal == channel.fragbufcount[FRAG_NORMAL_STREAM]);
        channel.fragbufs[FRAG_NORMAL_STREAM] = nullptr;
        channel.fragbufcount[FRAG_NORMAL_STREAM] = 0;
        if (encoded.size() >= 4 && std::memcmp(encoded.data(), "BZ2", 4) == 0) {
            Bytes decoded(NET_MAX_PAYLOAD);
            unsigned size = unsigned(decoded.size());
            Check(BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(decoded.data()), &size,
                reinterpret_cast<char*>(encoded.data() + 4), unsigned(encoded.size() - 4), 0, 0) == BZ_OK);
            decoded.resize(size); encoded = std::move(decoded);
        }
        Check(!encoded.empty() && encoded.size() <= NET_MAX_PAYLOAD);
        packets.push_back(std::move(encoded));
    }
    Check(!channel.waitlist[FRAG_NORMAL_STREAM] && !channel.waitlist[FRAG_FILE_STREAM]);
    Check(allocations.empty());
    return packets;
}

Packets Send(bool legacy, unsigned total, bool maximum, bool withResourceHook, bool withConsistencyHook) {
    Fill(total, maximum); Check(allocations.empty());
    auto client = std::make_unique<client_t>(); host_client = client.get();
    auto& channel = client->netchan;
    channel.message.data = channel.message_buf; channel.message.maxsize = sizeof(channel.message_buf);
    channel.message.buffername = "fixture reliable"; channel.message.flags = SIZEBUF_CHECK_OVERFLOW;
    channel.pfnNetchan_Blocksize = [](void*) { return 1024; };
    MSG_WriteByte(&channel.message, svc_nop); // Existing reliable data stays first.
    if (withResourceHook) hooks.m_SV_SendResources.registerHook(ResourceHook, 0);
    if (withConsistencyHook) hooks.m_SV_ShouldSendConsistencyList.registerHook(ConsistencyHook, 0);
    sendHooks = consistencyHooks = 0;
    if (legacy) Legacy_SendResourceManifest(client.get());
#ifdef VERIFY_FORMER_SENDER
    else Legacy_SendResourceManifest(client.get());
#else
    else GC_SendResourceManifest(client.get());
#endif
    Check(!manifestClient && manifestFirst == 0 && !channel.message.cursize);
    Check(sendHooks == (withResourceHook ? (std::max)(1u, (total + 127) / 128) : 0u));
    Check(consistencyHooks == (withConsistencyHook ? 1u : 0u));
    if (withResourceHook) hooks.m_SV_SendResources.unregisterHook(ResourceHook);
    if (withConsistencyHook) hooks.m_SV_ShouldSendConsistencyList.unregisterHook(ConsistencyHook);
    auto result = Drain(channel); host_client = nullptr; return result;
}

Bytes Concatenate(const Packets& packets) {
    Bytes all;
    for (const auto& packet : packets) all.insert(all.end(), packet.begin(), packet.end());
    return all;
}
void Write(const std::filesystem::path& path, const Packets& packets) {
    std::ofstream out(path, std::ios::binary); Check(bool(out));
    for (const auto& packet : packets) {
        const std::uint32_t size = unsigned(packet.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(packet.data()), packet.size());
    }
    Check(bool(out));
}
void WriteExpected(const std::filesystem::path& path, bool v4) {
    std::ofstream out(path, std::ios::binary); Check(bool(out));
    const auto number = [&](std::uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
    number(unsigned(gc_precache.resources.size()));
    for (std::size_t i = 0; i < gc_precache.resources.size(); ++i) {
        const auto& r = gc_precache.resources[i]; const auto& e = entries[i];
        number(r.type); number(r.nIndex);
        number(v4 && e.required ? e.size : (std::min)(unsigned(r.nDownloadSize), 0xffffffu));
        number(r.ucFlags & (RES_FATALIFMISSING | RES_CUSTOM));
        out.write(r.szFileName, sizeof(r.szFileName));
        out.write(reinterpret_cast<const char*>(r.rgucMD5_hash), sizeof(r.rgucMD5_hash));
        out.write(reinterpret_cast<const char*>(r.rguc_reserved), sizeof(r.rguc_reserved));
        number(v4 && e.required); number(e.size);
        out.write(reinterpret_cast<const char*>(e.sha256.data()), e.sha256.size());
    }
    Check(bool(out));
}
}

int main(int argc, char** argv) {
    using namespace fixture;
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "";
        if (!output.empty()) std::filesystem::create_directories(output);
        sv_downloadurl.string = const_cast<char*>("http://127.0.0.1:28080/goldcraft/");
        g_psvs.spawncount = 0x12345678;
        for (bool v4 : {false, true}) for (bool maximum : {false, true}) {
            sync = v4;
            for (unsigned total : {0u, 1u, 127u, 128u, 129u, 512u, 4057u, 16384u}) {
                const auto old = Send(true, total, maximum, false, false);
                const auto current = Send(false, total, maximum, false, false);
                Check(Concatenate(old) == Concatenate(current));
                Check(current.size() <= old.size());
                if (total >= 512) {
                    if (current.size() >= old.size())
                        throw std::runtime_error("former sender keeps one reliable group per 128 entries");
                    Check(current.size() < old.size());
                }
                if (total >= 4057) std::cout << "v" << (v4 ? 4 : 3) << " files=" << total
                    << " maximum=" << maximum << " oldGroups=" << old.size() - 1
                    << " newGroups=" << current.size() - 1 << '\n';
                if (!output.empty() && total == 4057 && !maximum) {
                    const auto stem = v4 ? "v4" : "v3";
                    Write(output / (std::string(stem) + ".packets"), current);
                    WriteExpected(output / (std::string(stem) + ".expected"), v4);
                }
            }
        }
        for (bool resource : {false, true}) for (bool consistency : {false, true}) {
            if (!resource && !consistency) continue;
            for (bool v4 : {false, true}) {
                sync = v4;
                const auto old = Send(true, 4057, false, resource, consistency);
                const auto current = Send(false, 4057, false, resource, consistency);
                Check(old == current); // Exact native reliable boundaries, not just a byte stream.
            }
        }
        ready = false; dropped = false;
        client_t client{}; GC_SendResourceManifest(&client);
        Check(dropped && !manifestClient && allocations.empty());
        std::cout << "manifest transport checks=" << checks << " failures=0\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
