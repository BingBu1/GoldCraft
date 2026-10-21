// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace csol {
struct SpriteFrame { std::vector<std::uint8_t> RGBA; };
struct Sprite {
    unsigned Type{}, Format{}, Width{}, Height{}, SyncType{};
    float Radius{}, BeamLength{};
    std::vector<SpriteFrame> Frames;
};
// Validates the complete DDS chain even when a dedicated server needs no pixels.
Sprite DecodeSpriteV3(std::span<const std::byte> bytes, bool pixels = true);
}
