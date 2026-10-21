// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <span>
#include <vector>
#include <cstddef>
#include <cstdint>
namespace csol {
struct Image { unsigned Width{}, Height{}; std::vector<std::uint8_t> RGBA; };
Image DecodeTGA(std::span<const std::byte> bytes);
Image DecodeBMP(std::span<const std::byte> bytes);
}
