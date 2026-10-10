#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace goldcraft::packet_entities {
constexpr int capacity = 1024, legacy_capacity = 256;
constexpr char capability[] = "_gcpe", capability_value[] = "1024";
constexpr std::size_t max_message = 65536, wire_bytes = 1400, header_bytes = 21;
constexpr std::size_t fragment_bytes = wire_bytes - header_bytes;
constexpr std::size_t max_fragments = (max_message + fragment_bytes - 1) / fragment_bytes;
constexpr std::uint32_t magic = 0x4b504347; // GCPK; packetID 0 is invalid in native split packets.
constexpr std::uint32_t sequence_mask = 0x7fffffff;
constexpr double expiry_seconds = 2.0;
static_assert(max_fragments <= 64);

inline std::uint32_t read(std::span<const std::uint8_t> bytes, std::size_t at, unsigned width) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(bytes[at + i]) << (8 * i);
    return value;
}
inline void write(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value, unsigned width) noexcept {
    for (unsigned i = 0; i < width; ++i) bytes[at + i] = std::uint8_t(value >> (8 * i));
}
inline bool extended(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= 9 && read(bytes, 0, 4) == 0xfffffffeu && bytes[8] == 0;
}
inline std::size_t encode(std::span<const std::uint8_t> message, std::uint32_t sequence,
                          std::size_t index, std::span<std::uint8_t> output) noexcept {
    if (message.empty() || message.size() > max_message || !sequence || sequence > sequence_mask)
        return 0;
    const auto count = (message.size() + fragment_bytes - 1) / fragment_bytes;
    if (index >= count) return 0;
    const auto offset = index * fragment_bytes;
    const auto payload = std::min(fragment_bytes, message.size() - offset);
    if (output.size() < header_bytes + payload) return 0;
    write(output, 0, 0xfffffffeu, 4); write(output, 4, sequence, 4); output[8] = 0;
    write(output, 9, magic, 4); write(output, 13, std::uint32_t(message.size()), 4);
    write(output, 17, std::uint32_t(index), 2); write(output, 19, std::uint32_t(count), 2);
    std::memcpy(output.data() + header_bytes, message.data() + offset, payload);
    return header_bytes + payload;
}

enum class Result { native, incomplete, complete, rejected };
class Reassembler {
  public:
    void reset() noexcept { seen_ = active_ = false; mask_ = 0; total_ = 0; }
    std::span<const std::uint8_t> data() const noexcept { return {buffer_.data(), total_}; }
    Result accept(std::span<const std::uint8_t> bytes, std::uint64_t source, double now) noexcept {
        if (!extended(bytes)) return Result::native;
        if (bytes.size() < header_bytes || bytes.size() > wire_bytes || !std::isfinite(now) ||
            read(bytes, 9, 4) != magic) return Result::rejected;
        const auto sequence = read(bytes, 4, 4), total = read(bytes, 13, 4);
        const auto index = read(bytes, 17, 2), count = read(bytes, 19, 2);
        if (!sequence || sequence > sequence_mask || !total || total > max_message ||
            count != (total + fragment_bytes - 1) / fragment_bytes || index >= count)
            return Result::rejected;
        const auto offset = index * fragment_bytes;
        const auto size = std::min(fragment_bytes, std::size_t(total) - offset);
        if (bytes.size() != header_bytes + size) return Result::rejected;
        if (seen_ && source != source_) {
            if (now >= started_ && now - started_ <= expiry_seconds) return Result::rejected;
            reset();
        }
        if (seen_ && sequence != sequence_) {
            const auto distance = (sequence - sequence_) & sequence_mask;
            if (distance >= 0x40000000) return Result::rejected;
        }
        if (!seen_ || sequence != sequence_) {
            seen_ = active_ = true; sequence_ = sequence; source_ = source;
            total_ = total; count_ = count; mask_ = 0; started_ = now;
        }
        if (!active_ || now < started_ || now - started_ > expiry_seconds || total != total_ || count != count_) {
            active_ = false;
            return Result::rejected;
        }
        const auto bit = std::uint64_t{1} << index;
        const auto *payload = bytes.data() + header_bytes;
        if (mask_ & bit) {
            if (std::memcmp(buffer_.data() + offset, payload, size)) {
                active_ = false;
                return Result::rejected;
            }
            return Result::incomplete;
        }
        std::memcpy(buffer_.data() + offset, payload, size);
        mask_ |= bit;
        if (mask_ != (std::uint64_t{1} << count_) - 1) return Result::incomplete;
        active_ = false;
        return Result::complete;
    }
  private:
    std::array<std::uint8_t, max_message> buffer_{};
    std::uint64_t source_{}, mask_{};
    std::uint32_t sequence_{}, total_{}, count_{};
    double started_{};
    bool seen_ = false, active_ = false;
};
} // namespace goldcraft::packet_entities
