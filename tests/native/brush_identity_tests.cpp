#include "goldcraft/brush_identity.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
std::uint64_t allocations = 0;
}
void *operator new(std::size_t size) {
    ++allocations;
    if (void *p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

using namespace goldcraft;
namespace {
brush::Receiver receiver;
using Buffer = std::array<std::uint8_t, 184>;
brush::Frame frame(std::uint32_t sequence, std::uint16_t count) {
    brush::Frame result;
    result.epoch = 83; result.revision = 9; result.sequence = sequence; result.total = count;
    for (unsigned i = 0; i < count; ++i) result.entries[i] = {33 + i, 0x80000000 + i, i + 1};
    return result;
}
void deliver(const brush::Frame &value) {
    Buffer buffer;
    for (std::size_t first = 0;; first += brush::chunk_entries) {
        const auto bytes = brush::encode(value, first, buffer);
        const bool complete = first + brush::chunk_entries >= value.total;
        assert(receiver.accept({buffer.data(), bytes}) == complete);
        if (complete) break;
    }
    assert(receiver.frame() && receiver.frame()->total == value.total);
}
void check(const brush::Frame &value) {
    for (unsigned i = 0; i < value.total; ++i) {
        const auto entry = value.entries[i];
        assert(receiver.matches(entry, value.sequence, value.epoch, value.revision));
        assert(!receiver.matches({entry.slot, entry.serial + 1, entry.model}, value.sequence, value.epoch, value.revision));
        assert(!receiver.matches({entry.slot, entry.serial, entry.model + 1}, value.sequence, value.epoch, value.revision));
        assert(!receiver.matches(entry, value.sequence + 1, value.epoch, value.revision));
        assert(!receiver.matches(entry, value.sequence, value.epoch + 1, value.revision));
        assert(!receiver.matches(entry, value.sequence, value.epoch, value.revision + 1));
    }
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const ProtocolError &) { rejected = true; }
    assert(rejected && !receiver.frame());
}
}
int main() {
    receiver.reset(83);
    auto first = frame(100, 1);
    first.entries[0] = {65535, 0, 65536}; // Full serial/model range, no signed narrowing.
    deliver(first); check(first);
    auto replacement = first;
    ++replacement.sequence; ++replacement.entries[0].serial;
    deliver(replacement); check(replacement);
    assert(!receiver.matches(first.entries[0], replacement.sequence, 83, 9));
    auto empty = frame(102, 0);
    deliver(empty);
    assert(!receiver.matches(replacement.entries[0], empty.sequence, 83, 9));

    auto many = frame(103, 256);
    Buffer data;
    auto length = brush::encode(many, 0, data);
    assert(!receiver.accept({data.data(), length}));
    assert(!receiver.matches(many.entries[0], many.sequence, 83, 9));
    for (unsigned offset = 16; offset < many.total; offset += 16) {
        length = brush::encode(many, offset, data);
        assert(receiver.accept({data.data(), length}) == (offset + 16 == many.total));
    }
    check(many);
    length = brush::encode(first, 0, data);
    assert(!receiver.accept({data.data(), length})); // Older packet is ignored.
    check(many);
    auto foreign = frame(200, 1); foreign.epoch = 84;
    length = brush::encode(foreign, 0, data);
    assert(!receiver.accept({data.data(), length})); check(many);
    length = brush::encode(many, 0, data);
    rejects([&] { receiver.accept({data.data(), length}); }); // Duplicate same-sequence set.
    auto gap = frame(104, 32);
    length = brush::encode(gap, 16, data);
    rejects([&] { receiver.accept({data.data(), length}); });
    deliver(frame(105, 2));
    auto partial = frame(106, 32);
    length = brush::encode(partial, 0, data);
    assert(!receiver.accept({data.data(), length}));
    deliver(frame(108, 3)); // Lost remainder recovers on a new complete datagram.
    length = brush::encode(partial, 16, data);
    assert(!receiver.accept({data.data(), length}));
    assert(receiver.frame()->sequence == 108);

    receiver.reset(83);
    deliver(frame(0x7fffffff, 3)); deliver(frame(0, 4)); check(frame(0, 4));
    length = brush::encode(frame(0x7ffffffe, 3), 0, data);
    assert(!receiver.accept({data.data(), length})); check(frame(0, 4));
    receiver.reset(84);
    assert(!receiver.frame() && !receiver.matches(many.entries[0], 103, 83, 9));
    receiver.reset(83);

    // Every truncation, a duplicate slot, and a changed fragment revision fail
    // closed without partially publishing a replacement identity table.
    for (std::size_t size = 0; size < 184; ++size) {
        receiver.reset(83);
        length = brush::encode(frame(300, 16), 0, data);
        rejects([&] { receiver.accept({data.data(), size}); });
    }
    receiver.reset(83);
    length = brush::encode(frame(300, 2), 0, data);
    data[34] = data[24]; data[35] = data[25];
    rejects([&] { receiver.accept({data.data(), length}); });
    receiver.reset(83);
    length = brush::encode(frame(301, 32), 0, data);
    assert(!receiver.accept({data.data(), length}));
    length = brush::encode(frame(301, 32), 16, data); ++data[8];
    rejects([&] { receiver.accept({data.data(), length}); });

    receiver.reset(83);
    const auto allocation_start = allocations;
    const auto time = std::chrono::steady_clock::now();
    unsigned matches = 0;
    for (unsigned i = 0; i < 4096; ++i) {
        const auto value = frame(500 + i, 256);
        deliver(value);
        for (unsigned j = 0; j < 256; ++j)
            matches += receiver.matches(value.entries[j], value.sequence, 83, 9);
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - time).count();
    const auto used = allocations - allocation_start;
    assert(used == 0 && matches == 4096 * 256);
    std::cout << "{\"brushIdentity\":true,\"frames\":4096,\"lookups\":" << matches
              << ",\"allocations\":" << used << ",\"frameRoundTripUs\":" << elapsed / 4096
              << ",\"sameFrameSerialModel\":true,\"atomicFragments\":true,\"lossWrapReset\":true,\"passed\":true}\n";
}
