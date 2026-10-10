#include <metahook.h>
#include <netadr.h>
#include "packet_entities.hpp"
#include "goldcraft/packet_entities.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>

namespace goldcraft::packet_client {
namespace {
struct Buffer {
    const char* name;
    std::uint16_t flags, padding;
    unsigned char* data;
    int maxsize, cursize;
};
static_assert(sizeof(Buffer) == 20 && offsetof(Buffer, data) == 8 && offsetof(Buffer, maxsize) == 12);
struct ChannelHead { int socket; netadr_t remote; };
static_assert(sizeof(netadr_t) == 20 && offsetof(ChannelHead, remote) == 4);
using ChannelSetup = void (__cdecl*)(int, void*, netadr_t, int, void*, int (*)(void*));
metahook_api_t* api = nullptr;
bool installed = false;
std::string error;
Buffer *incoming = nullptr, *message = nullptr;
netadr_t* source = nullptr;
ChannelHead* client_channel = nullptr;
int (__cdecl* original_get_long)(unsigned char*, int, int*) = nullptr;
ChannelSetup original_channel_setup = nullptr;
int (__cdecl* original_queue_packet)(int) = nullptr;
thread_local bool consumed_extension = false;
packet_entities::Reassembler reassembler;
std::mutex reassembly_mutex;
std::atomic<std::uint64_t> completed{0}, rejected{0}, delegated{0};

void* resolve(const char* name, mh_gamesymbol_kind_t kind) {
    void* address = nullptr;
    const auto symbol = std::string("GoldCraftPacket_") + name;
    const auto result = api->ResolveGameSymbol(api->GetEngineBase(), symbol.c_str(), kind, &address);
    if (result != MH_GAMESYMBOL_OK || !address)
        throw std::runtime_error(symbol + ": " + api->GetGameSymbolStatusString(result));
    return address;
}
template<std::size_t N>
void verify(const void* function, std::size_t offset, const std::array<unsigned char, N>& expected) {
    if (std::memcmp(static_cast<const unsigned char*>(function) + offset, expected.data(), N))
        throw std::runtime_error("packet parser/transport preflight instruction changed");
}
std::uint64_t source_key(const netadr_t& address) noexcept {
    // Only address identity participates: the unused IPX bytes of IPv4
    // netadr_t can contain stale data in the engine's original receive path.
    std::uint32_t ip = 0;
    std::memcpy(&ip, address.ip, sizeof(ip));
    return (std::uint64_t(std::uint16_t(address.type)) << 48) |
           (std::uint64_t(address.port) << 32) | ip;
}
void __cdecl channel_setup(int socket, void* channel, netadr_t address, int player,
                           void* connection_status, int (*blocksize)(void*)) {
    if (channel != client_channel) {
        original_channel_setup(socket, channel, address, player, connection_status, blocksize);
        return;
    }
    // A same-address reconnect also starts a fresh split sequence. Serialize
    // the reset and original address assignment with extension reception.
    const std::lock_guard lock(reassembly_mutex);
    reassembler.reset();
    original_channel_setup(socket, channel, address, player, connection_status, blocksize);
}
int __cdecl get_long(unsigned char* data, int size, int* output_size) {
    if (size < 0 || !data || !output_size) { ++rejected; return 0; }
    const auto bytes = std::span<const std::uint8_t>(data, std::size_t(size));
    if (!packet_entities::extended(bytes)) {
        ++delegated;
        return original_get_long(data, size, output_size);
    }
    consumed_extension = true;
    const std::lock_guard lock(reassembly_mutex);
    // NET_QueuePacket passes its original 64 KiB in_message buffer. The
    // subsequent NET_GetPacket copy targets a separate original 64 KiB
    // net_message buffer. Never write an extension into an arbitrary caller
    // buffer, and recheck engine-owned descriptors on every extension.
    if (data != incoming->data || !message->data || message->data == data ||
        incoming->maxsize < int(packet_entities::max_message) ||
        message->maxsize < int(packet_entities::max_message) || source->type != NA_IP ||
        source_key(*source) != source_key(client_channel->remote)) {
        ++rejected;
        return 0;
    }
    const auto result = reassembler.accept(bytes, source_key(*source), double(GetTickCount64()) / 1000.0);
    if (result == packet_entities::Result::rejected) ++rejected;
    if (result != packet_entities::Result::complete) return 0;
    const auto assembled = reassembler.data();
    std::memcpy(data, assembled.data(), assembled.size());
    *output_size = int(assembled.size());
    ++completed;
    return 1;
}
int __cdecl queue_packet(int socket) {
    // The original queue stops after an incomplete split. Drain only our
    // extensions, with room for duplicates and a strict work bound. Native
    // incomplete/empty results retain their original one-poll behavior.
    for (std::size_t i = 0; i < 2 * packet_entities::max_fragments; ++i) {
        consumed_extension = false;
        const auto result = original_queue_packet(socket);
        if (result || !consumed_extension) return result;
    }
    return 0;
}
}

bool install(metahook_api_t* value) {
    if (installed) return true;
    api = value;
    std::uint64_t crc = 0;
    if (!api || api->GetModuleCRC64(api->GetEngineBase(), &crc) != MH_GAMESYMBOL_OK ||
        crc != 0x6ef7192cd8254e2dULL) {
        error = "packet capability requires the verified HL10210 engine";
        return false;
    }
    void *get_long_address = nullptr, *setup_address = nullptr, *queue_address = nullptr;
    try {
        const auto parse = resolve("parse", MH_GAMESYMBOL_KIND_FUNCTION);
        const auto initialize = resolve("network_init", MH_GAMESYMBOL_KIND_FUNCTION);
        const auto publish = resolve("publish", MH_GAMESYMBOL_KIND_FUNCTION);
        get_long_address = resolve("get_long", MH_GAMESYMBOL_KIND_FUNCTION);
        setup_address = resolve("channel_setup", MH_GAMESYMBOL_KIND_FUNCTION);
        queue_address = resolve("queue_packet", MH_GAMESYMBOL_KIND_FUNCTION);
        incoming = static_cast<Buffer*>(resolve("in_message", MH_GAMESYMBOL_KIND_GLOBAL));
        message = static_cast<Buffer*>(resolve("net_message", MH_GAMESYMBOL_KIND_GLOBAL));
        source = static_cast<netadr_t*>(resolve("in_from", MH_GAMESYMBOL_KIND_GLOBAL));
        client_channel = static_cast<ChannelHead*>(resolve("client_channel", MH_GAMESYMBOL_KIND_GLOBAL));
        verify(parse, 0, std::array<unsigned char,9>{0x55,0x8b,0xec,0x81,0xec,0xd0,0,0,0});
        verify(parse, 0xd3, std::array<unsigned char,2>{0x69,0x3d});
        // Relocatable addresses are checked through MetaHook global metadata;
        // the immutable immediate/frame/bitmap/bounds are checked here.
        verify(parse, 0xd9, std::array<unsigned char,4>{0x18,0x43,0,0});
        verify(parse, 0x113, std::array<unsigned char,6>{0x69,0xc3,0x54,0x01,0,0});
        verify(parse, 0x11f, std::array<unsigned char,5>{0x68,0x80,0,0,0});
        for (const auto offset : {0x33d,0x47c,0x62e,0x7f0})
            verify(parse, offset, std::array<unsigned char,6>{0x81,0xfb,0,0x04,0,0});
        verify(publish, 0x11, std::array<unsigned char,7>{0x83,0xbf,0x7c,0x42,0,0,0});
        verify(publish, 0x30, std::array<unsigned char,6>{0x8b,0x97,0,0x43,0,0});
        verify(initialize, 0x26c, std::array<unsigned char,4>{0,0,0x01,0});
        verify(initialize, 0x290, std::array<unsigned char,4>{0,0,0x01,0});
        verify(get_long_address, 0, std::array<unsigned char,7>{0x55,0x8b,0xec,0x51,0x8b,0x45,0x08});
        verify(get_long_address, 0x1b, std::array<unsigned char,3>{0x83,0xff,0x05});
        verify(get_long_address, 0x24, std::array<unsigned char,3>{0x83,0xfe,0x05});
        verify(setup_address, 0, std::array<unsigned char,6>{0x55,0x8b,0xec,0x83,0xec,0x0c});
        verify(setup_address, 0x145, std::array<unsigned char,5>{0x68,0xc0,0x34,0,0});
        verify(setup_address, 0x15e, std::array<unsigned char,4>{0x0f,0x10,0x45,0x10});
        verify(setup_address, 0x16e, std::array<unsigned char,7>{0x0f,0x11,0x46,0x04,0x89,0x46,0x14});
        verify(queue_address, 0, std::array<unsigned char,8>{0x55,0x8b,0xec,0xb8,0x98,0x17,0,0});
        verify(queue_address, 0x17, std::array<unsigned char,3>{0x8b,0x45,0x08});
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
    const auto receive_hook = api->InlineHook(get_long_address, reinterpret_cast<void*>(get_long),
                                              reinterpret_cast<void**>(&original_get_long));
    if (!receive_hook) {
        error = "packet transport hook failed";
        return false;
    }
    const auto setup_hook = api->InlineHook(setup_address, reinterpret_cast<void*>(channel_setup),
                                            reinterpret_cast<void**>(&original_channel_setup));
    if (!setup_hook) {
        api->UnHook(receive_hook);
        error = "packet channel reset hook failed";
        return false;
    }
    if (!api->InlineHook(queue_address, reinterpret_cast<void*>(queue_packet),
                         reinterpret_cast<void**>(&original_queue_packet))) {
        api->UnHook(setup_hook);
        api->UnHook(receive_hook);
        error = "packet receive drain hook failed";
        return false;
    }
    installed = true;
    error.clear();
    reset();
    return true;
}
bool enabled() noexcept { return installed; }
const char* install_error() { return error.c_str(); }
void reset() noexcept { const std::lock_guard lock(reassembly_mutex); reassembler.reset(); }
void write_status(std::ostream& out) {
    out << "\"packetEntities\":{\"enabled\":" << installed
        << ",\"capacity\":" << (installed ? packet_entities::capacity : packet_entities::legacy_capacity)
        << ",\"messageCapacity\":" << (installed ? packet_entities::max_message : 6010)
        << ",\"completedMessages\":" << completed.load(std::memory_order_relaxed)
        << ",\"rejectedFragments\":" << rejected.load(std::memory_order_relaxed)
        << ",\"nativeDelegations\":" << delegated.load(std::memory_order_relaxed)
        << ",\"error\":" << std::quoted(error) << '}';
}
} // namespace goldcraft::packet_client
