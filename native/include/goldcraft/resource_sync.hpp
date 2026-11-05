// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace goldcraft::resource_sync {
// User-requested host resource update protocol. Media encoding remains v3;
// only clients advertising this additional capability receive this manifest.
inline constexpr char capability[] = "gc_resource_sync";
inline constexpr char capability_value[] = "1";
inline constexpr std::uint32_t manifest_version = 4;
inline constexpr std::uint32_t max_file_size = 64u * 1024u * 1024u;
using Digest = std::array<std::uint8_t, 32>;

// Wire v4 retains the v3 header through count, then writes generation low/high
// as two32-bit fields. After each entry's reserved bytes: required1 bit, then
// size32 bits and digest256 bits when required. Original consistency follows.
struct Entry {
    bool required{};
    std::uint32_t size{};
    Digest sha256{};
    bool operator==(const Entry&) const = default;
};
constexpr bool supported_manifest(std::uint32_t version) {
    return version == 3 || version == manifest_version;
}
} // namespace goldcraft::resource_sync
