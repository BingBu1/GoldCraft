// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace csol::scenario::custom
{
// A GoldSrc entity snapshot has an 11-bit entity index. The body stain uses
// entity_state_t.iuser3, independent of the clientdata_t movement flags.
inline constexpr unsigned BodyStainSlotCount = 1u << 11;
inline constexpr unsigned BodyStainSeedBits = 10;
inline constexpr unsigned BodyStainSeedMask = (1u << BodyStainSeedBits) - 1;
inline constexpr unsigned BodyStainFieldBits = 28;
inline constexpr std::uint32_t BodyStainTag = 0x6d00000;
inline constexpr std::uint32_t BodyStainSeedFields = (1u << (2 * BodyStainSeedBits)) - 1;
inline constexpr char BodyStainDeltaField[] = "goldcraft_body_stain";

struct BodyStain
{
    unsigned body{}, weapon{};
    bool enabled{};
};

constexpr std::uint32_t PackBodyStain(BodyStain stain) noexcept
{
    if (!stain.enabled || stain.body > BodyStainSeedMask || stain.weapon > BodyStainSeedMask) return 0;
    return BodyStainTag | (stain.weapon << BodyStainSeedBits) | stain.body;
}

constexpr BodyStain UnpackBodyStain(std::uint32_t field) noexcept
{
    if ((field & ~BodyStainSeedFields) != BodyStainTag) return {};
    return {field & BodyStainSeedMask, (field >> BodyStainSeedBits) & BodyStainSeedMask, true};
}

inline constexpr std::array BodyStainDecals{"{blood1", "{blood2", "{blood3",
    "{blood4", "{blood5", "{blood6"};

enum class BodyStainSurface : unsigned { Body, HeldWeapon, ViewWeapon, ViewArms };

constexpr bool IsBodyStainArmMaterial(std::string_view name) noexcept
{
    if (name.size() > 7 && name.front() == '#') name.remove_prefix(7);
    constexpr std::array names{"view_glove.bmp", "view_skin.bmp", "view_finger.bmp",
        "cso_highq_hand__long_512.bmp", "cso_highq_hand_512.bmp", "cso_girl_hand_long.bmp",
        "cso_girl_hand.bmp", "hand.bmp", "thumb.bmp",
        "fingers.bmp", "glove.bmp", "sleeve.bmp"};
    for (std::string_view arm : names)
    {
        if (arm.size() != name.size()) continue;
        std::size_t i{};
        for (; i < arm.size(); ++i)
        {
            const char c = name[i];
            if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != arm[i]) break;
        }
        if (i == arm.size()) return true;
    }
    return false;
}

struct BodyStainPlacement
{
    unsigned decal{};
    float scale[2]{}, offset[2]{};
};

constexpr BodyStainPlacement PlaceBodyStain(unsigned seed, unsigned material, BodyStainSurface surface) noexcept
{
    // Decorrelate the variants for separate materials and the player's three
    // surfaces. Both the texture and UV transform stay stable as bones move.
    std::uint32_t hash = (seed + 0x9e3779b9u * (material + 1)) ^ (0x85ebca6bu * (static_cast<unsigned>(surface) + 1));
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    BodyStainPlacement result;
    result.decal = hash % BodyStainDecals.size();
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        const auto bits = hash >> (8 + axis * 12);
        const float center = .24f + .52f * float(bits & 15) / 15.0f;
        const float size = 1.7f + .6f * float((bits >> 4) & 7) / 7.0f;
        result.scale[axis] = (bits & 128) ? -size : size;
        result.offset[axis] = .5f - center * result.scale[axis];
    }
    return result;
}
}
