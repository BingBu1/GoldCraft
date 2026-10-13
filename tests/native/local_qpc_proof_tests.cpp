#include <windows.h>
#include <aclapi.h>
#include "goldcraft/local_qpc_proof.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace goldcraft;
namespace {
int checks = 0;
void require(bool value) { ++checks; if (!value) throw std::runtime_error("local clock check failed"); }
template<class F> void rejects(F action) {
    bool failed = false;
    try { action(); } catch (const ProtocolError&) { failed = true; }
    require(failed);
}
struct Opened {
    std::uint64_t epoch = 0, frequency = 0, issued = 0, deadline = 0;
    Key challenge{};
    std::string name;
    HANDLE handle = nullptr;
    void* view = nullptr;
    explicit Opened(const LocalQpcProof& proof) {
        const auto bytes = proof.offer(); Reader offer(bytes);
        require(offer.u32() == 1 && offer.u32() == 80);
        epoch = offer.u64(); frequency = offer.u64(); issued = offer.u64(); deadline = offer.u64();
        name = offer.string(96); offer.finish();
        require(name.starts_with("Local\\GoldCraftClock-") && name.size() == 53);
        handle = OpenFileMappingA(FILE_MAP_READ | READ_CONTROL, FALSE, name.c_str());
        require(handle != nullptr);
        view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 80); require(view != nullptr);
        Reader mapped({static_cast<const std::uint8_t*>(view), 80});
        require(mapped.u32() == LocalQpcProof::magic && mapped.u32() == 1);
        require(mapped.u32() == 80 && mapped.u32() == 0);
        require(mapped.u64() == epoch && mapped.u64() == frequency && mapped.u64() == issued && mapped.u64() == deadline);
        challenge = mapped.key(); require(challenge != Key{}); require(mapped.key() == Key{}); mapped.finish();
        require(std::search(bytes.begin(), bytes.end(), challenge.begin(), challenge.end()) == bytes.end());
    }
    ~Opened() { if (view) UnmapViewOfFile(view); if (handle) CloseHandle(handle); }
    Bytes response(bool accepted = true) const {
        Writer w; w.u32(1); w.u32(accepted ? 1 : 0); w.u64(epoch);
        w.u64(accepted ? frequency : 0); w.bytes(accepted ? challenge : Key{}); return w.data;
    }
};
void local_mapping_and_proof() {
    rejects([] { LocalQpcProof::create(0); });
    auto proof = LocalQpcProof::create(42); require(proof != nullptr);
    std::string name;
    {
        Opened mapping(*proof); name = mapping.name;
        require(mapping.frequency > 0 && mapping.deadline - mapping.issued == mapping.frequency * 3);
        SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
        PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr;
        require(GetSecurityInfo(mapping.handle, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS);
        require(GetSecurityDescriptorControl(descriptor, &control, &revision));
        require((control & SE_DACL_PROTECTED) && acl && acl->AceCount == 2);
        HANDLE token = nullptr; require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
        DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size); Bytes user(size);
        require(GetTokenInformation(token, TokenUser, user.data(), size, &size)); CloseHandle(token);
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* entry = nullptr; require(GetAce(acl, i, &entry));
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
            require(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && !(ace->Header.AceFlags & INHERITED_ACE));
            auto* sid = const_cast<DWORD*>(&ace->SidStart);
            require(IsWellKnownSid(sid, WinLocalSystemSid) || EqualSid(sid, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid));
        }
        LocalFree(descriptor);
        require(proof->acknowledge(mapping.response()));
        rejects([&] { proof->acknowledge(mapping.response()); });
    }
    proof.reset();
    auto remaining = OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str());
    if (remaining) CloseHandle(remaining);
    require(remaining == nullptr);
}
void invalid_responses() {
    for (int mode = 0; mode < 8; ++mode) {
        auto proof = LocalQpcProof::create(123); require(proof != nullptr);
        Opened mapping(*proof);
        auto response = mapping.response();
        switch (mode) {
        case 0: response[0] = 2; break;
        case 1: response[4] = 2; break;
        case 2: response[8] ^= 1; break;
        case 3: response[16] ^= 1; break;
        case 4: response[24] ^= 1; break;
        case 5: response.pop_back(); break;
        case 6: response.push_back(0); break;
        case 7: response[4] = 0; break; // Decline with retained frequency/challenge.
        }
        rejects([&] { proof->acknowledge(response); });
    }
    auto proof = LocalQpcProof::create(123); require(proof != nullptr);
    Opened mapping(*proof);
    require(!proof->acknowledge(mapping.response(false)));
    rejects([&] { proof->acknowledge(mapping.response()); });

    auto other = LocalQpcProof::create(123); require(other != nullptr);
    Opened next(*other); require(mapping.name != next.name && mapping.challenge != next.challenge);
    rejects([&] { other->acknowledge(mapping.response()); }); // Same epoch/frequency is insufficient.
}
void handshake_roles_and_wire() {
    Key session{1}, token{2};
    for (auto peer : {Role::fabric_client, Role::fabric_server}) {
        const auto host = peer == Role::fabric_client ? Role::host_client : Role::host_server;
        const auto allowed = peer == Role::fabric_client ? capability_shared_bulk_v1 : capability_local_qpc_v1;
        for (std::uint32_t capability : {0u, 1u, 2u, 3u, 4u}) {
            HostHandshake handshake(session, token, host, peer, 77);
            Writer hello; hello.bytes(session); hello.bytes(token); hello.u32(static_cast<std::uint32_t>(peer)); hello.u32(capability);
            auto frame = encode_frame(Type::hello, 0, 1, hello.data);
            auto header = decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
            if (capability & ~allowed) rejects([&] { handshake.accept(header, hello.data); });
            else {
                handshake.accept(header, hello.data); require(handshake.ready());
                require(handshake.peer_capabilities() == capability);
                auto welcomeBytes = handshake.welcome(); Reader welcome(welcomeBytes);
                require(welcome.key() == session && welcome.u32() == static_cast<std::uint32_t>(host));
                require(welcome.u32() == 0); // Original welcome remains compatible.
            }
        }
    }
    for (auto type : {Type::clock_offer, Type::clock_ack, Type::clock_ready}) {
        require(known_type(type) && clock_internal(type));
        auto frame = encode_frame(type, 77, 2, {});
        require(decode_header(frame).type == type);
    }
    require(!clock_internal(Type::heartbeat) && !clock_internal(Type::bulk_ref));
}
}
int main() {
    try { local_mapping_and_proof(); invalid_responses(); handshake_roles_and_wire(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << checks << " local QPC proof checks passed; no Java/Endpoint/FML gameplay evidence\n";
}
