// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

namespace csol::scenario::custom
{
// A GoldSrc entity snapshot has an 11-bit entity index. The stock client
// overwrites the local frame's iuser slots before reusing it as a delta base.
// Use entity_state_t.fuser4 for this payload without touching those slots.
inline constexpr unsigned BodyStainSlotCount = 1u << 11;
inline constexpr unsigned BodyStainHitBits = 6;
inline constexpr unsigned BodyStainMaxHits = (1u << BodyStainHitBits) - 1;
inline constexpr unsigned BodyStainMarkBits = 2 * BodyStainHitBits;
inline constexpr unsigned BodyStainMarkMask = (1u << BodyStainMarkBits) - 1;
inline constexpr unsigned BodyStainFieldBits = 28;
inline constexpr std::uint32_t BodyStainTag = 0xb000000;
inline constexpr std::uint32_t BodyStainMarkFields = (1u << (2 * BodyStainMarkBits)) - 1;
inline constexpr char BodyStainDeltaField[] = "goldcraft_body_stain_v2";

struct BodyStain
{
    unsigned body{}, weapon{};
    bool enabled{};
};

constexpr unsigned BodyStainHits(unsigned mark) noexcept { return mark & BodyStainMaxHits; }

constexpr unsigned AddBodyStainHit(unsigned mark, unsigned seed) noexcept
{
    if (!mark) return ((seed & BodyStainMaxHits) << BodyStainHitBits) | 1u;
    return mark + (BodyStainHits(mark) < BodyStainMaxHits);
}

constexpr bool ValidBodyStainMark(unsigned mark) noexcept
{
    return mark <= BodyStainMarkMask && (!mark || BodyStainHits(mark));
}

constexpr std::uint32_t PackBodyStain(BodyStain stain) noexcept
{
    if (!stain.enabled || !ValidBodyStainMark(stain.body) || !ValidBodyStainMark(stain.weapon)) return 0;
    return BodyStainTag | (stain.weapon << BodyStainMarkBits) | stain.body;
}

constexpr BodyStain UnpackBodyStain(std::uint32_t field) noexcept
{
    if ((field & ~BodyStainMarkFields) != BodyStainTag) return {};
    const auto body = field & BodyStainMarkMask;
    const auto weapon = (field >> BodyStainMarkBits) & BodyStainMarkMask;
    return ValidBodyStainMark(body) && ValidBodyStainMark(weapon) ? BodyStain{body, weapon, true} : BodyStain{};
}

// ReHLDS encodes fuser4 as DT_INTEGER, preserving the raw payload bits.
constexpr float EncodeBodyStainField(BodyStain stain) noexcept
{
    return std::bit_cast<float>(PackBodyStain(stain));
}

constexpr BodyStain DecodeBodyStainField(float field) noexcept
{
    return UnpackBodyStain(std::bit_cast<std::uint32_t>(field));
}

inline constexpr std::array BodyStainDecals{"{blood1", "{blood2", "{blood3",
    "{blood4", "{blood5", "{blood6"};

inline constexpr unsigned BodyStainPatternCount = 2;
inline constexpr unsigned BodyStainLayerCount = BodyStainPatternCount * BodyStainMaxHits;

enum class BodyStainSurface : unsigned { Body, HeldWeapon, ViewWeapon, ViewArms };

constexpr unsigned BodyStainArmMaterial(std::string_view name) noexcept
{
    if (name.size() > 7 && name.front() == '#') name.remove_prefix(7);
    constexpr std::array names{"view_glove.bmp", "view_skin.bmp", "view_finger.bmp",
        "cso_highq_hand__long_512.bmp", "cso_highq_hand_512.bmp", "cso_girl_hand_long.bmp",
        "cso_girl_hand.bmp", "hand.bmp", "thumb.bmp",
        "fingers.bmp", "glove.bmp", "sleeve.bmp"};
    for (unsigned material = 0; material < names.size(); ++material)
    {
        const std::string_view arm = names[material];
        if (arm.size() != name.size()) continue;
        std::size_t i{};
        for (; i < arm.size(); ++i)
        {
            const char c = name[i];
            if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != arm[i]) break;
        }
        if (i == arm.size()) return material + 1;
    }
    return 0;
}

struct BodyStainPlacement
{
    unsigned layer{};
    float scale[2]{}, offset[2]{};
};

constexpr BodyStainPlacement PlaceBodyStain(unsigned mark, unsigned material, BodyStainSurface surface) noexcept
{
    // Changing the hit count selects the next texture layer. The pattern,
    // translation and mirroring stay fixed for this surface.
    const auto seed = mark >> BodyStainHitBits;
    std::uint32_t hash = (seed + 0x9e3779b9u * (material + 1)) ^
        (0x85ebca6bu * (static_cast<unsigned>(surface) + 1));
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    BodyStainPlacement result;
    result.layer = (hash % BodyStainPatternCount) * BodyStainMaxHits + BodyStainHits(mark) - 1;
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        const auto bits = hash >> (1 + axis * 12);
        result.scale[axis] = (bits & 1) ? -1.f : 1.f;
        result.offset[axis] = float((bits >> 1) & 255) / 256.f;
    }
    return result;
}
}
