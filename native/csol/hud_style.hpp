// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace csol::hud_style
{
inline constexpr char CatalogPath[] = "models/csol/hud_v1.json";
inline constexpr char FontPath[] = "resource/font/df_gb_y9.ttf";
enum class Art : unsigned { CharacterTop, CharacterBottom, WeaponTop, WeaponBottom,
    WeaponCard, Infinite, WhiteDigits, Health, Armor, Count };
struct Resource { const char* path; unsigned width, height; };
inline constexpr std::array<Resource, unsigned(Art::Count)> Resources{{
    {"resource/csol/hud/v1/hud_character_bg_new_top.tga",288,85},
    {"resource/csol/hud/v1/hud_character_bg_new_bottom.tga",288,85},
    {"resource/csol/hud/v1/hud_weapon_bg_top.tga",288,76},
    {"resource/csol/hud/v1/hud_weapon_bg_bottom.tga",288,76},
    {"resource/csol/hud/v1/hud_weapon_off_bg.tga",170,60},
    {"resource/csol/hud/v1/infinite.tga",36,24},
    {"resource/csol/hud/v1/digits.tga",170,22},
    {"resource/csol/hud/v1/health.tga",16,16},
    {"resource/csol/hud/v1/armor.tga",16,16},
}};
struct WeaponArt { std::string title, weapon, ammo, ammo2; };
struct Catalog
{
    std::unordered_map<std::string, WeaponArt> weapons;
    std::unordered_map<unsigned, std::string> portraits;
};
Catalog ParseCatalog(std::string_view json);
std::vector<std::string> ResourcePaths(const Catalog& catalog);

// The meaning belongs to the weapon, not its icon or numeric ID range.
enum class AmmoKind { None, Magazine, Total };
struct WeaponState
{
    int id{}, slot{}, clip{-1}, reserve{}, secondary{}, total{};
    AmmoKind kind{AmmoKind::None};
    bool infinite{}, specialAmmo{};
};
struct Vitals
{
    int health{}, armor{};
    bool healthKnown{}, armorKnown{};
    bool Health(std::span<const std::byte> bytes);
    bool Armor(std::span<const std::byte> bytes);
};
struct Rect { float x{}, y{}, width{}, height{}; };
struct Layout
{
    float scale{};
    Rect character, portrait, weapon, card;
};
Layout MakeLayout(int width, int height, float userScale);
}
