#include "goldcraft/packet_entities.hpp"
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>

using namespace goldcraft::packet_entities;
namespace {
Reassembler receiver;
std::array<std::uint8_t, max_message> payload;
using Datagram = std::array<std::uint8_t, wire_bytes>;
Datagram packet(std::size_t size, unsigned sequence, unsigned index, std::size_t &written) {
    Datagram out{};
    written = encode(std::span(payload).first(size), sequence, index, out);
    assert(written);
    return out;
}
}
int main() {
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = std::uint8_t(i * 31 + i / 251);
    unsigned cases = 0, sequence = 1;
    for (const auto size : {1u, 1379u, 1380u, 4000u, 6009u, 6010u, 11904u, 32000u, 65536u}) {
        for (const bool reverse : {false, true}) {
            receiver.reset();
            const auto count = unsigned((size + fragment_bytes - 1) / fragment_bytes);
            for (unsigned i = 0; i < count; ++i) {
                std::size_t bytes;
                const auto index = reverse ? count - i - 1 : i;
                auto wire = packet(size, sequence, index, bytes);
                const auto result = receiver.accept({wire.data(), bytes}, 41, 10 + i * .001);
                assert(result == (i + 1 == count ? Result::complete : Result::incomplete));
                if (i + 1 != count)
                    assert(receiver.accept({wire.data(), bytes}, 41, 10 + i * .001) == Result::incomplete);
            }
            assert(receiver.data().size() == size && std::equal(receiver.data().begin(), receiver.data().end(), payload.begin()));
            std::size_t bytes;
            const auto again = packet(size, sequence, 0, bytes);
            assert(receiver.accept({again.data(), bytes}, 41, 10.1) == Result::rejected);
            ++sequence; ++cases;
        }
    }
    receiver.reset();
    std::size_t bytes;
    auto first = packet(65536, 100, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 10) == Result::incomplete);
    assert(receiver.accept({first.data(), bytes}, 42, 10) == Result::rejected);
    auto conflict = first; conflict[header_bytes] ^= 1;
    assert(receiver.accept({conflict.data(), bytes}, 41, 10.1) == Result::rejected);
    assert(receiver.accept({first.data(), bytes}, 41, 10.2) == Result::rejected);
    first = packet(65536, 101, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 11) == Result::incomplete);
    auto last = packet(65536, 101, 47, bytes);
    assert(receiver.accept({last.data(), bytes}, 41, 13.1) == Result::rejected);
    first = packet(1, 102, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 13.2) == Result::complete);
    receiver.reset();
    first = packet(1, sequence_mask, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 15) == Result::complete);
    first = packet(1, 1, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 15.1) == Result::complete);
    first = packet(1, sequence_mask, 0, bytes);
    assert(receiver.accept({first.data(), bytes}, 41, 15.2) == Result::rejected);
    ++cases;
    receiver.reset();
    first = packet(32000, 120, 0, bytes);
    for (std::size_t i = 9; i < bytes; ++i) {
        receiver.reset();
        assert(receiver.accept({first.data(), i}, 41, 20) == Result::rejected);
    }
    for (const auto at : {4u, 9u, 13u, 17u, 19u}) {
        auto bad = first;
        write(bad, at, 0xffffffffu, at >= 17 ? 2 : 4);
        receiver.reset();
        assert(receiver.accept({bad.data(), bytes}, 41, 20) == Result::rejected);
    }
    auto native = first; native[8] = 0x21;
    assert(receiver.accept({native.data(), bytes}, 41, 20) == Result::native);
    assert(!encode({}, 1, 0, native));
    assert(!encode(payload, 0, 0, native));
    assert(!encode(payload, 1, max_fragments, native));
    ++cases;
    std::cout << "{\"packetTransport\":true,\"cases\":" << cases
              << ",\"maxBytes\":" << max_message << ",\"maxFragments\":" << max_fragments
              << ",\"reorderDuplicateLossExpiryWrapSourceChecks\":true,\"passed\":true}\n";
}
