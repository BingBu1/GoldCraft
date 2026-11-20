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

struct Point { int x, y; };

inline float Unit(std::uint32_t& random) noexcept
{
    return float(Advance(random) >> 16) / 65536.f;
}

inline Point SplashCenter(unsigned hit, std::uint32_t& random) noexcept
{
    // Low bits select opposite parts of the texture before revisiting a
    // region. Jitter within the cells keeps the spray from forming a grid.
    constexpr int cell = Side / 8;
    const unsigned x = ((hit & 1u) << 2) | ((hit & 4u) >> 1) | ((hit & 16u) >> 4);
    const unsigned y = ((hit & 2u) << 1) | ((hit & 8u) >> 2) | ((hit & 32u) >> 5);
    return {int(x) * cell + int(Advance(random) % cell),
        int(y) * cell + int(Advance(random) % cell)};
}

inline void Stamp(Coverage& coverage, const Decal& decal, Point center,
    std::uint32_t& random, float size, float strength) noexcept
{
    const float width = size * Side, height = width * (.76f + .44f * Unit(random));
    const float angle = Unit(random) * 6.2831853f;
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
            const float ink = std::clamp((Opacity(decal, u, v) - .10f) * 2.05f, 0.f, 1.f)
                * std::min(1.f, edge * 22.f);
            const auto added = unsigned(ink * strength * 255.f + .5f);
            auto& alpha = coverage[TiledIndex(center.x + x, center.y + y)];
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
            // Each hit adds a dense cluster and smaller spatters in other
            // UV regions. A model may only show a fraction of its texture.
            const float spread = std::max(.52f, 1.03f - float(hit) / 72.f);
            const auto spray = [&](unsigned region, unsigned firstDecal, float size, float variation)
            {
                // Open with the denser stock masks. Later shots also choose
                // among the sparser masks for smaller, separated droplets.
                const auto index = hit ? Advance(random) : pattern + firstDecal;
                const auto center = SplashCenter(region, random);
                const float diameter = (size + variation * Unit(random)) * spread;
                Stamp(coverage, decals[index % decals.size()], center, random, diameter, .94f);
            };
            spray(hit, 0, .48f, .15f);
            spray(hit ^ 3u, 2, .38f, .14f);
            spray(hit ^ 1u, 1, .28f, .12f);
            spray(hit ^ 2u, 3, .20f, .09f);
            for (int y = 0; y < Side; ++y)
            {
                for (int x = 0; x < Side; ++x)
                {
                    const auto i = unsigned(y * Side + x);
                    const int alpha = coverage[i];
                    // Derive a dark center and ridged edges from the thickness.
                    // Both renderers get the grain and relief from one sample.
                    const int relief = (int(coverage[TiledIndex(x - 1, y)]) + int(coverage[TiledIndex(x, y - 1)])
                        - int(coverage[TiledIndex(x + 1, y)]) - int(coverage[TiledIndex(x, y + 1)])) / 3;
                    const int grit = (int(grain[i]) - 128) / 3;
                    pixels[i] = {channel(192 - alpha / 7 + relief + grit),
                        channel(47 - alpha / 12 + relief / 4 + grit / 3),
                        channel(38 - alpha / 14 + relief / 5 + grit / 4), coverage[i]};
                }
            }
            pixels += Pixels;
        }
    }
    return atlas;
}
}
