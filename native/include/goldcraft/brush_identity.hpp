#pragma once
#include "goldcraft/wire.hpp"
#include "goldcraft/packet_entities.hpp"
#include <algorithm>

namespace goldcraft::brush {
// Optional snapshot sideband, appended immediately after packetentities in the
// same datagram. Never borrow entity_state iuser fields from other plugins.
constexpr char message_name[] = "GCBrush";
constexpr char capability[] = "gc_brushes";
constexpr char capability_value[] = "1";
constexpr std::size_t max_entities = packet_entities::capacity, max_slots = 65536;
constexpr std::size_t header_bytes = 24, entry_bytes = 10, chunk_entries = 16;
constexpr std::uint32_t sequence_mask = 0x7fffffff;
struct Identity {
    std::uint32_t slot{}, serial{}, model{};
    bool operator==(const Identity &) const = default;
};
struct Frame {
    std::uint64_t epoch{}, revision{};
    std::uint32_t sequence{};
    std::uint16_t total{};
    std::array<Identity, max_entities> entries{};
};
inline bool valid(Identity identity) noexcept {
    return identity.slot > 0 && identity.slot < max_slots && identity.model > 0 &&
           identity.model <= 0x7fffffff;
}
inline std::size_t packet_bytes(std::size_t count) noexcept {
    const auto chunks = std::max(std::size_t{1}, (count + chunk_entries - 1) / chunk_entries);
    return chunks * (header_bytes + 2) + count * entry_bytes;
}
// Caller supplies a 184-byte buffer. Encoding and successful decoding allocate
// nothing; the native datagram preflights the complete sideband before writing.
inline std::size_t encode(const Frame &frame, std::size_t first,
                          std::span<std::uint8_t> out) {
    if (!frame.epoch || !frame.revision || frame.sequence > sequence_mask ||
        frame.total > max_entities || first > frame.total || first % chunk_entries ||
        (first == frame.total && first))
        throw ProtocolError("Brush identity frame bounds");
    const auto count = std::min(chunk_entries, std::size_t(frame.total) - first);
    const auto size = header_bytes + entry_bytes * count;
    if (out.size() < size) throw ProtocolError("Brush identity output bounds");
    std::size_t cursor = 0;
    const auto put = [&](std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) out[cursor++] = std::uint8_t(value >> (8 * i));
    };
    put(frame.epoch, 8); put(frame.revision, 8); put(frame.sequence, 4);
    put(frame.total, 2); put(first, 2);
    for (std::size_t i = first; i < first + count; ++i) {
        const auto entry = frame.entries[i];
        if (!valid(entry) || (i && entry.slot <= frame.entries[i - 1].slot))
            throw ProtocolError("Brush identity entry order");
        put(entry.slot, 2); put(entry.serial, 4); put(entry.model, 4);
    }
    return size;
}

class Receiver {
  public:
    void reset(std::uint64_t epoch = 0) noexcept {
        epoch_ = epoch;
        assembling_ = ready_ = seen_ = false;
        received_ = 0;
        latest_ = 0;
        // Entries are generation stamped; do not clear 65536 slots per frame.
    }
    bool accept(std::span<const std::uint8_t> bytes) {
        try { return receive(bytes); }
        catch (...) { assembling_ = ready_ = false; throw; }
    }
    void invalidate() noexcept { assembling_ = ready_ = false; }
    bool matches(Identity target, std::uint32_t sequence, std::uint64_t epoch,
                 std::uint64_t minimum_revision) const noexcept {
        if (!ready_ || epoch != epoch_ || sequence != committed_.sequence ||
            committed_.revision < minimum_revision || !valid(target)) return false;
        const auto &entry = slots_[target.slot];
        return entry.generation == generation_ && entry.serial == target.serial &&
               entry.model == target.model;
    }
    const Frame *frame() const noexcept { return ready_ ? &committed_ : nullptr; }

  private:
    bool receive(std::span<const std::uint8_t> bytes) {
        if (bytes.size() < header_bytes || bytes.size() > header_bytes + entry_bytes * chunk_entries ||
            (bytes.size() - header_bytes) % entry_bytes)
            throw ProtocolError("Brush identity fragment length");
        Reader reader(bytes);
        const auto epoch = reader.u64(), revision = reader.u64();
        const auto sequence = reader.u32();
        const auto total = reader.u16(), first = reader.u16();
        const auto count = (bytes.size() - header_bytes) / entry_bytes;
        if (!epoch || !revision || sequence > sequence_mask || total > max_entities ||
            first > total || first % chunk_entries || (first == total && first) ||
            count != std::min(chunk_entries, std::size_t(total - first)))
            throw ProtocolError("Brush identity fragment bounds");
        if (epoch != epoch_ || !epoch_) return false;
        const auto distance = (sequence - latest_) & sequence_mask;
        if (seen_ && sequence != latest_ && distance >= 0x40000000) return false;
        if (!first) {
            if (seen_ && sequence == latest_)
                throw ProtocolError("Duplicate brush identity frame");
            latest_ = sequence;
            seen_ = assembling_ = true;
            pending_.epoch = epoch; pending_.revision = revision;
            pending_.sequence = sequence; pending_.total = total;
            received_ = 0;
        }
        if (!assembling_ || sequence != pending_.sequence || revision != pending_.revision ||
            total != pending_.total || first != received_)
            throw ProtocolError("Unordered brush identity fragment");
        for (std::size_t i = 0; i < count; ++i) {
            Identity entry{reader.u16(), reader.u32(), reader.u32()};
            if (!valid(entry) || (received_ && entry.slot <= pending_.entries[received_ - 1].slot))
                throw ProtocolError("Invalid brush identity entry");
            pending_.entries[received_++] = entry;
        }
        reader.finish();
        if (received_ != total) return false;
        if (++generation_ == 0) { slots_ = {}; ++generation_; }
        for (std::size_t i = 0; i < total; ++i) {
            const auto &entry = pending_.entries[i];
            slots_[entry.slot] = {generation_, entry.serial, entry.model};
        }
        committed_.epoch = epoch; committed_.revision = revision;
        committed_.sequence = sequence; committed_.total = total;
        std::copy_n(pending_.entries.begin(), total, committed_.entries.begin());
        assembling_ = false;
        ready_ = true;
        return true;
    }
    struct Slot { std::uint64_t generation{}; std::uint32_t serial{}, model{}; };
    std::array<Slot, max_slots> slots_{};
    Frame pending_, committed_;
    std::uint64_t epoch_{}, generation_{};
    std::uint32_t latest_{};
    std::size_t received_{};
    bool assembling_ = false, ready_ = false, seen_ = false;
};
} // namespace goldcraft::brush
