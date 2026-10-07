#pragma once

#include <cstdint>

namespace goldcraft { namespace precache {
// GoldSrc and Pawn expose signed int handles. v2 carries the complete positive
// 32-bit API range; memory, rather than a 16-bit wire field, limits table growth.
constexpr std::uint32_t max_media_index = 0x7fffffff;
constexpr std::uint32_t media_escape = 0xffff;
constexpr std::uint32_t marker = 0xfff;
constexpr std::uint32_t magic = 0x31504347; // "GCP1"
constexpr std::uint32_t version = 2;
constexpr const char* capability_value = "2";
constexpr std::uint32_t chunk_entries = 128;
constexpr const char* capability = "gc_precache";

// Offsets are in the original svc_temp_entity body, including the subtype byte.
// Only media fields expand. Entity IDs, coordinates, durations and decal IDs
// retain their original formats. Verified against HL10210, including its short
// player ID in TE_PLAYERSPRITES (the old SDK comment incorrectly says byte).
constexpr bool temp_model_field(unsigned type, unsigned offset) {
    switch (type) {
    case 0: case 15: case 19: case 20: case 21: case 110: case 119: case 120:
        return offset == 13;
    case 1: return offset == 9;
    case 3: case 5: case 17: case 23: case 100: return offset == 7;
    case 7: return offset == 16;
    case 8: case 24: return offset == 5;
    case 13: return offset == 11; // BSPDECAL, only emitted for a non-world entity
    case 18: return offset == 13 || offset == 15;
    case 22: case 105: case 121: return offset == 3;
    case 106: return offset == 14;
    case 107: case 123: return offset == 9;
    case 108: return offset == 20;
    case 113: case 114: return offset == 15;
    case 115: return offset == 7 || offset == 9;
    case 124: return offset == 4;
    default: return false;
    }
}

constexpr bool message_media_field(unsigned service, unsigned subtype, unsigned offset) {
    return (service == 23 && temp_model_field(subtype, offset)) ||
        (service == 20 && offset == 0) || // svc_spawnstatic
        (service == 29 && offset == 6);   // svc_spawnstaticsound
}

constexpr unsigned media_bytes(std::uint32_t index) {
    return index < media_escape ? 2 : 6;
}

// Little-endian escaped media fields. The short prefix stays byte-compatible
// below65535;65535 itself is escaped as well, so the boundary is unambiguous.
inline unsigned encode_media(std::uint8_t* out, std::uint32_t index) {
    const auto prefix = index < media_escape ? index : media_escape;
    out[0] = std::uint8_t(prefix); out[1] = std::uint8_t(prefix >> 8);
    if (prefix == media_escape)
        for (unsigned i = 0; i < 4; ++i) out[i + 2] = std::uint8_t(index >> (8 * i));
    return media_bytes(index);
}
inline std::uint32_t decode_media(const std::uint8_t* in) {
    std::uint32_t index = in[0] | (std::uint32_t(in[1]) << 8);
    if (index == media_escape) {
        index = 0;
        for (unsigned i = 0; i < 4; ++i) index |= std::uint32_t(in[i + 2]) << (8 * i);
    }
    return index;
}

struct ManifestChunk {
    std::uint32_t total = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    bool valid(std::uint32_t expected_first) const {
        return total <= 0x7fffffff && first == expected_first &&
            first <= total && count <= chunk_entries && count <= total - first &&
            (count != 0 || total == 0);
    }
    bool last() const { return first + count == total; }
};
} }
