// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace guest::cof
{
// Host transport identity. These values are never original object pointers,
// entity indices, or substitutes for server-side ownership validation.
struct AuthorityKey
{
    std::uint32_t sessionEpoch{};
    std::uint32_t lifeEpoch{};
    std::uint32_t generation{};
    bool operator==(const AuthorityKey&) const noexcept = default;
};

constexpr bool Valid(AuthorityKey key) noexcept
{
    return key.sessionEpoch && key.lifeEpoch && key.generation;
}
}
