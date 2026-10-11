#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include "goldcraft/endpoint.hpp"
#include "goldcraft/shared_bulk.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

namespace goldcraft {
// This friend has no implementation or extra branch in the production transport.
struct SharedBulkTestAccess {
    static std::uint8_t* view(SharedBulk& b) { return b.view_; }
    static HANDLE handle(SharedBulk& b) { return b.handle_; }
    static void seed(SharedBulk& b, std::uint32_t cursor) {
        b.read_cursor_ = cursor;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(b.view_ + 64), std::bit_cast<LONG>(cursor));
        InterlockedExchange(reinterpret_cast<volatile LONG*>(b.view_ + 128), std::bit_cast<LONG>(cursor));
    }
};
}
using namespace goldcraft;
namespace {
std::uint64_t checks = 0;
void require(bool condition) { ++checks; if (!condition) throw std::runtime_error("test condition failed"); }
template<class Fn> void rejects(Fn fn) { bool rejected = false; try { fn(); } catch (const ProtocolError&) { rejected = true; } require(rejected); }
Bytes ack(std::uint32_t accepted = 1) { Writer w; w.u32(1); w.u32(accepted); return w.data; }
Bytes ref(std::uint32_t type, std::uint32_t length, std::uint32_t start, std::uint32_t reserved = 0) {
    Writer w; w.u32(type); w.u32(length); w.u32(start); w.u32(reserved); return w.data;
}
std::unique_ptr<SharedBulk> active() { auto b = SharedBulk::create(42); require(!!b); b->acknowledge(ack()); return b; }
void publish(SharedBulk& b, std::span<const std::uint8_t> bytes, std::uint32_t cursor) {
    auto* view = SharedBulkTestAccess::view(b);
    const auto position = cursor & (bulk_capacity - 1);
    const auto first = std::min(static_cast<std::uint32_t>(bytes.size()), bulk_capacity - position);
    std::memcpy(view + 256 + position, bytes.data(), first);
    if (first < bytes.size()) std::memcpy(view + 256, bytes.data() + first, bytes.size() - first);
    const auto writer = cursor + ((static_cast<std::uint32_t>(bytes.size()) + 7u) & ~7u);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(view + 128), std::bit_cast<LONG>(writer));
}
void layout_and_security() {
    auto b = SharedBulk::create(42); require(!!b);
    auto offer = b->offer(); Reader r(offer);
    require(r.u32() == 1); require(r.u32() == bulk_capacity); require(r.u64() == 42);
    const auto name = r.string(96); r.finish();
    require(name.starts_with("Local\\GoldCraftBulk-") && name.size() == 52);
    auto name2 = SharedBulk::create(42)->offer(); require(offer != name2);
    DWORD flags = 0; require(GetHandleInformation(SharedBulkTestAccess::handle(*b), &flags)); require(!(flags & HANDLE_FLAG_INHERIT));
    PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr;
    require(GetSecurityInfo(SharedBulkTestAccess::handle(*b), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    require(GetSecurityDescriptorControl(descriptor, &control, &revision)); require(control & SE_DACL_PROTECTED);
    require(acl && acl->AceCount == 2);
    HANDLE token = nullptr; require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size); Bytes user(size);
    require(GetTokenInformation(token, TokenUser, user.data(), size, &size)); CloseHandle(token);
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* ace = nullptr; require(GetAce(acl, i, &ace)); auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        require(allowed->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && !(allowed->Header.AceFlags & INHERITED_ACE));
        auto* sid = reinterpret_cast<PSID>(&allowed->SidStart);
        require(IsWellKnownSid(sid, WinLocalSystemSid) || EqualSid(sid, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid));
    }
    LocalFree(descriptor);
    auto handle = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str()); require(handle != nullptr);
    auto* view = MapViewOfFile(handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bulk_capacity + 256); require(view != nullptr);
    require(reinterpret_cast<std::uintptr_t>(view) % 64 == 0);
    UnmapViewOfFile(view); CloseHandle(handle);
    b.reset(); require(OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str()) == nullptr);
}
void data_and_bounds() {
    for (const auto length : {bulk_min_payload, bulk_min_payload + 1, static_cast<std::uint32_t>(max_payload)}) {
        Bytes bytes(length); for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i * 37u + 11u);
        for (const auto cursor : {0u, bulk_capacity - 32u, 0xffffffe0u}) {
            auto b = active(); SharedBulkTestAccess::seed(*b, cursor);
            publish(*b, bytes, cursor);
            auto message = b->consume(ref(66, length, cursor)); require(message.type == Type::entity_frame); require(message.payload == bytes);
            const auto expected = cursor + ((length + 7u) & ~7u);
            Reader reader({SharedBulkTestAccess::view(*b) + 64, 4}); require(reader.u32() == expected);
            // The copy remains valid after the producer reuses the entire mapped data range.
            std::memset(SharedBulkTestAccess::view(*b) + 256, 0xa5, bulk_capacity); require(message.payload == bytes);
        }
    }
    auto b = active(); Bytes payload(bulk_min_payload, 19);
    auto good = ref(66, bulk_min_payload, 0);
    rejects([&] { b->consume(good); }); // bytes not published
    std::memcpy(SharedBulkTestAccess::view(*b) + 256, payload.data(), payload.size());
    rejects([&] { b->consume(good); }); // partial write still not published
    publish(*b, payload, 0);
    rejects([&] { b->consume(ref(66, bulk_min_payload - 1, 0)); });
    rejects([&] { b->consume(ref(66, static_cast<std::uint32_t>(max_payload) + 1, 0)); });
    rejects([&] { b->consume(ref(66, bulk_min_payload, 8)); });
    rejects([&] { b->consume(ref(66, bulk_min_payload, 0, 1)); });
    rejects([&] { b->consume(ref(69, bulk_min_payload, 0)); });
    rejects([&] { b->consume(ref(0x10042, bulk_min_payload, 0)); });
    auto trailing = good; trailing.push_back(0); rejects([&] { b->consume(trailing); });
    for (const auto offset : {0, 4, 8, 12, 16, 24, 68, 127, 132, 255, 64}) {
        auto* view = SharedBulkTestAccess::view(*b); view[offset] ^= 1;
        rejects([&] { b->consume(good); }); view[offset] ^= 1;
    }
    InterlockedExchange(reinterpret_cast<volatile LONG*>(SharedBulkTestAccess::view(*b) + 128), bulk_capacity + 8);
    rejects([&] { b->consume(good); }); publish(*b, payload, 0);
    require(b->consume(good).payload == payload); rejects([&] { b->consume(good); });
    rejects([&] { b->acknowledge(ack()); });
    auto fallback = SharedBulk::create(42); fallback->acknowledge(ack(0)); require(!fallback->enabled());
    rejects([&] { fallback->consume(good); }); rejects([&] { fallback->acknowledge(ack()); });
    auto invalid_ack = SharedBulk::create(42); rejects([&] { invalid_ack->acknowledge(ack(2)); });
}
void handshake_tests() {
    Key session{}, token{};
    auto hello = [&](Role role, std::uint32_t caps) { Writer w; w.bytes(session); w.bytes(token); w.u32(static_cast<std::uint32_t>(role)); w.u32(caps); return w.data; };
    for (auto caps : {0u, 1u}) {
        HostHandshake h(session, token, Role::host_client, Role::fabric_client, 42); auto p = hello(Role::fabric_client, caps);
        h.accept({Type::hello, static_cast<std::uint32_t>(p.size()), 0, 1}, p); require(h.ready() && h.peer_capabilities() == caps);
    }
    for (auto caps : {1u, 2u}) {
        HostHandshake h(session, token, Role::host_server, Role::fabric_server, 42); auto p = hello(Role::fabric_server, caps);
        rejects([&] { h.accept({Type::hello, static_cast<std::uint32_t>(p.size()), 0, 1}, p); });
    }
}
struct Peer {
    Endpoint endpoint;
    SOCKET socket = INVALID_SOCKET;
    Bytes input;
    std::uint64_t epoch = 0, sequence = 0;
    std::vector<Message> received;
    explicit Peer(bool allow = true, std::uint32_t caps = 1, bool server = false) {
        EndpointConfig cfg{}; cfg.shared_bulk = allow;
        if (server) { cfg.host = Role::host_server; cfg.peer = Role::fabric_server; }
        endpoint.start(cfg);
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); require(socket != INVALID_SOCKET);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(endpoint.port());
        require(connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        u_long nonblocking = 1; require(ioctlsocket(socket, FIONBIO, &nonblocking) == 0);
        Writer w; w.bytes(cfg.session); w.bytes(cfg.token); w.u32(static_cast<std::uint32_t>(cfg.peer)); w.u32(caps);
        send(Type::hello, w.data); pump();
    }
    ~Peer() { if (socket != INVALID_SOCKET) closesocket(socket); }
    void send(Type type, std::span<const std::uint8_t> payload) {
        auto frame = encode_frame(type, epoch, ++sequence, payload);
        require(::send(socket, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0) == static_cast<int>(frame.size()));
    }
    void pump() {
        for (int i = 0; i < 15; ++i) {
            endpoint.poll(); std::uint8_t bytes[4096]; int n = recv(socket, reinterpret_cast<char*>(bytes), sizeof(bytes), 0);
            if (n > 0) input.insert(input.end(), bytes, bytes + n);
            std::size_t offset = 0;
            while (input.size() - offset >= 32) {
                auto remaining = std::span<const std::uint8_t>(input).subspan(offset); auto h = decode_header(remaining.first(32));
                if (remaining.size() < 32 + h.payload_bytes) break;
                epoch = h.epoch; auto payload = remaining.subspan(32, h.payload_bytes);
                received.push_back({h.type, Bytes(payload.begin(), payload.end())}); offset += 32 + h.payload_bytes;
            }
            input.erase(input.begin(), input.begin() + offset); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
void endpoint_tests() {
    { Peer peer(true, 0); require(peer.endpoint.connected()); require(peer.received.size() == 1 && peer.received[0].type == Type::welcome);
      peer.send(Type::bulk_ack, ack()); peer.pump(); require(!peer.endpoint.connected()); }
    { Peer peer(false); require(peer.endpoint.connected()); require(peer.received.size() == 1); require(peer.endpoint.transport_stats().mapping_fallback == 1); }
    { Peer peer(true, 0, true); require(peer.endpoint.connected()); require(peer.received.size() == 1); peer.send(Type::bulk_offer, {}); peer.pump(); require(!peer.endpoint.connected()); }
    { Peer peer; require(peer.received.size() == 2 && peer.received[1].type == Type::bulk_offer);
      peer.send(Type::bulk_ref, ref(66, bulk_min_payload, 0)); peer.pump(); require(!peer.endpoint.connected()); }
    { Peer peer; peer.send(Type::bulk_ack, ack()); peer.pump(); require(peer.endpoint.transport_stats().shared_active);
      peer.send(Type::bulk_ack, ack()); peer.pump(); require(!peer.endpoint.connected()); }
    { Peer peer; peer.send(Type::bulk_ack, ack(0)); peer.pump(); require(peer.endpoint.connected() && !peer.endpoint.transport_stats().shared_active);
      peer.send(Type::camera, Bytes{1, 2, 3}); peer.pump(); auto messages = peer.endpoint.take_messages();
      require(messages.size() == 1 && messages[0].type == Type::camera && messages[0].payload == Bytes({1, 2, 3})); }
    {
        Peer peer; Reader offer(peer.received[1].payload); offer.u32(); offer.u32(); offer.u64(); const auto name = offer.string(); offer.finish();
        auto mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str()); require(mapping != nullptr);
        auto* view = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bulk_capacity + 256)); require(view != nullptr);
        peer.send(Type::bulk_ack, ack()); peer.pump();
        // Real Endpoint receives two maximum frames without application draining.
        // A third valid ref stays pending, with sequence/cursor untouched, until drain.
        std::uint32_t cursor = 0;
        for (int frame = 0; frame < 3; ++frame) {
            const auto position = cursor & (bulk_capacity - 1);
            std::memset(view + 256 + position, 31 + frame, max_payload);
            const auto end = cursor + static_cast<std::uint32_t>(max_payload);
            InterlockedExchange(reinterpret_cast<volatile LONG*>(view + 128), std::bit_cast<LONG>(end));
            peer.send(Type::bulk_ref, ref(66, static_cast<std::uint32_t>(max_payload), cursor)); peer.pump();
            cursor = end;
            require(peer.endpoint.connected());
            require(peer.endpoint.transport_stats().shared_received[66].frames == static_cast<std::uint64_t>(std::min(frame + 1, 2)));
        }
        require(peer.endpoint.transport_stats().incoming_bytes == max_queued_bytes);
        Reader reader({view + 64, 4}); require(reader.u32() == bulk_capacity);
        auto first = peer.endpoint.take_messages(); require(first.size() == 2);
        for (std::size_t i = 0; i < first.size(); ++i) {
            require(first[i].type == Type::entity_frame && first[i].payload.size() == max_payload);
            require(std::all_of(first[i].payload.begin(), first[i].payload.end(), [i](auto byte) { return byte == 31 + i; }));
        }
        peer.pump(); auto remaining = peer.endpoint.take_messages(); require(remaining.size() == 1);
        require(std::all_of(remaining[0].payload.begin(), remaining[0].payload.end(), [](auto byte) { return byte == 33; }));
        require(peer.endpoint.transport_stats().incoming_high_water == max_queued_bytes && peer.endpoint.transport_stats().shared_received[66].frames == 3);
        require(peer.endpoint.transport_stats().rejections == 0);
        UnmapViewOfFile(view); CloseHandle(mapping);
    }
}
void lifetime() {
    DWORD before = 0, after = 0; require(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 128; ++i) { auto b = SharedBulk::create(42); require(!!b); }
    require(GetProcessHandleCount(GetCurrentProcess(), &after)); require(before == after);
}
}
int main() {
    try {
        static_assert(sizeof(void*) == 4);
        layout_and_security(); data_and_bounds(); handshake_tests(); endpoint_tests(); lifetime();
        std::cout << "PASS " << checks << " checks; x86 actual Windows mapping/security/lifetime/protocol\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
