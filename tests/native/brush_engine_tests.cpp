// Exact copied hw.dll packet publication; no DllMain or game/desktop startup.
#include <metahook.h>
#include <cl_entity.h>
#include <com_model.h>
#include <bcrypt.h>
#include "goldcraft/brush_identity.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace goldcraft;
namespace {
model_t model{};
model_t *model_for_index(int index) { assert(index == 77); return &model; }
void no_animation() {}
void patch(unsigned char *at, const void *replacement) {
    DWORD old = 0, restored = 0;
    assert(VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old));
    at[0] = 0xe9;
    const auto relative = std::uint32_t(reinterpret_cast<std::uintptr_t>(replacement) -
                                      reinterpret_cast<std::uintptr_t>(at + 5));
    std::memcpy(at + 1, &relative, 4);
    assert(VirtualProtect(at, 5, old, &restored));
    assert(FlushInstructionCache(GetCurrentProcess(), at, 5));
}
brush::Receiver receiver;
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    std::ifstream file(argv[1], std::ios::binary);
    const Bytes bytes{std::istreambuf_iterator<char>(file), {}};
    std::array<unsigned char, 32> hash{};
    assert(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                      static_cast<ULONG>(bytes.size()), hash.data(), ULONG(hash.size())) == 0);
    constexpr unsigned char expected[]{0x9b,0xa9,0xa2,0xdb,0x5e,0x07,0x59,0x8f,0xd5,0x9a,0xfa,0x35,0x50,0x7a,0x98,0xc8,
                                      0x61,0x62,0xe4,0xe1,0x5b,0x38,0x35,0x17,0x7b,0x78,0xc1,0x18,0x42,0xcd,0x22,0x95};
    assert(std::equal(hash.begin(), hash.end(), expected));
    const auto image = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    assert(image);
    const auto base = reinterpret_cast<unsigned char *>(image);
    // These verified RVAs are used only in this hash-locked test, never hooks
    // shipped to a client. Stub unrelated model lookup/animation side effects;
    // execute the original state stamp, prevstate/curstate copies and indexing.
    patch(base + 0x1a2fa0, reinterpret_cast<const void *>(model_for_index));
    for (const auto rva : {0x1fa050,0x1a0ce0,0x19e9c0,0x1f6100,0x1a0c40})
        patch(base + rva, reinterpret_cast<const void *>(no_animation));
    std::strcpy(model.name, "*7");
    model.type = mod_brush;
    static_assert(sizeof(entity_state_t) == 340 && sizeof(cl_entity_t) == 3000);
    struct Frame {
        std::array<byte, 17020> previous{};
        int count{};
        std::array<byte, 128> resets{};
        entity_state_t *states{};
        std::array<byte, 20> tail{};
    } frame;
    static_assert(offsetof(Frame, count) == 17020 && offsetof(Frame, states) == 17152);
    std::vector<cl_entity_t> entities(2048);
    *reinterpret_cast<cl_entity_t **>(base + 0x32ead8) = entities.data();
    int max_clients = 32;
    *reinterpret_cast<int **>(base + 0x13fcc44) = &max_clients;
    *reinterpret_cast<int *>(base + 0x13f540c) = 0;
    auto &parsecount = *reinterpret_cast<std::uint32_t *>(base + 0x1282a84);
    *reinterpret_cast<double *>(base + 0x1282ee8) = 12.5;
    const auto publish = reinterpret_cast<void (*)(Frame *)>(base + 0x19fd20);
    const auto accept = [](brush::Frame &value) {
        std::array<std::uint8_t, 184> data;
        for (std::size_t i = 0;; i += brush::chunk_entries) {
            const auto size = brush::encode(value, i, data);
            const bool done = i + brush::chunk_entries >= value.total;
            assert(receiver.accept({data.data(), size}) == done);
            if (done) break;
        }
    };
    entity_state_t state{};
    state.number = 50; state.modelindex = 77; state.solid = SOLID_BSP;
    state.iuser1 = 111; state.iuser2 = 222; state.iuser3 = 333; state.iuser4 = 444;
    frame.count = 1; frame.states = &state;
    receiver.reset(83);
    brush::Frame identities;
    identities.epoch = 83; identities.revision = 9; identities.sequence = 731; identities.total = 1;
    identities.entries[0] = {50, 0, 7};
    parsecount = 731;
    publish(&frame);
    auto &ent = entities[50];
    assert(ent.index == 50 && ent.curstate.messagenum == 731 && ent.model == &model);
    accept(identities);
    assert(receiver.matches({50,0,7}, std::uint32_t(ent.curstate.messagenum), 83, 9));
    const auto saved = ent.curstate;
    parsecount = 732;
    publish(&frame); // Delta-copy equivalent: unchanged entity fields, new frame.
    assert(ent.curstate.messagenum == 732 && std::memcmp(&ent.prevstate, &saved, sizeof(saved)) == 0);
    assert(!receiver.matches({50,0,7}, std::uint32_t(ent.curstate.messagenum), 83, 9));
    ++identities.sequence; identities.entries[0].serial = 1;
    accept(identities);
    assert(!receiver.matches({50,0,7}, std::uint32_t(ent.curstate.messagenum), 83, 9));
    assert(receiver.matches({50,1,7}, std::uint32_t(ent.curstate.messagenum), 83, 9));
    assert(ent.curstate.iuser1 == 111 && ent.curstate.iuser2 == 222 && ent.curstate.iuser3 == 333 && ent.curstate.iuser4 == 444);
    bool server_bytes = false;
    if (argc == 3) {
        // Replay GCBrush payloads emitted by the separate actual ReHLDS test.
        std::ifstream stream(argv[2], std::ios::binary);
        const Bytes raw{std::istreambuf_iterator<char>(stream), {}};
        assert(raw.size() > 26);
        Reader first(std::span(raw).subspan(2));
        const auto epoch = first.u64();
        receiver.reset(epoch);
        std::size_t cursor = 0;
        while (cursor < raw.size()) {
            assert(cursor + 2 <= raw.size());
            const auto size = raw[cursor + 1];
            assert(cursor + 2 + size <= raw.size());
            receiver.accept(std::span(raw).subspan(cursor + 2, size));
            cursor += size + 2;
        }
        const auto *received = receiver.frame();
        assert(received && received->total);
        std::vector<entity_state_t> outgoing(received->total);
        for (unsigned i = 0; i < received->total; ++i) {
            assert(received->entries[i].slot > 32 && received->entries[i].slot < entities.size());
            outgoing[i] = state;
            outgoing[i].number = int(received->entries[i].slot);
        }
        frame.count = int(outgoing.size()); frame.states = outgoing.data();
        parsecount = received->sequence;
        publish(&frame);
        for (unsigned i = 0; i < received->total; ++i) {
            const auto identity = received->entries[i];
            assert(receiver.matches(identity, std::uint32_t(entities[identity.slot].curstate.messagenum), epoch, received->revision));
        }
        server_bytes = true;
    }
    FreeLibrary(image);
    std::cout << "{\"actualEngineBrushIdentity\":true,\"originalStatePublication\":true,\"sameSlotSameModelReuse\":true,"
                 "\"userFieldsPreserved\":true,\"replayedServerBytes\":" << (server_bytes ? "true" : "false")
              << ",\"passed\":true}\n";
}
