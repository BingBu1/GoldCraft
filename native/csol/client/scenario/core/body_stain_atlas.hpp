// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../../scenario/custom/body_stains.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace csol::client::stain
{
namespace custom = scenario::custom;
inline constexpr int Side = 96;
inline constexpr unsigned Pixels = Side * Side;

struct Decal
{
    int width{}, height{};
    std::vector<std::uint8_t> alpha;
};

struct Pixel { std::uint8_t red, green, blue, alpha; };
static_assert(sizeof(Pixel) == 4);

using Coverage = std::array<std::uint8_t, Pixels>;

inline std::uint32_t Advance(std::uint32_t& random) noexcept
{
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
}

inline unsigned TiledIndex(int x, int y) noexcept
{
    const auto wrap = [](int coord) { return (coord % Side + Side) % Side; };
    return wrap(x) + wrap(y) * Side;
}

inline float Opacity(const Decal& decal, float u, float v) noexcept
{
    const float s = u * (decal.width - 1), t = v * (decal.height - 1);
    const int x = std::min(int(s), decal.width - 2), y = std::min(int(t), decal.height - 2);
    const auto* row = decal.alpha.data() + y * decal.width + x;
    return std::lerp(std::lerp(float(row[0]), float(row[1]), s - x),
        std::lerp(float(row[decal.width]), float(row[decal.width + 1]), s - x), t - y) / 255.f;
}

inline void Stamp(Coverage& coverage, const Decal& decal, std::uint32_t& random,
    float size, float strength) noexcept
{
    const auto unit = [&] { return float(Advance(random) >> 16) / 65536.f; };
    const int centerX = int(unit() * Side), centerY = int(unit() * Side);
    const float width = size * Side, height = width * (.72f + .46f * unit());
    const float angle = unit() * 6.2831853f;
    const float sine = std::sin(angle), cosine = std::cos(angle);
    const float ux = cosine / width, uy = sine / width, vx = -sine / height, vy = cosine / height;
    const int rx = int(std::ceil((std::abs(cosine) * width + std::abs(sine) * height) * .5f));
    const int ry = int(std::ceil((std::abs(sine) * width + std::abs(cosine) * height) * .5f));
    for (int y = -ry; y <= ry; ++y)
    {
        for (int x = -rx; x <= rx; ++x)
        {
            const float u = .5f + x * ux + y * uy, v = .5f + x * vx + y * vy;
            const float edge = std::min({u, v, 1.f - u, 1.f - v});
            if (edge <= 0.f) continue;
            // Legacy wall decals can touch the image border. Fade the border
            // before wrapping the composition onto a model's repeating UVs.
            const float ink = std::min(1.f, 1.55f * Opacity(decal, u, v)) * std::min(1.f, edge * 12.f);
            const auto added = unsigned(ink * strength * 255.f + .5f);
            auto& alpha = coverage[TiledIndex(centerX + x, centerY + y)];
            alpha += ((255u - alpha) * added + 127u) / 255u;
        }
    }
}

inline std::vector<Pixel> BuildAtlas(std::span<const Decal> decals)
{
    if (decals.empty()) return {};
    std::vector<Pixel> atlas(custom::BodyStainLayerCount * Pixels);
    const auto channel = [](int value) { return std::uint8_t(std::clamp(value, 0, 255)); };
    for (unsigned pattern = 0; pattern < custom::BodyStainPatternCount; ++pattern)
    {
        std::uint32_t random = 0xa511e9b3u * (pattern + 1u);
        Coverage coverage{}, grain;
        for (auto& value : grain) value = std::uint8_t(Advance(random) >> 24);
        auto* pixels = atlas.data() + pattern * custom::BodyStainMaxHits * Pixels;
        for (unsigned hit = 0; hit < custom::BodyStainMaxHits; ++hit)
        {
            // The first hit is a broad cluster. Successive hits add smaller
            // spatters to the same surface without regenerating the old ones.
            if (!hit)
            {
                Stamp(coverage, decals[pattern % decals.size()], random, .98f, .97f);
                Stamp(coverage, decals[(pattern + 2) % decals.size()], random, .73f, .93f);
                Stamp(coverage, decals[(pattern + 3) % decals.size()], random, .61f, .87f);
            }
            else
            {
                const auto& decal = decals[Advance(random) % decals.size()];
                const float size = .24f + float(Advance(random) & 255u) * (.31f / 255.f);
                Stamp(coverage, decal, random, size, .83f);
            }
            for (int y = 0; y < Side; ++y)
            {
                for (int x = 0; x < Side; ++x)
                {
                    const auto i = unsigned(y * Side + x);
                    const int alpha = coverage[i];
                    // Ink thickness shapes dark clots and uneven edges. Bake
                    // the relief and grain into the color once for both shaders.
                    const int relief = (int(coverage[TiledIndex(x - 1, y)]) + int(coverage[TiledIndex(x, y - 1)])
                        - int(coverage[TiledIndex(x + 1, y)]) - int(coverage[TiledIndex(x, y + 1)])) / 6;
                    const int grit = (int(grain[i]) - 128) / 5;
                    pixels[i] = {channel(151 - alpha / 6 + relief + grit),
                        channel(26 - alpha / 15 + relief / 4 + grit / 3),
                        channel(21 - alpha / 18 + relief / 5 + grit / 4), coverage[i]};
                }
            }
            pixels += Pixels;
        }
    }
    return atlas;
}
}
