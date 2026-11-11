// SPDX-License-Identifier: GPL-3.0-or-later
#include "player_actions.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace guest::cof
{
namespace
{
constexpr std::size_t HeaderBytes = 244, DescriptorBytes = 176;
constexpr std::int32_t MaximumSequences = 2048;

std::uint32_t Word(std::span<const std::byte> data, std::size_t at)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data() + at);
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

std::int32_t Integer(std::span<const std::byte> data, std::size_t at)
{
    return std::bit_cast<std::int32_t>(Word(data, at));
}

float Float(std::span<const std::byte> data, std::size_t at)
{
    return std::bit_cast<float>(Word(data, at));
}

bool ValidLabel(std::string_view label, std::string_view prefix, PlayerActionLabelCompare compare)
{
    if (label.size() <= prefix.size() || label.size() >= 32 || label.find('\0') != label.npos)
        return false;
    char requestedPrefix[8]{};
    std::memcpy(requestedPrefix, label.data(), prefix.size());
    return compare(requestedPrefix, prefix.data()) == 0;
}
}

void PlayerActionBindings::Reset() noexcept
{
    size_ = 0;
    generation_ = {};
}

bool PlayerActionBindings::Bind(std::span<const std::byte> data,
                                PlayerModelGeneration generation,
                                std::span<const PlayerActionLabels> labels,
                                PlayerActionLabelCompare compare, std::string& error)
{
    Reset();
    error.clear();
    const auto reject = [&](const char* reason) { error = reason; return false; };
    if (!generation.mapEpoch || !generation.modelGeneration || !compare ||
        labels.empty() || labels.size() > Capacity)
        return reject("Invalid player action generation, comparator or action count");
    if (data.size() < HeaderBytes || data.size() > std::size_t(std::numeric_limits<std::int32_t>::max()) ||
        Word(data, 0) != 0x54534449 || Integer(data, 4) != 10 ||
        Integer(data, 72) != static_cast<std::int32_t>(data.size()))
        return reject("Player actions require a complete validated studio v10 model");

    const auto bones = Integer(data, 140), boneIndex = Integer(data, 144);
    const auto sequences = Integer(data, 164), sequenceIndex = Integer(data, 168);
    if (bones <= 0 || bones > 128 || boneIndex < std::int32_t(HeaderBytes) ||
        std::size_t(boneIndex) > data.size() || std::size_t(bones) > (data.size() - boneIndex) / 112 ||
        sequences <= 0 || sequences > MaximumSequences || sequenceIndex < std::int32_t(HeaderBytes) ||
        std::size_t(sequenceIndex) > data.size() ||
        std::size_t(sequences) > (data.size() - sequenceIndex) / DescriptorBytes)
        return reject("Player action skeleton or descriptor table is outside the model");
    for (std::int32_t i = 0; i < sequences; ++i)
    {
        const auto at = std::size_t(sequenceIndex) + std::size_t(i) * DescriptorBytes;
        const auto* label = reinterpret_cast<const char*>(data.data() + at);
        const auto frames = Integer(data, at + 56);
        const auto fps = Float(data, at + 32);
        if (!std::memchr(label, 0, 32) || frames <= 0 ||
            !std::isfinite(fps) || fps <= 0)
            return reject("Invalid player action descriptor label, frames or fps");
    }

    std::array<Pair, Capacity> prepared{};
    const auto resolve = [&](std::string_view requested, PlayerActionSequence& result)
    {
        char requestedName[32]{};
        std::memcpy(requestedName, requested.data(), requested.size());
        // Match ReGameDLL LookupSequence: configured Q_stricmp, first match.
        for (std::int32_t i = 0; i < sequences; ++i)
        {
            const auto at = std::size_t(sequenceIndex) + std::size_t(i) * DescriptorBytes;
            const auto* label = reinterpret_cast<const char*>(data.data() + at);
            if (compare(label, requestedName) != 0) continue;
            result.sequence = i;
            result.frames = Integer(data, at + 56);
            result.flags = Integer(data, at + 36);
            result.fps = Float(data, at + 32);
            // These are distinct SDK formulas. Keep float arithmetic/order.
            result.sdkDuration = static_cast<float>(result.frames) / result.fps;
            result.normalizedFrameRate = result.frames <= 1 ? 256.0f :
                result.fps * 256.0f / static_cast<float>(result.frames - 1);
            return std::isfinite(result.sdkDuration) && std::isfinite(result.normalizedFrameRate);
        }
        return false;
    };
    for (std::size_t i = 0; i < labels.size(); ++i)
    {
        if (!ValidLabel(labels[i].standing, "ref_", compare) || !ValidLabel(labels[i].crouching, "crouch_", compare))
            return reject("Player actions require explicit full standing and crouching labels");
        if (!resolve(labels[i].standing, prepared[i].standing) ||
            !resolve(labels[i].crouching, prepared[i].crouching))
            return reject("Required player action label is missing or has invalid SDK timing");
    }
    values_ = prepared;
    generation_ = generation;
    size_ = labels.size();
    return true;
}

std::optional<PlayerActionSequence> PlayerActionBindings::Select(PlayerModelGeneration current,
                                                                std::size_t action,
                                                                bool crouching) const noexcept
{
    if (current != generation_ || !current.mapEpoch || !current.modelGeneration || action >= size_)
        return std::nullopt;
    return crouching ? values_[action].crouching : values_[action].standing;
}
}
