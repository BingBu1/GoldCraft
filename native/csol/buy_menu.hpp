// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "character_menu.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace csol::buy {
inline constexpr char Message[]="CSOBuy";
inline constexpr std::size_t MessageBytes=16;
// GoldSrc retains downloads by path. Change this resource revision when the
// generated layout or required catalogue images change; the JSON schema is separate.
inline constexpr char PresentationResource[]="models/csol/buymenu_v11.json";
enum class Kind:std::uint8_t {Capability,Begin,Item,End,Result};
enum class Result:std::uint8_t {Available,Unavailable,Insufficient,Success,Failed};
struct Packet {Kind kind{};std::uint16_t request{},id{};std::int32_t price{},balance{};Result result{};std::uint8_t team{};};
std::array<std::uint8_t,MessageBytes> Encode(const Packet& packet);
bool Decode(std::span<const std::uint8_t> bytes,Packet& packet);
enum class WeaponSlot : std::uint8_t { ByCategory, Primary, Secondary, Melee };
struct Item {
    std::uint16_t id;int category;const char* alias;const char* name;const char* image;int extendedPrice;
    WeaponSlot slot{WeaponSlot::ByCategory};
};
// Purchase IDs share a namespace with extended weapons. A high weapon ID is
// not an equipment-array index (the two CSOL knife bridges use 1022/1023).
constexpr bool IsEquipment(int id) { return id >= 1001 && id <= 1005; }
constexpr bool IsAmmunition(int id) { return id == 2001 || id == 2002; }
constexpr bool IsWeapon(const Item& item) { return !IsEquipment(item.id) && !IsAmmunition(item.id); }
constexpr int RebuySlot(const Item& item)
{
    if (!IsWeapon(item)) return item.id == 1002 ? 1001 : int(item.id);
    if (item.slot == WeaponSlot::Melee) return 3;
    if (item.slot == WeaponSlot::Secondary) return 1;
    if (item.slot == WeaponSlot::Primary) return 2;
    return item.category == 1 ? 1 : 2;
}
constexpr int RebuyOrder(const Item& item)
{ return IsAmmunition(item.id) ? 2 : IsEquipment(item.id) ? 1 : 0; }
std::span<const Item> Catalog();
const Item* Find(int id);
struct WeaponInfoItem {
    unsigned itemId{};
    std::string name,description;
    std::optional<bool> table;
    std::array<std::string,10> values;
    std::array<int,3> grade{};
};
struct WeaponInfoPresentation {
    std::string priceLabel;
    std::array<std::string,10> attributeLabels;
    std::array<std::string,3> gradeLabels;
    MenuTexture background,divider;
    std::unordered_map<int,MenuTexture> grades;
    std::unordered_map<int,WeaponInfoItem> items;
    struct ScrollPresentation {
        MenuColors colors;
        std::array<MenuTexture,3> slider;
    } scroll;
};
struct Presentation {
    int width{},height{};
    bool normalizedLayout{};
    MenuRect root,listPanel,favoritePanel;
    std::string title,font;
    std::string listTitle,favoriteDescription;
    std::array<std::string,2> teamNames;
    float controlScale{0.625f};
    MenuRect info,previous,next,quit,account;
    MenuRect search{56,22,90,12},rebuy{219,281,150,25};
    std::array<MenuRect,2> teams{{{14,37,68,15},{82,37,70,15}}};
    std::array<MenuRect,8> favorites;
    std::array<MenuTexture,2> slotBackground;
    MenuTexture keyboard,favoriteBackground;
    std::array<MenuRect,10> slots;
    std::array<MenuTexture,9> frame;
    std::array<std::array<MenuTexture,3>,3> buttons;
    std::array<std::array<MenuTexture,3>,6> tabs;
    std::array<MenuTexture,3> close;
    MenuColors colors;
    std::unordered_map<int,MenuTexture> images;
    WeaponInfoPresentation weaponInfo;
};
using Favorites=std::array<std::uint16_t,8>;
Favorites ParseFavorites(std::string_view text);
std::string SerializeFavorites(const Favorites& favorites);
bool InTeamList(const Item& item,int team);
Presentation ParsePresentation(std::string_view json);
std::vector<std::string> Resources(const Presentation& presentation);
}
