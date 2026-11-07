// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

// Bullet impact presentation for CSOL scenario monsters. The original client
// (f4145136, 02568CF0) draws flesh/metal effects for solid 3/6 physents in
// scenario modes; the stock CS client only emits BSP wall effects. Our common
// monster TraceAttack adapter sends confirmed hits, without weapon hooks.
namespace csol::monster_impact {
inline constexpr int SolidSlideBox = 3, SolidCSO = 6;
inline constexpr int ShieldHitgroup = 8;
// Stock R_BloodSprite takes a palette index; CSO's sixth-argument default
// palette is not available in the stock engine. Use the CS server's red.
inline constexpr int BloodColor = 247;
inline constexpr char BloodSpray[] = "sprites/bloodspray.spr";
inline constexpr char BloodDrop[] = "sprites/blood.spr";
inline constexpr int SparkVelocityMin = 0, SparkVelocityMax = 500;
// TraceAttack has no reliable original client bullet ID, especially for a
// delayed projectile after its owner switches weapons. Keep CSO's unknown
// bullet fallback; never guess a calibre from the current active weapon.
inline constexpr int UnknownBullet = 0;
constexpr bool DirectHit(int bits)
{
    constexpr int physical = (1 << 1) | (1 << 2) | (1 << 7);
    constexpr int modifiers = (1 << 12) | (1 << 13);
    return (bits & physical) != 0 && (bits & ~(physical | modifiers)) == 0;
}

// Original jump table 025693B8/02569390 indexed by bullet-1 (1..139); other
// values use 4. Bullet 23 is forced to 4 only for blood.
inline constexpr std::array<std::uint8_t, 139> BulletSizes{
    6, 4, 4, 4, 4, 4, 4, 4, 5, 13, 6, 6, 7, 5, 6, 6, 6, 4, 13, 15,
    6, 4, 4, 4, 4, 4, 7, 6, 4, 4, 4, 4, 6, 4, 6, 4, 12, 6, 6, 4,
    15, 4, 4, 4, 4, 4, 4, 4, 4, 8, 12, 4, 4, 4, 4, 4, 4, 4, 4, 6,
    5, 4, 4, 4, 6, 6, 4, 6, 4, 15, 4, 6, 4, 4, 4, 4, 8, 4, 12, 4,
    4, 6, 6, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 15, 4, 4, 4, 6, 4, 4, 4, 4, 8, 4, 4, 4, 7};

// HW 371C1190 registers flag 0x20000 by exact (FNV-1a keyed) model name.
inline constexpr std::array<std::string_view, 11> MetalModels{
    "models/monster/dron_missile.mdl", "models/monster/flydron.mdl",
    "models/monster/osprey.mdl", "models/monster/giant.mdl",
    "models/monster/littlegiant.mdl", "models/monster/mx2000.mdl",
    "models/boss/zbs73.mdl", "models/boss/zbs73_obj.mdl",
    "models/boss/zbs73_missile.mdl", "models/boss/zbs84mx2000.mdl",
    "models/boss/zbs91_boss.mdl"};
// HW 371C1490 class registrations matched by substring: class 7 sparks on
// shield hitgroup hits, class 8 never bleeds.
inline constexpr std::string_view ShieldedModel = "models/monster/zbs_bossL_big08.mdl";
inline constexpr std::string_view BloodlessModel = "models/player/zg_goliath/zg_goliath2.mdl";

enum class Kind : std::uint8_t { None, Blood, Spark };
// Engine model classes used by the scenario branch (model_t+1F8).
enum class ModelClass : std::uint8_t { Other, Shielded = 7, Bloodless = 8 };
struct Effect
{
    Kind kind{};
    int amount{}; // Blood sprite size or spark count.
    constexpr bool operator==(const Effect&) const = default;
};

constexpr int BulletSize(int bullet)
{
    return bullet >= 1 && bullet <= static_cast<int>(BulletSizes.size()) ? BulletSizes[bullet - 1] : 4;
}

constexpr bool Metal(std::string_view model)
{
    for (const auto name : MetalModels)
        if (name == model) return true;
    return false;
}

constexpr ModelClass ClassOf(std::string_view model)
{
    if (model.find(ShieldedModel) != std::string_view::npos) return ModelClass::Shielded;
    if (model.find(BloodlessModel) != std::string_view::npos) return ModelClass::Bloodless;
    return ModelClass::Other;
}

// Original decision order after the mode/marker gate. physentInitial is the
// first byte of the engine's physent name ("player" for clients).
constexpr Effect Decide(char physentInitial, bool metal, ModelClass modelClass,
    int solid, int hitgroup, int bullet)
{
    if (solid != SolidSlideBox && solid != SolidCSO) return {};
    const int size = BulletSize(bullet);
    if (metal || (modelClass == ModelClass::Shielded && hitgroup == ShieldHitgroup))
        return {Kind::Spark, size * 4};
    if (physentInitial == 'p' || modelClass == ModelClass::Bloodless) return {};
    return {Kind::Blood, bullet == 23 ? 4 : size};
}

// model is the full studio model name; the physent name copy truncates it.
constexpr Effect Classify(std::string_view physentName, std::string_view model,
    int solid, int hitgroup, int bullet)
{
    return Decide(physentName.empty() ? '\0' : physentName.front(), Metal(model), ClassOf(model),
        solid, hitgroup, bullet);
}
}
