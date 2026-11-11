// SPDX-License-Identifier: GPL-3.0-or-later
#include "magazine_model.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#pragma clang fp contract(off)

namespace guest::cof
{
namespace
{
// Studio v10 layout, checked against the real SDK by this module's fixture.
constexpr std::size_t HeaderBytes = 244, DescriptorBytes = 176;
constexpr std::int32_t MaximumSequences = 2048;

std::uint32_t Word(std::span<const std::byte> data, std::size_t at) noexcept
{
    const auto* p = reinterpret_cast<const unsigned char*>(data.data() + at);
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::int32_t Integer(std::span<const std::byte> data, std::size_t at) noexcept
{
    return std::bit_cast<std::int32_t>(Word(data, at));
}
}

MagazineModelResult PrepareMagazineSequenceTimes(std::span<const std::byte> model,
    unsigned requiredCount, MagazineSequenceTimes& output) noexcept
{
    if (!requiredCount || requiredCount > output.duration.size())
        return MagazineModelResult::InvalidInput;
    if (model.size() < HeaderBytes ||
        model.size() > std::size_t(std::numeric_limits<std::int32_t>::max()) ||
        Word(model, 0) != 0x54534449 || Integer(model, 4) != 10 ||
        Integer(model, 72) != static_cast<std::int32_t>(model.size()))
        return MagazineModelResult::InvalidModel;
    const auto count = Integer(model, 164), start = Integer(model, 168);
    if (count <= 0 || count > MaximumSequences || start < std::int32_t(HeaderBytes) ||
        std::size_t(start) > model.size() ||
        std::size_t(count) > (model.size() - std::size_t(start)) / DescriptorBytes)
        return MagazineModelResult::InvalidModel;
    if (requiredCount > static_cast<unsigned>(count))
        return MagazineModelResult::MissingSequence;

    MagazineSequenceTimes prepared{};
    for (unsigned i = 0; i < requiredCount; ++i)
    {
        const auto at = std::size_t(start) + std::size_t(i) * DescriptorBytes;
        const auto frames = Integer(model, at + 56);
        const float fps = std::bit_cast<float>(Word(model, at + 32));
        if (frames <= 0 || !std::isfinite(fps) || fps <= 0)
            return MagazineModelResult::InvalidTiming;
        // Original1000F390 returns zero before division for one-frame tracks.
        // Convert frames-1 to float first, as its cvtdq2ps/divss instructions do.
        const float duration = frames <= 1 ? 0.0f : static_cast<float>(frames - 1) / fps;
        if (!std::isfinite(duration)) return MagazineModelResult::InvalidTiming;
        prepared.duration[i] = duration;
        prepared.present |= std::uint64_t{1} << i;
    }
    output = prepared;
    return MagazineModelResult::Prepared;
}
}
